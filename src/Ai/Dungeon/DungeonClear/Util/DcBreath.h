/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCBREATH_H
#define _PLAYERBOT_DCBREATH_H

#include "Define.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBreathDecision.h"

class Player;

// The game-facing half of surface-for-breath: what the core says about this
// bot's breath and the water around it. The decisions are in
// DcBreathDecision.h. Map thread only (liquid and VMAP reads).
namespace DcBreathGame
{
    // The core's own test for the breath bar draining
    // (Player::ProcessTerrainStatusUpdate -> UNDERWATER_INWATER): under the
    // surface of any liquid.
    bool IsUnderWater(Player* bot);

    // The bar's length in ms, as Player::getMaxTimer(BREATH_TIMER) computes it;
    // 0 when the bot has no breath timer (dead, Water Breathing, a GM account).
    int32 MaxMs(Player* bot);

    // A point just under the surface straight above the bot, when nothing but
    // water lies between (no tunnel roof). False when there is rock overhead,
    // the bot is not under water, or the surface is out of reach.
    bool OpenWaterAbove(Player* bot, DcBreath::Point& out);

    DcBreath::Point Here(Player* bot);
}

#endif
