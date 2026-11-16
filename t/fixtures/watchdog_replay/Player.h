/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#pragma once
#include "Map.h"
#include <string>
class Group;
class WorldObject
{
public:
    bool inWorld = true;
    Map* map = nullptr;
    bool IsInWorld() const { return inWorld; }
    Map* GetMap() const { return map; }
};
class Player : public WorldObject
{
public:
    bool alive = true;
    bool heals = false;
    bool resurrect = false;
    unsigned guid = 0;
    std::string name;
    Group* group = nullptr;
    unsigned offerBy = 0;
    unsigned GetGUID() const { return guid; }
    unsigned GetMapId() const { return map ? map->id : 0; }
    bool isDead() const { return !alive; }
    bool isResurrectRequestedBy(unsigned caster) const { return offerBy && offerBy == caster; }
    bool IsAlive() const { return alive; }
    Group* GetGroup() const { return group; }
    std::string const& GetName() const { return name; }
};
