/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSFKSORCERERGATEDECISION_H
#define _PLAYERBOT_DCSFKSORCERERGATEDECISION_H

#include "Define.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"

// PURE kernels for Shadowfang Keep's Sorcerer's Gate (GO 18972), the gate out
// of Fenrus the Devourer's room toward Wolf Master Nandos.
//
// The gate opens when the first of Arugal's Voidwalkers (4627) dies. They are
// summoned about 6s after Fenrus dies and despawn after 60s out of combat. A
// party that kills Fenrus at a pull camp away from the spawn ring, or rests or
// loots first, never fights them: the voidwalker event's WaitForSpawn times out,
// the event is Optional so it is skipped, and the gate stays shut for the life
// of the instance (the instance script re-opens it only when the GO is created
// again with Fenrus DONE). Two `dc skip`s (Nandos, then Arugal) then empty the
// boss list and the module switches itself off.
//
// Two decisions close that, each called by the live code and tested here:
//   - OpensOnTimeout: the voidwalker event's own spawn wait timed out with
//     Fenrus dead, the gate shut and no voidwalker left alive, so nothing the
//     party can do will ever open it. The event's step-timeout hook then opens
//     it, which is the state the instance script itself restores for a dead
//     Fenrus.
//   - RefusesSkip: while the gate is still shut, a `dc skip` of a boss behind
//     it is refused with one logged reason, instead of skipping both remaining
//     bosses and disabling the run.
namespace DcSfkSorcererGate
{
    constexpr uint32 MAP_ID = 33;
    constexpr uint32 GATE_ENTRY = 18972;   // Sorcerer's Gate
    constexpr uint32 FENRUS = 4274;        // Fenrus the Devourer
    constexpr uint32 VOIDWALKER = 4627;    // Arugal's Voidwalker
    constexpr uint32 NANDOS = 3927;        // Wolf Master Nandos (behind the gate)
    constexpr uint32 ARUGAL = 4275;        // Archmage Arugal (behind the gate)

    // Fixed text of the line logged when the timeout hook opens the gate. The
    // live proof greps DungeonClear.log for it, so keep it stable.
    constexpr char const* OPENED_ON_TIMEOUT_LOG =
        "SFK Sorcerer's Gate (GO 18972) opened: voidwalker spawn wait timed out with Fenrus dead";

    // Fixed reason for a refused `dc skip` while the gate is shut.
    constexpr char const* SKIP_REFUSED_REASON =
        "Cannot skip: the Sorcerer's Gate (GO 18972) is still shut, so Wolf Master Nandos and "
        "Archmage Arugal cannot be reached. Open the gate, then skip.";

    // True when the step that timed out is the voidwalker spawn wait and the
    // gate can no longer open by itself: Fenrus is dead (his death started the
    // summon), the gate is still shut, and no voidwalker is alive to kill. Any
    // other timed-out step (the sweep, the by-entry kill, the gate confirm) is
    // NOT a missed summon and keeps the gate shut.
    inline bool OpensOnTimeout(EventStepKind timedOutKind, uint32 timedOutEntry, bool fenrusDead,
                               bool gateShut, bool voidwalkerAlive)
    {
        return timedOutKind == EventStepKind::WaitForSpawn && timedOutEntry == VOIDWALKER &&
               fenrusDead && gateShut && !voidwalkerAlive;
    }

    // True when `dc skip` on `mapId` targeting `skipTargetEntry` must be
    // refused: the gate is shut and the target is one of the two bosses behind
    // it. Skipping either cannot get the party through, and skipping both
    // empties the boss list and disables the run.
    inline bool RefusesSkip(uint32 mapId, uint32 skipTargetEntry, bool gateShut)
    {
        return mapId == MAP_ID && gateShut &&
               (skipTargetEntry == NANDOS || skipTargetEntry == ARUGAL);
    }
}

#endif
