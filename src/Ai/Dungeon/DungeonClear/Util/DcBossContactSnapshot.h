/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCBOSSCONTACTSNAPSHOT_H
#define _PLAYERBOT_DCBOSSCONTACTSNAPSHOT_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Pure formatter for the one line written when the run's OBJECTIVE boss first
// touches a party member (Refs #130).
//
// Why it exists: the first Emperor Dagran Thaurissan pull (run 1823401) reached
// him with no member in combat while the at-boss gate read "party not ready", and
// thirty-five room mobs then took the tank in twenty seconds. The logs held the
// tank's position and the death samples, but not what the tank was chasing, which
// gate verdict was last standing, or how much health and mana each member carried
// into the fight. This line is that record. Engine-free so the field set and the
// no-mana / no-target shapes are pinned by t/TestBossContactSnapshot.
namespace DcBossContactSnapshot
{
    struct Member
    {
        std::string name;
        std::uint32_t hpPct = 0;
        int manaPct = -1;          // -1: the member has no mana bar
        bool inCombat = false;
        bool alive = true;
        float distToBoss = 0.0f;
    };

    struct Inputs
    {
        std::string contactName;       // the member the boss touched first
        std::string bossName;
        float contactDistToBoss = 0.0f;
        std::string targetName;        // what the member is attacking now; empty: none
        float targetDist = 0.0f;
        std::uint32_t roomTrashLeft = 0;
        bool roomGaveUp = false;
        std::string lastGate;          // last at-boss gate verdict logged; empty: none
        std::uint32_t pullPhase = 0;
        std::vector<Member> members;
    };

    inline std::string Format(Inputs const& in)
    {
        auto f1 = [](float v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
            return std::string(buf);
        };

        std::string out = "[DC:" + in.contactName + "] boss contact snapshot: " + in.bossName +
                          " at " + f1(in.contactDistToBoss) + "yd | target ";
        if (in.targetName.empty())
            out += "none";
        else
            out += in.targetName + " " + f1(in.targetDist) + "yd";
        out += " | room trash " + std::to_string(in.roomTrashLeft) + " left, room-clear " +
               (in.roomGaveUp ? "gave up" : "still going") + " | last gate " +
               (in.lastGate.empty() ? std::string("none") : in.lastGate) + " | pull phase " +
               std::to_string(in.pullPhase) + " | party:";
        for (Member const& m : in.members)
        {
            out += " " + m.name;
            if (!m.alive)
            {
                out += " dead";
                continue;
            }
            out += " hp " + std::to_string(m.hpPct) + "% mana ";
            out += m.manaPct < 0 ? std::string("none") : std::to_string(m.manaPct) + "%";
            out += m.inCombat ? " fighting " : " idle ";
            out += f1(m.distToBoss) + "yd;";
        }
        return out;
    }
}

#endif  // _PLAYERBOT_DCBOSSCONTACTSNAPSHOT_H
