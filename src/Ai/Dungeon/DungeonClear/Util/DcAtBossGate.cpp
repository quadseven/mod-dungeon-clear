/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcAtBossGate.h"

namespace DcAtBossGate
{
    char const* Name(Gate g)
    {
        switch (g)
        {
            case Gate::Open:          return "open";
            case Gate::NotDriving:    return "not driving";
            case Gate::NotBossAnchor: return "not a boss anchor";
            case Gate::EncounterHold: return "encounter hold";
            case Gate::PullOwns:      return "pull owns the tank";
            case Gate::NotAtEngage:   return "not at engage";
            case Gate::RoomTrash:     return "room trash";
            case Gate::ClosedDoor:    return "closed door";
            case Gate::AnchoredHops:  return "anchored hops ahead";
            case Gate::SealedMuster:  return "sealed muster";
            case Gate::RezPending:    return "rez pending";
            case Gate::RaidMuster:    return "raid muster";
            case Gate::PartyNotReady: return "party not ready";
        }
        return "?";
    }

    bool ValveCanOverride(Gate g)
    {
        return g == Gate::RoomTrash || g == Gate::SealedMuster;
    }

    bool DoorVetoes(bool losBlocked, std::uint32_t shutDoorsOnChord,
                    std::uint32_t eventGatesOnChord)
    {
        return losBlocked && shutDoorsOnChord > eventGatesOnChord;
    }

    bool PullStandsDownAtBoss(bool atBossEngage, bool roomClearActive, bool pullbackBoss,
                              bool atBossValveOpen)
    {
        if (!atBossEngage || pullbackBoss)
            return false;
        return !roomClearActive || atBossValveOpen;
    }

    Gate FirstClosed(Inputs const& in)
    {
        if (!in.driving)
            return Gate::NotDriving;
        if (!in.bossAnchor)
            return Gate::NotBossAnchor;
        if (in.encounterHold)
            return Gate::EncounterHold;
        if (in.pullOwns)
            return Gate::PullOwns;
        if (!in.atEngage)
            return Gate::NotAtEngage;
        if (in.roomTrash)
            return Gate::RoomTrash;
        if (in.closedDoor)
            return Gate::ClosedDoor;
        if (in.anchoredHops)
            return Gate::AnchoredHops;
        if (in.sealedHolds)
            return Gate::SealedMuster;
        if (in.raid)
        {
            if (in.rezPending)
                return Gate::RezPending;
            if (in.raidMusterHolds)
                return Gate::RaidMuster;
            return Gate::Open;
        }
        if (!in.partyReady)
            return Gate::PartyNotReady;
        return Gate::Open;
    }

    namespace
    {
        // Every gate that is down is one the valve may open past. Asked of the
        // whole set, not just the first: a room-trash hold with a real door
        // behind it must stay held.
        bool OnlyOverridableDown(Inputs const& in)
        {
            if (!in.driving || !in.bossAnchor || in.encounterHold || in.pullOwns ||
                !in.atEngage || in.closedDoor || in.anchoredHops)
                return false;
            return in.roomTrash || in.sealedHolds;
        }
    }

    Verdict Decide(Inputs const& in)
    {
        Verdict v;
        v.gate = FirstClosed(in);
        if (v.gate == Gate::Open)
        {
            v.fire = true;
            return v;
        }

        bool const eligible = !in.raid && in.liveBoss && in.partyReady && !in.lootPending &&
                              OnlyOverridableDown(in);
        if (!eligible)
            return v;  // not a valve hold: the clock resets

        // 0 is the "not holding" sentinel, so a hold that starts on the tick the
        // millisecond clock reads 0 is stamped 1.
        v.heldSinceMs = in.heldSinceMs ? in.heldSinceMs : (in.nowMs ? in.nowMs : 1u);
        v.heldMs = in.heldSinceMs ? in.nowMs - in.heldSinceMs : 0u;  // unsigned: wrap-safe
        if (in.valveMs && v.heldMs >= in.valveMs)
        {
            v.fire = true;
            v.valveOpen = true;
        }
        return v;
    }
}
