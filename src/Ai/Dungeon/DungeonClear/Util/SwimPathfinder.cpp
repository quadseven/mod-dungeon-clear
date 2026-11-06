/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SwimPathfinder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
#include <tuple>

#include "Optional.h"          // Optional<> used (unguarded) by GridTerrainData.h
#include "GridTerrainData.h"   // ZLiquidStatus (LIQUID_MAP_*); std::array<> used (unguarded) too
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"  // DC_PI

namespace
{
    // Greedy step geometry.
    constexpr float SWIM_STEP_LEN     = 4.0f;   // yd advanced per accepted hop
    constexpr int   SWIM_MAX_STEPS    = 220;    // hard budget (≈880yd of travel)
    constexpr float SWIM_ARRIVE       = 4.0f;   // within this of goal => done
    constexpr float SWIM_EXIT_RADIUS  = 12.0f;  // allow leaving water this close to goal (surfacing onto a dry platform)
    constexpr int   SWIM_MAX_NO_PROGRESS = 12;  // consecutive non-progress hops before search

    // Deflection fan, in degrees, off the straight-to-goal bearing. Tried only
    // when the straight hop is blocked. Ordered small->large so the least
    // deviating clear hop tends to win on score ties.
    constexpr float YAW_FAN[]   = { 0.0f, 20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f, 85.0f, -85.0f, 110.0f, -110.0f };
    constexpr float PITCH_FAN[] = { 0.0f, 20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f };

    constexpr float DEG2RAD = DC_PI / 180.0f;

    float Dist3D(G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        float const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // VMAP line-of-sight only (liquid is transparent to VMAP), so this tests
    // the swim line against rock / WMO geometry exactly like the route LOS
    // screen, but at the true submerged Z with no eye-height bump.
    bool LineClear(Map* map, uint32 phase, G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        return map->isInLineOfSight(a.x, a.y, a.z, b.x, b.y, b.z, phase,
                                    LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::Nothing);
    }

    bool InWater(Map* map, uint32 phase, G3D::Vector3 const& p, float coll)
    {
        LiquidData const liquid = map->GetLiquidData(phase, p.x, p.y, p.z, coll, {});
        return (liquid.Status & (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)) != 0;
    }

    // Unit direction from `base` azimuth/elevation, deflected by yaw (around
    // world Z) and pitch (elevation), returned as a world-space unit vector.
    G3D::Vector3 DeflectedDir(float az, float el, float yawDeg, float pitchDeg)
    {
        float const a = az + yawDeg * DEG2RAD;
        float e = el + pitchDeg * DEG2RAD;
        float const lim = DC_PI * 0.5f - 0.01f;
        if (e > lim) e = lim;
        if (e < -lim) e = -lim;
        float const ce = std::cos(e);
        return G3D::Vector3(ce * std::cos(a), ce * std::sin(a), std::sin(e));
    }

    struct SearchNode
    {
        int x, y, z;
        float cost;
        int parent;
        bool closed = false;
    };

    G3D::Vector3 SearchPoint(G3D::Vector3 const& start, SearchNode const& node)
    {
        return start + G3D::Vector3(node.x * SWIM_STEP_LEN,
                                     node.y * SWIM_STEP_LEN,
                                     node.z * SWIM_STEP_LEN);
    }

    struct SwimSearch
    {
        using Key = std::tuple<int, int, int>;
        using QueueEntry = std::pair<float, int>;
        G3D::Vector3 const& start;
        G3D::Vector3 const& goal;
        SwimPathfinder::LineProbe const& lineClear;
        SwimPathfinder::WaterProbe const& inWater;
        std::vector<SearchNode> nodes{{0, 0, 0, 0.0f, -1}};
        std::map<Key, int> visited{{Key(0, 0, 0), 0}};
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> open;

        SwimSearch(G3D::Vector3 const& from, G3D::Vector3 const& to,
                   SwimPathfinder::LineProbe const& clear, SwimPathfinder::WaterProbe const& wet)
            : start(from), goal(to), lineClear(clear), inWater(wet) { }

        bool WithinBounds(G3D::Vector3 const& point) const
        {
            // BFD's flooded halls can force a long sideways swim around a wall
            // before the route can turn back toward the boss. Keep enough
            // lateral room beyond the direct start/goal box for that bypass.
            constexpr float margin = 96.0f;
            return point.x >= std::min(start.x, goal.x) - margin &&
                   point.x <= std::max(start.x, goal.x) + margin &&
                   point.y >= std::min(start.y, goal.y) - margin &&
                   point.y <= std::max(start.y, goal.y) + margin &&
                   point.z >= std::min(start.z, goal.z) - margin &&
                   point.z <= std::max(start.z, goal.z) + margin;
        }

        void Consider(int index, SearchNode const& node, G3D::Vector3 const& here,
                      int dx, int dy, int dz)
        {
            if (!dx && !dy && !dz)
                return;
            SearchNode next{node.x + dx, node.y + dy, node.z + dz, 0.0f, index};
            G3D::Vector3 const point = SearchPoint(start, next);
            if (!WithinBounds(point))
                return;
            float const remaining = Dist3D(point, goal);
            if ((!inWater(point) && remaining > SWIM_EXIT_RADIUS) || !lineClear(here, point))
                return;
            float const cost = node.cost + Dist3D(here, point);
            if (cost > SWIM_MAX_STEPS * SWIM_STEP_LEN)
                return;
            Key const key(next.x, next.y, next.z);
            auto const found = visited.find(key);
            if (found != visited.end() && (nodes[found->second].closed || nodes[found->second].cost <= cost))
                return;
            next.cost = cost;
            if (found == visited.end())
            {
                int const nextIndex = static_cast<int>(nodes.size());
                nodes.push_back(next);
                visited.emplace(key, nextIndex);
                open.emplace(cost + remaining, nextIndex);
                return;
            }
            SearchNode& old = nodes[found->second];
            old.cost = cost;
            old.parent = index;
            open.emplace(cost + remaining, found->second);
        }

        SwimPathfinder::Result RouteTo(int index) const
        {
            SwimPathfinder::Result result;
            result.ok = true;
            for (int at = index; nodes[at].parent >= 0; at = nodes[at].parent)
                result.points.push_back(SearchPoint(start, nodes[at]));
            std::reverse(result.points.begin(), result.points.end());
            result.points.push_back(goal);
            return result;
        }
    };

    SwimPathfinder::Result SearchSwim(G3D::Vector3 const& start, G3D::Vector3 const& goal,
                                      SwimPathfinder::LineProbe const& lineClear,
                                      SwimPathfinder::WaterProbe const& inWater)
    {
        SwimSearch search{start, goal, lineClear, inWater};
        search.open.emplace(Dist3D(start, goal), 0);
        constexpr int MAX_EXPANDED = 12000;
        int expanded = 0;
        while (!search.open.empty() && expanded < MAX_EXPANDED)
        {
            int const index = search.open.top().second;
            search.open.pop();
            if (search.nodes[index].closed)
                continue;
            search.nodes[index].closed = true;
            ++expanded;
            SearchNode const node = search.nodes[index];
            G3D::Vector3 const here = SearchPoint(start, node);
            if (Dist3D(here, goal) <= SWIM_STEP_LEN * 2.0f && lineClear(here, goal))
                return search.RouteTo(index);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz)
                        search.Consider(index, node, here, dx, dy, dz);
        }
        SwimPathfinder::Result result;
        result.failureReason = expanded == MAX_EXPANDED ? "swim search: node budget exhausted"
                                                   : "swim search: no water route";
        return result;
    }
}

namespace SwimPathfinder
{
    Result Build(Player* bot, G3D::Vector3 const& start, G3D::Vector3 const& goal)
    {
        Result r;
        if (!bot)
        {
            r.failureReason = "no bot";
            return r;
        }
        Map* map = bot->GetMap();
        if (!map)
        {
            r.failureReason = "no map";
            return r;
        }
        uint32 const phase = bot->GetPhaseMask();
        float const coll = bot->GetCollisionHeight();

        return BuildCore(start, goal,
                         [map, phase](G3D::Vector3 const& a, G3D::Vector3 const& b)
                         { return LineClear(map, phase, a, b); },
                         [map, phase, coll](G3D::Vector3 const& p)
                         { return InWater(map, phase, p, coll); });
    }

    Result BuildCore(G3D::Vector3 const& start, G3D::Vector3 const& goal,
                     LineProbe const& lineClear, WaterProbe const& inWater)
    {
        Result r;
        G3D::Vector3 cur = start;
        float bestToGoal = Dist3D(cur, goal);
        int noProgress = 0;

        for (int step = 0; step < SWIM_MAX_STEPS; ++step)
        {
            float const d = Dist3D(cur, goal);
            // Close enough to dart straight in?
            if (d <= SWIM_ARRIVE || (d <= SWIM_STEP_LEN * 2.0f && lineClear(cur, goal)))
            {
                r.points.push_back(goal);
                r.ok = true;
                return r;
            }

            G3D::Vector3 const delta = goal - cur;
            float const len = delta.length();
            if (len < 0.001f)
            {
                r.points.push_back(goal);
                r.ok = true;
                return r;
            }
            float const az = std::atan2(delta.y, delta.x);
            float el = std::asin(std::max(-1.0f, std::min(1.0f, delta.z / len)));

            // Fast path: straight toward the goal. Most of a tubular tunnel is
            // taken one cheap raycast per step this way; only bends pay the fan.
            G3D::Vector3 const straight = cur + DeflectedDir(az, el, 0.0f, 0.0f) * SWIM_STEP_LEN;
            bool const straightOk = lineClear(cur, straight) &&
                (inWater(straight) || Dist3D(straight, goal) <= SWIM_EXIT_RADIUS);

            G3D::Vector3 chosen;
            bool found = false;
            if (straightOk)
            {
                chosen = straight;
                found = true;
            }
            else
            {
                // Probe the deflection fan; keep the clear, water-staying,
                // progressing candidate with the best (progress − deviation) score.
                float bestScore = -1.0e9f;
                for (float yaw : YAW_FAN)
                {
                    for (float pitch : PITCH_FAN)
                    {
                        if (yaw == 0.0f && pitch == 0.0f)
                            continue;  // already tried as the straight hop
                        G3D::Vector3 const cand = cur + DeflectedDir(az, el, yaw, pitch) * SWIM_STEP_LEN;
                        if (!lineClear(cur, cand))
                            continue;
                        float const candToGoal = Dist3D(cand, goal);
                        if (!inWater(cand) && candToGoal > SWIM_EXIT_RADIUS)
                            continue;
                        float const progress = d - candToGoal;
                        float const deviation = std::abs(yaw) + std::abs(pitch);
                        float const score = progress - 0.01f * deviation;
                        if (score > bestScore)
                        {
                            bestScore = score;
                            chosen = cand;
                            found = true;
                        }
                    }
                }
            }

            if (!found)
            {
                return SearchSwim(start, goal, lineClear, inWater);
            }

            r.points.push_back(chosen);

            float const newToGoal = Dist3D(chosen, goal);
            if (newToGoal >= bestToGoal - 0.1f)
            {
                if (++noProgress > SWIM_MAX_NO_PROGRESS)
                {
                    return SearchSwim(start, goal, lineClear, inWater);
                }
            }
            else
            {
                noProgress = 0;
                bestToGoal = newToGoal;
            }
            cur = chosen;
        }

        return SearchSwim(start, goal, lineClear, inWater);
    }

    bool WaterBetween(Player* bot, G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        if (!bot)
            return false;
        Map* map = bot->GetMap();
        if (!map)
            return false;
        uint32 const phase = bot->GetPhaseMask();
        float const coll = bot->GetCollisionHeight();

        constexpr int SAMPLES = 10;
        for (int i = 0; i <= SAMPLES; ++i)
        {
            float const t = float(i) / float(SAMPLES);
            G3D::Vector3 const p(a.x + (b.x - a.x) * t,
                                 a.y + (b.y - a.y) * t,
                                 a.z + (b.z - a.z) * t);
            if (InWater(map, phase, p, coll))
                return true;
        }
        return false;
    }
}
