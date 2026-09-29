/*
 * mod-dungeon-clear — StayDeadAction.cpp
 */

#include "StayDeadAction.h"

#include "Map.h"
#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"

bool DungeonClearStayDeadAction::isUseful()
{
    // Dungeon/raid maps only. The stay-dead override exists for the wipe-recovery
    // case — a party corpse-run or rez inside an instance — so a bot dead out in
    // the open world keeps stock auto-release (a world bot left as a corpse just
    // lies there until someone happens across it). NOTE: this can't be gated on
    // an active DC run instead — a party death auto-disables the run
    // (DungeonClearPartyDiedTrigger), so by the time bots are corpses `enabled`
    // is already false and a run gate would never hold.
    Map const* map = bot ? bot->GetMap() : nullptr;
    if (!map || !map->IsDungeon())
        return AutoReleaseSpiritAction::isUseful();

    // Read live (not cached) so `.reload config` and per-run overrides both take
    // effect without a restart. The dead-state "auto release" trigger fires on
    // the throttled "often" cadence, so the per-call lookup is negligible.
    // ...unless nobody can raise it. With no rez class standing, an expired rez
    // budget or a wipe, a pinned corpse never comes back and the run hangs on it
    // (a guild run sat 4000s that way). Release and let the stock corpse run walk
    // back; the run holds meanwhile and resumes when the corpse is reached.
    if (DcSettings::GetBool(bot, "PreventBotRelease"))
        return DcRezRecovery::MayReleaseSpirit(bot) ? AutoReleaseSpiritAction::isUseful()
                                                    : false;  // never auto-release; bot stays a corpse until rezzed

    return AutoReleaseSpiritAction::isUseful();
}
