# Cricket Unleashed

A premium, photorealistic 3D cricket game for **Android TV, Windows PC** and a
**phone-as-controller** web app. It will have single player, online head-to-head,
private rooms, ranked matchmaking, tournaments and global leaderboards.

> **Status: cricket rules milestone complete; Priority 2 (mobile controller) complete; next: Priority 3 (online multiplayer backend).** This repository has the
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
./build/cricket_tests                      # 217 C++ tests
(cd build && ctest)                        # C++ + phone-side JavaScript tests
./build/cricket_controller_host            # play with your phone: scan the QR it prints
./build/cricket_sim t20 0 1                # AI vs AI T20 with commentary
./build/cricket_sim odi 2 3 --quiet        # scorecard only
./build/cricket_sim test 4 5 --hindi       # Test match, Hindi commentary
./build/cricket_sim overs=5 --seed 42      # 5-over game, repeatable
./build/cricket_sim --teams                # list the fictional teams
./build/cricket_sim t20 --drs no-reviews   # DRS ruleset: icc | no-reviews | no-tech | custom:R:S[:lose][:calls]
./build/cricket_sim odi --rain             # random rain delays, non-official rain-rule targets
./build/cricket_server --port 8080         # online server: open http://localhost:8080/
```

## What works today

| Area | Done |
|---|---|
| Formats | T20, ODI, Test (5/4/3-day), custom overs |
| Scoring | runs, boundaries, wides, no-balls (bat runs, byes and leg-byes per Law 21.12), byes, leg-byes, short runs (Law 18.4), penalty runs (Law 41), maidens, milestones |
| Laws | free hit, which dismissals are allowed on each kind of ball, strike rotation, new batter's end, no bowler bowls two overs in a row, bowler over limits, all out, target, declarations, follow-on (margin by days left), innings wins, ties, draws, new ball after 80 overs |
| Test sessions | 90-over days, lunch / tea / stumps (tea / dinner day-night), changeovers, early interval near a break, day progression, draw at the last day's stumps; online play pauses at breaks. See [docs/TEST_SESSIONS.md](docs/TEST_SESSIONS.md) |
| Rain | lost time → overs, equal cuts, revised targets with our **non-official CU Rain Rule** (not DLS), par scores, minimum overs, no-result, Test time lost and made up. See [docs/RAIN_RULE.md](docs/RAIN_RULE.md) |
| Save / resume | versioned, checksummed saves for single matches and online sessions; exact replay; backup fallback; crash log recovery. See [docs/SAVE_AND_RESUME.md](docs/SAVE_AND_RESUME.md) |
| LBW / DRS | Law 36 LBW plus DRS rules: player reviews for caught, LBW and hit wicket (edge, clean catch, ball tracking, caught→LBW check); third-umpire referrals for run outs, stumpings, clean catches, hit wicket, obstruction and boundaries; optional wide / height no-ball reviews; automatic front-foot no-ball check; **configurable rulesets**; **server-side 15-second review timer**; correct runs when a decision changes; reasons in English and Hindi. ⚠️ Ball tracking and edge detection are **simulated placeholders**, not real DRS technology. See [docs/DRS_DESIGN.md](docs/DRS_DESIGN.md) |
| Fielding | 26 named positions, 10 presets, manual placement, 30-yard circle, powerplay limits, max 2 behind square on the leg side, no one on the pitch or past the boundary |
| Toss | only the calling captain calls, only the winner decides, server-seeded coin |
| AI | bowling changes (fatigue, death overs, powerplay), batting order, fields, follow-on, declarations, toss, new ball, DRS reviews from players' perception and match context (never sees the evidence; personalities; difficulty) |
| Commentary | lead + expert lines in English and Hindi, built only from verified ball events, never the same line twice in a row |
| Multiplayer core | server-authoritative session: join sides, rejects duplicate and out-of-turn commands, review timer, break pauses, weather and penalty actions, ordered event log so a reconnecting client can catch up, crash recovery from the session journal |
| Teams | 10 international sides with real country names and flags (India, Pakistan, ...); **players are fictional**; no board logos or kits |
| **Phone controller** | QR pairing over home Wi-Fi, secret never sent over the network, signed + numbered messages, captain / player / spectator controls (toss, captaincy, review, bowling, batting, fielding), automatic reconnection with the game waiting for you, TV page with QR codes. Ball outcomes are **simulated placeholders** until Unreal physics. See [docs/MOBILE_CONTROLLER.md](docs/MOBILE_CONTROLLER.md) and [docs/CONNECT_YOUR_PHONE.md](docs/CONNECT_YOUR_PHONE.md) |
| **Online multiplayer** (preview) | self-hosted `cricket_server` (₹0): guest players (no password), private rooms with 6-letter codes, quick + ranked matchmaking, Elo ratings, server-decided scores, reconnection (match waits 2 min, then ranked forfeit / casual AI takeover), anti-cheat timing check, duplicate-message protection, crash recovery, history, stats, leaderboards. ⚠️ Tested on one machine only - **the two-device internet test is still pending**. See [docs/ONLINE_MULTIPLAYER.md](docs/ONLINE_MULTIPLAYER.md) and [docs/ONLINE_SERVER_SETUP.md](docs/ONLINE_SERVER_SETUP.md) |

## Repository layout

```
core/include/cricket/   public headers (Types, MatchRules, Innings, Match, Toss,
                        Lbw, Drs, ReviewAI, RainRule, FieldPlacement,
                        AutoCaptain, Commentary, MatchSession, SaveGame,
                        TeamFactory)
core/src/               implementation
core/tests/             unit + full-match simulation tests
controller/             phone controller: C++ hub, match host, local server, QR, crypto
controller/web/         the phone page (HTML/CSS/JS) and its tests
tools/controller_host/  TV/PC program for phone play (stands in for the game)
tools/sim/              text-mode match simulator
docs/                   design documents
```

## Documents

- [Development plan](docs/DEVELOPMENT_PLAN.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Multiplayer design](docs/MULTIPLAYER_DESIGN.md)
- [DRS design](docs/DRS_DESIGN.md)
- [Test match sessions](docs/TEST_SESSIONS.md)
- [Rain rule (non-official)](docs/RAIN_RULE.md)
- [Save and resume](docs/SAVE_AND_RESUME.md)
- [Mobile controller](docs/MOBILE_CONTROLLER.md) · [Connect your phone](docs/CONNECT_YOUR_PHONE.md)
- [Online multiplayer](docs/ONLINE_MULTIPLAYER.md) · [Run the server for ₹0 + internet test](docs/ONLINE_SERVER_SETUP.md)
- [Moderation](docs/MODERATION.md) · [Account linking (future)](docs/ACCOUNT_LINKING.md) · [Priority 3 closure report](docs/P3_CLOSURE_REPORT.md)
- [Unreal Engine development plan](docs/UNREAL_DEVELOPMENT_PLAN.md)
- [Missing details and assumptions](docs/MISSING_DETAILS.md)
- [Commentary design](docs/COMMENTARY_DESIGN.md)
- [Leaderboard design](docs/LEADERBOARD_DESIGN.md)
- [Asset register (licensing)](docs/ASSET_REGISTER.md)
- [Test report](docs/TEST_REPORT.md)
- [Known issues](KNOWN_ISSUES.md)
- [Changelog](CHANGELOG.md)

## Licensing rule

Country names and national flags are used (permitted flag assets, see ASSET_REGISTER). Players and tournaments are **fictional** until licences are signed.
Real national team names, logos, kits, player likenesses, tournament branding and
copyrighted music are added only through entries in
[docs/ASSET_REGISTER.md](docs/ASSET_REGISTER.md).
