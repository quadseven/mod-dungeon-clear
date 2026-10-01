/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcStrategyGate.h"
#include "DcModuleEnable.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"

#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/Util/DcFollowerLifecycle.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMovement.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    char const* const kNonCombat = "dungeon clear";
    char const* const kCombat    = "dungeon clear combat";

    // Strip-time cleanup. A bot that is losing the DC strategies must not carry
    // live run state past the triggers that owned it, except for a dead run owner
    // whose corpse run can return to the original instance:
    //   * a LEADER's `dungeon clear enabled` flag would otherwise survive on its
    //     value context and auto-resume the clear the next time it enters a
    //     dungeon and the strategy is re-installed. DisableDungeonClear resets the
    //     whole run (flags + approach/pull FSMs + long-path cache) in lockstep.
    //   * a FOLLOWER's persistent MoveFollow generator (installed by the
    //     follow-tank action) is never self-healed by a self-bot — see the
    //     selfbot-stale-movefollow note. Clear it explicitly, mirroring the
    //     follow-tank teardown tick.
    // Both are gated so the common case (a bot that never ran DC) does no work and
    // emits no addon chatter.
    void TeardownOnStrip(PlayerbotAI* botAI, Player* bot, bool preserveRunForCorpse)
    {
        AiObjectContext* ctx = botAI->GetAiObjectContext();

        if (preserveRunForCorpse && DcRun::Of(ctx).enabled)
        {
            // The strategies are map-local, but the run belongs to this owner.
            // Keep it while the owner corpse-runs so it can resume on re-entry.
            DcRun::Of(ctx).corpseRunHold = true;
            DcStatusPublisher::UnmarkActiveTank(bot->GetGUID());
        }
        else if (DcRun::Of(ctx).enabled)
            DcActionShared::DisableDungeonClear(
                botAI, "Left the dungeon \xe2\x80\x94 dungeon clear disabled.");

        ObjectGuid& followed =
            ctx->GetValue<ObjectGuid>(DcKey::FollowedTank)->RefGet();
        if (!followed.IsEmpty())
        {
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            followed = ObjectGuid::Empty;
            DcFollowerLifecycle::UnmarkFollowing(bot->GetGUID());
        }
    }
}

namespace DcStrategyGate
{
    void Reconcile(Player* bot)
    {
        // Master switch. With the module disabled nothing was ever registered
        // into the shared contexts, so there is no DC strategy to install — and
        // nothing to strip either, which is why this is a plain return rather
        // than a forced teardown. See DcModuleEnable.h.
        if (!DcModule::IsEnabled())
            return;

        if (!bot)
            return;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return;  // real player (no bot AI) — nothing to gate

        Map* map = bot->GetMap();
        bool const inDungeon = map && map->IsDungeon();
        DcRunState& run = DcRun::Of(botAI);
        uint32 const instanceId = bot->GetInstanceId();
        bool const matchingInstance =
            inDungeon && run.runInstanceId != 0 && run.runInstanceId == instanceId;
        RunStateAction const runAction = DecideRunState(
            inDungeon, bot->isDead(), run.enabled, run.corpseRunHold, matchingInstance);
        bool const preserveRunForCorpse =
            runAction == RunStateAction::PreserveForCorpse ||
            (runAction == RunStateAction::Keep && run.corpseRunHold && bot->isDead());
        bool const holdCorpseRunInside =
            run.corpseRunHold && bot->isDead() && matchingInstance;

        if (runAction == RunStateAction::Disable)
            DcActionShared::DisableDungeonClear(
                botAI, "Corpse run ended outside its dungeon instance.");
        else if (runAction == RunStateAction::PreserveForCorpse)
            run.corpseRunHold = true;
        else if (runAction == RunStateAction::Resume)
        {
            run.corpseRunHold = false;
            DcStatusPublisher::MarkActiveTank(bot->GetGUID());
        }

        // A released ghost can re-enter the instance before reaching its corpse.
        // Keep the ordinary corpse-run AI until it revives; reinstalling the DC
        // strategy ladder while dead would take movement away from that recovery.
        bool const gateInDungeon = inDungeon && !holdCorpseRunInside;

        bool const hasNon = botAI->HasStrategy(kNonCombat, BOT_STATE_NON_COMBAT);
        bool const hasCmb = botAI->HasStrategy(kCombat, BOT_STATE_COMBAT);

        // RAID consumables (raid-support Plan C): playerbots' opt-in `worldbuff`
        // strategy is the v1 consumable stand-in — a level-banded simulated
        // flask/food aura matrix (conf-shipped, AddAura-applied). Bots never
        // stock real flasks, so on raid maps the strategy is installed for
        // every bot and stripped again on any non-raid instance map, keeping
        // the simulated buffs a raid-run behavior rather than a global one. A
        // bot in the open world is left alone either way.
        bool const hasWorldbuff = botAI->HasStrategy("worldbuff", BOT_STATE_NON_COMBAT);
        if (inDungeon && map->IsRaid() && !hasWorldbuff)
            botAI->ChangeStrategy("+worldbuff", BOT_STATE_NON_COMBAT);
        else if (inDungeon && !map->IsRaid() && hasWorldbuff)
            botAI->ChangeStrategy("-worldbuff", BOT_STATE_NON_COMBAT);

        // Each strategy in the engine it does NOT belong to. Never correct; see
        // the Plan comment in the header for how a bot gets into that state and
        // why it is otherwise permanent.
        bool const strayInCmb = botAI->HasStrategy(kNonCombat, BOT_STATE_COMBAT);
        bool const strayInNon = botAI->HasStrategy(kCombat, BOT_STATE_NON_COMBAT);

        // Per-engine decision via the pure kernel. The two engines are installed
        // and stripped together, but each is checked independently so a partial
        // state (e.g. a reset that rebuilt only one engine) self-heals.
        Plan const plan = MakePlan(gateInDungeon, hasNon, hasCmb, strayInCmb, strayInNon);

        if (plan.nonCombat == Action::None && plan.combat == Action::None &&
            !plan.stripStrayInCombat && !plan.stripStrayInNonCombat)
            return;  // already compliant — the hot path

        // Run the strip cleanup once, before removing any strategy, so the run
        // state is torn down while its values/actions still exist.
        if (plan.teardown)
            TeardownOnStrip(botAI, bot, preserveRunForCorpse);

        switch (plan.nonCombat)
        {
            case Action::Install: botAI->ChangeStrategy("+dungeon clear", BOT_STATE_NON_COMBAT); break;
            case Action::Strip:   botAI->ChangeStrategy("-dungeon clear", BOT_STATE_NON_COMBAT); break;
            case Action::None:    break;
        }
        switch (plan.combat)
        {
            case Action::Install: botAI->ChangeStrategy("+dungeon clear combat", BOT_STATE_COMBAT); break;
            case Action::Strip:   botAI->ChangeStrategy("-dungeon clear combat", BOT_STATE_COMBAT); break;
            case Action::None:    break;
        }

        if (plan.stripStrayInCombat)
            botAI->ChangeStrategy("-dungeon clear", BOT_STATE_COMBAT);
        if (plan.stripStrayInNonCombat)
            botAI->ChangeStrategy("-dungeon clear combat", BOT_STATE_NON_COMBAT);
    }

    void ReconcileAllBots()
    {
        if (!DcModule::IsEnabled())
            return;  // nothing registered -> nothing to reconcile; skip the walk

        // Iterate every online player and reconcile the ones that are bots.
        // Reconcile() no-ops on real players (no bot AI) and on already-compliant
        // bots, so this is cheap. Runs on the world thread inside World::Update,
        // the same thread that adds/removes players, so the container is stable
        // for the duration of the loop.
        for (auto const& kv : ObjectAccessor::GetPlayers())
            Reconcile(kv.second);
    }
}
