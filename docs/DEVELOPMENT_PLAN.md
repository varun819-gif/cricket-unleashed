# Development Plan

Status key: ✅ done · 🟡 in progress · ⬜ not started

| # | Milestone | Status | Notes |
|---|---|---|---|
| 1 | Hardware check, requirements, architecture | 🟡 | Architecture and gameplay core done and tested. Still needed: a dev PC with Unreal Engine 5 (see below) |
| 2 | Stadium, players, pitch, ball physics, camera | ⬜ | Needs Unreal and licensed or marketplace assets |
| 3 | Batting, bowling, scoring | 🟡 | Scoring done in core; batting/bowling input + animation pending |
| 4 | Fielding, wicketkeeping, cricket rules | 🟡 | Cricket rules milestone complete (DRS, sessions, rain, save); fielding gameplay needs physics |
| 5 | Single-player AI | 🟡 | AI captain done; batting/bowling AI needs physics |
| 6 | Captaincy, toss, field placement | 🟡 | Logic done; cinematics + field editor UI pending |
| 7 | Mobile QR pairing and controller | ⬜ | |
| 8 | Multiplayer server, matchmaking, rooms | 🟡 | Authoritative session core done; transport, backend pending |
| 9 | Commentary, crowd, stadium entertainment | 🟡 | Text commentary EN/HI done; audio pending |
| 10 | Tournaments, profiles, leaderboard | ⬜ | Design written |
| 11 | T20, ODI, Test complete | ✅ (rules) | Formats, Test sessions/days, rain + non-official targets, save/resume done; presentation pending with Unreal |
| 12 | Career, achievements, progression | ⬜ | |
| 13 | Cinematics, animation, replays, optimisation | ⬜ | |
| 14 | Android TV and PC builds | ⬜ | Needs real devices |
| 15 | Testing, security, release prep | ⬜ | |

## Cricket rules milestone (Priority 1)

| Step | Feature | Status |
|---|---|---|
| A | DRS complete: hit wicket, obstruction, wide / no-ball reviews, boundary checks, perception-based AI, Hindi reasons, short runs, penalty runs, new ball | ✅ `19bd183` |
| B | Test sessions: lunch / tea / stumps, days, changeovers, match end | ✅ `64a107c` |
| C | Rain: non-official CU Rain Rule targets, minimum overs, no-result, Test time lost | ✅ `883f6b9` |
| D | Save / resume, validation, backups, crash recovery | ✅ `b1cb181` |

Next candidates (no approval assumed): Priority 2 (mobile controller web app
with QR pairing) and Priority 3 (multiplayer backend architecture for India
vs Pakistan matches), as agreed.

## Milestone 1: what is still needed from the team

This cloud workspace is a Linux container with **no GPU and no Unreal Engine**,
so the 3D work can't start here. To continue:

1. **Dev PC** (minimum): Windows 11, 8+ core CPU, 64 GB RAM, RTX 3070-class
   GPU or better, 300 GB+ NVMe SSD.
2. Install **Unreal Engine 5** (latest stable from the Epic Games Launcher),
   Visual Studio 2022 with the "Game development with C++" workload, and
   Android Studio + NDK (for Android TV) as listed in Epic's docs for that UE
   version.
3. At least one **real Android TV device** (for example a mid-range Google TV
   box) for performance testing.
4. **Approve spending** for any assets, servers or services. Nothing has been
   bought.

## Next steps (proposed order)

1. Create the Unreal project `CricketUnleashed` and add `cricket_core` as a
   C++ module (`Source/CricketCore`) built from these same sources.
2. Grey-box stadium + pitch + ball physics (Chaos, fixed timestep). Replace
   `QuickSim` with physics-resolved `Delivery`s.
3. Batting and bowling input on gamepad (timing, direction, line/length).
4. Headless dedicated server build running `MatchSession` per room.
5. Phone controller: TV shows a QR code with a one-time pairing token; phone
   joins over local WebSocket.

## Decisions for the backend (proposal, nothing bought)

- **PostgreSQL**: accounts, profiles, match results, tournaments
  (source of truth).
- **Redis**: matchmaking queues, room codes, presence, leaderboard sorted sets
  (rebuildable from Postgres).
- **Game servers**: Unreal dedicated servers on Linux containers managed by an
  orchestrator such as Agones on Kubernetes, or a hosted fleet service. Choose
  after cost comparison.
- **Auth**: OAuth/OpenID provider (Google sign-in fits Android TV), short-lived
  JWTs, passwords (if any) hashed with Argon2id.
