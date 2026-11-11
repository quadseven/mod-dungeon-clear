/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// SDK adapters only. The generator appends production registry, roster,
// resurrection-offer and watchdog bodies unchanged; no decision is copied here.
#include "DcNoKillDecision.h"
#include "DcPartyState.h"
#include "DcSameInstance.h"
#include "Player.h"
#include "gtest/gtest.h"
#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <vector>
using uint64 = std::uint64_t;
struct GroupReference
{
    Player* player = nullptr;
    GroupReference* following = nullptr;
    Player* GetSource() const { return player; }
    GroupReference* next() const { return following; }
};
class Group
{
public:
    std::array<GroupReference, 5> refs;
    unsigned size = 0;
    bool raid = false;
    bool isRaidGroup() const { return raid; }
    void Add(Player* player)
    {
        if (size)
            refs[size - 1].following = &refs[size];
        refs[size++].player = player;
        player->group = this;
    }
    GroupReference const* GetFirstMember() const { return size ? &refs[0] : nullptr; }
    GroupReference* GetFirstMember() { return size ? &refs[0] : nullptr; }
};

using uint32 = unsigned;
using ObjectGuid = unsigned;
struct DungeonBossInfo
{
    std::string name;
};
struct DcRunState
{
    bool enabled = true, paused = false;
    struct
    {
        unsigned stampMs = 0;
    } killProgress;
};
struct Context
{
    std::optional<DungeonBossInfo> next;
    template <class T> struct Value
    {
        T value;
        T Get() const { return value; }
    };
    Value<std::optional<DungeonBossInfo>> value;
    template <class T> Value<T>* GetValue(char const*)
    {
        value.value = next;
        return &value;
    }
};
class PlayerbotAI
{
public:
    Context context;
    DcRunState run;
    static bool IsHeal(Player* player) { return player->heals; }
    Context* GetAiObjectContext() { return &context; }
};
static std::map<unsigned, Player*> players;
static std::map<Player*, PlayerbotAI*> ais;
static PlayerbotAI* stopped = nullptr;
static Player* foreignOwner = nullptr;
static std::string stopReason;
static bool standDown = false, chess = false;
#define GET_PLAYERBOT_AI(player) ais[player]
#define LOG_INFO(...) ((void)0)
namespace ObjectAccessor
{
    Player* FindPlayer(ObjectGuid id) { return players[id]; }
} // namespace ObjectAccessor
class DcStatusPublisher
{
public:
    static void MarkActiveTank(ObjectGuid);
    static void UnmarkActiveTank(ObjectGuid);
    static std::vector<ObjectGuid> ActiveTanks();
};
namespace DcRun
{
    DcRunState& Of(PlayerbotAI* ai) { return ai->run; }
} // namespace DcRun
namespace DcKey
{
    constexpr auto NextDungeonBoss = "next";
}
namespace DcSettings
{
    unsigned GetUInt(Player*, char const*) { return 1800; }
} // namespace DcSettings
namespace DcRunProgress
{
    template <class T> bool DetectKill(Player*, PlayerbotAI*, T&) { return false; }
} // namespace DcRunProgress
namespace DcBossStandDown
{
    bool IsActive(Player*) { return standDown; }
} // namespace DcBossStandDown
namespace DcKarazhan
{
    bool ChessIsOn(Player*) { return chess; }
} // namespace DcKarazhan
namespace DcRezRecovery
{
    void const* KnownResurrectSpell(Player const* p) { return p->resurrect ? p : nullptr; }
} // namespace DcRezRecovery
namespace DcLeaderSignal
{
    Player* FindRunOwner(Player*) { return foreignOwner; }
} // namespace DcLeaderSignal
namespace DcActionShared
{
    void DisableDungeonClear(PlayerbotAI* ai, std::string const& reason)
    {
        stopped = ai;
        stopReason = reason;
        ai->run.enabled = false;
    }
} // namespace DcActionShared
unsigned getMSTime() { return 10000; }
