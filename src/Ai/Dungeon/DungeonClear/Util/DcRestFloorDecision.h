/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_REST_FLOOR_DECISION_H
#define _DC_REST_FLOOR_DECISION_H

#include <vector>

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
        // A party whose lowest member is under kLowLevelParty and not clearly
        // above the content: everyone rests to the configured floors before every
        // pull, boss or trash. Natural regeneration refills a level 15 to 25 mana
        // bar in seconds, so a damage dealer with nothing to drink still waits
        // (kNoDrinkDamageCap does not apply); a mana tank with no drink keeps
        // kNoDrinkTankCap. Live (wow-overseer#575): 134 of 236 guild pulls went
        // in with the gate NOT ready, casters at a 50% median.
        LowLevel,
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
    // Party level under which pulls are sized by the tank's own health
    // (TankHpCeilingThirds) and rests go to the configured floors (Risk::LowLevel).
    constexpr int kLowLevelParty = 30;
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

    // A mana-using tank also cannot reach its boss reserve without a usable
    // drink. Keep more for the tank than damage dealers, but avoid a long
    // stationary wait for mana that cannot be replenished in the field.
    constexpr float kNoDrinkTankCap = 35.0f;

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

    // ---- power type: only a mana user waits on mana ---------------------------
    //
    // Warriors fight on rage (0 out of combat, never waited for), rogues on
    // energy (regenerates in seconds), a druid in bear form on rage and in cat
    // form on energy with its mana bar hidden. A druid TANK fights on rage and
    // cannot drink in bear form, so it is judged by health alone whatever form it
    // stands in. A shapeshifted druid HEALER shifts out to heal, so its mana
    // still counts. Everyone else counts mana exactly when its current power is
    // mana. Everyone gates on health.
    enum class Power
    {
        Mana,
        Rage,
        Energy,
        Other,  // runic power, focus, none
    };

    bool GatesOnMana(Power currentPower, bool druid, Role role);

    // Risk of the next fight. `nextBossLevel` 0 means unknown. A party whose
    // lowest member is under kLowLevelParty is LowLevel unless it is Easy.
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

    // ---- whose mana is "the healer's" -------------------------------------------
    //
    // The readiness gate read healer mana only from members with a heal strategy
    // and reported 100% when there were none, so a group whose healer seat held a
    // Retribution paladin read 100% at all 145 Wailing Caverns pull decisions
    // (wow-overseer#575). The healer is the member SEATED as healer (the group's
    // dungeon-finder role), else any member running a heal strategy; among
    // several, the lowest mana. Only members that gate on mana (GatesOnMana) are
    // candidates. Returns a negative number when there is no healer to read.
    struct HealerCandidate
    {
        bool  seatedHealer = false;
        bool  healStrategy = false;
        float manaPct = 100.0f;
    };

    float HealerManaPct(std::vector<HealerCandidate> const& members);

    // The verdict itself: a set-up (Advanced) pull when the pack outweighs the
    // readiness-scaled ceiling, OR when it sits within the edge margin of the
    // unscaled ceiling and the party is not fully ready. A pack at 12/3 against
    // a ceiling of 13/3 is accepted by a rested party and refused by one that is
    // short of mana or health.
    bool ShouldSetUp(unsigned weightThirds, unsigned ceilingThirds, Readiness const& r);

    // ---- low-level ceiling: what the tank's body can hold ----------------------
    //
    // The fragility scale above reads the WHOLE party's health per level. At low
    // levels the tank's own health is what a pull spends, and the elite count
    // alone over-states what a level 16 to 20 tank in white gear holds. Live
    // (wow-overseer#575, runs 190 to 211): Ragefire packs of 4 to 8 troggs and
    // Wailing Caverns packs of 3 were pulled against ceilings of 10 to 11 thirds,
    // and the tank or the warlocks died first.
    //
    // Below kLowLevelParty (party average level) the ceiling is also capped at
    // 3 * tankMaxHp / (kTankHpPerEliteLevel * mobLevel) thirds: a tank with 16
    // health per level of the mob holds one elite. A 650 health tank against
    // level 19 elites holds two, a 350 health one holds one. Never below one elite
    // and never above the ceiling it was given. Zero inputs (unknown) leave the
    // ceiling alone, and so does a party at or above kLowLevelParty.
    constexpr float kTankHpPerEliteLevel = 16.0f;

    unsigned TankHpCeilingThirds(unsigned ceilingThirds, int partyAverageLevel,
                                 unsigned tankMaxHp, int mobLevel);

    // ---- pull size: the third verdict -----------------------------------------
    //
    // ShouldSetUp answers face-pull or set-up pull. A set-up (Advanced) pull of a
    // pack far over the ceiling still drags the whole pack to camp: a human group
    // at level 18 does not pull eight troggs at once at all. So a pack whose
    // weight is over kOversizePct of the readiness-scaled ceiling is not pulled
    // whole:
    //
    //   - SetUp when what a ranged tag actually brings (`tagThirds`: the target,
    //     its formation, and one assist hop, without the proximity aggro of a
    //     fight on top of the pack) fits the readiness-scaled ceiling. The set-up
    //     pull tags the nearest mob from range, so that IS the split pull.
    //   - Wait when the tag is still over the scaled ceiling: hold out of aggro
    //     while the party rests (the scaled ceiling grows back) and wanderers move.
    //   - TooBig when the tag is over `neverWholePct` of the UNSCALED ceiling:
    //     never pulled, however long the wait. 0 disables the cap (TooBig reads
    //     as Wait).
    enum class PullSize
    {
        FacePull,  // Leeroy
        SetUp,     // Advanced pull to camp
        Wait,      // hold out of aggro; bounded by the caller's wait budget
        TooBig,    // hold; never pulled whole
    };

    constexpr unsigned kOversizePct   = 150;
    constexpr unsigned kNeverWholePct = 200;

    PullSize ClassifyPullSize(unsigned weightThirds, unsigned tagThirds,
                              unsigned ceilingThirds, Readiness const& r,
                              unsigned neverWholePct);

    // Whether the governor holds the pack this tick. Wait holds until its wait
    // budget runs out and then becomes a set-up pull; TooBig always holds.
    bool PullSizeHolds(PullSize size, bool waitExpired);
}

#endif  // _DC_REST_FLOOR_DECISION_H
