/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSTALLEDFALLBACKDECISION_H
#define _PLAYERBOT_DCSTALLEDFALLBACKDECISION_H

#include <cstddef>
#include <cstdint>

// STUB pinning today's behavior: the stalled fallback has no budget, so it never
// gives a target up and never bans one. TestStalledFallback fails against this.
namespace DcStalledFallback
{
    constexpr uint32_t BUDGET_MS = 30000;
    constexpr float PROGRESS_YARDS = 5.0f;
    constexpr float CONTACT_YARDS = 10.0f;
    constexpr uint32_t BAN_MS = 120000;

    enum class Verdict { Engage, GiveUp };

    struct Watch
    {
        static constexpr size_t MAX_BANNED = 4;
        void Reset() {}
        Verdict Observe(uint64_t, float, uint32_t) { return Verdict::Engage; }
        void Ban(uint64_t, uint32_t) {}
        bool IsBanned(uint64_t, uint32_t) const { return false; }
    };
}

#endif  // _PLAYERBOT_DCSTALLEDFALLBACKDECISION_H
