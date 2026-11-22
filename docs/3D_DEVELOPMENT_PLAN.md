# Cricket Unleashed: 3D Development Plan (consolidated)

Date: 2026-11-20. Supersedes the outline in UNREAL_DEVELOPMENT_PLAN.md.
Status: **plan only. No 3D feature exists yet.** Nothing was bought.

## 1. Can this laptop run Unreal Engine 5? No, not usefully.

| | Your laptop | Epic's recommended for UE5 | Gap |
|---|---|---|---|
| CPU | Intel i3 11th gen (2 cores / 4 threads) | quad-core 2.5 GHz+ | about half; shader and C++ builds take very long |
| RAM | 8 GB (shared with graphics) | 32 GB | 4x short; the editor runs out of memory with characters/stadium |
| Graphics | Intel UHD (integrated, no own memory) | DirectX 12 card, 8 GB+ video memory | no dedicated GPU: Lumen, Nanite, MetaHuman faces, realistic crowds effectively unusable |
| Disk | NVMe (good) | 100 GB+ free for engine + project | check free space |

What would happen: the editor may open an empty level at low frame rates,
shader compiling would take hours, and realistic characters or a stadium
would crash or crawl. That is not a workable way to build the game we want.
A RAM upgrade alone does not fix it (the integrated graphics is the main
limit).

**Useful on this laptop:** cricket rules, ball physics maths, multiplayer,
controller, tests (all engine-independent C++, runs in WSL); planning; light
Blender work; and **remote access to a powerful PC or cloud workstation**
(the laptop becomes the screen and keyboard).

## 2. What is preserved (checked)

Branch `ccr-1fd537b3-uv7e9l`, last commit `d81176d`, 241/241 C++ tests
passing, working tree clean. The 3D game will **reuse**, not rewrite:

| Existing work | Role in the 3D game |
|---|---|
| Rules engine (`core/`): T20/ODI/Test, DRS, rain rule, saves, AI captain, commentary text | the umpire and scorer inside Unreal (as a plugin) |
| Phone controller (`controller/`) | phone as gamepad for the TV/PC game |
| Online server (`online/`) | lobby, matchmaking, ratings, moderation for 3D online play |
| Web preview (`online/web/`) | kept as a test tool and phone controller UI |

The core already compiles the way Unreal requires (no exceptions, no RTTI;
checked on every build).

## 3. Recommended path

1. **Get a capable development PC** (Option A below, your approval needed).
   Until then, use **no paid cloud** by default.
2. ✅ **Done (2026-11-21, see BALL_PHYSICS.md):** the **ball physics model** in our
   C++ core: delivery speed, drag, swing, seam, spin, bounce, pitch
   friction, bat contact. It is deterministic and tested without graphics,
   and the online server can use the same model to keep results fair.
3. ✅ **Prepared (2026-11-21, see UNREAL_PROTOTYPE_SETUP.md):** `unreal/`
   project and CricketCore plugin skeleton, core copy script, ball replay
   actor, match recordings Unreal can replay, a read-only PC check script.
   Untested until a capable PC exists.
4. **On the new PC:** create the Unreal project, plug in the core, and build
   the **first playable prototype** (Section 6).
5. Then, in order: animations (bowling, batting), fielders + keeper +
   catches, stadium, crowd, broadcast cameras and scoreboard, faces
   (MetaHuman), commentary audio, online, phone controller in 3D.

**Target platform:** the realistic look you want (MetaHuman faces, full
stadium crowds) is **Windows PC first**. Android TV boxes cannot run that
quality; Android TV needs a much-reduced version or cloud streaming later.
This is a decision for you (Section 9).

## 4. Hardware options and costs (approximate, check current prices)

| Option | What | Approx. cost | Notes |
|---|---|---|---|
| **A. Desktop PC (recommended)** | 8-core CPU (Ryzen 7 / Core i7 class), 32 GB RAM (64 GB better), RTX 4060 Ti 16 GB or RTX 4070-class GPU, 1-2 TB NVMe, 650-750 W PSU | about ₹1.1-1.6 lakh | best value long-term; also the test machine for "premium" graphics |
| B. Gaming laptop | RTX 4060/4070 laptop GPU (8 GB), 32 GB RAM | about ₹1.2-1.8 lakh | portable, but 8 GB video memory limits MetaHuman/stadium work |
| C. Cloud workstation | e.g. AWS g5.xlarge (NVIDIA A10G 24 GB) in Mumbai, about USD 1.2/hour Linux on-demand; Windows costs more | about ₹10,000-20,000 per month at 80-100 hours, plus storage | no upfront cost, but adds up: more than a PC after roughly 8-12 months; needs care to switch off; your approval needed |
| D. Keep laptop only | - | ₹0 | 3D work not possible; only core/physics/multiplayer work continues |

Everything except the hardware can start at ₹0.

## 5. Software and assets

**Free tools (₹0)**

| Tool | Use | Licence note |
|---|---|---|
| Unreal Engine 5 (latest stable) via Epic Games Launcher | the 3D game | free; Epic royalty only above USD 1 million gross revenue per product (check current EULA) |
| Visual Studio 2022 Community | C++ for Unreal | free for individuals / small teams |
| Git + Git LFS | code + large files | GitHub LFS free tier is only ~1 GB: large art needs a paid data pack **or** a free self-hosted alternative (Perforce Helix Core free tier) - decide before art arrives |
| Blender | models, pitch, stadium parts, animation clean-up | free |
| MetaHuman (inside Unreal) | realistic fictional player faces and bodies | free with Unreal; no real player likenesses |
| Epic free samples (Game Animation Sample, City Sample crowds, mannequins) | placeholder movement, crowd tech | free for use in Unreal projects |
| Mixamo | generic animations (idle, run, celebrate) | free with an Adobe account; no cricket-specific moves |
| Claude Code on the PC | me building and testing in the project | runs on Windows |

**The hard part: cricket animation.** Realistic run-ups, bowling actions
and batting shots do not exist as free assets. Options:

| Option | Cost | Quality |
|---|---|---|
| Hand-keyed in Blender from reference video | ₹0, slow | placeholder to fair |
| AI motion capture from phone video of a real club cricketer (with written consent) - e.g. Rokoko Video, Move AI | free tiers are limited; paid plans roughly USD 20-50/month | good, needs clean-up |
| Bought cricket animation packs (Fab marketplace) | roughly USD 20-300 per pack, if suitable ones exist | varies |
| Professional mocap studio session | often ₹1-5 lakh+ | best |

**Stadium and crowd:** start with our own modular stadium (Blender +
Unreal) and Epic's crowd technology. Marketplace stadium packs are paid
(approval). No real stadium names or brands without licences.

**Any purchase or subscription above needs your approval first.**

## 6. First playable 3D prototype ("P1: One Ball")

**Goal:** one bowler bowls one delivery to one batter, the batter plays a
shot, and the camera follows the ball. Repeatable, running on the dev PC,
demonstrated on video.

| Part | What it does | Built with |
|---|---|---|
| Pitch and ground | 22-yard pitch with creases, stumps and bails, outfield circle, boundary rope, simple sky/light | Unreal static meshes (our own) |
| Bowler | run-up, delivery stride, release at the right frame; choose delivery (pace, length, line, swing/spin) | Unreal mannequin or MetaHuman body; placeholder bowling animation; animation notify releases the ball |
| Ball | flies, swings, pitches, bounces, spins, hits pad/bat/stumps | **our core physics model** (Section 3, step 2) computes the path; Unreal draws it |
| Batter | stance, trigger movement, 3 shots: defend, drive, pull; timing + direction input | placeholder animations; contact decided by core bat-contact model |
| Outcome | ball reaches boundary / stops / hits stumps → runs or bowled, scored by our **existing rules engine** | `CricketCore` plugin (`MatchSession`) |
| Cameras | broadcast end-on view behind the bowler; on contact, switch to a camera that follows the ball; replay of the delivery | Unreal camera actors + Sequencer replay |
| HUD | minimal score line (runs/wickets/balls) from the rules engine | Unreal UMG |
| Input | keyboard/gamepad: bowl (length meter), bat (direction + timing) | Unreal Enhanced Input |

**Not in P1:** fielders, catches, crowd, faces, commentary, online.

**Done when (acceptance):**
1. The packaged Windows build runs at 60 fps at 1080p on the dev PC.
2. Same delivery inputs give the same ball path (automated test in core).
3. Bowled, defended, driven-to-boundary and missed balls each happen and
   are scored correctly by the rules engine.
4. A screen recording of the demo is shared with you.
Nothing is called complete without that recording.

**Estimated effort** (once the PC is ready): core ball physics model
about 1-2 weeks (can start now, ₹0); Unreal prototype about 3-5 weeks.
Animation quality in P1 is placeholder; realistic motion needs the
animation decision in Section 5.

## 7. What can be done where

| Work | This laptop | Dev PC (Option A/B) or cloud |
|---|---|---|
| Ball physics model, rules, multiplayer, tests | ✅ | ✅ |
| Planning, docs, light Blender modelling | ✅ | ✅ |
| Unreal editor: levels, characters, animation, lighting | ❌ | ✅ |
| MetaHuman faces, stadium, crowd | ❌ | ✅ (16 GB+ video memory comfortable) |
| Packaging and performance testing | ❌ | ✅ |
| Remote access to the PC/cloud | ✅ (laptop as screen) | - |

## 8. Phases after P1 (each ends with a recorded demo)

| Phase | Content |
|---|---|
| P2 | realistic bowling and batting animation set (after animation decision) |
| P3 | 11 fielders, wicketkeeper, catches, throws, run-outs |
| P4 | stadium + crowd + day/night lighting |
| P5 | broadcast cameras, replays, full batting/bowling scoreboards (UI) |
| P6 | MetaHuman fictional players, kits in national colours (no board logos) |
| P7 | commentary audio English + Hindi (voice decision, approval) |
| P8 | full matches with the rules engine, AI, phone controller |
| P9 | online play in 3D (existing server), then Creator Studio |
| P10 | platform decision: Android TV reduced version or streaming |

## 9. Decisions needed from you

1. **Hardware:** Option A (desktop, recommended), B (gaming laptop), or C
   (cloud, monthly cost). No purchase until you approve.
2. **Platform:** accept "Windows PC first, Android TV later in reduced
   quality or via streaming"?
3. **Animation source** for cricket moves (Section 5) - can start with ₹0
   placeholders.
4. **Large-file storage:** paid GitHub LFS data pack vs free self-hosted
   Perforce.
5. Approve starting the **core ball physics model now** (₹0, no hardware
   needed).
