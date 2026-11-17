/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRunWing.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_set>

#include "AiObjectContext.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"

char const* DcRunWing::SourceName(Source source)
{
    switch (source)
    {
        case Source::Fallback: return "fallback";
        case Source::Lfg:      return "lfg";
        case Source::Explicit: return "explicit";
        case Source::None:     break;
    }
    return "none";
}

DcRunWing::Latch DcRunWing::LatchStore::Get(uint64 party, uint32 instanceId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _entries.find(std::make_pair(party, instanceId));
    return it != _entries.end() ? it->second.latch : Latch{};
}

bool DcRunWing::LatchStore::Put(uint64 party, uint32 instanceId, std::string const& token,
                                Source source, Latch* prior)
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::pair<uint64, uint32> const key(party, instanceId);
    auto it = _entries.find(key);
    Latch const current = it != _entries.end() ? it->second.latch : Latch{};
    if (prior)
        *prior = current;
    if (token.empty() || source == Source::None)
        return false;
    if (!ShouldReplace(current, instanceId, source))
        return current.token == token;

    _entries[key] = Entry{Latch{instanceId, token, source}, ++_nextSeq};
    if (_entries.size() > kMaxEntries)
    {
        auto oldest = _entries.begin();
        for (auto e = _entries.begin(); e != _entries.end(); ++e)
            if (e->second.seq < oldest->second.seq)
                oldest = e;
        _entries.erase(oldest);
    }
    return true;
}

void DcRunWing::LatchStore::DropFallback(uint64 party, uint32 instanceId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _entries.find(std::make_pair(party, instanceId));
    if (it != _entries.end() && it->second.latch.source == Source::Fallback)
        _entries.erase(it);
}

std::size_t DcRunWing::LatchStore::Size() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _entries.size();
}

std::string DcRunWing::ResolveFallback(DungeonWingLayout const& layout, char const* regionWing)
{
    if (regionWing && *regionWing)
        if (DungeonWing const* wing = DungeonWingRegistry::FindWing(layout, regionWing))
            return wing->token;
    if (DungeonWingRegistry::FindWing(layout, layout.defaultWing))
        return layout.defaultWing;
    return layout.wings.empty() ? std::string() : layout.wings.front().token;
}

std::vector<DungeonBossInfo> DcRunWing::FilterToWing(std::vector<DungeonBossInfo> const& bosses,
                                                     DungeonWing const& wing)
{
    std::unordered_set<uint32> const keep(wing.bossEntries.begin(), wing.bossEntries.end());
    std::vector<DungeonBossInfo> filtered;
    filtered.reserve(keep.size());
    for (DungeonBossInfo const& b : bosses)
        if (keep.count(b.entry))
            filtered.push_back(b);
    return filtered;
}

std::size_t DcRunWing::PickByProximity(DungeonWingLayout const& layout,
                                       std::vector<DungeonBossInfo> const& bosses,
                                       float x, float y, float z)
{
    std::vector<DungeonWing> const& wings = layout.wings;
    std::size_t bestWing = wings.size();
    float bestDistSq = std::numeric_limits<float>::max();
    for (std::size_t w = 0; w < wings.size(); ++w)
    {
        for (uint32 entry : wings[w].bossEntries)
        {
            for (DungeonBossInfo const& b : bosses)
            {
                if (b.entry != entry)
                    continue;
                float const dx = b.x - x;
                float const dy = b.y - y;
                float const dz = b.z - z;
                float const d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < bestDistSq)
                {
                    bestDistSq = d2;
                    bestWing = w;
                }
            }
        }
    }
    return bestWing;
}

bool DcRunWing::TerminalDone(DungeonWing const& wing, std::vector<DungeonBossInfo> const& bosses,
                             uint32 completedMask, bool terminalCorpse)
{
    if (!wing.terminalBossEntry)
        return false;
    for (DungeonBossInfo const& b : bosses)
    {
        if (b.entry != wing.terminalBossEntry || b.kind != DungeonAnchorKind::Boss)
            continue;
        if (b.encounterIndex < 32 && (completedMask & (1u << b.encounterIndex)))
            return true;
        return terminalCorpse;
    }
    return false;
}

namespace
{
    DungeonWingLayout const* ExplicitLayout(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || !bot->GetMap())
            return nullptr;
        DungeonWingLayout const* layout = DungeonWingRegistry::Get(bot->GetMapId());
        if (!layout || !layout->isolated || layout->select != WingSelect::Explicit ||
            layout->wings.empty())
            return nullptr;
        return layout;
    }

    // Every party's latch, for the life of the process.
    DcRunWing::LatchStore g_latches;

    // The latch key's party half: the group, or the bot itself in no group. No
    // member's identity enters it, so every member of a party reads one latch.
    uint64 PartyKey(Player* bot)
    {
        Group* group = bot->GetGroup();
        return group ? group->GetGUID().GetRawValue() : bot->GetGUID().GetRawValue();
    }

    // Where the party is, for the fallback's region read: the run owner while a
    // clear runs, the leader tank before one starts, else the bot itself.
    Player* PartyAnchor(Player* bot)
    {
        if (Player* owner = DcLeaderSignal::FindRunOwner(bot))
            return owner;
        if (Player* leader = DcLeaderSignal::FindLeaderTank(bot))
            if (GET_PLAYERBOT_AI(leader))
                return leader;
        return bot;
    }

    // Every party bot on the map re-derives its boss list (5s-cached) and next
    // boss on its next read, so a wing change takes effect at once.
    void RefreshPartyBossLists(Player* ref)
    {
        auto refresh = [](Player* member)
        {
            PlayerbotAI* ai = member ? GET_PLAYERBOT_AI(member) : nullptr;
            if (!ai)
                return;
            AiObjectContext* ctx = ai->GetAiObjectContext();
            ctx->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Reset();
            ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Reset();
        };
        Group* group = ref->GetGroup();
        if (!group)
        {
            refresh(ref);
            return;
        }
        for (GroupReference* it = group->GetFirstMember(); it; it = it->next())
            if (Player* member = it->GetSource(); member && member->GetMapId() == ref->GetMapId())
                refresh(member);
    }
}

DungeonWing const* DcRunWing::Resolve(Player* bot)
{
    DungeonWingLayout const* layout = ExplicitLayout(bot);
    if (!layout)
        return nullptr;

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    uint64 const party = PartyKey(bot);
    Latch const latch = g_latches.Get(party, instanceId);
    if (Holds(latch, instanceId))
        if (DungeonWing const* wing = DungeonWingRegistry::FindWing(*layout, latch.token))
            return wing;

    // Fallback: the wing whose region the party stands in, judged from the
    // anchor (the leader, where the party is) when it is on this map.
    Player* const anchor = PartyAnchor(bot);
    Player* where = anchor->GetMapId() == bot->GetMapId() ? anchor : bot;
    char const* region = DcNavPenaltyRegistry::WingRegionAt(
        bot->GetMapId(), where->GetPositionX(), where->GetPositionY(), where->GetPositionZ());
    DungeonWing const* wing = DungeonWingRegistry::FindWing(*layout, ResolveFallback(*layout, region));
    if (!wing)
        wing = &layout->wings.front();

    // Only while a clear runs: before `dc on` the fallback is recomputed from
    // where the party stands, and `dc on` itself latches it.
    if (Player* owner = DcLeaderSignal::FindRunOwner(bot))
        if (g_latches.Put(party, instanceId, wing->token, Source::Fallback))
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] run wing '{}' ({}) latched for instance {} from fallback ({})",
                     owner->GetName(), wing->token, wing->name, instanceId,
                     region ? std::string("standing in ") + region : std::string("default"));
    return wing;
}

std::string DcRunWing::FenceWing(Player* bot)
{
    DungeonWing const* wing = Resolve(bot);
    return wing ? wing->token : std::string();
}

std::string DcRunWing::ActiveWingToken(Player* bot)
{
    if (!bot || !bot->GetMap())
        return "";
    DungeonWingLayout const* layout = DungeonWingRegistry::Get(bot->GetMapId());
    if (!layout || !layout->isolated)
        return "";
    if (layout->select == WingSelect::Explicit)
        return FenceWing(bot);

    Player* owner = DcLeaderSignal::FindRunOwner(bot);
    PlayerbotAI* ai = GET_PLAYERBOT_AI(owner ? owner : bot);
    if (!ai)
        return "";
    std::vector<DungeonBossInfo> const& bosses =
        ai->GetAiObjectContext()->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();
    for (DungeonBossInfo const& b : bosses)
        if (DungeonWing const* wing = DungeonWingRegistry::WingOf(bot->GetMapId(), b.entry))
            return wing->token;
    return "";
}

bool DcRunWing::Set(Player* bot, DungeonWing const& wing, Source source)
{
    if (!ExplicitLayout(bot))
        return false;

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    Latch prior;
    if (!g_latches.Put(PartyKey(bot), instanceId, wing.token, source, &prior))
    {
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] run wing '{}' from {} ignored — '{}' already latched from {}",
                 bot->GetName(), wing.token, SourceName(source), prior.token,
                 SourceName(prior.source));
        return false;
    }
    // A lower source asking for the wing already latched changes nothing.
    if (!ShouldReplace(prior, instanceId, source))
        return true;

    bool const changed = !Holds(prior, instanceId) || prior.token != wing.token;
    LOG_INFO("playerbots.dungeonclear", "[DC:{}] run wing '{}' ({}) latched for instance {} from {}",
             bot->GetName(), wing.token, wing.name, instanceId, SourceName(source));
    if (changed)
        RefreshPartyBossLists(bot);
    return true;
}

void DcRunWing::ClearFallback(Player* bot)
{
    if (!bot || !bot->GetMap())
        return;
    g_latches.DropFallback(PartyKey(bot), bot->GetMap()->GetInstanceId());
}
