/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "TestRun/DcTestComp.h"
#include "TestRun/DcTestStartOptions.h"

#include <cstdint>
#include <string>
#include <vector>

using DcTestComp::Roster;
using DcTestStartOptions::Anchor;
using DcTestStartOptions::CheckExclude;
using DcTestStartOptions::ParseExclude;
using DcTestStartOptions::Parser;
using DcTestStartOptions::ResolveFrom;
using DcTestStartOptions::StartPlan;

namespace
{
    Anchor Boss(char const* name, std::uint32_t entry)
    {
        Anchor a;
        a.name = name;
        a.entry = entry;
        return a;
    }

    Anchor Objective(char const* name, std::uint32_t entry, float x, float y, float z)
    {
        Anchor a;
        a.name = name;
        a.entry = entry;
        a.isBoss = false;
        a.hasStart = true;
        a.x = x;
        a.y = y;
        a.z = z;
        return a;
    }

    // The Deadmines as the run walks it: the Iron Clad Door objective sits
    // between Gilnid and the ship (BossRosterRegistry, map 36).
    std::vector<Anchor> Deadmines()
    {
        return {
            Boss("Rhahk'Zor", 644),
            Boss("Sneed's Shredder", 643),
            Boss("Gilnid", 1763),
            Objective("Iron Clad Door", 0x4F000001u, -107.56f, -659.67f, 7.21f),
            Boss("Mr. Smite", 646),
            Boss("Captain Greenskin", 647),
            Boss("Edwin VanCleef", 639),
        };
    }
}

// --- exclude= ---------------------------------------------------------------

TEST(DcTestStartOptions, ParseExcludeBuildsAMask)
{
    std::uint32_t mask = 0;
    std::string err;
    ASSERT_TRUE(ParseExclude("rogue,Mage,dk", &mask, &err)) << err;
    EXPECT_EQ(mask, DcTestComp::ClassBit(4) | DcTestComp::ClassBit(8) | DcTestComp::ClassBit(6));
}

TEST(DcTestStartOptions, ParseExcludeRefusesAnUnknownClass)
{
    std::uint32_t mask = 0;
    std::string err;
    EXPECT_FALSE(ParseExclude("rogue,bard", &mask, &err));
    EXPECT_NE(err.find("bard"), std::string::npos);
    EXPECT_NE(err.find("warlock"), std::string::npos) << "the valid names are listed";
}

// A roster that cannot fill a role without the excluded classes is refused at
// parse time, and the message names the role.
TEST(DcTestStartOptions, ExcludeThatEmptiesARoleIsRefusedNamingIt)
{
    std::uint32_t mask = 0;
    std::string err;
    ASSERT_TRUE(ParseExclude("warrior,paladin,druid", &mask, &err)) << err;
    EXPECT_FALSE(CheckExclude(mask, Roster::NoDeathKnights, 0, &err));
    EXPECT_NE(err.find("tank"), std::string::npos) << err;

    ASSERT_TRUE(ParseExclude("priest,paladin,shaman,druid", &mask, &err)) << err;
    EXPECT_FALSE(CheckExclude(mask, Roster::NoDeathKnights, 0, &err));
    EXPECT_NE(err.find("heal"), std::string::npos) << err;
}

TEST(DcTestStartOptions, ExcludeThatLeavesEveryRoleIsAccepted)
{
    std::uint32_t mask = 0;
    std::string err;
    ASSERT_TRUE(ParseExclude("rogue", &mask, &err)) << err;
    EXPECT_TRUE(CheckExclude(mask, Roster::NoDeathKnights, 0, &err)) << err;
    EXPECT_TRUE(CheckExclude(mask, Roster::WithDeathKnights, 25, &err)) << err;
}

// --- from= ------------------------------------------------------------------

// A valid boss sets the start boss and counts every earlier boss as skipped.
// The objective in front of the boss is NOT skipped: it gates the boss, so the
// run starts at it and still performs it.
TEST(DcTestStartOptions, FromBossSkipsEarlierBossesAndKeepsTheGatingObjective)
{
    StartPlan plan;
    std::string err;
    ASSERT_TRUE(ResolveFrom(Deadmines(), "Mr. Smite", &plan, &err)) << err;
    EXPECT_EQ(plan.bossName, "Mr. Smite");
    EXPECT_EQ(plan.skipEntries, (std::vector<std::uint32_t>{644, 643, 1763}));
    EXPECT_FALSE(plan.atEntrance);
    EXPECT_FLOAT_EQ(plan.x, -107.56f);
    EXPECT_FLOAT_EQ(plan.y, -659.67f);
    EXPECT_FLOAT_EQ(plan.z, 7.21f);
}

TEST(DcTestStartOptions, FromTheFirstBossStartsAtTheEntranceSkippingNothing)
{
    StartPlan plan;
    std::string err;
    ASSERT_TRUE(ResolveFrom(Deadmines(), "rhahk'zor", &plan, &err)) << err;
    EXPECT_TRUE(plan.atEntrance);
    EXPECT_TRUE(plan.skipEntries.empty());
}

TEST(DcTestStartOptions, FromMatchesCaseBlindAndByUniqueSubstring)
{
    StartPlan plan;
    std::string err;
    ASSERT_TRUE(ResolveFrom(Deadmines(), "SMITE", &plan, &err)) << err;
    EXPECT_EQ(plan.bossName, "Mr. Smite");
    ASSERT_TRUE(ResolveFrom(Deadmines(), "mr._smite", &plan, &err)) << err;
    EXPECT_EQ(plan.bossName, "Mr. Smite");
}

// An unknown boss is refused with the valid names, bosses only.
TEST(DcTestStartOptions, FromAnUnknownBossListsTheValidNames)
{
    StartPlan plan;
    std::string err;
    EXPECT_FALSE(ResolveFrom(Deadmines(), "Hogger", &plan, &err));
    EXPECT_NE(err.find("Hogger"), std::string::npos);
    for (char const* name : {"Rhahk'Zor", "Sneed's Shredder", "Gilnid", "Mr. Smite",
                             "Captain Greenskin", "Edwin VanCleef"})
        EXPECT_NE(err.find(name), std::string::npos) << name;
    EXPECT_EQ(err.find("Iron Clad Door"), std::string::npos) << "objectives are not bosses";
}

TEST(DcTestStartOptions, FromAnAmbiguousNameIsRefused)
{
    std::vector<Anchor> roster = {Boss("Hearthsinger Forresten", 1), Boss("Hearthsinger Nerub", 2)};
    StartPlan plan;
    std::string err;
    EXPECT_FALSE(ResolveFrom(roster, "Hearthsinger", &plan, &err));
    EXPECT_NE(err.find("several"), std::string::npos) << err;
}

// A boss with nothing in front of it that marks a spot cannot be started at: the
// refusal names the boss rather than dropping the party somewhere unsafe.
TEST(DcTestStartOptions, FromABossWithNoKnownStartSpotIsRefused)
{
    StartPlan plan;
    std::string err;
    EXPECT_FALSE(ResolveFrom(Deadmines(), "Captain Greenskin", &plan, &err));
    EXPECT_NE(err.find("Captain Greenskin"), std::string::npos) << err;
}

// --- the word parser ----------------------------------------------------------

// A boss name with spaces arrives as several words; the parser joins the ones
// that follow from= and leaves the caller's own bare words alone.
TEST(DcTestStartOptions, ParserJoinsABossNameAcrossWords)
{
    Parser p;
    std::string err;
    EXPECT_EQ(p.Consume("exclude=rogue", false, &err), Parser::Result::Consumed);
    EXPECT_EQ(p.Consume("from=Mr.", false, &err), Parser::Result::Consumed);
    EXPECT_EQ(p.Consume("Smite", false, &err), Parser::Result::Consumed);
    EXPECT_EQ(p.Consume("heroic", true, &err), Parser::Result::NotMine);
    EXPECT_EQ(p.Consume("seed=5", false, &err), Parser::Result::NotMine);
    EXPECT_EQ(p.Consume("extra", false, &err), Parser::Result::NotMine) << "after seed= it is not a name";
    EXPECT_EQ(p.options().startBoss, "Mr. Smite");
    EXPECT_EQ(p.options().excludeMask, DcTestComp::ClassBit(4));
}

TEST(DcTestStartOptions, ParserRefusesABadClassAndAnEmptyBoss)
{
    Parser p;
    std::string err;
    EXPECT_EQ(p.Consume("exclude=bard", false, &err), Parser::Result::Error);
    EXPECT_FALSE(err.empty());
    EXPECT_EQ(p.Consume("from=", false, &err), Parser::Result::Error);
}
