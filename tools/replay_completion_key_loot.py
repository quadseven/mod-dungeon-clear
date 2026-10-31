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
prefix = r'''
#include "DcCompletionLoot.h"
#include <cassert>
#include <iostream>
struct Event {};
struct Player {};
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
suffix = r'''
int main()
{
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
    cpp.write_text(prefix + action + suffix)
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-I",
         str(root / "src/Ai/Dungeon/DungeonClear/Util"), str(cpp), "-o", str(binary)],
        check=True,
        timeout=30,
    )
    subprocess.run([str(binary)], check=True, timeout=5)
print("PASS: terminal completion yields for key loot, then finishes")
