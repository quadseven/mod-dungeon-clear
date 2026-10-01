/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under the terms of the GNU Affero General Public License as published by the FSF, either version 3
 * of the License, or (at your option) any later version.
 */

#ifndef _PLAYERBOT_DUNGEONEXPECTEDENCOUNTERMASKVALUE_H
#define _PLAYERBOT_DUNGEONEXPECTEDENCOUNTERMASKVALUE_H

#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// The completed-encounter bits for the bosses the dungeon brain keeps in this
// character's current wing. Non-split maps get their full boss roster.
class DungeonExpectedEncounterMaskValue : public CalculatedValue<uint32>
{
public:
    DungeonExpectedEncounterMaskValue(PlayerbotAI* botAI)
        : CalculatedValue<uint32>(botAI, DcKey::ExpectedEncounterMask, 1000)
    {
    }

protected:
    uint32 Calculate() override;
};

#endif
