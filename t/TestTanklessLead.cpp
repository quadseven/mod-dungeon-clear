/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "DcTanklessLead.h"

// Who drives a party with no tank. FindLeaderTank consults this kernel only
// after no tank BOT won the election, so every case below is already "no tank
// bot"; what varies is whether any tank at all is present and whether the
// group leader can drive.
//
// Argument order, once:
//   (isRaid, anyTankOnMap, groupLeaderIsBot, groupLeaderAlive, groupLeaderOnMap)
using DcTanklessLead::GroupLeaderDrives;

// The measured party: warrior (fury), mage, priest, cat druid, shaman. Nobody
// carries a tank strategy, the leader is a bot on the map. It drives.
TEST(DcTanklessLead, TanklessPartyIsDrivenByItsBotLeader)
{
    EXPECT_TRUE(GroupLeaderDrives(false, false, true, true, true));
}

// A person tanking blocks it: the party is being led through the dungeon by a
// human, and a bot must not start driving over them.
TEST(DcTanklessLead, HumanTankOnTheMapBlocksIt)
{
    EXPECT_FALSE(GroupLeaderDrives(false, true, true, true, true));
}

// A human group leader has no PlayerbotAI to run the ladder.
TEST(DcTanklessLead, HumanGroupLeaderCannotDrive)
{
    EXPECT_FALSE(GroupLeaderDrives(false, false, false, true, true));
}

// A dead leader, or one outside this instance copy, cannot drive it. The
// latter is the cross-thread write #20 closed for tanks; it stays closed.
TEST(DcTanklessLead, DeadOrElsewhereLeaderCannotDrive)
{
    EXPECT_FALSE(GroupLeaderDrives(false, false, true, false, true));
    EXPECT_FALSE(GroupLeaderDrives(false, false, true, true, false));
}

// Raids keep their own election; a tankless raid is not guessed at.
TEST(DcTanklessLead, RaidsAreNotCovered)
{
    EXPECT_FALSE(GroupLeaderDrives(true, false, true, true, true));
}

static_assert(GroupLeaderDrives(false, false, true, true, true), "tankless party, bot leader");
static_assert(!GroupLeaderDrives(false, true, true, true, true), "a human tank blocks it");
static_assert(!GroupLeaderDrives(true, false, true, true, true), "raids excluded");
