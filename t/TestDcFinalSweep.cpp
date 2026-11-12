/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DcFinalSweep.h"

// The final sweep is pure: boss list + completed mask + budget state in, the
// boss to re-target (and the gate objectives to reopen) out. The fixture is the
// Blackrock Depths Upper City tail of the Nov 12 run that lost a full clear:
// Hurley 11, Phalanx 12, Ribbly 13, Plugger 14, with Phalanx skipped mid-run.

namespace
{
    constexpr uint32 HURLEY = 9537, PHALANX = 9502, RIBBLY = 9543, PLUGGER = 9499;
    constexpr uint32 ALE_RUN = 900002, EMPEROR = 9019;

    DungeonBossInfo Boss(uint32 entry, uint32 bit, int32 order = -1)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = bit;
        b.orderOverride = order;
        return b;
    }

    DungeonBossInfo Gate(uint32 entry, uint32 bit, int32 order)
    {
        DungeonBossInfo b = Boss(entry, bit, order);
        b.kind = DungeonAnchorKind::Objective;
        b.doneWithBossBit = true;
        return b;
    }

    // Clear order: Hurley, Ribbly, ale run (gates Phalanx), Phalanx, Plugger, Emperor.
    std::vector<DungeonBossInfo> Roster()
    {
        return {Boss(HURLEY, 11, 11),  Boss(RIBBLY, 13, 12), Gate(ALE_RUN, 12, 12),
                Boss(PHALANX, 12, 13), Boss(PLUGGER, 14),    Boss(EMPEROR, 18)};
    }

    uint32 Mask(std::initializer_list<uint32> bits)
    {
        uint32 m = 0;
        for (uint32 b : bits)
            m |= 1u << b;
        return m;
    }

    DcFinalSweep::Decision RunSweep(std::vector<DungeonBossInfo> const& bosses, uint32 mask,
                               DcFinalSweep::State& st, uint32 now = 1000,
                               std::unordered_set<uint32> corpses = {})
    {
        return DcFinalSweep::Decide(
            bosses, mask, st, now, [](DungeonBossInfo const&) { return false; },
            [&](uint32 e) { return corpses.count(e) != 0; });
    }
}

TEST(DcFinalSweepTest, IncidentMaskLackingBit12SweepsPhalanxAndItsAleRun)
{
    // 520191 in the incident = every bit but 12; here the BRD tail with the Emperor down.
    DcFinalSweep::State st;
    uint32 const mask = Mask({11, 13, 14, 18});
    auto d = RunSweep(Roster(), mask, st);
    ASSERT_EQ(d.outcome, DcFinalSweep::Outcome::Sweep);
    EXPECT_EQ(d.target, PHALANX);
    EXPECT_EQ(d.missingMask, 1u << 12);
    EXPECT_EQ(d.attempt, 1u);
    // The boss and the objective that opens it come back together.
    EXPECT_NE(std::find(d.reopen.begin(), d.reopen.end(), PHALANX), d.reopen.end());
    EXPECT_NE(std::find(d.reopen.begin(), d.reopen.end(), ALE_RUN), d.reopen.end());
    EXPECT_EQ(d.reopen.size(), 2u);
}

TEST(DcFinalSweepTest, AllBitsSetIsReallyCleared)
{
    DcFinalSweep::State st;
    auto d = RunSweep(Roster(), Mask({11, 12, 13, 14, 18}), st);
    EXPECT_EQ(d.outcome, DcFinalSweep::Outcome::NothingMissing);
    EXPECT_EQ(d.missingMask, 0u);
    EXPECT_TRUE(st.attempts.empty());
    EXPECT_EQ(st.startedMs, 0u);
}

TEST(DcFinalSweepTest, LowestClearOrderMissingBossIsSweptFirst)
{
    // Ribbly (order 12) and Phalanx (order 13) both missing: the clear order wins,
    // not the raw bit order.
    DcFinalSweep::State st;
    auto d = RunSweep(Roster(), Mask({11, 14, 18}), st);
    ASSERT_EQ(d.outcome, DcFinalSweep::Outcome::Sweep);
    EXPECT_EQ(d.target, RIBBLY);
    EXPECT_EQ(d.missingMask, Mask({12, 13}));
    // Ribbly has no gate objective at its bit here: Phalanx's ale run (bit 12) is
    // NOT a Ribbly gate (bit 13), so it must not be reopened for him.
    EXPECT_EQ(d.reopen.size(), 1u);
}

TEST(DcFinalSweepTest, GateObjectiveKeyedToAnotherBitIsLeftAlone)
{
    DcFinalSweep::State st;
    auto roster = Roster();
    // Mask lacks only Plugger (14): nothing gates it, only the boss reopens.
    auto d = RunSweep(roster, Mask({11, 12, 13, 18}), st);
    ASSERT_EQ(d.outcome, DcFinalSweep::Outcome::Sweep);
    EXPECT_EQ(d.target, PLUGGER);
    EXPECT_EQ(d.reopen, std::vector<uint32>{PLUGGER});
}

TEST(DcFinalSweepTest, PerBossAttemptBudgetIsBounded)
{
    DcFinalSweep::State st;
    uint32 const mask = Mask({11, 13, 14, 18});
    for (uint32 i = 1; i <= DcFinalSweep::kMaxAttemptsPerBoss; ++i)
    {
        auto d = RunSweep(Roster(), mask, st, 1000 + i);
        ASSERT_EQ(d.outcome, DcFinalSweep::Outcome::Sweep);
        EXPECT_EQ(d.attempt, i);
    }
    // Skipped again after every sweep: the skip now stands.
    auto spent = RunSweep(Roster(), mask, st, 5000);
    EXPECT_EQ(spent.outcome, DcFinalSweep::Outcome::BudgetSpent);
    EXPECT_EQ(spent.missingMask, 1u << 12);
    EXPECT_EQ(st.attempts[PHALANX], DcFinalSweep::kMaxAttemptsPerBoss);
}

TEST(DcFinalSweepTest, SpentBossDoesNotBlockAnotherMissingBoss)
{
    DcFinalSweep::State st;
    st.attempts[RIBBLY] = DcFinalSweep::kMaxAttemptsPerBoss;
    st.startedMs = 1000;
    auto d = RunSweep(Roster(), Mask({11, 14, 18}), st, 2000);
    ASSERT_EQ(d.outcome, DcFinalSweep::Outcome::Sweep);
    EXPECT_EQ(d.target, PHALANX);
}

TEST(DcFinalSweepTest, TotalTimeCapStopsTheSweep)
{
    DcFinalSweep::State st;
    uint32 const mask = Mask({11, 13, 14, 18});
    ASSERT_EQ(RunSweep(Roster(), mask, st, 1000).outcome, DcFinalSweep::Outcome::Sweep);
    // Just inside the cap: still sweeps (second attempt).
    EXPECT_EQ(RunSweep(Roster(), mask, st, 1000 + DcFinalSweep::kTotalCapMs - 1).outcome,
              DcFinalSweep::Outcome::Sweep);
    DcFinalSweep::State st2;
    ASSERT_EQ(RunSweep(Roster(), mask, st2, 1000).outcome, DcFinalSweep::Outcome::Sweep);
    auto capped = RunSweep(Roster(), mask, st2, 1000 + DcFinalSweep::kTotalCapMs);
    EXPECT_EQ(capped.outcome, DcFinalSweep::Outcome::TimeCapped);
    EXPECT_EQ(capped.missingMask, 1u << 12);
}

TEST(DcFinalSweepTest, TimeCapSurvivesMillisecondClockWrap)
{
    DcFinalSweep::State st;
    uint32 const mask = Mask({11, 13, 14, 18});
    uint32 const start = 0xFFFFFFFFu - 1000u;
    ASSERT_EQ(RunSweep(Roster(), mask, st, start).outcome, DcFinalSweep::Outcome::Sweep);
    EXPECT_EQ(RunSweep(Roster(), mask, st, start + 5000u).outcome, DcFinalSweep::Outcome::Sweep);
}

TEST(DcFinalSweepTest, CorpseIsNeverASweepTarget)
{
    // Dead with the bit not flipped yet: transient, it dies into its bit on its own.
    DcFinalSweep::State st;
    auto d = RunSweep(Roster(), Mask({11, 13, 14, 18}), st, 1000, {PHALANX});
    EXPECT_EQ(d.outcome, DcFinalSweep::Outcome::NothingMissing);
    EXPECT_EQ(d.missingMask, 0u);  // a corpse lends no bit to the log
    EXPECT_TRUE(st.attempts.empty());
}

TEST(DcFinalSweepTest, SkipByDesignBossIsNeverMissing)
{
    DcFinalSweep::State st;
    auto roster = Roster();
    for (auto& b : roster)
        if (b.entry == PHALANX)
            b.skipByDesign = true;
    EXPECT_EQ(RunSweep(roster, Mask({11, 13, 14, 18}), st).outcome,
              DcFinalSweep::Outcome::NothingMissing);
}

TEST(DcFinalSweepTest, InstanceDoneBossWithoutABitIsNotMissingAndIsKeptOutOfTheMask)
{
    // A boss the instance script finishes (no DBC bit): done by the predicate is not
    // missing; not done is missing but contributes no bit to the log mask.
    DcFinalSweep::State st;
    auto roster = Roster();
    DungeonBossInfo gw = Boss(777, 0);
    gw.doneBossStateIndex = 3;
    roster.push_back(gw);
    uint32 const full = Mask({11, 12, 13, 14, 18});
    auto done = DcFinalSweep::Decide(
        roster, full, st, 1000, [](DungeonBossInfo const& b) { return b.entry == 777; },
        [](uint32) { return false; });
    EXPECT_EQ(done.outcome, DcFinalSweep::Outcome::NothingMissing);
    auto open = RunSweep(roster, full, st);
    ASSERT_EQ(open.outcome, DcFinalSweep::Outcome::Sweep);
    EXPECT_EQ(open.target, 777u);
    EXPECT_EQ(open.missingMask, 0u);
}

TEST(DcFinalSweepTest, ObjectivesAloneAreNeverTheMissingThing)
{
    DcFinalSweep::State st;
    std::vector<DungeonBossInfo> roster{Boss(HURLEY, 11), Gate(ALE_RUN, 12, 12)};
    EXPECT_EQ(RunSweep(roster, Mask({11}), st).outcome, DcFinalSweep::Outcome::NothingMissing);
}
