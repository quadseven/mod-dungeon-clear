/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Unit tests for the stalled fallback's give-up budget (DcStalledFallbackDecision).
//
// The failure being pinned: Advance stalls, the stalled trigger (relevance 20)
// outranks Advance (15), and the fallback action returns true every tick it is
// walking at a target it never reaches. Only a successful Advance clears the stall,
// so Advance never runs again and the leader is stuck off its route for the whole
// run (guild finder run 298505, Ragefire Chasm: 19347 fallback ticks, 0 Advance
// ticks). LadderSim below reproduces exactly that two-rung arbitration.

#include "gtest/gtest.h"

#include "DcStalledFallbackDecision.h"

using DcStalledFallback::Verdict;
using DcStalledFallback::Watch;

namespace
{
    constexpr uint32_t TICK_MS = 500;   // the door value's own cadence; a fair tick
    constexpr uint64_t TARGET_A = 0xf130002c38000006ULL;
    constexpr uint64_t TARGET_B = 0xf130002c38000007ULL;

    // The relevance ladder as the engine runs it: the stalled rung (20) is tried
    // first and, when it returns true, ends the tick; otherwise Advance (15) runs.
    // `distFor` gives the leader's distance to the fallback target at a given ms.
    struct LadderSim
    {
        Watch watch;
        bool stallSet = true;   // Advance stalled on the tick before the sim starts
        uint32_t advanceTicks = 0;
        uint32_t fallbackTicks = 0;
        uint32_t now = 1000;
        uint64_t target = TARGET_A;

        template <typename DistFn>
        void Run(uint32_t totalMs, DistFn distFor)
        {
            uint32_t const end = now + totalMs;
            for (; now < end; now += TICK_MS)
            {
                if (stallSet)
                {
                    // Mirrors DungeonClearClearStalledAction::Execute.
                    uint64_t pick = watch.IsBanned(target, now) ? TARGET_B : target;
                    if (watch.IsBanned(pick, now))
                    {
                        // Nothing left to work: the action returns false and Advance runs.
                    }
                    else if (watch.Observe(pick, distFor(now), now) == Verdict::GiveUp)
                    {
                        watch.Ban(pick, now);
                        stallSet = false;           // ClearStall, then fall through
                    }
                    else
                    {
                        ++fallbackTicks;
                        continue;                   // returned true: Advance starved
                    }
                }
                ++advanceTicks;                     // Advance got the tick
            }
        }
    };
}

TEST(StalledFallback, AdvanceGetsATickWhenTheTargetIsNeverClosed)
{
    LadderSim sim;
    sim.Run(10 * 60 * 1000, [](uint32_t) { return 33.0f; });
    EXPECT_GT(sim.advanceTicks, 0u)
        << "the fallback owned every tick for ten minutes; Advance never ran";
}

TEST(StalledFallback, TheFirstAdvanceTickComesWithinTheBudget)
{
    LadderSim sim;
    uint32_t const start = sim.now;
    sim.Run(DcStalledFallback::BUDGET_MS + 2 * TICK_MS, [](uint32_t) { return 33.0f; });
    EXPECT_GT(sim.advanceTicks, 0u);
    EXPECT_LE(sim.fallbackTicks * TICK_MS, DcStalledFallback::BUDGET_MS + 2 * TICK_MS);
    EXPECT_EQ(sim.now - start, DcStalledFallback::BUDGET_MS + 2 * TICK_MS);
}

TEST(StalledFallback, OscillatingBetweenTwoDistancesIsNoProgress)
{
    // The live shape: the leader walked back and forth, 25 to 34 route vertices
    // and 30 to 34yd off, never getting anywhere.
    Watch w;
    uint32_t now = 1000;
    Verdict last = Verdict::Engage;
    for (uint32_t i = 0; i < 200 && last == Verdict::Engage; ++i, now += TICK_MS)
        last = w.Observe(TARGET_A, (i % 2) ? 34.0f : 31.0f, now);
    EXPECT_EQ(last, Verdict::GiveUp);
}

TEST(StalledFallback, ClosingOnTheTargetNeverGivesUp)
{
    Watch w;
    float dist = 300.0f;
    uint32_t now = 1000;
    for (uint32_t i = 0; i < 400; ++i, now += TICK_MS, dist -= 1.0f)
        ASSERT_EQ(w.Observe(TARGET_A, dist, now), Verdict::Engage) << "tick " << i;
}

TEST(StalledFallback, ContactRangeIsProgressForever)
{
    Watch w;
    uint32_t now = 1000;
    for (uint32_t i = 0; i < 1000; ++i, now += TICK_MS)
        ASSERT_EQ(w.Observe(TARGET_A, 4.0f, now), Verdict::Engage) << "tick " << i;
}

TEST(StalledFallback, ANewTargetStartsAFreshBudget)
{
    Watch w;
    uint32_t now = 1000;
    w.Observe(TARGET_A, 40.0f, now);
    now += DcStalledFallback::BUDGET_MS - TICK_MS;
    ASSERT_EQ(w.Observe(TARGET_A, 40.0f, now), Verdict::Engage);
    now += TICK_MS;
    EXPECT_EQ(w.Observe(TARGET_B, 40.0f, now), Verdict::Engage)
        << "switching target must not inherit the old target's spent budget";
}

TEST(StalledFallback, ASmallJitterInsideProgressYardsDoesNotResetTheBudget)
{
    Watch w;
    uint32_t now = 1000;
    w.Observe(TARGET_A, 40.0f, now);
    Verdict v = Verdict::Engage;
    for (uint32_t i = 0; i < 200 && v == Verdict::Engage; ++i)
    {
        now += TICK_MS;
        v = w.Observe(TARGET_A, 40.0f - 0.9f * (i % 4), now);   // never 5yd better
    }
    EXPECT_EQ(v, Verdict::GiveUp);
}

TEST(StalledFallback, ABannedTargetStaysBannedThenExpires)
{
    Watch w;
    uint32_t const now = 5000;
    w.Ban(TARGET_A, now);
    EXPECT_TRUE(w.IsBanned(TARGET_A, now));
    EXPECT_TRUE(w.IsBanned(TARGET_A, now + DcStalledFallback::BAN_MS - 1));
    EXPECT_FALSE(w.IsBanned(TARGET_A, now + DcStalledFallback::BAN_MS));
    EXPECT_FALSE(w.IsBanned(TARGET_B, now));
}

TEST(StalledFallback, TheBanListRecyclesItsOldestSlot)
{
    Watch w;
    uint32_t const now = 5000;
    for (uint64_t g = 1; g <= Watch::MAX_BANNED + 1; ++g)
        w.Ban(g, now);
    EXPECT_FALSE(w.IsBanned(1, now)) << "oldest ban is overwritten";
    EXPECT_TRUE(w.IsBanned(Watch::MAX_BANNED + 1, now));
}

TEST(StalledFallback, TheEmptyGuidIsNeverBanned)
{
    Watch w;
    EXPECT_FALSE(w.IsBanned(0, 1000));
}

TEST(StalledFallback, TheBudgetSurvivesTheMillisecondClockWrapping)
{
    Watch w;
    uint32_t now = 0xFFFFFFFFu - 10000u;   // 10s before the u32 ms clock wraps
    Verdict v = Verdict::Engage;
    uint32_t elapsed = 0;
    w.Observe(TARGET_A, 33.0f, now);
    while (v == Verdict::Engage && elapsed < 60000u)
    {
        now += TICK_MS;
        elapsed += TICK_MS;
        v = w.Observe(TARGET_A, 33.0f, now);
    }
    EXPECT_EQ(v, Verdict::GiveUp);
    EXPECT_EQ(elapsed, DcStalledFallback::BUDGET_MS);
}

TEST(StalledFallback, BanExpiryIsWrapSafe)
{
    Watch w;
    uint32_t const now = 0xFFFFFFFFu - 1000u;
    w.Ban(TARGET_A, now);
    EXPECT_TRUE(w.IsBanned(TARGET_A, now + 5000u));    // clock has wrapped, ban still live
    EXPECT_FALSE(w.IsBanned(TARGET_A, now + DcStalledFallback::BAN_MS + 1u));
}
