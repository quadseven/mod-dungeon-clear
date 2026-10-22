/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTSTARTOPTIONS_H
#define _PLAYERBOT_DCTESTSTARTOPTIONS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "TestRun/DcTestComp.h"

// Pure kernel for the two `.dc test start` / `.dc test plan start` options that
// shape WHO runs and WHERE the run begins, so a fix to one dungeon event can be
// tested in minutes instead of waiting for a party to hit it:
//
//   exclude=<class>[,<class>...]   never field these classes (DcTestComp)
//   from=<boss>                    start at that boss instead of the entrance
//
// Engine-free like DcTestPlan / DcTestRoster: the argument parsing, the role
// check and the boss resolution are unit-testable in isolation. The command and
// the plan parser both feed words through Parser, and DcTestRunJob applies the
// resolved StartPlan.

namespace DcTestStartOptions
{
    struct Options
    {
        std::uint32_t excludeMask = 0;  // DcTestComp::ClassBit of each excluded class
        std::string startBoss;          // as typed; empty = start at the entrance
    };

    // `exclude=` value: comma-separated class names. False with *err naming the
    // unknown class (and the valid spellings) on a bad name or an empty list.
    bool ParseExclude(std::string const& csv, std::uint32_t* maskOut, std::string* err);

    // Refuse an exclusion that leaves a role with no class to field, naming the
    // role. `size` 0 is the classic 5-man (see DcTestComp::UnfillableRole).
    bool CheckExclude(std::uint32_t excludeMask, DcTestComp::Roster roster, std::size_t size,
                      std::string* err);

    // Feeds the words of a start command to the two options. A boss name may
    // hold spaces ("from=Mr. Smite"), so a bare word that follows `from=` and is
    // not another option extends the name; `_` also stands for a space.
    class Parser
    {
    public:
        enum class Result : std::uint8_t
        {
            NotMine,   // not an exclude=/from= word and not a continuation: caller handles it
            Consumed,
            Error,     // *err says why
        };

        // `word` is one whitespace-delimited token. `isKnownBareWord` lets the
        // caller keep its own bare words (heroic, 10, 25) out of a boss name.
        Result Consume(std::string const& word, bool isKnownBareWord, std::string* err);

        Options const& options() const { return _opts; }

    private:
        Options _opts;
        bool _inFrom = false;
    };

    // One entry of a dungeon's boss list in progression order: what the run
    // would clear, bosses and the travel objectives between them.
    struct Anchor
    {
        std::string name;
        std::uint32_t entry = 0;
        bool isBoss = true;
        // Where the party can be dropped to begin at this anchor: an objective's
        // own spot, or the first waypoint of a boss's registered route. The
        // engine fills it; false when nothing is known.
        bool hasStart = false;
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    struct StartPlan
    {
        std::string bossName;                  // the boss `from=` resolved to, as spelled by the roster
        std::vector<std::uint32_t> skipEntries;  // earlier anchors, exactly what `dc skip` records
        bool atEntrance = false;               // nothing precedes it: use the registry's entrance
        float x = 0.0f, y = 0.0f, z = 0.0f;    // otherwise, where the party is placed
    };

    // Resolve `from=<query>` against the roster. Matches a BOSS by name, case
    // blind: exact first, else a unique substring (ambiguous is refused). An
    // unknown name is refused with the list of valid names.
    //
    // Everything before the boss counts as skipped, except the travel objectives
    // that sit directly in front of it: those gate it (the Deadmines Iron Clad
    // Door lies between Gilnid and Mr. Smite), so the run starts AT the first of
    // them and still performs them. Where the party stands needs a known spot:
    // refused, naming the anchor, when the roster has none for it.
    bool ResolveFrom(std::vector<Anchor> const& roster, std::string const& query,
                     StartPlan* out, std::string* err);
}

#endif  // _PLAYERBOT_DCTESTSTARTOPTIONS_H
