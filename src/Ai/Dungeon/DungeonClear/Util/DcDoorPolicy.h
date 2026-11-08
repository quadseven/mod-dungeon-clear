/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCDOORPOLICY_H
#define _PLAYERBOT_DCDOORPOLICY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "SharedDefines.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"

// Pure, testable kernel answering "could a human player at this bot's keyboard
// open this lock?" for door gameobjects. Mirrors the core's lock adjudication
// (Spell::CanOpenLock) rather than inventing a stricter policy — the previous
// hand-rolled gate refused locks the game itself hands to every player:
//
//   - A lock entry with NO typed slots at all (e.g. lock 85, the Deadmines
//     Factory/Foundry/Mast Room doors) imposes no requirement; CanOpenLock
//     returns OK for anyone. reqKey never gets set, so the empty lock falls
//     through to success.
//   - A LOCK_KEY_SKILL slot whose locktype is a bare-hands interaction
//     (Open / Quick Open / Slow Open / kneeling / attacking variants, e.g.
//     lock 86 on the Deadmines Heavy Doors) maps to SKILL_NONE — no skill
//     check, any player's click performs it.
//
// The one place we are deliberately stricter than a raw CanOpenLock mirror:
// when the GO carries GO_FLAG_LOCKED the bare-hands slots do NOT count. A
// flagged-locked gate keeps its bare-hands slot only as cast-time/animation
// data; the client requires the real key or skill (Stratholme's King's Square
// Gate, lock 879, carries a Quick Open slot yet demands the Key to the City —
// same shape on the Deadmines Iron Clad Door, lock 202, which only a rogue
// picks or the cannon event blows open).
//
// Locktypes needing consumables or professions we don't model for doors
// (Blasting = seaforium, Open Tinkering = engineering, gathering skills)
// never satisfy a slot here; a door gated only by those parks the run and
// asks the human, which is the safe default.
namespace DcDoorPolicy
{
    // Keep closed-door detection ahead of long dungeon approaches. The
    // Gnomeregan Final Chamber gate is about 90yd up the Thermaplugg route.
    constexpr float BLOCKING_DOOR_LOOK_AHEAD = 120.0f;

    // One decoded Lock.dbc case: LOCK_KEY_* discriminator, its Index (item
    // entry for ITEM, LockType for SKILL), and the required skill value.
    struct LockSlot
    {
        uint8 keyType = 0;
        uint32 index = 0;
        uint32 requiredSkill = 0;
    };

    constexpr std::size_t LOCK_SLOT_COUNT = 8;  // MAX_LOCK_CASE

    // Locktypes any player performs empty-handed by clicking. CLOSE variants
    // included: a slot authored as Quick/Slow Close is still a bare-hands
    // interaction on the same door.
    inline bool IsBareHandsLockType(uint32 lockType)
    {
        switch (lockType)
        {
            case LOCKTYPE_OPEN:
            case LOCKTYPE_CLOSE:
            case LOCKTYPE_QUICK_OPEN:
            case LOCKTYPE_QUICK_CLOSE:
            case LOCKTYPE_OPEN_KNEELING:
            case LOCKTYPE_OPEN_ATTACKING:
            case LOCKTYPE_SLOW_OPEN:
            case LOCKTYPE_SLOW_CLOSE:
                return true;
            default:
                return false;
        }
    }

    // Slot walk mirroring Spell::CanOpenLock: any satisfied slot opens; a lock
    // whose slots are all untyped imposed no requirement and opens for anyone.
    //
    //   lockEnforced  — the GO carries GO_FLAG_LOCKED (suppresses bare-hands
    //                   slots, see the header comment).
    //   hasKeyItem    — callable, uint32 item entry -> bool (in the bot's bags).
    //   lockpickSkill — the bot's lockpicking value, or -1 when unskilled.
    template <typename HasItemFn>
    bool CanOpenSlots(LockSlot const* slots, std::size_t count, bool lockEnforced,
                      HasItemFn&& hasKeyItem, int32 lockpickSkill)
    {
        bool requirementSeen = false;
        for (std::size_t i = 0; i < count; ++i)
        {
            LockSlot const& slot = slots[i];
            switch (slot.keyType)
            {
                case LOCK_KEY_ITEM:
                    if (slot.index && hasKeyItem(slot.index))
                        return true;
                    requirementSeen = true;
                    break;
                case LOCK_KEY_SKILL:
                    requirementSeen = true;
                    if (slot.index == LOCKTYPE_PICKLOCK)
                    {
                        if (lockpickSkill >= 0 &&
                            lockpickSkill >= static_cast<int32>(slot.requiredSkill))
                            return true;
                    }
                    else if (IsBareHandsLockType(slot.index))
                    {
                        if (!lockEnforced)
                            return true;
                    }
                    // Consumable / profession locktypes: never satisfied here.
                    break;
                case LOCK_KEY_SPELL:
                    // Bots don't model the opening-spell inventory (seaforium
                    // etc.); treat as an unsatisfiable requirement.
                    requirementSeen = true;
                    break;
                default:
                    break;
            }
        }
        return !requirementSeen;
    }

    // Does this lock name a key at all: a LOCK_KEY_ITEM slot with an item entry
    // (Herod's Door's Scarlet Key, the Shadowforge Gates' Shadowforge Key, the
    // Shadowforge Braziers' Shadowforge Torch). A KEYED object opens only for a
    // party member who carries one of its keys or picks the lock: nobody else
    // can open it for them, and nothing waives it.
    inline bool LockNamesKey(LockSlot const* slots, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (slots[i].keyType == LOCK_KEY_ITEM && slots[i].index)
                return true;
        return false;
    }

    // ---- SHOULD THIS SHUT DOOR BE FLAGGED AS CORRIDOR-BLOCKING --------------
    // The decision DungeonClearBlockingDoorValue::Calculate makes per shut door,
    // lifted out so it is testable offline. Flag ONLY a door the look-ahead route
    // transits: a door the corridor runs past (off the planned segment) is left
    // alone. Distance to the bot is never a criterion.

    // Lateral slack around the door model footprint (bot body radius + the gap
    // between the smoothed centerline and the walked line).
    constexpr float DOOR_PATH_PAD = 2.0f;
    // Vertical tolerance over the door model Z extent (mirrors DC_DOOR_Z_BAND).
    constexpr float DOOR_Z_BAND = 6.0f;
    // 2D proximity gate for the GameObject-LOS detector, in yards.
    constexpr float DOOR_LOS_NEAR = 12.0f;

    // The door as the decision reads it: the GO origin and orientation, and the
    // model box already scaled by the GO scale, in the door local frame.
    struct DoorFootprint
    {
        float gx = 0, gy = 0, gz = 0;
        float orientation = 0;
        float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    };

    // True when a leg shares the door floor: a leg whose height span misses the
    // door padded Z span runs over or under it.
    inline bool LegSharesDoorFloor(DoorFootprint const& d, float az, float bz)
    {
        float const loZ = d.gz + d.minZ - DOOR_Z_BAND;
        float const hiZ = d.gz + d.maxZ + DOOR_Z_BAND;
        return std::max(az, bz) >= loZ && std::min(az, bz) <= hiZ;
    }

    // True when a leg passes THROUGH the door footprint (oriented box + pad).
    inline bool LegCrossesDoor(DoorFootprint const& d,
                               DungeonClearMath::LookAheadLeg const& leg)
    {
        if (!LegSharesDoorFloor(d, leg.az, leg.bz))
            return false;
        float const sinA = std::sin(d.orientation);
        float const cosA = std::cos(d.orientation);
        auto const toLocal = [&](float wx, float wy, float& lx, float& ly)
        {
            float const ddx = wx - d.gx;
            float const ddy = wy - d.gy;
            lx = sinA * ddx + cosA * ddy;
            ly = cosA * ddx - sinA * ddy;
        };
        float lax, lay, lbx, lby;
        toLocal(leg.ax, leg.ay, lax, lay);
        toLocal(leg.bx, leg.by, lbx, lby);
        return DungeonClearMath::SegmentIntersectsAABB2D(
            lax, lay, lbx, lby, d.minX - DOOR_PATH_PAD, d.minY - DOOR_PATH_PAD,
            d.maxX + DOOR_PATH_PAD, d.maxY + DOOR_PATH_PAD);
    }

    // excluded: the door is on the run exclusion list (a door pause timed out on
    // it). losBlocked(leg): the GameObject-only line of sight along that leg is
    // blocked; called only for a leg on the door floor within DOOR_LOS_NEAR yards
    // of the door origin, so the costly map query stays lazy.
    template <class LosBlockedFn>
    bool ShouldFlagDoor(bool excluded, DoorFootprint const& door,
                        std::vector<DungeonClearMath::LookAheadLeg> const& legs,
                        LosBlockedFn losBlocked)
    {
        if (excluded)
            return false;
        for (auto const& leg : legs)
            if (LegCrossesDoor(door, leg))
                return true;
        constexpr float NEAR_SQ = DOOR_LOS_NEAR * DOOR_LOS_NEAR;
        for (auto const& leg : legs)
        {
            if (!LegSharesDoorFloor(door, leg.az, leg.bz))
                continue;
            if (DungeonClearMath::DistSqToSegment2D(door.gx, door.gy, leg.ax, leg.ay,
                                                    leg.bx, leg.by) > NEAR_SQ)
                continue;
            if (losBlocked(leg))
                return true;
        }
        return false;
    }

    // WHAT A DOOR PAUSE DOES NEXT. A pause the door-blocked action stamps for a
    // door nobody can open used to have no exit but a player opening that door:
    // `dc skip` never touched it and nothing re-evaluated the route while paused
    // (issue 105: a Blackrock Depths run stood at the Shadowforge Gate for 18+
    // minutes). Hold while inside the bound; once the door reads open, resume as
    // before; once the bound passes with the door still shut, resume with that
    // door excluded from the blocking-door scan so the run re-plans and walks on.
    enum class DoorPauseExit
    {
        Hold,                  // not a door pause, or still inside the bound
        ResumeDoorOpen,        // the door reads open (or is gone)
        ResumeExcludingDoor,   // bound passed, door still shut: re-plan without it
    };

    // pausedForDoor: the pause was stamped by the door auto-pause (pausedDoor set).
    // boundMs 0 disables the timed exit (the door-open exit still applies).
    inline DoorPauseExit DecideDoorPauseExit(bool pausedForDoor, bool doorClosed,
                                             uint32 pausedForMs, uint32 boundMs)
    {
        if (!pausedForDoor)
            return DoorPauseExit::Hold;
        if (!doorClosed)
            return DoorPauseExit::ResumeDoorOpen;
        if (boundMs > 0 && pausedForMs >= boundMs)
            return DoorPauseExit::ResumeExcludingDoor;
        return DoorPauseExit::Hold;
    }

    // `dc skip` clears a door pause (it already resets the route and target); a
    // manual or wait-at-boss pause is the player's own hold and stays.
    inline bool SkipClearsPause(bool paused, bool pausedForDoor)
    {
        return paused && pausedForDoor;
    }

    // One party member as the opener choice reads it.
    struct OpenerCandidate
    {
        bool self = false;     // the bot the run is driving (the one at the door)
        bool alive = false;    // alive and in the world, on the object's map
        bool inReach = false;  // within interaction reach of the object
        bool canOpen = false;  // CanOpenSlots with this member's own keys and skill
    };

    // WHO OPENS A KEYED OBJECT: the member who can, the way a player at that
    // member's keyboard would: alive, within reach of it, and carrying one of
    // its keys or the lockpicking it asks. The bot itself first, so a door the
    // tank holds the key for opens as it always did; otherwise the first other
    // member who qualifies. -1 when nobody does, and the object stays shut.
    inline int PickOpener(std::vector<OpenerCandidate> const& party)
    {
        int other = -1;
        for (std::size_t i = 0; i < party.size(); ++i)
        {
            OpenerCandidate const& c = party[i];
            if (!c.alive || !c.inReach || !c.canOpen)
                continue;
            if (c.self)
                return static_cast<int>(i);
            if (other < 0)
                other = static_cast<int>(i);
        }
        return other;
    }
}

#endif
