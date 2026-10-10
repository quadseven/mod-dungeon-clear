/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearActions.h"

#include <vector>

#include "Log.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "MoveSplineInitArgs.h"
#include "Player.h"
#include "Timer.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBreath.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBreathDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMovement.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearStateValues.h"
#include "Playerbots.h"

namespace
{
    // Points per climb window; the next window is issued when this one ends.
    constexpr size_t kWindowCap = 24;

    bool ClimbInFlight(Player* bot)
    {
        MotionMaster* mm = bot->GetMotionMaster();
        return mm && mm->GetCurrentMovementGeneratorType() == ESCORT_MOTION_TYPE &&
               bot->movespline && !bot->movespline->Finalized();
    }

    // A raw 3D escort spline, Z verbatim (no navmesh snap: there is no mesh
    // under the water). Deliberately NOT DcMovement::SplinePath, which refuses
    // while the run is paused: a paused bot under water still has to breathe.
    bool Climb(Player* bot, std::vector<DcBreath::Point> const& route)
    {
        if (route.size() < 2)
            return false;
        MotionMaster* mm = bot->GetMotionMaster();
        if (!mm)
            return false;
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        if (bot->IsNonMeleeSpellCast(true))
            bot->CastStop();
        Movement::PointsArray pts;
        pts.reserve(route.size());
        for (DcBreath::Point const& p : route)
            pts.emplace_back(p.x, p.y, p.z);
        mm->MoveSplinePath(&pts, FORCED_MOVEMENT_NONE);
        return ClimbInFlight(bot);
    }
}

bool DungeonClearSurfaceForBreathAction::Execute(Event /*event*/)
{
    if (!bot || !bot->IsAlive())
        return false;

    DcBreath::Model& m = context->GetValue<DcBreath::Model&>(DcKey::BreathState)->Get();
    bool const under = DcBreathGame::IsUnderWater(bot);
    DcBreath::Point const here = DcBreathGame::Here(bot);

    if (!m.surfacing)
    {
        m.surfacing = true;
        m.routeIssuedMs = 0;
        // This bot's own swim leg (the tank's) would read the climb as a
        // wedge and stall the run underwater, which is how the party drowned.
        // Drop it; Advance rebuilds it from the surface.
        context->GetValue<DungeonClearSwimState&>(DcKey::SwimState)->Get().Reset();
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] surfacing for breath: {}s of {}s of air left, {:.0f}yd back to "
                 "the last breath, at ({:.1f},{:.1f},{:.1f})",
                 bot->GetName(), m.remainingMs / 1000, m.maxMs / 1000,
                 DcBreath::TrailYards(m, here), here.x, here.y, here.z);
    }

    if (!under)
    {
        // Head above water: tread water until the trigger says the bar is
        // full enough to go back down.
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        return true;
    }

    // Our own climb is under way: leave it alone. A spline that was already
    // in flight before we took over (a swim leg heading deeper) is not ours.
    if (m.routeIssuedMs != 0 && ClimbInFlight(bot))
        return true;

    std::vector<DcBreath::Point> route;
    DcBreath::Point top;
    char const* how = "back along its swim";
    if (DcBreathGame::OpenWaterAbove(bot, top))
    {
        route = { here, top };
        how = "straight up";
    }
    else
    {
        route = DcBreath::RetraceWindow(m, here, kWindowCap);
        if (route.size() < 2 && !m.trail.empty())
            route = { here, m.trail.front() };
    }

    if (!Climb(bot, route))
    {
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] surfacing for breath: no way up from ({:.1f},{:.1f},{:.1f}) "
                  "({} points)",
                  bot->GetName(), here.x, here.y, here.z, route.size());
        return true;
    }
    if (m.routeIssuedMs == 0)
        LOG_INFO("playerbots.dungeonclear", "[DC:{}] surfacing for breath: swimming {} ({} points)",
                 bot->GetName(), how, route.size());
    m.routeIssuedMs = getMSTime();
    if (m.routeIssuedMs == 0)
        m.routeIssuedMs = 1;
    return true;
}
