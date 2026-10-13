/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcAtBoss.h"

#include <string>

#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/SealedEncounterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcCombatFlag.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLogThrottle.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRaidMuster.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Playerbots.h"

namespace
{
    // True while the sealed-encounter muster holds the boss engage. Stamps and
    // re-arms DcRunState::sealedMusterSince as a side effect, idempotently within
    // a tick (the trigger and the engage hold both evaluate the gate).
    bool SealedMusterHolds(Player* bot, AiObjectContext* context, DungeonBossInfo const& next)
    {
        // SEALED ENCOUNTER MUSTER. This boss's room locks the instant the encounter
        // starts - an InstanceScript DOOR_TYPE_ROOM door, held `open &= (state !=
        // IN_PROGRESS)` for the whole fight (SealedEncounterRegistry). Engage with anyone
        // still outside and they spend the entire fight at a closed door while the tank
        // solos it. Reported live for Selin Fireheart, whose Assembly Chamber Door
        // (188065) hangs at (215.1, 0.4).
        //
        // The test is the VOLUME, not a radius around the tank, and the difference
        // matters: the tank crosses the threshold BEFORE it engages, so a follower a
        // tolerable 10yd behind it is several yards on the wrong side of the door. Only
        // "is this member inside the room" answers the actual question.
        //
        // The clump that makes this satisfiable in a second or two - rather than a long
        // wait parked inside the boss's aggro radius - is the tank-anchored spread
        // override in DcPartyState::GetSpreadGate, which arms over the same
        // approachRadius. This is the guarantee; that is the thing that makes the
        // guarantee cheap.
        if (SealedEncounterRow const* const sealed =
                SealedEncounterRegistry::Find(bot->GetMapId(), next.entry))
        {
            if (SealedEncounterRegistry::InApproachRange(
                    *sealed, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                    next.x, next.y, next.z))
            {
                DcRunState& run = DcRun::Of(context);
                uint32 const now = getMSTime();
                if (!run.sealedMusterSince)
                    run.sealedMusterSince = now;

                // RAID quorum: with 10-40 members, one wedged straggler outside must
                // not hold the whole raid inside the boss's aggro radius for the full
                // timeout. A raid holds only while an outside member is a tank/healer
                // or the outside share exceeds the quorum's tolerance; a dungeon
                // still holds for literally anyone outside (5 people, one door - the
                // strict contract is cheap and right there).
                bool const raidQuorum = bot->GetMap() && bot->GetMap()->IsRaid();
                uint32 const quorumPct =
                    raidQuorum ? DcSettings::GetUInt(bot, "RaidReadyQuorumPct") : 100;
                uint32 living = 0;
                uint32 outsideCount = 0;
                bool outsideCritical = false;
                Player* outside = nullptr;
                if (Group* group = bot->GetGroup())
                {
                    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* member = ref->GetSource();
                        if (!member || member == bot || member->isDead())
                            continue;
                        if (member->GetMapId() != bot->GetMapId())
                            continue;
                        if (!GET_PLAYERBOT_AI(member))
                            continue;   // a real player is never gated on
                        ++living;
                        if (!SealedEncounterRegistry::InSealedRoom(
                                *sealed, member->GetPositionX(), member->GetPositionY()))
                        {
                            ++outsideCount;
                            if (!outside)
                                outside = member;
                            if (PlayerbotAI::IsTank(member) || PlayerbotAI::IsHeal(member))
                                outsideCritical = true;
                        }
                    }
                }
                if (raidQuorum && outside && !outsideCritical && living &&
                    (living - outsideCount) * 100 >= living * quorumPct)
                    outside = nullptr;  // quorum inside, stragglers tolerated

                // Bounded: a member that cannot path in (stuck, mid-rez, feared out)
                // must not hold the run open. On expiry engage anyway and say so - that
                // member is about to miss the fight and the log should not be silent
                // about why.
                if (outside && (now - run.sealedMusterSince) < DC_SEALED_MUSTER_TIMEOUT_MS)
                {
                    DC_PULL_TRACE("[DC:{}] sealed encounter: holding the boss engage - {} "
                                  "is still outside the room ({} ms of {})",
                                  bot->GetName(), outside->GetName(),
                                  now - run.sealedMusterSince, DC_SEALED_MUSTER_TIMEOUT_MS);
                    return true;
                }
                if (outside)
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC:{}] sealed encounter: muster timed out after {} ms with "
                             "{} still outside - engaging anyway; that member will be "
                             "locked out of the fight",
                             bot->GetName(), now - run.sealedMusterSince, outside->GetName());
            }
            else
            {
                DcRun::Of(context).sealedMusterSince = 0;   // left the approach; re-arm
            }
        }
        return false;
    }
}

namespace DcAtBoss
{
    DcAtBossGate::Verdict Evaluate(Player* bot, PlayerbotAI* botAI, AiObjectContext* context,
                                   DungeonBossInfo const& next)
    {
        DcAtBossGate::Inputs in;
        if (!bot || !context)
            return DcAtBossGate::Decide(in);

        Map* const map = bot->GetMap();
        DcApproachState& appr =
            context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();
        in.nowMs = getMSTime();
        in.heldSinceMs = appr.atBossHeldSinceMs;
        in.valveMs = DC_AT_BOSS_HOLD_VALVE_MS;
        in.raid = map && map->IsRaid();

        // The drive gate DungeonClearAtBossTrigger::IsActive used to test inline
        // (MayDrive). It is the first gate Decide asks, and the valve never opens
        // past it (EveryGateTheTriggerTestedStillRefusesTheEngage).
        in.driving = !bot->isDead() && map && map->IsDungeon() &&
                     DcCombatFlag::MayDrive(bot, context);

        // Travel objectives are not combat targets - the at-objective trigger owns
        // arrival. Stand down so engage-boss never fires on a non-creature anchor.
        in.bossAnchor = next.kind == DungeonAnchorKind::Boss;

        // Blackwing Lair, phase 1: the anchor behind us (Grethok) has cleared and
        // the next boss is a POSSESSED Razorgore our own runner is walking egg to
        // egg. Everything this rung leads to is wrong for that boss: the raid
        // muster it hosts would sit the raid down to eat and rebuff mid-egg-run
        // (live: "raid muster: (rest timed out) rebuff round for Razorgore the
        // Untamed", 23:15:32), and the engage behind it would pull the one boss in
        // the game whose death here casts 20038 on all forty of them. Stand down for
        // the whole egg run; the camp rung holds the raid meanwhile, and this reopens
        // within ~3s of the last egg. See DcBlackwingLair::EggRunHoldsTheRaid.
        in.encounterHold = in.bossAnchor && DcBlackwingLair::EggRunHoldsTheRaid(bot);

        // Mid-maneuver the pull owns the tank - same window, same bound, same reason as
        // the idle rung (DungeonClearIdleTrigger). A trash pull taken within engage
        // range of the boss must not have the boss engage fire underneath it while its
        // own trigger is quiet; that is how a pull one room short of a boss turns into
        // the boss pull.
        // And unconditionally while a scripted row is latched, for the reason spelled out
        // on the idle rung: the plan owns the tank, and for THIS rung the destination it
        // would drive to is the very boss the plan is peeling trash away from.
        DcPullContext const& pullCtx =
            context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
        in.pullOwns = pullCtx.scriptedStage >= 0 ||
                      (pullCtx.phase != DcPullPhase::Idle &&
                       getMSTimeDiff(pullCtx.phaseSince, in.nowMs) < DC_PULL_ADVANCE_STANDDOWN_MAX_MS);

        // Close enough AND on the boss's own floor (not just 3D-near while passing
        // under an upper-floor boss). See IsAtBossEngage.
        in.atEngage = in.driving && in.bossAnchor &&
                      DcTickMemoAccess::AtBossEngage(bot, context, next);

        // Everything below is asked only at the boss: out of range the verdict is
        // NotAtEngage whatever it would say, and these are the costly reads.
        if (in.atEngage && !in.encounterHold && !in.pullOwns)
        {
            Creature* const liveBoss = DcTargeting::GetLiveBoss(bot, context, next.entry);
            in.liveBoss = liveBoss && liveBoss->IsAlive();

            // Room-wide-aggro boss (RoomAggroRegistry): on engage it force-pulls the
            // whole room, so HOLD the boss pull while any room trash remains. The
            // room-clear driver (Off/Leeroy) or the pull pipeline (advanced/dynamic)
            // clears it first; the gate reopens the instant the room is clear (or the
            // RoomClearTimeout valve fires inside the value). Cheap cached read.
            in.roomTrash = DcTargeting::ActiveBossRoom(bot, context, next.entry) &&
                           !context->GetValue<GuidVector>(DcKey::RoomTrashRemaining)->Get().empty();

            // A closed door between us and the boss means it's BEYOND it and not actually
            // reachable yet - even when it's within straight-line engage range (a boss or
            // its pack right behind the door). Without this, IsAtBossEngage (a pure
            // distance+floor test, door-blind) fires the at-boss engage, which outranks
            // door-blocked (30 vs 22) and bee-lines the tank onto/through the door. Stand
            // down so door-blocked parks at its stand-off; re-evaluates the instant the
            // door opens. Checked fresh, not via the 500ms-cached blocking-door value,
            // which can still read empty the moment the boss first comes into range.
            float const bx = liveBoss ? liveBoss->GetPositionX() : next.x;
            float const by = liveBoss ? liveBoss->GetPositionY() : next.y;
            float const bz = liveBoss ? liveBoss->GetPositionZ() : next.z;
            //
            // EXCEPT an interact-through GATE (DcEventDoorRegistry::IsNavigationIgnored).
            // The ray is entry-blind, and for that handful of doors standing down is the
            // exact wrong answer: nothing DC does opens them, the objective behind one is
            // performed from THIS side, and the rung this stand-down hands the tick to
            // cannot open it either. Blackwing Lair's Chromaggus cage is the deadlock -
            // the portcullis sits on the tank->boss line, so this returned false, the
            // raid muster (ticked at the bottom of this function) never staged, and
            // ChromaggusCageDue waits on muster Ready, so the lever that opens the cage
            // was never pulled. tr-20260829-204120-2 idled 30yd out to teardown with
            // every watchdog clear and zero "raid muster: staging at Chromaggus" lines.
            // The boss-engage rung holds the tank on the anchor meanwhile (it reads
            // `caged`), so letting this through does not walk anyone into the gate.
            //
            // AND ONLY A DOOR. The ray is GameObject LOS, which any GameObject with
            // collision blocks; with no shut door from the door index on the line,
            // nothing door-blocked can work on either, and the old veto parked the
            // tank at the boss for good (Blackfathom Deeps, Gelihast: "holding for
            // at-boss ... partyReady=1" 1,911 times in five minutes, 2026-10-12).
            // See DcAtBossGate::DoorVetoes.
            if (DcEngageGeometry::ClosedDoorBetween(bot, bx, by, bz))
            {
                uint32 shutDoors = 0;
                uint32 eventGates = 0;
                DcEngageGeometry::CountShutDoorsOnChord(bot, bx, by, bz, shutDoors, eventGates);
                in.closedDoor = DcAtBossGate::DoorVetoes(true, shutDoors, eventGates);
            }

            // When the long-path cache is anchored (registered route), make sure the
            // intermediate anchors STILL AHEAD OF US have been resolved before firing.
            // This prevents the bot from "engaging" a boss it's geometrically near but
            // separated from by a wall or door - the cached anchor list runs the bot
            // around through the actual corridor first.
            //
            // FROM THE FOLLOWER'S CURSOR, NOT FROM ZERO. This loop used to start at
            // segment 0, which asks the bot to be within one arriveRadius (6yd) of every
            // anchor on the route SIMULTANEOUSLY - satisfiable only by a route whose
            // anchors all sit inside a 6yd bubble, and false forever for any real one.
            // It went unnoticed because DungeonClearRouteRegistry had no rows until
            // Azjol-Nerub's; the first registered route (12 anchors over 250yd) turned
            // it into a hard stall - 19 of 20 runs in tp-20260818-200553-1 parked 29yd
            // from Anub'arak at full health, out of combat, watchdogs clear, until the
            // no-progress watchdog ended them.
            //
            // DcAdvanceAction used to carry its own copy of this check (its atBoss
            // handoff, the one that logs "holding for at-boss"), and the two disagreeing
            // is precisely why Advance said it was done navigating while the trigger
            // never fired. There is one copy now, here, and both read it.
            ChunkedPathfinder::Result const& path =
                context->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Get();
            if (path.reachable && !path.segments.empty())
            {
                DungeonFollowerState const& follower =
                    context->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get();
                // Last segment is the boss anchor; the anchored ones between the cursor
                // and it must be within their arriveRadius. Anchors already walked past
                // don't gate the engage.
                for (size_t i = follower.segmentIdx; i + 1 < path.segments.size(); ++i)
                {
                    PathSegment const& seg = path.segments[i];
                    if (seg.anchored && bot->GetDistance(seg.ex, seg.ey, seg.ez) > seg.arriveRadius)
                    {
                        in.anchoredHops = true;
                        break;
                    }
                }
            }

            // The sealed muster stamps its own clock, so it is asked only once the
            // tank is truly at the room (no door, no route still ahead), as before.
            if (!in.closedDoor && !in.anchoredHops)
                in.sealedHolds = SealedMusterHolds(bot, context, next);

            // RAID BOSS: THE PRE-BOSS MUSTER IS THE GATE.
            //
            // Plan C's muster (stage -> top off -> rebuff -> release, every phase and the
            // whole thing timeout-bounded) used to be hosted only in the engage action
            // BEHIND this trigger, with IsBetweenPullsReady below as the gate. That is a
            // loop the muster cannot get out of: it pushes RestHealthPct/RestManaPct to
            // 100 for the run so bots eat and drink to the bars, IsPartyReady is strict
            // on every tank and healer, and a priest single-target buffing thirty-nine
            // people is a healer who is never at full mana. The gate stayed down, the
            // engage action never ran, and every budget the muster owns - which is only
            // ever compared against getMSTime() on a tick that EVALUATES it - was never
            // sampled. Live at Vaelastrasz (2026-08-28, five 40-mans, 60s ceiling): the
            // kernel was asked three times in 126s and released 66s late; the siblings
            // ran 77s, 89s, 116s and 218s. Nothing was wrong with DcRaidMusterDecision.
            //
            // So tick it HERE, every tick, and gate on its verdict. It subsumes what
            // IsBetweenPullsReady was doing at a raid boss and does it better: `staged`
            // is the same PartyMaxSpread walk, `topped` is a stricter HP/mana bar than
            // the rest floors, and unlike the readiness gate it hands itself forward on a
            // clock instead of waiting forever. A pending party rez still outranks it -
            // releasing forty players onto a boss over a corpse the elected rezzer is
            // already working is never the lesser evil. (DcPartyState::GetRestGate also
            // stops feeding the muster's own override back into the general readiness
            // gate, so advance and the pull rungs no longer inherit a 100/100 bar either.)
            //
            // The engage action still calls the same helper as the action-side half of
            // this guard, the pairing used throughout this module; by then the phase
            // reads Ready and it passes straight through.
            if (in.raid)
            {
                in.rezPending = DcRezRecovery::IsPending(bot);
                // Ticked only once every gate ahead of it is open, as before: the
                // muster sits the raid down, which is wrong while it cannot pull.
                if (!in.rezPending && !in.roomTrash && !in.closedDoor &&
                    !in.anchoredHops && !in.sealedHolds)
                    in.raidMusterHolds = DcRaidMuster::Holds(bot, botAI, context, next);
            }
            else
            {
                // Don't pull while party is still recovering or tank-side loot is pending.
                // The idle-trigger's advance action holds the tank in place meanwhile.
                in.partyReady =
                    DcTickMemoAccess::BetweenPullsReady(bot, context, /*requireNoLoot*/ true);
            }

            in.lootPending =
                context->GetValue<bool>(DcKey::Stock::HasAvailableLoot)->Get() ||
                context->GetValue<bool>(DcKey::Stock::CanLoot)->Get();
        }

        DcAtBossGate::Verdict const v = DcAtBossGate::Decide(in);
        appr.atBossHeldSinceMs = v.heldSinceMs;

        // SAY WHICH GATE IS DOWN. Only at the boss (out of range is just travel),
        // on a change, and once per interval while it stays the same: the hold this
        // answers for is written every tick and filled the log in minutes.
        // The valve line is its own key, so it goes out even when the gate it
        // opened past was the one logged last.
        uint8 const gate = static_cast<uint8>(v.gate) | (v.valveOpen ? 0x80 : 0x00);
        if (in.atEngage && (v.gate != DcAtBossGate::Gate::Open || v.valveOpen) &&
            DcLogThrottle::ShouldLog(appr.atBossGateLogMs == 0, gate != appr.atBossGateLogged,
                                     getMSTimeDiff(appr.atBossGateLogMs, in.nowMs),
                                     DC_AT_BOSS_GATE_LOG_INTERVAL_MS))
        {
            if (v.valveOpen)
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] at-boss valve: held {} ms at {} on gate '{}' with the "
                         "party ready and no loot pending -> engaging anyway",
                         bot->GetName(), v.heldMs, next.name, DcAtBossGate::Name(v.gate));
            else
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] at-boss gate down at {}: '{}' [live={} partyReady={} "
                         "loot={} valve={}/{} ms]",
                         bot->GetName(), next.name, DcAtBossGate::Name(v.gate),
                         in.liveBoss ? 1 : 0, in.partyReady ? 1 : 0, in.lootPending ? 1 : 0,
                         v.heldMs, in.valveMs);
            appr.atBossGateLogged = gate;
            appr.atBossGateLogMs = in.nowMs != 0 ? in.nowMs : 1u;
        }
        else if (!in.atEngage)
        {
            appr.atBossGateLogged = 0;
            appr.atBossGateLogMs = 0;
        }
        return v;
    }
}
