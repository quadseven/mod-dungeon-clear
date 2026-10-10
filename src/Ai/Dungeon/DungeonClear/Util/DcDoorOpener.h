/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCDOOROPENER_H
#define _PLAYERBOT_DCDOOROPENER_H

#include "Ai/Dungeon/DungeonClear/Util/DcDoorPolicy.h"

class GameObject;
class Player;

// WHO IN THE PARTY OPENS A KEYED OBJECT. The server opens a door or a button on
// GameObject::Use() with no lock check at all: the key is the client's gate (a
// player without it cannot click, or casts an Opening that the core refuses). A
// bot has no client, so the run is the only thing that can hold it to the key.
//
// A keyed object (DcDoorPolicy::LockNamesKey) is opened by the party member who
// could open it at a keyboard: alive, on the object's map, at the side of the
// bot the run walks to it (within `reach` of that bot), and carrying one of the
// lock's key items or the lockpicking it asks (DcDoorPolicy::CanOpenSlots with
// that member's own bags and skill). The bot itself is asked first; whether it
// stands close enough to click is its caller's rule. The opener then Use()s it
// as itself.
namespace DcDoorOpener
{
    // The object's lock, decoded from Lock.dbc. False when it has no lock row
    // (lockId 0 or an unknown id); the slots are then left untyped.
    bool ReadLock(GameObject const* go, DcDoorPolicy::LockSlot (&slots)[DcDoorPolicy::LOCK_SLOT_COUNT]);

    // The party member, `bot` first, who can open `go`'s lock as a player
    // would, standing within `reach` of `bot`; nullptr when nobody can.
    // `lockEnforced` is the object's GO_FLAG_LOCKED (bare-hands slots then do
    // not count).
    Player* PartyOpener(Player* bot, GameObject* go,
                        DcDoorPolicy::LockSlot const (&slots)[DcDoorPolicy::LOCK_SLOT_COUNT],
                        bool lockEnforced, float reach);
}

#endif
