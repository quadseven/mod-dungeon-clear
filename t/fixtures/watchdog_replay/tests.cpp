/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// AI-REVIEW 2026-11-11 [gpt-6-luna (subscription)] DC121-E6:
// Drive the actual registered-owner sweep, not only its decision kernel.
class DcRegisteredWatchdogReplay : public ::testing::Test
{
protected:
    Map instance, outside;
    Group group;
    Player owner, rogue;
    std::array<Player, 3> ghosts;
    PlayerbotAI ownerAI, otherAI;
    void SetUp() override
    {
        // Reset the real sweep through its public tick, with no active owner.
        DcNoKillWatchdog::Tick(5000);
        instance.dungeon = true;
        outside.id = 0;
        owner.guid = 7;
        owner.map = &outside;
        owner.alive = false;
        group.Add(&owner);
        rogue.map = &instance;
        rogue.guid = 8;
        group.Add(&rogue);
        // AI-REVIEW 2026-11-11 [Codex/in-process/independent-review] DC121-REV-008:
        // Every roster member has a distinct identity, as in a real group.
        unsigned ghostGuid = 9;
        for (auto& ghost : ghosts)
        {
            ghost.guid = ghostGuid++;
            ghost.map = &outside;
            ghost.alive = false;
            group.Add(&ghost);
        }
        players[owner.GetGUID()] = &owner;
        ais[&owner] = &ownerAI;
        DcStatusPublisher::MarkActiveTank(owner.GetGUID());
        stopped = nullptr;
        foreignOwner = nullptr;
        stopReason.clear();
        standDown = false;
        chess = false;
    }
    void TearDown() override
    {
        DcStatusPublisher::UnmarkActiveTank(owner.GetGUID());
        players.clear();
        ais.clear();
    }
};
TEST_F(DcRegisteredWatchdogReplay, ReleasedDeadOwnerEndsItsOwnRunWithOneRogueInside)
{
    DcNoKillWatchdog::Tick(4999);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
    DcNoKillWatchdog::Tick(1);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_FALSE(ownerAI.run.enabled);
    EXPECT_TRUE(otherAI.run.enabled);
    EXPECT_EQ(stopReason, "Dungeon clear stopped: partial wipe (1 alive, 4 dead in the group; no "
                          "ready resurrector).");
}
TEST_F(DcRegisteredWatchdogReplay, FourLivingInsideDoNotLookLikeAWipeOnTheOwnersOutsideMap)
{
    for (auto& ghost : ghosts)
    {
        ghost.map = &instance;
        ghost.alive = true;
    }
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, ReadyResurrectionPreservesRecovery)
{
    rogue.resurrect = true;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, FullWipePreservesExistingRecovery)
{
    rogue.alive = false;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, PausedRunRemainsPaused)
{
    ownerAI.run.paused = true;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, RaidGroupIsExcluded)
{
    group.raid = true;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, SurvivorOutsideADungeonIsExcluded)
{
    rogue.map = &outside;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, UnavailableRegisteredOwnerIsNotVisited)
{
    owner.inWorld = false;
    for (auto& ghost : ghosts)
        ghost.inWorld = false;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}

TEST_F(DcRegisteredWatchdogReplay, AnotherInstanceOwnerCannotBeRetargeted)
{
    Map otherCopy;
    otherCopy.dungeon = true;
    Player otherOwner;
    otherOwner.map = &otherCopy;
    foreignOwner = &otherOwner;
    ais[&otherOwner] = &otherAI;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_FALSE(ownerAI.run.enabled);
    EXPECT_TRUE(otherAI.run.enabled);
}
TEST_F(DcRegisteredWatchdogReplay, EncounterStandDownKeepsTheRun)
{
    standDown = true;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, ChessHoldKeepsTheRun)
{
    chess = true;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, DisabledRegisteredContextIsUntouched)
{
    ownerAI.run.enabled = false;
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
    ownerAI.run.enabled = true;
    DcStatusPublisher::UnmarkActiveTank(owner.GetGUID());
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
}

TEST_F(DcRegisteredWatchdogReplay, PendingOfferPreservesRecoveryAfterTheSpellGoesOnCooldown)
{
    ghosts[0].map = &instance;
    ghosts[0].offerBy = rogue.guid;
    ASSERT_EQ(DcRezRecovery::KnownResurrectSpell(&rogue), nullptr);
    ASSERT_TRUE(DcRezRecovery::HoldsResurrectOffer(&rogue));
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
    ghosts[0].offerBy = 0; // the offer expires without acceptance
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_FALSE(ownerAI.run.enabled);
}
