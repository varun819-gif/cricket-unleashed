# Architecture

## Principles

1. **Gameplay logic is separate from presentation.** The laws of cricket,
   scoring and match flow never depend on rendering, audio or input.
2. **The server is authoritative.** In online play only the dedicated server
   decides ball outcomes, scores and results. Clients send *intents*.
3. **One rules implementation.** The same `cricket_core` library is used by
   the Unreal client, the dedicated server, tools and tests.
4. **Modules talk through events.** UI, commentary, cameras and audio react to
   `DeliveryRecord`s and `SessionEvent`s. They never poke match state directly.

## Layers

```
+-------------------------------------------------------------+
|  Unreal Engine 5 client (PC / Android TV)                    |
|  UIManager  CameraDirector  CommentaryAudioManager  Crowd...  |
|  BattingSystem / BowlingSystem / FieldingSystem (input+anim)  |
|  BallPhysics (Chaos, deterministic sub-stepping)              |
+------------------------------+------------------------------+
                               | events / intents
+------------------------------v------------------------------+
|  cricket_core (this repo, plain C++20, no engine deps)       |
|  MatchRules  Innings  Match  Toss  Lbw  Drs  FieldPlacement  |
|  AutoCaptain(CricketAI)  Commentary  MatchSession            |
+------------------------------+------------------------------+
                               | same library
+------------------------------v------------------------------+
|  Dedicated match server (headless, Linux)                    |
|  MatchSession per room + server-side BallPhysics             |
|  Backend: auth, matchmaking, rooms, leaderboards (Postgres,  |
|  Redis)                                                      |
+-------------------------------------------------------------+
```

## Spec module to code mapping

| Spec module | Where it lives now / later |
|---|---|
| CricketRules | `MatchRules`, `Innings`, `Lbw`, `Drs` (done) |
| MatchManager | `Match` (done); Unreal `ACricketGameMode` wraps it (planned) |
| TossManager | `Toss` (done) + toss cinematic in Unreal (planned) |
| FieldPlacementManager | `FieldPlacement` (done) + overhead field editor UI (planned) |
| CaptaincyManager | `AutoCaptain` + `MatchSession` commands (done); Auto / Manual / Hybrid modes = who answers `WaitingFor()` (planned UI) |
| CricketAI | `AutoCaptain` (captaincy done); batting/bowling AI on top of physics (Milestone 5) |
| MultiplayerManager / NetworkManager | `MatchSession` authority core (done); transport + server (Milestone 8) |
| CommentaryEngine | `Commentary` text engine (done); `CommentaryAudioManager` (Milestone 9) |
| BallPhysics, Batting/Bowling/FieldingSystem | Unreal C++ (Milestones 2-4); `QuickSim` is a temporary statistical stand-in |
| TeamManager / PlayerManager | `Types`, `TeamFactory` (basic); squads, profiles, DB (Milestone 10) |
| TournamentManager, LeaderboardManager, PlayerProfileManager | backend services (Milestone 10), see LEADERBOARD_DESIGN.md |
| MobileController | web app + local WebSocket bridge (Milestone 7) |
| SaveSystem | serialise the command log + seed (replays rebuild state exactly) |
| CameraDirector, UIManager, StadiumEntertainmentManager, CrowdAudioManager | Unreal (Milestones 9, 13) |

## Key data flow for one ball

1. The fielding captain's `SetBowler` / `SetField` commands are checked by `MatchSession`.
2. Player inputs (bowling plan, shot timing) go to the **server** physics.
3. Physics resolves a `Delivery` (runs, extras, dismissal) and calls
   `MatchSession::ResolveDelivery`. For a close call it instead builds a
   `DecisionCase` (both outcomes + replay evidence) and calls
   `ResolveAppeal`. Play is held until the review / referral is decided (see
   DRS_DESIGN.md).
4. `Innings::Bowl` checks it against the laws and updates the scorecard. It
   returns a `DeliveryRecord`.
5. A `SessionEvent` is added to the log. Clients get it, play the replay and
   camera, and the commentary engine picks its lines from the same record.
   Both players hear the same events.

## Coordinates (field)

Metres. Origin is the striker's middle stump. +y points down the pitch, +x
points to the off side for a right-hander, mirrored for a left-hander. The
30-yard circle is the "stadium" shape: everything within 27.43 m of the
stump-to-stump line.
