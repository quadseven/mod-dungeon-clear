/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_LOG_THROTTLE_H
#define _DC_LOG_THROTTLE_H

#include <cstdint>

// SAY A REPEATING STATE WHEN IT CHANGES, AND OTHERWISE ONLY NOW AND THEN.
//
// The between-pulls yield ("advance yielding after N ticks: party not ready /
// resting - waiting on X") is written on every AI tick the party is not ready,
// which is several lines a second per leader for as long as the wait lasts.
// Measured on a dev realm: a leader waiting on a member it could never reach
// filled the pod log fast enough that it rotated within minutes, taking every
// other module's lines with it.
//
// The rule: log when the text differs from the last one logged (a new reason
// is news), or when `intervalMs` has passed since the last line (a wait that
// is still going is worth one reminder, not a hundred). `neverLogged` makes
// the first line of a wait always go out. The caller owns the clock and the
// remembered text; this owns only what they mean.
namespace DcLogThrottle
{
    constexpr bool ShouldLog(bool neverLogged, bool textChanged, std::uint32_t sinceLastMs,
                             std::uint32_t intervalMs)
    {
        return neverLogged || textChanged || sinceLastMs >= intervalMs;
    }
}

#endif  // _DC_LOG_THROTTLE_H
