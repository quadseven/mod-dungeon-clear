/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"

// The loot yield holds the whole party while any member loots. Live, one clear
// spent 8% of its wall time in it and one pile chained 98s of 15s timeouts, so the
// window is pinned: long enough for a walk-in and a pickup, short of a wedge.
TEST(DcLootYieldTest, TimeoutIsShortEnoughNotToChainIntoMinutes)
{
    EXPECT_LE(DcActionShared::DC_LOOT_YIELD_TIMEOUT_MS, 8000u);
}

TEST(DcLootYieldTest, TimeoutStillCoversAWalkInAndAPickup)
{
    EXPECT_GE(DcActionShared::DC_LOOT_YIELD_TIMEOUT_MS,
              DcActionShared::DC_LOOT_CAMP_TIMEOUT_MS + 2000u);
}
