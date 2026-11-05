/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_NO_KILL_WATCHDOG_H
#define _DC_NO_KILL_WATCHDOG_H

#include "Define.h"

// Run-level no-kill watchdog: the glue around DcNoKillDecision.
//
// Ticked from the world OnUpdate (DungeonClearModule.cpp), beside
// DcCombatPurge::Tick, so it runs whatever engine the bots are parked on. Each
// sweep walks the clearing tanks, feeds the kill edge from
// DcRunProgress::DetectKill on DcRunState::killProgress into the kernel, and
// ends a run the kernel calls through DcActionShared::DisableDungeonClear, the
// funnel every other ending uses. Internally throttled to one sweep every few
// seconds.
namespace DcNoKillWatchdog
{
    void Tick(uint32 diff);
}

#endif  // _DC_NO_KILL_WATCHDOG_H
