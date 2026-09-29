/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSTALLEDFALLBACKDECISION_H
#define _PLAYERBOT_DCSTALLEDFALLBACKDECISION_H

#include <cstddef>
#include <cstdint>

// Pure decision kernel for the stalled fallback's give-up budget.
//
// THE DEADLOCK IT ENDS. Advance sets the run's stall reason when it cannot move
// (off-line rejoin refused, no path, dead-end). DungeonClearStalledTrigger
// (relevance 20) then fires on every tick and outranks Advance (15), and
// DungeonClearClearStalledAction walks the leader at the nearest reachable hostile.
// That action returns true on every tick it has a target and is still walking, and
// the stall reason is cleared ONLY by a successful Advance, which the action's own
// win of the tick prevents. A target the leader can path to on paper but never
// actually closes on (across a gap, behind geometry, oscillating on a ledge) makes
// the loop permanent: Advance never runs again, so the off-line rejoin that would
// bring the leader back to its route is never retried.
//
// Live (guild finder run 298505, Ragefire Chasm): 19347 consecutive fallback ticks,
// zero Advance ticks, zero first contacts, until the 7200s backstop.
//
// THE RULE. A fallback target gets a budget. Progress is closing on it by
// PROGRESS_YARDS or being in contact range. When the budget passes with no
// progress the target is banned for BAN_MS and the caller clears the stall and
// hands the tick to Advance. If Advance stalls again the fallback picks its next
// candidate, so the two alternate instead of one starving the other. Nothing here
// stops the run.
//
// Engine-free and header-only so it is unit-testable in isolation, mirroring
// DcStrandedDecision / DcRezDecision.
namespace DcStalledFallback
{
    // How long one target may hold the tick without the leader closing on it.
    constexpr uint32_t BUDGET_MS = 30000;
    // Closing by at least this much resets the budget.
    constexpr float PROGRESS_YARDS = 5.0f;
    // At or inside this distance the leader is in contact, which is progress by
    // definition (EngageDirect stops walking and fights from here).
    constexpr float CONTACT_YARDS = 10.0f;
    // How long a given-up target stays off the candidate list.
    constexpr uint32_t BAN_MS = 120000;

    enum class Verdict
    {
        Engage,  // keep walking at / fighting the target
        GiveUp,  // budget spent with no progress: ban it, clear the stall, yield to Advance
    };

    struct Watch
    {
        static constexpr size_t MAX_BANNED = 4;

        uint64_t target = 0;       // raw GUID of the target being worked, 0 = none
        uint32_t startMs = 0;      // when this target was first worked
        uint32_t progressMs = 0;   // last time the leader closed on it
        float bestDist = 0.0f;     // closest approach seen
        uint64_t banned[MAX_BANNED] = {};
        uint32_t bannedUntilMs[MAX_BANNED] = {};
        size_t nextSlot = 0;

        void Reset()
        {
            *this = Watch{};
        }

        // Called once per fallback tick that has a target. `now` and the stored
        // stamps are unsigned ms, so the subtraction is wrap-safe.
        Verdict Observe(uint64_t guid, float dist, uint32_t now)
        {
            if (guid != target)
            {
                target = guid;
                startMs = progressMs = now;
                bestDist = dist;
                return Verdict::Engage;
            }
            if (dist <= CONTACT_YARDS || dist <= bestDist - PROGRESS_YARDS)
            {
                bestDist = dist;
                progressMs = now;
                return Verdict::Engage;
            }
            return (now - progressMs) >= BUDGET_MS ? Verdict::GiveUp : Verdict::Engage;
        }

        void Ban(uint64_t guid, uint32_t now)
        {
            banned[nextSlot] = guid;
            bannedUntilMs[nextSlot] = now + BAN_MS;
            nextSlot = (nextSlot + 1) % MAX_BANNED;
            target = 0;
        }

        bool IsBanned(uint64_t guid, uint32_t now) const
        {
            for (size_t i = 0; i < MAX_BANNED; ++i)
                if (guid != 0 && banned[i] == guid &&
                    static_cast<int32_t>(bannedUntilMs[i] - now) > 0)
                    return true;
            return false;
        }
    };
}

#endif  // _PLAYERBOT_DCSTALLEDFALLBACKDECISION_H
