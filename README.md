# Cricket Unleashed

A premium, photorealistic 3D cricket game for **Android TV, Windows PC** and a
**phone-as-controller** web app. It will have single player, online head-to-head,
private rooms, ranked matchmaking, tournaments and global leaderboards.

> **Status: Milestone 1 core done; Priority 1 (missing rules) in progress - Step 1 DRS done.** This repository has the
> **engine-independent gameplay core**: rules, scoring, toss, field
> restrictions, AI captain, commentary and the server-side match session. It is
> tested, and it runs full T20, ODI and Test matches in text mode. The Unreal
> Engine client has **not** been started yet. See [docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md).

## Why the core comes first

The rules of cricket must be the same on the TV, on the PC and on the
multiplayer server. So they live in one plain C++20 library (`core/`) with no
Unreal code. Then:

- the **dedicated server** runs it to decide every score (players can't cheat),
- the **Unreal client** links the same code as a module for single player and
  prediction,
- **tests** run in seconds on any machine.

## Build and run

Needs CMake 3.20+ and a C++20 compiler (GCC 11+, Clang 14+ or MSVC 2022).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/cricket_tests                      # 65 tests
./build/cricket_sim t20 0 1                # AI vs AI T20 with commentary
./build/cricket_sim odi 2 3 --quiet        # scorecard only
./build/cricket_sim test 4 5 --hindi       # Test match, Hindi commentary
./build/cricket_sim overs=5 --seed 42      # 5-over game, repeatable
./build/cricket_sim --teams                # list the fictional teams
```

## What works today

| Area | Done |
|---|---|
| Formats | T20, ODI, Test (5/4/3-day), custom overs |
| Scoring | runs, boundaries, wides, no-balls, byes, leg-byes, maidens, milestones |
| Laws | free hit, which dismissals are allowed on each kind of ball, strike rotation, new batter's end, no bowler bowls two overs in a row, bowler over limits, all out, target, declarations, follow-on, innings wins, ties, draws when time runs out |
| LBW / DRS | Law 36 LBW plus full DRS: player reviews for caught and LBW (edge, clean catch, ball tracking, caught→LBW check), third-umpire referrals for run outs, stumpings and clean catches, automatic front-foot no-ball check, 2 unsuccessful reviews per innings, dead-ball run correction, play held until decided. See [docs/DRS_DESIGN.md](docs/DRS_DESIGN.md) |
| Fielding | 26 named positions, 10 presets, manual placement, 30-yard circle, powerplay limits, max 2 behind square on the leg side, no one on the pitch or past the boundary |
| Toss | only the calling captain calls, only the winner decides, server-seeded coin |
| AI | bowling changes (fatigue, death overs, powerplay), batting order, fields, follow-on, declarations, toss |
| Commentary | lead + expert lines in English and Hindi, built only from verified ball events, never the same line twice in a row |
| Multiplayer core | server-authoritative session: join sides, rejects duplicate and out-of-turn commands, ordered event log so a reconnecting client can catch up |
| Teams | 10 fictional international sides (no licensed names or likenesses) |

## Repository layout

```
core/include/cricket/   public headers (Types, MatchRules, Innings, Match, Toss,
                        Lbw, Drs, FieldPlacement, AutoCaptain, Commentary,
                        MatchSession, TeamFactory)
core/src/               implementation
core/tests/             unit + full-match simulation tests
tools/sim/              text-mode match simulator
docs/                   design documents
```

## Documents

- [Development plan](docs/DEVELOPMENT_PLAN.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Multiplayer design](docs/MULTIPLAYER_DESIGN.md)
- [DRS design](docs/DRS_DESIGN.md)
- [Commentary design](docs/COMMENTARY_DESIGN.md)
- [Leaderboard design](docs/LEADERBOARD_DESIGN.md)
- [Asset register (licensing)](docs/ASSET_REGISTER.md)
- [Test report](docs/TEST_REPORT.md)
- [Known issues](KNOWN_ISSUES.md)
- [Changelog](CHANGELOG.md)

## Licensing rule

Teams, players and tournaments are **fictional** until licences are signed.
Real national team names, logos, kits, player likenesses, tournament branding and
copyrighted music are added only through entries in
[docs/ASSET_REGISTER.md](docs/ASSET_REGISTER.md).
