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
        replayNowMs = 0;
        replayRecoveryGraceSecs = 90;
        replayOwnerGrace.clear();
        stopped = nullptr;
        foreignOwner = nullptr;
        stopReason.clear();
        standDown = false;
        chess = false;
    }
    void Sweep(unsigned elapsed)
    {
        while (elapsed > 5000)
        {
            replayNowMs += 5000;
            DcNoKillWatchdog::Tick(5000);
            elapsed -= 5000;
        }
        replayNowMs += elapsed;
        DcNoKillWatchdog::Tick(elapsed);
    }
    void TearDown() override
    {
        DcStatusPublisher::UnmarkActiveTank(owner.GetGUID());
        players.clear();
        ais.clear();
    }
};
TEST_F(DcRegisteredWatchdogReplay, ReleasedDeadOwnerReceivesRecoveryGraceBeforeItsOwnRunEnds)
{
    Sweep(4999);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
    Sweep(1);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
    Sweep(85000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_FALSE(ownerAI.run.enabled);
    EXPECT_TRUE(otherAI.run.enabled);
    EXPECT_NE(stopReason.find("recovery grace 90s"), std::string::npos);
}
TEST_F(DcRegisteredWatchdogReplay, FourDeathsWithOneDungeonSurvivorEndWithinRecoveryBound)
{
    // The registered tank is dead outside the instance; only the rogue remains
    // alive inside. The first sweep observes the partial wipe, then the same
    // production watchdog must end this unrecoverable run within its 90s grace.
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(85000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_LE(replayNowMs, 95000u);
    EXPECT_FALSE(ownerAI.run.enabled);
}
TEST_F(DcRegisteredWatchdogReplay, FourLivingInsideDoNotLookLikeAWipeOnTheOwnersOutsideMap)
{
    for (auto& ghost : ghosts)
    {
        ghost.map = &instance;
        ghost.alive = true;
    }
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, ReadyResurrectionPreservesRecovery)
{
    rogue.resurrect = true;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, FullWipePreservesExistingRecovery)
{
    rogue.alive = false;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, PausedRunRemainsPaused)
{
    ownerAI.run.paused = true;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, RaidGroupIsExcluded)
{
    group.raid = true;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, SurvivorOutsideADungeonIsExcluded)
{
    rogue.map = &outside;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, UnavailableRegisteredOwnerIsNotVisited)
{
    owner.inWorld = false;
    for (auto& ghost : ghosts)
        ghost.inWorld = false;
    Sweep(5000);
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
    Sweep(5000);
    Sweep(90000);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_FALSE(ownerAI.run.enabled);
    EXPECT_TRUE(otherAI.run.enabled);
}
TEST_F(DcRegisteredWatchdogReplay, EncounterStandDownKeepsTheRun)
{
    standDown = true;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, ChessHoldKeepsTheRun)
{
    chess = true;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
}
TEST_F(DcRegisteredWatchdogReplay, DisabledRegisteredContextIsUntouched)
{
    ownerAI.run.enabled = false;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    ownerAI.run.enabled = true;
    DcStatusPublisher::UnmarkActiveTank(owner.GetGUID());
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
}

TEST_F(DcRegisteredWatchdogReplay, PendingOfferPreservesRecoveryAfterTheSpellGoesOnCooldown)
{
    ghosts[0].map = &instance;
    ghosts[0].offerBy = rogue.guid;
    ASSERT_EQ(DcRezRecovery::KnownResurrectSpell(&rogue), nullptr);
    ASSERT_TRUE(DcRezRecovery::HoldsResurrectOffer(&rogue));
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
    ghosts[0].offerBy = 0; // the offer expires without acceptance
    Sweep(5000);
    Sweep(90000);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_FALSE(ownerAI.run.enabled);
}

TEST_F(DcRegisteredWatchdogReplay, RecoveryDuringGraceCancelsTheEnding)
{
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    ghosts[0].alive = true;
    Sweep(45000);
    ghosts[0].alive = false;
    Sweep(45000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(90000);
    EXPECT_EQ(stopped, &ownerAI);
}
TEST_F(DcRegisteredWatchdogReplay, LoadingMembersPreventACompleteWipeObservation)
{
    ghosts[0].inWorld = false;
    ghosts[1].inWorld = false;
    Sweep(5000);
    Sweep(120000);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
}
TEST_F(DcRegisteredWatchdogReplay, MissingGroupReferencePreventsACompleteWipeObservation)
{
    group.refs[4].player = nullptr;
    Sweep(5000);
    Sweep(120000);
    EXPECT_EQ(stopped, nullptr);
    EXPECT_TRUE(ownerAI.run.enabled);
}
TEST_F(DcRegisteredWatchdogReplay, OrdinaryNoKillRetainsTheExistingResolvedOwnerTarget)
{
    owner.alive = true;
    // AI-REVIEW 2026-11-11 [Codex/in-process/root-review] DC121-REV-013:
    // A real next-boss context requires the registered owner inside the dungeon.
    owner.map = &instance;
    for (auto& ghost : ghosts)
        ghost.alive = true;
    Player otherOwner;
    otherOwner.guid = 99;
    otherOwner.map = &instance;
    otherOwner.group = &group;
    group.refs[2].player = &otherOwner; // an existing live group slot
    foreignOwner = &otherOwner;
    ais[&otherOwner] = &otherAI;
    ownerAI.context.next = DungeonBossInfo{"next boss"};
    ownerAI.run.killProgress.stampMs = 1000;
    replayNowMs = 31 * 60000; // one ordinary ending observation, not repeated post-stop ticks
    DcNoKillWatchdog::Tick(5000);
    EXPECT_EQ(stopped, &otherAI);
    EXPECT_FALSE(otherAI.run.enabled);
    EXPECT_TRUE(ownerAI.run.enabled);
}

TEST_F(DcRegisteredWatchdogReplay, LargeConfiguredGraceIsCappedBelowFiveMinutes)
{
    replayRecoveryGraceSecs = 10000000;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(235000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_EQ(replayNowMs, 245000u);
    EXPECT_NE(stopReason.find("recovery grace 240s"), std::string::npos);
}
TEST_F(DcRegisteredWatchdogReplay, ZeroConfiguredGraceRetainsTheDefaultRecoveryWindow)
{
    replayRecoveryGraceSecs = 0;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(85000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
}
TEST_F(DcRegisteredWatchdogReplay, RecoveryWindowIsWrapSafe)
{
    replayNowMs = 0xffffffffu - 10000;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(85000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
}
TEST_F(DcRegisteredWatchdogReplay, PendingOfferRestartsTheRecoveryWindow)
{
    Sweep(5000);
    ghosts[0].map = &instance;
    ghosts[0].offerBy = rogue.guid;
    Sweep(45000);
    ghosts[0].offerBy = 0;
    Sweep(5000);
    Sweep(85000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
}

TEST_F(DcRegisteredWatchdogReplay, OwnerTransitCannotCarryAnOldRecoveryClock)
{
    Sweep(5000);
    owner.inWorld = false;
    Sweep(120000);
    owner.inWorld = true;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(85000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
}
TEST_F(DcRegisteredWatchdogReplay, MissedObservationsRestartTheRecoveryWindow)
{
    Sweep(5000);
    replayNowMs += 30000; // no world watchdog observation during this interval
    Sweep(5000);
    Sweep(85000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
}

TEST_F(DcRegisteredWatchdogReplay, GraceUsesTheRegisteredOwnersSettingWithoutLeaderResolution)
{
    Player otherOwner;
    otherOwner.guid = 99;
    foreignOwner = &otherOwner;
    replayOwnerGrace[otherOwner.guid] = 240;
    replayOwnerGrace[owner.guid] = 20;
    Sweep(5000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(15000);
    EXPECT_EQ(stopped, nullptr);
    Sweep(5000);
    EXPECT_EQ(stopped, &ownerAI);
    EXPECT_NE(stopReason.find("recovery grace 20s"), std::string::npos);
}
