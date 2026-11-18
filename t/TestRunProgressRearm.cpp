/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// The no-kill clock must not be re-armed by a re-arm.
//
// Live (guild run 6257, Scholomance, level 60, 2026-11-18): the party wiped six
// times in 85 minutes with one boss down and the 30 minute no-kill watchdog
// never ended the run. The coordinator re-arms the run after every corpse run
// (`dc on`), and `dc on` clears the cleared-anchor set; the objectives then
// latch again as the party re-clears them. DetectKill read ANY change of the
// anchor count (and of the encounter mask) as a kill, so the drop to 0 and every
// re-latch stamped the clock. Its own header says the signals are that a mask or
// an anchor set GREW. Gained() holds that: a boss bit not seen before, or an
// anchor count above the run's high-water mark, and nothing else.

#include "gtest/gtest.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunProgress.h"

using DcRunProgress::Gained;
using DcRunProgress::Mark;

namespace
{
    constexpr std::uint32_t RUN = 1000;  // an instance id
}

TEST(DcRunProgressGained, ANewBossBitIsAKill)
{
    Mark m;
    EXPECT_TRUE(Gained(m, RUN, 0x1, 0));
    EXPECT_FALSE(Gained(m, RUN, 0x1, 0));
    EXPECT_TRUE(Gained(m, RUN, 0x3, 0));
}

TEST(DcRunProgressGained, ANewObjectiveIsAKillOnlyAboveTheHighWaterMark)
{
    Mark m;
    EXPECT_TRUE(Gained(m, RUN, 0, 1));
    EXPECT_TRUE(Gained(m, RUN, 0, 2));
    EXPECT_FALSE(Gained(m, RUN, 0, 2));
}

TEST(DcRunProgressGained, AReArmThatClearsAndRelatchesTheAnchorsIsNoKill)
{
    Mark m;
    ASSERT_TRUE(Gained(m, RUN, 0x1, 2));  // one boss, two objectives latched
    // `dc on` after a wipe: the set is cleared ...
    EXPECT_FALSE(Gained(m, RUN, 0x1, 0));
    // ... and the same objectives latch again, one by one.
    EXPECT_FALSE(Gained(m, RUN, 0x1, 1));
    EXPECT_FALSE(Gained(m, RUN, 0x1, 2));
    // A third objective, never reached before, is a real kill.
    EXPECT_TRUE(Gained(m, RUN, 0x1, 3));
}

TEST(DcRunProgressGained, AMaskThatDropsAndRegrowsOnlyCountsTheBitsNotSeenBefore)
{
    Mark m;
    ASSERT_TRUE(Gained(m, RUN, 0x3, 0));
    // The mask reading dropping is not a kill.
    EXPECT_FALSE(Gained(m, RUN, 0x1, 0));
    // A bit that was set before and comes back is a kill of that encounter again
    // (the instance reset it), which is progress the clock should see.
    EXPECT_TRUE(Gained(m, RUN, 0x3, 0));
}

TEST(DcRunProgressGained, ANewInstanceRestartsTheHighWaterMark)
{
    Mark m;
    ASSERT_TRUE(Gained(m, RUN, 0, 5));
    // A new copy of the dungeon: its first objective is a kill again.
    EXPECT_TRUE(Gained(m, RUN + 1, 0, 1));
    EXPECT_FALSE(Gained(m, RUN + 1, 0, 1));
}
