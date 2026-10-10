/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcDoorOpener.h"

#include <vector>

#include "DBCStores.h"
#include "GameObject.h"
#include "Group.h"
#include "Player.h"
#include "SharedDefines.h"

namespace DcDoorOpener
{
    bool ReadLock(GameObject const* go, DcDoorPolicy::LockSlot (&slots)[DcDoorPolicy::LOCK_SLOT_COUNT])
    {
        if (!go)
            return false;
        GameObjectTemplate const* info = go->GetGOInfo();
        uint32 const lockId = info ? info->GetLockId() : 0;
        if (!lockId)
            return false;
        LockEntry const* lock = sLockStore.LookupEntry(lockId);
        if (!lock)
            return false;
        for (std::size_t i = 0; i < MAX_LOCK_CASE && i < DcDoorPolicy::LOCK_SLOT_COUNT; ++i)
        {
            slots[i].keyType = lock->Type[i];
            slots[i].index = lock->Index[i];
            slots[i].requiredSkill = lock->Skill[i];
        }
        return true;
    }

    namespace
    {
        DcDoorPolicy::OpenerCandidate Candidate(Player* member, Player* bot, GameObject* go,
                                                DcDoorPolicy::LockSlot const* slots,
                                                bool lockEnforced, float reach)
        {
            DcDoorPolicy::OpenerCandidate c;
            c.self = member == bot;
            c.alive = member->IsInWorld() && member->IsAlive() && member->GetMap() == go->GetMap();
            if (!c.alive)
                return c;
            // The bot is the one the run walks to the object: whether it is
            // close enough to click is its caller's question (the door-blocked
            // action holds short of a far door, the UseGO step walks in). Any
            // other member is the bot's company, at its side.
            c.inReach = c.self || member->IsWithinDistInMap(bot, reach);
            int32 const lockpick = member->HasSkill(SKILL_LOCKPICKING)
                                       ? static_cast<int32>(member->GetSkillValue(SKILL_LOCKPICKING))
                                       : -1;
            // Keys are not consumed by a door, so carrying one is the whole
            // requirement. Bags and key ring only: a key in the bank opens
            // nothing.
            c.canOpen = DcDoorPolicy::CanOpenSlots(
                slots, DcDoorPolicy::LOCK_SLOT_COUNT, lockEnforced,
                [member](uint32 itemEntry) { return member->HasItemCount(itemEntry, 1, false); },
                lockpick);
            return c;
        }
    }

    Player* PartyOpener(Player* bot, GameObject* go,
                        DcDoorPolicy::LockSlot const (&slots)[DcDoorPolicy::LOCK_SLOT_COUNT],
                        bool lockEnforced, float reach)
    {
        if (!bot || !go)
            return nullptr;
        std::vector<Player*> members{bot};
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (member != bot)
                        members.push_back(member);
        std::vector<DcDoorPolicy::OpenerCandidate> party;
        party.reserve(members.size());
        for (Player* member : members)
            party.push_back(Candidate(member, bot, go, slots, lockEnforced, reach));
        int const chosen = DcDoorPolicy::PickOpener(party);
        return chosen < 0 ? nullptr : members[static_cast<std::size_t>(chosen)];
    }
}
