/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCDEFIASGUNPOWDER_H
#define _PLAYERBOT_DCDEFIASGUNPOWDER_H

#include <cstddef>
#include <cstdint>

// THE DEADMINES CANNON, PLAYED THE WAY A PLAYER PLAYS IT.
//
// The Iron Clad Door (GO 16397) opens when the Defias Cannon (GO 16398) is hit
// by spell 6250, and 6250 is the on-use of the Defias Gunpowder (item 5397): an
// OPEN_LOCK against the cannon's ITEM lock, so only the item-use cast lands. A
// player gets the gunpowder from the Defias Gunpowder chest (GO 17155, chest
// type, lock 57, loot template 2882 which drops 5397 every time) sitting on the
// ledge about 42yd up from the cannon. The hook used to AddItem the gunpowder
// instead. Now a member walks to the chest, opens it with an Opening spell it
// knows (the core's OPEN_LOCK -> Player::SendLoot), takes the gunpowder out of
// the loot window (CMSG_AUTOSTORE_LOOT_ITEM), then walks back and uses it.
//
// This kernel answers, from a plain snapshot, what the step does this tick.
// There is deliberately no answer that creates the item: when the chest is
// gone or empty, or nobody can open it, the step says why and keeps waiting,
// and the event's step timeout ends it.
//
// WHY OPENING THE CHEST WAS NOT ENOUGH. The Opening cast (spell 3365) has a 1s
// cast time, so its loot window opens inside the core's spell update, never
// inside this hook. The opener is a playerbot, and its own "loot response"
// handler (playerbots StoreLootAction) answers that window on its next AI tick,
// before this hook ticks again. That handler skips any item that does not stack
// (item 5397 does not) while the bot's "bag space" reads over 80 percent used,
// then releases the window. The gunpowder stays in the opened chest, the hook
// sees no loot window, and the step waits out its timeouts while the guild run
// sits at 3 bosses until its 2 hour cap. On the realm (bags read after the
// fact), the tanks of the runs that stalled at the cannon carried 91 to 100
// percent full bags, and the tanks of runs that passed it were under half
// full. So the step now (a) sends a member whose bags can hold the gunpowder,
// and (b) when the chest is already opened with the gunpowder still in it,
// opens it again with the cast landing at once and takes the gunpowder from
// the window in the same tick, before any other handler can close it.
//
// Engine-free and header-only (no Player / GameObject), so it is gtested in
// isolation; ObjectiveHookRegistry's FireDefiasCannon is the glue.
namespace DcDefiasGunpowder
{
    constexpr std::uint32_t ITEM_GUNPOWDER    = 5397;   // on-use spell 6250 (OPEN_LOCK)
    constexpr std::uint32_t GO_GUNPOWDER      = 17155;  // the chest a player loots it from
    constexpr std::uint32_t GO_CANNON         = 16398;
    constexpr std::uint32_t GO_IRON_CLAD_DOOR = 16397;

    // The event area, scanned from the bot. The chest stands ~42yd from the
    // cannon (spawn -106.4,-617.3,13.8 vs -107.6,-659.7,7.2), so 60yd reaches it
    // from the cannon without reaching another room's objects.
    constexpr float EVENT_AREA_RADIUS = 60.0f;

    // Reach for the two interactions. The chest's open ends in Player::SendLoot,
    // which range-checks the GO's interact box (the Mechanar cache uses the same
    // 4yd). The cannon gets the event's MoveTo radius, the spot the item use has
    // always fired from.
    constexpr float CHEST_REACH  = 4.0f;
    constexpr float CANNON_REACH = 5.0f;

    // Who in the party holds item 5397. Self beats a party member: the bot that
    // runs the hook uses its own gunpowder rather than wait on someone else's.
    enum class Carrier : std::uint8_t
    {
        None,        // nobody: the chest has to be looted
        Self,        // the bot running the hook
        PartyBot,    // another bot in the party: it is walked to the cannon
        PartyHuman,  // a player (or a self-bot): they use it themselves
    };

    // The nearest GO 17155 in the event area.
    enum class Chest : std::uint8_t
    {
        Missing,   // none spawned in range (looted and despawned, or not there)
        Lootable,  // spawned, never opened, still holding the gunpowder
        Spent,     // spawned but opened and emptied of it by somebody
        Opened,    // opened (its lock already beaten), the loot window closed,
                   // and the gunpowder still in it
    };

    struct Inputs
    {
        bool    doorOpen = false;        // the Iron Clad Door has left GO_STATE_READY
        bool    cannonFound = false;     // GO 16398 within EVENT_AREA_RADIUS
        Carrier carrier = Carrier::None;
        // The member who acts this tick: the carrier when there is one, else the
        // opener (PickOpener: the bot running the hook unless its bags cannot
        // hold the gunpowder). The chest fields below are the opener's.
        bool    actorCasting = false;    // a cast in flight (the 2s gunpowder use, the open)
        float   actorToCannon = 0.0f;
        Chest   chest = Chest::Missing;
        float   botToChest = 0.0f;          // the opener's distance to the chest
        bool    lootWindowOnChest = false;  // the opener's loot window is the chest's
        bool    hasOpeningSpell = false;    // the opener knows a spell that opens lock 57
        bool    openerHasRoom = true;       // some party bot's bags can hold item 5397
        bool    openingAttempted = false;   // ReopenAttempted(): a reopen this attempt
                                            // still left the gunpowder in the chest
    };

    enum class Action : std::uint8_t
    {
        Done,            // the door is open: the cannon has fired
        WaitForCast,     // let the cast in flight finish
        WalkToCannon,    // the carrier walks to the cannon
        UseOnCannon,     // the carrier uses item 5397 on GO 16398
        WaitForPlayer,   // a player carries it; the cannon is theirs to fire
        TakeFromChest,   // the chest's loot window is up: take item 5397 out of it
        WalkToChest,     // the bot walks to GO 17155
        OpenChest,       // the bot casts its Opening spell on GO 17155
        // Reports. Each holds the step (Running) and says why; none of them
        // grants anything, and the event's step timeout bounds the wait.
        NoCannon,        // GO 16398 is not in range yet
        NoChest,         // no GO 17155 in the event area
        ChestSpent,      // GO 17155 is there, but its gunpowder is gone
        NoOpeningSpell,  // the bot knows no spell that opens the chest's lock
        OpeningNoLoot,   // a reopen left the gunpowder in the chest; do not cast it again
        // Appended, so the report codes of the actions above do not move.
        ReopenChest,     // the chest is Opened: open it again, the cast landing this
                         // tick, and take item 5397 from the window in the same tick
        NoBagRoom,       // no party bot's bags can hold item 5397 (a report)
    };

    struct Plan
    {
        Action        action = Action::Done;
        std::uint32_t goEntry = 0;  // the gameobject the action is aimed at, if any
    };

    inline Plan Decide(Inputs const& in)
    {
        // The door is the success latch, and the reason this is idempotent: a
        // second fire would summon two more Squallshapers.
        if (in.doorOpen)
            return {Action::Done, 0};

        if (in.carrier == Carrier::PartyHuman)
            return {Action::WaitForPlayer, GO_CANNON};

        if (in.carrier != Carrier::None)
        {
            if (!in.cannonFound)
                return {Action::NoCannon, GO_CANNON};
            if (in.actorCasting)
                return {Action::WaitForCast, GO_CANNON};
            if (in.actorToCannon > CANNON_REACH)
                return {Action::WalkToCannon, GO_CANNON};
            return {Action::UseOnCannon, GO_CANNON};
        }

        // Nobody carries it: the bot loots the chest. A loot window already up
        // on the chest comes first, whatever state the chest reads in by now.
        if (in.lootWindowOnChest)
            return {Action::TakeFromChest, GO_GUNPOWDER};
        if (in.actorCasting)
            return {Action::WaitForCast, GO_GUNPOWDER};
        if (in.openingAttempted)
            return {Action::OpeningNoLoot, GO_GUNPOWDER};
        if (in.chest == Chest::Missing)
            return {Action::NoChest, GO_GUNPOWDER};
        if (in.chest == Chest::Spent)
            return {Action::ChestSpent, GO_GUNPOWDER};
        if (!in.openerHasRoom)
            return {Action::NoBagRoom, GO_GUNPOWDER};
        if (in.botToChest > CHEST_REACH)
            return {Action::WalkToChest, GO_GUNPOWDER};
        if (!in.hasOpeningSpell)
            return {Action::NoOpeningSpell, GO_GUNPOWDER};
        // An Opened chest has had its window closed on the gunpowder (see the
        // header comment): a second normal Opening would lose it the same way.
        if (in.chest == Chest::Opened)
            return {Action::ReopenChest, GO_GUNPOWDER};
        return {Action::OpenChest, GO_GUNPOWDER};
    }

    // WHO OPENS THE CHEST. One candidate per party bot in the event area, the
    // bot running the hook first.
    struct Looter
    {
        bool         canStore = false;  // the core would store one item 5397 in its bags
        std::uint8_t bagSpace = 100;    // playerbots "bag space": percent of bag slots used
    };

    // The index of the member who opens the chest, or -1 when nobody's bags can
    // hold the gunpowder. The bot running the hook goes itself whenever its bags
    // can hold it: a reopen (ReopenChest) takes the gunpowder whatever its bag
    // space reads. Otherwise the member with the emptiest bags goes (ties to the
    // earlier member), since one at or under 80 percent loots it on the first
    // Opening through its own loot handler.
    inline int PickOpener(Looter const* looters, std::size_t n)
    {
        if (!looters || n == 0)
            return -1;
        if (looters[0].canStore)
            return 0;
        int best = -1;
        for (std::size_t i = 1; i < n; ++i)
            if (looters[i].canStore && (best < 0 || looters[i].bagSpace < looters[best].bagSpace))
                best = static_cast<int>(i);
        return best;
    }

    // The reports: the step cannot move forward by itself and says why.
    inline bool IsReport(Action a)
    {
        return a == Action::NoCannon || a == Action::NoChest || a == Action::ChestSpent ||
               a == Action::NoOpeningSpell || a == Action::OpeningNoLoot ||
               a == Action::WaitForPlayer || a == Action::NoBagRoom;
    }

    // The step's "why it is still Running" line is logged once per change, not
    // once per tick: a live realm held this step Running for 21 minutes with
    // nothing to say which branch it was in. The hook remembers the last code
    // it logged (DcRunState::eventHookReason; 0 = nothing said yet, and the
    // event executor clears it at every new attempt at the step) and logs when
    // ReportCode answers something else.
    //
    // The code is 1 + the Action, with three flags for the casts the step starts.
    // Each flag has its own bit, so no flag can read as another.
    constexpr std::uint8_t REPORT_ACTION_MASK = 0x1F;
    constexpr std::uint8_t REPORT_OPENING_ATTEMPTED = 0x20;  // ReopenChest cast this attempt
    constexpr std::uint8_t REPORT_CAST_REFUSED = 0x40;  // UseOnCannon / OpenChest /
                                                        // ReopenChest started no cast
    constexpr std::uint8_t REPORT_CAST_AGAIN   = 0x80;  // UseOnCannon again: the last use's
                                                        // cast ended with the door still shut
    static_assert(static_cast<std::uint8_t>(Action::NoBagRoom) + 1 <= REPORT_ACTION_MASK,
                  "every action's report code must fit under REPORT_ACTION_MASK");

    inline std::uint8_t ReportCode(Action a, bool castRefused, std::uint8_t last)
    {
        std::uint8_t const code = static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) + 1);
        std::uint8_t const lastAction = last & REPORT_ACTION_MASK;
        bool const lastCastStarted =
            !(last & REPORT_CAST_REFUSED) &&
            (lastAction == static_cast<std::uint8_t>(static_cast<std::uint8_t>(Action::UseOnCannon) + 1) ||
             lastAction == static_cast<std::uint8_t>(static_cast<std::uint8_t>(Action::OpenChest) + 1) ||
             lastAction == static_cast<std::uint8_t>(static_cast<std::uint8_t>(Action::ReopenChest) + 1));
        bool const casts =
            a == Action::UseOnCannon || a == Action::OpenChest || a == Action::ReopenChest;

        // The cast this step just started is that action's own wait, not news.
        if (a == Action::WaitForCast && lastCastStarted)
            return last;
        if (casts && castRefused)
            return code | REPORT_CAST_REFUSED;
        if (a == Action::UseOnCannon && lastCastStarted && lastAction == code)
            return code | REPORT_CAST_AGAIN;
        if (a == Action::ReopenChest)
            return code | REPORT_OPENING_ATTEMPTED;
        if (a == Action::OpeningNoLoot && (last & REPORT_OPENING_ATTEMPTED))
            return code | REPORT_OPENING_ATTEMPTED;
        return code;
    }

    // The latch, read from the live stored code (DcRunState::eventHookReason)
    // each tick: a reopen was cast this attempt and the gunpowder is still not
    // in anyone's bags. ReopenChest stores the flag; OpeningNoLoot keeps it, so
    // the latch survives its own report. Only a reopen latches: a first
    // Opening whose window was closed on the gunpowder leaves the chest Opened,
    // and the reopen is what gets it out.
    inline bool ReopenAttempted(std::uint8_t last)
    {
        std::uint8_t const action = last & REPORT_ACTION_MASK;
        std::uint8_t const reopen =
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(Action::ReopenChest) + 1);
        std::uint8_t const noLoot =
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(Action::OpeningNoLoot) + 1);
        return !(last & REPORT_CAST_REFUSED) && (last & REPORT_OPENING_ATTEMPTED) &&
               (action == reopen || action == noLoot);
    }

    inline char const* Describe(Action a)
    {
        switch (a)
        {
            case Action::Done:           return "the Iron Clad Door is open";
            case Action::WaitForCast:    return "waiting for the cast in flight";
            case Action::WalkToCannon:   return "walking the gunpowder to the Defias Cannon";
            case Action::UseOnCannon:    return "using the Defias Gunpowder on the Defias Cannon";
            case Action::WaitForPlayer:  return "a player carries the Defias Gunpowder; waiting for them to use it on the cannon";
            case Action::TakeFromChest:  return "taking the Defias Gunpowder out of the chest";
            case Action::WalkToChest:    return "walking to the Defias Gunpowder chest";
            case Action::OpenChest:      return "opening the Defias Gunpowder chest";
            case Action::NoCannon:       return "the Defias Cannon is not in range";
            case Action::NoChest:        return "no Defias Gunpowder chest in the event area (already looted, or not spawned)";
            case Action::ChestSpent:     return "the Defias Gunpowder chest has been emptied of its gunpowder";
            case Action::NoOpeningSpell: return "no known spell opens the Defias Gunpowder chest's lock";
            case Action::OpeningNoLoot:  return "opening the Defias Gunpowder chest again left the gunpowder in it";
            case Action::ReopenChest:    return "opening the Defias Gunpowder chest again and taking the gunpowder from it";
            case Action::NoBagRoom:      return "no party bot has room in its bags for the Defias Gunpowder";
        }
        return "?";
    }
}

#endif  // _PLAYERBOT_DCDEFIASGUNPOWDER_H
