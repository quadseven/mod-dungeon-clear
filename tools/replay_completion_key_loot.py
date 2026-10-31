"""Compile the actual terminal action and check its loot handoff."""

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/Ai/Dungeon/DungeonClear/Action/DcEngageActions.cpp").read_text()
signature = "bool DungeonClearDisableOnClearedAction::Execute(Event /*event*/)"
start = source.index(signature)
opening = source.index("{", start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
action = source[start:end]
loot_source = (root / "src/Ai/Dungeon/DungeonClear/Util/DcLootPolicy.cpp").read_text()
owned_start = loot_source.index("            uint32 const key = DcCompletionLoot::KeyForChest(go->GetEntry());")
owned_end = loot_source.index("keep = false;", owned_start) + len("keep = false;")
owned_filter = loot_source[owned_start:owned_end]
pickup_start = source.index("    missing.erase(std::remove_if(missing.begin(), missing.end(), [&](ObjectGuid guid)", opening)
pickup_end = source.index("    GameObject* go =", pickup_start)
pickup = source[pickup_start:pickup_end]
prefix = r'''
#include "DcCompletionLoot.h"
#include <cassert>
using uint32 = std::uint32_t;
#include <iostream>
struct Event {};
#include <algorithm>
#include <map>
#include <vector>
using ObjectGuid = unsigned;
struct Player
{
    bool carried = false;
    bool banked = false;
    bool sameInstance = true;
    bool HasItemCount(unsigned, unsigned, bool bank) const
    { return carried || (bank && banked); }
};
struct PlayerbotAI {};
namespace DcActionShared { constexpr char kReasonAllCleared[] = "cleared"; }
unsigned disabled = 0;
void DisableDungeonClear(PlayerbotAI*, char const*) { ++disabled; }
PlayerbotAI* RunOwnerAI(Player*, PlayerbotAI* ai) { return ai; }
struct DungeonClearDisableOnClearedAction
{
    Player* bot = nullptr;
    PlayerbotAI* botAI = nullptr;
    bool waiting = false;
    bool WaitForKeyReward() { return waiting; }
    bool Execute(Event);
};
'''
owned_prefix = r'''
struct Chest { unsigned entry; unsigned GetEntry() const { return entry; } };
struct Holder { bool owns; bool HasItemCount(unsigned, unsigned, bool) const { return owns; } };
bool KeepChest(Chest* go, Holder* bot)
{
    bool keep = true;
'''
owned_suffix = r'''
    return keep;
}
'''
pickup_prefix = r'''
unsigned pickupLogs = 0;
#define LOG_INFO(...) (++pickupLogs)
namespace ObjectAccessor
{
    std::map<ObjectGuid, Player*> players;
    Player* FindPlayer(ObjectGuid id)
    {
        auto const it = players.find(id);
        return it == players.end() ? nullptr : it->second;
    }
}
bool DcSameInstance(Player*, Player* member) { return member->sameInstance; }
struct Run { unsigned completionKeyItem = 7146; Chest completionKeyChest{103821}; };
void Reconcile(std::vector<ObjectGuid>& missing, Run const& run, Player* bot)
{
'''
pickup_suffix = r'''
}
'''
suffix = r'''
int main()
{
    // AI-REVIEW 2026-10-31 [poolside/laguna-s-2.1] L1 (PR #92):
    // Compile the real filter, including bank ownership, rather than a copy.
    Holder held{true}, missing{false};
    for (unsigned entry : {103821u, 179516u})
    {
        Chest chest{entry};
        assert(!KeepChest(&chest, &held));
        assert(KeepChest(&chest, &missing));
    }
    Chest unrelated{12345u};
    assert(KeepChest(&unrelated, &held));
    // AI-REVIEW 2026-10-31 [poolside/laguna-s-2.1] H2 (PR #92):
    // Only actual carried keys in this instance justify a pickup success log.
    Player bankOnly, elsewhere, driver;
    bankOnly.banked = true;
    elsewhere.carried = true;
    elsewhere.sameInstance = false;
    ObjectAccessor::players = {{1, &bankOnly}, {2, &elsewhere}};
    std::vector<ObjectGuid> absent{1, 2, 3};
    Run run;
    Reconcile(absent, run, &driver);
    assert(absent.size() == 3 && pickupLogs == 0);
    bankOnly.carried = true;
    Reconcile(absent, run, &driver);
    assert((absent == std::vector<ObjectGuid>{2, 3}) && pickupLogs == 1);
    Reconcile(absent, run, &driver);
    assert(pickupLogs == 1);
    using namespace DcCompletionLoot;
    Inputs in;
    in.started = in.needsKey = in.chestPresent = true;
    in.yards = 30;
    in.lootRadius = 15;
    assert(Decide(in) == Step::Approach);
    in.busy = true;
    assert(Decide(in) == Step::Wait);
    in.busy = false;
    in.walking = true;
    assert(Decide(in) == Step::Wait);
    in.walking = false;
    in.yards = 14;
    assert(Decide(in) == Step::Loot);
    in.chestPresent = false;
    assert(Decide(in) == Step::Wait);
    in.elapsedMs = BUDGET_MS;
    assert(Decide(in) == Step::TimedOut);
    in.needsKey = false;
    assert(Decide(in) == Step::Finish);
    DungeonClearDisableOnClearedAction action;
    action.waiting = true;
    if (action.Execute(Event{}) || disabled)
    {
        std::cerr << "completion disabled the drive before required-key loot finished\n";
        return 1;
    }
    action.waiting = false;
    if (!action.Execute(Event{}) || disabled != 1)
    {
        std::cerr << "completion did not finish after the bounded loot handoff\n";
        return 1;
    }
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "completion.cpp"
    binary = Path(directory) / "completion"
    cpp.write_text(prefix + action + owned_prefix + owned_filter + owned_suffix +
                   pickup_prefix + pickup + pickup_suffix + suffix)
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-I",
         str(root / "src/Ai/Dungeon/DungeonClear/Util"), str(cpp), "-o", str(binary)],
        check=True,
        timeout=30,
    )
    subprocess.run([str(binary)], check=True, timeout=5)
print("PASS: terminal completion yields for key loot, then finishes")
