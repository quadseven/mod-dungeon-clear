/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestDriver.h"

#include <algorithm>

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "Timer.h"
#include "WorldSession.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"

namespace DcTestDriver
{
    namespace
    {
        ObjectGuid _guid;           // resolved from the config name, cached
        std::string _resolvedName;  // name _guid was resolved for (conf can change)
        bool _loginIssued = false;
        bool _initialized = false;

        // How a provisioning attempt ended, so Ensure() can tell "wait, this
        // fixes itself" from "this will never work".
        enum class Provision
        {
            Created,  // the character now exists (its DB write may be in flight)
            Retry,    // transient: a queued write has not landed yet
            Refused,  // standing condition: retrying cannot help
        };

        // Why provisioning is not being attempted again right now.
        enum class ProvisionLatch
        {
            None,     // nothing has failed yet
            Stuck,    // transient failures used up the burst budget; a later
                      // burst may still succeed
            Refused,  // standing condition; only a conf change clears it
        };

        // Auto-provisioning state. A refusal that retrying cannot fix (bad
        // name, forbidden account, auto-provisioning off) latches so it is
        // reported once instead of being re-attempted by every command. A
        // TRANSIENT failure does not latch: it is retried on a cooldown, in a
        // bounded burst whose budget refills once the caller goes quiet.
        // _awaitingFlush holds the login back until the character we just
        // created is actually readable from the DB.
        ProvisionLatch _provisionLatch = ProvisionLatch::None;
        std::string _provisionRefusal;  // replayed, so the retry keeps the real reason
        uint32 _provisionAttempts = 0;
        uint32 _provisionProbedAt = 0;      // getMSTime() of the last attempt
        bool _accountCreateIssued = false;  // the account INSERT is queued; never re-issue it
        std::string _provisionAccount;      // account the state above belongs to
        bool _awaitingFlush = false;
        uint32 _flushProbedAt = 0;  // getMSTime() of the last "is it there yet" probe

        // A transient provisioning failure resolves in the time a queued INSERT
        // takes to commit, so probe on this cadence rather than once per
        // command. The burst is capped so a genuinely stuck database reports a
        // real problem instead of an eternal "please wait", and the cap refills
        // after a quiet period so a slow write never becomes "broken until you
        // restart the worldserver".
        constexpr uint32 PROVISION_RETRY_MS = 500;
        constexpr uint32 PROVISION_MAX_ATTEMPTS = 20;
        constexpr uint32 PROVISION_BURST_RESET_MS = 60000;

        void ResetProvisionState()
        {
            _provisionLatch = ProvisionLatch::None;
            _provisionRefusal.clear();
            _provisionAttempts = 0;
            _provisionProbedAt = 0;
            _accountCreateIssued = false;
            _awaitingFlush = false;
        }

        std::string ConfName()
        {
            // showLogs=false: a missing conf line is normal (the default below is
            // authoritative), and this is re-read on every driver resolve — see
            // DcSettings.h. String-valued, so the numeric registry can't hold it.
            return sConfigMgr->GetOption<std::string>("DungeonClear.TestRun.DriverCharacter",
                                                      "Dcdriver", false);
        }

        // Account that owns the driver character. Empty turns auto-provisioning
        // off entirely, for operators who would rather create it by hand.
        std::string ConfAccount()
        {
            return sConfigMgr->GetOption<std::string>("DungeonClear.TestRun.DriverAccount",
                                                      "dcdriver", false);
        }

        // Resolve (and re-resolve after a conf change) the driver's guid.
        ObjectGuid ResolveGuid()
        {
            std::string const name = ConfName();
            if (name.empty())
                return ObjectGuid::Empty;

            // A re-pointed account gets its own provisioning attempt too: a
            // latched refusal and the "the INSERT is already queued" flag both
            // describe the OLD account and must not outlive it.
            std::string const account = ConfAccount();
            if (account != _provisionAccount)
            {
                _provisionAccount = account;
                ResetProvisionState();
            }

            if (_guid && name == _resolvedName)
                return _guid;
            if (name != _resolvedName)
            {
                // A renamed driver is a different character: the new name
                // gets its own provisioning attempt.
                ResetProvisionState();
            }
            _guid = sCharacterCache->GetCharacterGuidByName(name);
            _resolvedName = name;
            _initialized = false;
            _loginIssued = false;
            return _guid;
        }

        // The manual recipe, for every case where we won't or can't do it.
        std::string ManualSetup(std::string const& name)
        {
            return "test driver character '" + name +
                   "' not found — create it on a dedicated bot account and set "
                   "DungeonClear.TestRun.DriverCharacter";
        }

        // A password for an account nobody is meant to log into. The character
        // is logged in headlessly through the fake-session path, which never
        // authenticates, so this is written once and deliberately never
        // recorded anywhere — an operator who wants the account back uses
        // `account set password`.
        std::string RandomPassword()
        {
            static char const alphabet[] =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            std::string out;
            out.reserve(MAX_PASS_STR - 1);
            for (uint32 i = 0; i < MAX_PASS_STR - 1; ++i)
                out += alphabet[urand(0, sizeof(alphabet) - 2)];
            return out;
        }

        // Create the driver's account and character when they don't exist.
        //
        // Every `.dc test` that isn't typed by an in-game GM needs this
        // character, and that is all of them from the console, the AC Command
        // Deck and Test Deck (SOAP commands run through the same CLI queue).
        // Without this the harness is unusable until someone reads a config
        // comment, makes an account and logs into the game with a client to
        // make one level-1 character.
        //
        // Mirrors RandomPlayerbotFactory::CreateRandomBot, the core's only
        // headless character-creation path: a detached WorldSession, a
        // CharacterCreateInfo, Player::Create, SaveToDB, then the cache entry
        // that makes the name resolvable. Human warrior with default
        // appearance — nothing about a parked GM stand-in depends on either,
        // and it is a valid race/class pair on every realm.
        //
        // Provision::Created when the character now exists (its DB write may
        // still be in flight — see _awaitingFlush).
        Provision TryProvision(std::string const& name, std::string* why)
        {
            std::string const account = ConfAccount();
            if (account.empty())
            {
                // Explicitly opted out of auto-provisioning.
                *why = ManualSetup(name);
                return Provision::Refused;
            }

            // Refuse a name the lookup could never find again. Player::Create
            // does not validate names, so creating "dcdriver" would succeed
            // and then never resolve through GetCharacterGuidByName's exact
            // match — provisioning forever, one orphan character per attempt.
            std::string normalized = name;
            if (!normalizePlayerName(normalized) || normalized != name ||
                ObjectMgr::CheckPlayerName(name, true) != CHAR_NAME_SUCCESS)
            {
                *why = "DungeonClear.TestRun.DriverCharacter = '" + name +
                       "' is not a usable character name" +
                       (normalized != name ? " (did you mean '" + normalized + "'?)" : "") +
                       " — the driver could not be created";
                return Provision::Refused;
            }

            uint32 accountId = AccountMgr::GetId(account);
            if (!accountId)
            {
                // AccountMgr::CreateAccount issues its INSERT through
                // LoginDatabase.Execute, an ASYNCHRONOUS write on a worker
                // connection, while AccountMgr::GetId is a synchronous Query
                // on a different connection from the same pool. Nothing orders
                // the two, so the read-back legitimately misses a row that is
                // already on its way. Issue the INSERT exactly once and then
                // WAIT for it. Re-issuing it would queue a duplicate that the
                // unique index on account.username rejects, and treating the
                // miss as a standing refusal is what turned a sub-second race
                // into "the harness is broken until you restart the server".
                if (!_accountCreateIssued)
                {
                    AccountOpResult const res =
                        sAccountMgr->CreateAccount(account, RandomPassword());
                    // AOR_NAME_ALREADY_EXIST means the row is there (or in
                    // flight) and only our read is behind, so it is a wait,
                    // not a failure.
                    if (res != AOR_OK && res != AOR_NAME_ALREADY_EXIST)
                    {
                        *why = "could not create the test driver account '" + account +
                               "' (error " + std::to_string(static_cast<uint32>(res)) +
                               ") — " + ManualSetup(name);
                        return Provision::Refused;
                    }
                    _accountCreateIssued = true;
                    LOG_INFO("playerbots.dungeonclear",
                             "TESTDRIVER created account '{}' for the test driver "
                             "(password randomised and not recorded; use `account set "
                             "password` if you need it), waiting for the row to land",
                             account);
                }

                *why = "test driver account '" + account +
                       "' was just created and its row has not landed yet";
                return Provision::Retry;
            }

            // The rotation logs its own accounts' characters in and out on its
            // own schedule, which would pull the driver out from under a live
            // run. Refuse rather than produce an intermittently broken harness.
            auto const& rnd = sPlayerbotAIConfig.randomBotAccounts;
            if (std::find(rnd.begin(), rnd.end(), accountId) != rnd.end())
            {
                *why = "test driver account '" + account +
                       "' is one of AiPlayerbot.RandomBotAccounts — the bot rotation "
                       "would log the driver out mid-run. Point "
                       "DungeonClear.TestRun.DriverAccount at a plain account";
                return Provision::Refused;
            }

            // SEC_PLAYER: the driver elevates its own session at init
            // (SetSecurity below in Tick), so the account itself never needs
            // a GM level.
            WorldSession* session =
                new WorldSession(accountId, "", 0, nullptr, SEC_PLAYER,
                                 EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0),
                                 LOCALE_enUS, 0, false, false, 0, true);

            CharacterCreateInfo createInfo(name, RACE_HUMAN, CLASS_WARRIOR,
                                           GENDER_MALE, 0, 0, 0, 0, 0);
            Player* player = new Player(session);
            player->GetMotionMaster()->Initialize();
            if (!player->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(),
                                &createInfo))
            {
                player->CleanupsBeforeDelete();
                delete player;
                delete session;
                *why = "could not create the test driver character '" + name +
                       "' on account '" + account + "' — " + ManualSetup(name);
                return Provision::Refused;
            }

            player->setCinematic(2);          // skip the intro movie on login
            player->SetAtLoginFlag(AT_LOGIN_NONE);
            player->SaveToDB(true, false);
            sCharacterCache->AddCharacterCacheEntry(
                player->GetGUID(), accountId, player->GetName(), player->getGender(),
                player->getRace(), player->getClass(), player->GetLevel());

            ObjectGuid const guid = player->GetGUID();
            player->CleanupsBeforeDelete();
            delete player;
            delete session;

            // SaveToDB queues; the login path loads the character back out of
            // the database, so issuing it now would race the write. Tick()
            // releases the hold once the row is readable.
            _awaitingFlush = true;
            _flushProbedAt = getMSTime();
            LOG_INFO("playerbots.dungeonclear",
                     "TESTDRIVER created character '{}' ({}) on account '{}' ({}) — "
                     "waiting for the character save to land", name, guid.ToString(),
                     account, accountId);
            return Provision::Created;
        }

        Player* FindOnline()
        {
            if (!_guid)
                return nullptr;
            Player* p = ObjectAccessor::FindPlayer(_guid);
            return p && p->IsInWorld() ? p : nullptr;
        }
    }

    Player* Get()
    {
        ResolveGuid();
        return _initialized ? FindOnline() : nullptr;
    }

    Readiness Ensure(std::string* why)
    {
        if (Get())
            return Readiness::Ready;

        if (!ResolveGuid())
        {
            std::string const name = ConfName();
            if (name.empty())
            {
                if (why)
                    *why = "DungeonClear.TestRun.DriverCharacter is empty — the "
                           "harness has no GM to run as";
                return Readiness::Unavailable;
            }

            // A burst of transient failures is bounded, but the budget refills:
            // an attempt made long after the last one starts over, so a write
            // that was slow once never becomes a permanent refusal.
            if (_provisionLatch != ProvisionLatch::Refused && _provisionAttempts &&
                GetMSTimeDiffToNow(_provisionProbedAt) >= PROVISION_BURST_RESET_MS)
            {
                _provisionAttempts = 0;
                if (_provisionLatch == ProvisionLatch::Stuck)
                    _provisionLatch = ProvisionLatch::None;
            }

            // A refusal is a standing condition, and retrying it on every
            // command would just repeat the message.
            if (_provisionLatch != ProvisionLatch::None)
            {
                if (why)
                    *why = _provisionRefusal.empty() ? ManualSetup(name)
                                                     : _provisionRefusal;
                return Readiness::Unavailable;
            }

            // Between attempts, report the pending state instead of asking the
            // login database again once per command.
            if (_provisionAttempts &&
                GetMSTimeDiffToNow(_provisionProbedAt) < PROVISION_RETRY_MS)
            {
                if (why)
                    *why = _provisionRefusal;
                return Readiness::PendingLogin;
            }

            _provisionProbedAt = getMSTime();
            ++_provisionAttempts;

            std::string provisionWhy;
            Provision const outcome = TryProvision(name, &provisionWhy);
            if (outcome == Provision::Refused)
            {
                LOG_WARN("playerbots.dungeonclear", "TESTDRIVER {}", provisionWhy);
                _provisionLatch = ProvisionLatch::Refused;
                _provisionRefusal = provisionWhy;
                if (why)
                    *why = provisionWhy;
                return Readiness::Unavailable;
            }
            if (outcome == Provision::Retry)
            {
                _provisionRefusal = provisionWhy;
                if (_provisionAttempts >= PROVISION_MAX_ATTEMPTS)
                {
                    // Not a race any more. Say so, and stop probing until the
                    // caller has been quiet long enough to refill the budget.
                    LOG_WARN("playerbots.dungeonclear",
                             "TESTDRIVER gave up provisioning after {} attempts: {}",
                             _provisionAttempts, provisionWhy);
                    _provisionLatch = ProvisionLatch::Stuck;
                    if (why)
                        *why = provisionWhy;
                    return Readiness::Unavailable;
                }
                if (why)
                    *why = provisionWhy;
                return Readiness::PendingLogin;
            }

            // Resolve again so _guid picks up the cache entry we just added.
            if (!ResolveGuid())
            {
                if (why)
                    *why = ManualSetup(name);
                return Readiness::Unavailable;
            }
            if (why)
                *why = "created the test driver character '" + name + "' — it is "
                       "logging in";
            return Readiness::PendingLogin;
        }

        if (_awaitingFlush)
        {
            if (why)
                *why = "test driver '" + _resolvedName + "' was just created and is "
                       "still being written to the database";
            return Readiness::PendingLogin;
        }

        if (!_loginIssued)
        {
            // Masterless fake-session login (the random-bot path; the driver's
            // account is not a random account, so the rotation ignores it).
            sRandomPlayerbotMgr.AddPlayerBot(_guid, 0);
            _loginIssued = true;
            LOG_INFO("playerbots.dungeonclear", "TESTDRIVER logging in '{}' ({})",
                     _resolvedName, _guid.ToString());
        }

        if (why)
            *why = "test driver '" + _resolvedName + "' is logging in";
        return Readiness::PendingLogin;
    }

    bool EnsureOnline(std::string* whyPending)
    {
        std::string why;
        if (Ensure(&why) == Readiness::Ready)
            return true;
        if (whyPending)
            *whyPending = why + " — retry in a few seconds";
        return false;
    }

    void Tick()
    {
        // A just-created driver is not loadable until its queued INSERTs have
        // run, and the login path reads the character back out of the
        // database — a login issued too early fails and latches _loginIssued
        // with nothing to un-latch it. So hold until the row is really there.
        //
        // Asking for the row beats waiting on the write queue to empty: the
        // queue carries every other character save on the realm and on a busy
        // one it may never read zero, which would strand the driver offline.
        if (_awaitingFlush)
        {
            // Throttled: this is a synchronous query on the world thread, and
            // the answer cannot change faster than the write behind it lands.
            if (GetMSTimeDiffToNow(_flushProbedAt) < 500)
                return;
            _flushProbedAt = getMSTime();

            CharacterDatabasePreparedStatement* stmt =
                CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
            stmt->SetData(0, _resolvedName);
            if (!CharacterDatabase.Query(stmt))
                return;  // still queued — poll again next tick
            _awaitingFlush = false;
            LOG_INFO("playerbots.dungeonclear",
                     "TESTDRIVER character save landed — '{}' can log in now",
                     _resolvedName);
        }

        if (_initialized)
        {
            // A vanished driver (kick, crash recovery) re-arms the login so the
            // next EnsureOnline can bring it back.
            if (_loginIssued && !FindOnline())
            {
                _initialized = false;
                _loginIssued = false;
            }
            return;
        }
        if (!_loginIssued)
            return;

        Player* driver = FindOnline();
        if (!driver || !GET_PLAYERBOT_AI(driver))
            return;  // still loading — poll again next tick

        // One-time setup, in dependency order:
        // 1. Its own PlayerbotMgr, so GET_PLAYERBOT_MGR(driver) resolves for
        //    AddPlayerBot / LogoutPlayerBot (the AI map and mgr map are
        //    separate registries — both can exist for one guid).
        //
        //    NOTHING MAY HOLD A PlayerbotAI* ACROSS THIS CALL.
        //    PlayerbotsMgr::AddPlayerbotData(p, false) constructs the manager
        //    and then immediately runs PlayerbotMgr::OnPlayerLogin(p), which on
        //    a realm with AiPlayerbot.SelfBotLevel > 2 issues the `self`
        //    command for us. `self` is a TOGGLE, and on a character that
        //    already has a PlayerbotAI (which the driver does, installed by the
        //    login's own PlayerbotHolder::OnBotLogin) it takes the "off" half:
        //    `delete GET_PLAYERBOT_AI(master)`. ~PlayerbotAI deletes its three
        //    Engines and unregisters itself, so a PlayerbotAI* read before this
        //    line dangles after it, and ResetStrategies()'s unchecked
        //    `engines[i]->removeAllStrategies()` then calls through freed
        //    memory. That was a SIGSEGV on the driver's first tick in world,
        //    every single login, on any realm configured that way.
        if (!GET_PLAYERBOT_MGR(driver))
            sPlayerbotsMgr.AddPlayerbotData(driver, false);

        // 1b. Only now resolve the AI, and put one back if the `self` toggle
        //     above removed it. Everything below drives the driver through its
        //     AI, and so does every `.dc` command the harness issues, so a
        //     driver without one is not a driver.
        PlayerbotAI* ai = GET_PLAYERBOT_AI(driver);
        if (!ai)
        {
            sPlayerbotsMgr.AddPlayerbotData(driver, true);
            ai = GET_PLAYERBOT_AI(driver);
            if (!ai)
                return;  // unreachable while playerbots is enabled, and the
                         // gate above already required that
        }

        // 2. Self-mastered: the stock real-player-master gate resolves the
        //    master via IsSelfBot(master) (master == bot; pre-PR-2592 this was
        //    masterBotAI->IsRealPlayer()), so this one line is what keeps the
        //    stock fast path (react delay etc.) for every run the driver issues.
        ai->SetMaster(driver);

        // 3. Neutralize. The masterless login installed the random-bot
        //    strategy set (grind/travel/rpg) — re-derive for the now
        //    self-mastered "real player" first (the .playerbots self flow),
        //    then pin it in place. GM mode also drops it from mob
        //    threat/visibility entirely.
        // 2b. Actually a GM. The playerbots fake-session login hardcodes
        // SEC_PLAYER (PlayerbotMgr.cpp: `new WorldSession(..., SEC_PLAYER,
        // ...)`) regardless of the account's real gmlevel, so the driver
        // looked like a plain player to every security check — including the
        // dc commands' GM allowance, which is what lets the harness drive a
        // bot party it is deliberately not a member of. Without this, every
        // `dc on` the harness issued was refused and each run died at setup.
        // SetGameMaster below is only the GM *mode* flag; it does not touch
        // session security.
        if (WorldSession* session = driver->GetSession())
            session->SetSecurity(SEC_GAMEMASTER);

        ai->ResetStrategies();
        ai->ChangeStrategy("+stay", BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+passive", BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+passive", BOT_STATE_COMBAT);
        driver->SetGameMaster(true);

        _initialized = true;
        LOG_INFO("playerbots.dungeonclear",
                 "TESTDRIVER ready: '{}' online (account {}, security {}), self-mastered, "
                 "parked at map {} {:.1f} {:.1f} {:.1f}",
                 driver->GetName(), driver->GetSession()->GetAccountId(),
                 static_cast<uint32>(driver->GetSession()->GetSecurity()), driver->GetMapId(),
                 driver->GetPositionX(), driver->GetPositionY(), driver->GetPositionZ());
    }
}
