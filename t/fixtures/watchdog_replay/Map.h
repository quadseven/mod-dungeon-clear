/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#pragma once
class Map
{
public:
    unsigned id = 1;
    bool dungeon = false, raid = false;
    bool IsDungeon() const { return dungeon; }
    bool IsRaid() const { return raid; }
};
