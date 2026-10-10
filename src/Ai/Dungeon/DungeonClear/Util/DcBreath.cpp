/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include <array>   // std::array must be complete before GridTerrainData.h (core header omits it)

#include "DcBreath.h"

#include "Optional.h"          // Optional<> used (unguarded) by GridTerrainData.h
#include "GridTerrainData.h"   // LiquidData, LIQUID_MAP_*, MAP_ALL_LIQUIDS
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "Player.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"

namespace
{
    // Deeper than this and a straight climb is not "just above": the trail
    // back is the safer bet.
    constexpr float kMaxClimb = 60.0f;
}

namespace DcBreathGame
{
    bool IsUnderWater(Player* bot)
    {
        if (!bot)
            return false;
        LiquidData const& liquid = bot->GetLiquidData();
        return (liquid.Flags & MAP_ALL_LIQUIDS) != 0 &&
               (liquid.Status & LIQUID_MAP_UNDER_WATER) != 0;
    }

    int32 MaxMs(Player* bot)
    {
        if (!bot || !bot->IsAlive() || bot->HasWaterBreathingAura())
            return 0;
        WorldSession* session = bot->GetSession();
        if (session && session->GetSecurity() >=
                           AccountTypes(sWorld->getIntConfig(CONFIG_DISABLE_BREATHING)))
            return 0;
        float const bar = float(sWorld->getIntConfig(CONFIG_WATER_BREATH_TIMER)) *
                          bot->GetTotalAuraMultiplier(SPELL_AURA_MOD_WATER_BREATHING);
        return bar > 0.0f ? int32(bar) : 0;
    }

    bool OpenWaterAbove(Player* bot, DcBreath::Point& out)
    {
        if (!bot || !IsUnderWater(bot))
            return false;
        Map* map = bot->GetMap();
        if (!map)
            return false;
        float const x = bot->GetPositionX();
        float const y = bot->GetPositionY();
        float const z = bot->GetPositionZ();
        LiquidData const& liquid = bot->GetLiquidData();
        // Head out of the water: the core reads "under" while the surface is
        // more than the collision height above the feet, so a point one yard
        // under the surface is breathing.
        float const top = liquid.Level - 1.0f;
        if (top <= z || top - z > kMaxClimb)
            return false;
        if (!map->isInLineOfSight(x, y, z + 0.5f, x, y, top, bot->GetPhaseMask(),
                                  LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::Nothing))
            return false;
        out = DcBreath::Point{ x, y, top };
        return true;
    }

    DcBreath::Point Here(Player* bot)
    {
        if (!bot)
            return DcBreath::Point{};
        return DcBreath::Point{ bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() };
    }
}
