/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestStartOptions.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace DcTestStartOptions
{
    namespace
    {
        constexpr char const* kClassNames =
            "warrior, paladin, hunter, rogue, priest, deathknight, shaman, mage, warlock, druid";

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // The form a boss name is compared in: lower case, `_` read as a space.
        std::string Fold(std::string s)
        {
            std::replace(s.begin(), s.end(), '_', ' ');
            return Lower(std::move(s));
        }
    }

    bool ParseExclude(std::string const& csv, std::uint32_t* maskOut, std::string* err)
    {
        std::uint32_t mask = 0;
        std::istringstream in{csv};
        std::string name;
        while (std::getline(in, name, ','))
        {
            std::uint8_t const classId = DcTestComp::ClassIdFromName(Lower(name));
            if (classId == 0)
            {
                if (err)
                    *err = "unknown class '" + name + "' in exclude= (" + kClassNames + ")";
                return false;
            }
            mask |= DcTestComp::ClassBit(classId);
        }
        if (mask == 0)
        {
            if (err)
                *err = std::string("exclude= needs at least one class (") + kClassNames + ")";
            return false;
        }
        *maskOut = mask;
        return true;
    }

    bool CheckExclude(std::uint32_t excludeMask, DcTestComp::Roster roster, std::size_t size,
                      std::string* err)
    {
        char const* const role = DcTestComp::UnfillableRole(excludeMask, roster, size);
        if (!role)
            return true;
        if (err)
            *err = std::string("exclude= leaves no class to fill the ") + role + " role";
        return false;
    }

    Parser::Result Parser::Consume(std::string const& word, bool isKnownBareWord, std::string* err)
    {
        std::size_t const eq = word.find('=');
        if (eq != std::string::npos)
        {
            std::string const key = word.substr(0, eq);
            std::string const val = word.substr(eq + 1);
            if (key == "exclude")
            {
                _inFrom = false;
                return ParseExclude(val, &_opts.excludeMask, err) ? Result::Consumed
                                                                  : Result::Error;
            }
            if (key == "from")
            {
                if (val.empty())
                {
                    if (err)
                        *err = "from= needs a boss name";
                    return Result::Error;
                }
                _opts.startBoss = val;
                _inFrom = true;
                return Result::Consumed;
            }
            _inFrom = false;
            return Result::NotMine;
        }

        if (_inFrom && !isKnownBareWord)
        {
            _opts.startBoss += ' ' + word;
            return Result::Consumed;
        }
        return Result::NotMine;
    }

    bool ResolveFrom(std::vector<Anchor> const& roster, std::string const& query,
                     StartPlan* out, std::string* err)
    {
        std::string const wanted = Fold(query);
        std::vector<std::size_t> exact;
        std::vector<std::size_t> partial;
        std::string valid;
        for (std::size_t i = 0; i < roster.size(); ++i)
        {
            if (!roster[i].isBoss)
                continue;
            if (!valid.empty())
                valid += ", ";
            valid += roster[i].name;
            std::string const have = Fold(roster[i].name);
            if (have == wanted)
                exact.push_back(i);
            else if (!wanted.empty() && have.find(wanted) != std::string::npos)
                partial.push_back(i);
        }

        std::vector<std::size_t> const& hits = exact.empty() ? partial : exact;
        if (hits.empty())
        {
            if (err)
                *err = "unknown boss '" + query + "' in from= (valid: " + valid + ")";
            return false;
        }
        if (hits.size() > 1)
        {
            if (err)
            {
                *err = "'" + query + "' matches several bosses in from= (";
                for (std::size_t k = 0; k < hits.size(); ++k)
                    *err += (k ? ", " : "") + roster[hits[k]].name;
                *err += ")";
            }
            return false;
        }

        std::size_t const target = hits.front();

        // Walk back over the objectives that sit directly in front of the boss:
        // they gate it, so the run begins at the first of them.
        std::size_t head = target;
        while (head > 0 && !roster[head - 1].isBoss)
            --head;

        StartPlan plan;
        plan.bossName = roster[target].name;
        for (std::size_t i = 0; i < head; ++i)
            plan.skipEntries.push_back(roster[i].entry);

        if (head == 0)
            plan.atEntrance = true;
        else if (roster[head].hasStart)
        {
            plan.x = roster[head].x;
            plan.y = roster[head].y;
            plan.z = roster[head].z;
        }
        else
        {
            if (err)
                *err = "from=" + roster[target].name + " cannot start there: no start spot is "
                       "known for '" + roster[head].name + "' (it has no registered route)";
            return false;
        }

        *out = std::move(plan);
        return true;
    }
}
