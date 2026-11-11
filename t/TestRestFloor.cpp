/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcRestFloorDecision.h"

using namespace DcRestFloorDecision;

namespace
{
    // The live stock floors: AiPlayerbot.AlmostFullHealth 85, HighMana 65.
    constexpr float kHp = 85.0f;
    constexpr float kMp = 65.0f;

    Member Mage()   { Member m; m.role = Role::Damage; m.usesMana = true; return m; }
    Member Priest() { Member m; m.role = Role::Healer; m.usesMana = true; return m; }
    Member Warrior(){ Member m; m.role = Role::Tank;   m.usesMana = false; return m; }
    Member Rogue()  { Member m; m.role = Role::Damage; m.usesMana = false; return m; }

    bool Meets(Member const& m, float hp, float mp, Risk r)
    {
        Floors const f = FloorsFor(m, kHp, kMp, r);
        return hp >= f.hp && (!m.usesMana || mp >= f.mp);
    }
}

// ---- the live case --------------------------------------------------------------

TEST(DcRestFloorTest, MageAtSixtyFourPercentDoesNotHoldABossAfterTrash)
{
    // The configured HighMana floor is 65. Boss readiness should keep the
    // healer at that reserve without making a damage mage wait for it.
    EXPECT_FALSE(Meets(Mage(), 100.0f, 63.7f, Risk::Hard));
    EXPECT_TRUE(Meets(Mage(), 100.0f, 63.7f, Risk::Boss));
    EXPECT_TRUE(Meets(Mage(), 100.0f, 63.7f, Risk::Easy));
    EXPECT_TRUE(Meets(Mage(), 100.0f, 63.7f, Risk::Normal));
}

TEST(DcRestFloorTest, ClassifiesTheStockadeAsEasy)
{
    // Lowest party level 35 against the highest Stockade boss, level 29.
    EXPECT_EQ(Risk::Easy, ClassifyRisk(false, false, 35, 29));
}

// ---- risk classification --------------------------------------------------------

TEST(DcRestFloorTest, BossPullUsesRoleFloorsWhileRaidsRemainHard)
{
    EXPECT_EQ(Risk::Boss, ClassifyRisk(true, false, 60, 20));
    EXPECT_EQ(Risk::Hard, ClassifyRisk(false, true, 60, 20));
    EXPECT_EQ(Risk::Boss, ClassifyRisk(true, false, 35, 29));
}

TEST(DcRestFloorTest, PartyAtOrBelowTheBossIsHard)
{
    EXPECT_EQ(Risk::Hard, ClassifyRisk(false, false, 80, 82));   // heroic
    EXPECT_EQ(Risk::Hard, ClassifyRisk(false, false, 30, 30));
}

TEST(DcRestFloorTest, ALeadOfFourIsNormalAndFiveIsEasy)
{
    EXPECT_EQ(Risk::Normal, ClassifyRisk(false, false, 34, 30));
    EXPECT_EQ(Risk::Easy, ClassifyRisk(false, false, 35, 30));
    EXPECT_EQ(Risk::Normal, ClassifyRisk(false, false, 31, 30));
}

TEST(DcRestFloorTest, UnknownLevelsAreNormalNotEasy)
{
    EXPECT_EQ(Risk::Normal, ClassifyRisk(false, false, 35, 0));
    EXPECT_EQ(Risk::Normal, ClassifyRisk(false, false, 0, 29));
}

// ---- hard changes nothing -------------------------------------------------------

TEST(DcRestFloorTest, HardKeepsTheConfiguredFloorsForEveryRole)
{
    for (Member const& m : {Mage(), Priest(), Warrior(), Rogue()})
    {
        Floors const f = FloorsFor(m, kHp, kMp, Risk::Hard);
        EXPECT_FLOAT_EQ(kHp, f.hp);
        EXPECT_FLOAT_EQ(m.usesMana ? kMp : 0.0f, f.mp);
    }
}

TEST(DcRestFloorTest, BossKeepsHealerReserveButLetsOtherManaUsersStartAtHalf)
{
    Floors const healer = FloorsFor(Priest(), kHp, kMp, Risk::Boss);
    Floors const damage = FloorsFor(Mage(), kHp, kMp, Risk::Boss);
    Member manaTank = Mage();
    manaTank.role = Role::Tank;
    Floors const tankMana = FloorsFor(manaTank, kHp, kMp, Risk::Boss);

    EXPECT_FLOAT_EQ(kHp, healer.hp);
    EXPECT_FLOAT_EQ(kMp, healer.mp);
    EXPECT_FLOAT_EQ(kHp, damage.hp);
    EXPECT_FLOAT_EQ(kBossDamageMana, damage.mp);
    EXPECT_FLOAT_EQ(kBossTankMana, tankMana.mp);
    EXPECT_TRUE(Meets(Mage(), 100.0f, 50.0f, Risk::Boss));
    EXPECT_FALSE(Meets(Priest(), 100.0f, 64.9f, Risk::Boss));
    EXPECT_FALSE(Meets(Mage(), 100.0f, 49.9f, Risk::Boss));
}

TEST(DcRestFloorTest, BossManaFloorsNeverRaiseAnOperatorFloor)
{
    Floors const healer = FloorsFor(Priest(), kHp, 40.0f, Risk::Boss);
    Floors const damage = FloorsFor(Mage(), kHp, 40.0f, Risk::Boss);

    EXPECT_FLOAT_EQ(40.0f, healer.mp);
    EXPECT_FLOAT_EQ(40.0f, damage.mp);
}

// ---- the healer is the insurance ------------------------------------------------

TEST(DcRestFloorTest, HealerKeepsTheHighestManaFloorAtEveryRisk)
{
    Member const p = Priest();
    EXPECT_GT(FloorsFor(p, kHp, kMp, Risk::Normal).mp, FloorsFor(Mage(), kHp, kMp, Risk::Normal).mp);
    EXPECT_GT(FloorsFor(p, kHp, kMp, Risk::Easy).mp, FloorsFor(Mage(), kHp, kMp, Risk::Easy).mp);
    EXPECT_FLOAT_EQ(kHealerEasyMana, FloorsFor(p, kHp, kMp, Risk::Easy).mp);
}

TEST(DcRestFloorTest, AHealerBelowItsFloorStillHoldsAnEasyPull)
{
    EXPECT_FALSE(Meets(Priest(), 100.0f, 35.0f, Risk::Easy));
    EXPECT_TRUE(Meets(Priest(), 100.0f, 51.0f, Risk::Easy));
    EXPECT_FALSE(Meets(Priest(), 100.0f, 45.0f, Risk::Easy));
    EXPECT_FALSE(Meets(Priest(), 100.0f, 55.0f, Risk::Normal));
}

// ---- the tank spends HP first ---------------------------------------------------

TEST(DcRestFloorTest, TankHpFloorRelaxesLastAndNeverBelowSeventyFive)
{
    EXPECT_FLOAT_EQ(kTankEasyHp, FloorsFor(Warrior(), kHp, kMp, Risk::Easy).hp);
    EXPECT_GT(FloorsFor(Warrior(), kHp, kMp, Risk::Easy).hp, FloorsFor(Rogue(), kHp, kMp, Risk::Easy).hp);
    EXPECT_FALSE(Meets(Warrior(), 70.0f, 0.0f, Risk::Easy));
}

// ---- a lower configured floor always wins ---------------------------------------

TEST(DcRestFloorTest, NeverRaisesAConfiguredFloor)
{
    Floors const f = FloorsFor(Priest(), 50.0f, 30.0f, Risk::Normal);
    EXPECT_FLOAT_EQ(50.0f, f.hp);
    EXPECT_FLOAT_EQ(30.0f, f.mp);
}

TEST(DcRestFloorTest, NonManaUserHasNoManaFloor)
{
    EXPECT_FLOAT_EQ(0.0f, FloorsFor(Warrior(), kHp, kMp, Risk::Hard).mp);
    EXPECT_FLOAT_EQ(0.0f, FloorsFor(Rogue(), kHp, kMp, Risk::Easy).mp);
}

// ---- nothing to drink -----------------------------------------------------------

TEST(DcRestFloorTest, DamageDealerWithNoDrinkIsCappedEvenOnABossPull)
{
    Member m = Mage();
    m.canDrink = false;
    EXPECT_FLOAT_EQ(kNoDrinkDamageCap, FloorsFor(m, kHp, kMp, Risk::Hard).mp);
    // and the cap does not touch a mage that can drink
    EXPECT_FLOAT_EQ(kMp, FloorsFor(Mage(), kHp, kMp, Risk::Hard).mp);
}

TEST(DcRestFloorTest, TankWithNoDrinkUsesTheLowerReserveOnABossPull)
{
    Member tank = Mage();
    tank.role = Role::Tank;
    tank.canDrink = false;
    EXPECT_FLOAT_EQ(kNoDrinkTankCap, FloorsFor(tank, kHp, kMp, Risk::Boss).mp);
    EXPECT_FLOAT_EQ(kNoDrinkTankCap, FloorsFor(tank, kHp, kMp, Risk::Hard).mp);
    tank.canDrink = true;
    EXPECT_FLOAT_EQ(kBossTankMana, FloorsFor(tank, kHp, kMp, Risk::Boss).mp);
}

TEST(DcRestFloorTest, HealerWithNoDrinkIsNotCapped)
{
    Member p = Priest();
    p.canDrink = false;
    EXPECT_FLOAT_EQ(kMp, FloorsFor(p, kHp, kMp, Risk::Hard).mp);
}

// ---- the damage-mana waiver (issue 102) -------------------------------------------

TEST(DcRestFloorTest, AWaivedDamageDealerHasNoManaFloorAtAnyRisk)
{
    // Live, run 4747 (Scarlet Library): Grunty, a level 53 hunter with no drink in
    // a level 35 to 38 party, held the wait twice at Arcanist Doan and the run was
    // ended both times. Once the wait has gone on without its mana, the hunter
    // holds nothing, whatever the next fight is.
    Member hunter = Mage();
    hunter.canDrink = false;
    hunter.manaWaived = true;
    for (Risk r : {Risk::Easy, Risk::Normal, Risk::Boss, Risk::Hard, Risk::LowLevel})
    {
        EXPECT_FLOAT_EQ(0.0f, FloorsFor(hunter, kHp, kMp, r).mp);
        EXPECT_TRUE(Meets(hunter, 100.0f, 14.0f, r));
    }
    // A drink in the bags changes nothing about a waiver.
    hunter.canDrink = true;
    EXPECT_FLOAT_EQ(0.0f, FloorsFor(hunter, kHp, kMp, Risk::Hard).mp);
}

TEST(DcRestFloorTest, TheWaiverNeverTouchesHealersTanksOrHealth)
{
    Member priest = Priest();
    priest.manaWaived = true;
    EXPECT_FLOAT_EQ(kMp, FloorsFor(priest, kHp, kMp, Risk::Hard).mp);
    EXPECT_FALSE(Meets(priest, 100.0f, 30.0f, Risk::Boss));

    Member tank = Mage();
    tank.role = Role::Tank;
    tank.manaWaived = true;
    EXPECT_FLOAT_EQ(kBossTankMana, FloorsFor(tank, kHp, kMp, Risk::Boss).mp);

    // Health stays held for the waived damage dealer.
    Member hunter = Mage();
    hunter.manaWaived = true;
    EXPECT_FLOAT_EQ(kHp, FloorsFor(hunter, kHp, kMp, Risk::Hard).hp);
    EXPECT_FALSE(Meets(hunter, 40.0f, 100.0f, Risk::Hard));
}

TEST(DcRestFloorTest, WithoutTheWaiverANoDrinkDamageDealerStillWaits)
{
    // The waiver is the only thing that drops the floor: the ordinary no-drink
    // damage dealer keeps its 50% cap, so a fresh wait still lets it regenerate.
    Member hunter = Mage();
    hunter.canDrink = false;
    EXPECT_FALSE(Meets(hunter, 100.0f, 14.0f, Risk::Boss));
    EXPECT_FLOAT_EQ(kNoDrinkDamageCap, FloorsFor(hunter, kHp, kMp, Risk::Hard).mp);
}


// ---- readiness-scaled Leeroy ceiling --------------------------------------------

TEST(DcReadinessCeilingTest, ReadyPartyKeepsTheCeiling)
{
    EXPECT_EQ(15u, ReadinessScaledCeilingThirds(15, Readiness{}));
}

TEST(DcReadinessCeilingTest, ALowHealerShrinksTheCeiling)
{
    Readiness r;
    r.healerManaPct = 20.0f;
    EXPECT_EQ(6u, ReadinessScaledCeilingThirds(15, r));
    r.healerManaPct = 45.0f;
    EXPECT_EQ(11u, ReadinessScaledCeilingThirds(15, r));
    r.healerManaPct = 50.0f;
    EXPECT_EQ(15u, ReadinessScaledCeilingThirds(15, r));
}

TEST(DcReadinessCeilingTest, AHurtTankShrinksTheCeiling)
{
    Readiness r;
    r.tankHpPct = 55.0f;
    EXPECT_EQ(8u, ReadinessScaledCeilingThirds(15, r));
    r.tankHpPct = 75.0f;
    EXPECT_EQ(12u, ReadinessScaledCeilingThirds(15, r));
}

TEST(DcReadinessCeilingTest, AFightAlreadyOnAndAnUnreadyGateEachCost)
{
    Readiness r;
    r.membersFighting = 2;
    EXPECT_EQ(11u, ReadinessScaledCeilingThirds(15, r));
    r.membersFighting = 1;
    EXPECT_EQ(15u, ReadinessScaledCeilingThirds(15, r));
    r.gateNotReady = true;
    EXPECT_EQ(11u, ReadinessScaledCeilingThirds(15, r));
}

TEST(DcReadinessCeilingTest, NeverBelowOneElite)
{
    Readiness r;
    r.healerManaPct = 5.0f;
    r.tankHpPct = 20.0f;
    r.membersFighting = 4;
    r.gateNotReady = true;
    EXPECT_EQ(3u, ReadinessScaledCeilingThirds(15, r));
    EXPECT_EQ(3u, ReadinessScaledCeilingThirds(3, r));
    EXPECT_EQ(0u, ReadinessScaledCeilingThirds(0, r));
}

TEST(DcReadinessCeilingTest, TheStockadeWipeIsAdvancedNotLeeroy)
{
    // The live wipe: a four-mob inmate pack (weight 12 thirds) against a ceiling
    // of 13, with the healer low, the tank hurt and two members already fighting.
    // Full readiness says Leeroy; this readiness must say set it up.
    unsigned const weight = 12;
    EXPECT_LE(weight, ReadinessScaledCeilingThirds(13, Readiness{}));
    Readiness r;
    r.healerManaPct = 35.0f;
    r.tankHpPct = 70.0f;
    r.membersFighting = 2;
    r.gateNotReady = true;
    EXPECT_GT(weight, ReadinessScaledCeilingThirds(13, r));
}


TEST(DcReadinessCeilingTest, ADeadMemberAndALowCasterCost)
{
    Readiness r;
    r.membersDown = 1;
    EXPECT_EQ(9u, ReadinessScaledCeilingThirds(15, r));
    r = Readiness{};
    r.lowestManaPct = 10.0f;
    EXPECT_EQ(13u, ReadinessScaledCeilingThirds(15, r));
}

TEST(DcReadinessCeilingTest, AnEdgePullNeedsFullReadiness)
{
    // The live pull: weight 12/3 against 13/3, ADVANCED-or-LEEROY hinged on
    // nothing but the static ceiling. Rested: a face-pull. Anything short of
    // full readiness: a set-up.
    EXPECT_FALSE(ShouldSetUp(12, 13, Readiness{}));
    Readiness r;
    r.lowestManaPct = 35.0f;
    EXPECT_TRUE(ShouldSetUp(12, 13, r));
    r = Readiness{};
    r.healerManaPct = 70.0f;
    EXPECT_TRUE(ShouldSetUp(12, 13, r));
    r = Readiness{};
    r.membersFighting = 1;
    EXPECT_TRUE(ShouldSetUp(12, 13, r));
}

TEST(DcReadinessCeilingTest, AWellInsideThePullIsNotHeldByTheMargin)
{
    Readiness r;
    r.lowestManaPct = 35.0f;   // short of full readiness
    EXPECT_FALSE(ShouldSetUp(6, 13, r));    // 46% of the ceiling
    EXPECT_FALSE(ShouldSetUp(11, 13, r));   // 84.6%, just inside the margin
    EXPECT_TRUE(ShouldSetUp(12, 13, r));    // 92%
}

TEST(DcReadinessCeilingTest, OverTheScaledCeilingIsAlwaysASetUp)
{
    EXPECT_TRUE(ShouldSetUp(14, 13, Readiness{}));
}

// ---- low-level ceiling: the tank's body -------------------------------------

TEST(DcPullSizeTest, LowLevelCeilingFollowsTheTanksHealth)
{
    // 3 * hp / (16 * mob level) thirds. A 650 health tank against level 19
    // elites holds two; a 350 health one holds one.
    EXPECT_EQ(6u, TankHpCeilingThirds(15, 19, 650, 19));
    EXPECT_EQ(3u, TankHpCeilingThirds(15, 16, 350, 19));
    // Ragefire: a level 17 warrior at 450 health against level 14 troggs.
    EXPECT_EQ(6u, TankHpCeilingThirds(10, 17, 450, 14));
}

TEST(DcPullSizeTest, LowLevelCeilingNeverRaisesNorDropsUnderOneElite)
{
    EXPECT_EQ(9u, TankHpCeilingThirds(9, 25, 5000, 20));  // never above the ceiling given
    EXPECT_EQ(3u, TankHpCeilingThirds(15, 12, 100, 14));  // never under one elite
}

TEST(DcPullSizeTest, LowLevelCeilingLeavesHigherLevelsAndUnknownsAlone)
{
    EXPECT_EQ(15u, TankHpCeilingThirds(15, 30, 350, 19));  // at the line: unchanged
    EXPECT_EQ(15u, TankHpCeilingThirds(15, 60, 350, 62));
    EXPECT_EQ(15u, TankHpCeilingThirds(15, 0, 350, 19));   // unknown level
    EXPECT_EQ(15u, TankHpCeilingThirds(15, 19, 0, 19));    // unknown health
    EXPECT_EQ(15u, TankHpCeilingThirds(15, 19, 350, 0));   // unknown mob level
}

// ---- pull size: the third verdict -------------------------------------------

TEST(DcPullSizeTest, UnderTheOversizeLineKeepsTheTwoWayVerdict)
{
    Readiness ready;
    EXPECT_EQ(PullSize::FacePull, ClassifyPullSize(6, 6, 9, ready, kNeverWholePct));
    EXPECT_EQ(PullSize::SetUp, ClassifyPullSize(12, 12, 9, ready, kNeverWholePct));  // 1.33x
    EXPECT_EQ(PullSize::SetUp, ClassifyPullSize(9, 9, 6, ready, kNeverWholePct));    // exactly 1.5x
}

TEST(DcPullSizeTest, RagefireEightTroggsAreNeverPulledWhole)
{
    // Run 205: packs of 4 to 8 troggs, the tank at 450 health, ceiling 6 thirds
    // after the tank cap. Eight troggs clumped inside one assist hop: the tag
    // brings all 24 thirds, four times the ceiling.
    Readiness ready;
    EXPECT_EQ(PullSize::TooBig, ClassifyPullSize(24, 24, 6, ready, kNeverWholePct));
    EXPECT_EQ(PullSize::TooBig, ClassifyPullSize(15, 15, 6, ready, kNeverWholePct));
    EXPECT_TRUE(PullSizeHolds(PullSize::TooBig, false));
    EXPECT_TRUE(PullSizeHolds(PullSize::TooBig, true));
}

TEST(DcPullSizeTest, ARoomClearThatGaveUpReleasesATooBigHold)
{
    // Emperor Dagran Thaurissan's throne room (live, 2026-11-11): a 32-strong
    // Senator pack reads TooBig, which holds for good. Room clear gave up with 47
    // left and pulled the boss anyway, but the tank stood planted in front of the
    // pack until a `dc skip` skipped the Emperor and ended the run. Once the room
    // clear has given up, neither a TooBig nor a Wait hold may stand.
    Readiness ready;
    EXPECT_EQ(PullSize::TooBig, ClassifyPullSize(56, 56, 15, ready, kNeverWholePct));
    EXPECT_TRUE(PullSizeHolds(PullSize::TooBig, false, false));
    EXPECT_FALSE(PullSizeHolds(PullSize::TooBig, false, true));
    EXPECT_FALSE(PullSizeHolds(PullSize::Wait, false, true));
}

TEST(DcPullSizeTest, AFourTroggClumpWaitsForRestThenIsSetUp)
{
    // Healer at 40%: the scaled ceiling is 4 of 6. Twelve thirds is three
    // times that, the tag is no smaller, and it sits at exactly twice the full
    // ceiling, so it is held until the party rests, never face-pulled.
    Readiness tired;
    tired.healerManaPct = 40.0f;
    EXPECT_EQ(4u, ReadinessScaledCeilingThirds(6, tired));
    EXPECT_EQ(PullSize::Wait, ClassifyPullSize(12, 12, 6, tired, kNeverWholePct));
    EXPECT_TRUE(PullSizeHolds(PullSize::Wait, false));
    EXPECT_FALSE(PullSizeHolds(PullSize::Wait, true));  // budget spent: set it up
}

TEST(DcPullSizeTest, ASplitPullTakesOnlyWhatTheTagBrings)
{
    // Fifteen thirds counted for a fight on top of the pack, but a ranged tag
    // of the nearest mob brings two elites: the set-up pull takes those.
    Readiness ready;
    EXPECT_EQ(PullSize::SetUp, ClassifyPullSize(15, 6, 6, ready, kNeverWholePct));
    EXPECT_FALSE(PullSizeHolds(PullSize::SetUp, false));
}

TEST(DcPullSizeTest, ANeverCapOfZeroOnlyWaits)
{
    Readiness ready;
    EXPECT_EQ(PullSize::Wait, ClassifyPullSize(24, 24, 6, ready, 0));
}

// ---- power type: only a mana user waits on mana -----------------------------

namespace
{
    // A member as DcPartyState builds it: usesMana from its current power and form.
    Member Built(Power power, bool druid, Role role)
    {
        Member m;
        m.role = role;
        m.usesMana = GatesOnMana(power, druid, role);
        m.canDrink = false;
        return m;
    }

    void ExpectNeverBlocksOnMana(Member const& m)
    {
        for (Risk r : {Risk::Easy, Risk::Normal, Risk::Boss, Risk::Hard, Risk::LowLevel})
        {
            EXPECT_FLOAT_EQ(0.0f, FloorsFor(m, kHp, kMp, r).mp);
            EXPECT_TRUE(Meets(m, 100.0f, 0.0f, r));
        }
    }
}

TEST(DcRestFloorTest, AWarriorTankNeverBlocksAPullOnMana)
{
    // Rage starts at 0 out of combat; waiting for it would wait forever.
    EXPECT_FALSE(GatesOnMana(Power::Rage, false, Role::Tank));
    ExpectNeverBlocksOnMana(Built(Power::Rage, false, Role::Tank));
}

TEST(DcRestFloorTest, ARogueNeverBlocksAPullOnMana)
{
    EXPECT_FALSE(GatesOnMana(Power::Energy, false, Role::Damage));
    ExpectNeverBlocksOnMana(Built(Power::Energy, false, Role::Damage));
}

TEST(DcRestFloorTest, ABearFormDruidTankNeverBlocksAPullOnMana)
{
    // In bear form its mana is hidden and it cannot drink; judged by health only.
    EXPECT_FALSE(GatesOnMana(Power::Rage, true, Role::Tank));
    ExpectNeverBlocksOnMana(Built(Power::Rage, true, Role::Tank));
    // Out of form between pulls it still tanks on rage: its mana stays irrelevant.
    EXPECT_FALSE(GatesOnMana(Power::Mana, true, Role::Tank));
    // Health still gates it.
    EXPECT_FALSE(Meets(Built(Power::Rage, true, Role::Tank), 50.0f, 0.0f, Risk::LowLevel));
}

TEST(DcRestFloorTest, ManaUsersAndAShapeshiftedDruidHealerStillWaitOnMana)
{
    EXPECT_TRUE(GatesOnMana(Power::Mana, false, Role::Healer));   // priest
    EXPECT_TRUE(GatesOnMana(Power::Mana, false, Role::Damage));   // mage, warlock, hunter
    EXPECT_TRUE(GatesOnMana(Power::Mana, false, Role::Tank));     // paladin tank
    EXPECT_TRUE(GatesOnMana(Power::Energy, true, Role::Healer));  // shifts out to heal
    EXPECT_FALSE(GatesOnMana(Power::Energy, true, Role::Damage)); // cat-form druid
}

// ---- low level: rest to the configured floor ---------------------------------

TEST(DcRestFloorTest, ALowLevelPartyRestsToTheConfiguredFloor)
{
    // wow-overseer#575: casters pulled at a 50% median with no drinks. Under
    // level 30 the stock floors hold for every role, boss or trash.
    EXPECT_EQ(Risk::LowLevel, ClassifyRisk(false, false, 18, 20));
    EXPECT_EQ(Risk::LowLevel, ClassifyRisk(false, false, 18, 15));
    EXPECT_EQ(Risk::LowLevel, ClassifyRisk(true, false, 19, 20));
    EXPECT_EQ(Risk::Easy, ClassifyRisk(false, false, 25, 18));    // clearly over it
    EXPECT_EQ(Risk::Hard, ClassifyRisk(false, true, 18, 20));     // raids unchanged

    Risk const r = ClassifyRisk(true, false, 19, 20);
    EXPECT_FLOAT_EQ(kMp, FloorsFor(Mage(), kHp, kMp, r).mp);
    EXPECT_FLOAT_EQ(kMp, FloorsFor(Priest(), kHp, kMp, r).mp);
    EXPECT_FLOAT_EQ(kHp, FloorsFor(Warrior(), kHp, kMp, r).hp);
}

TEST(DcRestFloorTest, ALowLevelCasterWithNoDrinkStillRegensToTheFloor)
{
    Member mage = Mage();
    mage.canDrink = false;
    EXPECT_FLOAT_EQ(kMp, FloorsFor(mage, kHp, kMp, ClassifyRisk(false, false, 18, 20)).mp);
    // A mana tank with no drink keeps its bounded wait (#12).
    Member pally;
    pally.role = Role::Tank;
    pally.usesMana = true;
    pally.canDrink = false;
    EXPECT_FLOAT_EQ(kNoDrinkTankCap,
                    FloorsFor(pally, kHp, kMp, ClassifyRisk(false, false, 18, 20)).mp);
}

// ---- whose mana is the healer's ----------------------------------------------

TEST(DcRestFloorTest, TheSeatedHealerIsReadEvenWithoutAHealStrategy)
{
    // Cave's Retribution paladin in the healer seat: no heal strategy, so the
    // gate read 100% at every pull. The seat is read now.
    HealerCandidate ret;
    ret.seatedHealer = true;
    ret.manaPct = 30.0f;
    HealerCandidate mage;
    mage.manaPct = 10.0f;
    EXPECT_FLOAT_EQ(30.0f, HealerManaPct({ret, mage}));
}

TEST(DcRestFloorTest, TheSeatWinsElseAHealStrategyElseNobody)
{
    HealerCandidate seat;
    seat.seatedHealer = true;
    seat.manaPct = 70.0f;
    HealerCandidate shaman;
    shaman.healStrategy = true;
    shaman.manaPct = 20.0f;
    EXPECT_FLOAT_EQ(70.0f, HealerManaPct({seat, shaman}));
    EXPECT_FLOAT_EQ(20.0f, HealerManaPct({shaman}));
    HealerCandidate dps;
    EXPECT_LT(HealerManaPct({dps}), 0.0f);
    EXPECT_LT(HealerManaPct({}), 0.0f);
}

// ---- patrol-contended ceiling: the damage mana waiver must not raise the ceiling ----
//
// When a lone patroller in chain range is the only reason a pack reads ADVANCED
// (patrol-contended), the damage mana waiver (issue 102) must not make the party
// appear ready and raise the ceiling. The BRD Stilgiss/Fineous case: Fineous
// (patroller) is in chain range of Stilgiss; a dry mage with waived mana would
// raise the ceiling via gateNotReady=false, incorrectly making the patrol wait
// expire and the pull commit. The fix computes the ceiling WITHOUT the waiver for
// the patrol-contended check.
TEST(DcReadinessCeilingTest, PatrolContendedCeilingIgnoresDamageManaWaiver)
{
    // Full readiness (waived): gateNotReady=false -> ceiling = 15
    Readiness readyWaived;
    readyWaived.gateNotReady = false;
    EXPECT_EQ(15u, ReadinessScaledCeilingThirds(15, readyWaived));

    // Same party, no waiver (damage dealer OOM): gateNotReady=true -> ceiling = 11
    Readiness readyNoWaiver = readyWaived;
    readyNoWaiver.gateNotReady = true;
    EXPECT_EQ(11u, ReadinessScaledCeilingThirds(15, readyNoWaiver));

    // A pack at weight 12/3: with waiver it's a face-pull (12 <= 15),
    // without waiver it's patrol-contended (12 > 11). The patrol-contended
    // check MUST use the non-waived ceiling.
    unsigned const weight = 12;
    EXPECT_LE(weight, ReadinessScaledCeilingThirds(15, readyWaived));   // appears Leeroy
    EXPECT_GT(weight, ReadinessScaledCeilingThirds(15, readyNoWaiver)); // actually contended
}

// ---- Stilgiss/Fineous BRD scenario: dry damage dealer pulling Fineous in chain range ----
//
// Live issue: BRD Stilgiss (static boss) and Fineous (patroller) share a chamber.
// Fineous patrols into chain range of Stilgiss. A dry damage dealer (mage with 0 mana)
// that received the damage-mana waiver (issue 102) would have gateNotReady=false,
// incorrectly making the party appear fully ready and raising the patrol-contended
// ceiling. The fix ensures the patrol-contended check uses the non-waived ceiling.
// This test covers the full classification: ShouldSetUp returns true (set-up pull,
// not face-pull) because the pack is patrol-contended even though the waiver makes
// the party look ready at first glance.
TEST(DcReadinessCeilingTest, StilgissFineousPatrolContendedWithDryMage)
{
    // Ceiling for this chamber: 13 thirds (roughly 4-5 elites)
    unsigned const ceiling = 13;
    // Pack weight: Stilgiss + Fineous = 12 thirds (at the edge margin of 13)
    unsigned const weight = 12;
    // Tag: only Fineous in chain range = 6 thirds (well within ceiling)
    unsigned const tag = 6;
    unsigned const neverWholePct = 200; // default

    // Waived readiness: dry mage's mana waived, gate is green
    Readiness readyWaived;
    readyWaived.gateNotReady = false;
    readyWaived.healerManaPct = 80.0f;   // healer fine
    readyWaived.tankHpPct = 90.0f;       // tank fine
    readyWaived.lowestManaPct = 10.0f;   // dry mage at 10% mana (waived, so not counted)
    readyWaived.membersFighting = 0;
    readyWaived.membersDown = 0;

    // Non-waived readiness: dry mage OOM, gate is red
    Readiness readyNoWaiver = readyWaived;
    readyNoWaiver.gateNotReady = true;

    // With waiver: ceiling = 13, weight 12 <= 13 -> appears face-pull
    EXPECT_LE(weight, ReadinessScaledCeilingThirds(ceiling, readyWaived));
    EXPECT_FALSE(ShouldSetUp(weight, ReadinessScaledCeilingThirds(ceiling, readyWaived), readyWaived));

    // Without waiver: ceiling = 9 (13 scaled down by gateNotReady), weight 12 > 9 -> patrol-contended
    EXPECT_GT(weight, ReadinessScaledCeilingThirds(ceiling, readyNoWaiver));
    EXPECT_TRUE(ShouldSetUp(weight, ReadinessScaledCeilingThirds(ceiling, readyNoWaiver), readyNoWaiver));

    // Full classification: the tag (6) fits the scaled ceiling (9) -> SetUp, not Wait
    PullSize const sizeWaived = ClassifyPullSize(weight, tag, ceiling, readyWaived, neverWholePct);
    PullSize const sizeNoWaiver = ClassifyPullSize(weight, tag, ceiling, readyNoWaiver, neverWholePct);

    // With waiver the pack appears FacePull (dangerous - would charge into patroller)
    EXPECT_EQ(PullSize::FacePull, sizeWaived);

    // Without waiver it's correctly SetUp (Advanced pull to camp)
    EXPECT_EQ(PullSize::SetUp, sizeNoWaiver);

    // The governor must hold on SetUp until ready; the patrol-wait gate handles the rest.
    EXPECT_FALSE(PullSizeHolds(sizeWaived, false));
    EXPECT_FALSE(PullSizeHolds(sizeNoWaiver, false));
}
