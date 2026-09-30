/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_REST_FLOOR_DECISION_H
#define _DC_REST_FLOOR_DECISION_H

// Pure decision kernel: how much HP and mana one member must have before the
// between-pulls gate lets the party pull again, scaled by how dangerous the
// next fight is and by what the member is for.
//
// WHY THIS EXISTS. The gate held EVERY living member to one pair of floors
// (RestMinHpPct / RestMinMpPct: 85 HP and 65 mana with stock playerbots
// settings) whatever was ahead. Live in The Stockade (campaign 27, a party of
// levels 35-39 against bosses of levels 24-29, zero deaths across two clean
// runs): about half of a 34 minute run was the tank standing still on
// "advance yielding: party not ready / resting - Waiting on <mage> (low mana)", a
// level 35 mage sitting at 64% mana against the 65% floor, while the tank and
// healer were fine and the next pack was trivial. Nothing about that wait
// bought safety: a wipe is what a floor is for, and a mage at 64% mana behind
// a healthy tank is not one.
//
// WHAT A FLOOR IS FOR, PER ROLE. The healer's mana is the party's insurance, so
// it keeps the highest floor of the three and never drops under
// kHealerEasyMana (50). The tank's HP is what a pull spends first, so its HP floor
// is the last to relax. A damage dealer's mana only shortens a fight, so it is
// the first to relax and the deepest.
//
// RISK IS A FACT ABOUT THE DUNGEON, NOT A GUESS ABOUT THE PACK. The caller
// classifies it from the lowest party level against the next boss's level and
// whether that boss is about to be pulled (ClassifyRisk). Hard leaves both
// floors exactly as configured. A dungeon boss keeps the configured HP and
// healer mana floors, while tank and damage mana floors reflect the resources
// those roles can safely spend.
//
// Engine-free so it is unit-testable in isolation (t/TestRestFloor), mirroring
// DcSmartRestDecision. DcPartyState is the glue: it snapshots each member, asks
// FloorsFor, and holds the gate against the answer. The status panel and the
// "waiting on" log line ask the same kernel, so a wait is never named that the
// gate is not holding for.

namespace DcRestFloorDecision
{
    enum class Risk
    {
        Easy,    // the party clearly outlevels the content
        Normal,  // in between, or the content's level is unknown
        Boss,    // a dungeon boss pull; healer reserve stays high, others can spend
        Hard,    // a raid or a party not clearly above the content
    };

    enum class Role
    {
        Tank,
        Healer,
        Damage,
    };

    // How many levels the lowest party member must be ABOVE the next boss for
    // the content to count as Easy. At or below zero it is Hard.
    constexpr int kEasyLevelLead = 5;
    constexpr int kHardLevelLead = 0;

    // Mana floors (percent) by role and risk. Each is capped by the configured
    // floor, so a lower configured floor always wins. Hard is the configured
    // floor untouched.
    constexpr float kHealerNormalMana = 60.0f;
    constexpr float kHealerEasyMana   = 50.0f;
    constexpr float kTankNormalMana   = 45.0f;
    constexpr float kTankEasyMana     = 30.0f;
    constexpr float kDamageNormalMana = 45.0f;
    constexpr float kDamageEasyMana   = 25.0f;

    // At a dungeon boss, preserve the healer's configured reserve while letting
    // tanks and damage casters start with half a bar. Holding every mana user at
    // HighMana (65 by default) repeatedly parks the party after ordinary trash;
    // the readiness-scaled pull ceiling still treats a caster below 40% as not
    // fully ready and shrinks further below 20%.
    constexpr float kBossTankMana   = 50.0f;
    constexpr float kBossDamageMana = 50.0f;

    // HP floors (percent), same rules. The tank keeps the highest.
    constexpr float kNormalHp       = 80.0f;
    constexpr float kTankEasyHp     = 75.0f;
    constexpr float kOthersEasyHp   = 65.0f;

    // A damage dealer that has nothing it can drink recovers mana only by
    // standing still. Keep the wait bounded even when the role floor is at its
    // most conservative. Capped here even at Hard.
    constexpr float kNoDrinkDamageCap = 50.0f;

    struct Member
    {
        Role role = Role::Damage;
        bool usesMana = false;
        // Whether the member holds a drink it is old enough to use. True for a
        // real player (their client drinks) and for a member nobody asked.
        bool canDrink = true;
    };

    struct Floors
    {
        float hp = 0.0f;
        float mp = 0.0f;  // 0 for a member that does not use mana
    };

    // Risk of the next fight. `nextBossLevel` 0 means unknown.
    Risk ClassifyRisk(bool bossPull, bool raid, int lowestPartyLevel, int nextBossLevel);

    // The floors this member must meet, given the configured floors.
    Floors FloorsFor(Member const& m, float configuredHp, float configuredMp, Risk risk);

    // ---- readiness-scaled Leeroy ceiling -------------------------------------
    //
    // The dynamic pull verdict compares a pack's weight to a ceiling (in thirds
    // of an elite) that already shrinks for a fragile party. It never asked how
    // READY the party is at the moment of the pull: a fresh healer and a tank at
    // full health carry the same ceiling as a healer at 20% mana and a tank at
    // half health, in the middle of a fight that is already on. This scales the
    // ceiling down for exactly that, so a marginal pack becomes a set-up
    // (Advanced) pull instead of a face-pull. Never below three thirds (one
    // elite), and never up: a ready party keeps the ceiling it had.
    struct Readiness
    {
        float tankHpPct = 100.0f;
        float healerManaPct = 100.0f;      // 100 when there is no healer to read
        unsigned membersFighting = 0;      // party members already in combat
        bool gateNotReady = false;         // the between-pulls gate is not green
        unsigned membersDown = 0;          // dead same-map members
        float lowestManaPct = 100.0f;      // lowest mana among mana users
    };

    constexpr float kHealerManaLow   = 30.0f;
    constexpr float kHealerManaShort = 50.0f;
    constexpr float kTankHpLow       = 60.0f;
    constexpr float kTankHpShort     = 80.0f;
    constexpr unsigned kFightOnCount = 2;

    constexpr float kLowManaCaster = 20.0f;

    // Full readiness: the bar a pull within kEdgeMarginPct of the ceiling needs.
    constexpr float kFullHealerMana = 80.0f;
    constexpr float kFullTankHp     = 90.0f;
    constexpr float kFullLowestMana = 40.0f;
    constexpr unsigned kEdgeMarginPct = 85;   // weight above 85% of the ceiling is "the edge"

    bool FullyReady(Readiness const& r);

    unsigned ReadinessScaledCeilingThirds(unsigned ceilingThirds, Readiness const& r);

    // The verdict itself: a set-up (Advanced) pull when the pack outweighs the
    // readiness-scaled ceiling, OR when it sits within the edge margin of the
    // unscaled ceiling and the party is not fully ready. A pack at 12/3 against
    // a ceiling of 13/3 is accepted by a rested party and refused by one that is
    // short of mana or health.
    bool ShouldSetUp(unsigned weightThirds, unsigned ceilingThirds, Readiness const& r);
}

#endif  // _DC_REST_FLOOR_DECISION_H
