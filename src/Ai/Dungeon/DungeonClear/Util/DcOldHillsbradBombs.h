/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DC_OLD_HILLSBRAD_BOMBS_H
#define _PLAYERBOT_DC_OLD_HILLSBRAD_BOMBS_H

namespace DcOldHillsbradBombs
{
    enum class Action
    {
        Continue,
        ContinueWithoutBombs
    };

    constexpr Action Decide(bool tankHasBombs, bool partyMemberHasBombs)
    {
        return tankHasBombs || partyMemberHasBombs ? Action::Continue : Action::ContinueWithoutBombs;
    }
}

#endif
