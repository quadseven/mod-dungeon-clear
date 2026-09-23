/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "DcLogThrottle.h"

// (neverLogged, textChanged, sinceLastMs, intervalMs)
using DcLogThrottle::ShouldLog;

TEST(DcLogThrottle, FirstLineOfAWaitAlwaysGoesOut)
{
    EXPECT_TRUE(ShouldLog(true, false, 0, 30000));
}

TEST(DcLogThrottle, SameReasonInsideTheIntervalIsQuiet)
{
    // The measured flood: the same "waiting on Oz (out of range)" every tick.
    EXPECT_FALSE(ShouldLog(false, false, 16, 30000));
    EXPECT_FALSE(ShouldLog(false, false, 29999, 30000));
}

TEST(DcLogThrottle, ANewReasonIsNewsAtOnce)
{
    EXPECT_TRUE(ShouldLog(false, true, 16, 30000));
}

TEST(DcLogThrottle, AStillRunningWaitGetsAReminder)
{
    EXPECT_TRUE(ShouldLog(false, false, 30000, 30000));
}

static_assert(!ShouldLog(false, false, 100, 30000), "same text, early: quiet");
static_assert(ShouldLog(false, true, 100, 30000), "new text: logged");
