/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Util/DcBreathDecision.h"

// Surface for breath (DcBreathDecision.h). Live on Blackfathom Deeps: 19 of 25
// deaths on map 48 were drownings, five members at once, at the depth the swim
// legs run.

namespace
{
    constexpr int32_t kBar = 180000;     // WaterBreath.Timer default
    constexpr float kSwim = 4.722f;      // base swim speed, yd/s

    DcBreath::Point P(float x, float y, float z) { return DcBreath::Point{ x, y, z }; }
}

TEST(DcBreathTest, BarStartsFullAndDrainsOneMsPerMsUnderWater)
{
    DcBreath::Model m;
    DcBreath::Tick(m, /*under*/ true, kBar, 1000);
    EXPECT_EQ(m.remainingMs, kBar);
    DcBreath::Tick(m, true, kBar, 61000);
    EXPECT_EQ(m.remainingMs, kBar - 60000);
}

TEST(DcBreathTest, BarRefillsTenTimesFasterAboveWaterAndCaps)
{
    DcBreath::Model m;
    DcBreath::Tick(m, true, kBar, 0);
    DcBreath::Tick(m, true, kBar, 170000);
    EXPECT_EQ(m.remainingMs, 10000);
    DcBreath::Tick(m, false, kBar, 175000);   // 5 s up: +50 s
    EXPECT_EQ(m.remainingMs, 60000);
    DcBreath::Tick(m, false, kBar, 200000);   // capped at the bar
    EXPECT_EQ(m.remainingMs, kBar);
}

TEST(DcBreathTest, NoTimerMeansNothingIsOwed)
{
    DcBreath::Model m;
    DcBreath::Tick(m, true, /*Water Breathing*/ 0, 0);
    DcBreath::Tick(m, true, 0, 600000);
    EXPECT_FALSE(DcBreath::ShouldSurface(m, true, 100.0f, kSwim));
    EXPECT_TRUE(DcBreath::DoneSurfacing(m, true, false));
}

// The drowning case: a stalled bot sits underwater. It must go up while the
// swim to air, doubled, plus the reserve, still fits in the bar.
TEST(DcBreathTest, SurfacesWhileTheSwimToAirStillFits)
{
    DcBreath::Model m;
    DcBreath::Tick(m, true, kBar, 0);
    float const yards = 50.0f;    // 10 s of swimming at 5 yd/s
    // need = 10 s * 2 + 15 s = 35 s
    DcBreath::Tick(m, true, kBar, kBar - 36000);
    EXPECT_FALSE(DcBreath::ShouldSurface(m, true, yards, 5.0f));
    DcBreath::Tick(m, true, kBar, kBar - 35000);
    EXPECT_TRUE(DcBreath::ShouldSurface(m, true, yards, 5.0f));
    // It never waits for the damage ticks.
    EXPECT_GT(m.remainingMs, 0);
}

TEST(DcBreathTest, NeverSurfacesAboveWater)
{
    DcBreath::Model m;
    DcBreath::Tick(m, true, kBar, 0);
    DcBreath::Tick(m, true, kBar, kBar - 1000);
    EXPECT_FALSE(DcBreath::ShouldSurface(m, /*under*/ false, 10.0f, kSwim));
}

TEST(DcBreathTest, RefillsBeforeDivingAgainLessSoInAFight)
{
    DcBreath::Model m;
    DcBreath::Tick(m, true, kBar, 0);
    DcBreath::Tick(m, true, kBar, kBar - 20000);   // 20 s left
    EXPECT_FALSE(DcBreath::DoneSurfacing(m, true, false));
    DcBreath::Tick(m, false, kBar, kBar - 10000);  // +100 s -> 120 s
    EXPECT_FALSE(DcBreath::DoneSurfacing(m, false, false));
    EXPECT_TRUE(DcBreath::DoneSurfacing(m, false, /*inCombat*/ true));
    DcBreath::Tick(m, false, kBar, kBar - 4000);   // capped -> 180 s
    EXPECT_TRUE(DcBreath::DoneSurfacing(m, false, false));
}

TEST(DcBreathTest, AWaterBreathingAuraEndingKeepsTheBarsShare)
{
    DcBreath::Model m;
    DcBreath::Tick(m, true, kBar, 0);
    DcBreath::Tick(m, true, kBar, 90000);          // half left
    DcBreath::Tick(m, true, 2 * kBar, 90000);      // a racial or buff doubles it
    EXPECT_EQ(m.remainingMs, kBar);
}

// The way back is the bot's own swim since it last breathed.
TEST(DcBreathTest, TrailStartsAtTheLastBreathAndFollowsTheSwim)
{
    DcBreath::Model m;
    DcBreath::RecordTrail(m, false, P(0, 0, 0));
    DcBreath::RecordTrail(m, false, P(5, 0, 0));   // still breathing: just moves
    ASSERT_EQ(m.trail.size(), 1u);
    DcBreath::RecordTrail(m, true, P(9, 0, -3));   // 5yd from the breath
    DcBreath::RecordTrail(m, true, P(14, 0, -3));
    DcBreath::RecordTrail(m, true, P(15, 0, -3));  // under spacing: skipped
    DcBreath::RecordTrail(m, true, P(19, 0, -3));
    ASSERT_EQ(m.trail.size(), 4u);
    EXPECT_FLOAT_EQ(m.trail[0].x, 5.0f);
    EXPECT_FLOAT_EQ(m.trail[3].x, 19.0f);
    EXPECT_NEAR(DcBreath::TrailYards(m, P(19, 0, -3)), 15.0f, 1e-3f);
}

TEST(DcBreathTest, RetraceRunsTheTrailBackwardToTheLastBreath)
{
    DcBreath::Model m;
    DcBreath::RecordTrail(m, false, P(0, 0, 0));
    for (int i = 1; i <= 5; ++i)
        DcBreath::RecordTrail(m, true, P(float(i * 5), 0, -5));
    std::vector<DcBreath::Point> w = DcBreath::RetraceWindow(m, P(26, 0, -5), 24);
    ASSERT_EQ(w.size(), 7u);
    EXPECT_FLOAT_EQ(w[0].x, 26.0f);
    EXPECT_FLOAT_EQ(w[1].x, 25.0f);
    EXPECT_FLOAT_EQ(w.back().x, 0.0f);
    EXPECT_FLOAT_EQ(w.back().z, 0.0f);
}

// Swimming back must not extend the trail behind itself, or the next window
// would turn the bot around toward the deep end.
TEST(DcBreathTest, SurfacingShortensTheTrailInsteadOfExtendingIt)
{
    DcBreath::Model m;
    DcBreath::RecordTrail(m, false, P(0, 0, 0));
    for (int i = 1; i <= 5; ++i)
        DcBreath::RecordTrail(m, true, P(float(i * 5), 0, -5));
    m.surfacing = true;
    DcBreath::RecordTrail(m, true, P(19, 0, -5));  // passed 25 and 20
    ASSERT_EQ(m.trail.size(), 4u);
    EXPECT_FLOAT_EQ(m.trail.back().x, 15.0f);
    std::vector<DcBreath::Point> w = DcBreath::RetraceWindow(m, P(19, 0, -5), 24);
    for (size_t i = 1; i < w.size(); ++i)
        EXPECT_LT(w[i].x, w[i - 1].x) << "window must only head back toward air";
}

TEST(DcBreathTest, TrailNeverDropsTheBreathPoint)
{
    DcBreath::Model m;
    DcBreath::RecordTrail(m, false, P(-1, -1, 0));
    for (size_t i = 1; i <= DcBreath::TrailMax + 50; ++i)
        DcBreath::RecordTrail(m, true, P(float(i * 5), 0, -5));
    EXPECT_EQ(m.trail.size(), DcBreath::TrailMax);
    EXPECT_FLOAT_EQ(m.trail[0].x, -1.0f);
}

TEST(DcBreathTest, RetraceWindowIsCapped)
{
    DcBreath::Model m;
    DcBreath::RecordTrail(m, false, P(0, 0, 0));
    for (int i = 1; i <= 60; ++i)
        DcBreath::RecordTrail(m, true, P(float(i * 5), 0, -5));
    EXPECT_EQ(DcBreath::RetraceWindow(m, P(301, 0, -5), 24).size(), 24u);
}
