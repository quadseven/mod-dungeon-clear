/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCBOSSWAKEVETO_H
#define _PLAYERBOT_DCBOSSWAKEVETO_H

// Pure rule: is a pack off limits as a PULL target because it stands inside a
// sleeping room-aggro boss's wake radius? (Refs #132)
//
// The room clear already excludes such packs from its own count ("they come with
// the boss"), but every protection built on that (the keep-out ring, the camp
// off-limits zone, the chase ban) reads "room clear active" and so switches off
// when the clear gives up. The pull planner then falls back to its corridor scan
// and can offer one of those packs. Run 1823401 (Emperor Dagran Thaurissan): a
// Senator 21.4yd from the throne against a 24.3yd sphere was planned, pulled and
// fought where it stood, and the Emperor woke on the tank 14.6yd out with the rest
// of the party 49-63yd behind. A pack there cannot be pulled without waking the
// boss, and the boss has his own gate (party rested, looted, together), so the
// planner must not offer it.
//
// `wakeRadius` is what the room-trash exclusion uses: the room's pull-out radius
// when it has one, else the computed wake sphere. A pack exactly on the edge is
// outside (the exclusion keeps a pack when dist > radius).
namespace DcBossWakeVeto
{
    inline bool PackOffLimits(bool roomAggroBoss, bool bossInCombat,
                              float packToBoss, float wakeRadius)
    {
        return roomAggroBoss && !bossInCombat && wakeRadius > 0.0f &&
               packToBoss < wakeRadius;
    }
}

#endif  // _PLAYERBOT_DCBOSSWAKEVETO_H
