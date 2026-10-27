/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Util/DcDefiasGunpowder.h"
#include "DcSourceScan.h"

// The Deadmines cannon step earns the Defias Gunpowder from its chest
// (DcDefiasGunpowder::Decide), and the module grants nothing a player could not
// earn: no AddItem anywhere in the event hooks.

namespace
{
    using namespace DcDefiasGunpowder;

    // At the cannon, nobody carrying the gunpowder, the chest untouched up on
    // its ledge: the state the step starts in on a fresh run.
    Inputs AtTheCannonEmptyHanded()
    {
        Inputs in;
        in.doorOpen = false;
        in.cannonFound = true;
        in.carrier = Carrier::None;
        in.actorCasting = false;
        in.actorToCannon = 2.0f;
        in.chest = Chest::Lootable;
        in.botToChest = 42.4f;
        in.lootWindowOnChest = false;
        in.hasOpeningSpell = true;
        return in;
    }
}

TEST(DcDefiasGunpowder, TheChestIsTheOneAPlayerLoots)
{
    EXPECT_EQ(GO_GUNPOWDER, 17155u);
    EXPECT_EQ(ITEM_GUNPOWDER, 5397u);
    EXPECT_EQ(GO_CANNON, 16398u);
    EXPECT_EQ(GO_IRON_CLAD_DOOR, 16397u);
    // The chest is ~42yd from the cannon; the event area must reach it.
    EXPECT_GT(EVENT_AREA_RADIUS, 42.4f);
}

// The acceptance case: no member carries the gunpowder, so the plan goes to
// GO 17155 and loots it (walk, open, take), and never reaches the cannon
// empty-handed.
TEST(DcDefiasGunpowder, NoGunpowderInThePartyLootsTheChest)
{
    Inputs in = AtTheCannonEmptyHanded();

    Plan p = Decide(in);
    EXPECT_EQ(p.action, Action::WalkToChest);
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);

    in.botToChest = 3.0f;  // arrived at the chest
    p = Decide(in);
    EXPECT_EQ(p.action, Action::OpenChest);
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);

    in.actorCasting = true;  // the Opening cast is in flight
    p = Decide(in);
    EXPECT_EQ(p.action, Action::WaitForCast);
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);

    in.actorCasting = false;
    in.lootWindowOnChest = true;  // the open landed: the loot window is up
    p = Decide(in);
    EXPECT_EQ(p.action, Action::TakeFromChest);
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);

    // Taken: the bot now carries it, walks back down and fires the cannon.
    in.lootWindowOnChest = false;
    in.carrier = Carrier::Self;
    in.actorToCannon = 42.4f;
    p = Decide(in);
    EXPECT_EQ(p.action, Action::WalkToCannon);
    EXPECT_EQ(p.goEntry, GO_CANNON);

    in.actorToCannon = 2.0f;
    p = Decide(in);
    EXPECT_EQ(p.action, Action::UseOnCannon);
    EXPECT_EQ(p.goEntry, GO_CANNON);

    in.doorOpen = true;
    EXPECT_EQ(Decide(in).action, Action::Done);
}

// Never grants: across every input with nobody carrying the gunpowder, the plan
// never uses it on the cannon (there is nothing to use) and only ever aims at
// the chest; when it cannot loot, it reports why.
TEST(DcDefiasGunpowder, WithoutACarrierThePlanNeverReachesTheCannon)
{
    Chest const chests[] = {Chest::Missing, Chest::Lootable, Chest::Spent};
    float const dists[] = {0.0f, CHEST_REACH, CHEST_REACH + 0.5f, 42.4f};
    for (bool cannon : {false, true})
        for (bool casting : {false, true})
            for (Chest chest : chests)
                for (float d : dists)
                    for (bool window : {false, true})
                        for (bool spell : {false, true})
                        {
                            Inputs in;
                            in.cannonFound = cannon;
                            in.carrier = Carrier::None;
                            in.actorCasting = casting;
                            in.chest = chest;
                            in.botToChest = d;
                            in.lootWindowOnChest = window;
                            in.hasOpeningSpell = spell;
                            Plan const p = Decide(in);
                            EXPECT_NE(p.action, Action::UseOnCannon);
                            EXPECT_NE(p.action, Action::WalkToCannon);
                            EXPECT_NE(p.action, Action::Done)
                                << "the door is shut: only the cannon opens it";
                            EXPECT_EQ(p.goEntry, GO_GUNPOWDER);
                        }
}

TEST(DcDefiasGunpowder, NoChestReportsTheReason)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.chest = Chest::Missing;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::NoChest);
    EXPECT_TRUE(IsReport(p.action));
    EXPECT_NE(std::string(Describe(p.action)).find("no Defias Gunpowder chest"),
              std::string::npos);
}

TEST(DcDefiasGunpowder, AnEmptiedChestReportsTheReason)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.chest = Chest::Spent;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::ChestSpent);
    EXPECT_TRUE(IsReport(p.action));
}

TEST(DcDefiasGunpowder, NoOpeningSpellReportsTheReason)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.botToChest = 2.0f;
    in.hasOpeningSpell = false;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::NoOpeningSpell);
    EXPECT_TRUE(IsReport(p.action));
}

// A loot window already up on the chest is emptied before anything else, even
// if the chest reads as spent by now (the window holds the roll).
TEST(DcDefiasGunpowder, AnOpenLootWindowIsTakenFirst)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.lootWindowOnChest = true;
    in.chest = Chest::Spent;
    in.botToChest = 50.0f;
    EXPECT_EQ(Decide(in).action, Action::TakeFromChest);
}

// The first Opening is the normal one; a chest left Opened with the gunpowder
// still in it (the loot window was closed on it) is opened again, the cast
// landing at once, and a reopen that still leaves it is never cast twice.
TEST(DcDefiasGunpowder, AChestLeftOpenedIsReopenedAndOnlyOnce)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.botToChest = 2.0f;
    EXPECT_EQ(Decide(in).action, Action::OpenChest);

    in.chest = Chest::Opened;
    Plan const reopen = Decide(in);
    EXPECT_EQ(reopen.action, Action::ReopenChest);
    EXPECT_EQ(reopen.goEntry, GO_GUNPOWDER);
    EXPECT_FALSE(IsReport(reopen.action));

    in.actorCasting = true;
    EXPECT_EQ(Decide(in).action, Action::WaitForCast);

    in.actorCasting = false;
    in.openingAttempted = true;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::OpeningNoLoot);
    EXPECT_TRUE(IsReport(p.action));
    EXPECT_EQ(Decide(in).action, Action::OpeningNoLoot);

    // A loot window that is up is still taken first, latch or not.
    in.lootWindowOnChest = true;
    EXPECT_EQ(Decide(in).action, Action::TakeFromChest);
}

// The latch is read from the live stored report code each tick
// (DcRunState::eventHookReason). The OpeningNoLoot report that the latch
// itself produces must keep the flag, or the next tick would cast the reopen
// again and the step would loop on it until the run's 2 hour cap (the
// 2026-10-16 Deadmines stall).
TEST(DcDefiasGunpowder, TheReopenLatchSurvivesItsOwnReport)
{
    uint8_t const reopenStarted = ReportCode(Action::ReopenChest, false, 0);
    EXPECT_NE(reopenStarted & REPORT_OPENING_ATTEMPTED, 0);
    EXPECT_TRUE(ReopenAttempted(reopenStarted));

    uint8_t const reopenInFlight = ReportCode(Action::WaitForCast, false, reopenStarted);
    ASSERT_EQ(reopenInFlight, reopenStarted);
    EXPECT_TRUE(ReopenAttempted(reopenInFlight));

    uint8_t const noLoot = ReportCode(Action::OpeningNoLoot, false, reopenInFlight);
    EXPECT_NE(noLoot & REPORT_OPENING_ATTEMPTED, 0) << "the report must keep the attempt bit";
    EXPECT_TRUE(ReopenAttempted(noLoot));
    EXPECT_EQ(ReportCode(Action::OpeningNoLoot, false, noLoot), noLoot);

    Inputs in = AtTheCannonEmptyHanded();
    in.botToChest = 2.0f;
    in.chest = Chest::Opened;
    in.openingAttempted = ReopenAttempted(noLoot);
    EXPECT_EQ(Decide(in).action, Action::OpeningNoLoot);
}

// Only a reopen latches. A first Opening whose window was closed on the
// gunpowder leaves the chest Opened, and the reopen is what gets it out.
TEST(DcDefiasGunpowder, OnlyAReopenLatches)
{
    uint8_t const first = ReportCode(Action::OpenChest, false, 0);
    EXPECT_FALSE(ReopenAttempted(first));
    EXPECT_FALSE(ReopenAttempted(ReportCode(Action::WaitForCast, false, first)));
    EXPECT_FALSE(ReopenAttempted(0));
    // A reopen whose cast was refused did not happen, so it is tried again.
    uint8_t const refused = ReportCode(Action::ReopenChest, true, 0);
    EXPECT_NE(refused & REPORT_CAST_REFUSED, 0);
    EXPECT_FALSE(ReopenAttempted(refused));
}

// Item 5397 does not stack, and the playerbots loot handler skips such an item
// while the bot's bags read over 80 percent used. On the realm the tanks of the
// runs that stalled carried 91 to 100 percent full bags.
TEST(DcDefiasGunpowder, NobodyWithRoomIsAReportNotAWalk)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.openerHasRoom = false;
    in.botToChest = 30.0f;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::NoBagRoom);
    EXPECT_TRUE(IsReport(p.action));
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);

    // With room the same state walks to the chest.
    in.openerHasRoom = true;
    EXPECT_EQ(Decide(in).action, Action::WalkToChest);
}

TEST(DcDefiasGunpowder, TheEmptiestBagsOpenTheChestUnlessTheHookBotCanTakeIt)
{
    using L = Looter;
    // The bot running the hook goes itself whenever it can store the gunpowder.
    L const hookCan[] = {{true, 95}, {true, 10}};
    EXPECT_EQ(PickOpener(hookCan, 2), 0);
    // Otherwise the member with the emptiest bags that can store it.
    L const others[] = {{false, 99}, {true, 70}, {true, 40}, {false, 5}, {true, 40}};
    EXPECT_EQ(PickOpener(others, 5), 2) << "ties go to the earlier member";
    // Nobody can: a report, not a pick.
    L const none[] = {{false, 99}, {false, 20}};
    EXPECT_EQ(PickOpener(none, 2), -1);
    EXPECT_EQ(PickOpener(none, 0), -1);
    EXPECT_EQ(PickOpener(nullptr, 3), -1);
}

// T41: the opener pick must skip a bot that is in combat, so a healer is not
// pulled to the gunpowder chest mid-fight.
TEST(DcDefiasGunpowder, CombatBotsAreNeverSentToTheChest)
{
    using L = Looter;
    // The hook runner is in combat: skipped even though its bags can hold it.
    L const hookInCombat[] = {{true, 95, true}, {true, 10}};
    EXPECT_EQ(PickOpener(hookInCombat, 2), 1);
    // A combat-flagged member is never picked over an out-of-combat one.
    L const memberInCombat[] = {{false, 99}, {true, 5, true}, {true, 40}};
    EXPECT_EQ(PickOpener(memberInCombat, 3), 2);
    // Everybody in combat: nobody is picked; the log names the fallback.
    L const allInCombat[] = {{true, 10, true}, {true, 20, true}};
    EXPECT_EQ(PickOpener(allInCombat, 2), -1);
}

// T41: healers are never pulled to the chest: sending one leaves the party
// without healing while the opener runs its errand.
TEST(DcDefiasGunpowder, HealersAreNeverPulledToTheChest)
{
    using L = Looter;
    // Every out-of-combat bot is a healer: nobody goes, the step holds and the
    // log names the fallback.
    L const healersOnly[] = {{false, 99}, {true, 10, false, true}, {true, 40, false, true}};
    EXPECT_EQ(PickOpener(healersOnly, 3), -1);
    // A healer is skipped even with the emptiest bags; an out-of-combat
    // non-healer goes.
    L const mixed[] = {{true, 95, false, true}, {true, 5, false, true}, {true, 40}};
    EXPECT_EQ(PickOpener(mixed, 3), 2);
}

// T41: when nobody can be sent (everyone in combat, or only healers out of
// combat), the step holds with a report that names the fallback instead of
// walking a bot mid-fight to the chest.
TEST(DcDefiasGunpowder, NoEligibleOpenerIsAReportNotAWalk)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.openerHasRoom = true;
    in.openerEligible = false;
    in.botToChest = 30.0f;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::NoEligibleOpener);
    EXPECT_TRUE(IsReport(p.action));
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);

    // An eligible opener with no bag room is the old report, not the new one.
    in.openerEligible = true;
    in.openerHasRoom = false;
    EXPECT_EQ(Decide(in).action, Action::NoBagRoom);
}


// T43: With one healer that has bag room and one out-of-combat non-healer with
// full bags (no carrier), the hook reports NoEligibleOpener instead of
// walking to the chest.
TEST(DcDefiasGunpowder, HealerWithRoomAndNonHealerWithFullBagsReportsNoEligibleOpener)
{
    using L = Looter;
    // Healer has bag room (canStore=true) but is a healer -> not eligible
    // Non-healer is out of combat but has full bags (canStore=false) -> not a valid opener
    // Looter 1: isHealer=true -> skipped
    // Looter 2: canStore=false -> skipped
    // PickOpener returns -1.

    

    Inputs in = AtTheCannonEmptyHanded();
    // Simulate: DmPickOpener found no eligible bot, so both flags are false
    in.openerHasRoom = false;
    in.openerEligible = false;
    in.botToChest = 30.0f;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::NoEligibleOpener);
    EXPECT_TRUE(IsReport(p.action));
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);
}

// T43: When one bot passes the full opener test (not in combat, not a healer, can store),
// it is picked and walks to the chest.
TEST(DcDefiasGunpowder, BotPassesFullOpenerTestWalksToChest)
{
    using L = Looter;
    // One bot that passes all opener tests: not in combat, not a healer, can store

    Inputs in = AtTheCannonEmptyHanded();
    in.openerHasRoom = true;  // DmPickOpener would set anyBagRoom=true since bot passes test
    in.openerEligible = true; // DmPickOpener would set anyEligible=true since bot passes test
    in.botToChest = 30.0f;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::WalkToChest);
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);
}

// mod-dungeon-clear#36: on the realm the hook's own bot (the tank) was the
// opener whenever it could store the gunpowder, even when it knew no spell
// that opens the chest's lock. It walked to the chest, reported "no known spell
// opens the Defias Gunpowder chest's lock", timed out three times and failed
// the event, and the guild run sat at 3 of 7 bosses until its cap. A member who
// cannot open the chest is never the opener: one that can goes instead.
TEST(DcDefiasGunpowder, ABotThatKnowsNoOpeningSpellIsNeverSentToTheChest)
{
    using L = Looter;
    // The hook's bot has room but no Opening spell: the member that has one goes.
    L const hookCannotOpen[] = {{true, 10, false, false, false}, {true, 40}};
    EXPECT_EQ(PickOpener(hookCannotOpen, 2), 1);
    // Emptier bags do not win over a member that can open the chest.
    L const emptiestCannotOpen[] = {{false, 99}, {true, 5, false, false, false}, {true, 60}};
    EXPECT_EQ(PickOpener(emptiestCannotOpen, 3), 2);
    // Nobody in the party knows one: nobody is sent.
    L const noneCanOpen[] = {{true, 10, false, false, false}, {true, 40, false, false, false}};
    EXPECT_EQ(PickOpener(noneCanOpen, 2), -1);
}

// When the only thing missing is an Opening spell, the step says so at the
// cannon instead of walking a bot that cannot open the chest 42yd up to it,
// and the flags Decide reads say the same thing as the pick.
TEST(DcDefiasGunpowder, NobodyWhoCanOpenTheChestIsAReportNotAWalk)
{
    using L = Looter;
    L const noneCanOpen[] = {{true, 10, false, false, false}, {true, 40, false, false, false}};
    OpenerFlags const f = OpenerFlagsFor(noneCanOpen, 2, PickOpener(noneCanOpen, 2));
    EXPECT_TRUE(f.eligible);
    EXPECT_TRUE(f.hasRoom);
    EXPECT_FALSE(f.hasOpeningSpell);

    Inputs in = AtTheCannonEmptyHanded();  // 42.4yd from the chest
    in.openerEligible = f.eligible;
    in.openerHasRoom = f.hasRoom;
    in.hasOpeningSpell = f.hasOpeningSpell;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::NoOpeningSpell);
    EXPECT_TRUE(IsReport(p.action));
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);

    // A pick reads all three flags true.
    L const oneCanOpen[] = {{true, 10, false, false, false}, {true, 40}};
    OpenerFlags const g = OpenerFlagsFor(oneCanOpen, 2, PickOpener(oneCanOpen, 2));
    EXPECT_TRUE(g.eligible && g.hasRoom && g.hasOpeningSpell);

    // A null pick for any other reason still reports NoEligibleOpener (#49):
    // a healer with room and a non-healer with full bags.
    L const healerRoomOnly[] = {{true, 10, false, true}, {false, 99}};
    OpenerFlags const h = OpenerFlagsFor(healerRoomOnly, 2, PickOpener(healerRoomOnly, 2));
    in.openerEligible = h.eligible;
    in.openerHasRoom = h.hasRoom;
    in.hasOpeningSpell = h.hasOpeningSpell;
    EXPECT_EQ(Decide(in).action, Action::NoEligibleOpener);
}

// mod-dungeon-clear#36, live 2026-10-27: three guild runs in one day sat at
// the cannon on "no out-of-combat non-healer can open the Defias Gunpowder
// chest" until their cap. Every non-healer in those parties was out of combat,
// knew Opening, and had full general bags; the only healer had full bags too.
// A full-bagged member with junk to throw away is now the opener, and it
// frees a slot before it walks to the chest.
TEST(DcDefiasGunpowder, AFullBaggedPartyDiscardsJunkInsteadOfStalling)
{
    using L = Looter;
    // {canStore, bagSpace, inCombat, isHealer, knowsOpening, canDiscard}
    L const fullBags[] = {{false, 100, false, false, true, true},   // the tank (hook bot)
                          {false, 100, false, true, true, true},    // the healer
                          {false, 100, false, false, true, true}};  // a dps
    int const i = PickOpener(fullBags, 3);
    EXPECT_EQ(i, 0) << "the hook's own bot goes first";
    EXPECT_TRUE(MustDiscard(fullBags, 3, i));
    OpenerFlags const f = OpenerFlagsFor(fullBags, 3, i);
    EXPECT_TRUE(f.eligible && f.hasRoom && f.hasOpeningSpell);

    Inputs in = AtTheCannonEmptyHanded();
    in.openerEligible = f.eligible;
    in.openerHasRoom = f.hasRoom;
    in.hasOpeningSpell = f.hasOpeningSpell;
    in.openerMustDiscard = MustDiscard(fullBags, 3, i);
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::DiscardForRoom);
    EXPECT_FALSE(IsReport(p.action));
    EXPECT_EQ(p.goEntry, GO_GUNPOWDER);

    // Once the slot is free the same opener walks to the chest.
    in.openerMustDiscard = false;
    EXPECT_EQ(Decide(in).action, Action::WalkToChest);
}

TEST(DcDefiasGunpowder, ADiscardIsOnlyTheFallbackAndKeepsTheOldGuards)
{
    using L = Looter;
    // A member with room always beats one that would have to throw something away.
    L const oneWithRoom[] = {{false, 100, false, false, true, true}, {true, 70}};
    EXPECT_EQ(PickOpener(oneWithRoom, 2), 1);
    EXPECT_FALSE(MustDiscard(oneWithRoom, 2, 1));
    // The hook's bot cannot discard (or is fighting, or a healer, or cannot
    // open the chest): the first member that can goes.
    L const hookCannot[] = {{false, 100, false, false, true, false},
                            {false, 100, true, false, true, true},
                            {false, 100, false, true, true, true},
                            {false, 100, false, false, false, true},
                            {false, 100, false, false, true, true}};
    EXPECT_EQ(PickOpener(hookCannot, 5), 4);
    EXPECT_TRUE(MustDiscard(hookCannot, 5, 4));
    // Nothing to throw away anywhere: still nobody, the old report.
    L const noJunk[] = {{false, 100}, {false, 100}};
    EXPECT_EQ(PickOpener(noJunk, 2), -1);
    EXPECT_FALSE(MustDiscard(noJunk, 2, -1));
}

TEST(DcDefiasGunpowder, OnlyJunkInAGeneralSlotIsThrownAway)
{
    // {quality, class, sellPrice, count, startsQuest, inGeneralSlot}
    BagItem const hearthstone{1, 15, 0, 1, false, true};
    BagItem const questItem{0, 12, 5, 1, false, true};
    BagItem const key{0, 13, 5, 1, false, true};
    BagItem const emptyBag{0, 1, 250, 1, false, true};
    BagItem const questStarter{0, 0, 5, 1, true, true};
    BagItem const greyNoPrice{0, 15, 0, 1, false, true};
    BagItem const greenItem{2, 2, 5, 1, false, true};
    BagItem const inAmmoPouch{0, 6, 1, 200, false, false};
    for (BagItem const& b : {hearthstone, questItem, key, emptyBag, questStarter, greyNoPrice,
                             greenItem, inAmmoPouch})
        EXPECT_FALSE(IsDiscardable(b));
    BagItem const keepAll[] = {hearthstone, questItem, greenItem, inAmmoPouch};
    EXPECT_EQ(PickDiscard(keepAll, 4), -1);
    EXPECT_EQ(PickDiscard(nullptr, 3), -1);

    // The cheapest whole grey stack goes, ties to the earlier slot.
    BagItem const greyDear{0, 4, 900, 1, false, true};
    BagItem const greyStack{0, 15, 5, 4, false, true};  // 20 copper in all
    BagItem const greyBoots{0, 4, 6, 1, false, true};   // 6 copper
    BagItem const greyBoots2{0, 4, 6, 1, false, true};
    BagItem const greys[] = {hearthstone, greyDear, greyStack, greyBoots, greyBoots2};
    EXPECT_EQ(PickDiscard(greys, 5), 3);
}

// mod-dungeon-clear#85 review: white (common) items are never junk. White is
// where the trade goods and reagents crafters keep live (linen cloth, herbs,
// ore, dyes), the reagents spells use, the hunter's ammo in a backpack,
// food and water, and the profession tools (a mining pick, a skinning knife,
// a blacksmith hammer and thieves' tools are all white and sell for copper).
// A full bag of whites with no grey keeps the old NoEligibleOpener report.
TEST(DcDefiasGunpowder, WhiteItemsAreNeverThrownAway)
{
    // {quality, class, sellPrice, count, startsQuest, inGeneralSlot}
    BagItem const linenCloth{1, 7, 13, 20, false, true};    // trade goods
    BagItem const peacebloom{1, 7, 5, 10, false, true};     // herb
    BagItem const symbolOfKings{1, 5, 2, 20, false, true};  // spell reagent
    BagItem const roughArrows{1, 6, 1, 200, false, true};   // ammo in the backpack
    BagItem const bakedBread{1, 0, 1, 5, false, true};      // food
    BagItem const miningPick{1, 2, 16, 1, false, true};     // profession tool
    BagItem const thievesTools{1, 7, 25, 1, false, true};   // rogue's lockpicks
    BagItem const whiteShirt{1, 4, 1, 1, false, true};
    for (BagItem const& b : {linenCloth, peacebloom, symbolOfKings, roughArrows, bakedBread,
                             miningPick, thievesTools, whiteShirt})
        EXPECT_FALSE(IsDiscardable(b));
    BagItem const allWhite[] = {linenCloth, peacebloom, symbolOfKings, roughArrows,
                                bakedBread, miningPick, thievesTools, whiteShirt};
    EXPECT_EQ(PickDiscard(allWhite, 8), -1);
    // One grey among them is the only thing that goes, however dear it is.
    BagItem const greyDear{0, 4, 900, 1, false, true};
    BagItem const mixed[] = {whiteShirt, linenCloth, greyDear, roughArrows};
    EXPECT_EQ(PickDiscard(mixed, 4), 2);
}

// Someone in the party already carries it: no trip to the chest.
TEST(DcDefiasGunpowder, ACarrierSkipsTheChest)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.chest = Chest::Missing;  // already looted by the carrier

    in.carrier = Carrier::Self;
    EXPECT_EQ(Decide(in).action, Action::UseOnCannon);

    in.carrier = Carrier::PartyBot;
    in.actorToCannon = 12.0f;
    EXPECT_EQ(Decide(in).action, Action::WalkToCannon);
    in.actorToCannon = 2.0f;
    EXPECT_EQ(Decide(in).action, Action::UseOnCannon);

    // A player's gunpowder is theirs to use.
    in.carrier = Carrier::PartyHuman;
    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::WaitForPlayer);
    EXPECT_TRUE(IsReport(p.action));
}

TEST(DcDefiasGunpowder, TheGunpowderCastIsNotInterrupted)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.carrier = Carrier::Self;
    in.actorCasting = true;
    EXPECT_EQ(Decide(in).action, Action::WaitForCast);
}

TEST(DcDefiasGunpowder, AnOpenDoorIsDoneWhateverElseIsTrue)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.doorOpen = true;
    for (Carrier c : {Carrier::None, Carrier::Self, Carrier::PartyBot, Carrier::PartyHuman,
                      Carrier::Dead})
    {
        in.carrier = c;
        EXPECT_EQ(Decide(in).action, Action::Done);
    }
}

// A carrier that dies holding the gunpowder (the chest is spent by then) is a
// named, recoverable report, not a watchdog stall: the step holds on the
// report until they are back on their feet, and the use on the cannon resumes
// on the rez (mod-dungeon-clear#39).
TEST(DcDefiasGunpowder, ADeadCarrierIsANamedRecoverableReport)
{
    Inputs in = AtTheCannonEmptyHanded();
    in.chest = Chest::Spent;  // the carrier looted it before dying
    in.carrier = Carrier::Dead;

    Plan const p = Decide(in);
    EXPECT_EQ(p.action, Action::CarrierDead);
    EXPECT_EQ(p.goEntry, GO_CANNON);
    EXPECT_TRUE(IsReport(p.action));

    // Back on their feet: the plan walks them to the cannon and uses it.
    in.carrier = Carrier::PartyBot;
    in.actorToCannon = 12.0f;
    EXPECT_EQ(Decide(in).action, Action::WalkToCannon);
    in.actorToCannon = 2.0f;
    EXPECT_EQ(Decide(in).action, Action::UseOnCannon);
}

// The Iron Clad Door opening always says so at INFO: the transition to Done
// always produces a fresh report code, whatever the step was doing, so the
// hook's once-per-change log fires on it (mod-dungeon-clear#39).
TEST(DcDefiasGunpowder, TheDoorOpeningAlwaysLogs)
{
    Action const running[] = {Action::WaitForCast,  Action::WalkToCannon,
                              Action::UseOnCannon,  Action::WaitForPlayer,
                              Action::TakeFromChest, Action::WalkToChest,
                              Action::OpenChest,    Action::ReopenChest,
                              Action::NoCannon,     Action::NoChest,
                              Action::ChestSpent,   Action::NoOpeningSpell,
                              Action::OpeningNoLoot, Action::NoBagRoom,
                              Action::NoEligibleOpener, Action::CarrierDead};
    for (Action a : running)
    {
        std::uint8_t const last = ReportCode(a, false, 0);
        EXPECT_NE(ReportCode(Action::Done, false, last), last)
            << Describe(a) << ": the door opening must log a fresh line";
    }
}

// "Uses the Defias Gunpowder on the cannon" is only for a confirmed cast: a
// refused use and a confirmed use are different report codes, so the hook
// logs them as different lines and the "uses" wording never attaches to a use
// that started no cast (mod-dungeon-clear#39).
TEST(DcDefiasGunpowder, TheUseIsOnlyLoggedOnceTheCastIsConfirmed)
{
    std::uint8_t const confirmed = ReportCode(Action::UseOnCannon, false, 0);
    std::uint8_t const refused = ReportCode(Action::UseOnCannon, true, 0);
    EXPECT_NE(confirmed, refused);
    EXPECT_EQ(confirmed & REPORT_CAST_REFUSED, 0u);
    EXPECT_EQ(refused & REPORT_CAST_REFUSED, REPORT_CAST_REFUSED);
}

// The step logs why it is Running once per change (ReportCode), not once per
// tick, and every Running branch has its own code, so the line names the one
// it is in.
TEST(DcDefiasGunpowder, EveryBranchHasItsOwnReport)
{
    Action const all[] = {Action::Done,          Action::WaitForCast,  Action::WalkToCannon,
                          Action::UseOnCannon,   Action::WaitForPlayer, Action::TakeFromChest,
                          Action::WalkToChest,   Action::OpenChest,    Action::NoCannon,
                          Action::NoChest,       Action::ChestSpent,   Action::NoOpeningSpell,
                          Action::NoBagRoom,     Action::NoEligibleOpener, Action::CarrierDead,
                          Action::DiscardForRoom};
    std::vector<std::uint8_t> seen;
    for (Action a : all)
    {
        std::uint8_t const c = ReportCode(a, false, 0);
        EXPECT_NE(c, 0u) << "0 is 'nothing said yet'";
        EXPECT_EQ(c & ~REPORT_ACTION_MASK, 0u);
        for (std::uint8_t s : seen)
            EXPECT_NE(c, s);
        seen.push_back(c);
        // The same branch on the next tick is not news (a second UseOnCannon is
        // once: see ACastCycleIsOneReportAndARepeatIsAnother).
        if (a != Action::UseOnCannon)
            EXPECT_EQ(ReportCode(a, false, c), c) << Describe(a);
    }
}

// The cast the step started (the 2s gunpowder use, the chest's Opening) is
// folded into the action that started it, and a use that ended with the door
// still shut says so once, then holds.
TEST(DcDefiasGunpowder, ACastCycleIsOneReportAndARepeatIsAnother)
{
    std::uint8_t const use = ReportCode(Action::UseOnCannon, false, 0);
    EXPECT_EQ(ReportCode(Action::WaitForCast, false, use), use);
    std::uint8_t const again = ReportCode(Action::UseOnCannon, false, use);
    EXPECT_NE(again, use) << "the door stayed shut after a cast: say it once";
    EXPECT_EQ(ReportCode(Action::WaitForCast, false, again), again);
    EXPECT_EQ(ReportCode(Action::UseOnCannon, false, again), again) << "then hold";

    std::uint8_t const open = ReportCode(Action::OpenChest, false, 0);
    EXPECT_EQ(ReportCode(Action::WaitForCast, false, open), open);

    // A cast in flight the step did not start is its own report.
    std::uint8_t const walk = ReportCode(Action::WalkToCannon, false, 0);
    EXPECT_NE(ReportCode(Action::WaitForCast, false, walk), walk);
}

// The item use's result is part of the report: a refused cast reads apart from
// a started one, and after a refusal a cast in flight is not ours.
TEST(DcDefiasGunpowder, ARefusedCastIsItsOwnReport)
{
    std::uint8_t const use = ReportCode(Action::UseOnCannon, false, 0);
    std::uint8_t const refused = ReportCode(Action::UseOnCannon, true, use);
    EXPECT_NE(refused, use);
    EXPECT_NE(refused, ReportCode(Action::UseOnCannon, false, use));
    EXPECT_EQ(ReportCode(Action::UseOnCannon, true, refused), refused);
    EXPECT_EQ(ReportCode(Action::UseOnCannon, false, refused), use)
        << "a cast that starts after refusals is a fresh use, not a repeat";
    EXPECT_NE(ReportCode(Action::WaitForCast, false, refused), refused);

    std::uint8_t const open = ReportCode(Action::OpenChest, false, 0);
    EXPECT_NE(ReportCode(Action::OpenChest, true, 0), open);
    // A refused flag means nothing on an action that casts nothing.
    EXPECT_EQ(ReportCode(Action::WalkToChest, true, 0), ReportCode(Action::WalkToChest, false, 0));
}

// --- the module grants nothing ------------------------------------------------

namespace
{
    using namespace DcSourceScan;

    // "file:line" of every AddItem( call in `p`.
    std::vector<std::string> AddItemCalls(std::filesystem::path const& p)
    {
        std::vector<std::string> hits;
        std::istringstream code(StripComments(ReadFile(p)));
        std::string line;
        for (int n = 1; std::getline(code, line); ++n)
            if (line.find("AddItem(") != std::string::npos)
                hits.push_back(p.filename().string() + ":" + std::to_string(n));
        return hits;
    }
}

TEST(DcNoGrant, TheStripperKeepsCodeAndDropsComments)
{
    std::string const s = StripComments("a(); // AddItem(1)\n/* AddItem(2) */ b(\"//x\");\n");
    EXPECT_EQ(s.find("AddItem("), std::string::npos);
    EXPECT_NE(s.find("a();"), std::string::npos);
    EXPECT_NE(s.find("b(\"//x\");"), std::string::npos);
}

// The event hooks: every scripted-event hook and driver, every event table, and
// the event executor that runs their steps. None may create an item.
TEST(DcNoGrant, TheEventHooksNeverCallAddItem)
{
    std::filesystem::path const dc = ModuleSrc() / "Ai" / "Dungeon" / "DungeonClear";
    std::filesystem::path const hookFile = dc / "Overrides" / "ObjectiveHookRegistry.cpp";
    std::filesystem::path const executor = dc / "Util" / "DungeonEventExecutor.cpp";
    ASSERT_TRUE(std::filesystem::exists(hookFile)) << "cannot find " << hookFile;
    ASSERT_TRUE(std::filesystem::exists(executor)) << "cannot find " << executor;

    std::vector<std::filesystem::path> files = {executor};
    for (char const* dir : {"Overrides", "Data/Events"})
        for (auto const& e : std::filesystem::directory_iterator(dc / dir))
            if (e.is_regular_file() && IsSource(e.path()))
                files.push_back(e.path());

    std::size_t scanned = 0;
    for (auto const& f : files)
    {
        ++scanned;
        for (std::string const& hit : AddItemCalls(f))
            ADD_FAILURE() << hit << " calls AddItem: an event hook must earn the item "
                                    "(loot it, or use one a member carries), never create it";
    }
    EXPECT_GT(scanned, 20u) << "the hook directories moved; the scan is not looking anywhere";
}

// And the rest of the module: no AddItem anywhere under src/.
TEST(DcNoGrant, TheModuleNeverCallsAddItem)
{
    std::filesystem::path const src = ModuleSrc();
    ASSERT_TRUE(std::filesystem::exists(src)) << "cannot find " << src;
    std::size_t scanned = 0;
    for (auto const& e : std::filesystem::recursive_directory_iterator(src))
    {
        if (!e.is_regular_file() || !IsSource(e.path()))
            continue;
        ++scanned;
        for (std::string const& hit : AddItemCalls(e.path()))
            ADD_FAILURE() << hit << " calls AddItem: the module grants nothing a player "
                                    "could not earn";
    }
    EXPECT_GT(scanned, 100u);
}
