/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Blackfathom Deeps (map 48) - the FIRES OF AKU'MAI, ANCHORED + PERSISTENT ---
//
// The door to Aku'mai's lair (Portal of Aku'Mai, GO 21117, lock 85 = no lock)
// is opened by instance_blackfathom_deeps and nothing else. Its OnUnitDeath
// opens the portal when BOTH hold:
//
//   * TYPE_FIRE1..TYPE_FIRE4 are all DONE. Each Fire of Aku'mai (GO 21118 to
//     21121, GAMEOBJECT_TYPE_BUTTON, SmartGameObjectAI) sets its own TYPE_FIREn
//     to DONE on SMART_EVENT_GOSSIP_HELLO with filter 1, the plain Use() path
//     (not report-use), and in the same breath summons its creature_summon_groups
//     group 1: four Aku'mai Snapjaws (4825), four Murkshallow Softshells (4977),
//     four Barbed Crustaceans (4823) or two Aku'mai Servants (4978), at the
//     platform's east and west edges (x -862 and -775, z -25.9). Every one of
//     them "Is Summoned - Set In Combat With Zone", so the wave comes for the
//     party at once.
//   * The summon counter is back to zero. OnCreatureCreate counts every SUMMON
//     of those four entries and OnUnitDeath counts them down; the portal opens
//     on the death that brings it to zero after the fourth fire is lit.
//
// So a party opens the door the way players do: light a fire, kill its wave,
// light the next. The fires are buttons with autoCloseTime 0, so a lit fire
// stays GO_STATE_ACTIVE and UseGO's idempotence (skip a non-READY GO) holds.
// That matters: the SmartAI runs on EVERY Use(), so a second click on a lit
// fire would summon a second wave. One fire at a time also keeps the party
// facing one wave, not four.
//
// The wave gate is a ClearRadius filtered to the four wave entries, centred on
// the platform with a 48yd radius and an 8yd floor band. An entry-only
// KillCreature would wait forever: the map has STATIC Snapjaws behind the
// still-shut portal (y -215 to -253, 56yd from the centre) and a static Barbed
// Crustacean in the pool below (z -37). The radius stops short of the first and
// the band leaves out the second; every summon point is 44 to 45yd from the
// centre at platform height.
//
// Twilight Lord Kelris (4832) stands among the fires, so the objective is
// ordered right after him. Old Serra'kis (4830, bit 5) swims in the pool PAST
// the portal; the encounter-index order sent the tank to him straight after
// Lorgus Jett, at a door nothing would open, which is where the live runs sat
// out their two hours. Aku'mai (4829, bit 7) is the finder's finish
// (instance_encounters.lastEncounterDungeon), so he is ordered before
// Old Serra'kis: the run is over at Aku'mai, and Old Serra'kis is a swimming
// boss in deep water, where the party would otherwise fight underwater.

namespace
{
    constexpr uint32 BFD_MAP = 48;

    constexpr uint32 BFD_FIRE_1 = 21118;
    constexpr uint32 BFD_FIRE_2 = 21119;
    constexpr uint32 BFD_FIRE_3 = 21120;
    constexpr uint32 BFD_FIRE_4 = 21121;
    constexpr uint32 BFD_PORTAL_OF_AKUMAI = 21117;

    constexpr uint32 BFD_AKUMAI_SNAPJAW = 4825;
    constexpr uint32 BFD_MURKSHALLOW_SOFTSHELL = 4977;
    constexpr uint32 BFD_BARBED_CRUSTACEAN = 4823;
    constexpr uint32 BFD_AKUMAI_SERVANT = 4978;

    constexpr uint32 BFD_SERRAKIS = 4830;
    constexpr uint32 BFD_AKUMAI = 4829;

    // Platform centre: the mean of the four fires' x/y, at Kelris's z.
    constexpr float BFD_FIRES_X = -818.72f;
    constexpr float BFD_FIRES_Y = -164.48f;
    constexpr float BFD_FIRES_Z = -25.79f;
    constexpr float BFD_WAVE_RADIUS = 48.0f;
    constexpr float BFD_WAVE_ZBAND = 8.0f;
    // A fire is 5 to 7yd from the centre; 30yd finds it from anywhere the
    // objective's arrival puts the tank.
    constexpr float BFD_FIRE_SEARCH = 30.0f;
    constexpr uint32 BFD_WAVE_TIMEOUT = 180000;
    // The portal is 36yd from the centre and opens on the wave's last death.
    constexpr float BFD_PORTAL_SEARCH = 60.0f;
    constexpr uint32 BFD_PORTAL_TIMEOUT = 30000;

    // Clear one fire's wave before the next fire is lit.
    EventBuilder& LightFire(EventBuilder& b, uint32 fire, uint32 wave)
    {
        return b.UseGO(fire, BFD_FIRE_SEARCH)
            .ClearRadius(BFD_FIRES_X, BFD_FIRES_Y, BFD_FIRES_Z, BFD_WAVE_RADIUS, BFD_WAVE_ZBAND)
            .OnlyEntries({ wave })
            .Timeout(BFD_WAVE_TIMEOUT);
    }
}

void RegisterBlackfathomDeepsEvents(std::vector<DungeonEvent>& out)
{
    EventBuilder b(BFD_MAP, 1, "Light the Fires of Aku'mai");
    b.Anchored(/*orderIndex, doc-only*/ 7);
    LightFire(b, BFD_FIRE_1, BFD_AKUMAI_SNAPJAW);
    LightFire(b, BFD_FIRE_2, BFD_MURKSHALLOW_SOFTSHELL);
    LightFire(b, BFD_FIRE_3, BFD_BARBED_CRUSTACEAN);
    LightFire(b, BFD_FIRE_4, BFD_AKUMAI_SERVANT);
    // GO_STATE_ACTIVE (0): the portal is open.
    b.WaitForGOState(BFD_PORTAL_OF_AKUMAI, /*GO_STATE_ACTIVE*/ 0, BFD_PORTAL_TIMEOUT,
                     BFD_PORTAL_SEARCH);
    // Persistent: each wave is a fight, and a non-persistent anchored event
    // rewinds to step 0 after any gap in its drive. The fires' idempotence
    // would make that rewind harmless, but there is no reason to re-walk it.
    b.Persistent();
    out.push_back(b.Build());
}

void RegisterBlackfathomDeepsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // Auto-derived order (DungeonEncounter bits): Ghamoo-ra 0, Lady Sarevess 1,
    // Gelihast 2, Lorgus Jett 3, Old Serra'kis 5, Twilight Lord Kelris 6,
    // Aku'mai 7. Baron Aquanis (bit 4) has no instance_encounters row and no
    // static spawn, so he is not on the list. Kelris keeps his place (6); the
    // fires follow at 7, then Aku'mai at 8 and Old Serra'kis at 9. Both keep
    // their real kill-bits; only the order moves.
    BossRosterPatch p;
    p.mapId = BFD_MAP;
    p.reorder = {
        { BFD_AKUMAI, 8 },
        { BFD_SERRAKIS, 9 },
    };
    p.add = {
        MakeObjective(OBJ(1), /*encounterIndex*/ 6, BFD_MAP, "Fires of Aku'mai",
                      BFD_FIRES_X, BFD_FIRES_Y, BFD_FIRES_Z, /*arriveRadius*/ 10.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1,
                      /*orderOverride*/ 7),
    };
    t.push_back(std::move(p));
}
