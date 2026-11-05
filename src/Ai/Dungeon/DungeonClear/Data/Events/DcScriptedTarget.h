/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSCRIPTEDTARGET_H
#define _PLAYERBOT_DCSCRIPTEDTARGET_H

#include <cstddef>
#include <string>
#include <vector>

#include "Common.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// A SCRIPTED TARGET: a friendly, non-boss NPC the party must talk to at each of
// a fixed run of stops before it turns hostile and fights. It is not a
// DungeonEncounter boss, so the derived roster never sends a run to it, and its
// stops are far apart (200-280yd), past what an event's short MoveTo hop may
// drive. One row of this type expands into the shapes the clear already runs:
//
//   * one travel OBJECTIVE per stop, so boss navigation delivers the tank to
//     each stop in order (the long haul stays with the anchor machinery);
//   * one ANCHORED event per stop that gossips the NPC (option `gossipOption`).
//     The NPC runs off to the next stop on each gossip, and the next objective
//     follows it there. The LAST stop's event also kills the NPC once the final
//     gossip turns it hostile;
//   * the objectives' entries for the wing list, so wing filtering keeps them.
//
// Dire Maul East's Pusillin (DireMaulEvents.cpp) is the first row: five
// gossips, then he fights and drops the Crescent Key to every party member.
struct DcScriptedStop
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct DcScriptedTarget
{
    uint32 mapId{0};
    uint32 creatureEntry{0};
    std::string name;
    int32 gossipOption{0};
    // Where the NPC waits for each gossip, in order: its spawn first, then the
    // end of each scripted run. stops.size() is the gossip count.
    std::vector<DcScriptedStop> stops;

    // Id ranges on the map, one per stop, counting up from these.
    uint32 firstEventId{0};
    uint32 firstObjectiveSeq{0};
    uint32 firstEncounterIndex{0};   // synthetic highs, never a DBC kill-bit
    int32 firstOrder{0};             // clear-path keys, one per stop

    float arriveRadius{12.0f};
    // Gossip step timeout: covers the NPC's run to the stop plus the gossip.
    uint32 gossipTimeoutMs{120000};
    // Search radius for the final kill gate.
    float killRadius{80.0f};
};

namespace DcScripted
{
    inline std::string StopName(DcScriptedTarget const& t, std::size_t i)
    {
        std::string name = t.name + ": gossip " + std::to_string(i + 1) + " of " +
                           std::to_string(t.stops.size());
        if (i + 1 == t.stops.size())
            name += ", then kill";
        return name;
    }

    // One anchored event per stop. Each is Persistent (a combat gap must not
    // rewind it) and Optional (a gossip that never lands degrades to moving on).
    // SkipIfTargetMissing lets every step pass when the NPC is already dead (a
    // re-entered instance), so a spent target never holds the run.
    inline std::vector<DungeonEvent> Events(DcScriptedTarget const& t)
    {
        std::vector<DungeonEvent> out;
        for (std::size_t i = 0; i < t.stops.size(); ++i)
        {
            uint32 const id = t.firstEventId + static_cast<uint32>(i);
            EventBuilder b(t.mapId, id, StopName(t, i));
            b.Anchored(/*orderIndex, doc-only*/ id)
                .Gossip(t.creatureEntry, t.gossipOption, /*searchRadius*/ 100.0f)
                .SkipIfTargetMissing()
                .Timeout(t.gossipTimeoutMs);
            if (i + 1 == t.stops.size())
                b.KillCreatureEngage(t.creatureEntry, 1, t.killRadius).Timeout(180000);
            out.push_back(b.Persistent().Optional().Build());
        }
        return out;
    }

    // One travel objective per stop, wired to the stop's event.
    inline std::vector<DungeonBossInfo> Objectives(DcScriptedTarget const& t)
    {
        std::vector<DungeonBossInfo> out;
        for (std::size_t i = 0; i < t.stops.size(); ++i)
        {
            uint32 const n = static_cast<uint32>(i);
            DcScriptedStop const& s = t.stops[i];
            out.push_back(DcRoster::MakeObjective(
                DcRoster::OBJ(t.firstObjectiveSeq + n), t.firstEncounterIndex + n, t.mapId,
                StopName(t, i).c_str(), s.x, s.y, s.z, t.arriveRadius, /*gateEntry*/ 0,
                /*hook*/ 0, t.firstEventId + n, t.firstOrder + static_cast<int32>(n)));
        }
        return out;
    }

    // The objectives' synthetic entries, for the wing list.
    inline std::vector<uint32> WingEntries(DcScriptedTarget const& t)
    {
        std::vector<uint32> out;
        for (std::size_t i = 0; i < t.stops.size(); ++i)
            out.push_back(DcRoster::OBJ(t.firstObjectiveSeq + static_cast<uint32>(i)));
        return out;
    }
}

#endif
