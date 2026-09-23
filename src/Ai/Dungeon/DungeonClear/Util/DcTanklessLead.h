/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_TANKLESS_LEAD_H
#define _DC_TANKLESS_LEAD_H

// WHO DRIVES A PARTY THAT HAS NO TANK.
//
// DcLeaderSignal::FindLeaderTank elects the clear's leader from the alive tank
// BOTS on the reference's map, and a tank bot is one carrying a TANK-typed
// strategy (PlayerbotAI::IsTank). A party with none of those used to elect
// nobody, and nobody is a silent answer: DcOnAction's non-leader branch returns
// true for every member, so `dc on` issued to all five "succeeds" five times
// while no bot sets `enabled`, no trigger fires, and the party stands at the
// door resting between pulls that never come.
//
// Measured on a dev realm: a level 22-25 party of a fury warrior, a mage, a
// holy priest, a cat druid and an enhancement shaman stood at the Ragefire
// Chasm entrance for over an hour with `dc on` and three `dc skip`s all
// reported accepted, and not one line from this module past its instance
// transitions.
//
// THE RULE. In a PARTY with no tank at all on this map, human or bot, the
// group's own leader drives the clear, provided it is a bot (it needs a
// PlayerbotAI to run the ladder), alive, and on this Map. The group leader is
// the one character every member already agrees on, so the election stays
// deterministic without a new tiebreak.
//
// WHAT IT DELIBERATELY DOES NOT DO:
//   * It never overrides a tank. One tank bot anywhere on the map wins exactly
//     as before.
//   * A HUMAN tank on the map also blocks it. That party is being tanked by a
//     person, and a bot deciding where it goes would be driving over them.
//   * Raids keep their own rule (Main Tank flag, then gear score). A raid with
//     no tank is not a shape this module can carry, and guessing is worse than
//     the refusal DcOnAction now gives.
namespace DcTanklessLead
{
    constexpr bool GroupLeaderDrives(bool isRaid, bool anyTankOnMap, bool groupLeaderIsBot,
                                     bool groupLeaderAlive, bool groupLeaderOnMap)
    {
        return !isRaid && !anyTankOnMap && groupLeaderIsBot && groupLeaderAlive &&
               groupLeaderOnMap;
    }
}

#endif  // _DC_TANKLESS_LEAD_H
