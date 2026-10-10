/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCBREATHDECISION_H
#define _PLAYERBOT_DCBREATHDECISION_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// SURFACE FOR BREATH. Pure decision core for DungeonClearSurfaceForBreath
// {Trigger,Action}; no game types, so it is unit-tested standalone.
//
// Why: playerbots has no breath handling at all, and the dungeon-clear swim
// legs (SwimPathfinder) dive the tank through flooded passages the navmesh
// cannot model. A leg that wedges stalls the run where it stands, underwater,
// and the followers hold beside it. Live on Blackfathom Deeps (dev realm,
// 2026-10-09): 19 of the 25 deaths ever recorded on map 48 were drownings at
// z -67 to -71, five members at once, three times in one hour.
//
// A player watches the breath bar and swims up before it runs out. This does
// the same. Nothing here touches the breath timer itself (no Water Breathing,
// no air bubble): it only decides WHEN to swim for air and when the bot has
// breathed enough to go back down.
//
// The breath bar is private to Player (m_MirrorTimer), so the bot keeps its own
// copy of it, ticked with the core's own rules (Player::HandleDrowning): under
// water it drains 1 ms per ms; otherwise it refills 10 ms per ms up to the max
// (Player::getMaxTimer: the WaterBreath.Timer config times the
// SPELL_AURA_MOD_WATER_BREATHING multiplier, or no timer at all while a Water
// Breathing aura is up).
namespace DcBreath
{
    constexpr int32_t RegenPerMs = 10;

    // Swim for air when the breath left is at or under the time to reach air,
    // doubled, plus this reserve. The doubling covers a slow spline (a turn,
    // a snare, a mob in the way); the reserve covers the tick that decides.
    constexpr float SurfaceTimeFactor = 2.0f;
    constexpr int32_t SurfaceReserveMs = 15000;

    // Breathe until this share of the bar is back before diving again. Lower
    // in a fight, where a bot holding at the surface is not fighting.
    constexpr float RefillShare = 0.95f;
    constexpr float RefillShareInCombat = 0.60f;

    // The way back to air: the bot's own swim since its last breath.
    constexpr float TrailSpacing = 4.0f;
    constexpr size_t TrailMax = 256;  // ~1000yd, past a 180 s bar at swim speed

    struct Point
    {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
    };

    inline float Dist(Point const& a, Point const& b)
    {
        float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    struct Model
    {
        // Breath left in ms. Meaningful only while maxMs > 0.
        int32_t remainingMs{0};
        int32_t maxMs{0};
        uint32_t lastTickMs{0};
        bool ticked{false};
        // Committed to reaching air and refilling.
        bool surfacing{false};
        // When the climb was issued (0: not yet). Lets the action tell its own
        // climb from a swim leg still in flight underneath it.
        uint32_t routeIssuedMs{0};
        // trail[0] is the last place the bot breathed; the rest is its swim
        // since then, oldest first.
        std::vector<Point> trail;

        void Reset()
        {
            remainingMs = 0;
            maxMs = 0;
            lastTickMs = 0;
            ticked = false;
            surfacing = false;
            routeIssuedMs = 0;
            trail.clear();
        }
    };

    // Advance the bar to `nowMs`. `maxMs <= 0` means no breath timer (Water
    // Breathing, dead, a GM): the bar reads full and nothing is owed. The whole
    // gap since the last tick is charged at the CURRENT state, which errs
    // toward surfacing early when the bot is under (a missed tick never makes
    // it think it has more air than it does).
    inline void Tick(Model& m, bool underWater, int32_t maxMs, uint32_t nowMs)
    {
        if (maxMs <= 0)
        {
            m.maxMs = 0;
            m.remainingMs = 0;
            m.lastTickMs = nowMs;
            m.ticked = true;
            return;
        }
        if (!m.ticked || m.maxMs <= 0)
        {
            // First sight of a timer: start full, as the core does on the
            // first submerged tick.
            m.remainingMs = maxMs;
            m.maxMs = maxMs;
            m.lastTickMs = nowMs;
            m.ticked = true;
            return;
        }
        if (maxMs != m.maxMs)
        {
            // An aura changed the bar's length: keep the same share of it.
            double const share = double(m.remainingMs) / double(m.maxMs);
            m.remainingMs = int32_t(share * double(maxMs));
            m.maxMs = maxMs;
        }
        uint32_t const dt = nowMs - m.lastTickMs;  // wrap-safe
        m.lastTickMs = nowMs;
        int64_t r = m.remainingMs;
        if (underWater)
            r -= int64_t(dt);
        else
            r += int64_t(dt) * RegenPerMs;
        if (r > m.maxMs)
            r = m.maxMs;
        if (r < 0)
            r = 0;
        m.remainingMs = int32_t(r);
    }

    // The time, in ms, to swim `yards` at `yardsPerSec`.
    inline int32_t SwimMs(float yards, float yardsPerSec)
    {
        if (yardsPerSec <= 0.1f)
            yardsPerSec = 0.1f;
        return int32_t(yards / yardsPerSec * 1000.0f);
    }

    // Start for air now? `yardsToAir` is the shortest known way up.
    inline bool ShouldSurface(Model const& m, bool underWater, float yardsToAir,
                              float yardsPerSec)
    {
        if (!underWater || m.maxMs <= 0)
            return false;
        int32_t const need =
            int32_t(float(SwimMs(yardsToAir, yardsPerSec)) * SurfaceTimeFactor) +
            SurfaceReserveMs;
        return m.remainingMs <= need;
    }

    // Has the bot breathed enough to stop surfacing?
    inline bool DoneSurfacing(Model const& m, bool underWater, bool inCombat)
    {
        if (m.maxMs <= 0)
            return true;
        if (underWater)
            return false;
        float const share = inCombat ? RefillShareInCombat : RefillShare;
        return float(m.remainingMs) >= share * float(m.maxMs);
    }

    // Keep the way back to air. Above water the trail is just where the bot
    // is; under water each step of TrailSpacing is added after the last
    // breath, and the oldest SWIM step (never the breath point) is dropped
    // past TrailMax. While surfacing the bot is swimming the trail BACKWARD,
    // so it is not extended. Instead, once the bot is at a trail step, that
    // step and everything after it are dropped, so the next retrace window
    // starts where the bot is and only ever heads toward the last breath.
    inline void RecordTrail(Model& m, bool underWater, Point const& pos)
    {
        if (!underWater || m.trail.empty())
        {
            m.trail.assign(1, pos);
            return;
        }
        if (m.surfacing)
        {
            size_t nearest = 0;
            float best = Dist(m.trail[0], pos);
            for (size_t i = 1; i < m.trail.size(); ++i)
            {
                float const d = Dist(m.trail[i], pos);
                if (d < best)
                {
                    best = d;
                    nearest = i;
                }
            }
            if (best < TrailSpacing && nearest > 0)
                m.trail.resize(nearest);
            return;
        }
        if (Dist(m.trail.back(), pos) < TrailSpacing)
            return;
        m.trail.push_back(pos);
        if (m.trail.size() > TrailMax)
            m.trail.erase(m.trail.begin() + 1);
    }

    // Yards from `pos` back along the trail to the last breath.
    inline float TrailYards(Model const& m, Point const& pos)
    {
        if (m.trail.empty())
            return 0.0f;
        float d = Dist(pos, m.trail.back());
        for (size_t i = m.trail.size() - 1; i > 0; --i)
            d += Dist(m.trail[i], m.trail[i - 1]);
        return d;
    }

    // The way back: the trail reversed, from the bot's position to the last
    // breath, at most `cap` points (the caller issues the rest next window).
    inline std::vector<Point> RetraceWindow(Model const& m, Point const& pos, size_t cap)
    {
        std::vector<Point> out;
        out.push_back(pos);
        for (size_t i = m.trail.size(); i > 0 && out.size() < cap; --i)
        {
            Point const& p = m.trail[i - 1];
            if (Dist(out.back(), p) < 1.0f)
                continue;
            out.push_back(p);
        }
        return out;
    }
}

#endif
