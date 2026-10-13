/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCDEFIASGUNPOWDER_H
#define _PLAYERBOT_DCDEFIASGUNPOWDER_H

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
        Lootable,  // spawned and still holding the gunpowder
        Spent,     // spawned but opened and emptied of it by somebody
    };

    struct Inputs
    {
        bool    doorOpen = false;        // the Iron Clad Door has left GO_STATE_READY
        bool    cannonFound = false;     // GO 16398 within EVENT_AREA_RADIUS
        Carrier carrier = Carrier::None;
        // The member who acts this tick: the carrier when there is one, else the
        // bot running the hook (it loots the chest).
        bool    actorCasting = false;    // a cast in flight (the 2s gunpowder use, the open)
        float   actorToCannon = 0.0f;
        Chest   chest = Chest::Missing;
        float   botToChest = 0.0f;
        bool    lootWindowOnChest = false;  // the bot's loot window is the chest's
        bool    hasOpeningSpell = false;    // the bot knows a spell that opens lock 57
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
        if (in.chest == Chest::Missing)
            return {Action::NoChest, GO_GUNPOWDER};
        if (in.chest == Chest::Spent)
            return {Action::ChestSpent, GO_GUNPOWDER};
        if (in.botToChest > CHEST_REACH)
            return {Action::WalkToChest, GO_GUNPOWDER};
        if (!in.hasOpeningSpell)
            return {Action::NoOpeningSpell, GO_GUNPOWDER};
        return {Action::OpenChest, GO_GUNPOWDER};
    }

    // The reports: the step cannot move forward by itself and says why.
    inline bool IsReport(Action a)
    {
        return a == Action::NoCannon || a == Action::NoChest || a == Action::ChestSpent ||
               a == Action::NoOpeningSpell || a == Action::WaitForPlayer;
    }

    // The step's "why it is still Running" line is logged once per change, not
    // once per tick: a live realm held this step Running for 21 minutes with
    // nothing to say which branch it was in. The hook remembers the last code
    // it logged (DcRunState::eventHookReason; 0 = nothing said yet, and the
    // event executor clears it at every new attempt at the step) and logs when
    // ReportCode answers something else.
    //
    // The code is 1 + the Action, with two flags for the casts the step starts:
    constexpr std::uint8_t REPORT_ACTION_MASK = 0x3F;
    constexpr std::uint8_t REPORT_CAST_REFUSED = 0x40;  // UseOnCannon / OpenChest started no cast
    constexpr std::uint8_t REPORT_CAST_AGAIN   = 0x80;  // UseOnCannon again: the last use's
                                                        // cast ended with the door still shut

    inline std::uint8_t ReportCode(Action a, bool castRefused, std::uint8_t last)
    {
        std::uint8_t const code = static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) + 1);
        std::uint8_t const lastAction = last & REPORT_ACTION_MASK;
        bool const lastCastStarted =
            !(last & REPORT_CAST_REFUSED) &&
            (lastAction == static_cast<std::uint8_t>(static_cast<std::uint8_t>(Action::UseOnCannon) + 1) ||
             lastAction == static_cast<std::uint8_t>(static_cast<std::uint8_t>(Action::OpenChest) + 1));
        bool const casts = a == Action::UseOnCannon || a == Action::OpenChest;

        // The cast this step just started is that action's own wait, not news.
        if (a == Action::WaitForCast && lastCastStarted)
            return last;
        if (casts && castRefused)
            return code | REPORT_CAST_REFUSED;
        if (a == Action::UseOnCannon && lastCastStarted && lastAction == code)
            return code | REPORT_CAST_AGAIN;
        return code;
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
        }
        return "?";
    }
}

#endif  // _PLAYERBOT_DCDEFIASGUNPOWDER_H
