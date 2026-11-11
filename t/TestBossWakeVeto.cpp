/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcBossWakeVeto.h"

using DcBossWakeVeto::PackOffLimits;

// Run 1823401: the Emperor's computed wake sphere was 24.3yd and the planner's
// target Senator stood 21.4yd from him.
TEST(DungeonClearBossWakeVetoTest, ASenatorInsideTheSleepingEmperorsSphereIsOffLimits)
{
    EXPECT_TRUE(PackOffLimits(true, false, 21.4f, 24.3f));
    EXPECT_TRUE(PackOffLimits(true, false, 18.7f, 24.3f));
}

TEST(DungeonClearBossWakeVetoTest, APackOutsideTheSphereOrOnItsEdgeIsStillPullable)
{
    EXPECT_FALSE(PackOffLimits(true, false, 25.6f, 24.3f));
    EXPECT_FALSE(PackOffLimits(true, false, 24.3f, 24.3f));
}

TEST(DungeonClearBossWakeVetoTest, OnlyASleepingRoomAggroBossVetoesAPack)
{
    // An awake boss: the pack is part of the fight, not a wake risk.
    EXPECT_FALSE(PackOffLimits(true, true, 10.0f, 24.3f));
    // A boss with no room-aggro row keeps every pull it had.
    EXPECT_FALSE(PackOffLimits(false, false, 10.0f, 24.3f));
    // No radius resolved: never veto on a zero.
    EXPECT_FALSE(PackOffLimits(true, false, 10.0f, 0.0f));
}

// A room with a pull-out radius (the Mechanar's Sepethrea) keeps a pack on the
// boss's aggro edge as clearable trash: the shrunken radius, not the sphere, rules.
TEST(DungeonClearBossWakeVetoTest, ThePullOutRadiusIsTheLineForAShrunkExclusion)
{
    EXPECT_FALSE(PackOffLimits(true, false, 18.0f, 14.0f));
    EXPECT_TRUE(PackOffLimits(true, false, 12.0f, 14.0f));
}
