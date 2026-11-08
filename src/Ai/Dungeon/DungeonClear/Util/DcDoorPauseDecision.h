/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCDOORPAUSEDECISION_H
#define _PLAYERBOT_DCDOORPAUSEDECISION_H

#include <cstdint>

// Pure decision kernel for door-blocked auto-pause bounded exit.
//
// When a closed door blocks the corridor and cannot be opened (e.g. requires a
// party key the bots do not hold, like the Shadowforge Key in Blackrock Depths),
// dungeon clear auto-pauses so a human player can open the door.
//
// Previously, the pause waited indefinitely for the door to open (polling
// DcRunState::pausedDoor via DungeonClearDoorReopenedTrigger). If the door never
// opened, the run froze forever with no way out.
//
// This decision bounds that auto-pause: if the door opens before the timeout,
// the clear auto-resumes immediately. If the door remains shut past the bound,
// the pause exits with timedOut = true, allowing the clear to exclude the door
// and re-plan the corridor around it.
//
// Extracted engine-free so it is unit-testable in isolation.
namespace DcDoorPauseDecision
{
    // Default duration (ms) a door-blocked auto-pause waits for a closed door
    // before timing out and re-planning around it.
    constexpr std::uint32_t DEFAULT_TIMEOUT_MS = 30000;

    struct Inputs
    {
        bool          doorOpen = false;       // door is now open in world
        std::uint32_t pausedSinceMs = 0;      // getMSTime() when door auto-pause began (0 = unarmed)
        std::uint32_t nowMs = 0;              // current getMSTime()
        std::uint32_t timeoutMs = DEFAULT_TIMEOUT_MS; // wait ceiling (0 = infinite wait)
    };

    struct Result
    {
        bool shouldResume = false;  // unpause the run now
        bool timedOut = false;      // true if resumed because bound elapsed rather than door opening
    };

    // Decide whether the door-blocked pause should resume.
    inline Result Decide(Inputs const& in)
    {
        Result res;
        if (in.doorOpen)
        {
            res.shouldResume = true;
            res.timedOut = false;
            return res;
        }

        if (in.pausedSinceMs > 0 && in.timeoutMs > 0)
        {
            if (static_cast<std::uint32_t>(in.nowMs - in.pausedSinceMs) >= in.timeoutMs)
            {
                res.shouldResume = true;
                res.timedOut = true;
                return res;
            }
        }

        return res;
    }
}

#endif  // _PLAYERBOT_DCDOORPAUSEDECISION_H
