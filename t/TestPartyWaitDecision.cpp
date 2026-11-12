/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcPartyWaitDecision.h"
#include <string>

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

// --- the rescue: at the stranded clock, before the party-wait ceiling --------

namespace
{
    Inputs HealthyWithStranded()
    {
        Inputs in = Healthy();
        in.strandedOutOfRange = 2;  // two bots out of spread of the tank
        in.strandedRecoveryEnabled = true;
        return in;
    }
}

TEST(DcPartyWaitTest, CeilingWithStrandedMembersAndRescueOnRescues)
{
    // At the 180s ceiling with two out-of-range members and StrandedRecovery on:
    // rescue instead of ending the run.
    Inputs in = HealthyWithStranded();
    in.waitedMs = 180000;
    in.strandedTimeoutMs = 60000;
    DcPartyWaitDecision::Result const r = Decide(in);
    EXPECT_EQ(r.outcome, DcPartyWaitDecision::Outcome::Rescue);
    EXPECT_EQ(r.reason, DcPartyWaitDecision::Reason::StillWaiting);
}

TEST(DcPartyWaitTest, CeilingWithStrandedMembersButRescueOffTimesOut)
{
    // Same ceiling, same strays, but StrandedRecovery is off: the run ends.
    Inputs in = HealthyWithStranded();
    in.strandedRecoveryEnabled = false;
    in.waitedMs = 180000;
    DcPartyWaitDecision::Result const r = Decide(in);
    EXPECT_EQ(r.outcome, DcPartyWaitDecision::Outcome::EndRun);
    EXPECT_EQ(r.reason, DcPartyWaitDecision::Reason::Timeout);
}

TEST(DcPartyWaitTest, CeilingWithoutStrandedMembersTimesOut)
{
    // Everyone in range, so there is nothing to rescue: the run ends.
    Inputs in = Healthy();
    in.waitedMs = 180000;
    DcPartyWaitDecision::Result const r = Decide(in);
    EXPECT_EQ(r.outcome, DcPartyWaitDecision::Outcome::EndRun);
    EXPECT_EQ(r.reason, DcPartyWaitDecision::Reason::Timeout);
}

TEST(DcPartyWaitTest, RescueDoesNotFireBeforeTheStrandedClock)
{
    // Strays present, rescue on, but the stranded clock has not elapsed: wait.
    Inputs in = HealthyWithStranded();
    in.strandedTimeoutMs = 60000;
    in.waitedMs = 59999;
    EXPECT_EQ(Decide(in).outcome, DcPartyWaitDecision::Outcome::Wait);
}

TEST(DcPartyWaitTest, RescueFiresAtStrandedClockBeforePartyWaitCeiling)
{
    Inputs in = HealthyWithStranded();
    in.strandedTimeoutMs = 60000;
    in.waitedMs = 60000;
    EXPECT_EQ(Decide(in).outcome, DcPartyWaitDecision::Outcome::Rescue);
}

TEST(DcPartyWaitTest, ZeroTimeoutMeansNoRescueEither)
{
    // With the ceiling disabled, a wait that never ends is not rescued - there is
    // no ceiling to rescue at.
    Inputs in = HealthyWithStranded();
    in.timeoutMs = 0;
    in.waitedMs = 24u * 60u * 60u * 1000u;  // a day
    EXPECT_EQ(Decide(in).outcome, DcPartyWaitDecision::Outcome::Wait);
}

TEST(DcPartyWaitTest, ViabilityStillOutranksRescue)
{
    // An unviable party stops now even if there are strays and rescue is on: the
    // viability check runs first, without reference to the clock.
    Inputs in = HealthyWithStranded();
    in.waitedMs = 0;
    LeaveInstance(in, 1);
    in.presentHasHealer = false;  // the one who left was the healer
    DcPartyWaitDecision::Result const r = Decide(in);
    EXPECT_EQ(r.outcome, DcPartyWaitDecision::Outcome::EndRun);
    EXPECT_EQ(r.reason, DcPartyWaitDecision::Reason::LostHealer);
}

// Criterion 2 (issue #66): a second party-wait timeout at the same boss for the
// same members ends the run with a distinct reason that names the boss and the
// members. The memory is kept outside DcApproachState because the stop resets it.
TEST(DcPartyWaitTest, FirstTimeoutIsNotARepeat)
{
    DcPartyWaitDecision::TimeoutMemory mem;
    EXPECT_FALSE(DcPartyWaitDecision::IsRepeatTimeout(mem, 6243u, "Waiting on Gorvash (out of range)"));
    std::string const r = DcPartyWaitDecision::TimeoutStopReason(
        false, 180, "Gelihast", "Waiting on Gorvash (out of range)");
    EXPECT_NE(r.find("the party never became ready after 180s"), std::string::npos);
    EXPECT_EQ(r.find("RepeatedTimeout"), std::string::npos);
}

TEST(DcPartyWaitTest, SecondIdenticalTimeoutIsARepeatNamingBossAndMembers)
{
    std::string const why = "Waiting on Gorvash (out of range), Grassfed (out of range)";
    DcPartyWaitDecision::TimeoutMemory mem;
    mem.bossEntry = 6243u;  // Gelihast
    mem.why = why;
    ASSERT_TRUE(DcPartyWaitDecision::IsRepeatTimeout(mem, 6243u, why));

    // The ceiling itself still says Timeout; the repeat is told apart by the memory.
    Inputs in = Healthy();
    in.waitedMs = in.timeoutMs;
    EXPECT_EQ(Decide(in).reason, DcPartyWaitDecision::Reason::Timeout);

    std::string const r = DcPartyWaitDecision::TimeoutStopReason(true, 180, "Gelihast", why);
    EXPECT_NE(r.find("RepeatedTimeout"), std::string::npos);
    EXPECT_NE(r.find("Gelihast"), std::string::npos);
    EXPECT_NE(r.find("Gorvash"), std::string::npos);
    EXPECT_NE(r.find("Grassfed"), std::string::npos);
    EXPECT_EQ(r.find("the party never became ready after"), std::string::npos);
}

TEST(DcPartyWaitTest, DifferentBossOrDifferentMembersIsNotARepeat)
{
    DcPartyWaitDecision::TimeoutMemory mem;
    mem.bossEntry = 6243u;
    mem.why = "Waiting on Gorvash (out of range)";
    EXPECT_FALSE(DcPartyWaitDecision::IsRepeatTimeout(mem, 4887u, mem.why));
    EXPECT_FALSE(DcPartyWaitDecision::IsRepeatTimeout(mem, 6243u, "Waiting on Grassfed (out of range)"));
}

// --- the rescue is bounded: one per wait -------------------------------------

TEST(DcPartyWaitTest, RescueAlreadyFiredThisWaitEndsAtTheCeiling)
{
    // A rescue already fired at this boss in this wait and the strays are still
    // out of range: the ceiling ends the run instead of rescuing forever.
    Inputs in = HealthyWithStranded();
    in.strandedTimeoutMs = 60000;
    in.waitedMs = in.timeoutMs;
    in.rescueAlreadyFired = true;
    DcPartyWaitDecision::Result const r = Decide(in);
    EXPECT_EQ(r.outcome, DcPartyWaitDecision::Outcome::EndRun);
    EXPECT_EQ(r.reason, DcPartyWaitDecision::Reason::Timeout);
}

TEST(DcPartyWaitTest, RescueAlreadyFiredThisWaitKeepsWaitingBelowTheCeiling)
{
    // Past the stranded clock but short of the ceiling: no second rescue, just wait.
    Inputs in = HealthyWithStranded();
    in.strandedTimeoutMs = 60000;
    in.waitedMs = 120000;
    in.rescueAlreadyFired = true;
    EXPECT_EQ(Decide(in).outcome, DcPartyWaitDecision::Outcome::Wait);
}

TEST(DcPartyWaitTest, RescueMemoryAllowsOneRescuePerWait)
{
    DcPartyWaitDecision::TimeoutMemory mem;
    EXPECT_FALSE(DcPartyWaitDecision::RescueFiredThisWait(mem, 6243u, 1000u));
    DcPartyWaitDecision::RecordRescue(mem, 6243u, 1000u);
    EXPECT_TRUE(DcPartyWaitDecision::RescueFiredThisWait(mem, 6243u, 1000u));

    // A new wait (the run re-enabled, or the party became ready and then not) or
    // another boss gets its own rescue.
    EXPECT_FALSE(DcPartyWaitDecision::RescueFiredThisWait(mem, 6243u, 2000u));
    EXPECT_FALSE(DcPartyWaitDecision::RescueFiredThisWait(mem, 4887u, 1000u));

    // Recording a timeout leaves the rescue record alone.
    DcPartyWaitDecision::RecordTimeout(mem, 6243u, "Gelihast", 180, "Waiting on Gorvash (out of range)");
    EXPECT_TRUE(DcPartyWaitDecision::RescueFiredThisWait(mem, 6243u, 1000u));
}

// --- the regroup rung (mod-dungeon-clear#107) ---------------------------------

namespace
{
    // Blackrock Depths, wow-dev: the healer alive, in the instance, 30 yards from
    // the tank, the only member holding the wait. StrandedRecovery is OFF (its
    // default), so no teleport rescue is on offer.
    Inputs LaggingHealer()
    {
        Inputs in = Healthy();
        in.strandedOutOfRange = 1;
        in.regroupableStrays = 1;
        in.regroupAfterMs = 20000;
        in.strandedRecoveryEnabled = false;
        return in;
    }
}

TEST(DcPartyWaitTest, ALaggingHealerIsRegroupedBeforeTheCeilingWithRescueOff)
{
    Inputs in = LaggingHealer();
    in.waitedMs = 20000;
    EXPECT_EQ(Decide(in).outcome, Outcome::Regroup);
    EXPECT_EQ(Decide(in).reason, Reason::StillWaiting);
}

TEST(DcPartyWaitTest, NoRegroupBeforeItsClock)
{
    Inputs in = LaggingHealer();
    in.waitedMs = 19999;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

TEST(DcPartyWaitTest, RegroupIsOncePerWaitAndTheCeilingStillEndsAnUnhelpedWait)
{
    Inputs in = LaggingHealer();
    in.regroupAlreadyFired = true;
    in.waitedMs = 100000;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
    in.waitedMs = 180000;
    EXPECT_EQ(Decide(in).outcome, Outcome::EndRun);
    EXPECT_EQ(Decide(in).reason, Reason::Timeout);
}

TEST(DcPartyWaitTest, AMemberFarBeyondSightIsStuckNotLaggingAndIsNotRegrouped)
{
    // Out of range but not within the regroup distance: that is the teleport
    // rescue's member, and with the rescue off the ceiling ends the run as before.
    Inputs in = LaggingHealer();
    in.regroupableStrays = 0;
    in.waitedMs = 100000;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
    in.waitedMs = 180000;
    EXPECT_EQ(Decide(in).outcome, Outcome::EndRun);
}

TEST(DcPartyWaitTest, RegroupNeedsEveryStrayToBeLagging)
{
    // One near, one far: ordering the near one would not release the wait, and the
    // far one is the rescue's. No regroup.
    Inputs in = LaggingHealer();
    in.strandedOutOfRange = 2;
    in.regroupableStrays = 1;
    in.waitedMs = 30000;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

TEST(DcPartyWaitTest, RegroupOffAtZeroSecs)
{
    Inputs in = LaggingHealer();
    in.regroupAfterMs = 0;
    in.waitedMs = 100000;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

TEST(DcPartyWaitTest, ADeadMemberIsNeverRegroupedAndStillStopsAtTheCeiling)
{
    // A dead member is not a stray (OutOfRangeMemberCount skips the dead), so the
    // wait it holds is the rez recovery's and the ceiling ends it as before.
    Inputs in = Healthy();
    in.regroupAfterMs = 20000;
    in.waitedMs = 100000;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
    in.waitedMs = 180000;
    EXPECT_EQ(Decide(in).outcome, Outcome::EndRun);
    EXPECT_EQ(Decide(in).reason, Reason::Timeout);
}

TEST(DcPartyWaitTest, AHealerOnAnotherMapStillEndsTheRunImmediately)
{
    // Viability outranks the regroup: a healer who left the instance cannot be
    // ordered back by a follow command.
    Inputs in = LaggingHealer();
    in.presentAlive = 4;
    in.presentHasHealer = false;
    in.strandedOutOfRange = 0;
    in.regroupableStrays = 0;
    in.waitedMs = 25000;
    EXPECT_EQ(Decide(in).outcome, Outcome::EndRun);
    EXPECT_EQ(Decide(in).reason, Reason::LostHealer);
}

TEST(DcPartyWaitTest, RegroupIsTriedBeforeTheTeleportRescueWhenBothAreDue)
{
    Inputs in = LaggingHealer();
    in.strandedRecoveryEnabled = true;
    in.strandedTimeoutMs = 20000;
    in.waitedMs = 25000;
    EXPECT_EQ(Decide(in).outcome, Outcome::Regroup);
    in.regroupAlreadyFired = true;
    EXPECT_EQ(Decide(in).outcome, Outcome::Rescue);
}

TEST(DcPartyWaitTest, RegroupMemoryAllowsOneRegroupPerWait)
{
    DcPartyWaitDecision::TimeoutMemory mem;
    EXPECT_FALSE(DcPartyWaitDecision::RegroupFiredThisWait(mem, 9025u, 1000u));
    DcPartyWaitDecision::RecordRegroup(mem, 9025u, 1000u);
    EXPECT_TRUE(DcPartyWaitDecision::RegroupFiredThisWait(mem, 9025u, 1000u));
    EXPECT_FALSE(DcPartyWaitDecision::RegroupFiredThisWait(mem, 9025u, 2000u));
    EXPECT_FALSE(DcPartyWaitDecision::RegroupFiredThisWait(mem, 4887u, 1000u));
    // The rescue record is separate.
    EXPECT_FALSE(DcPartyWaitDecision::RescueFiredThisWait(mem, 9025u, 1000u));
}

// --- a lagging member never ends a living party's run (mod-dungeon-clear#144) --

TEST(DcPartyWaitTest, AtTheCeilingALivingPartyWithOneLaggardGoesOnInsteadOfEnding)
{
    // Live, wow-dev 2026-11-12: two family runs in a row ended at the 180s ceiling
    // with all five alive, one member 45 to 49 yards behind (a priest, then a
    // shaman) after the one regroup order had not closed the gap. Ending the run
    // left the party inside with no controller.
    Inputs in = LaggingHealer();
    in.waitedMs = 180000;
    in.regroupAlreadyFired = true;
    in.onlyStragglersShort = true;
    auto const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::GoOn);
    EXPECT_EQ(r.reason, Reason::LaggardOnly);
}

TEST(DcPartyWaitTest, BelowTheCeilingALaggardStillWaitsItsWholeClock)
{
    Inputs in = LaggingHealer();
    in.waitedMs = 179999;
    in.regroupAlreadyFired = true;
    in.onlyStragglersShort = true;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

TEST(DcPartyWaitTest, AMemberThatIsNotJustLaggingStillEndsTheRunAtTheCeiling)
{
    // Not set: a member beyond sight range (stuck), or something else (low health,
    // a healer's mana) also holding the wait. The ceiling ends the run as before.
    Inputs in = LaggingHealer();
    in.waitedMs = 180000;
    in.regroupAlreadyFired = true;
    in.onlyStragglersShort = false;
    auto const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::EndRun);
    EXPECT_EQ(r.reason, Reason::Timeout);
}

TEST(DcPartyWaitTest, ALostHealerOutranksGoingOnWithALaggard)
{
    Inputs in = LaggingHealer();
    in.waitedMs = 180000;
    in.onlyStragglersShort = true;
    in.presentHasHealer = false;
    LeaveInstance(in, 1);
    auto const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::EndRun);
    EXPECT_EQ(r.reason, Reason::LostHealer);
}

TEST(DcPartyWaitTest, ZeroTimeoutNeverGoesOnForALaggardEither)
{
    Inputs in = LaggingHealer();
    in.timeoutMs = 0;
    in.waitedMs = 999999;
    in.regroupAlreadyFired = true;
    in.onlyStragglersShort = true;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

// --- a wait held only by damage dealers' mana goes on (issue 102) -------------

TEST(DcPartyWaitTest, AtTheCeilingDamageManaAloneGoesOnInsteadOfEnding)
{
    // Live: 26 of 48 guild run stalls in 72 hours ended "the party never became
    // ready after 180s (Waiting on <hunter/paladin/shaman> (low mana))", every
    // one a damage dealer with no drink. The run goes on instead.
    Inputs in = Healthy();
    in.waitedMs = 180000;
    in.onlyDamageManaShort = true;
    auto const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::GoOn);
    EXPECT_EQ(r.reason, Reason::DamageManaOnly);
}

TEST(DcPartyWaitTest, BelowTheCeilingDamageManaStillWaits)
{
    // The waiver is not a shortcut: the damage dealer gets the whole ceiling to
    // regenerate first.
    Inputs in = Healthy();
    in.waitedMs = 179999;
    in.onlyDamageManaShort = true;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}

TEST(DcPartyWaitTest, AnyOtherWaitAtTheCeilingStillEndsTheRun)
{
    // Out of range, a hurt member, a healer's or tank's mana: onlyDamageManaShort
    // is false and the ceiling ends the run exactly as before.
    Inputs in = Healthy();
    in.waitedMs = 180000;
    auto const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::EndRun);
    EXPECT_EQ(r.reason, Reason::Timeout);
}

TEST(DcPartyWaitTest, ViabilityStillOutranksGoingOn)
{
    Inputs in = Healthy();
    in.waitedMs = 180000;
    in.onlyDamageManaShort = true;
    in.presentHasHealer = false;
    LeaveInstance(in, 1);
    auto const r = Decide(in);
    EXPECT_EQ(r.outcome, Outcome::EndRun);
    EXPECT_EQ(r.reason, Reason::LostHealer);
}

TEST(DcPartyWaitTest, ZeroTimeoutNeverGoesOnEither)
{
    // An operator who turned the ceiling off keeps a frozen wait, as before.
    Inputs in = Healthy();
    in.timeoutMs = 0;
    in.waitedMs = 3600000;
    in.onlyDamageManaShort = true;
    EXPECT_EQ(Decide(in).outcome, Outcome::Wait);
}
