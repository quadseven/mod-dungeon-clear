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
