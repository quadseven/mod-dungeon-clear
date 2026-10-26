/*
 * DcCoreCompat::MoveKnockbackFrom on both core shapes (#80).
 *
 * The deployed core takes a fifth allowClientControlled argument; the previous
 * (fork) core takes four and skips client-controlled players, with
 * MoveKnockbackFromForPlayer as its unguarded copy. These fakes have exactly
 * those shapes, so the shim is checked for both cores in one build.
 */

#include "gtest/gtest.h"
#include "Ai/Dungeon/DungeonClear/Util/DcCoreCompat.h"

#include <string>

namespace
{
    struct FakeOwner
    {
        bool player = false;
        bool clientControlled = false;
        bool IsPlayer() const { return player; }
        bool IsClientControlled() const { return clientControlled; }
    };

    // Deployed core: one call with an allowClientControlled flag.
    struct FakeNewMotion
    {
        std::string called;
        bool allow = false;
        float xy = 0.0f;
        void MoveKnockbackFrom(float, float, float speedXY, float, bool allowClientControlled = false)
        {
            called = "MoveKnockbackFrom";
            allow = allowClientControlled;
            xy = speedXY;
        }
    };

    // Previous core: four arguments, plus the player-only unguarded variant.
    struct FakeOldMotion
    {
        std::string called;
        float xy = 0.0f;
        void MoveKnockbackFrom(float, float, float speedXY, float)
        {
            called = "MoveKnockbackFrom";
            xy = speedXY;
        }
        void MoveKnockbackFromForPlayer(float, float, float speedXY, float)
        {
            called = "MoveKnockbackFromForPlayer";
            xy = speedXY;
        }
    };
}

TEST(DcCoreCompatKnockback, NewCorePassesAllowClientControlled)
{
    FakeNewMotion motion;
    FakeOwner owner{true, true};
    DcCoreCompat::Detail::MoveKnockbackFrom(motion, &owner, 1.0f, 2.0f, 10.0f, 5.0f);
    EXPECT_EQ(motion.called, "MoveKnockbackFrom");
    EXPECT_TRUE(motion.allow);
    EXPECT_FLOAT_EQ(motion.xy, 10.0f);
}

TEST(DcCoreCompatKnockback, OldCoreClientControlledPlayerUsesForPlayer)
{
    FakeOldMotion motion;
    FakeOwner owner{true, true};
    DcCoreCompat::Detail::MoveKnockbackFrom(motion, &owner, 1.0f, 2.0f, 10.0f, 5.0f);
    EXPECT_EQ(motion.called, "MoveKnockbackFromForPlayer");
    EXPECT_FLOAT_EQ(motion.xy, 10.0f);
}

TEST(DcCoreCompatKnockback, OldCoreCreatureUsesFourArgCall)
{
    FakeOldMotion motion;
    FakeOwner owner{false, false};
    DcCoreCompat::Detail::MoveKnockbackFrom(motion, &owner, 1.0f, 2.0f, 10.0f, 5.0f);
    EXPECT_EQ(motion.called, "MoveKnockbackFrom");
}

TEST(DcCoreCompatKnockback, OldCoreServerControlledPlayerUsesFourArgCall)
{
    FakeOldMotion motion;
    FakeOwner owner{true, false};
    DcCoreCompat::Detail::MoveKnockbackFrom(motion, &owner, 1.0f, 2.0f, 10.0f, 5.0f);
    EXPECT_EQ(motion.called, "MoveKnockbackFrom");
}

TEST(DcCoreCompatKnockback, NullUnitIsANoOp)
{
    DcCoreCompat::MoveKnockbackFrom(nullptr, 1.0f, 2.0f, 10.0f, 5.0f);
    SUCCEED();
}
