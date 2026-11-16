"""Emit the production registered-owner watchdog with isolated SDK adapters.

The native CMake target compiles and runs this replay. Registry operations,
roster observation, offer detection and the full throttled sweep come from the
production files; only external game SDK collaborators use test adapters.
"""

from pathlib import Path
import sys


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def generate(root: Path) -> str:
    util = root / "src/Ai/Dungeon/DungeonClear/Util"
    fixture = root / "t/fixtures/watchdog_replay"
    status = (util / "DcStatusPublisher.cpp").read_text(encoding="utf-8")
    registry = status[status.index("    struct DcPushState"):]
    registry = registry[:registry.index("    std::mutex g_dcActiveTanksMutex;") +
                        len("    std::mutex g_dcActiveTanksMutex;")]
    parts = [(fixture / "prefix.cpp").read_text(encoding="utf-8"), "namespace {\n", registry, "}\n"]
    for signature in ("void DcStatusPublisher::MarkActiveTank(",
                      "void DcStatusPublisher::UnmarkActiveTank(",
                      "std::vector<ObjectGuid> DcStatusPublisher::ActiveTanks("):
        parts.append(function(status, signature))
    parts.append(function((util / "DcPartyState.cpp").read_text(encoding="utf-8"),
                          "DcPartyState::PartyPresence DcPartyState::GetPartyPresence("))
    parts.extend(["namespace DcRezRecovery {\n",
                  function((util / "DcRezRecovery.cpp").read_text(encoding="utf-8"), "    bool HoldsResurrectOffer("),
                  "}\n"])
    watchdog = (root / "src/DcNoKillWatchdog.cpp").read_text(encoding="utf-8")
    parts.append(watchdog[watchdog.index("namespace\n{"):])
    parts.append((fixture / "tests.cpp").read_text(encoding="utf-8"))
    return "\n".join(parts)


if __name__ == "__main__":
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generate(Path(__file__).resolve().parents[1]), encoding="utf-8")
