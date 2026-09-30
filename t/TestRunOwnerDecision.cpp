/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option) any later version.
 */

#include "gtest/gtest.h"
#include "DcRunOwnerDecision.h"

using DcRunOwnerDecision::Candidate;
using DcRunOwnerDecision::IsOwnerForInstance;

TEST(DcRunOwnerDecision, ReleasedOwnerRemainsBoundToTheInstanceHoldingItsCorpse)
{
    Candidate const owner{true, false, 1, 0, true, 389, 389, 42};

    EXPECT_TRUE(IsOwnerForInstance(owner, 389, 42));
}

TEST(DcRunOwnerDecision, ReleasedOwnerCannotBeResolvedFromAnotherDungeonCopy)
{
    Candidate const owner{true, false, 1, 0, true, 389, 389, 42};

    EXPECT_FALSE(IsOwnerForInstance(owner, 389, 43));
}

TEST(DcRunOwnerDecision, CorpseOutsideTheRunMapDoesNotCarryOwnershipIntoTheDungeon)
{
    Candidate const owner{true, false, 1, 0, true, 1, 389, 42};

    EXPECT_FALSE(IsOwnerForInstance(owner, 389, 42));
}

TEST(DcRunOwnerDecision, UnenabledMemberDoesNotBecomeTheRunOwner)
{
    Candidate const owner{false, true, 389, 42, false, 0, 389, 42};

    EXPECT_FALSE(IsOwnerForInstance(owner, 389, 42));
}
