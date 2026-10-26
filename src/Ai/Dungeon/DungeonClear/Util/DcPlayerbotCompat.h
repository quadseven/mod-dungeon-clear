/*
 * mod-dungeon-clear — DcPlayerbotCompat.h
 *
 * "Who is driving this character?", asked so that it means the same thing on
 * every mod-playerbots branch this module is built against.
 *
 * mod-playerbots PR #2592 (on test-staging; not on master as of Aug 2026)
 * reshuffled these predicates AND reused one of the names for something with
 * the opposite meaning:
 *
 *   before #2592                       after #2592
 *   ------------------------------     --------------------------------------
 *   botAI->IsRealPlayer()              IsSelfBot(player)
 *     master == bot, i.e. a SELF-BOT     same meaning, new name
 *   (no equivalent)                    IsRealPlayer(player)
 *                                        no PlayerbotAI at all — a person on
 *                                        a game client. The OPPOSITE of what
 *                                        the old member of that name tested.
 *   botAI->HasRealPlayerMaster()       botAI->HasGameClientMaster()
 *   botAI->HasActivePlayerMaster()     IsRealPlayer(botAI->GetMaster())
 *
 * So a module that must build on both cannot call either name directly: the
 * member is gone on one branch, and on the other the surviving free function
 * of the same name compiles fine and inverts the answer. That is not a
 * hypothetical — it is what makes this a shim rather than a typedef.
 *
 * Nothing here is conditional. Both predicates are written in terms of the two
 * pieces of API that are byte-identical on both branches — the
 * GET_PLAYERBOT_AI macro and the public PlayerbotAI::GetMaster() — and each is
 * literally the body of its branch's own version:
 *
 *   pre-#2592   IsRealPlayer()  { return master ? (master == bot) : false; }
 *   post-#2592  IsSelfBot(p)    { ai = GET_PLAYERBOT_AI(p); return ai && ai->GetMaster() == p; }
 *
 * which are the same test. No SFINAE, no version macro, no #if: there is
 * nothing to detect, so detecting it would only add a way to be wrong. (The
 * second half of this file, for the 2026-10 catch-up, is the opposite case.)
 *
 * Call these qualified (DcPlayerbotCompat::IsSelfBot(p)). Unqualified would be
 * ambiguous against the global IsSelfBot/IsRealPlayer that exist post-#2592.
 *
 * There is deliberately no HasGameClientMaster() here: DC does not need one,
 * and it is spelled IsHumanControlled(botAI->GetMaster()) if it ever does.
 */

#ifndef _DUNGEONCLEAR_DCPLAYERBOTCOMPAT_H
#define _DUNGEONCLEAR_DCPLAYERBOTCOMPAT_H

#include "Playerbots.h"

class Player;

namespace DcPlayerbotCompat
{
    // A bot whose master is itself: the human's own character on autopilot.
    // They share one GUID, so anything this bot does the player is credited
    // with — see the loot double-roll this module exists to suppress.
    inline bool IsSelfBot(Player* player)
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        return botAI && botAI->GetMaster() == player;
    }

    // No bot AI of any kind: an actual person playing through a game client.
    // The null check is load-bearing — GET_PLAYERBOT_AI(nullptr) returns
    // nullptr, so without it a null Player would read as a real person.
    inline bool IsRealPlayer(Player* player)
    {
        return player && !GET_PLAYERBOT_AI(player);
    }

    // Either of the above: somebody who will act for themselves — walk up and
    // loot, answer their own roll. This is usually the one you want, and it is
    // what the pre-#2592 idiom `!ai || ai->IsRealPlayer()` spelled out.
    //
    // Qualified even here, one scope away from the definitions, and that is
    // not style: Player lives in the global namespace, so ADL on a Player*
    // argument reaches global scope, where post-#2592 there is an
    // ::IsRealPlayer(Player*) with this exact signature. An unqualified call
    // would put both in the overload set — two exact matches, ambiguous, and
    // it would fail to compile on the very branch this shim exists for.
    inline bool IsHumanControlled(Player* player)
    {
        return DcPlayerbotCompat::IsRealPlayer(player)
            || DcPlayerbotCompat::IsSelfBot(player);
    }
}

/*
 * ---------------------------------------------------------------------------
 * The 2026-10 upstream catch-up: one module, two pin sets.
 * ---------------------------------------------------------------------------
 *
 * Everything below DOES detect, because here the two pin sets really have no
 * spelling in common. The old pins are the playerbots fork core plus a
 * mod-playerbots from before #2854 / #2912; the new pins are the
 * upstream-shaped core (fork-only hooks deleted) plus a mod-playerbots that
 * has both. What changed, and the helper that hides it:
 *
 *   old pins                             new pins                    helper
 *   -----------------------------------  --------------------------  ----------------------------
 *   sPlayerbotAIConfig.reactDelay        .ReactDelay (#2854: every   DcPbConfig::ReactDelay() ...
 *     (lowerCamelCase members)             member UpperCamelCase)
 *   TargetValueExclusionType::Tank       ::TankTarget (#2912)        kTankTargetExclusions
 *   TargetValueExclusionType::Dps        ::DpsTarget                 kDpsTargetExclusions
 *   TargetValueExclusionType::Attacker   ::DebuffTarget              kDebuffTargetExclusions
 *     (attacker-without-aura pool)         (same pool, renamed)
 *   PlayerbotScript::OnPlayerbotUpdate   WorldScript::OnUpdate       WorldTickScript
 *   PlayerbotScript::                    ServerScript::OnPacketSent  BotPacketScript
 *     OnPlayerbotPacketSent(p, pkt*)       (session, pkt&)
 *   Player::BotCanUseItem                Player::CanUseItem (the     CanBotUseItem
 *                                          relic checks moved in)
 *   new WorldSession(..., is_bot=true)   no is_bot; a null socket    NewSocketlessSession
 *                                          already means headless
 *   IWorld::AddQueryHolderCallback       moved off IWorld            kWorldHasQueryHolderCallback
 *
 * Each helper picks with a C++20 requires-expression on the type that changed
 * (the build is C++20 on both cores), never a version macro or a CMake probe:
 * the test is the exact member being used, so a half-synced fork still reads
 * correctly. On the old pins every helper resolves to the very expression the
 * call site used before, so behaviour there is unchanged.
 *
 * The new-pin enum adds a fourth pool, Aoe (the AoE target count). It has no
 * old-pin equivalent and DC never excluded from it, so it stays unhandled.
 *
 * Delete a helper, and spell its call sites the new way, once no supported pin
 * set needs the old spelling.
 */

#include "PlayerbotAIConfig.h"
#include "ScriptMgr.h"
#include "ServerScript.h"
#include "Strategy.h"
#include "WorldScript.h"
#include "WorldSession.h"

#include <type_traits>
#include <utility>

struct ItemTemplate;
class WorldPacket;

// Declared by the old (fork) core only. Naming it here lets the old-pin half of
// the script adapters below spell it on the new pins too, where it stays an
// incomplete type that nothing instantiates.
class PlayerbotScript;

// PlayerbotAIConfig members, by their new (#2854) names. Each returns a
// reference to the live member, so a test can still assign through it.
namespace DcPbConfig
{
#define DC_PB_CONFIG_MEMBER(Upper, lower)                                      \
    namespace Detail                                                           \
    {                                                                          \
        template <typename C>                                                  \
        decltype(auto) Upper(C& config)                                        \
        {                                                                      \
            if constexpr (requires { config.Upper; })                          \
                return (config.Upper);                                         \
            else                                                               \
                return (config.lower);                                         \
        }                                                                      \
    }                                                                          \
    inline decltype(auto) Upper() { return Detail::Upper(sPlayerbotAIConfig); }

    DC_PB_CONFIG_MEMBER(AlmostFullHealth, almostFullHealth)
    DC_PB_CONFIG_MEMBER(ApplyInstanceStrategies, applyInstanceStrategies)
    DC_PB_CONFIG_MEMBER(AutoGearQualityLimit, autoGearQualityLimit)
    DC_PB_CONFIG_MEMBER(AutoGearScoreLimit, autoGearScoreLimit)
    DC_PB_CONFIG_MEMBER(FollowDistance, followDistance)
    DC_PB_CONFIG_MEMBER(HealDistance, healDistance)
    DC_PB_CONFIG_MEMBER(HighMana, highMana)
    DC_PB_CONFIG_MEMBER(LootDistance, lootDistance)
    DC_PB_CONFIG_MEMBER(LootGreedRollLevel, lootGreedRollLevel)
    DC_PB_CONFIG_MEMBER(LootNeedRollLevel, lootNeedRollLevel)
    DC_PB_CONFIG_MEMBER(MaxAddedBots, maxAddedBots)
    DC_PB_CONFIG_MEMBER(MaxWaitForMove, maxWaitForMove)
    DC_PB_CONFIG_MEMBER(MediumHealth, mediumHealth)
    DC_PB_CONFIG_MEMBER(MinEnchantingBotLevel, minEnchantingBotLevel)
    DC_PB_CONFIG_MEMBER(PassiveDelay, passiveDelay)
    DC_PB_CONFIG_MEMBER(PremadeSpecName, premadeSpecName)
    DC_PB_CONFIG_MEMBER(RandomBotAccounts, randomBotAccounts)
    DC_PB_CONFIG_MEMBER(ReactDelay, reactDelay)
    DC_PB_CONFIG_MEMBER(ReactDistance, reactDistance)
    DC_PB_CONFIG_MEMBER(SightDistance, sightDistance)
    DC_PB_CONFIG_MEMBER(SpellDistance, spellDistance)

#undef DC_PB_CONFIG_MEMBER
}

namespace DcPlayerbotCompat
{
    namespace Detail
    {
        template <typename E>
        constexpr E TankTargetExclusions()
        {
            if constexpr (requires { E::TankTarget; })
                return E::TankTarget;
            else
                return E::Tank;
        }

        template <typename E>
        constexpr E DpsTargetExclusions()
        {
            if constexpr (requires { E::DpsTarget; })
                return E::DpsTarget;
            else
                return E::Dps;
        }

        template <typename E>
        constexpr E DebuffTargetExclusions()
        {
            if constexpr (requires { E::DebuffTarget; })
                return E::DebuffTarget;
            else
                return E::Attacker;
        }

        // The fork core's ScriptMgr still dispatches the PlayerbotScript hooks.
        template <typename M>
        concept HasForkTickHook = requires(M& mgr, uint32 diff) { mgr.OnPlayerbotUpdate(diff); };

        template <typename M>
        concept HasForkPacketHook = requires(M& mgr, Player* player, WorldPacket const* packet) {
            mgr.OnPlayerbotPacketSent(player, packet);
        };

        template <typename P>
        InventoryResult CanBotUseItem(P const* bot, ItemTemplate const* proto)
        {
            if constexpr (requires { bot->BotCanUseItem(proto); })
                return bot->BotCanUseItem(proto);
            else
                return bot->CanUseItem(proto);
        }

        template <typename W>
        concept HasQueryHolderCallback = requires(W& world, SQLQueryHolderCallback&& callback) {
            world.AddQueryHolderCallback(std::move(callback));
        };

        // Every base below is a template parameter, so the half that does not
        // apply to the core being built is never instantiated and may name
        // hooks that core lacks.
        template <bool Fork, typename ForkBase, typename WorldBase>
        class WorldTickScript;

        template <typename ForkBase, typename WorldBase>
        class WorldTickScript<true, ForkBase, WorldBase> : public ForkBase
        {
        protected:
            explicit WorldTickScript(char const* name) : ForkBase(name) {}
            virtual void OnWorldTick(uint32 diff) = 0;

        public:
            void OnPlayerbotUpdate(uint32 diff) override { OnWorldTick(diff); }
        };

        template <typename ForkBase, typename WorldBase>
        class WorldTickScript<false, ForkBase, WorldBase> : public WorldBase
        {
        protected:
            explicit WorldTickScript(char const* name)
                : WorldBase(name, { WORLDHOOK_ON_UPDATE }) {}
            virtual void OnWorldTick(uint32 diff) = 0;

        public:
            void OnUpdate(uint32 diff) override { OnWorldTick(diff); }
        };

        template <bool Fork, typename ForkBase, typename ServerBase, typename Hooks>
        class BotPacketScript;

        template <typename ForkBase, typename ServerBase, typename Hooks>
        class BotPacketScript<true, ForkBase, ServerBase, Hooks> : public ForkBase
        {
        protected:
            explicit BotPacketScript(char const* name) : ForkBase(name) {}
            virtual void OnBotPacketSent(Player* player, WorldPacket const& packet) = 0;

        public:
            void OnPlayerbotPacketSent(Player* player, WorldPacket const* packet) override
            {
                if (packet)
                    OnBotPacketSent(player, *packet);
            }
        };

        template <typename ForkBase, typename ServerBase, typename Hooks>
        class BotPacketScript<false, ForkBase, ServerBase, Hooks> : public ServerBase
        {
        protected:
            // Only this hook: an empty list would enable every ServerScript hook.
            explicit BotPacketScript(char const* name)
                : ServerBase(name, { Hooks::SERVERHOOK_ON_PACKET_SENT }) {}
            virtual void OnBotPacketSent(Player* player, WorldPacket const& packet) = 0;

        public:
            void OnPacketSent(WorldSession* session, WorldPacket const& packet) override
            {
                OnBotPacketSent(session ? session->GetPlayer() : nullptr, packet);
            }
        };
    }

    // The three strategy target-exclusion pools DC appends to.
    inline constexpr TargetValueExclusionType kTankTargetExclusions =
        Detail::TankTargetExclusions<TargetValueExclusionType>();
    inline constexpr TargetValueExclusionType kDpsTargetExclusions =
        Detail::DpsTargetExclusions<TargetValueExclusionType>();
    inline constexpr TargetValueExclusionType kDebuffTargetExclusions =
        Detail::DebuffTargetExclusions<TargetValueExclusionType>();

    // A global once-per-world-tick hook that fires whether or not any bot AI
    // exists. Derive from it and override OnWorldTick(diff). On the old pins it
    // is PlayerbotScript::OnPlayerbotUpdate, exactly as before.
    using WorldTickScript = Detail::WorldTickScript<Detail::HasForkTickHook<ScriptMgr>,
                                                    PlayerbotScript, WorldScript>;

    // Sees every packet WorldSession::SendPacket is handed, before the
    // socketless early return, so it is the one place a bot's packets are
    // visible. Derive from it and override OnBotPacketSent(player, packet);
    // player may be null. On the old pins it is
    // PlayerbotScript::OnPlayerbotPacketSent, exactly as before.
    using BotPacketScript = Detail::BotPacketScript<Detail::HasForkPacketHook<ScriptMgr>,
                                                    PlayerbotScript, ServerScript, ServerHook>;

    // The level-blind usability test the loot roll uses. The fork core's
    // BotCanUseItem was CanUseItem plus the relic class checks; the new core's
    // CanUseItem does those checks itself.
    inline InventoryResult CanBotUseItem(Player const* bot, ItemTemplate const* proto)
    {
        return Detail::CanBotUseItem(bot, proto);
    }

    // A session with no socket, for a character no client drives. The fork
    // core is told so with a trailing is_bot = true; the new core has no such
    // argument and derives it from the null socket.
    template <typename... Args>
    WorldSession* NewSocketlessSession(Args&&... args)
    {
        if constexpr (std::is_constructible_v<WorldSession, Args&&..., bool>)
            return new WorldSession(std::forward<Args>(args)..., true);
        else
            return new WorldSession(std::forward<Args>(args)...);
    }

    // Whether IWorld still declares AddQueryHolderCallback (a pure virtual a
    // world mock has to implement on the old pins only).
    inline constexpr bool kWorldHasQueryHolderCallback = Detail::HasQueryHolderCallback<IWorld>;
}

#endif
