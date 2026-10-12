/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cstdint>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcAtBossGate.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"

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
