/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#pragma once
#include "Map.h"
#include <string>
class Group;
// SDK adapters for the lost-healer classification: only the surface it reads.
constexpr unsigned PLAYER_FLAGS_GHOST = 0x10;
constexpr unsigned SPELL_EFFECT_TELEPORT_UNITS = 5;
enum CurrentSpellTypes
{
    CURRENT_GENERIC_SPELL,
    CURRENT_CHANNELED_SPELL
};
struct SpellInfo
{
    unsigned Id = 0;
    bool teleportEffect = false;
    bool HasEffect(unsigned effect) const { return teleportEffect && effect == SPELL_EFFECT_TELEPORT_UNITS; }
};
struct Spell
{
    SpellInfo const* info = nullptr;
    SpellInfo const* GetSpellInfo() const { return info; }
};
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
    bool ghost = false, teleporting = false;
    Spell* genericCast = nullptr;
    Spell* channeledCast = nullptr;
    unsigned GetGUID() const { return guid; }
    unsigned GetMapId() const { return map ? map->id : 0; }
    bool isDead() const { return !alive; }
    bool isResurrectRequestedBy(unsigned caster) const { return offerBy && offerBy == caster; }
    bool IsAlive() const { return alive; }
    bool HasPlayerFlag(unsigned flag) const { return flag == PLAYER_FLAGS_GHOST && ghost; }
    bool IsBeingTeleported() const { return teleporting; }
    Spell* GetCurrentSpell(CurrentSpellTypes type) const
    {
        return type == CURRENT_GENERIC_SPELL ? genericCast : channeledCast;
    }
    Group* GetGroup() const { return group; }
    std::string const& GetName() const { return name; }
};
