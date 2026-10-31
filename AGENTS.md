# mod-dungeon-clear

An AzerothCore module that lets a mod-playerbots tank bot clear a dungeon on its own. This repo is the quadseven fork of `jrad7/mod-dungeon-clear`, and the realm's worldserver runs it. `CLAUDE.md` holds the fork's development workflow and is binding here; `README.md` holds the player's guide.

This repo is public. Follow the [public repo rules](https://github.com/quadseven/infra-public/blob/main/templates/public-repo/AGENTS.md): no personal names, private hostnames, private IPs, user paths or credentials on any surface, including issues and PR bodies. Say "the operator".

## Branches

- `quadseven/prod` is the default and production branch: upstream master plus every production fix. Branch from it and open every PR against it. The fork's `master` has diverged from it, so never target `master`.
- GitHub treats this repo as a fork, so pass `-R quadseven/mod-dungeon-clear` to `gh pr create`.
- An upstream sync is a merge into `quadseven/prod`, never a rebase, so no production commit changes its SHA.

## Layout

- `src/`: the module. `src/TestRun/` is the `.dc test` harness; `src/Lab/` is the Pull Lab.
- `t/`: the gtest suite, built as `dungeon_clear_tests`. `t/fixtures/`: replay and nav fixtures.
- `tools/`: offline developer scripts and CI lints, never compiled in. `tools/README.md` lists them.
- `testdeck/`: the Python and web Test Deck, with its own suite. `lab/`: Pull Lab scenarios.
- `conf/mod_dungeon_clear.conf.dist`: every setting.

`CLAUDE.md` names `deployment-files/` and two permanent `.claude/worktrees/` checkouts. Those are not in this repo.

## Build, test, lint

The build-free checks run from the repo root in seconds and match the first steps of `tests.yml`:

    python3 -B tools/replay_completion_key_loot.py
    bash tools/check_determinism.sh
    python3 tools/check_config_reads.py
    python3 tools/check_msvc_portability.py

The gtest suite links the full core `game` library, so it needs a complete tree: the core fork at the root, with mod-playerbots and this module under `modules/`. A cold build takes about an hour, and a lane sandbox lacks the toolchain, so CI runs it there. `README.md` ("Run and test locally") has the commands, and the `tests.yml` job matrix has the pins. `t/run_tests.sh` assumes that tree and uses a wider filter than CI.

For `testdeck/`, run `bash testdeck/t/run_tests.sh` from a checkout with its dev dependencies installed. Its Python floor is 3.9. `dist/` is committed, and CI fails when it differs from what `web/src` builds to, so rebuild and commit it.

For a test-run id (`tr-...` or `tp-...`), start with `python3 tools/dc_test_run.py <id>`, not grep.

## Checks on a pull request

Required by ruleset on `quadseven/prod`: `Grug - Chief` and `Grug - Elder`. Nothing else is required, but the merge gate below binds these:

- `dc-fast (gtest, deployed)`: every PR. It must pass.
- `dc-fast (gtest, previous)`: every PR, and allowed to fail. Treat a break as a warning.
- `msvc compile (windows-latest)`: only when `src/`, `t/` or CMake files change.
- `testdeck` jobs: only when `testdeck/` changes.

`upstream-smoke.yml` runs nightly on a self-hosted runner and is not a PR check.

## Traps

- The `tests.yml` filter and the `t/run_tests.sh` filter both list suites by name. A per-dungeon suite that matches neither pattern is skipped silently, so name it or add it to the filter.
- `tests.yml` pins core and mod-playerbots in two matrix legs. Move both legs when the deployed pins move.
- Sources are globbed at configure time. After adding or removing a `.cpp`, touch `CMakeLists.txt` to force a re-glob.
- Tunables are read through `DcSettings`, decision cores use no RNG primitives, and code must build under MSVC. The three lints above enforce these.
- `.gitattributes` forces LF.

## How it ships

Merging deploys nothing. A private deploy repo pins the module SHA, and a worldserver roll logs every bot out. The procedure is `docs/ROLLS.md` in `quadseven/wow-overseer`. Label a merged, undeployed ticket `loom:merged`; `loom:shipped` means it runs on the realm and its proof was seen.

## How work moves here

Several AI brains work this repo beside the operator: Claude Code, Muse, Dot and the Loom factory. Loom has a station that dispatches, a brain that briefs and merges, and lanes that build. No brain outranks another. Each acts under its own GitHub App, never a person's account, and signs posts `-- <name>`.

**Claims.** Put `claim:<name>` on a ticket before starting, and create the label if this repo lacks it. Check first and skip a ticket already held. One claim per ticket: the station keeps the earliest and removes the rest. A claim expires after 2 hours with no real activity, unless a comment holds it with `waiting: <what> until <time>`, or an open PR the claimer wrote and referenced holds it for up to 7 days. `claim:quadseven` is a person: leave those tickets alone. On stopping, push the work and say what is done, what is next, and the branch and head. Take over only an expired or plainly dead claim, after reading the holder's comments and open PRs.

**Labels.** Loom sets one state label per ticket:

`loom:triage` (not ready) -> `loom:queued` (no lane) -> `loom:briefed` + `lane:<name>` -> `loom:running` -> `loom:pr` -> `loom:merged` -> `loom:shipped` -> `loom:verified`, then closed.

`loom:ticket` marks every ticket. `loom:failed` means the runner gave up; the reason is on the ticket. `wait:*` names why a ticket is held. `needs:human` is an ask for a person. `loom:factory` marks a fault in the factory itself.

**CI and required checks.** Required checks differ per repo, so read them live: `gh api repos/<owner>/<repo>/rules/branches/<default-branch>`. Every repo needs a PR and `Grug - Chief`; some add test jobs; some add `Grug - Elder`. Run each command in `.loom/validation.conf` before pushing, and read the failing workflow file before guessing at a red check.

- Shared workflows: none. Every workflow here lives in this repo.
- Leak scan: not called here, so nothing in CI catches a private name or a cross-repo ref. Read every diff and PR body for them before posting. Bare `#N` means this repo.
- Trust gate and other guards: not called here.
- Agent Apps cannot push `.github/workflows` changes.

**`grug`.** The bot is `grug-tribe[bot]`, driven by slash commands in a PR comment. Its handle takes no at-sign.

- `Grug - Chief` reads the PR body. It wants `## Why` (5+ words), `## Acceptance criteria` (3+ bullets), `## Out of scope`, `Size: XS|S|M|L`, and `Refs <owner>/<repo>#N` or `Refs #N` to a ticket that is an epic or sits under one. Text in HTML comments does not count. Closing keywords are out: tickets close when their boxes are ticked.
- A body with an agent marker (such as a session link) from an App author, or a Loom lane footer, gets a neutral Chief: `Hunt Plan skipped`. Neutral passes. A body with no marker gets the full check, so write the full shape every time.
- `Grug - Elder` reviews the code by LLM. `/grug improve` re-runs it. `/grug recheck` re-runs Chief only. Five pushes in ten minutes pause Elder until `/grug resume`.
- A degraded or partial review says `Degraded review (<reason>): not a verdict`. It is not a pass, whether it concludes failure or neutral. Run `/grug improve` once the backend recovers.
- `Grug - Guard` scans for security problems and is advisory unless the repo turns on blocking. Haunt, once called Sentinel, comments when a PR closes over an unresolved critical finding. It posts no check; answer its comment and re-verify.

**Merge gate.** Rulesets enforce less than this, and this binds: CI green, `Grug - Elder` success on the head (neutral is not enough), `Grug - Chief` success or neutral, not behind base, every bot thread answered, and a test that fails without the fix. `gh pr checks` does not show threads, so read `reviewThreads` by GraphQL and `pulls/<n>/reviews`. Anyone merges what they built. Loom is in shadow on this repo: it runs every gate and records each merge or close for the operator instead of taking it, until the operator graduates the repo. After graduation, Loom merges docs, test and app code tiers alone and holds deploy, security and cross-repo tiers for the operator. A change to how brains merge, close or rewrite needs a fleet chat line and a 5 minute wait. Never use `--admin`.

**Human line.** People do: permanent deletion of records, creating, rotating or entering credentials, access grants, new spending, accepting terms. People decide: another brain's role and the fleet goal text. A second brain must agree, in writing, to clear a lane demotion, lift another brain's hold, or reopen a sound close. Everything else needs nobody. Ask with `needs:human` and one exact question. A relayed yes is not a yes. Never print a credential.

**When stuck.** Labels and stages: `docs/BRAIN.md` in `quadseven/loom`. Chief's rules and every `/grug` verb: the README in `quadseven/grug`, whose `make chief-check` tests a body before posting. Fleet log: issue 30 in `quadseven/loom`. File a factory fault there as `loom:factory`. Poll `gh` at most once per 5 minutes.
