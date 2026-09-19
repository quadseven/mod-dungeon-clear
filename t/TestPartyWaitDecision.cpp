/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcPartyWaitDecision.h"

using DcPartyWaitDecision::Decide;
using DcPartyWaitDecision::Inputs;
using DcPartyWaitDecision::Outcome;
using DcPartyWaitDecision::Reason;

namespace
{
    // A healthy five-member party, all present in the leader's own instance, one
    // of them a healer, three seconds into an ordinary between-pulls wait with a
    // three-minute ceiling. Individual tests take members out of the instance off
    // this base, which is exactly what the live failure did.
    Inputs Healthy()
    {
        Inputs in;
        in.waitedMs = 3000;
        in.timeoutMs = 180000;
        in.presentAlive = 5;
        in.presentHasHealer = true;
        in.rosterAlive = 5;
        in.rosterHasHealer = true;
        return in;
    }

    // Take `n` non-healers out of the instance: still in the group, not on the
    // leader's Map.
    void LeaveInstance(Inputs& in, std::uint32_t n)
    {
        in.presentAlive -= n;
    }
}

// --- the ordinary wait is still a wait ---------------------------------------

TEST(DcPartyWaitTest, AnIntactPartyJustKeepsWaiting)
{
    EXPECT_EQ(Decide(Healthy()).outcome, Outcome::Wait);
    EXPECT_EQ(Decide(Healthy()).reason, Reason::StillWaiting);
}

TEST(DcPartyWaitTest, ASmallPartyThatSTARTEDSmallIsNotCancelled)
{
    // Two members, both here, no healer in the comp at all. Nothing has been
    // lost, so this rung has no business ending the run: the viability test only
    // fires when the roster has actually shrunk.
    Inputs in = Healthy();
    in.presentAlive = 2;
    in.rosterAlive = 2;
    in.presentHasHealer = false;
    in.rosterHasHealer = false;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);

    in.presentAlive = 1;
    in.rosterAlive = 1;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

// --- continue with who is left: the point of the whole change ----------------

TEST(DcPartyWaitTest, LosingDpsKeepsTheRunGoing)
{
    // The live case: three of five pulled out of the instance by their owners'
    // clients. Tank and healer are still here, so four (or two) people finish the
    // dungeon rather than the run being thrown away for a client reconnect.
    Inputs in = Healthy();
    LeaveInstance(in, 1);
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);

    in = Healthy();
    LeaveInstance(in, 3);   // tank + healer left in the instance
    ASSERT_EQ(in.presentAlive, 2u);
    EXPECT_TRUE(in.presentHasHealer);
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

// --- ... but not into a composition that cannot finish -----------------------

TEST(DcPartyWaitTest, LosingTheOnlyHealerEndsTheRun)
{
    Inputs in = Healthy();
    LeaveInstance(in, 1);
    in.presentHasHealer = false;   // the one who left was the healer

    DcPartyWaitDecision::Result const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::EndRun);
    EXPECT_EQ(r.reason, Reason::LostHealer);
}

TEST(DcPartyWaitTest, AHealerlessCompDoesNotLoseAHealerItNeverHad)
{
    // Five DPS is a legitimate comp. "presentHasHealer == false" is its normal
    // state, and must not read as a loss when somebody unrelated leaves.
    Inputs in = Healthy();
    in.presentHasHealer = false;
    in.rosterHasHealer = false;
    LeaveInstance(in, 2);
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

TEST(DcPartyWaitTest, DroppingToASingleMemberEndsTheRun)
{
    Inputs in = Healthy();
    LeaveInstance(in, 4);          // only the tank is left
    ASSERT_EQ(in.presentAlive, 1u);
    in.presentHasHealer = false;

    DcPartyWaitDecision::Result const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::EndRun);
    // Too-few outranks lost-healer: "there is nobody here" is the more useful
    // thing to put in front of a human than "the healer is gone".
    EXPECT_EQ(r.reason, Reason::TooFewMembers);
}

TEST(DcPartyWaitTest, ViabilityIsJudgedBeforeTheClock)
{
    // An unviable party stops NOW, not in three minutes. Every one of those
    // minutes would be spent walking a tank at a pack it cannot survive.
    Inputs in = Healthy();
    in.waitedMs = 0;
    LeaveInstance(in, 1);
    in.presentHasHealer = false;
    EXPECT_EQ(Decide(in).outcome, Outcome::EndRun);
}

// --- the ceiling: the catch-all that makes termination a property ------------

TEST(DcPartyWaitTest, TheCeilingEndsAWaitNothingElseExplains)
{
    // Everyone present and correct, so the viability test says nothing; the wait
    // is simply never satisfied (a member wedged in this instance, an unreachable
    // rest floor, a latch that never releases).
    Inputs in = Healthy();
    in.waitedMs = 179999;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);

    in.waitedMs = 180000;
    DcPartyWaitDecision::Result const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::EndRun);
    EXPECT_EQ(r.reason, Reason::Timeout);
}

TEST(DcPartyWaitTest, ZeroTimeoutDisablesTheCeilingOnly)
{
    Inputs in = Healthy();
    in.timeoutMs = 0;
    in.waitedMs = 24u * 60u * 60u * 1000u;   // a day
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);

    // ... but it does NOT disable the viability check: an operator who turned the
    // ceiling off asked for a longer wait, not for a healerless party to keep
    // pulling.
    in.presentAlive = 4;
    in.presentHasHealer = false;
    EXPECT_EQ(Decide(in).outcome, Outcome::EndRun);
    EXPECT_EQ(Decide(in).reason, Reason::LostHealer);
}

TEST(DcPartyWaitTest, TheMinimumViablePartyIsTankPlusHealer)
{
    // Pins the constant the policy turns on, so a future change to it is a
    // deliberate one rather than a silent widening.
    EXPECT_EQ(DcPartyWaitDecision::kMinViableMembers, 2u);
}
