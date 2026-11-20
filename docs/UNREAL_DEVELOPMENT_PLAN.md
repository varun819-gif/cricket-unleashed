# Unreal Engine Development Plan

> **Superseded by [3D_DEVELOPMENT_PLAN.md](3D_DEVELOPMENT_PLAN.md)** (hardware assessment, costs, first 3D prototype).

Status: **plan only. No Unreal (3D) work has started.** Nothing has been
bought and no paid service is used.

## 1. Where we start from

| Ready now (tested) | Not started |
|---|---|
| Full cricket rules engine (T20, ODI, Test, DRS, rain, saves) | 3D stadium, players, ball, cameras |
| Phone controller (QR pairing, controls, reconnection) | Real ball physics (today: simulated placeholder `QuickSim`) |
| Online server (rooms, ranked, recovery, moderation) | Animations, crowd, broadcast graphics |
| Commentary text (English + Hindi) | Commentary audio |
| **Core compiles the way Unreal compiles C++** (no exceptions, no RTTI) - checked on every build by `cricket_unreal_compat_check` | Android TV packaging |

## 2. Costs and licences (₹0 rule)

- **Unreal Engine 5**: free to download and use. Epic's licence asks for a
  royalty only after a game earns a large amount (today 5% of gross revenue
  above USD 1 million per product - **check Epic's current EULA before
  release**). No payment is needed to develop.
- Visual Studio 2022 Community, Android Studio, Blender: free (VS Community
  has licence conditions for larger companies - check if that applies).
- Assets: start with Epic's free sample content and our own grey boxes.
  **Every asset is recorded in ASSET_REGISTER.md with its licence before
  use. Any paid asset, plug-in, voice actor, motion capture or service
  needs your approval first.**
- No board logos, kits or real player likenesses (ASSET_REGISTER.md).

## 3. What you need to provide (cannot be done in this cloud workspace)

This workspace is Linux with no graphics card, so it cannot run Unreal.

1. **A development PC** (minimum): Windows 11, 8-core CPU, 32 GB RAM (64 GB
   better), RTX 3060/3070-class graphics card, 300 GB+ free SSD.
2. Install (all free): **Epic Games Launcher → Unreal Engine 5** (latest
   stable), **Visual Studio 2022** with "Game development with C++",
   **Git**. Later: Android Studio + NDK for Android TV.
3. A free **Epic Games account** (needed to download Unreal).
4. Later: one real **Android TV / Google TV device** for testing.
5. A way for me to work on that PC (for example a Claude session on that
   machine), or you run the steps and send me results/screenshots.

## 4. Architecture (how the pieces fit)

```
 Unreal project "CricketUnleashed"
 ├─ Plugin CricketCore  ← compiles our core/ + controller/ C++ unchanged
 │    (rules, DRS, saves, AI captain, commentary text, phone hub)
 ├─ Game code (UE C++)
 │    ACricketGameMode   owns a MatchSession (the umpire and scorer)
 │    ABall              shows the ball; flight comes from the ball model
 │    ABatter/ABowler/AFielder   characters + animation
 │    ACameraDirector    broadcast and cinematic cameras
 │    UI (UMG)           scoreboards, DRS screens, menus
 │    Audio              crowd, commentary (EN/HI)
 │    Controller         QR on the TV + phone hub (local Wi-Fi)
 │    Online             WebSocket client to cricket_server
 └─ Content (maps, meshes, animations, sounds) - licences in ASSET_REGISTER
```

Key decision: **the ball physics model lives in our engine-independent
C++ core**, not only inside Unreal.
- It stays deterministic and testable here (no GPU needed).
- The online server can run the **same** model, so online results stay
  server-decided, without paying for heavy Unreal dedicated servers.
- Unreal shows the flight, bounce and bat contact the model computes, and
  adds visuals (animation, effects, cameras).

## 5. Milestones

Each milestone ends with tests and a short report, like Priorities 1-3.

| # | Milestone | Where | Done when |
|---|---|---|---|
| **U1** | **Ball physics model in core** (flight, drag, swing, seam, spin, bounce, pitch wear, bat contact, edges); replaces `QuickSim`; outcomes feed DRS ball tracking for real | **here** (can start now) | deterministic tests, sensible speeds/bounce vs real data, server uses it |
| **U2** | Unreal project + `CricketCore` plugin builds; a match plays in text inside Unreal (debug screen) | your PC | builds clean, Unreal automation test runs a full T20 |
| U3 | Grey-box stadium, pitch, stumps, ball rendering from U1, basic cameras (bowler's end, side-on) | your PC | ball looks right at 60 fps on the PC |
| U4 | Batting and bowling input: gamepad + the phone controller, timing feel | your PC | play a full over by hand |
| U5 | Characters and animation (Epic mannequin first; real animation later, licence/approval) | your PC | bowl, bat, run, field, celebrate |
| U6 | **Broadcast scoreboards**: batting card, bowling card, partnership, run rate, required rate, DRS and rain screens | your PC | matches the reference style you choose |
| U7 | **Commentary audio English + Hindi** (recordings or text-to-speech - licence and approval first) | both | lines play in time with events |
| U8 | Online in the 3D game: the TV joins `cricket_server`; phones on the Wi-Fi control it | both | the two-phone internet test, but on TVs |
| U9 | **Cinematic stadium**: crowd, lighting, day/night, replays, stadium entertainment (adult performers only, tasteful, can be switched off) | your PC | cinematic replay of a wicket |
| U10 | **Cricket Creator Studio**: record matches (our match journals already replay exactly), highlight builder, export video; YouTube upload needs a Google API project - your approval | both | 60-second highlight file exported |
| U11 | Android TV build and performance; Windows PC build | your PC + TV | 60 fps target on PC, 30+ on a mid-range TV box |

## 6. What I will do next (no Unreal needed)

1. U1 ball physics model in the core, with tests (starts now in this
   workspace).
2. Windows socket port of the local server (needed for the phone controller
   inside Unreal on Windows PCs), tested by cross-compiling.
3. Prepare the `CricketCore` plugin files (module description, build file)
   so U2 is quick - clearly marked "untested until built on your PC".

## 7. Risks

| Risk | How we handle it |
|---|---|
| No Unreal PC available | U1 and other core work continue here; 3D waits |
| Realistic animation needs motion capture or bought assets | start with the free mannequin; any purchase needs approval |
| Android TV performance | test early on a real device (U11 can start in parallel after U3) |
| Licence mistakes (logos, music, likeness) | ASSET_REGISTER before adding anything |
| Online fairness with real physics | same deterministic model on server and client (Section 4) |
