/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCATBOSS_H
#define _PLAYERBOT_DCATBOSS_H

#include "Ai/Dungeon/DungeonClear/Util/DcAtBossGate.h"

class AiObjectContext;
class Player;
class PlayerbotAI;
struct DungeonBossInfo;

namespace DcAtBoss
{
    // The at-boss gate, live: gathers DcAtBossGate::Inputs for `next` from the
    // bot's state, decides, writes the valve clock back to DcApproachState, and
    // logs the gate that is down (rationed, at the boss only).
    //
    // DungeonClearAtBossTrigger fires on `fire`; DungeonClearAdvanceAction's
    // engage hold holds on the same verdict and names its `gate`. Both call this,
    // so the two can no longer disagree. Safe to call more than once a tick: the
    // clock write is idempotent, and the side-effecting reads it makes (the sealed
    // muster's clock, the raid muster) are re-entrant within a tick.
    DcAtBossGate::Verdict Evaluate(Player* bot, PlayerbotAI* botAI, AiObjectContext* context,
                                   DungeonBossInfo const& next);
}

#endif  // _PLAYERBOT_DCATBOSS_H
