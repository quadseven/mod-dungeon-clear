#include "gtest/gtest.h"
#include "DcCompletionLoot.h"
#include <limits>

namespace
{
    DcCompletionLoot::Inputs MissingKey()
    {
        DcCompletionLoot::Inputs in;
        in.started = true;
        in.needsKey = true;
        in.chestPresent = true;
        in.yards = 30.0f;
        in.lootRadius = 15.0f;
        return in;
    }
}

TEST(DcCompletionLoot, ApproachesTheOutOfRadiusStrongboxThenYieldsToOrdinaryLoot)
{
    auto in = MissingKey();
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Approach);
    in.walking = true;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Wait);
    in.walking = false;
    in.yards = 14.0f;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Loot);
    in.needsKey = false;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Finish);
}

TEST(DcCompletionLoot, PreservesCombatAndFinalBossLootBeforeApproaching)
{
    auto in = MissingKey();
    in.mayAct = false;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Wait);
    in.mayAct = true;
    in.busy = true;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Wait);
}

TEST(DcCompletionLoot, ADespawningChestCannotKeepTheDriveForever)
{
    auto in = MissingKey();
    in.chestPresent = false;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Wait);
    in.elapsedMs = DcCompletionLoot::BUDGET_MS;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::TimedOut);
    in.started = false;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Finish);
}

TEST(DcCompletionLoot, RefusesDistantAndInvalidMovement)
{
    auto in = MissingKey();
    for (float yards : {41.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        in.yards = yards;
        EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Finish);
    }
    in.yards = 30.0f;
    in.lootRadius = 0.0f;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Finish);
}

TEST(DcCompletionLoot, UsesTheOrdinaryLootKeyCatalogue)
{
    EXPECT_EQ(DcCompletionLoot::KeyForChest(103821), 7146u);
    EXPECT_EQ(DcCompletionLoot::KeyForChest(179516), 18266u);
    EXPECT_EQ(DcCompletionLoot::KeyForChest(12345), 0u);
}

// AI-REVIEW 2026-10-31 [poolside/laguna-s-2.1] L1 (PR #92):
// An already-owned key must finish before any approach, not only after looting.
TEST(DcCompletionLoot, AlreadyOwnedKeyNeverStartsAnApproach)
{
    auto in = MissingKey();
    in.started = false;
    in.needsKey = false;
    EXPECT_EQ(DcCompletionLoot::Decide(in), DcCompletionLoot::Step::Finish);
}
