/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include <string>
#include <vector>

#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSplineInitArgs.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"
#include "Ai/Dungeon/DungeonClear/DcRunState.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMovement.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPoolExitDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRawPathDrive.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"

// --- Blackfathom Deeps (map 48) - the FIRES OF AKU'MAI, ANCHORED + PERSISTENT ---
//
// The door to Aku'mai's lair (Portal of Aku'Mai, GO 21117, lock 85 = no lock)
// is opened by instance_blackfathom_deeps and nothing else. Its OnUnitDeath
// opens the portal when BOTH hold:
//
//   * TYPE_FIRE1..TYPE_FIRE4 are all DONE. Each Fire of Aku'mai (GO 21118 to
//     21121, GAMEOBJECT_TYPE_BUTTON, SmartGameObjectAI) sets its own TYPE_FIREn
//     to DONE on SMART_EVENT_GOSSIP_HELLO with filter 1, the plain Use() path
//     (not report-use), and in the same breath summons its creature_summon_groups
//     group 1: four Aku'mai Snapjaws (4825), four Murkshallow Softshells (4977),
//     four Barbed Crustaceans (4823) or two Aku'mai Servants (4978), at the
//     platform's east and west edges (x -862 and -775, z -25.9). Every one of
//     them "Is Summoned - Set In Combat With Zone", so the wave comes for the
//     party at once.
//   * The summon counter is back to zero. OnCreatureCreate counts every SUMMON
//     of those four entries and OnUnitDeath counts them down; the portal opens
//     on the death that brings it to zero after the fourth fire is lit.
//
// So a party opens the door the way players do: light a fire, kill its wave,
// light the next. The fires are buttons with autoCloseTime 0, so a lit fire
// stays GO_STATE_ACTIVE and UseGO's idempotence (skip a non-READY GO) holds.
// That matters: the SmartAI runs on EVERY Use(), so a second click on a lit
// fire would summon a second wave. One fire at a time also keeps the party
// facing one wave, not four.
//
// The wave gate is a ClearRadius filtered to the four wave entries, centred on
// the platform with a 48yd radius and an 8yd floor band. An entry-only
// KillCreature would wait forever: the map has STATIC Snapjaws behind the
// still-shut portal (y -215 to -253, 56yd from the centre) and a static Barbed
// Crustacean in the pool below (z -37). The radius stops short of the first and
// the band leaves out the second; every summon point is 44 to 45yd from the
// centre at platform height.
//
// Twilight Lord Kelris (4832) stands among the fires, so the objective is
// ordered right after him. Old Serra'kis (4830, bit 5) swims in the pool PAST
// the portal; the encounter-index order sent the tank to him straight after
// Lorgus Jett, at a door nothing would open, which is where the live runs sat
// out their two hours. Aku'mai (4829, bit 7) is the finder's finish
// (instance_encounters.lastEncounterDungeon), so he is ordered before
// Old Serra'kis: the run is over at Aku'mai, and Old Serra'kis is a swimming
// boss in deep water, where the party would otherwise fight underwater.

// Old Serra'kis's water also needs a way out. Kelris is reached along a walkway
// over the Forgotten Pool, and a bot that falls off lands on mesh not linked to
// it, so every route rebuilt from the water dead-ends under Kelris. Event 8
// (upstream's, unchanged) climbs the tank out by the ramp at the pool's north
// end: Detour's floor corridor swum 1.5yd off the floor, then up the ramp.
// Conditional and Repeatable, so a second fall re-arms it.
//
// Upstream's 2026-10-26 Blackfathom work also authored a jump puzzle, a swim to
// Lady Sarevess, its own fires event and a roster without Old Serra'kis. This
// branch keeps the production fires, roster and swim route for those, as it
// keeps production behavior wherever both lines fixed the same thing. The
// party-wide chain kernels they use are merged and tested, and unused here.

namespace
{
    constexpr uint32 BFD_MAP = 48;

    constexpr uint32 BFD_FIRE_1 = 21118;
    constexpr uint32 BFD_FIRE_2 = 21119;
    constexpr uint32 BFD_FIRE_3 = 21120;
    constexpr uint32 BFD_FIRE_4 = 21121;
    constexpr uint32 BFD_PORTAL_OF_AKUMAI = 21117;

    constexpr uint32 BFD_AKUMAI_SNAPJAW = 4825;
    constexpr uint32 BFD_MURKSHALLOW_SOFTSHELL = 4977;
    constexpr uint32 BFD_BARBED_CRUSTACEAN = 4823;
    constexpr uint32 BFD_AKUMAI_SERVANT = 4978;

    constexpr uint32 BFD_SERRAKIS = 4830;
    constexpr uint32 BFD_AKUMAI = 4829;

    // Platform centre: the mean of the four fires' x/y, at Kelris's z.
    constexpr float BFD_FIRES_X = -818.72f;
    constexpr float BFD_FIRES_Y = -164.48f;
    constexpr float BFD_FIRES_Z = -25.79f;
    constexpr float BFD_WAVE_RADIUS = 48.0f;
    constexpr float BFD_WAVE_ZBAND = 8.0f;
    // A fire is 5 to 7yd from the centre; 30yd finds it from anywhere the
    // objective's arrival puts the tank.
    constexpr float BFD_FIRE_SEARCH = 30.0f;
    constexpr uint32 BFD_WAVE_TIMEOUT = 180000;
    // The portal is 36yd from the centre and opens on the wave's last death.
    constexpr float BFD_PORTAL_SEARCH = 60.0f;
    constexpr uint32 BFD_PORTAL_TIMEOUT = 30000;

    // Clear one fire's wave before the next fire is lit.
    EventBuilder& LightFire(EventBuilder& b, uint32 fire, uint32 wave)
    {
        return b.UseGO(fire, BFD_FIRE_SEARCH)
            .ClearRadius(BFD_FIRES_X, BFD_FIRES_Y, BFD_FIRES_Z, BFD_WAVE_RADIUS, BFD_WAVE_ZBAND)
            .OnlyEntries({ wave })
            .Timeout(BFD_WAVE_TIMEOUT);
    }

    // --- 4. the Forgotten Pool exit -----------------------------------------
    // The box is the pool floor island's (#1) bbox; Aku'mai's lake (y < -250)
    // is outside it. The surface is the #7 sheet (-31..-30). The walkway's
    // north end is ankle-deep water AT the surface (-31.3), which the 3yd depth
    // gate keeps out; elsewhere the walkway is above the surface.
    constexpr float BFD_FPOOL_MIN_X = -872.0f, BFD_FPOOL_MAX_X = -679.0f;
    constexpr float BFD_FPOOL_MIN_Y = -194.0f, BFD_FPOOL_MAX_Y = 5.0f;
    constexpr float BFD_FPOOL_SURFACE_Z = -30.9f;
    constexpr float BFD_FPOOL_MIN_DEPTH = 3.0f;
    // Where Detour's floor corridor ends at the foot of the ramp (#1, offline
    // pathq from three fall spots), and the top of the ramp (#0, user .gps).
    G3D::Vector3 const BFD_FPOOL_RAMP_FOOT(-746.7f, -10.9f, -35.2f);
    G3D::Vector3 const BFD_FPOOL_RAMP_TOP(-740.39f, -2.38f, -31.28f);
    // Wedges before the climb gives up and stalls the run.
    constexpr uint32 BFD_FPOOL_MAX_WEDGES = 3;

    // The floor under a bot: the first collision BELOW it (VMAP ignores liquid).
    float FloorUnder(Player* bot)
    {
        return bot->GetMapHeight(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() + 1.0f);
    }

    bool InForgottenPool(Player* bot)
    {
        DcPoolExit::Pool const& pool = DcBlackfathomDeeps::ForgottenPool();
        // The box first: this runs on every tick of every BFD leader.
        if (!DcPoolExit::InBox(pool, bot->GetPositionX(), bot->GetPositionY()))
            return false;
        float const floorZ = FloorUnder(bot);
        if (floorZ <= INVALID_HEIGHT)
            return false;
        return DcPoolExit::InPool(pool, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), floorZ);
    }

    bool ForgottenPoolExitDue(Player* bot, AiObjectContext* /*context*/)
    {
        return bot && bot->GetMapId() == 48 && bot->IsAlive() && InForgottenPool(bot);
    }

    // Detour's corridor from the floor under the bot to the ramp foot. The start
    // is the FLOOR, not the bot: a swimmer's own z snaps to the #7 surface
    // sheet, whose corridor stops at (-783.7,-133.6), short of the ramp.
    std::vector<G3D::Vector3> FloorCorridor(Player* bot, float floorZ)
    {
        Map* map = bot->GetMap();
        dtNavMesh const* mesh = map ? map->GetMapCollisionData().GetMMapData().GetNavMesh() : nullptr;
        if (!mesh)
            return {};
        LongRangePathfinder::RawResult const raw = LongRangePathfinder::BuildCoreFromMesh(
            mesh, 48, bot->GetPositionX(), bot->GetPositionY(), floorZ,
            BFD_FPOOL_RAMP_FOOT.x, BFD_FPOOL_RAMP_FOOT.y, BFD_FPOOL_RAMP_FOOT.z);
        if (!raw.reachable)
            return {};
        return raw.rawPts;
    }
}

DcPoolExit::Pool const& DcBlackfathomDeeps::ForgottenPool()
{
    static DcPoolExit::Pool const pool{BFD_FPOOL_MIN_X, BFD_FPOOL_MAX_X, BFD_FPOOL_MIN_Y, BFD_FPOOL_MAX_Y,
                                       BFD_FPOOL_SURFACE_Z, BFD_FPOOL_MIN_DEPTH,
                                       BFD_FPOOL_RAMP_FOOT, BFD_FPOOL_RAMP_TOP,
                                       /*doneRadius*/ 3.0f, /*floorLift*/ 1.5f};
    return pool;
}

namespace
{
    // The climb out, one tick at a time: build the leg on the first tick, then
    // drive it with the shared raw-path cursor (the authored RawPath's rules).
    // Done the moment the bot is out of the pool — the ramp top, a teleport, a
    // rez — and Blocked (a stall naming the problem) after repeated wedges.
    ObjectiveArriveResult DriveForgottenPoolExit(Player* bot, AiObjectContext* context,
                                                 DungeonBossInfo const& /*info*/)
    {
        using namespace DcRawPathDrive;
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        if (!botAI || !context || bot->GetMapId() != 48)
            return ObjectiveArriveResult::Done;

        DcRunState& run = DcRun::Of(context);
        DcPoolExit::Pool const& pool = DcBlackfathomDeeps::ForgottenPool();
        G3D::Vector3 const pos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

        if (!InForgottenPool(bot))
        {
            if (!run.poolExitLeg.empty())
            {
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] forgotten pool: out of the water at ({:.1f},{:.1f},{:.1f}) -> back to the clear",
                         bot->GetName(), pos.x, pos.y, pos.z);
                DcMovement::ResolveEscortConflict(bot);
            }
            run.ClearPoolExit();
            return ObjectiveArriveResult::Done;
        }

        uint32 const now = getMSTime();
        if (run.poolExitLeg.empty())
        {
            std::vector<G3D::Vector3> const corridor = FloorCorridor(bot, FloorUnder(bot));
            run.poolExitLeg = DcPoolExit::BuildLeg(pool, corridor);
            run.poolExitCursor = 0;
            run.poolExitWatch.Reset();
            ++run.poolExits;
            run.SetTestExtra("poolExits", std::to_string(run.poolExits), /*numeric*/ true);
            DcMovement::ResolveEscortConflict(bot);
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] forgotten pool: fell in at ({:.1f},{:.1f},{:.1f}) -> swimming {} pts "
                     "({}) to the ramp (exit {} this run)",
                     bot->GetName(), pos.x, pos.y, pos.z, run.poolExitLeg.size(),
                     corridor.empty() ? "no floor corridor, straight" : "floor corridor", run.poolExits);
        }

        MotionMaster* mm = bot->GetMotionMaster();
        bool const splineRunning =
            mm && mm->GetCurrentMovementGeneratorType() == ESCORT_MOTION_TYPE && bot->isMoving();
        Decision const d = Step(run.poolExitLeg, run.poolExitCursor, pos, pool.doneRadius, splineRunning, now,
                                run.poolExitWatch);
        run.poolExitCursor = d.cursor;
        switch (d.verdict)
        {
            case Verdict::Arrived:
                LOG_INFO("playerbots.dungeonclear", "[DC:{}] forgotten pool: up the ramp -> back to the clear",
                         bot->GetName());
                DcMovement::ResolveEscortConflict(bot);
                run.ClearPoolExit();
                return ObjectiveArriveResult::Done;
            case Verdict::InFlight:
                return ObjectiveArriveResult::Running;
            case Verdict::OffLeg:
                // Knocked or dragged off the leg (a fight in the water): rebuild
                // from where the bot is now.
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] forgotten pool: {:.0f}yd off the leg -> rebuilding it",
                         bot->GetName(), (pos - run.poolExitLeg[run.poolExitCursor]).length());
                run.poolExitLeg.clear();
                return ObjectiveArriveResult::Running;
            case Verdict::Wedged:
                ++run.poolExitWedges;
                run.poolExitWatch.Reset();
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] forgotten pool: no progress toward point {} (wedge {} of {})",
                         bot->GetName(), run.poolExitCursor, run.poolExitWedges, BFD_FPOOL_MAX_WEDGES);
                if (run.poolExitWedges >= BFD_FPOOL_MAX_WEDGES)
                {
                    run.ClearPoolExit();
                    return ObjectiveArriveResult::Blocked;
                }
                DcMovement::ResolveEscortConflict(bot);
                [[fallthrough]];
            case Verdict::Issue:
                break;
        }

        // Spline window from the cursor, Z verbatim (submerged; no navmesh snap).
        Movement::PointsArray pts;
        pts.push_back(pos);
        for (size_t i = run.poolExitCursor;
             i < run.poolExitLeg.size() && pts.size() < DungeonPathFollower::MAX_SPLINE_WINDOW_POINTS; ++i)
            pts.push_back(run.poolExitLeg[i]);
        DcMovement::SplinePath(botAI, pts);
        return ObjectiveArriveResult::Running;
    }
}

void RegisterBlackfathomDeepsEvents(std::vector<DungeonEvent>& out)
{
    EventBuilder b(BFD_MAP, 1, "Light the Fires of Aku'mai");
    b.Anchored(/*orderIndex, doc-only*/ 7);
    LightFire(b, BFD_FIRE_1, BFD_AKUMAI_SNAPJAW);
    LightFire(b, BFD_FIRE_2, BFD_MURKSHALLOW_SOFTSHELL);
    LightFire(b, BFD_FIRE_3, BFD_BARBED_CRUSTACEAN);
    LightFire(b, BFD_FIRE_4, BFD_AKUMAI_SERVANT);
    // GO_STATE_ACTIVE (0): the portal is open.
    b.WaitForGOState(BFD_PORTAL_OF_AKUMAI, /*GO_STATE_ACTIVE*/ 0, BFD_PORTAL_TIMEOUT,
                     BFD_PORTAL_SEARCH);
    // Persistent: each wave is a fight, and a non-persistent anchored event
    // rewinds to step 0 after any gap in its drive. The fires' idempotence
    // would make that rewind harmless, but there is no reason to re-walk it.
    b.Persistent();
    out.push_back(b.Build());

    // --- 4. Climb out of the Forgotten Pool (conditional, event 8) -----------
    // Fires whenever the tank is in the pool; the condition going false (up the
    // ramp) is the only "done", so a second fall re-arms it. Steps own movement
    // (the hook issues its own spline). PanelAfterBoss, never PanelBeforeBoss:
    // a Repeatable event never latches, so a BeforeBoss gate would hold the
    // pull at Kelris forever ([[dc-panelbeforeboss-repeatable-permanent-hold]]).
    out.push_back(
        EventBuilder(48, DcBlackfathomDeeps::EVENT_POOL_EXIT, "Climb out of the Forgotten Pool")
            .Conditional(&ForgottenPoolExitDue)
            .Repeatable()
            .StepsOwnMovement()
            .PanelAfterBoss(DcBlackfathomDeeps::NPC_LORGUS_JETT)
            .Custom(DcBlackfathomDeeps::HOOK_POOL_EXIT)
                .Timeout(DcBlackfathomDeeps::POOL_EXIT_TIMEOUT_MS)
            .Build());
}

void RegisterBlackfathomDeepsHooks(ObjectiveHookRegistry::HookTable& out)
{
    ObjectiveHookRegistry::AddHook(out, DcBlackfathomDeeps::HOOK_POOL_EXIT, &DriveForgottenPoolExit);
}


void RegisterBlackfathomDeepsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // Auto-derived order (DungeonEncounter bits): Ghamoo-ra 0, Lady Sarevess 1,
    // Gelihast 2, Lorgus Jett 3, Old Serra'kis 5, Twilight Lord Kelris 6,
    // Aku'mai 7. Baron Aquanis (bit 4) has no instance_encounters row and no
    // static spawn, so he is not on the list. Kelris keeps his place (6); the
    // fires follow at 7, then Aku'mai at 8 and Old Serra'kis at 9. Both keep
    // their real kill-bits; only the order moves.
    BossRosterPatch p;
    p.mapId = BFD_MAP;
    p.reorder = {
        { BFD_AKUMAI, 8 },
        { BFD_SERRAKIS, 9 },
    };
    p.add = {
        MakeObjective(OBJ(1), /*encounterIndex*/ 6, BFD_MAP, "Fires of Aku'mai",
                      BFD_FIRES_X, BFD_FIRES_Y, BFD_FIRES_Z, /*arriveRadius*/ 10.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1,
                      /*orderOverride*/ 7),
    };
    t.push_back(std::move(p));
}
