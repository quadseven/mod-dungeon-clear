/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCATBOSSGATE_H
#define _PLAYERBOT_DCATBOSSGATE_H

#include <cstdint>

// THE AT-BOSS GATE: ONE PREDICATE FOR THE HOLD AND THE TRIGGER.
//
// DungeonClearAdvanceAction::TryEngageHold parks the tank inside engage range
// and waits for DungeonClearAtBossTrigger to pull. The two used to carry their
// own copies of the test, and the trigger's half said nothing when it refused.
// Live, Blackfathom Deeps (2026-10-12): two tanks logged
//   within engage range of Gelihast (14yd, live=1) -> holding for at-boss
//   [partyReady=1 availLoot=0 canLoot=0 tankCombat=0]
// 1,911 times in five minutes, and nothing anywhere said which of the trigger's
// gates was down. No guild run cleared in 24 hours. The same shape stalled
// Razorfen Downs and Azjol-Nerub before (the second was the two copies of the
// anchored-segment loop disagreeing).
//
// So the gate is computed once, here, from observations the live caller
// (DcAtBoss::Evaluate) gathers, and both sides read the same Verdict: the
// trigger fires on `fire`, the hold logs `gate` by name.
//
// THE VALVE. A hold at a live boss with the party ready and no loot pending is
// a stall, whatever the reason; a few gates have no clock of their own that
// reliably ends one. When the only gates down are ones the valve may override
// (ValveCanOverride) and the tank has held that way for valveMs, the verdict
// fires anyway. The clock resets on any tick that is not such a hold, so a
// party that drops below ready, a pull, combat, or the gate opening all re-arm
// it. Dungeons only: a raid boss is gated by the raid muster, which has its own
// clock.
//
// Engine-free so it is unit-testable; the caller owns the clock (heldSinceMs)
// and writes back the returned one.
namespace DcAtBossGate
{
    // In evaluation order: the first one down is the one reported.
    enum class Gate : std::uint8_t
    {
        Open = 0,
        NotDriving,      // dead, or flagged in combat past the phantom-flag grace
        NotBossAnchor,   // the next anchor is a travel objective
        EncounterHold,   // an encounter script owns the pull (Blackwing Lair egg run)
        PullOwns,        // a pull maneuver or a latched scripted stage owns the tank
        NotAtEngage,     // out of engage range, or off the boss's floor
        RoomTrash,       // room-wide-aggro boss with room trash still up
        ClosedDoor,      // a real shut door on the tank->boss line
        AnchoredHops,    // registered-route anchors still ahead of the cursor
        SealedMuster,    // sealed encounter room with a member still outside
        RezPending,      // raid: a party resurrection is pending
        RaidMuster,      // raid: the pre-boss muster has not released
        PartyNotReady,   // dungeon: between-pulls readiness (rest, spread, loot)
    };

    char const* Name(Gate g);

    // Gates the valve may open past. Each is a hold that can be satisfied by
    // waiting and has no clock of its own that reliably ends it:
    //   RoomTrash    - RoomClearTimeout re-arms on every drop in the count, so a
    //                  set that flickers (respawn, a unit sliding in and out of
    //                  reach) never gives up.
    //   SealedMuster - bounded by its own timeout already; listed so a muster
    //                  that is the only thing left down is not a valve blind spot.
    // Never a real shut door (the tank would walk into it), never the anchored
    // route (the tank is still walking, not held), never readiness (the valve
    // requires it), never a pull or encounter script that owns the tank.
    bool ValveCanOverride(Gate g);

    // Does a blocked door ray veto the engage?
    //
    // DcEngageGeometry::ClosedDoorBetween is a GameObject-LOS ray, and GameObject
    // LOS is blocked by ANY GameObject with collision, not only doors: a hut, a
    // shrine, a pillar placed as a GameObject. On its own it vetoed a boss beside
    // such a model forever, with no door anywhere for door-blocked to work on.
    // A veto therefore needs a shut door from the door index on the line
    // (`shutDoorsOnChord`), and at least one of them that is not an
    // interact-through event gate (`eventGatesOnChord`, the Chromaggus cage
    // exception). A ray blocked by nothing in the door index is not a door.
    bool DoorVetoes(bool losBlocked, std::uint32_t shutDoorsOnChord,
                    std::uint32_t eventGatesOnChord);

    // Does the pull pipeline (DcRel::Pull, 35) stand down at the boss?
    //
    // At the boss the at-boss engage (DcRel::AtBoss, 30) owns the pull, so the
    // pull rung stands down there, with two exceptions: a pull-back boss (the pull
    // is the only thing that may engage him) and a room clear in progress (the
    // pull pipeline is what clears that room). The valve ends the second one.
    // Once the valve has opened past room trash, the room clear has had its
    // chance and the boss is engaged anyway; a pull rung that kept the room-clear
    // exception would outrank the very engage the valve opened, every tick.
    // Live, Blackfathom Deeps (2026-10-27, run 2798): the valve at Gelihast
    // logged "engaging anyway" 30 times over 14 minutes while the tank never
    // moved, because the room-clear pull at 35 held the tick the engage at 30
    // needed. A pull-back boss stays with the pull, valve or not.
    bool PullStandsDownAtBoss(bool atBossEngage, bool roomClearActive, bool pullbackBoss,
                              bool atBossValveOpen);

    struct Inputs
    {
        bool driving = false;
        bool bossAnchor = false;
        bool encounterHold = false;
        bool pullOwns = false;
        bool atEngage = false;
        bool roomTrash = false;
        bool closedDoor = false;       // already DoorVetoes()
        bool anchoredHops = false;
        bool sealedHolds = false;
        bool raid = false;
        bool rezPending = false;       // read on raid maps only
        bool raidMusterHolds = false;  // read on raid maps only
        bool partyReady = false;       // read on dungeon maps only
        bool lootPending = false;      // tank-side loot flags
        bool liveBoss = false;         // the boss creature is loaded and alive

        std::uint32_t nowMs = 0;
        std::uint32_t heldSinceMs = 0;  // the caller's clock; 0 = not holding
        std::uint32_t valveMs = 0;      // 0 = valve off
    };

    struct Verdict
    {
        Gate gate = Gate::Open;          // first gate down; Open when none is
        bool fire = false;               // the at-boss engage may run this tick
        bool valveOpen = false;          // fired through the valve, past `gate`
        std::uint32_t heldSinceMs = 0;   // write back to the caller's clock
        std::uint32_t heldMs = 0;        // how long the valve-eligible hold has run
    };

    Gate FirstClosed(Inputs const& in);
    Verdict Decide(Inputs const& in);
}

#endif  // _PLAYERBOT_DCATBOSSGATE_H
