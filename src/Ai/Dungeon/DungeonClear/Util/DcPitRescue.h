/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef MOD_DUNGEON_CLEAR_DC_PIT_RESCUE_H
#define MOD_DUNGEON_CLEAR_DC_PIT_RESCUE_H

// What a pit rescue event does on its tick. Two pits have no walk or swim exit:
// the Scarlet trap-alley pit in Stratholme (DcStratTrapAlley.h) and the void
// under Karazhan's Guardian's Library (DcKaraLibraryPit.h). The only way out of
// either is a party teleport, which is not something a player party can do, so
// it runs only with DungeonClear.PitRescueTeleport on (default off, like
// StrandedRecovery). With it off, a party in the pit ends the run with a reason
// instead of sitting on the 120s stall.
namespace DcPitRescue
{
    enum class Step
    {
        Out,       // nobody is in the pit any more (a rez, a member walked off): done
        Teleport,  // relocate the party to the landing
        Abort,     // end the run: no exit, and the teleport is not allowed
    };

    inline Step Decide(bool inPit, bool teleportAllowed)
    {
        if (!inPit)
            return Step::Out;
        return teleportAllowed ? Step::Teleport : Step::Abort;
    }

    // The settings key both rescues read.
    inline constexpr char const* SETTING = "PitRescueTeleport";
}

#endif
