/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcSourceScan.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcAtBossGate.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBreathSource.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPartyWideChain.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPartyWaitDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPoolExitDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRawPathDrive.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStrandedDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Util/SwimPathfinder.h"

using DcPartyWaitDecision::Decide;
using DcPartyWaitDecision::Inputs;
using DcPartyWaitDecision::Outcome;
using DcPartyWaitDecision::Reason;
using DcPartyWaitDecision::Result;

TEST(DungeonEventBlackfathomDeeps, SwimRouteGoesAroundFloodedWallToSarevess)
{
    // A flooded wall across the direct bearing forces the tank to swim sideways
    // before it can close on the boss. The old greedy fan loops at x=16.
    G3D::Vector3 const start(0.0f, 0.0f, 0.0f);
    G3D::Vector3 const sarevess(100.0f, 0.0f, 0.0f);
    auto const wet = [](G3D::Vector3 const& p)
    {
        return std::abs(p.z) < 3.0f;
    };
    auto const clear = [](G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        float const length = (b - a).length();
        int const samples = std::max(1, static_cast<int>(std::ceil(length)));
        for (int i = 0; i <= samples; ++i)
        {
            float const t = static_cast<float>(i) / samples;
            G3D::Vector3 const p = a + (b - a) * t;
            if (p.x >= 20.0f && p.x <= 24.0f && std::abs(p.y) <= 30.0f)
                return false;
        }
        return true;
    };

    SwimPathfinder::Result const route = SwimPathfinder::BuildCore(start, sarevess, clear, wet);
    ASSERT_TRUE(route.ok) << route.failureReason;
    ASSERT_FALSE(route.points.empty());
    EXPECT_LT((route.points.back() - sarevess).length(), 0.01f);
    G3D::Vector3 previous = start;
    for (G3D::Vector3 const& point : route.points)
    {
        EXPECT_TRUE(clear(previous, point));
        EXPECT_TRUE(wet(point));
        previous = point;
    }
}

// Blackfathom Deeps (map 48). Live, on the dev realm: 9 guild runs, 0 clears.
// Five sat out the 7200 s ceiling at 1 to 4 of 7 bosses. The derived order sent
// the tank to Old Serra'kis (bit 5) straight after Lorgus Jett, behind the
// Portal of Aku'Mai, and nothing lit the four Fires of Aku'mai that open it.

namespace
{
    constexpr uint32 MAP = 48;

    DungeonBossInfo Boss(uint32 entry, uint32 bit, char const* name)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = bit;
        b.name = name;
        b.mapId = MAP;
        return b;
    }

    // What BossSpawnIndex derives for map 48: one row per spawned
    // kill-credit encounter, in DungeonEncounter bit order.
    std::vector<DungeonBossInfo> DerivedList()
    {
        return {
            Boss(4887, 0, "Ghamoo-ra"),
            Boss(4831, 1, "Lady Sarevess"),
            Boss(6243, 2, "Gelihast"),
            Boss(12902, 3, "Lorgus Jett"),
            Boss(4830, 5, "Old Serra'kis"),
            Boss(4832, 6, "Twilight Lord Kelris"),
            Boss(4829, 7, "Aku'mai"),
        };
    }

    std::vector<DungeonBossInfo> Applied()
    {
        return BossRosterRegistry::Apply(
            MAP, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), DerivedList());
    }
}

TEST(DungeonEventBlackfathomDeeps, MapHasAPatch)
{
    EXPECT_TRUE(BossRosterRegistry::HasPatch(MAP));
    EXPECT_TRUE(DungeonEventRegistry::HasEvents(MAP));
}

// Kelris stands among the fires; the fires open the portal; Old Serra'kis and
// Aku'mai are behind it. Aku'mai is the finder's finish, so he goes first.
TEST(DungeonEventBlackfathomDeeps, FiresComeAfterKelrisAndBeforeTheBossesBehindThePortal)
{
    std::vector<DungeonBossInfo> const out = Applied();
    ASSERT_EQ(out.size(), 8u);

    EXPECT_EQ(out[0].entry, 4887u);   // Ghamoo-ra
    EXPECT_EQ(out[1].entry, 4831u);   // Lady Sarevess
    EXPECT_EQ(out[2].entry, 6243u);   // Gelihast
    EXPECT_EQ(out[3].entry, 12902u);  // Lorgus Jett
    EXPECT_EQ(out[4].entry, 4832u);   // Twilight Lord Kelris
    EXPECT_EQ(out[5].kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(out[5].eventId, 1u);
    EXPECT_EQ(out[6].entry, 4829u);   // Aku'mai
    EXPECT_EQ(out[7].entry, 4830u);   // Old Serra'kis
}

// Only the ORDER moves: completion still reads each boss's own DBC bit.
TEST(DungeonEventBlackfathomDeeps, ReorderedBossesKeepTheirKillBits)
{
    std::vector<DungeonBossInfo> const out = Applied();
    for (DungeonBossInfo const& b : out)
    {
        if (b.entry == 4829)
            EXPECT_EQ(b.encounterIndex, 7u);
        if (b.entry == 4830)
            EXPECT_EQ(b.encounterIndex, 5u);
        if (b.entry == 4832)
            EXPECT_EQ(b.encounterIndex, 6u);
    }
}

// The objective sits on the fire platform, where the event's ClearRadius is
// judged from (the executor only certifies a clear from within 12yd).
TEST(DungeonEventBlackfathomDeeps, ObjectiveSitsOnTheFirePlatform)
{
    for (DungeonBossInfo const& b : Applied())
    {
        if (b.kind != DungeonAnchorKind::Objective)
            continue;
        EXPECT_NEAR(b.x, -818.7f, 2.0f);
        EXPECT_NEAR(b.y, -164.5f, 2.0f);
        EXPECT_NEAR(b.z, -25.8f, 2.0f);
        return;
    }
    FAIL() << "no Fires of Aku'mai objective on map 48";
}

// Light a fire, clear ITS wave, light the next: the fires' SmartAI summons a
// wave on every Use(), and the portal opens on the last summon's death after
// the fourth fire. Then wait for the portal.
TEST(DungeonEventBlackfathomDeeps, FiresEventLightsEachFireThenClearsItsWave)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, 1);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Anchored);
    EXPECT_TRUE(ev->persistent);
    EXPECT_TRUE(ev->required);

    struct Pair { uint32 fire; uint32 wave; };
    Pair const expected[] = {
        { 21118, 4825 },  // Aku'mai Snapjaw
        { 21119, 4977 },  // Murkshallow Softshell
        { 21120, 4823 },  // Barbed Crustacean
        { 21121, 4978 },  // Aku'mai Servant
    };

    ASSERT_EQ(ev->steps.size(), 9u);
    for (size_t i = 0; i < 4; ++i)
    {
        EventStep const& use = ev->steps[i * 2];
        EventStep const& clear = ev->steps[i * 2 + 1];
        EXPECT_EQ(use.kind, EventStepKind::UseGameObject) << "fire " << i;
        EXPECT_EQ(use.goEntry, expected[i].fire) << "fire " << i;
        // The fires answer the plain Use() (gossip-hello filter 1).
        EXPECT_FALSE(use.reportUse) << "fire " << i;

        EXPECT_EQ(clear.kind, EventStepKind::ClearRadius) << "wave " << i;
        ASSERT_EQ(clear.entryFilter.size(), 1u) << "wave " << i;
        EXPECT_EQ(clear.entryFilter[0], expected[i].wave) << "wave " << i;
    }

    EventStep const& door = ev->steps[8];
    EXPECT_EQ(door.kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(door.goEntry, 21117u);  // Portal of Aku'Mai
    EXPECT_EQ(door.wantState, 0u);    // GO_STATE_ACTIVE
}

// The wave volume must hold every summon point and nothing static: the summon
// points are 44 to 45yd out at platform height, static Snapjaws behind the shut
// portal are 56yd out, and a static Barbed Crustacean sits 11yd below.
TEST(DungeonEventBlackfathomDeeps, WaveVolumeHoldsTheSummonsAndNoStaticSpawn)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, 1);
    ASSERT_NE(ev, nullptr);
    for (EventStep const& s : ev->steps)
    {
        if (s.kind != EventStepKind::ClearRadius)
            continue;
        EXPECT_GE(s.radius, 45.0f);
        EXPECT_LT(s.radius, 56.0f);
        EXPECT_LT(s.zBand, 11.0f);
        EXPECT_GE(s.zBand, 2.0f);
    }
}

// The portal opens by script only. A bot click would skip the fires.
TEST(DungeonEventBlackfathomDeeps, PortalOfAkumaiIsScriptOnly)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsScriptOnly(21117));
}

// ---------------------------------------------------------------------------
// Gelihast: the tank holds at the boss and the at-boss engage never fires.
//
// Live (2026-10-12, 41 guild runs, 0 clears, 13 stopped at 2 bosses): two tanks
// logged "within engage range of Gelihast (14yd, live=1) -> holding for at-boss
// [partyReady=1 availLoot=0 canLoot=0 tankCombat=0]" 1,911 times in five
// minutes. The hold and the trigger now read one verdict (DcAtBossGate), the
// door gate needs a real door, and a valve opens past the gates that can wedge.
// ---------------------------------------------------------------------------
namespace
{
    using DcAtBossGate::Gate;

    // The tank on Gelihast's spot, 14yd out: driving, on his floor, the party
    // ready, nothing to loot, Gelihast alive.
    DcAtBossGate::Inputs AtGelihast()
    {
        DcAtBossGate::Inputs in;
        in.driving = true;
        in.bossAnchor = true;
        in.atEngage = true;
        in.liveBoss = true;
        in.partyReady = true;
        in.valveMs = DC_AT_BOSS_HOLD_VALVE_MS;
        return in;
    }

    constexpr uint32_t TICK_MS = 100;

    // Ticks the gate from `startMs` the way the live caller does, carrying the
    // valve clock, until it fires or `budgetMs` runs out. Returns the ms it took
    // to fire, or UINT32_MAX.
    uint32_t MsToFire(DcAtBossGate::Inputs in, uint32_t startMs, uint32_t budgetMs,
                      DcAtBossGate::Verdict* last = nullptr)
    {
        in.heldSinceMs = 0;
        for (uint32_t t = 0; t <= budgetMs; t += TICK_MS)
        {
            in.nowMs = startMs + t;
            DcAtBossGate::Verdict const v = DcAtBossGate::Decide(in);
            in.heldSinceMs = v.heldSinceMs;
            if (last)
                *last = v;
            if (v.fire)
                return t;
        }
        return UINT32_MAX;
    }
}

// A GameObject that is not a door (a hut, the shrine) blocks the door ray on the
// line to Gelihast, and the door index has no shut door there. That is not a
// door, so the engage fires on the first tick.
TEST(DungeonEventBlackfathomDeeps, GelihastEngagesPastAModelThatIsNotADoor)
{
    DcAtBossGate::Inputs in = AtGelihast();
    in.closedDoor = DcAtBossGate::DoorVetoes(/*losBlocked*/ true, /*shutDoors*/ 0,
                                             /*eventGates*/ 0);
    EXPECT_FALSE(in.closedDoor);

    DcAtBossGate::Verdict v;
    EXPECT_EQ(MsToFire(in, 50000, 0, &v), 0u);
    EXPECT_EQ(v.gate, Gate::Open);
    EXPECT_FALSE(v.valveOpen);
}

// The acceptance case: the tank at Gelihast's spot with the party ready, held by
// room trash that never clears, reaches the engage within the valve and not
// before it.
TEST(DungeonEventBlackfathomDeeps, TankHeldAtGelihastWithThePartyReadyEngagesWithinTheValve)
{
    DcAtBossGate::Inputs in = AtGelihast();
    in.roomTrash = true;

    DcAtBossGate::Verdict v;
    uint32_t const took = MsToFire(in, 50000, 3 * DC_AT_BOSS_HOLD_VALVE_MS, &v);
    ASSERT_NE(took, UINT32_MAX) << "the tank never engaged Gelihast";
    EXPECT_LE(took, DC_AT_BOSS_HOLD_VALVE_MS);
    EXPECT_GE(took, DC_AT_BOSS_HOLD_VALVE_MS - TICK_MS);
    EXPECT_TRUE(v.valveOpen);
    EXPECT_EQ(v.gate, Gate::RoomTrash);
}

// Same hold across the millisecond clock's wrap.
TEST(DungeonEventBlackfathomDeeps, ValveSurvivesTheClockWrap)
{
    DcAtBossGate::Inputs in = AtGelihast();
    in.roomTrash = true;
    uint32_t const took =
        MsToFire(in, UINT32_MAX - 1000, 3 * DC_AT_BOSS_HOLD_VALVE_MS);
    EXPECT_LE(took, DC_AT_BOSS_HOLD_VALVE_MS);
    EXPECT_GE(took, DC_AT_BOSS_HOLD_VALVE_MS - TICK_MS);
}

// The valve is for a party that is ready and a tank that is held. Anything else
// keeps the gate shut and the clock at zero.
TEST(DungeonEventBlackfathomDeeps, ValveStaysShutUnlessThePartyIsReadyAtALiveBoss)
{
    uint32_t const budget = 3 * DC_AT_BOSS_HOLD_VALVE_MS;
    auto held = []() {
        DcAtBossGate::Inputs in = AtGelihast();
        in.roomTrash = true;
        return in;
    };

    DcAtBossGate::Inputs in = held();
    in.partyReady = false;
    EXPECT_EQ(MsToFire(in, 1000, budget), UINT32_MAX) << "party not ready";

    in = held();
    in.lootPending = true;
    EXPECT_EQ(MsToFire(in, 1000, budget), UINT32_MAX) << "loot pending";

    in = held();
    in.liveBoss = false;
    EXPECT_EQ(MsToFire(in, 1000, budget), UINT32_MAX) << "boss not live";

    in = held();
    in.raid = true;
    EXPECT_EQ(MsToFire(in, 1000, budget), UINT32_MAX) << "raid: the muster owns it";

    in = held();
    in.valveMs = 0;
    EXPECT_EQ(MsToFire(in, 1000, budget), UINT32_MAX) << "valve off";
}

// Never past a real shut door, the anchored route, a pull, or out of range, even
// when room trash is also down.
TEST(DungeonEventBlackfathomDeeps, ValveNeverOpensPastAHardGate)
{
    uint32_t const budget = 3 * DC_AT_BOSS_HOLD_VALVE_MS;
    struct Case { char const* name; void (*set)(DcAtBossGate::Inputs&); Gate want; };
    Case const cases[] = {
        { "door",     [](DcAtBossGate::Inputs& i) { i.closedDoor = true; },   Gate::RoomTrash },
        { "hops",     [](DcAtBossGate::Inputs& i) { i.anchoredHops = true; }, Gate::RoomTrash },
        { "pull",     [](DcAtBossGate::Inputs& i) { i.pullOwns = true; },     Gate::PullOwns },
        { "range",    [](DcAtBossGate::Inputs& i) { i.atEngage = false; },    Gate::NotAtEngage },
        { "encounter",[](DcAtBossGate::Inputs& i) { i.encounterHold = true; },Gate::EncounterHold },
        { "drive",    [](DcAtBossGate::Inputs& i) { i.driving = false; },     Gate::NotDriving },
        { "anchor",   [](DcAtBossGate::Inputs& i) { i.bossAnchor = false; },  Gate::NotBossAnchor },
    };
    for (Case const& c : cases)
    {
        DcAtBossGate::Inputs in = AtGelihast();
        in.roomTrash = true;
        c.set(in);
        DcAtBossGate::Verdict v;
        EXPECT_EQ(MsToFire(in, 1000, budget, &v), UINT32_MAX) << c.name;
        EXPECT_EQ(v.gate, c.want) << c.name;
        EXPECT_EQ(v.heldSinceMs, 0u) << c.name;
    }

    // A real door alone is reported as the door.
    DcAtBossGate::Inputs in = AtGelihast();
    in.closedDoor = true;
    EXPECT_EQ(DcAtBossGate::Decide(in).gate, Gate::ClosedDoor);
}

// Every gate DungeonClearAtBossTrigger::IsActive used to test inline, each down
// on its own, still refuses the engage now that the trigger reads only
// DcAtBoss::Evaluate(...).fire, and the hard ones refuse it past the valve too.
// Drive, anchor kind, encounter hold, pull ownership, engage range, door,
// anchored route, raid rez, raid muster and dungeon readiness never fire; room
// trash and a sealed muster hold until the valve, and not one tick before it.
TEST(DungeonEventBlackfathomDeeps, EveryGateTheTriggerTestedStillRefusesTheEngage)
{
    uint32_t const budget = 3 * DC_AT_BOSS_HOLD_VALVE_MS;
    struct Case { char const* name; void (*set)(DcAtBossGate::Inputs&); Gate want; bool valve; };
    Case const cases[] = {
        { "drive",     [](DcAtBossGate::Inputs& i) { i.driving = false; },       Gate::NotDriving,    false },
        { "anchor",    [](DcAtBossGate::Inputs& i) { i.bossAnchor = false; },    Gate::NotBossAnchor, false },
        { "encounter", [](DcAtBossGate::Inputs& i) { i.encounterHold = true; },  Gate::EncounterHold, false },
        { "pull",      [](DcAtBossGate::Inputs& i) { i.pullOwns = true; },       Gate::PullOwns,      false },
        { "range",     [](DcAtBossGate::Inputs& i) { i.atEngage = false; },      Gate::NotAtEngage,   false },
        { "door",      [](DcAtBossGate::Inputs& i) { i.closedDoor = true; },     Gate::ClosedDoor,    false },
        { "hops",      [](DcAtBossGate::Inputs& i) { i.anchoredHops = true; },   Gate::AnchoredHops,  false },
        { "ready",     [](DcAtBossGate::Inputs& i) { i.partyReady = false; },    Gate::PartyNotReady, false },
        { "rez",       [](DcAtBossGate::Inputs& i) { i.raid = true; i.rezPending = true; },
                                                                                 Gate::RezPending,    false },
        { "muster",    [](DcAtBossGate::Inputs& i) { i.raid = true; i.raidMusterHolds = true; },
                                                                                 Gate::RaidMuster,    false },
        { "trash",     [](DcAtBossGate::Inputs& i) { i.roomTrash = true; },      Gate::RoomTrash,     true },
        { "sealed",    [](DcAtBossGate::Inputs& i) { i.sealedHolds = true; },    Gate::SealedMuster,  true },
    };
    for (Case const& c : cases)
    {
        DcAtBossGate::Inputs in = AtGelihast();
        c.set(in);
        in.nowMs = 50000;
        DcAtBossGate::Verdict const first = DcAtBossGate::Decide(in);
        EXPECT_FALSE(first.fire) << c.name;
        EXPECT_EQ(first.gate, c.want) << c.name;

        uint32_t const took = MsToFire(in, 50000, budget);
        if (c.valve)
            EXPECT_GE(took, DC_AT_BOSS_HOLD_VALVE_MS - TICK_MS) << c.name;
        else
            EXPECT_EQ(took, UINT32_MAX) << c.name;
    }
}

// One tick that is not a valve hold (the party dips below ready) re-arms the
// clock: the valve measures one continuous hold.
TEST(DungeonEventBlackfathomDeeps, ANotReadyTickReArmsTheValve)
{
    DcAtBossGate::Inputs in = AtGelihast();
    in.roomTrash = true;
    in.nowMs = 10000;
    DcAtBossGate::Verdict v = DcAtBossGate::Decide(in);
    EXPECT_EQ(v.heldSinceMs, 10000u);
    EXPECT_FALSE(v.fire);

    in.heldSinceMs = v.heldSinceMs;
    in.nowMs = 10000 + DC_AT_BOSS_HOLD_VALVE_MS - 1;
    in.partyReady = false;
    v = DcAtBossGate::Decide(in);
    EXPECT_EQ(v.heldSinceMs, 0u);
    EXPECT_EQ(v.gate, Gate::RoomTrash);

    in.heldSinceMs = v.heldSinceMs;
    in.partyReady = true;
    in.nowMs += 1;
    v = DcAtBossGate::Decide(in);
    EXPECT_FALSE(v.fire);
    EXPECT_EQ(v.heldSinceMs, in.nowMs);
}

// Losing the drive gate during a hold must discard elapsed time. If the tank
// resumes driving beside Gelihast, it gets a fresh valve window.
TEST(DungeonEventBlackfathomDeeps, DriveGateInterruptsTheGelihastValveClock)
{
    DcAtBossGate::Inputs in = AtGelihast();
    in.roomTrash = true;
    in.nowMs = 10000;
    DcAtBossGate::Verdict v = DcAtBossGate::Decide(in);
    ASSERT_EQ(v.heldSinceMs, 10000u);

    in.heldSinceMs = v.heldSinceMs;
    in.nowMs += DC_AT_BOSS_HOLD_VALVE_MS - 1;
    in.driving = false;
    v = DcAtBossGate::Decide(in);
    EXPECT_EQ(v.gate, Gate::NotDriving);
    EXPECT_FALSE(v.fire);
    EXPECT_EQ(v.heldSinceMs, 0u);

    in.heldSinceMs = v.heldSinceMs;
    in.driving = true;
    ++in.nowMs;
    v = DcAtBossGate::Decide(in);
    EXPECT_FALSE(v.fire);
    EXPECT_EQ(v.heldSinceMs, in.nowMs);

    in.heldSinceMs = v.heldSinceMs;
    in.nowMs += DC_AT_BOSS_HOLD_VALVE_MS;
    v = DcAtBossGate::Decide(in);
    EXPECT_TRUE(v.fire);
    EXPECT_TRUE(v.valveOpen);
}

// Loot appearing mid-hold re-arms the clock the same way: the valve fires only
// after a full, continuous valve-length hold with no loot pending.
TEST(DungeonEventBlackfathomDeeps, ALootPendingTickReArmsTheValve)
{
    DcAtBossGate::Inputs in = AtGelihast();
    in.roomTrash = true;
    in.nowMs = 10000;
    DcAtBossGate::Verdict v = DcAtBossGate::Decide(in);
    EXPECT_EQ(v.heldSinceMs, 10000u);

    in.heldSinceMs = v.heldSinceMs;
    in.nowMs = 10000 + DC_AT_BOSS_HOLD_VALVE_MS - 1;
    in.lootPending = true;
    v = DcAtBossGate::Decide(in);
    EXPECT_FALSE(v.fire);
    EXPECT_EQ(v.heldSinceMs, 0u);

    // Loot cleared one tick later: the old hold's time does not count.
    in.heldSinceMs = v.heldSinceMs;
    in.lootPending = false;
    in.nowMs += 1;
    v = DcAtBossGate::Decide(in);
    EXPECT_FALSE(v.fire);
    EXPECT_EQ(v.heldSinceMs, in.nowMs);
    EXPECT_GE(MsToFire(in, in.nowMs, 3 * DC_AT_BOSS_HOLD_VALVE_MS),
              DC_AT_BOSS_HOLD_VALVE_MS - TICK_MS);
}

// Both valve-overridable gates down together still open through the valve (the
// first one down is the one reported), but either alongside a hard gate does not.
TEST(DungeonEventBlackfathomDeeps, ValveOpensPastTrashAndMusterTogetherButNotWithADoor)
{
    uint32_t const budget = 3 * DC_AT_BOSS_HOLD_VALVE_MS;

    DcAtBossGate::Inputs in = AtGelihast();
    in.roomTrash = true;
    in.sealedHolds = true;
    DcAtBossGate::Verdict v;
    uint32_t const took = MsToFire(in, 1000, budget, &v);
    ASSERT_NE(took, UINT32_MAX);
    EXPECT_TRUE(v.valveOpen);
    EXPECT_EQ(v.gate, Gate::RoomTrash);

    in = AtGelihast();
    in.sealedHolds = true;
    in.closedDoor = true;
    EXPECT_EQ(MsToFire(in, 1000, budget, &v), UINT32_MAX);
    EXPECT_EQ(v.gate, Gate::ClosedDoor);
}

// The door veto needs a shut door from the door index on the line, and one that
// is not an interact-through event gate (the Blackwing Lair cage exception).
TEST(DungeonEventBlackfathomDeeps, DoorVetoNeedsARealShutDoor)
{
    EXPECT_FALSE(DcAtBossGate::DoorVetoes(false, 0, 0));
    EXPECT_FALSE(DcAtBossGate::DoorVetoes(false, 1, 0));  // ray clear: door is open/elsewhere
    EXPECT_FALSE(DcAtBossGate::DoorVetoes(true, 0, 0));   // not a door (Gelihast)
    EXPECT_TRUE(DcAtBossGate::DoorVetoes(true, 1, 0));    // a door
    EXPECT_FALSE(DcAtBossGate::DoorVetoes(true, 1, 1));   // only an event gate
    EXPECT_TRUE(DcAtBossGate::DoorVetoes(true, 2, 1));    // a gate and a door
}

// The gate's first-down order, which is what the logs report.
TEST(DungeonEventBlackfathomDeeps, AtBossGateReportsTheFirstGateDown)
{
    DcAtBossGate::Inputs in = AtGelihast();
    EXPECT_EQ(DcAtBossGate::FirstClosed(in), Gate::Open);

    in.partyReady = false;
    EXPECT_EQ(DcAtBossGate::FirstClosed(in), Gate::PartyNotReady);
    in.sealedHolds = true;
    EXPECT_EQ(DcAtBossGate::FirstClosed(in), Gate::SealedMuster);
    in.anchoredHops = true;
    EXPECT_EQ(DcAtBossGate::FirstClosed(in), Gate::AnchoredHops);
    in.closedDoor = true;
    EXPECT_EQ(DcAtBossGate::FirstClosed(in), Gate::ClosedDoor);
    in.roomTrash = true;
    EXPECT_EQ(DcAtBossGate::FirstClosed(in), Gate::RoomTrash);

    // Raid: the muster and a pending rez gate it, not dungeon readiness.
    DcAtBossGate::Inputs raid = AtGelihast();
    raid.raid = true;
    raid.partyReady = false;
    EXPECT_EQ(DcAtBossGate::FirstClosed(raid), Gate::Open);
    raid.raidMusterHolds = true;
    EXPECT_EQ(DcAtBossGate::FirstClosed(raid), Gate::RaidMuster);
    raid.rezPending = true;
    EXPECT_EQ(DcAtBossGate::FirstClosed(raid), Gate::RezPending);

    EXPECT_STREQ(DcAtBossGate::Name(Gate::RoomTrash), "room trash");
    EXPECT_TRUE(DcAtBossGate::ValveCanOverride(Gate::RoomTrash));
    EXPECT_TRUE(DcAtBossGate::ValveCanOverride(Gate::SealedMuster));
    EXPECT_FALSE(DcAtBossGate::ValveCanOverride(Gate::ClosedDoor));
    EXPECT_FALSE(DcAtBossGate::ValveCanOverride(Gate::AnchoredHops));
    EXPECT_FALSE(DcAtBossGate::ValveCanOverride(Gate::PartyNotReady));
}

// ---------------------------------------------------------------------------
// The valve has to win the tick, not only open.
//
// Live (2026-10-27, run 2798): the tank sat at Gelihast from 07:55:31Z to
// 08:10:21Z on gate 'room trash' with the party ready and nothing to loot. The
// valve opened at 40050 ms and logged "engaging anyway" 30 times, up to 884176
// ms, and the tank never moved: Gelihast is a room-clear boss, and the pull
// rung (DcRel::Pull, 35) keeps its room-clear exception at the boss, so it took
// every tick the at-boss engage (DcRel::AtBoss, 30) needed. The pull now stands
// down once the valve is open.
// ---------------------------------------------------------------------------

// Ticks the gate at Gelihast with room trash that never clears and reports, per
// tick, whether the pull rung would still hold the tick. Returns the ms at which
// the engage first both fires and has the pull out of its way, or UINT32_MAX.
namespace
{
    uint32_t MsToEngageTakingTheTick(DcAtBossGate::Inputs in, bool pullbackBoss,
                                     uint32_t budgetMs)
    {
        in.heldSinceMs = 0;
        for (uint32_t t = 0; t <= budgetMs; t += TICK_MS)
        {
            in.nowMs = 50000 + t;
            DcAtBossGate::Verdict const v = DcAtBossGate::Decide(in);
            in.heldSinceMs = v.heldSinceMs;
            bool const pullStandsDown = DcAtBossGate::PullStandsDownAtBoss(
                /*atBossEngage*/ true, /*roomClearActive*/ in.roomTrash, pullbackBoss,
                v.valveOpen);
            if (v.fire && pullStandsDown)
                return t;
        }
        return UINT32_MAX;
    }
}

TEST(DungeonEventBlackfathomDeeps, GelihastValveHandsTheTickFromTheRoomClearPullToTheEngage)
{
    DcAtBossGate::Inputs in = AtGelihast();
    in.roomTrash = true;

    // Before the valve: the room clear is the pull's, and the engage is held.
    EXPECT_FALSE(DcAtBossGate::PullStandsDownAtBoss(true, /*roomClear*/ true,
                                                    /*pullback*/ false, /*valve*/ false));

    uint32_t const took = MsToEngageTakingTheTick(in, /*pullbackBoss*/ false,
                                                  3 * DC_AT_BOSS_HOLD_VALVE_MS);
    ASSERT_NE(took, UINT32_MAX)
        << "the valve opened but the pull rung kept the tick: the engage never ran";
    EXPECT_LE(took, DC_AT_BOSS_HOLD_VALVE_MS);
    EXPECT_GE(took, DC_AT_BOSS_HOLD_VALVE_MS - TICK_MS);
}

// The rest of the pull rung's at-boss rule is unchanged: at a plain boss it
// stands down, away from the boss this rule does not stand it down, and a
// pull-back boss keeps the pull whatever the valve says (the walk-in would go
// where he stands).
TEST(DungeonEventBlackfathomDeeps, PullStandDownAtTheBossKeepsItsOtherCases)
{
    EXPECT_TRUE(DcAtBossGate::PullStandsDownAtBoss(true, false, false, false));
    EXPECT_FALSE(DcAtBossGate::PullStandsDownAtBoss(false, false, false, false));
    EXPECT_FALSE(DcAtBossGate::PullStandsDownAtBoss(false, true, false, true));
    EXPECT_FALSE(DcAtBossGate::PullStandsDownAtBoss(true, false, true, false));
    EXPECT_FALSE(DcAtBossGate::PullStandsDownAtBoss(true, true, true, true));

    DcAtBossGate::Inputs in = AtGelihast();
    in.roomTrash = true;
    EXPECT_EQ(MsToEngageTakingTheTick(in, /*pullbackBoss*/ true, 3 * DC_AT_BOSS_HOLD_VALVE_MS),
              UINT32_MAX);
}

// The live pull trigger asks this predicate with the live valve, rather than
// keeping its own copy of the room-clear exception.
TEST(DungeonEventBlackfathomDeeps, PullTriggerAsksTheValveAtTheBoss)
{
    std::string const src = DcSourceScan::StripComments(DcSourceScan::ReadFile(
        DcSourceScan::ModuleSrc() / "Ai/Dungeon/DungeonClear/Trigger/DungeonClearTriggers.cpp"));
    auto const begin = src.find("bool DungeonClearPullTrigger::IsActive()");
    ASSERT_NE(begin, std::string::npos);
    auto const end = src.find("bool DungeonClearPullManeuverTrigger::IsActive()", begin);
    ASSERT_NE(end, std::string::npos);
    std::string const body = src.substr(begin, end - begin);
    EXPECT_FALSE(DcSourceScan::CallOffsets(body, "PullStandsDownAtBoss").empty());
    EXPECT_NE(body.find(".valveOpen"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Razorfen Downs: Tuten'kash (entry 7355) is summoned by the gong, so until the
// third ring there is no live boss at his anchor. The valve must not open on an
// empty anchor (the gong event owns that wait), and once he is up the same room
// trash hold at him goes through the valve and the pull hands the tick over,
// as at Gelihast.
// ---------------------------------------------------------------------------
TEST(DungeonEventRazorfenDowns, TutenkashAtBossValveWaitsForTheSummonThenEngages)
{
    DcAtBossGate::Inputs in = AtGelihast();   // same shape: at the anchor, party ready
    in.roomTrash = true;

    in.liveBoss = false;   // before the gong's third ring
    EXPECT_EQ(MsToFire(in, 1000, 3 * DC_AT_BOSS_HOLD_VALVE_MS), UINT32_MAX)
        << "the valve opened on an anchor with no boss on it";

    in.liveBoss = true;    // summoned
    uint32_t const took = MsToEngageTakingTheTick(in, /*pullbackBoss*/ false,
                                                  3 * DC_AT_BOSS_HOLD_VALVE_MS);
    ASSERT_NE(took, UINT32_MAX);
    EXPECT_LE(took, DC_AT_BOSS_HOLD_VALVE_MS);
}

// Criterion 1 (issue #66): at the 180s ceiling at Gelihast (map 48) with two
// out-of-range members and StrandedRecovery on, the wait is rescued instead of
// ended - the same kernel the live BFD Gelihast stall would hit, framed against
// map 48.
TEST(DungeonEventBlackfathomDeeps, AtTheCeilingAtGelihastWithStrandedMembersRescuesInsteadOfStopping)
{
    // The tank and healer are here, two members are out of spread, the 180s
    // ceiling has run, and StrandedRecovery is opted in: teleport the strays to
    // the tank and keep waiting, do not end the run.
    Inputs in;
    in.waitedMs = 180000;
    in.timeoutMs = 180000;
    in.strandedTimeoutMs = 60000;
    in.presentAlive = 5;
    in.presentHasHealer = true;
    in.rosterAlive = 5;
    in.rosterHasHealer = true;
    in.strandedOutOfRange = 2;
    in.strandedRecoveryEnabled = true;

    Result const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::Rescue);
    EXPECT_EQ(r.reason, Reason::StillWaiting);
}

// Criterion 1 (issue #66): Gelihast's tank is still holding while two living
// bots, present in the instance but with no walkable route, remain 40yd away.
// The stranded clock expires before the longer party-wait ceiling; rescue both
// and keep the wait alive rather than waiting for the ceiling to stop the run.
TEST(DungeonEventBlackfathomDeeps, GelihastRescuesTwoStrandedMembersBeforePartyWaitCeiling)
{
    using DcStrandedDecision::Member;
    using StrandedInputs = DcStrandedDecision::Inputs;
    using StrandedResult = DcStrandedDecision::Result;

    StrandedInputs stranded;
    stranded.enabled = true;
    stranded.nowMs = 160000;
    stranded.lastProgressMs = 100000;
    stranded.noProgressTimeoutMs = 60000;
    stranded.maxSpread = 25.0f;

    std::vector<Member> party(5);
    party[0].isBot = true;
    party[0].isAlive = true;
    party[0].onMap = true;
    party[0].isTank = true; // Pongakiah, holding at Gelihast's spot
    for (std::size_t i = 1; i < party.size(); ++i)
    {
        party[i].isBot = true;
        party[i].isAlive = true;
        party[i].onMap = true; // present in the same BFD instance
        party[i].distToTank = i == 1 ? 5.0f : (i < 4 ? 40.0f : 10.0f);
    }

    auto const outOfRange = [&party, &stranded]()
    {
        std::uint32_t count = 0;
        for (Member const& m : party)
            if (!m.isTank && m.distToTank > stranded.maxSpread)
                ++count;
        return count;
    };

    Inputs wait;
    wait.waitedMs = 60000;
    wait.timeoutMs = 180000;
    wait.strandedTimeoutMs = 60000;
    wait.presentAlive = 5;
    wait.presentHasHealer = true;
    wait.rosterAlive = 5;
    wait.rosterHasHealer = true;
    wait.strandedOutOfRange = outOfRange();
    wait.strandedRecoveryEnabled = true;

    // Both strays are still 40yd out when the stranded clock runs out: rescue.
    ASSERT_EQ(wait.strandedOutOfRange, 2u);
    EXPECT_EQ(Decide(wait).outcome, Outcome::Rescue);

    StrandedResult const rescue = DcStrandedDecision::Decide(stranded, party);
    ASSERT_TRUE(rescue.recover);
    ASSERT_EQ(rescue.strandedIdx.size(), 2u);
    for (int idx : rescue.strandedIdx)
        party[static_cast<std::size_t>(idx)].distToTank = 0.0f; // Recover brings them to tank

    // Recovery moved both inside spread, well before the ceiling: keep waiting.
    wait.strandedOutOfRange = outOfRange();
    wait.rescueAlreadyFired = true;
    EXPECT_EQ(wait.strandedOutOfRange, 0u);
    EXPECT_EQ(Decide(wait).outcome, Outcome::Wait);
    EXPECT_LT(wait.waitedMs, wait.timeoutMs);
}

// Criterion 2 (issue #66): the run is stopped at Gelihast for Gorvash and
// Grassfed, re-enabled, and stopped again for the same pair. The second stop is
// spoken through the same memory the call site keeps across DisableDungeonClear,
// so it ends with the distinct RepeatedTimeout reason, not another plain timeout.
TEST(DungeonEventBlackfathomDeeps, SecondGelihastTimeoutOnTheSameMembersEndsWithRepeatedTimeout)
{
    std::string const why = "Waiting on Gorvash (out of range), Grassfed (out of range)";
    DcPartyWaitDecision::TimeoutMemory mem;

    std::string const first = DcPartyWaitDecision::RecordTimeout(mem, 6243u, "Gelihast", 180, why);
    EXPECT_NE(first.find("the party never became ready after 180s"), std::string::npos);
    EXPECT_EQ(first.find("RepeatedTimeout"), std::string::npos);

    std::string const second = DcPartyWaitDecision::RecordTimeout(mem, 6243u, "Gelihast", 180, why);
    EXPECT_NE(second.find("RepeatedTimeout at Gelihast"), std::string::npos);
    EXPECT_NE(second.find("Gorvash"), std::string::npos);
    EXPECT_NE(second.find("Grassfed"), std::string::npos);
    EXPECT_EQ(second.find("the party never became ready after"), std::string::npos);

    // A stop on someone else is a fresh first timeout again.
    std::string const other = DcPartyWaitDecision::RecordTimeout(
        mem, 6243u, "Gelihast", 180, "Waiting on Gorvash (out of range)");
    EXPECT_EQ(other.find("RepeatedTimeout"), std::string::npos);
}

// Upstream 2026-10-26: the party-wide chain kernels and the Forgotten Pool exit.
// Its tests of the authored jump puzzle, swims, fires and roster are not here:
// production keeps its own (BlackfathomDeepsEvents.cpp).

namespace
{
    constexpr uint32 BFD = 48;

    DungeonEvent const* Ev(uint32 id)
    {
        DungeonEvent const* ev = DungeonEventRegistry::Find(BFD, id);
        EXPECT_NE(ev, nullptr) << "map 48 event " << id << " is not registered";
        return ev;
    }

    // Upstream's jump chain and swim, built here because this branch does not
    // register them. They exercise the chain kernels with real coordinates.
    DungeonEvent JumpChain()
    {
        return EventBuilder(BFD, 6, "Cross the jump puzzle")
            .Anchored(0)
            .Persistent()
            .PartyWide()
            .FallVolume(-366.0f, -265.0f, 0.0f, 120.0f, -57.0f, -360.27f, 34.96f, -53.09f, -314.77f,
                        68.71f, -53.57f, 3)
            .MoveTo(-360.27f, 34.96f, -53.09f, 1.5f)
            .Jump(-354.35f, 35.09f, -52.12f, 1.5f)
            .MoveTo(-337.57f, 43.21f, -53.12f, 1.5f)
            .Jump(-334.89f, 48.72f, -53.12f, 1.5f)
            .MoveTo(-330.01f, 50.59f, -53.12f, 1.5f)
            .Jump(-323.65f, 50.43f, -53.12f, 1.5f)
            .MoveTo(-315.25f, 61.75f, -53.12f, 1.5f)
            .Jump(-314.77f, 68.71f, -53.57f, 2.0f)
            .Build();
    }

    DungeonEvent SwimChain()
    {
        return EventBuilder(BFD, 1, "Swim")
            .Anchored(1)
            .Persistent()
            .PartyWide()
            .MoveTo(-378.0f, 292.0f, -56.0f, 3.0f)
            .RawPath({G3D::Vector3(-370.0f, 296.0f, -62.0f), G3D::Vector3(-348.0f, 320.0f, -55.5f)}, 4.0f)
            .Swim()
            .Build();
    }

    std::vector<G3D::Vector3> Line()
    {
        return {G3D::Vector3(0, 0, 0), G3D::Vector3(10, 0, 0), G3D::Vector3(20, 0, 0),
                G3D::Vector3(30, 0, 0)};
    }
}

// --- DcRawPathDrive ---------------------------------------------------------

TEST(DcRawPathDrive, FirstTickIssuesFromTheCursorAndArmsTheWatch)
{
    DcProgressWatchdog w;
    auto const d = DcRawPathDrive::Step(Line(), 0, G3D::Vector3(-5, 0, 0), 3.0f, /*spline*/ false, 1000, w);
    EXPECT_EQ(d.verdict, DcRawPathDrive::Verdict::Issue);
    EXPECT_EQ(d.cursor, 0u);
    EXPECT_EQ(w.lastProgressMs, 1000u);
}

TEST(DcRawPathDrive, CursorAdvancesPastReachedPointsAndRearmsPerPoint)
{
    DcProgressWatchdog w;
    DcRawPathDrive::Step(Line(), 0, G3D::Vector3(-5, 0, 0), 3.0f, false, 1000, w);
    // Standing on point 1 (10,0,0): points 0 and 1 are consumed, the cursor
    // moves to 2, and the watchdog is re-armed for the new point.
    auto const d = DcRawPathDrive::Step(Line(), 0, G3D::Vector3(10.5f, 0, 0), 3.0f, true, 3000, w);
    EXPECT_EQ(d.cursor, 2u);
    EXPECT_EQ(d.verdict, DcRawPathDrive::Verdict::InFlight);
    EXPECT_EQ(w.lastProgressMs, 3000u);
}

TEST(DcRawPathDrive, LastPointIsNeverConsumedShortOfTheArriveRadius)
{
    DcProgressWatchdog w;
    // 2.9yd from the last point with pointReached 3 but arriveRadius 1.5: the
    // leg is NOT done and the cursor stays on the last point.
    auto const d = DcRawPathDrive::Step(Line(), 3, G3D::Vector3(27.1f, 0, 0), 1.5f, false, 1000, w);
    EXPECT_NE(d.verdict, DcRawPathDrive::Verdict::Arrived);
    EXPECT_EQ(d.cursor, 3u);
    auto const e = DcRawPathDrive::Step(Line(), 3, G3D::Vector3(29.0f, 0, 0), 1.5f, false, 1000, w);
    EXPECT_EQ(e.verdict, DcRawPathDrive::Verdict::Arrived);
}

TEST(DcRawPathDrive, ArrivedIsIdempotentFromAnyCursor)
{
    DcProgressWatchdog w;
    auto const d = DcRawPathDrive::Step(Line(), 0, G3D::Vector3(30, 0.5f, 0), 3.0f, false, 1000, w);
    EXPECT_EQ(d.verdict, DcRawPathDrive::Verdict::Arrived);
}

TEST(DcRawPathDrive, NoProgressForStuckMsIsAWedge)
{
    DcProgressWatchdog w;
    DcRawPathDrive::Step(Line(), 0, G3D::Vector3(-5, 0, 0), 3.0f, false, 1000, w);
    EXPECT_EQ(DcRawPathDrive::Step(Line(), 0, G3D::Vector3(-5, 0, 0), 3.0f, true, 6000, w).verdict,
              DcRawPathDrive::Verdict::InFlight);
    EXPECT_EQ(DcRawPathDrive::Step(Line(), 0, G3D::Vector3(-5, 0, 0), 3.0f, true, 7100, w).verdict,
              DcRawPathDrive::Verdict::Wedged);
    // Closing again (after the caller re-issued) is progress and clears the wedge.
    w.Reset();
    EXPECT_EQ(DcRawPathDrive::Step(Line(), 0, G3D::Vector3(-2, 0, 0), 3.0f, true, 7200, w).verdict,
              DcRawPathDrive::Verdict::InFlight);
}

TEST(DcRawPathDrive, FarFromTheCurrentPointIsOffLeg)
{
    DcProgressWatchdog w;
    auto const d = DcRawPathDrive::Step(Line(), 1, G3D::Vector3(10, 80, 0), 3.0f, false, 1000, w);
    EXPECT_EQ(d.verdict, DcRawPathDrive::Verdict::OffLeg);
}

TEST(DcRawPathDrive, EmptyPathIsArrived)
{
    DcProgressWatchdog w;
    EXPECT_EQ(DcRawPathDrive::Step({}, 0, G3D::Vector3(0, 0, 0), 3.0f, false, 1, w).verdict,
              DcRawPathDrive::Verdict::Arrived);
}

// --- DcChainRelease ----------------------------------------------------------

TEST(DcChainRelease, ReleaseAfterStepIsTheFirstJumpElseTheFirstRawPath)
{
    DungeonEvent const chain = JumpChain();
    DungeonEvent const* puzzle = &chain;
    // MoveTo take-off(0), Jump 1(1), walk(2) ...: a follower starts once the bot
    // ahead is PAST step 1, i.e. has landed on the ridge and is walking it.
    EXPECT_EQ(DcChainRelease::ReleaseAfterStep(*puzzle), 1u);

    DungeonEvent const swim = SwimChain();
    EXPECT_EQ(DcChainRelease::ReleaseAfterStep(swim), 1u);
}

TEST(DcChainRelease, ThreeFollowersReleaseOneStoneApart)
{
    using namespace DcChainRelease;
    uint32 const after = 2;
    uint32 const spacing = 8000;

    // Leader not started: nobody goes.
    Ahead leader;
    EXPECT_FALSE(MayStart(leader, after, 1000, spacing));
    leader.known = true;
    EXPECT_FALSE(MayStart(leader, after, 1000, spacing));

    // Leader on the rim (step 1) / on A's hop (step 2): follower 1 waits.
    leader.startedMs = 1000;
    leader.stepIndex = 1;
    EXPECT_FALSE(MayStart(leader, after, 2000, spacing));
    leader.stepIndex = 2;
    EXPECT_FALSE(MayStart(leader, after, 3000, spacing));
    // Leader landed A, now on B's hop (step 3): follower 1 is released.
    leader.stepIndex = 3;
    EXPECT_TRUE(MayStart(leader, after, 4000, spacing));

    // Follower 2 keys on follower 1, not on the leader: still waiting while
    // follower 1 is on the rim ...
    Ahead f1;
    f1.known = true;
    f1.startedMs = 4000;
    f1.stepIndex = 1;
    EXPECT_FALSE(MayStart(f1, after, 5000, spacing));
    // ... released once it has left stone A ...
    f1.stepIndex = 3;
    EXPECT_TRUE(MayStart(f1, after, 6000, spacing));
    // ... or when it finished, or by the time-spacing fallback.
    Ahead f2;
    f2.known = true;
    f2.startedMs = 6000;
    f2.stepIndex = 1;
    EXPECT_FALSE(MayStart(f2, after, 10000, spacing));
    EXPECT_TRUE(MayStart(f2, after, 14000, spacing));
    f2.done = true;
    EXPECT_TRUE(MayStart(f2, after, 6500, spacing));
}

// --- DcFallVolume ------------------------------------------------------------

TEST(DcFallVolume, ClassifiesPoolTakeOffIslandsAndLanding)
{
    DungeonEvent const chain = JumpChain();
    DungeonEvent const* puzzle = &chain;
    DcFallVolume const& f = puzzle->fall;
    ASSERT_TRUE(f.enabled);
    // In the pool: inside the cavern box, below the water line — including the
    // gaps between the islands and the water at the foot of the pocket's ramp.
    EXPECT_TRUE(f.Contains(-300.0f, 60.0f, -59.0f));
    EXPECT_TRUE(f.Contains(-344.0f, 46.0f, -59.0f));
    EXPECT_TRUE(f.Contains(-357.1f, 33.1f, -58.1f));
    // On the take-off / an island / the landing: above the water line.
    EXPECT_FALSE(f.Contains(-360.27f, 34.96f, -53.09f));
    EXPECT_FALSE(f.Contains(-337.57f, 43.21f, -53.12f));
    EXPECT_FALSE(f.Contains(-323.65f, 50.43f, -53.12f));
    EXPECT_FALSE(f.Contains(-314.77f, 68.71f, -53.57f));
    // Deep but outside the cavern box (the lake beyond the tunnel): not a fall.
    EXPECT_FALSE(f.Contains(-400.0f, 200.0f, -66.0f));
    // Disabled volume never matches.
    DcFallVolume off;
    EXPECT_FALSE(off.Contains(-300.0f, 60.0f, -59.0f));
}

TEST(DcFallVolume, ArmedFromTheFirstHopNotOnTheApproachSwim)
{
    DungeonEvent const chain = JumpChain();
    DungeonEvent const* puzzle = &chain;
    // Step 0 is the approach: a follower may still be swimming it, through
    // the very water the volume covers. Armed from jump 1 on.
    EXPECT_FALSE(DcPartyWideChain::FallArmed(*puzzle, 0));
    for (uint32 i = 1; i < puzzle->steps.size(); ++i)
        EXPECT_TRUE(DcPartyWideChain::FallArmed(*puzzle, i)) << "step " << i;

    // A submerged RawPath is an approach, not a hop: never arms the volume.
    DungeonEvent swim;
    EventStep move;
    move.kind = EventStepKind::MoveTo;
    EventStep sub;
    sub.kind = EventStepKind::RawPath;
    sub.swim = true;
    swim.steps = {move, sub};
    EXPECT_FALSE(DcPartyWideChain::FallArmed(swim, 1));
    swim.steps[1].swim = false;
    EXPECT_TRUE(DcPartyWideChain::FallArmed(swim, 1));
}

// --- DcBreathSource -----------------------------------------------------------

namespace
{
    DcBreathSource::Member Bot(bool aura = false, bool shaman = false, bool warlock = false)
    {
        DcBreathSource::Member m;
        m.isBot = true;
        m.alive = true;
        m.hasAura = aura;
        m.canCastShamanBreath = shaman;
        m.canCastWarlockBreath = warlock;
        return m;
    }
}

TEST(DcBreathSource, ShamanCoversEveryoneIncludingItself)
{
    std::vector<DcBreathSource::Member> party = {Bot(), Bot(false, true), Bot(), Bot(), Bot()};
    auto const plan = DcBreathSource::Plan(party);
    ASSERT_EQ(plan.size(), 5u);
    for (auto const& a : plan)
    {
        EXPECT_EQ(a.kind, DcBreathSource::Kind::Shaman);
        EXPECT_EQ(a.caster, 1);
    }
    EXPECT_FALSE(DcBreathSource::AllBotsCovered(plan));
}

TEST(DcBreathSource, WarlockWithoutShamanThenElixirWithNeither)
{
    std::vector<DcBreathSource::Member> party = {Bot(), Bot(false, false, true), Bot()};
    auto const plan = DcBreathSource::Plan(party);
    for (auto const& a : plan)
    {
        EXPECT_EQ(a.kind, DcBreathSource::Kind::Warlock);
        EXPECT_EQ(a.caster, 1);
    }
    std::vector<DcBreathSource::Member> none = {Bot(), Bot(), Bot()};
    for (auto const& a : DcBreathSource::Plan(none))
    {
        EXPECT_EQ(a.kind, DcBreathSource::Kind::Elixir);
        EXPECT_EQ(a.caster, -1);
    }
}

TEST(DcBreathSource, ShamanBeatsWarlockAndCoveredHumanDeadAreLeftAlone)
{
    DcBreathSource::Member human;
    human.isBot = false;
    human.alive = true;
    DcBreathSource::Member dead = Bot();
    dead.alive = false;
    std::vector<DcBreathSource::Member> party = {Bot(true), Bot(false, false, true), Bot(false, true), human, dead};
    auto const plan = DcBreathSource::Plan(party);
    EXPECT_EQ(plan[0].kind, DcBreathSource::Kind::Covered);
    EXPECT_EQ(plan[1].kind, DcBreathSource::Kind::Shaman);
    EXPECT_EQ(plan[1].caster, 2);
    EXPECT_EQ(plan[2].kind, DcBreathSource::Kind::Shaman);
    EXPECT_EQ(plan[3].kind, DcBreathSource::Kind::Human);
    EXPECT_EQ(plan[4].kind, DcBreathSource::Kind::Dead);

    std::vector<DcBreathSource::Member> covered = {Bot(true), Bot(true), human, dead};
    EXPECT_TRUE(DcBreathSource::AllBotsCovered(DcBreathSource::Plan(covered)));
}

TEST(DcBreathSource, UnderwaterVerdicts)
{
    using namespace DcBreathSource;
    // Short dips are nothing.
    EXPECT_EQ(DecideUnderwater(5000, false, false), Verdict::None);
    // 20s under with no swim leg and no fight: stranded -> rescue.
    EXPECT_EQ(DecideUnderwater(21000, false, false), Verdict::Rescue);
    // The same 20s ON a swim leg, or in combat: leave it.
    EXPECT_EQ(DecideUnderwater(21000, true, false), Verdict::None);
    EXPECT_EQ(DecideUnderwater(21000, false, true), Verdict::None);
    // 150s of the 180s timer: rescue out of combat, log only in it.
    EXPECT_EQ(DecideUnderwater(151000, true, false), Verdict::Rescue);
    EXPECT_EQ(DecideUnderwater(151000, true, true), Verdict::LogOnly);
}

// --- pooled spawns -------------------------------------------------------------

TEST(DcPooledSpawn, ResolveLiveSpawnPicksTheOneWithALiveCreatureElseTheFirst)
{
    std::vector<DcSpawnPoint> spawns = {{-622.4f, -10.4f, -22.8f}, {-455.9f, -40.0f, -32.5f},
                                        {-474.2f, -86.9f, -39.9f}};
    auto near = [](float x, float y, float) { return std::fabs(x + 455.9f) < 1.0f && std::fabs(y + 40.0f) < 1.0f; };
    EXPECT_EQ(ResolveLiveSpawn(spawns, near), 1u);
    auto none = [](float, float, float) { return false; };
    EXPECT_EQ(ResolveLiveSpawn(spawns, none), 0u);
    std::vector<DcSpawnPoint> single;
    EXPECT_EQ(ResolveLiveSpawn(single, near), 0u);
}

// --- the authored events ------------------------------------------------------

TEST(DungeonEventBlackfathom, BuilderStampsPartyWideAndLastMovementOntoSteps)
{
    DungeonEvent ev = EventBuilder(1, 1, "x")
                          .Anchored(0)
                          .Persistent()
                          .PartyWide()
                          .EnsureWaterBreathing()
                          .MoveTo(0, 0, 0, 2)
                          .RawPath({G3D::Vector3(1, 0, 0), G3D::Vector3(2, 0, 0)}, 1.5f)
                          .Swim()
                          .Jump(3, 0, 0, 1.5f)
                          .JumpSpeedZ(5.0f)
                          .UseGO(7, 10)
                          .SkipIfInstanceData(4, 3)
                          .Build();
    ASSERT_EQ(ev.steps.size(), 5u);
    EXPECT_TRUE(ev.partyWide);
    for (EventStep const& s : ev.steps)
        EXPECT_TRUE(s.partyWide);
    EXPECT_FALSE(ev.steps[1].lastMovementStep);
    EXPECT_FALSE(ev.steps[2].lastMovementStep);
    EXPECT_TRUE(ev.steps[3].lastMovementStep);
    EXPECT_FALSE(ev.steps[4].lastMovementStep);
    EXPECT_TRUE(ev.steps[2].swim);
    EXPECT_FLOAT_EQ(ev.steps[2].x, 2.0f) << "RawPath mirrors its last point into x/y/z";
    EXPECT_FLOAT_EQ(ev.steps[3].jumpSpeedZ, 5.0f);
    EXPECT_EQ(ev.steps[4].skipIfDataId, 4);
    EXPECT_EQ(ev.steps[4].skipIfDataValue, 3u);
    EXPECT_TRUE(IsMovementStepKind(EventStepKind::RawPath));
    EXPECT_TRUE(IsOffMeshStepKind(EventStepKind::Jump));
    EXPECT_FALSE(IsOffMeshStepKind(EventStepKind::MoveTo));
    EXPECT_FALSE(IsMovementStepKind(EventStepKind::EnsureWaterBreathing));
}

// --- the Forgotten Pool exit --------------------------------------------------
// Positions are the sk-20261022-002111 timeouts' own (tank parked under Kelris,
// swimming the pool) and the walkway they fell from; floors are the offline
// navmesh/VMAP reads. A bot is "in the pool" only over deep water inside it.

TEST(DcPoolExit, InPoolOnlyOverDeepWaterInsideThePool)
{
    DcPoolExit::Pool const& pool = DcBlackfathomDeeps::ForgottenPool();
    // tr-...-1026's final tank position, on the pool floor under Kelris.
    EXPECT_TRUE(DcPoolExit::InPool(pool, -808.3f, -153.0f, -36.2f, -38.4f));
    // tr-...-690's, bobbing at the surface under the walkway overhang.
    EXPECT_TRUE(DcPoolExit::InPool(pool, -818.8f, -154.5f, -29.5f, -40.0f));
    // Swimming mid-pool where it fell in.
    EXPECT_TRUE(DcPoolExit::InPool(pool, -689.9f, -55.9f, -36.6f, -37.2f));

    // The walkway's north end: ankle-deep water at the surface level.
    EXPECT_FALSE(DcPoolExit::InPool(pool, -770.1f, -14.5f, -31.3f, -31.3f));
    // Kelris's walkway: above the surface.
    EXPECT_FALSE(DcPoolExit::InPool(pool, -818.8f, -155.6f, -25.9f, -25.9f));
    // The ramp top itself (the leg's end) is out, whatever is under it.
    EXPECT_FALSE(DcPoolExit::InPool(pool, -740.39f, -2.38f, -31.28f, -40.0f));
    // Outside the box: Aku'mai's lake, and the corridor in from Lorgus.
    EXPECT_FALSE(DcPoolExit::InPool(pool, -848.4f, -453.9f, -33.9f, -50.0f));
    EXPECT_FALSE(DcPoolExit::InPool(pool, -709.7f, 28.4f, -29.8f, -40.0f));
}

TEST(DcPoolExit, LegFollowsTheFloorCorridorOffTheFloorThenUpTheRamp)
{
    DcPoolExit::Pool const& pool = DcBlackfathomDeeps::ForgottenPool();
    // pathq's floor corridor from the -1026 spot (start point first).
    std::vector<G3D::Vector3> const corridor = {
        {-808.3f, -153.0f, -38.4f}, {-801.6f, -110.7f, -37.8f}, {-768.0f, -64.0f, -44.2f},
        {-760.8f, -49.1f, -40.5f},  {-746.7f, -10.9f, -35.2f}};
    std::vector<G3D::Vector3> const leg = DcPoolExit::BuildLeg(pool, corridor);
    // Start dropped, the four corridor points, then the ramp top (the foot is
    // already the corridor's end, so it is not doubled).
    ASSERT_EQ(leg.size(), 5u);
    EXPECT_FLOAT_EQ(leg[0].x, -801.6f);
    EXPECT_FLOAT_EQ(leg[1].z, -44.2f + pool.floorLift) << "swum off the floor";
    for (G3D::Vector3 const& q : leg)
        EXPECT_LE(q.z, pool.surfaceZ) << "never above the water";
    EXPECT_EQ(leg.back(), pool.rampTop);
    // It reaches the walkway (#0) at the user's .gps.
    EXPECT_FLOAT_EQ(leg.back().x, -740.39f);
    EXPECT_FLOAT_EQ(leg.back().y, -2.38f);
}

TEST(DcPoolExit, NoCorridorStillSwimsToTheFootAndUpTheRamp)
{
    DcPoolExit::Pool const& pool = DcBlackfathomDeeps::ForgottenPool();
    std::vector<G3D::Vector3> const leg = DcPoolExit::BuildLeg(pool, {});
    ASSERT_EQ(leg.size(), 2u);
    EXPECT_FLOAT_EQ(leg[0].x, pool.rampFoot.x);
    EXPECT_EQ(leg[1], pool.rampTop);
    // A partial corridor that stops short gets the foot appended.
    std::vector<G3D::Vector3> const partial = DcPoolExit::BuildLeg(
        pool, {{-808.3f, -153.0f, -38.4f}, {-783.7f, -133.6f, -31.4f}});
    ASSERT_EQ(partial.size(), 3u);
    EXPECT_FLOAT_EQ(partial[1].x, pool.rampFoot.x);
}

TEST(DungeonEventBlackfathom, PoolExitIsARepeatableConditionalCustomHook)
{
    using namespace DcBlackfathomDeeps;
    DungeonEvent const* ev = Ev(EVENT_POOL_EXIT);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Conditional);
    EXPECT_TRUE(ev->condition != nullptr);
    EXPECT_TRUE(ev->repeatable) << "a second fall must re-arm it";
    EXPECT_TRUE(ev->stepsOwnMovement) << "the hook issues its own spline";
    EXPECT_FALSE(ev->drivesInCombat) << "a fight in the water is the combat engine's";
    ASSERT_EQ(ev->steps.size(), 1u);
    EXPECT_EQ(ev->steps[0].kind, EventStepKind::Custom);
    EXPECT_EQ(ev->steps[0].hookId, HOOK_POOL_EXIT);
    EXPECT_EQ(ev->steps[0].timeoutMs, POOL_EXIT_TIMEOUT_MS);
    EXPECT_TRUE(ObjectiveHookRegistry::Has(HOOK_POOL_EXIT));
    // PanelBeforeBoss on a Repeatable event is a permanent pull hold.
    EXPECT_EQ(ev->panelGatesBossEntry, 0u);
    EXPECT_EQ(ev->panelSortAfterBossEntry, NPC_LORGUS_JETT);
}
