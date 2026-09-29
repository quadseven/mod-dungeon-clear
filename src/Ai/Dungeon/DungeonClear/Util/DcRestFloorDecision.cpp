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
        if (bossPull || raid)
            return Risk::Hard;
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

        if (risk != Risk::Hard)
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

        if (m.usesMana && !m.canDrink && m.role == Role::Damage)
            out.mp = std::min(out.mp, kNoDrinkDamageCap);
        return out;
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
}
