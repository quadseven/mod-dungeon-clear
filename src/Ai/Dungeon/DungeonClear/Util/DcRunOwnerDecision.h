/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option) any later version.
 */

#ifndef _PLAYERBOT_DCRUNOWNERDECISION_H
#define _PLAYERBOT_DCRUNOWNERDECISION_H

#include <cstdint>

// A run's owner can leave the dungeon map as a released ghost while the party
// and the raisable corpse remain in the same instance. Keep that run resolvable
// only when the corpse map and the owner's last live run identity both match the
// exact map instance asking for the owner.
namespace DcRunOwnerDecision
{
    struct Candidate
    {
        bool enabled = false;
        bool alive = false;
        uint32_t mapId = 0;
        uint32_t instanceId = 0;
        bool hasCorpse = false;
        uint32_t corpseMapId = 0;
        uint32_t lastRunMapId = 0;
        uint32_t lastRunInstanceId = 0;
    };

    constexpr bool IsOwnerForInstance(Candidate const& candidate,
                                      uint32_t referenceMapId,
                                      uint32_t referenceInstanceId)
    {
        return candidate.enabled && candidate.mapId == referenceMapId &&
               candidate.instanceId == referenceInstanceId;
    }
}

#endif  // _PLAYERBOT_DCRUNOWNERDECISION_H
