/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcSourceScan.h"

#include <cstdint>
#include <string>

#include "Ai/Dungeon/DungeonClear/Util/DcAtBossGate.h"

// ---------------------------------------------------------------------------
// The Emperor room, 2026-11-12 (run 1886163): 18 of 19 bosses down, the room's
// guards down to three, all five members alive and the tank at the boss, and the
// party stood for twenty minutes on "at-boss gate down: 'room trash'
// [partyReady=0 valve=0/40000]". The between-pulls wait's ceiling lives below the
// at-boss hold in the advance ladder and the hold returned first; the valve
// counts only while the party is ready; the room-clear give-up clock pauses while
// it is not. Nothing ended the hold until the stuck-run watchdog walked the party
// out. At a dungeon boss, a party that is not ready at the room-trash or
// party-not-ready gate now runs the wait from the hold.
// ---------------------------------------------------------------------------
namespace
{
    using DcAtBossGate::Gate;
    using DcAtBossGate::HoldRunsPartyWait;
}

TEST(AtBossHoldWait, RoomTrashHoldWithAnUnreadyPartyRunsTheWait)
{
    EXPECT_TRUE(HoldRunsPartyWait(Gate::RoomTrash, /*raid*/ false, /*partyReady*/ false));
}

TEST(AtBossHoldWait, PartyNotReadyHoldRunsTheWait)
{
    EXPECT_TRUE(HoldRunsPartyWait(Gate::PartyNotReady, false, false));
}

TEST(AtBossHoldWait, AReadyPartyDoesNotRunTheWait)
{
    // Ready: the valve is counting (or the gate is open); the wait has nothing to bound.
    EXPECT_FALSE(HoldRunsPartyWait(Gate::RoomTrash, false, true));
    EXPECT_FALSE(HoldRunsPartyWait(Gate::PartyNotReady, false, true));
}

TEST(AtBossHoldWait, ARaidKeepsItsOwnMusterClock)
{
    EXPECT_FALSE(HoldRunsPartyWait(Gate::RoomTrash, true, false));
    EXPECT_FALSE(HoldRunsPartyWait(Gate::PartyNotReady, true, false));
}

TEST(AtBossHoldWait, OtherGatesAreNotAReadinessWait)
{
    for (Gate g : {Gate::Open, Gate::NotDriving, Gate::NotBossAnchor, Gate::EncounterHold,
                   Gate::PullOwns, Gate::NotAtEngage, Gate::ClosedDoor, Gate::AnchoredHops,
                   Gate::SealedMuster, Gate::RezPending, Gate::RaidMuster})
        EXPECT_FALSE(HoldRunsPartyWait(g, false, false)) << DcAtBossGate::Name(g);
}

// The call site: the at-boss hold must actually ask. Without this the predicate
// above could pass while the hold still parks the tank for good.
TEST(AtBossHoldWait, TheAtBossHoldCallsTheWaitThroughThePredicate)
{
    std::string const code = DcSourceScan::StripComments(DcSourceScan::ReadFile(
        DcSourceScan::ModuleSrc() / "Ai/Dungeon/DungeonClear/Action/DcAdvanceAction.cpp"));
    std::size_t const begin = code.find("DungeonClearAdvanceAction::TryEngageHold(");
    ASSERT_NE(begin, std::string::npos);
    std::size_t const end = code.find("DungeonClearAdvanceAction::TryEngageWalkYield(", begin);
    ASSERT_NE(end, std::string::npos);
    std::string const body = code.substr(begin, end - begin);

    auto const ask = DcSourceScan::CallOffsets(body, "DcAtBossGate::HoldRunsPartyWait");
    auto const wait = DcSourceScan::CallOffsets(body, "TryBetweenPullsRest");
    ASSERT_EQ(ask.size(), 1u);
    ASSERT_EQ(wait.size(), 1u);
    EXPECT_LT(ask[0], wait[0]);
    // ... and before the hold parks the tank for good.
    std::size_t const park = body.rfind("DcMovement::StopBot(bot, DcMovement::Stop::Hold);");
    ASSERT_NE(park, std::string::npos);
    EXPECT_LT(wait[0], park);
}
