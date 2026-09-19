/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCPARTYWAITDECISION_H
#define _PLAYERBOT_DCPARTYWAITDECISION_H

#include <cstdint>

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
        EndRun,  // stop the clear and report `reason`
    };

    enum class Reason
    {
        StillWaiting,     // Wait
        LostHealer,       // the group's only healer is not in this instance any more
        TooFewMembers,    // not enough living members left here to clear anything
        Timeout,          // the wait never became satisfiable
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

        // Living members sharing the LEADER'S OWN Map (the leader included).
        // "Same Map object", never "same map id" - see DcSameInstance.h.
        std::uint32_t presentAlive = 0;
        bool presentHasHealer = false;

        // Living members of the group anywhere, and whether any of them heals.
        // The comparison against `present*` is what detects "somebody left the
        // instance" without needing to remember the comp the run started with.
        std::uint32_t rosterAlive = 0;
        bool rosterHasHealer = false;
    };

    struct Result
    {
        Outcome outcome = Outcome::Wait;
        Reason  reason  = Reason::StillWaiting;
    };

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
        if (in.presentAlive < in.rosterAlive)
        {
            if (in.presentAlive < kMinViableMembers)
                return verdict(Outcome::EndRun, Reason::TooFewMembers);
            // Only a party that HAD a healer can have lost one. A deliberate
            // healer-less comp keeps running exactly as it did before.
            if (in.rosterHasHealer && !in.presentHasHealer)
                return verdict(Outcome::EndRun, Reason::LostHealer);
        }

        // 2. The ceiling. Disabled (0) only by an operator who has decided a
        //    frozen run is preferable to an ended one; the registry's floor keeps
        //    the enabled values long enough that a legitimate rest never trips it.
        if (in.timeoutMs && in.waitedMs >= in.timeoutMs)
            return verdict(Outcome::EndRun, Reason::Timeout);

        return verdict(Outcome::Wait, Reason::StillWaiting);
    }
}

#endif  // _PLAYERBOT_DCPARTYWAITDECISION_H
