/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "DcSourceScan.h"

// The grant ledger (#25): every call in src/ that hands a player something it
// could not earn by playing, and why it is still there.
//
// A grant is an item made from nothing, a spell, skill, level, XP, quest credit
// or money set, an aura or immunity put on (or a debuff taken off) a member, a
// full restore, a resurrection that is not a class spell or a spirit healer, a
// teleport that is not the player's own means, a forced run speed, a triggered
// cast that skips cast time, cost or reagents, and the playerbots factory
// calls that roll a level and gear onto a character.
//
// The ledger pins each remaining grant by file, API and count. A new grant
// anywhere in src/ fails DcGrantLedger.NoGrantOutsideTheLedger; a grant that is
// removed without its ledger row going too fails
// DcGrantLedger.EveryLedgerRowStillMatches, so the ledger never claims more
// than the code does. Each row names the follow-up that removes it (the letter
// is the follow-up ticket body in the PR that added this file).

namespace
{
    using namespace DcSourceScan;

    // Called by name. Each is a grant wherever it appears in this module.
    char const* const kGrantApis[] = {
        // items and loot
        "AddItem", "StoreNewItem", "EquipNewItem", "FillLoot",
        // spells, skills, levels, XP, quest credit, money
        "learnSpell", "LearnSpell", "addSpell", "SetSkill", "GiveLevel", "GiveXP", "SetLevel",
        "SetRewardedQuest", "RewardQuest", "CompleteQuest", "ModifyMoney", "SetMoney",
        // auras, immunities and debuffs taken off
        "AddAura", "ApplySpellImmune", "RemoveAurasDueToSpell",
        // restores and resurrections
        "SetFullHealth", "SetFullPower", "SetHealth", "SetPower", "ResurrectPlayer",
        // teleports and speed
        "TeleportTo", "NearTeleportTo", "SetSpeed",
        // the playerbots factory: level, talents, gear, glyphs, enchants, pet, ammo
        "Randomize", "InitEquipment", "InitTalentsBySpecNo", "InitGlyphs", "InitPet", "InitAmmo",
        "ApplyEnchantAndGemsNew",
    };

    // A CastSpell whose last argument asks for a triggered cast (true, a
    // `triggered` flag, or any TRIGGERED_ flag but TRIGGERED_NONE). Counted
    // under this pseudo API name.
    constexpr char const* kTriggeredCast = "CastSpell(triggered)";

    bool IsTriggeredFlag(std::string const& arg)
    {
        if (arg == "true" || arg == "triggered")
            return true;
        return arg.find("TRIGGERED_") != std::string::npos && arg != "TRIGGERED_NONE";
    }

    struct Row
    {
        char const* file;      // relative to src/
        char const* api;
        std::size_t count;
        char const* followUp;  // the follow-up ticket body that removes it
    };

    // clang-format off
    Row const kLedger[] = {
        // A. party relocations travel instead of teleporting
        {"Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.cpp",  "NearTeleportTo",        3, "A"},
        {"Ai/Dungeon/DungeonClear/Util/DcStrandedRecovery.cpp",    "NearTeleportTo",        1, "A"},
        {"Ai/Dungeon/DungeonClear/Action/DcPullActions.cpp",       "NearTeleportTo",        1, "A"},
        // B. a raid wipe in a test run releases and runs back
        {"Ai/Dungeon/DungeonClear/Util/DcRezRecovery.cpp",         "RemoveAurasDueToSpell", 1, "B"},
        {"Ai/Dungeon/DungeonClear/Util/DcRezRecovery.cpp",         "ResurrectPlayer",       1, "B"},
        {"Ai/Dungeon/DungeonClear/Util/DcRezRecovery.cpp",         "TeleportTo",            1, "B"},
        // C. the dungeon queue fill picks bots that are already eligible
        {"DungeonQueueFill/DcDungeonQueueFillJob.cpp",             "RemoveAurasDueToSpell", 4, "C"},
        {"DungeonQueueFill/DcDungeonQueueFillJob.cpp",             "ResurrectPlayer",       1, "C"},
        // ...the pool-bot steps the merge of upstream master lifted out of the
        // job into Util/DcPoolBots (travel to the capital, sanitise, the death
        // knight chain credit), plus upstream's Death Gate for a pool death
        // knight still on Acherus (DcDungeonAccess::UnlockTravel).
        {"Util/DcPoolBots.cpp",                                    "ResurrectPlayer",       2, "C"},
        {"Util/DcPoolBots.cpp",                                    "SetRewardedQuest",      1, "C"},
        {"Util/DcPoolBots.cpp",                                    "TeleportTo",            1, "C"},
        {"Util/DcDungeonAccess.cpp",                               "learnSpell",            1, "C"},
        {"Util/DcBotProvisioning.cpp",                             "Randomize",             1, "C"},
        {"Util/DcBotProvisioning.cpp",                             "InitTalentsBySpecNo",   1, "C"},
        {"Util/DcBotProvisioning.cpp",                             "InitEquipment",         1, "C"},
        {"Util/DcBotProvisioning.cpp",                             "InitGlyphs",            1, "C"},
        {"Util/DcBotProvisioning.cpp",                             "ApplyEnchantAndGemsNew",1, "C"},
        {"Util/DcBotProvisioning.cpp",                             "InitPet",               1, "C"},
        {"Util/DcBotProvisioning.cpp",                             "InitAmmo",              1, "C"},
        // D. scripted casts pay their cast time
        {"Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.cpp", kTriggeredCast,     1, "D"},
        {"Ai/Dungeon/DungeonClear/Overrides/BlackwingLairDriver.cpp",   kTriggeredCast,     1, "D"},
        {"Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.cpp",       kTriggeredCast,     1, "D"},
        // E. the pulling tank can be dazed
        {"Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.cpp",        "ApplySpellImmune",      2, "E"},
        {"Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.cpp",        "RemoveAurasDueToSpell", 1, "E"},
        {"Ai/Dungeon/DungeonClear/Action/DcPullActions.cpp",       "RemoveAurasDueToSpell", 1, "E"},
        // F. an escort party mounts instead of a forced run speed
        {"Ai/Dungeon/DungeonClear/Action/DcEngageActions.cpp",     "SetSpeed",              1, "F"},
        // H. the GM test harness and the GM observer
        {"TestRun/DcTestRunJob.cpp",                               "TeleportTo",            4, "H"},
        {"TestRun/DcTestRunJob.cpp",                               "ResurrectPlayer",       1, "H"},
        {"TestRun/DcTestRunJob.cpp",                               "RemoveAurasDueToSpell", 1, "H"},
        {"DungeonClearCommand.cpp",                                "TeleportTo",            2, "H"},
        {"Util/DcSpectator.cpp",                                   "SetSpeed",              2, "H"},
        // ...the harness's attunement grant before it teleports a party in
        // (DcDungeonAccess::GrantEntry; the dungeon queue fill does not call it
        // on this branch), and the Pull Lab (`.dc lab`, src/Lab), GM-only.
        {"Util/DcDungeonAccess.cpp",                               "AddItem",               1, "H"},
        {"Util/DcDungeonAccess.cpp",                               "SetRewardedQuest",      2, "H"},
        {"Lab/DcLabInject.cpp",                                    "AddAura",               1, "H"},
        {"Lab/DcLabJob.cpp",                                       "NearTeleportTo",        2, "H"},
        {"Lab/DcLabJob.cpp",                                       "RemoveAurasDueToSpell", 1, "H"},
        {"Lab/DcLabJob.cpp",                                       "ResurrectPlayer",       1, "H"},
        {"Lab/DcLabJob.cpp",                                       "SetFullHealth",         3, "H"},
        {"Lab/DcLabJob.cpp",                                       "SetPower",              2, "H"},
        {"Lab/DcLabPuppet.cpp",                                    kTriggeredCast,          1, "H"},
        // U. carried in from upstream master by the merge that made quadseven/prod
        // (2026-10-22). Content production has not run yet: review each before a
        // realm plays it. The BRD row is the Rocknot ale turn-in, a real quest
        // reward after CanRewardQuest, with ale the bot bought itself.
        {"Ai/Dungeon/DungeonClear/Action/DcOculusRiderAction.cpp",  "AddItem",              1, "U"},
        {"Ai/Dungeon/DungeonClear/Action/DcOculusRiderAction.cpp",  "NearTeleportTo",       1, "U"},
        {"Ai/Dungeon/DungeonClear/Data/Events/BlackrockDepthsEvents.cpp", "RewardQuest",    1, "U"},
        {"Ai/Dungeon/DungeonClear/Overrides/KarazhanChessDriver.cpp", kTriggeredCast,       1, "U"},
        {"Ai/Dungeon/DungeonClear/Overrides/KarazhanChessDriver.cpp", "NearTeleportTo",     1, "U"},
        {"Ai/Dungeon/DungeonClear/Overrides/KarazhanChessDriver.cpp", "RemoveAurasDueToSpell", 2, "U"},
        {"Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.cpp",  "AddItem",               1, "U"},
        {"BgQueueFill/DcBgQueueFillJob.cpp",                       "RemoveAurasDueToSpell", 1, "U"},
        {"BgQueueFill/DcBgQueueFillJob.cpp",                       "ResurrectPlayer",       1, "U"},
        {"BgQueueFill/DcBgQueueFillManager.cpp",                   "RemoveAurasDueToSpell", 1, "U"},
    };
    // clang-format on

    using Key = std::pair<std::string, std::string>;  // (file, api)

    struct Scan
    {
        std::map<Key, std::size_t> counts;
        std::map<Key, std::vector<std::string>> where;  // "file:line" per hit
        std::size_t files = 0;
    };

    void Hit(Scan& s, std::string const& rel, std::string const& api, std::string const& code,
             std::size_t at)
    {
        Key const k{rel, api};
        ++s.counts[k];
        s.where[k].push_back(rel + ":" + std::to_string(LineOf(code, at)));
    }

    Scan ScanModule()
    {
        Scan s;
        std::filesystem::path const src = ModuleSrc();
        for (auto const& e : std::filesystem::recursive_directory_iterator(src))
        {
            if (!e.is_regular_file() || !IsSource(e.path()))
                continue;
            ++s.files;
            std::string const rel = std::filesystem::relative(e.path(), src).generic_string();
            std::string const code = StripComments(ReadFile(e.path()));
            for (char const* api : kGrantApis)
                for (std::size_t at : CallOffsets(code, api))
                    Hit(s, rel, api, code, at);
            for (std::size_t at : CallOffsets(code, "CastSpell"))
            {
                std::vector<std::string> const args = CallArgs(code, at);
                if (!args.empty() && IsTriggeredFlag(args.back()))
                    Hit(s, rel, kTriggeredCast, code, at);
            }
        }
        return s;
    }

    std::string Join(std::vector<std::string> const& v)
    {
        std::string out;
        for (std::string const& x : v)
            out += (out.empty() ? "" : ", ") + x;
        return out;
    }
}

// The scanner itself: whole identifiers only, calls only, comments ignored, and
// the triggered-cast reading of the last argument.
TEST(DcGrantLedger, TheScannerFindsCallsNotNames)
{
    std::string const code = StripComments(
        "a->NearTeleportTo(1);\n"
        "b->TeleportTo (2);\n"
        "// c->TeleportTo(3);\n"
        "bool TeleportToFar = true;\n"
        "x->CastSpell(t, 1, true);\n"
        "y->CastSpell(t, s, nullptr, TRIGGERED_CAST_DIRECTLY);\n"
        "z->CastSpell(t, 1, false);\n"
        "w->CastSpell(f(1, 2), TRIGGERED_NONE);\n"
        "ai->CastSpell(spell, target);\n");
    EXPECT_EQ(CallOffsets(code, "TeleportTo").size(), 1u);
    EXPECT_EQ(CallOffsets(code, "NearTeleportTo").size(), 1u);

    std::size_t triggered = 0;
    for (std::size_t at : CallOffsets(code, "CastSpell"))
    {
        std::vector<std::string> const args = CallArgs(code, at);
        ASSERT_FALSE(args.empty());
        if (IsTriggeredFlag(args.back()))
            ++triggered;
    }
    EXPECT_EQ(triggered, 2u);
    EXPECT_EQ(CallArgs(code, code.find("w->CastSpell")).size(), 2u);
}

// No grant anywhere in src/ that the ledger does not account for.
TEST(DcGrantLedger, NoGrantOutsideTheLedger)
{
    ASSERT_TRUE(std::filesystem::exists(ModuleSrc())) << "cannot find " << ModuleSrc();
    Scan const s = ScanModule();
    EXPECT_GT(s.files, 100u) << "the scan is not looking at the module";

    std::map<Key, Row const*> ledger;
    for (Row const& r : kLedger)
        ledger[{r.file, r.api}] = &r;

    for (auto const& [k, n] : s.counts)
    {
        auto const it = ledger.find(k);
        if (it == ledger.end())
            ADD_FAILURE() << k.second << " at " << Join(s.where.at(k))
                          << " is a grant a player could not earn: replace it with the natural "
                             "action, or add a ledger row naming the follow-up that will";
        else if (it->second->count != n)
            ADD_FAILURE() << k.first << " calls " << k.second << " " << n << " time(s), the "
                          << "ledger says " << it->second->count << " (follow-up "
                          << it->second->followUp << "): " << Join(s.where.at(k));
    }
}

// And no ledger row for a grant that is gone.
TEST(DcGrantLedger, EveryLedgerRowStillMatches)
{
    Scan const s = ScanModule();
    for (Row const& r : kLedger)
        if (!s.counts.count({r.file, r.api}))
            ADD_FAILURE() << r.file << " no longer calls " << r.api
                          << ": drop its ledger row (follow-up " << r.followUp << ")";
}

// #25: the Hakkari Blood comes out of the loot the core rolled on the
// Bloodkeeper's corpse. The action used to roll that loot itself when the corpse
// had none, which hands the party a drop no player at that corpse could take.
TEST(DcNoGrant, HakkariBloodIsTakenOnlyFromLootTheCoreRolled)
{
    std::filesystem::path const hakkar =
        ModuleSrc() / "Ai" / "Dungeon" / "DungeonClear" / "Action" / "DcHakkar.cpp";
    ASSERT_TRUE(std::filesystem::exists(hakkar)) << "cannot find " << hakkar;
    std::string const code = StripComments(ReadFile(hakkar));
    EXPECT_NE(code.find("StoreLootItem"), std::string::npos)
        << "the blood is no longer looted at all; this check is looking at the wrong code";
    for (std::size_t at : CallOffsets(code, "FillLoot"))
        ADD_FAILURE() << "DcHakkar.cpp:" << LineOf(code, at)
                      << " rolls the Bloodkeeper's loot itself";
}

// #25: the queue fill hands the dungeon finder a bot at whatever health it has.
// It used to top every bot up to full, twice: once before the first queue and
// again on every re-queue.
TEST(DcNoGrant, TheQueueFillNeverRestoresABot)
{
    std::filesystem::path const job =
        ModuleSrc() / "DungeonQueueFill" / "DcDungeonQueueFillJob.cpp";
    ASSERT_TRUE(std::filesystem::exists(job)) << "cannot find " << job;
    std::string const code = StripComments(ReadFile(job));
    EXPECT_NE(code.find("TickSanitizing"), std::string::npos)
        << "the sanitising stage moved; this check is looking at the wrong code";
    for (char const* api : {"SetFullHealth", "SetFullPower", "SetHealth", "SetPower"})
        for (std::size_t at : CallOffsets(code, api))
            ADD_FAILURE() << "DcDungeonQueueFillJob.cpp:" << LineOf(code, at) << " calls " << api;
}
