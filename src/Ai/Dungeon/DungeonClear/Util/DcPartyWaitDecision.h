/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCPARTYWAITDECISION_H
#define _PLAYERBOT_DCPARTYWAITDECISION_H

#include <cstdint>
#include <string>

// Pure decision kernel for the between-pulls wait's GIVE-UP step.
//
// The wait itself is right: the tank must not walk into the next pack while a
// member is behind, low, or drinking. What it never had was an end. Past
// DC_PARTY_YIELD_DEBOUNCE_TICKS the advance gate logs and halts every tick,
// forever, and the only thing that can release it is the party - so when the
// party cannot become ready, nothing ever happens again. Live: a five-member run
// whose members were pulled out of the instance by their POV clients logging in;
// the tank logged "party not ready / resting - waiting on Bork (out of range)"
// for 293 consecutive ticks and the run never moved or ended.
//
// This kernel answers, once the wait is already past its debounce: keep waiting,
// or end the run and say why?
//
// TWO INDEPENDENT REASONS TO STOP, and they are deliberately ordered.
//
//   1. THE PARTY IS NO LONGER VIABLE. Checked FIRST, and without reference to
//      the clock: a party that has lost the only member who can heal it should
//      stop now, not in three minutes, because every one of those minutes is
//      spent walking a tank at a pack it cannot survive. This is the roster
//      question, not the readiness question - it compares who is in the leader's
//      instance against who is still in the group at all.
//
//      SHORT-HANDED IS NOT UNVIABLE. On a realm where the characters are
//      streamed from live clients, a member leaving mid-run is ordinary, and a
//      reconnect must not throw away a dungeon run: four people finish a 5-man.
//      So the default answer to "somebody left" is CONTINUE, and only the two
//      compositions that cannot finish stop the run.
//
//   2. THE CEILING. Whatever else is true, a wait that has run for timeoutMs
//      without becoming satisfiable is not going to. This is the catch-all for
//      every shape the viability test cannot see (a member in the instance and
//      wedged where no recovery reaches it, an unreachable rest floor, a latch
//      that never releases) and it is what makes "it terminates" a property of
//      the gate rather than of any particular diagnosis.
//
// Engine-free so it is unit-testable in isolation, mirroring DcStrandedDecision /
// DcRezDecision / DcSmartRestDecision. Header-only; nothing here touches a
// Player/Unit/context.
namespace DcPartyWaitDecision
{
    enum class Outcome
    {
        Wait,    // keep holding the tank - the wait is still legitimate
        Regroup, // order the lagging members to follow the tank again and keep waiting (once per wait)
        Rescue,  // teleport the stranded out-of-range members to the tank and keep waiting (once per wait)
        EndRun,  // stop the clear and report `reason`
        GoOn,    // stop waiting and pull: only damage dealers' mana, or a lagging member in sight, was holding the party
    };

    enum class Reason
    {
        StillWaiting,     // Wait
        LostHealer,       // the group's only healer is not in this instance any more
        TooFewMembers,    // not enough living members left here to clear anything
        Timeout,          // the wait never became satisfiable
        DamageManaOnly,   // GoOn: the ceiling was reached on damage dealers' mana alone
        LaggardOnly,       // GoOn: the ceiling was reached on living members lagging within sight alone
        MixedHold,         // GoOn: the ceiling was reached on waivable mana plus members lagging in sight, together
    };

    // The party has to keep a healer AND at least this many living members in the
    // instance to be worth continuing with.
    //
    // Two is the floor rather than three because the pair that matters is
    // tank + healer: a bot tank with a healer behind it grinds a level-appropriate
    // 5-man slowly but survives it, which is the outcome a family realm wants out
    // of a reconnect. One living member is a tank soloing an instance it was
    // geared to tank, not to kill things in, and every pull from there is a
    // corpse run. The healer requirement is the load-bearing half: a full four
    // DPS with no healer wipes on the first elite pack, which is strictly worse
    // than stopping with a reason somebody can read.
    inline constexpr std::uint32_t kMinViableMembers = 2;

    struct Inputs
    {
        std::uint32_t waitedMs = 0;       // how long this uninterrupted wait has run
        std::uint32_t timeoutMs = 0;      // DungeonClear.PartyWaitTimeoutSecs * 1000; 0 = no ceiling
        std::uint32_t strandedTimeoutMs = 0; // StrandedRecoveryNoProgressSecs * 1000; 0 = disabled
        // Grace for a viability loss (lost healer, or too few members):
        // PartyWaitAbsentGraceSecs * 1000; 0 = no grace, the loss ends the run on
        // the first tick that sees it. A healer who is only briefly out of the
        // instance (dead at the entrance and running back, a teleport in flight)
        // gets this long to come back before the run is thrown away.
        std::uint32_t absentGraceMs = 0;
        // How long the CURRENT viability loss has lasted, from AbsenceTracker
        // (AbsentForMs), NOT how long the party has been waiting: a healer who
        // leaves after the party already waited past the grace still gets all of
        // it. 0 when no loss is being tracked.
        std::uint32_t absentMs = 0;

        // Living members sharing the LEADER'S OWN Map (the leader included).
        // "Same Map object", never "same map id" - see DcSameInstance.h.
        std::uint32_t presentAlive = 0;
        bool presentHasHealer = false;

        // Living members of the group anywhere, and whether any of them heals.
        // The comparison against `present*` is what detects "somebody left the
        // instance" without needing to remember the comp the run started with.
        std::uint32_t rosterAlive = 0;
        bool rosterHasHealer = false;

        // Living bot members on the leader's map that are out of spread of the
        // tank - the strays the stranded rescue would teleport to the tank.
        std::uint32_t strandedOutOfRange = 0;
        // Whether DungeonClear.StrandedRecovery is on. The rescue only fires when
        // the operator has opted in to teleporting members.
        bool strandedRecoveryEnabled = false;
        // Whether this wait has already spent its one rescue (RescueFiredThisWait).
        // A second rescue is never tried in the same wait, so a rescue that did not
        // bring the members in leaves the ceiling to end the run.
        bool rescueAlreadyFired = false;

        // The REGROUP rung: a non-teleport answer for a member that is lagging but
        // plainly able to come back (alive, on this map, within kRegroupMaxYards of
        // the tank). StrandedRecovery is off by default and its clock is the run's
        // no-progress clock, so before this rung a healer standing 30yd back had no
        // remedy at all and the ceiling ended the run (mod-dungeon-clear#107).
        std::uint32_t regroupAfterMs = 0;     // DungeonClear.PartyWaitRegroupSecs * 1000; 0 = off
        // How many of the out-of-range bots are within kRegroupMaxYards of the tank.
        // The rung fires only when ALL of them are (a member far beyond that is
        // stuck, not lagging, and is the teleport rescue's problem).
        std::uint32_t regroupableStrays = 0;
        bool regroupAlreadyFired = false;     // RegroupFiredThisWait

        // The party would be ready this tick if damage dealers had no mana floor:
        // every member is in range, every health bar and every healer's and tank's
        // mana is met, and nobody is waiting on a resurrection. A damage dealer's
        // mana only makes a fight slower, so a wait held by nothing else is not a
        // reason to end the run (issue 102).
        bool onlyDamageManaShort = false;

        // The party would be ready this tick if a member lagging within
        // kRegroupMaxYards of the tank were not held to the spread gate: every
        // out-of-range member is alive, on this map and inside sight range, and
        // nothing else (health, a healer's or tank's mana, a resurrection) holds
        // the wait. The regroup order is once per wait and a member that did not
        // come in for it is not coming in for a longer wait either; ending the
        // run left the whole living party inside with no controller (live,
        // wow-dev 2026-11-12, two family runs in a row, a member 45 to 49 yards
        // back, mod-dungeon-clear#144).
        bool onlyStragglersShort = false;

        // The party would be ready this tick with BOTH waivers at once: damage
        // dealers' (and no-drink members') mana floor waived AND every out-of-range
        // member measured against the tank within kRegroupMaxYards. Neither single
        // predicate above holds when the hold is a combination (live, wow-dev
        // 2026-11-12, Blackrock Depths Emperor room: two damage dealers low on
        // mana plus one member 24 yards out, all five alive and inside, gave up at
        // 206 s). Same hard stops as the singles: a member beyond kRegroupMaxYards,
        // a dead member, health, a healer's or tank's mana still hold the wait.
        bool onlyMixedShort = false;
    };

    // A lagging member farther than this from the tank is stuck rather than slow:
    // the regroup order is not enough and is not tried. Sight range at a camp.
    inline constexpr float kRegroupMaxYards = 60.0f;

    struct Result
    {
        Outcome outcome = Outcome::Wait;
        Reason  reason  = Reason::StillWaiting;
    };

    // The party has lost something it cannot clear without: the roster shrank,
    // and what is left is below the viable floor or has lost the healer the roster
    // had. Only a party that HAD a healer can have lost one; a deliberate
    // healer-less comp is never a loss.
    inline bool IsViabilityLoss(Inputs const& in)
    {
        if (in.presentAlive >= in.rosterAlive)
            return false;
        return in.presentAlive < kMinViableMembers ||
               (in.rosterHasHealer && !in.presentHasHealer);
    }

    // What took the healer out of the instance, as far as the code can see it.
    enum class LeftCause : std::uint8_t
    {
        Unknown,          // nothing known: the stop line stays bare
        Claimed,          // drawn away by a queue fill's claim on the character
        Hearth,           // casting a hearthstone
        Teleport,         // a teleport in flight or a teleport spell being cast
        DiedAndReleased,  // died and released, so it came back outside at a graveyard
    };

    // What one look at the healer shows. Plain booleans so the decision stays pure.
    struct HealerSample
    {
        bool claimedByFill = false;
        bool castingHearth = false;
        bool teleporting = false;     // IsBeingTeleported or casting a teleport spell
        bool deadOrGhost = false;
    };

    // The most specific known cause wins: a claim explains a teleport, and a cast
    // hearth explains the teleport it starts.
    inline LeftCause ClassifyLeftCause(HealerSample const& s)
    {
        if (s.claimedByFill)
            return LeftCause::Claimed;
        if (s.castingHearth)
            return LeftCause::Hearth;
        if (s.teleporting)
            return LeftCause::Teleport;
        if (s.deadOrGhost)
            return LeftCause::DiedAndReleased;
        return LeftCause::Unknown;
    }

    inline char const* LeftCauseText(LeftCause c)
    {
        switch (c)
        {
            case LeftCause::Claimed:         return "claimed away by a queue fill";
            case LeftCause::Hearth:          return "cast a hearthstone";
            case LeftCause::Teleport:        return "was teleported out";
            case LeftCause::DiedAndReleased: return "died and released to a graveyard";
            case LeftCause::Unknown:         break;
        }
        return "";
    }

    // A cause seen this long before the loss began still counts as its reason (a
    // death, then a run back, then a graveyard resurrection spans minutes).
    inline constexpr std::uint32_t kCauseFreshMs = 300000;

    // Tracks when the CURRENT viability loss began and what caused it. Lives in
    // TimeoutMemory so it outlives the approach state a stop wipes.
    struct AbsenceTracker
    {
        std::uint32_t sinceMs = 0;      // 0 = no loss in progress
        LeftCause cause = LeftCause::Unknown;
        LeftCause lastSeen = LeftCause::Unknown;  // latest cause seen, loss or not
        std::uint32_t lastSeenMs = 0;   // 0 = none
    };

    // Feed it one look per tick. `lossNow` is IsViabilityLoss for this tick and
    // `causeNow` is the healer's classification this tick (Unknown when nothing
    // shows). The clock starts on the first tick of a loss and resets the moment
    // the loss is gone, so a healer that returns and leaves again gets a fresh
    // grace and one that returns is never charged for its earlier absence.
    inline void ObserveAbsence(AbsenceTracker& t, bool lossNow, LeftCause causeNow,
                               std::uint32_t nowMs)
    {
        if (causeNow != LeftCause::Unknown)
        {
            t.lastSeen = causeNow;
            t.lastSeenMs = nowMs ? nowMs : 1u;
        }
        else if (t.lastSeenMs != 0 && nowMs - t.lastSeenMs > kCauseFreshMs)
        {
            t.lastSeen = LeftCause::Unknown;
            t.lastSeenMs = 0;
        }

        if (!lossNow)
        {
            t.sinceMs = 0;
            t.cause = LeftCause::Unknown;
            return;
        }
        if (t.sinceMs == 0)
        {
            t.sinceMs = nowMs ? nowMs : 1u;
            t.cause = t.lastSeen;
        }
        else if (causeNow != LeftCause::Unknown)
        {
            t.cause = causeNow;
        }
    }

    // How long the loss in progress has lasted; 0 when none is tracked.
    inline std::uint32_t AbsentForMs(AbsenceTracker const& t, std::uint32_t nowMs)
    {
        return t.sinceMs == 0 ? 0u : nowMs - t.sinceMs;
    }

    // The LostHealer stop line: who left and, when known, why.
    inline std::string LostHealerStopReason(std::string const& absentNames, LeftCause cause)
    {
        std::string out = "Dungeon clear stopped: the party's healer is no longer in the "
                          "instance (" + absentNames + " left the run";
        if (cause != LeftCause::Unknown)
            out += std::string(": ") + LeftCauseText(cause);
        out += ").";
        return out;
    }

    // Memory of the last party-wait timeout, so a second identical stop (same
    // boss, same members waited on) can be told from the first. It lives in its
    // own per-bot value (DcKey::PartyWaitMemory), NOT on DcApproachState: the
    // stop itself runs DisableDungeonClear, which wipes the approach state, and
    // the memory has to survive exactly that to be of any use.
    struct TimeoutMemory
    {
        std::uint32_t bossEntry = 0;  // 0 = no timeout remembered
        std::string why;              // who the wait was on, as DescribePartyNotReady said it

        // The wait that last spent its rescue: the boss it was at and the wait's
        // start (DcApproachState::partyNotReadySinceMs, never 0 while armed).
        std::uint32_t rescueBossEntry = 0;
        std::uint32_t rescueWaitStartMs = 0;  // 0 = no rescue remembered

        // The wait that last spent its regroup order, same shape as the rescue's.
        std::uint32_t regroupBossEntry = 0;
        std::uint32_t regroupWaitStartMs = 0;  // 0 = no regroup remembered

        // When the current viability loss began and what caused it (issue #162).
        AbsenceTracker absence;
    };

    // True when the wait that started at waitStartMs, at this boss, has already
    // had its regroup order. A new wait gets one of its own.
    inline bool RegroupFiredThisWait(TimeoutMemory const& mem, std::uint32_t bossEntry, std::uint32_t waitStartMs)
    {
        return mem.regroupWaitStartMs != 0 && mem.regroupWaitStartMs == waitStartMs &&
               mem.regroupBossEntry == bossEntry;
    }

    // Remember that this wait spent its regroup. The only writer of the regroup fields.
    inline void RecordRegroup(TimeoutMemory& mem, std::uint32_t bossEntry, std::uint32_t waitStartMs)
    {
        mem.regroupBossEntry = bossEntry;
        mem.regroupWaitStartMs = waitStartMs;
    }

    // True when the wait that started at waitStartMs, at this boss, has already
    // had its rescue. A new wait (the party became ready, or the run was stopped
    // and re-enabled) starts at a new time and so gets one rescue of its own.
    inline bool RescueFiredThisWait(TimeoutMemory const& mem, std::uint32_t bossEntry, std::uint32_t waitStartMs)
    {
        return mem.rescueWaitStartMs != 0 && mem.rescueWaitStartMs == waitStartMs &&
               mem.rescueBossEntry == bossEntry;
    }

    // Remember that this wait spent its rescue. The only writer of the rescue fields.
    inline void RecordRescue(TimeoutMemory& mem, std::uint32_t bossEntry, std::uint32_t waitStartMs)
    {
        mem.rescueBossEntry = bossEntry;
        mem.rescueWaitStartMs = waitStartMs;
    }

    // True when this timeout repeats the remembered one.
    inline bool IsRepeatTimeout(TimeoutMemory const& mem, std::uint32_t bossEntry, std::string const& why)
    {
        return mem.bossEntry != 0 && mem.bossEntry == bossEntry && mem.why == why;
    }

    // The stop reason for a Timeout. The repeat form starts with "RepeatedTimeout"
    // and names the boss and the members, so whatever re-enables runs can tell
    // that a third try will end the same way.
    inline std::string TimeoutStopReason(bool repeated, std::uint32_t waitedSecs,
                                         std::string const& bossName, std::string const& why)
    {
        std::string const secs = std::to_string(waitedSecs) + "s";
        if (repeated)
            return "Dungeon clear stopped: RepeatedTimeout at " + bossName + " after " + secs +
                   " waiting on the same member(s) as the last stop (" + why + ").";
        return "Dungeon clear stopped: the party never became ready after " + secs +
               (why.empty() ? "." : (" (" + why + ")."));
    }

    // The Timeout stop as the caller speaks it: tell a repeat from the memory,
    // build the matching reason, then remember this stop so the next identical
    // one is the repeat. The only writer of TimeoutMemory.
    inline std::string RecordTimeout(TimeoutMemory& mem, std::uint32_t bossEntry, std::string const& bossName,
                                     std::uint32_t waitedSecs, std::string const& why)
    {
        bool const repeated = IsRepeatTimeout(mem, bossEntry, why);
        mem.bossEntry = bossEntry;
        mem.why = why;
        return TimeoutStopReason(repeated, waitedSecs, bossName, why);
    }

    inline Result Decide(Inputs const& in)
    {
        auto const verdict = [](Outcome o, Reason r)
        {
            Result out;
            out.outcome = o;
            out.reason = r;
            return out;
        };

        // 1. Viability, only when the roster has actually shrunk. A party that is
        //    all present and merely not ready yet is an ordinary wait, however
        //    small it is - a two-member run that STARTED as two is not something
        //    this rung gets to cancel.
        if (IsViabilityLoss(in))
        {
            bool const tooFew = in.presentAlive < kMinViableMembers;
            // Grace: a loss that has not lasted absentGraceMs yet is a wait, not
            // an ending. Measured from when the loss began (absentMs), never from
            // when the wait began (waitedMs).
            if (in.absentGraceMs == 0 || in.absentMs >= in.absentGraceMs)
                return verdict(Outcome::EndRun,
                               tooFew ? Reason::TooFewMembers : Reason::LostHealer);
        }

        // 2a. Regroup first: the cheap, human-like answer (tell the laggard to
        //     follow again) before any teleport. Once per wait, clock untouched,
        //     so a regroup that does not help still reaches the rescue and the
        //     ceiling below.
        if (!in.regroupAlreadyFired && in.regroupAfterMs > 0 && in.waitedMs >= in.regroupAfterMs &&
            in.regroupableStrays > 0 && in.regroupableStrays == in.strandedOutOfRange)
            return verdict(Outcome::Regroup, Reason::StillWaiting);

        // 2b. Rescue once the stranded clock has elapsed. Do this before the
        //    longer party-wait ceiling so a rescuable wait gets a chance to make
        //    progress instead of stopping at the ceiling. Once per wait only, and
        //    the clock is not reset by it, so the ceiling below still bounds a
        //    rescue that did not help.
        if (!in.rescueAlreadyFired && in.strandedTimeoutMs > 0 && in.waitedMs >= in.strandedTimeoutMs &&
            in.strandedOutOfRange > 0 && in.strandedRecoveryEnabled)
            return verdict(Outcome::Rescue, Reason::StillWaiting);

        // 3. The ceiling. Disabled (0) only by an operator who has decided a
        //    frozen run is preferable to an ended one; the registry's floor keeps
        //    the enabled values long enough that a legitimate rest never trips it.
        //    A wait held only by damage dealers' mana goes on instead of ending:
        //    the party pulls a little slower, which beats losing the whole run.
        if (in.timeoutMs && in.waitedMs >= in.timeoutMs)
        {
            if (in.onlyDamageManaShort)
                return verdict(Outcome::GoOn, Reason::DamageManaOnly);
            // A living member in sight that the regroup did not bring in: the tank
            // goes on and the gate stops measuring that member for a while (the
            // caller's straggler waiver); follow-tank keeps trying as the tank moves.
            if (in.onlyStragglersShort)
                return verdict(Outcome::GoOn, Reason::LaggardOnly);
            // Both at once: the caller applies both waivers and the wait starts over.
            if (in.onlyMixedShort)
                return verdict(Outcome::GoOn, Reason::MixedHold);
            return verdict(Outcome::EndRun, Reason::Timeout);
        }

        return verdict(Outcome::Wait, Reason::StillWaiting);
    }
}

#endif  // _PLAYERBOT_DCPARTYWAITDECISION_H
