/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCNOKILLDECISION_H
#define _PLAYERBOT_DCNOKILLDECISION_H

#include <cstdint>
#include <string>

// Pure decision kernel for the run-level NO-KILL watchdog.
//
// Every other no-progress failsafe in the module answers one shape of stall: a
// member stranded out of range (DcStrandedDecision), a fight that cannot end
// (DcCombatPurge), a between-pulls wait that cannot be satisfied
// (DcPartyWaitDecision), a door that will not open (DcApproachState's door
// clock). A stall none of them recognises had no end inside the module. Live
// (mod-dungeon-clear#96): six Gnomeregan guild runs sat with three bosses down
// and no kill on the way to Mekgineer Thermaplugg for about 90 minutes each,
// until the coordinator's two-hour cap ended them. The tank was re-pathing the
// whole time, so the closing-distance signal kept the shared progress clock
// fresh and nothing ever fired.
//
// This kernel is the backstop for all of them: when the run has completed no
// boss or objective for longer than DungeonClear.NoKillTimeoutSecs and a boss
// is still ahead, the run ENDS with a reason naming the wait and the boss. End,
// not recover: each recovery the module has already had its turn inside that
// window, so a run still without a kill at the limit is past what the module
// can fix by itself, and the funnel every other ending uses
// (DcActionShared::DisableDungeonClear) tells the addon, the status panel and
// the coordinator why.
//
// THE CLOCK is a DcRunProgress::Mark on DcRunState (killProgress), the same
// type and the same encounter-mask / cleared-anchor signals the other
// no-progress clocks read, with only the KILL half of the detector
// (DcRunProgress::DetectKill). Closing distance is deliberately not progress
// here: a tank that re-paths forever is the stall this exists for. The kernel
// owns the stamp so the reset rules live in one tested place.
//
// Header-only and engine-free, like DcStrandedDecision.
namespace DcNoKillDecision
{
    struct Inputs
    {
        std::uint32_t nowMs = 0;
        std::uint32_t limitMs = 0;   // NoKillTimeoutSecs * 1000; 0 = off
        bool          killed = false; // a boss or objective completed this tick
        bool          held = false;   // paused / raid boss stand-down / chess: an intentional hold
        std::string   nextBoss;       // the boss still ahead; empty = none left (or unknown)
    };

    struct Result
    {
        bool        end = false;  // stop the run now
        std::string reason;       // why, for DisableDungeonClear
    };

    constexpr std::uint32_t MS_PER_MIN = 60u * 1000u;

    // The stop reason. Whole minutes, rounded down, so 31m59s reads "31 min".
    inline std::string StopReason(std::uint32_t quietMs, std::string const& nextBoss)
    {
        return "Dungeon clear stopped: no kill for " + std::to_string(quietMs / MS_PER_MIN) +
               " min before " + nextBoss + ".";
    }

    // One tick. `killStampMs` is the clock (DcRunState::killProgress.stampMs;
    // 0 = unarmed). A kill, an intentional hold, or an unarmed clock re-stamps it
    // to now and never ends the run. Otherwise the run ends once the quiet time
    // is MORE than the limit with a boss still ahead.
    inline Result Tick(std::uint32_t& killStampMs, Inputs const& in)
    {
        Result r;
        if (in.killed || in.held || killStampMs == 0)
        {
            killStampMs = in.nowMs ? in.nowMs : 1u;  // never 0, which means unarmed
            return r;
        }
        if (in.limitMs == 0 || in.nextBoss.empty())
            return r;

        // Wrap-safe on the same monotonic getMSTime() clock.
        std::uint32_t const quietMs = in.nowMs - killStampMs;
        if (quietMs <= in.limitMs)
            return r;

        r.end = true;
        r.reason = StopReason(quietMs, in.nextBoss);
        return r;
    }
}

#endif  // _PLAYERBOT_DCNOKILLDECISION_H
