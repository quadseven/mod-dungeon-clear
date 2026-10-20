/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- The Deadmines (map 36) — the DEFIAS CANNON, ANCHORED + PERSISTENT ---
// The way from the goblin foundry to the pirate ship is sealed by the Iron Clad
// Door (GO 16397, lock 202 — rogue-pick only, so a bot party can never click it
// open). The intended opening: loot the Defias Gunpowder chest (GO 17155) for
// the gunpowder, then use it on the Defias Cannon (GO 16398) — that casts spell
// 6250 at the cannon, whose SmartGameObjectAI fires it: the door swings to
// GO_STATE_ACTIVE_ALTERNATIVE ("closed door open by cannon fire"), two Defias
// Squallshapers are summoned, and the cannon instance data is set.
//
// Navigation is by the boss list: the cannon is added as an OBJECTIVE anchor
// (BossRosterRegistry, encounterIndex 3 — shared with Mr. Smite, the first ship
// boss; the objective-before-boss tie-break runs it first). It gates the way
// between Gilnid (bit 2, last foundry boss) and the whole ship (Mr. Smite bit 3,
// Cookie 4, Greenskin 5, VanCleef 6). Boss-nav drives the tank to the cannon
// exactly as to any boss; the event then fires it in place.
//
// The cannon's lock (83) is an ITEM lock requiring the Defias Gunpowder (item
// 5397) as the key, so spell 6250 only fires the cannon when cast FROM that
// item. The FireDefiasCannon hook earns the gunpowder the way a player does:
// when nobody in the party carries it, the bot walks ~42yd up to the Defias
// Gunpowder chest (GO 17155), opens it and loots the gunpowder, then walks back
// and uses it on the cannon (the item-use path supplies the cast-item the
// OPEN_LOCK needs). Nothing is granted. The gunpowder has a 2s "Opening" cast.
// See ObjectiveHookRegistry and Util/DcDefiasGunpowder.h.
//
// STEPS OWN MOVEMENT: the hook walks the tank to the chest and back, and the
// at-objective hold would otherwise cancel that walk every tick.
//
// PERSISTENT: firing the cannon summons two Squallshapers that immediately
// engage, so the event sees a >1s Drive gap on the combat engine right after the
// Custom step. A non-persistent event would rewind to step 0 across that gap and
// re-fire the cannon (summoning two more each time); persistence keeps the step
// progress so the cannon fires exactly once.

void RegisterDeadminesEvents(std::vector<DungeonEvent>& out)
{
    constexpr uint32 DM_GO_IRON_CLAD_DOOR = 16397;
    constexpr uint32 DM_DOOR_OPEN_STATE   = 2;  // GO_STATE_ACTIVE_ALTERNATIVE
    constexpr uint32 DM_FIRE_CANNON_HOOK  = 2;  // ObjectiveHookRegistry id
    // Covers a slow chest round trip: the ~42yd walk to the chest and back
    // plus the two casts can run past 30s on a bad path, and the old bound
    // false-stalled the step before it recovered (the 3x no-progress watchdog
    // then stalled the run until `dc skip`). A gunpowder looted before a
    // timeout stays in the bags, so the retry only walks back onto the cannon
    // and uses it.
    constexpr uint32 DM_FIRE_CANNON_TIMEOUT_MS = 90000;
    constexpr uint32 DM_FIRE_CANNON_RETRIES    = 2;

    out.push_back(
        EventBuilder(36, 1, "Iron Clad Door (Defias Cannon)")
            .Anchored(/*encounterIndex*/ 3)
            .Persistent()
            // A failed cannon ends the run with a logged reason instead of
            // stalling for a person: the retries below bound it to about five
            // minutes, and nobody is at the door to sort it (#54).
            .AbortOnFailure()
            .StepsOwnMovement()
            // 1. Step up onto the cannon (the objective's arrive radius may
            //    leave the tank ~10yd off; the cannon GO is at this spot).
            .MoveTo(-107.56f, -659.67f, 7.21f, /*radius*/ 5.0f)
            // 2. Fire it. The hook loots the gunpowder from its chest if nobody
            //    carries it, then uses it on the cannon GO (spell 6250) -> its
            //    SmartAI opens the door + summons the Squallshapers. The hook
            //    returns Running until the door opens, so its timeout is what
            //    ends a stuck attempt: walk back onto the cannon and try again,
            //    twice, then fail the event rather than hold at the door.
            //    Pinned by t/TestDungeonEvent.cpp,
            //    DungeonEventAdvance.DeadminesCannonCustomTimeoutRewindsThenFails.
            .Custom(DM_FIRE_CANNON_HOOK)
            .Timeout(DM_FIRE_CANNON_TIMEOUT_MS)
            .RetryOnTimeout(DM_FIRE_CANNON_RETRIES)
            // 3. Hold until the Iron Clad Door is confirmed open, then the clear
            //    advances through it to Mr. Smite and the rest of the ship.
            .WaitForGOState(DM_GO_IRON_CLAD_DOOR, DM_DOOR_OPEN_STATE)
            .Timeout(30000)
            .Build());
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterDeadminesRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- The Deadmines (map 36) — Defias Cannon door ------------------
    // The Iron Clad Door (GO 16397, lock 202 — rogue-pick only) seals the
    // foundry from the pirate ship; a bot party can never click it open.
    // Add the Defias Cannon (GO 16398) as an OBJECTIVE anchor so boss-nav
    // drives the tank to it between Gilnid (bit 2, last foundry boss) and
    // Mr. Smite (bit 3, first ship boss); the event (eventId 1) fires the
    // cannon, opening the door to the whole ship (Smite 3, Cookie 4,
    // Greenskin 5, VanCleef 6). encounterIndex 3 is shared with Mr. Smite:
    // the objective-before-boss tie-break + the picker's strictly-greater
    // advance hand the objective over first, then the boss. No gateEntry
    // (the event owns completion via the door-open gate; see
    // DeadminesEvents).
    {
        BossRosterPatch p;
        p.mapId = 36;
        p.add = {
            MakeObjective(OBJ(1), /*encounterIndex*/ 3, 36, "Iron Clad Door",
                          -107.56f, -659.67f, 7.21f, /*arriveRadius*/ 10.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1),
        };
        t.push_back(std::move(p));
    }
}
