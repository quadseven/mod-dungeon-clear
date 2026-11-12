/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCFINALSWEEP_H
#define _PLAYERBOT_DCFINALSWEEP_H

#include <unordered_map>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"

// FINAL SWEEP: the second defense behind a wrong "done" signal.
//
// The planner (NextDungeonBossValue) drops a boss from the candidate list when
// it is killed, skipped (`dc skip`, or the overseer's stall watchdog) or a
// latched travel objective. When the candidate list runs dry it used to mean
// "all bosses cleared" - even when the instance's completed-encounter mask still
// lacked a boss bit of the run's boss list. A Blackrock Depths run (Nov 12) lost
// a full clear that way: Phalanx (bit 12) was skipped mid-run, the path went on
// to Plugger, and nothing ever walked back, so the Emperor died but the run
// stalled with bit 12 missing.
//
// When there is no next target but a real boss bit is still missing, Decide
// names a missing boss to re-target - plus any gate objective that opens it - so
// the planner reopens them instead of stalling. It is BOUNDED:
//   - kMaxAttemptsPerBoss sweeps per boss: a boss skipped again after its sweep
//     is honoured, so a player's (or the watchdog's) repeated skip still ends it;
//   - kTotalCapMs from the first sweep of the run: no more revisits after that;
//   - a corpse (dead, bit not yet flipped) is never a sweep target;
//   - skipByDesign bosses and bosses whose done signal reads set are never missing.
// Pure: no game types. The caller reads the live signals into the predicates.
namespace DcFinalSweep
{
    inline constexpr uint32 kMaxAttemptsPerBoss = 2;
    inline constexpr uint32 kTotalCapMs = 15u * 60u * 1000u;

    // Per-run state (lives in DcRunState; Reset() clears it with the run).
    struct State
    {
        std::unordered_map<uint32, uint32> attempts;  // boss entry -> sweeps spent
        uint32 startedMs = 0;                         // first sweep of the run; 0 = none yet
    };

    enum class Outcome : uint8
    {
        NothingMissing,  // every boss in the list is done: the clear really is complete
        BudgetSpent,     // bosses are missing but every one used its attempts
        TimeCapped,      // bosses are missing but the run's sweep time is spent
        Sweep,           // re-target `target` (and reopen `reopen`)
    };

    struct Decision
    {
        Outcome outcome = Outcome::NothingMissing;
        uint32 missingMask = 0;      // real kill bits still missing (log line)
        uint32 target = 0;           // boss entry to re-target (Sweep only)
        uint32 attempt = 0;          // which attempt this is for `target` (Sweep only)
        std::vector<uint32> reopen;  // entries to drop from skipped + cleared (Sweep only)
    };

    // A boss bit this list can actually be missing: a real DBC kill bit (the
    // done-by-instance bosses have no bit, their signal is the predicate).
    inline bool HasRealBit(DungeonBossInfo const& b)
    {
        return b.kind == DungeonAnchorKind::Boss && b.encounterIndex < 32 &&
               b.doneBossStateIndex < 0 && b.doneInstanceDataId < 0;
    }

    // `doneByInstance(b)` - the instance script reads this anchor finished
    // (boss-state slot / GetData), `isCorpse(entry)` - spawned and dead.
    // Records the attempt and the clock in `st` when it returns Sweep.
    template <typename DoneByInstance, typename IsCorpse>
    inline Decision Decide(std::vector<DungeonBossInfo> const& bosses, uint32 completedMask,
                           State& st, uint32 nowMs, DoneByInstance const& doneByInstance,
                           IsCorpse const& isCorpse)
    {
        Decision d;
        DungeonBossInfo const* best = nullptr;
        bool anyMissing = false;
        for (DungeonBossInfo const& b : bosses)
        {
            if (b.kind != DungeonAnchorKind::Boss || b.skipByDesign)
                continue;
            if (AnchorDoneByEncounterMask(b, completedMask) || doneByInstance(b))
                continue;
            if (isCorpse(b.entry))
                continue;  // dies into its bit on its own; never walk at a corpse
            if (HasRealBit(b))
                d.missingMask |= 1u << b.encounterIndex;
            anyMissing = true;
            auto it = st.attempts.find(b.entry);
            if (it != st.attempts.end() && it->second >= kMaxAttemptsPerBoss)
                continue;
            if (!best || BossOrderKey(b) < BossOrderKey(*best))
                best = &b;
        }

        if (!anyMissing)
        {
            d.outcome = Outcome::NothingMissing;
            return d;
        }
        if (!best)
        {
            d.outcome = Outcome::BudgetSpent;
            return d;
        }
        if (st.startedMs != 0 && static_cast<uint32>(nowMs - st.startedMs) >= kTotalCapMs)
        {
            d.outcome = Outcome::TimeCapped;
            return d;
        }

        d.outcome = Outcome::Sweep;
        d.target = best->entry;
        d.reopen.push_back(best->entry);
        // The objectives that only gate this boss (its ale run, kegs, gossip) share
        // its kill bit and read done off it: they are finished only when the boss
        // is dead, so a skipped or latched one is reopened with the boss.
        for (DungeonBossInfo const& o : bosses)
            if (o.kind == DungeonAnchorKind::Objective && o.doneWithBossBit &&
                o.encounterIndex == best->encounterIndex && !doneByInstance(o))
                d.reopen.push_back(o.entry);

        if (st.startedMs == 0)
            st.startedMs = nowMs ? nowMs : 1u;
        d.attempt = ++st.attempts[best->entry];
        return d;
    }
}

#endif
