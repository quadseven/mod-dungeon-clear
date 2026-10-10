/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h"

// Invariants of the settings table itself. The resolution CHAIN
// (override -> heroic conf -> heroic default -> conf -> default) needs a live
// sConfigMgr and a Player on a heroic map, so it is exercised at runtime rather
// than here; what these pin is the data the chain reads, which is where an
// authoring mistake would actually land.

namespace
{
    std::vector<DcSettingDef const*> HeroicRows()
    {
        std::vector<DcSettingDef const*> out;
        for (DcSettingDef const& d : kDcSettings)
            if (DcHasHeroicDefault(d))
                out.push_back(&d);
        return out;
    }

    std::vector<DcSettingDef const*> RaidRows()
    {
        std::vector<DcSettingDef const*> out;
        for (DcSettingDef const& d : kDcSettings)
            if (DcHasRaidDefault(d))
                out.push_back(&d);
        return out;
    }
}

TEST(DcSettingsRegistryTest, SentinelMeansNoHeroicLayer)
{
    DcSettingDef plain{"X", DcType::Float, 1, 0, 10, true};
    EXPECT_FALSE(DcHasHeroicDefault(plain));
    EXPECT_TRUE(std::isnan(plain.heroicVal));

    DcSettingDef heroic{"Y", DcType::Float, 1, 0, 10, true, 4};
    EXPECT_TRUE(DcHasHeroicDefault(heroic));
    EXPECT_EQ(heroic.heroicVal, 4);
}

TEST(DcSettingsRegistryTest, HeroicDefaultsSitInsideTheRowsOwnRange)
{
    // A heroic default outside [minVal, maxVal] would be a value the addon could
    // never reproduce as an override, and the two layers would disagree about
    // what is legal for the same key.
    for (DcSettingDef const* d : HeroicRows())
    {
        EXPECT_GE(d->heroicVal, d->minVal) << d->key;
        EXPECT_LE(d->heroicVal, d->maxVal) << d->key;
    }
}

TEST(DcSettingsRegistryTest, HeroicDefaultsAreWholeNumbersForDiscreteTypes)
{
    // Bool/UInt/Int rows round on read; an authored 2.5 would silently become 2
    // (or 3) and the table would not say what the server actually uses.
    for (DcSettingDef const* d : HeroicRows())
    {
        if (d->type == DcType::Float)
            continue;
        EXPECT_EQ(d->heroicVal, std::round(d->heroicVal)) << d->key;
        if (d->type == DcType::Bool)
            EXPECT_TRUE(d->heroicVal == 0 || d->heroicVal == 1) << d->key;
    }
}

TEST(DcSettingsRegistryTest, HeroicDefaultsActuallyDifferFromNormal)
{
    // A heroic value equal to the normal default is dead weight that reads, to
    // anyone scanning the table, as a deliberate difficulty split that isn't one.
    for (DcSettingDef const* d : HeroicRows())
        EXPECT_NE(d->heroicVal, d->defVal) << d->key;
}

TEST(DcSettingsRegistryTest, ServerOnlyRowsCarryNoHeroicDefault)
{
    // Server-only rows govern the harness, the path workers and the spectator
    // camera — none of which is a property of the dungeon's difficulty. Keeping
    // them out means the difficulty lookup is never reached from the worker
    // threads that read them (see the layer tests in DcSettings::GetRaw).
    for (DcSettingDef const* d : HeroicRows())
        EXPECT_TRUE(d->playerFacing) << d->key;
}

// --- Raid layer (same contracts as the heroic layer) ----------------------

TEST(DcSettingsRegistryTest, SentinelMeansNoRaidLayer)
{
    DcSettingDef plain{"X", DcType::Float, 1, 0, 10, true};
    EXPECT_FALSE(DcHasRaidDefault(plain));
    EXPECT_TRUE(std::isnan(plain.raidVal));

    DcSettingDef raided{"Y", DcType::Float, 1, 0, 10, true, kDcNoHeroic, 4};
    EXPECT_TRUE(DcHasRaidDefault(raided));
    EXPECT_FALSE(DcHasHeroicDefault(raided));
    EXPECT_EQ(raided.raidVal, 4);
}

TEST(DcSettingsRegistryTest, RaidDefaultsSitInsideTheRowsOwnRange)
{
    for (DcSettingDef const* d : RaidRows())
    {
        EXPECT_GE(d->raidVal, d->minVal) << d->key;
        EXPECT_LE(d->raidVal, d->maxVal) << d->key;
    }
}

TEST(DcSettingsRegistryTest, RaidDefaultsAreWholeNumbersForDiscreteTypes)
{
    for (DcSettingDef const* d : RaidRows())
    {
        if (d->type == DcType::Float)
            continue;
        EXPECT_EQ(d->raidVal, std::round(d->raidVal)) << d->key;
        if (d->type == DcType::Bool)
            EXPECT_TRUE(d->raidVal == 0 || d->raidVal == 1) << d->key;
    }
}

TEST(DcSettingsRegistryTest, RaidDefaultsActuallyDifferFromNormal)
{
    for (DcSettingDef const* d : RaidRows())
        EXPECT_NE(d->raidVal, d->defVal) << d->key;
}

TEST(DcSettingsRegistryTest, ServerOnlyRowsCarryNoRaidDefault)
{
    for (DcSettingDef const* d : RaidRows())
        EXPECT_TRUE(d->playerFacing) << d->key;
}

TEST(DcSettingsRegistryTest, RaidProfileIsExactlyTheScaleSet)
{
    // The raid layer is only the numbers that must scale with 10-40 members.
    // Pinned like the heroic set: adding a raid default to an unrelated key has
    // to fail here and be justified. Keep in step with the RAID DEFAULTS block
    // in mod_dungeon_clear.conf.dist. (Plan B/C keys — quorum, muster budgets —
    // join this list when they land with authored raid values.)
    std::vector<std::string> const expected{
        "PartyMaxSpread",
        "PostCombatRezTimeoutSecs",
    };

    std::vector<std::string> actual;
    for (DcSettingDef const* d : RaidRows())
        actual.emplace_back(d->key);

    std::vector<std::string> sortedExpected = expected;
    std::vector<std::string> sortedActual = actual;
    std::sort(sortedExpected.begin(), sortedExpected.end());
    std::sort(sortedActual.begin(), sortedActual.end());
    EXPECT_EQ(sortedActual, sortedExpected);
}

TEST(DcSettingsRegistryTest, HeroicProfileIsExactlyThePullSafetySet)
{
    // The heroic layer is deliberately narrow: the pull safety profile, nothing
    // else. Pinning the membership means a heroic default cannot be added to an
    // unrelated key without this failing and making someone justify it — and it
    // doubles as the readable list of what heroic actually changes. Keep in step
    // with the HEROIC DEFAULTS block in mod_dungeon_clear.conf.dist.
    //
    // Smart Rest was in this set and was removed: forcing it on in heroics made
    // runs crawl (see the SmartRest block in DcSettingsRegistry.h). It is opt-in
    // on both difficulties now.
    std::vector<std::string> const expected{
        "PullSetback",
        "PullCampSafeRadius",
        "PullMaxDrag",
        "PullRangedMaxDrag",
        "PullPlayerReleaseDelay",
        "PullThreatLeadPanicHp",
        "PullSafetyHpPct",
        "PullSafetyGrace",
        "PullPetReleaseDelay",
        "PullCommitRangeFloor",
        "PullDynamicMaxLeeroyMobs",
        "PullCombatSpread",
        "PullDynamicPartyLag",
        "PullPatrolWaitSec",
        "PullOversizeHold",
        "ClearBossNeighbours",
        "PullEnRouteAvoid",
        "AdvanceWindowYards",
        "TrashWidthCap",
    };

    std::vector<std::string> actual;
    for (DcSettingDef const* d : HeroicRows())
        actual.emplace_back(d->key);

    // Order follows the table, so compare as sets.
    std::vector<std::string> sortedExpected = expected;
    std::vector<std::string> sortedActual = actual;
    std::sort(sortedExpected.begin(), sortedExpected.end());
    std::sort(sortedActual.begin(), sortedActual.end());
    EXPECT_EQ(sortedActual, sortedExpected);
}

TEST(DcSettingsRegistryTest, ForceAdvancedIsOffOnBothDifficulties)
{
    // The A/B lever must never ship on: it costs the full pull FSM on every
    // single-mob pack. An operator opts in per difficulty with a conf line.
    DcSettingDef const* d = FindDcSetting("PullForceAdvanced");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->defVal, 0);
    EXPECT_FALSE(DcHasHeroicDefault(*d));
}

TEST(DcSettingsRegistryTest, KeysAreUniqueAndLookupFindsThem)
{
    for (DcSettingDef const& d : kDcSettings)
    {
        DcSettingDef const* found = FindDcSetting(d.key);
        ASSERT_NE(found, nullptr) << d.key;
        // Unique keys: the linear lookup returns the FIRST match, so a duplicate
        // would silently shadow whatever came after it.
        EXPECT_EQ(found, &d) << d.key;
    }
    EXPECT_EQ(FindDcSetting("NotARealSetting"), nullptr);
}

TEST(DcSettingsRegistryTest, TrashBandClampedToHeroicCap)
{
    // C.2: the heroic band cap. With the unified reach a common heroic elite's
    // band lands ~32-36yd, which the normal 30 cap silently clips — discarding
    // exactly the reach the unification added. Normal keeps 30.
    DcSettingDef const* d = FindDcSetting("TrashWidthCap");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->defVal, 30);
    ASSERT_TRUE(DcHasHeroicDefault(*d));
    EXPECT_EQ(d->heroicVal, 42);
}

// The shipped conf, not just the table. The worldserver copies
// mod_dungeon_clear.conf.dist to mod_dungeon_clear.conf on every start, so a
// value written in the .dist is the value the realm runs, and it outranks the
// registry default above. These pin the conf lines an operator decision set,
// read from the source tree (DC_FIXTURE_DIR is <module>/t/fixtures).
#ifdef DC_FIXTURE_DIR
namespace
{
    std::string ShippedConfPath()
    {
        return std::string(DC_FIXTURE_DIR) + "/../../conf/mod_dungeon_clear.conf.dist";
    }

    // "DungeonClear.Key = value" lines of the shipped conf, keyed by full name.
    // Comments and blank lines are skipped; the value is trimmed. An unopenable
    // file yields an empty map; each test asserts on that with the path, so a
    // moved file fails as "cannot open <path>", not as a missing key.
    std::map<std::string, std::string> ShippedConf()
    {
        std::map<std::string, std::string> out;
        std::ifstream in(ShippedConfPath());
        std::string line;
        auto const trim = [](std::string s)
        {
            std::size_t const b = s.find_first_not_of(" \t\r");
            std::size_t const e = s.find_last_not_of(" \t\r");
            return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
        };
        while (std::getline(in, line))
        {
            std::string const t = trim(line);
            if (t.empty() || t[0] == '#')
                continue;
            std::size_t const eq = t.find('=');
            if (eq == std::string::npos)
                continue;
            out[trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
        }
        return out;
    }
}

TEST(DcSettingsRegistryTest, ShippedConfIsReadable)
{
    // Guards the two tests below: an unreadable file would make every lookup
    // miss and the assertions fail for the wrong reason.
    std::ifstream in(ShippedConfPath());
    ASSERT_TRUE(in.is_open()) << "cannot open " << ShippedConfPath();
    EXPECT_GT(ShippedConf().size(), 50u) << ShippedConfPath();
}

TEST(DcSettingsRegistryTest, StrandedRecoveryNeverTeleportsByDefault)
{
    // Operator decision 2026-10-10: a stuck member is not teleported to the
    // tank. A teleport is not something a human party can do. Off in the
    // shipped conf AND in the compiled-in default, so a missing conf line
    // cannot turn it back on.
    std::map<std::string, std::string> const conf = ShippedConf();
    ASSERT_FALSE(conf.empty()) << "cannot open " << ShippedConfPath();
    auto const it = conf.find("DungeonClear.StrandedRecovery");
    ASSERT_NE(it, conf.end());
    EXPECT_EQ(it->second, "0");

    DcSettingDef const* d = FindDcSetting("StrandedRecovery");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->defVal, 0);
    EXPECT_FALSE(DcHasHeroicDefault(*d));
    EXPECT_FALSE(DcHasRaidDefault(*d));
}

TEST(DcSettingsRegistryTest, RoomClearGivesACarefulGroupTwoMinutes)
{
    // Operator decision 2026-10-10: a careful group keeps clearing trash before
    // it pulls the boss. 30s gave up while packs were still dying (run 507,
    // Gilnid, 8 left) and pulled the boss with the room up. Conf and registry
    // agree so the conf's "Default:" line is the truth.
    std::map<std::string, std::string> const conf = ShippedConf();
    ASSERT_FALSE(conf.empty()) << "cannot open " << ShippedConfPath();
    auto const it = conf.find("DungeonClear.RoomClearTimeout");
    ASSERT_NE(it, conf.end());
    EXPECT_EQ(it->second, "120");

    DcSettingDef const* d = FindDcSetting("RoomClearTimeout");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->defVal, 120);
    EXPECT_GE(d->defVal, d->minVal);
    EXPECT_LE(d->defVal, d->maxVal);
}
#endif
