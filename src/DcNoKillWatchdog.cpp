/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcNoKillWatchdog.h"

#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBossStandDown.h"
#include "Ai/Dungeon/DungeonClear/Util/DcNoKillDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPartyState.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunProgress.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"

#include <optional>
#include <string>
#include <vector>

#include "AiObjectContext.h"
#include "Log.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"

namespace
{
    // The limit is minutes; a sweep every five seconds is plenty and keeps the
    // cost to a handful of compares per clearing tank.
    constexpr uint32 DC_NO_KILL_SWEEP_MS = 5000;
    uint32 g_noKillSweepAccumMs = 0;
}

namespace DcNoKillWatchdog
{
    void Tick(uint32 diff)
    {
        g_noKillSweepAccumMs += diff;
        if (g_noKillSweepAccumMs < DC_NO_KILL_SWEEP_MS)
            return;
        g_noKillSweepAccumMs = 0;

        std::vector<ObjectGuid> const tanks = DcStatusPublisher::ActiveTanks();
        if (tanks.empty())
            return;

        uint32 const now = getMSTime();

        for (ObjectGuid guid : tanks)
        {
            Player* leader = ObjectAccessor::FindPlayer(guid);
            if (!leader || !leader->IsInWorld())
                continue;
            // Not gated on the tank being alive, like DcCombatPurge: a wipe the
            // party never recovers from is a stall too, and the rez recovery has
            // the whole window to answer it first.
            PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
            if (!leaderAI)
                continue;

            DcRunState& run = DcRun::Of(leaderAI);
            if (!run.enabled)
                continue;

            std::optional<DungeonBossInfo> const next =
                leaderAI->GetAiObjectContext()
                    ->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)
                    ->Get();

            DcNoKillDecision::Inputs in;
            in.nowMs = now;
            in.limitMs = DcSettings::GetUInt(leader, "NoKillTimeoutSecs") * 1000;
            in.killed = DcRunProgress::DetectKill(leader, leaderAI, run.killProgress);
            // The same holds the stranded and purge clocks stamp through: a pause
            // is the human's, a raid encounter is the playerbots strategy's fight,
            // and Karazhan's chess game is ten quiet minutes by design.
            in.held = run.paused || DcBossStandDown::IsActive(leader) || DcKarazhan::ChessIsOn(leader);
            // AI-REVIEW 2026-11-11 [Codex/in-process/runtime-trace] DC121-REV-002:
            // Readiness may skip corpses; this sweep is independent of that gate.
            // Inspect this registered owner's group without resolving a living
            // leader on the survivor's map. Released owners have no next boss
            // value, but their enabled run is still this exact AI's context.
            DcPartyState::PartyPresence const presence = DcPartyState::GetPartyPresence(leader);
            in.rosterAlive = presence.rosterAlive;
            in.rosterDead = presence.rosterDead;
            Player* const survivor = presence.soleLivingMember;
            Group* const group = leader->GetGroup();
            Map* const survivorMap = survivor ? survivor->GetMap() : nullptr;
            in.dungeonParty = group && !group->isRaidGroup() && survivorMap &&
                              survivorMap->IsDungeon() && !survivorMap->IsRaid();
            in.survivorCanResurrect = survivor && DcRezRecovery::KnownResurrectSpell(survivor);
            // Any boss still on the roster counts, on whatever map the tank stands:
            // a tank that has left the instance (a corpse run that never came
            // back) is a stalled run too. No next boss with a roster means the
            // dungeon is cleared, and DungeonClearAllClearedTrigger owns that
            // ending. A partial-wipe ending remains independent of this value:
            // a released owner outside the dungeon cannot calculate next boss.
            if (next.has_value())
                in.nextBoss = next->name;

            DcNoKillDecision::Result const verdict =
                DcNoKillDecision::Tick(run.killProgress.stampMs, in);
            if (!verdict.end)
                continue;

            LOG_INFO("playerbots.dungeonclear", "[DC:{}] run watchdog ended: {}",
                     leader->GetName(), verdict.reason);
            // ActiveTanks registered this exact enabled context. Do not retarget
            // another run through a same-map leader election after a release.
            DcActionShared::DisableDungeonClear(leaderAI, verdict.reason);
        }
    }
}
