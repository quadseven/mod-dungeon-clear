/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Util/DcOldHillsbradBombs.h"

TEST(OldHillsbradBombs, NonTankCarrierLetsRequiredEventContinue)
{
    EXPECT_EQ(DcOldHillsbradBombs::Action::Continue,
              DcOldHillsbradBombs::Decide(false, true));
}

TEST(OldHillsbradBombs, EmptyPartyContinuesWithoutWaiting)
{
    EXPECT_EQ(DcOldHillsbradBombs::Action::ContinueWithoutBombs,
              DcOldHillsbradBombs::Decide(false, false));
}
