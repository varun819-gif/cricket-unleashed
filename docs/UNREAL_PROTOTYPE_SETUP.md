# Unreal Engine: first 3D prototype - PC check and setup

Date: 2026-11-21. Status: **prepared, NOT built.** No Unreal Engine is
installed anywhere this project has run. The files in `unreal/` were
written without Unreal and have never been compiled by it.

## 1. Can your PC run Unreal Engine 5?

No PC is connected to this workspace, so I could not read your hardware
directly. Based on the laptop you described (Intel i3 11th gen, 8 GB RAM,
Intel UHD graphics, NVMe SSD):

| Part | Your laptop | Unreal Engine 5 (Epic's recommended) | OK? |
|---|---|---|---|
| Processor | i3 11th gen (2 cores / 4 threads) | 4+ cores, 2.5 GHz+ | ✘ too few cores |
| Memory | 8 GB | 32 GB (16 GB = bare minimum) | ✘ |
| Graphics | Intel UHD, built-in, no own memory | DirectX 12 card with 8 GB | ✘ biggest problem |
| Disk | NVMe SSD | SSD with 100 GB+ free | ✔ (check free space) |

**Verdict: not practical for Unreal 3D work.** The editor may open an
empty level, but building shaders would take hours and a pitch with
players and lighting would crawl or crash. Nothing small (like more RAM)
fixes it, because the built-in graphics is the main limit.

The laptop is **fine for everything else**: the game server, phone
controller, single player in the browser, physics and rules work, tests.

**To check any PC yourself (free, read-only):** copy the folder
`tools/pc_check` to that PC and double-click `check_pc.bat`. It reads the
processor, memory, graphics card, graphics memory and disk space, prints a
verdict and saves `pc_check_result.txt` - send me that file. It installs
and changes nothing. (Written for Windows 10/11 PowerShell; it could not
be run here - tell me if it shows an error.)

I am not asking you to buy anything. When a capable PC is available (your
own later, a friend's, a college lab), the steps below take you from zero to
the first 3D ball.

## 2. What is ready in the project

| Piece | Where | State |
|---|---|---|
| Rules, DRS, scorecards, ball physics, recording | `core/` | built and tested every commit (Linux) |
| Unreal compile settings check | `cricket_unreal_compat_check` | every build: no exceptions, no RTTI, and a check that no core code uses names Unreal reserves as macros (`check`, `verify`, `ensure`, `TEXT`, `PI` ...) |
| Unreal project | `unreal/CricketUnleashed/CricketUnleashed.uproject` | skeleton, untested |
| CricketCore plugin | `unreal/CricketUnleashed/Plugins/CricketCore` | wraps the core; `SimulateDelivery` and `LoadRecordedBall` for Blueprints; untested |
| Ball replay actor | `unreal/CricketUnleashed/Source/CricketUnleashed/BallReplayActor.*` | moves a ball mesh along a physics path; untested |
| Core → plugin copy | `tools/unreal/sync_core.ps1` (Windows) / `.sh` | copies `core/` into the plugin |
| Match recordings | `<id>.replay.jsonl` (server), `cricket_sim --record FILE` | every ball's flight every 10 ms, the shot, the path after the shot, highlights - what Unreal replays |

Coordinates: the core uses metres, origin at the striker's middle stump,
+y towards the bowler, +x off side, +z up. Unreal uses centimetres:
`Unreal (X, Y, Z) = 100 × (core y, core x, core z)` (X towards the bowler).

## 3. Steps on a capable Windows PC (all free)

1. Install **Epic Games Launcher** → Unreal Engine **5.4 or newer**.
2. Install **Visual Studio 2022 Community** with "Game development with
   C++" (and the Windows SDK it suggests).
3. Install **Git for Windows**, then:
   ```
   git clone -b ccr-1fd537b3-uv7e9l https://github.com/varun819-gif/cricket-unleashed.git
   cd cricket-unleashed
   powershell -ExecutionPolicy Bypass -File tools\unreal\sync_core.ps1
   ```
4. Right-click `unreal\CricketUnleashed\CricketUnleashed.uproject` →
   *Generate Visual Studio project files*. Open the `.sln`, build
   **Development Editor**. (If 5.4 isn't installed, right-click → *Switch
   Unreal Engine version*.)
5. Open the project. First test (P1 step 1):
   - New level; add a plane (pitch, 20 m long), place three thin cylinders
     at the striker's end (stumps) - their base is `StrikerStumps`.
   - Place **BallReplayActor**, give it the engine's sphere mesh scaled to
     7.2 cm (0.072).
   - Level Blueprint, on BeginPlay: `SimulateDelivery(140, 6, 0.1, 15)` →
     `PlayPath` on the actor.
   - Press Play: the ball should leave the bowler's hand at ~2 m height,
     swing, bounce about 6 m in front of the stumps and pass the stumps in
     about half a second.
6. Second test: `cricket_sim --record match.replay.jsonl` (in WSL), copy the
   file, `LoadRecordedBall(file, 25, Flight, AfterShot, Summary)` → play
   `Flight` then `AfterShot` → a real ball from a simulated match, shot and
   all.
7. Send me any compile errors exactly as shown (the first one matters most).

Then the P1 plan in [3D_DEVELOPMENT_PLAN.md](3D_DEVELOPMENT_PLAN.md)
section 6 continues: bowler/batter placeholders, cameras, HUD, input.

## 4. Known risks in the untested Unreal files

- The plugin and actor code has never been compiled by Unreal: expect a
  few small fixes (include paths, a warning treated as an error).
- MSVC must give the same physics numbers as GCC/Clang (`PathHash` test);
  this will be checked on the first Windows build.
- `bUseUnity = false` makes the plugin build a little slower but avoids
  name clashes between core files.

## 5. Cricket Creator Studio (remembered)

The future Creator Studio (record matches, replays, YouTube highlights)
uses the same recordings: see [CREATOR_STUDIO.md](CREATOR_STUDIO.md).
