/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"

// Blackfathom Deeps (map 48). Live, on the dev realm: 9 guild runs, 0 clears.
// Five sat out the 7200 s ceiling at 1 to 4 of 7 bosses. The derived order sent
// the tank to Old Serra'kis (bit 5) straight after Lorgus Jett, behind the
// Portal of Aku'Mai, and nothing lit the four Fires of Aku'mai that open it.

namespace
{
    constexpr uint32 MAP = 48;

    DungeonBossInfo Boss(uint32 entry, uint32 bit, char const* name)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = bit;
        b.name = name;
        b.mapId = MAP;
        return b;
    }

    // What BossSpawnIndex derives for map 48: one row per spawned
    // kill-credit encounter, in DungeonEncounter bit order.
    std::vector<DungeonBossInfo> DerivedList()
    {
        return {
            Boss(4887, 0, "Ghamoo-ra"),
            Boss(4831, 1, "Lady Sarevess"),
            Boss(6243, 2, "Gelihast"),
            Boss(12902, 3, "Lorgus Jett"),
            Boss(4830, 5, "Old Serra'kis"),
            Boss(4832, 6, "Twilight Lord Kelris"),
            Boss(4829, 7, "Aku'mai"),
        };
    }

    std::vector<DungeonBossInfo> Applied()
    {
        return BossRosterRegistry::Apply(
            MAP, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), DerivedList());
    }
}

TEST(DungeonEventBlackfathomDeeps, MapHasAPatch)
{
    EXPECT_TRUE(BossRosterRegistry::HasPatch(MAP));
    EXPECT_TRUE(DungeonEventRegistry::HasEvents(MAP));
}

// Kelris stands among the fires; the fires open the portal; Old Serra'kis and
// Aku'mai are behind it. Aku'mai is the finder's finish, so he goes first.
TEST(DungeonEventBlackfathomDeeps, FiresComeAfterKelrisAndBeforeTheBossesBehindThePortal)
{
    std::vector<DungeonBossInfo> const out = Applied();
    ASSERT_EQ(out.size(), 8u);

    EXPECT_EQ(out[0].entry, 4887u);   // Ghamoo-ra
    EXPECT_EQ(out[1].entry, 4831u);   // Lady Sarevess
    EXPECT_EQ(out[2].entry, 6243u);   // Gelihast
    EXPECT_EQ(out[3].entry, 12902u);  // Lorgus Jett
    EXPECT_EQ(out[4].entry, 4832u);   // Twilight Lord Kelris
    EXPECT_EQ(out[5].kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(out[5].eventId, 1u);
    EXPECT_EQ(out[6].entry, 4829u);   // Aku'mai
    EXPECT_EQ(out[7].entry, 4830u);   // Old Serra'kis
}

// Only the ORDER moves: completion still reads each boss's own DBC bit.
TEST(DungeonEventBlackfathomDeeps, ReorderedBossesKeepTheirKillBits)
{
    std::vector<DungeonBossInfo> const out = Applied();
    for (DungeonBossInfo const& b : out)
    {
        if (b.entry == 4829)
            EXPECT_EQ(b.encounterIndex, 7u);
        if (b.entry == 4830)
            EXPECT_EQ(b.encounterIndex, 5u);
        if (b.entry == 4832)
            EXPECT_EQ(b.encounterIndex, 6u);
    }
}

// The objective sits on the fire platform, where the event's ClearRadius is
// judged from (the executor only certifies a clear from within 12yd).
TEST(DungeonEventBlackfathomDeeps, ObjectiveSitsOnTheFirePlatform)
{
    for (DungeonBossInfo const& b : Applied())
    {
        if (b.kind != DungeonAnchorKind::Objective)
            continue;
        EXPECT_NEAR(b.x, -818.7f, 2.0f);
        EXPECT_NEAR(b.y, -164.5f, 2.0f);
        EXPECT_NEAR(b.z, -25.8f, 2.0f);
        return;
    }
    FAIL() << "no Fires of Aku'mai objective on map 48";
}

// Light a fire, clear ITS wave, light the next: the fires' SmartAI summons a
// wave on every Use(), and the portal opens on the last summon's death after
// the fourth fire. Then wait for the portal.
TEST(DungeonEventBlackfathomDeeps, FiresEventLightsEachFireThenClearsItsWave)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, 1);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Anchored);
    EXPECT_TRUE(ev->persistent);
    EXPECT_TRUE(ev->required);

    struct Pair { uint32 fire; uint32 wave; };
    Pair const expected[] = {
        { 21118, 4825 },  // Aku'mai Snapjaw
        { 21119, 4977 },  // Murkshallow Softshell
        { 21120, 4823 },  // Barbed Crustacean
        { 21121, 4978 },  // Aku'mai Servant
    };

    ASSERT_EQ(ev->steps.size(), 9u);
    for (size_t i = 0; i < 4; ++i)
    {
        EventStep const& use = ev->steps[i * 2];
        EventStep const& clear = ev->steps[i * 2 + 1];
        EXPECT_EQ(use.kind, EventStepKind::UseGameObject) << "fire " << i;
        EXPECT_EQ(use.goEntry, expected[i].fire) << "fire " << i;
        // The fires answer the plain Use() (gossip-hello filter 1).
        EXPECT_FALSE(use.reportUse) << "fire " << i;

        EXPECT_EQ(clear.kind, EventStepKind::ClearRadius) << "wave " << i;
        ASSERT_EQ(clear.entryFilter.size(), 1u) << "wave " << i;
        EXPECT_EQ(clear.entryFilter[0], expected[i].wave) << "wave " << i;
    }

    EventStep const& door = ev->steps[8];
    EXPECT_EQ(door.kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(door.goEntry, 21117u);  // Portal of Aku'Mai
    EXPECT_EQ(door.wantState, 0u);    // GO_STATE_ACTIVE
}

// The wave volume must hold every summon point and nothing static: the summon
// points are 44 to 45yd out at platform height, static Snapjaws behind the shut
// portal are 56yd out, and a static Barbed Crustacean sits 11yd below.
TEST(DungeonEventBlackfathomDeeps, WaveVolumeHoldsTheSummonsAndNoStaticSpawn)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, 1);
    ASSERT_NE(ev, nullptr);
    for (EventStep const& s : ev->steps)
    {
        if (s.kind != EventStepKind::ClearRadius)
            continue;
        EXPECT_GE(s.radius, 45.0f);
        EXPECT_LT(s.radius, 56.0f);
        EXPECT_LT(s.zBand, 11.0f);
        EXPECT_GE(s.zBand, 2.0f);
    }
}

// The portal opens by script only. A bot click would skip the fires.
TEST(DungeonEventBlackfathomDeeps, PortalOfAkumaiIsScriptOnly)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsScriptOnly(21117));
}
