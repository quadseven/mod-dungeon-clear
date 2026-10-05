/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRestFloorDecision.h"

#include <algorithm>

namespace DcRestFloorDecision
{
    Risk ClassifyRisk(bool bossPull, bool raid, int lowestPartyLevel, int nextBossLevel)
    {
        if (raid)
            return Risk::Hard;
        if (lowestPartyLevel > 0 && lowestPartyLevel < kLowLevelParty)
        {
            bool const easy = !bossPull && nextBossLevel > 0 &&
                              lowestPartyLevel - nextBossLevel >= kEasyLevelLead;
            return easy ? Risk::Easy : Risk::LowLevel;
        }
        if (bossPull)
            return Risk::Boss;
        if (lowestPartyLevel <= 0 || nextBossLevel <= 0)
            return Risk::Normal;
        int const lead = lowestPartyLevel - nextBossLevel;
        if (lead >= kEasyLevelLead)
            return Risk::Easy;
        if (lead <= kHardLevelLead)
            return Risk::Hard;
        return Risk::Normal;
    }

    Floors FloorsFor(Member const& m, float configuredHp, float configuredMp, Risk risk)
    {
        Floors out;
        out.hp = configuredHp;
        out.mp = m.usesMana ? configuredMp : 0.0f;

        if (risk == Risk::Boss && m.usesMana)
        {
            float bossMp = configuredMp;
            if (m.role == Role::Tank)
                bossMp = kBossTankMana;
            else if (m.role == Role::Damage)
                bossMp = kBossDamageMana;
            out.mp = std::min(out.mp, bossMp);
        }

        if (risk != Risk::Hard && risk != Risk::Boss && risk != Risk::LowLevel)
        {
            bool const easy = risk == Risk::Easy;
            float hp = kNormalHp;
            if (easy)
                hp = m.role == Role::Tank ? kTankEasyHp : kOthersEasyHp;
            out.hp = std::min(out.hp, hp);

            if (m.usesMana)
            {
                float mp = 0.0f;
                switch (m.role)
                {
                    case Role::Healer:
                        mp = easy ? kHealerEasyMana : kHealerNormalMana;
                        break;
                    case Role::Tank:
                        mp = easy ? kTankEasyMana : kTankNormalMana;
                        break;
                    case Role::Damage:
                        mp = easy ? kDamageEasyMana : kDamageNormalMana;
                        break;
                }
                out.mp = std::min(out.mp, mp);
            }
        }

        if (m.usesMana && !m.canDrink)
        {
            // A low-level caster regenerates its bar in seconds standing still.
            if (m.role == Role::Damage && risk != Risk::LowLevel)
                out.mp = std::min(out.mp, kNoDrinkDamageCap);
            else if (m.role == Role::Tank)
                out.mp = std::min(out.mp, kNoDrinkTankCap);
        }
        return out;
    }

    bool GatesOnMana(Power currentPower, bool druid, Role role)
    {
        if (druid && role == Role::Tank)
            return false;  // fights on rage; cannot drink in bear form
        if (currentPower == Power::Mana)
            return true;
        return druid && role == Role::Healer;  // shifts out to heal
    }

    float HealerManaPct(std::vector<HealerCandidate> const& members)
    {
        float seated = -1.0f;
        float strategy = -1.0f;
        for (HealerCandidate const& m : members)
        {
            if (m.seatedHealer && (seated < 0.0f || m.manaPct < seated))
                seated = m.manaPct;
            if (m.healStrategy && (strategy < 0.0f || m.manaPct < strategy))
                strategy = m.manaPct;
        }
        return seated >= 0.0f ? seated : strategy;
    }

    unsigned ReadinessScaledCeilingThirds(unsigned ceilingThirds, Readiness const& r)
    {
        if (ceilingThirds <= 3)
            return ceilingThirds;
        float scale = 1.0f;
        if (r.healerManaPct < kHealerManaLow)
            scale *= 0.4f;
        else if (r.healerManaPct < kHealerManaShort)
            scale *= 0.7f;
        if (r.tankHpPct < kTankHpLow)
            scale *= 0.5f;
        else if (r.tankHpPct < kTankHpShort)
            scale *= 0.8f;
        if (r.membersFighting >= kFightOnCount)
            scale *= 0.7f;
        if (r.gateNotReady)
            scale *= 0.75f;
        if (r.membersDown >= 1)
            scale *= 0.6f;
        if (r.lowestManaPct < kLowManaCaster)
            scale *= 0.85f;
        unsigned const scaled = static_cast<unsigned>(static_cast<float>(ceilingThirds) * scale + 0.5f);
        return scaled < 3 ? 3 : scaled;
    }

    bool FullyReady(Readiness const& r)
    {
        return r.healerManaPct >= kFullHealerMana && r.tankHpPct >= kFullTankHp &&
               r.membersFighting == 0 && !r.gateNotReady && r.membersDown == 0 &&
               r.lowestManaPct >= kFullLowestMana;
    }

    bool ShouldSetUp(unsigned weightThirds, unsigned ceilingThirds, Readiness const& r)
    {
        if (weightThirds > ReadinessScaledCeilingThirds(ceilingThirds, r))
            return true;
        return weightThirds * 100 > ceilingThirds * kEdgeMarginPct && !FullyReady(r);
    }

    unsigned TankHpCeilingThirds(unsigned ceilingThirds, int partyAverageLevel,
                                 unsigned tankMaxHp, int mobLevel)
    {
        if (partyAverageLevel <= 0 || partyAverageLevel >= kLowLevelParty)
            return ceilingThirds;
        if (tankMaxHp == 0 || mobLevel <= 0 || ceilingThirds <= 3)
            return ceilingThirds;
        float const held = 3.0f * static_cast<float>(tankMaxHp) /
                           (kTankHpPerEliteLevel * static_cast<float>(mobLevel));
        unsigned const thirds = static_cast<unsigned>(held + 0.5f);
        if (thirds < 3)
            return 3;
        return thirds < ceilingThirds ? thirds : ceilingThirds;
    }

    PullSize ClassifyPullSize(unsigned weightThirds, unsigned tagThirds,
                              unsigned ceilingThirds, Readiness const& r,
                              unsigned neverWholePct)
    {
        unsigned const scaled = ReadinessScaledCeilingThirds(ceilingThirds, r);
        if (weightThirds * 100 <= scaled * kOversizePct)
            return ShouldSetUp(weightThirds, ceilingThirds, r) ? PullSize::SetUp
                                                               : PullSize::FacePull;
        if (neverWholePct > 0 && tagThirds * 100 > ceilingThirds * neverWholePct)
            return PullSize::TooBig;
        if (tagThirds > scaled)
            return PullSize::Wait;
        return PullSize::SetUp;
    }

    bool PullSizeHolds(PullSize size, bool waitExpired)
    {
        switch (size)
        {
            case PullSize::TooBig:
                return true;
            case PullSize::Wait:
                return !waitExpired;
            case PullSize::FacePull:
            case PullSize::SetUp:
                break;
        }
        return false;
    }
}
