/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_SWIMPATHFINDER_H
#define _PLAYERBOT_SWIMPATHFINDER_H

#include <string>
#include <vector>
#include <functional>

#include "G3D/Vector3.h"

class Player;

// Swim leg pathfinder: a greedy 3D navigator through a water volume
// that needs NO navmesh and NO per-zone data.
//
// Why this exists: the mmap generator discards the floor under liquid and keeps
// only a flat water-surface navmesh sheet (TerrainBuilder "pick the higher
// one"), so there is no navmesh inside a submerged tunnel. A submerged area
// behind that tunnel (e.g. Blackfathom Deeps -> Lady Sarevess) is therefore a
// disconnected mesh island the normal route planner declares unreachable.
//
// Build() runs only when the navmesh planner has already given up AND water lies
// between the bot and the goal (see WaterBetween). It walks toward the goal in
// 3D; when a straight hop is blocked by geometry it probes a fan of yaw/pitch
// deflections and takes the clearest one that still makes progress and stays in
// water. When it cannot advance, a bounded 3D lattice search finds a detour
// around concave walls. Collision is
// tested with VMAP line-of-sight (liquid is transparent to VMAP, so only rock /
// WMO geometry blocks); "in water" is tested with the map's liquid status.
//
// The produced polyline carries SUBMERGED Z verbatim — callers must NOT pass it
// through UpdateAllowedPositionZ / NavmeshSnap / corridor centering (those are
// navmesh-bound and would yank the points back up to the surface sheet, which is
// the whole bug). Drive it with a raw 3D escort spline.
//
// The greedy tier handles open corridors cheaply; the search only runs when it
// fails to make progress, has no clear hop, or exhausts its step budget.
namespace SwimPathfinder
{
    using LineProbe = std::function<bool(G3D::Vector3 const&, G3D::Vector3 const&)>;
    using WaterProbe = std::function<bool(G3D::Vector3 const&)>;
    struct Result
    {
        bool ok{false};
        std::vector<G3D::Vector3> points;   // 3D polyline toward the goal, EXCLUDING the start point
        std::string failureReason;          // populated when ok == false
    };

    // 3D swim route from `start` to `goal`. Both are world coords on the
    // bot's map. On success `points` ends at (or within arrive-radius of) the
    // goal. Synchronous and map-thread only (touches VMAP + liquid).
    Result Build(Player* bot, G3D::Vector3 const& start, G3D::Vector3 const& goal);

    // The geometry-only route seam used by the live map adapter and route tests.
    Result BuildCore(G3D::Vector3 const& start, G3D::Vector3 const& goal,
                     LineProbe const& lineClear, WaterProbe const& inWater);

    // Cheap gate: is there any water on the straight line between a and b?
    // Sampled, so it can miss a thin sliver, but a submerged tunnel is never
    // thin along its length. Used to confirm a dead-end is a swim candidate
    // before paying for Build().
    bool WaterBetween(Player* bot, G3D::Vector3 const& a, G3D::Vector3 const& b);
}

#endif
