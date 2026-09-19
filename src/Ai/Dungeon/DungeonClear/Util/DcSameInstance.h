/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSAMEINSTANCE_H
#define _PLAYERBOT_DCSAMEINSTANCE_H

#include "Map.h"
#include "Player.h"

// "Is this groupmate in the SAME dungeon as us" - the one predicate every group
// walk in this module is asking, spelled correctly.
//
// The module used to ask it as `member->GetMapId() != bot->GetMapId()`, in about
// a dozen places. That test is wrong, and DcLeaderSignal::ValidateCachedLeader
// has said so in a comment since issue #20:
//
//     a groupmate in another copy of the same dungeon passes a GetMapId() test
//     while living on a different Map, updated by a different MapUpdater thread
//
// Leader election was fixed to compare the MAP OBJECT. Nothing else was, so the
// rest of the module kept the id compare - including the walks that WRITE
// through the member pointer (DcStrandedRecovery's teleport, the event
// executor's relocation and combat drop, the escort speed apply) and the
// between-pulls readiness gate that decides whether the tank may advance at all.
//
// Two states pass a map-id compare and must not:
//
//   1. ANOTHER COPY OF THE SAME DUNGEON. A member pulled out of the run and
//      logged back in re-enters map 43 as a fresh instance copy: same map id,
//      different Map, different update thread. Reading its position is racy;
//      teleporting it or clearing its MotionMaster is a cross-thread write.
//      It is also unreachable by construction, so the spread gate waits on a
//      member that can never arrive - the run freezes with the tank logging
//      "party not ready - waiting on <name> (out of range)" every tick.
//
//   2. MID-TRANSITION. Between TeleportTo and HandleMoveWorldportAck the id has
//      already flipped while the object has not settled onto the new map.
//
// IsInWorld() first, then GetMap(): WorldObject::GetMap() ASSERTs on a null map,
// and a member on its way out of the world is exactly the caller we are guarding
// against (same ordering, same reason, as DcCombatFlag::IsEngaged).
//
// Header-only and dependency-free on purpose: it is included by the party-state,
// leader-signal, stranded-recovery, event-executor and engage TUs, which already
// include each other in enough directions.
inline bool DcSameInstance(WorldObject const* a, WorldObject const* b)
{
    if (!a || !b)
        return false;
    if (a == b)
        return true;
    if (!a->IsInWorld() || !b->IsInWorld())
        return false;
    return a->GetMap() == b->GetMap();
}

#endif  // _PLAYERBOT_DCSAMEINSTANCE_H
