/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under the terms of the GNU Affero General Public License as published by the FSF, either version 3
 * of the License, or (at your option) any later version.
 */

#include "DungeonExpectedEncounterMaskValue.h"

#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Playerbots.h"

uint32 DungeonExpectedEncounterMaskValue::Calculate()
{
    return DungeonBossesExpectedEncounterMask(
        AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses));
}
