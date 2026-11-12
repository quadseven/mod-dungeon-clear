/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cstdint>
#include <string>

#include "Define.h"
#include "DcDoorPolicy.h"
#include "DcSourceScan.h"

// --- A hold for a key holder who never arrives is bounded (#140) ---------------
//
// Blackrock Depths run 1842024, 2026-11-11: the Shadowforge Lock held for a key
// holder 3271 times in 9.5 minutes. The key holder stood 13yd from a corpse it
// could not loot, never came within 25yd of the tank, and only the run watchdog
// ended it. The hold now has a clock: regroup every interval, give up at the
// ceiling, and pace its log line.
namespace
{
    constexpr uint32 KH_REARM = 10000;
    constexpr uint32 KH_REGROUP = 20000;
    constexpr uint32 KH_CEILING = 90000;
    constexpr uint32 KH_LOG = 30000;

    DcDoorPolicy::KeyHoldVerdict Tick(DcDoorPolicy::KeyHoldClock& c, uint64_t door, uint32 now)
    {
        return DcDoorPolicy::ObserveKeyHold(c, door, now, KH_REARM, KH_REGROUP, KH_CEILING, KH_LOG);
    }
}

TEST(DcKeyHoldTest, AHoldThatNeverEndsGivesUpAtTheCeiling)
{
    using DcDoorPolicy::KeyHoldStep;
    DcDoorPolicy::KeyHoldClock c;
    uint32 now = 5000;
    KeyHoldStep last = KeyHoldStep::Hold;
    uint32 gaveUpAfter = 0;
    // The pre-bound behaviour: a tick every 170ms for ten minutes and no end.
    for (uint32 t = 0; t <= 600000; t += 170)
    {
        auto const v = Tick(c, 161460, now + t);
        last = v.step;
        if (v.step == KeyHoldStep::GiveUp)
        {
            gaveUpAfter = v.heldMs;
            break;
        }
    }
    EXPECT_EQ(last, KeyHoldStep::GiveUp);
    EXPECT_GE(gaveUpAfter, KH_CEILING);
    EXPECT_LT(gaveUpAfter, KH_CEILING + 1000u) << "well under the 300s run watchdog";
}

TEST(DcKeyHoldTest, TheKeyHolderIsCalledInEveryRegroupInterval)
{
    using DcDoorPolicy::KeyHoldStep;
    DcDoorPolicy::KeyHoldClock c;
    uint32 const t0 = 1000;
    uint32 regroups = 0;
    for (uint32 t = 0; t < KH_CEILING; t += 170)
        if (Tick(c, 7, t0 + t).step == KeyHoldStep::Regroup)
            ++regroups;
    // 20s, 40s, 60s and 80s of a 90s hold.
    EXPECT_EQ(regroups, 4u);

    DcDoorPolicy::KeyHoldClock fresh;
    EXPECT_EQ(Tick(fresh, 7, t0).step, KeyHoldStep::Hold) << "no regroup at the start";
    for (uint32 t = 1000; t < KH_REGROUP; t += 1000)
        EXPECT_EQ(Tick(fresh, 7, t0 + t).step, KeyHoldStep::Hold);
    EXPECT_EQ(Tick(fresh, 7, t0 + KH_REGROUP).step, KeyHoldStep::Regroup);
    EXPECT_EQ(Tick(fresh, 7, t0 + KH_REGROUP + 170).step, KeyHoldStep::Hold)
        << "one order per interval, not one per tick";
}

TEST(DcKeyHoldTest, ARegroupIntervalOfZeroAndACeilingOfZeroSwitchThemOff)
{
    using DcDoorPolicy::KeyHoldStep;
    DcDoorPolicy::KeyHoldClock c;
    for (uint32 t = 0; t <= 400000; t += 1000)
    {
        auto const v = DcDoorPolicy::ObserveKeyHold(c, 7, 1000 + t, KH_REARM, 0, 0, KH_LOG);
        EXPECT_EQ(v.step, KeyHoldStep::Hold);
    }
}

TEST(DcKeyHoldTest, ANewDoorOrAGapStartsAFreshClock)
{
    using DcDoorPolicy::KeyHoldStep;
    DcDoorPolicy::KeyHoldClock c;
    for (uint32 t = 0; t < 80000; t += 1000)
        Tick(c, 1, 1000 + t);
    EXPECT_EQ(Tick(c, 1, 1000 + 80000).heldMs, 80000u);
    // Another door: the clock starts over, so the old hold's age is not charged.
    auto const other = Tick(c, 2, 1000 + 81000);
    EXPECT_EQ(other.heldMs, 0u);
    EXPECT_EQ(other.step, KeyHoldStep::Hold);
    // The same door after a gap (the hold ended and a later one began).
    Tick(c, 2, 1000 + 82000);
    auto const later = Tick(c, 2, 1000 + 82000 + KH_REARM);
    EXPECT_EQ(later.heldMs, 0u);
    EXPECT_TRUE(later.log);
}

TEST(DcKeyHoldTest, OpeningTheDoorClearsTheClock)
{
    DcDoorPolicy::KeyHoldClock c;
    for (uint32 t = 0; t <= 60000; t += 1000)
        Tick(c, 1, 1000 + t);
    ASSERT_TRUE(c.armed);
    DcDoorPolicy::ClearKeyHold(c);
    EXPECT_FALSE(c.armed);
    auto const v = Tick(c, 1, 62000);
    EXPECT_EQ(v.heldMs, 0u) << "the next hold on the same door starts from nothing";
    EXPECT_EQ(v.step, DcDoorPolicy::KeyHoldStep::Hold);
}

TEST(DcKeyHoldTest, TheHoldLineIsLoggedOnceAndThenEveryLogInterval)
{
    DcDoorPolicy::KeyHoldClock c;
    uint32 lines = 0;
    // 9.5 minutes of ticks every 170ms, the live run; a clock that gives up at the
    // ceiling is cleared by the caller, so measure one hold of the ceiling's length.
    for (uint32 t = 0; t < KH_CEILING; t += 170)
        if (Tick(c, 161460, 1000 + t).log)
            ++lines;
    EXPECT_EQ(lines, 3u) << "the first tick, then 30s and 60s: not one line per tick";

    DcDoorPolicy::KeyHoldClock every;
    EXPECT_TRUE(DcDoorPolicy::ObserveKeyHold(every, 1, 1000, KH_REARM, 0, 0, 0).log);
    EXPECT_TRUE(DcDoorPolicy::ObserveKeyHold(every, 1, 1170, KH_REARM, 0, 0, 0).log)
        << "an interval of 0 logs every tick";
}

TEST(DcKeyHoldTest, TheClockSurvivesTheMillisecondCounterWrapping)
{
    using DcDoorPolicy::KeyHoldStep;
    DcDoorPolicy::KeyHoldClock c;
    uint32 const start = 0xFFFFFFFFu - 5000u;
    for (uint32 t = 0; t < KH_REGROUP; t += 1000)
        Tick(c, 1, start + t);
    auto const v = Tick(c, 1, start + KH_REGROUP);  // wraps past zero
    EXPECT_EQ(v.heldMs, KH_REGROUP);
    EXPECT_EQ(v.step, KeyHoldStep::Regroup);
}

// The door-blocked action is the one caller: the hold must go through the clock,
// call the key holder in, and not write the old per-tick line.
TEST(DcKeyHoldTest, TheDoorBlockedActionHoldsThroughTheClock)
{
    std::string const src = DcSourceScan::StripComments(DcSourceScan::ReadFile(
        DcSourceScan::ModuleSrc() / "Ai/Dungeon/DungeonClear/Action/DcEngageActions.cpp"));
    EXPECT_NE(src.find("DcDoorPolicy::ObserveKeyHold("), std::string::npos);
    EXPECT_NE(src.find("DcStrandedRecovery::RegroupMember("), std::string::npos);
    EXPECT_NE(src.find("KeyHoldStep::GiveUp"), std::string::npos);
    EXPECT_NE(src.find("if (hold.log)"), std::string::npos)
        << "the hold line is paced by the clock";
}
