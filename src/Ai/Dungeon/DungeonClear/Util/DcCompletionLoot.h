#ifndef _PLAYERBOT_DC_COMPLETION_LOOT_H
#define _PLAYERBOT_DC_COMPLETION_LOOT_H

#include <cstdint>

namespace DcCompletionLoot
{
    // One catalogue for both the ordinary loot filter and terminal pickup.
    constexpr std::uint32_t KeyForChest(std::uint32_t chest)
    {
        return chest == 103821 ? 7146 : chest == 179516 ? 18266 : 0;
    }

    constexpr float MAX_APPROACH_YARDS = 40.0f;
    constexpr std::uint32_t BUDGET_MS = 15000;

    struct Inputs
    {
        bool mayAct = true;
        bool started = false;
        bool needsKey = false;
        bool chestPresent = false;
        bool busy = false;
        bool walking = false;
        float yards = 0.0f;
        float lootRadius = 0.0f;
        std::uint32_t elapsedMs = 0;
    };

    enum class Step { Finish, Wait, Approach, Loot, TimedOut };

    constexpr Step Decide(Inputs const& in)
    {
        if (!in.mayAct)
            return Step::Wait;
        if (!in.needsKey)
            return Step::Finish;
        if (in.started && in.elapsedMs >= BUDGET_MS)
            return Step::TimedOut;
        // A consumable chest briefly despawns between ordinary pickups.
        if (!in.chestPresent)
            return in.started ? Step::Wait : Step::Finish;
        if (!(in.yards >= 0.0f && in.yards <= MAX_APPROACH_YARDS) ||
            !(in.lootRadius > 1.0f))
            return Step::Finish;
        if (in.busy || in.walking)
            return Step::Wait;
        return in.yards > in.lootRadius - 1.0f ? Step::Approach : Step::Loot;
    }
}

#endif
