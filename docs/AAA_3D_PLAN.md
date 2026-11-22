# CRICKET UNLEASHED: Realistic 3D Plan (first playable prototype → full T20)

Date: 2026-11-22. **Status: PLAN ONLY.** No 3D player, animation, pitch
model or stadium exists yet (see the audit, commit `023d8c8`). Nothing will
be bought or subscribed to without your approval. Prices are approximate
(late 2026, India) and change often, especially memory and graphics cards.

Visual target (your decision): **realistic, AAA-style** players, kit,
animation, stadium, lighting, crowd and broadcast cameras. T20 stays the
only format players can pick. The existing C++ engine (rules, scoring, DRS,
physics) is kept and reused, not rebuilt.

---

## 0. An honest word about "AAA"

Big studio cricket games are made by teams of 50-150 people over several
years, with motion-capture studios and licensed players. We are one
developer (me, through Claude Code) plus you. What is realistic:

| Goal | Realistic for us? |
|---|---|
| Realistic-looking fictional players (MetaHuman faces and bodies) | **Yes** - this is the strongest free tool available |
| Realistic stadium, grass, lighting at golden hour / under lights | **Yes** with Unreal 5 (Lumen lighting, Nanite detail) on a strong PC |
| Broadcast cameras, replays, Hawk-Eye style graphics | **Yes** - our physics already gives exact ball paths |
| Batting and bowling animation as smooth as a top studio game | **Partly**. This is the hardest and slowest part. Good results need motion capture of a real cricketer (can be done cheaply with phone video + AI tools, then cleaned up) |
| 22 players + umpires + crowd at 60 fps | **Yes on a strong gaming PC**, with careful level-of-detail settings |
| Same quality on Android TV boxes | **No.** Android TV needs a much lighter version or cloud streaming (section 9) |

Aim: "looks like a real TV broadcast at first glance". Every milestone
gets screenshots and a video so you can judge it yourself.

---

## 1. Unreal Engine 5 project architecture

Engine: **Unreal Engine 5.6 or newer** (free). Windows PC first.

```
unreal/CricketUnleashed/
  CricketUnleashed.uproject
  Plugins/CricketCore/        <- our existing C++ engine (copied from core/ by
                                 tools/unreal/sync_core.*; core/ stays the master)
  Source/CricketGame/         <- new Unreal-side code (C++)
  Content/                    <- 3D assets (NOT in GitHub; see section 10)
     Characters/  Animations/  Props/  Ground/  Stadium/  Crowd/
     Cameras/  UI/  Audio/  Materials/  Maps/
```

Main pieces of the new `CricketGame` module:

| Piece (Unreal class) | Job |
|---|---|
| `AMatchDirector` | owns the existing `MatchSession` (rules, scoring, DRS). The ONLY place results come from |
| `ADeliveryDirector` | runs one ball as a timeline: run-up → release → flight → bounce → shot → fielding → dead ball → replay |
| `ACricketer` (Character) | one class for every player; role = batter, non-striker, bowler, keeper, fielder, umpire |
| `UCricketAnimInstance` | the animation brain (Animation Blueprint): motion matching for movement, montages for shots and deliveries |
| `ABallActor` | the ball: follows the physics path exactly (never Unreal's own physics) |
| `AFieldLayout` | turns our field presets (26 named positions) into spots on the ground |
| `ACameraDirector` | broadcast camera cuts and replays |
| `UScoreboardWidget` | on-screen scoreboard from our existing `Scorecard` module |
| Input | Unreal Enhanced Input (keyboard / gamepad) + the existing phone controller |

Rule: **Unreal only shows things; the C++ engine decides things.** That
keeps online play fair and lets us test the rules without graphics.

How I work on it: mostly C++ and Unreal's Python editor scripts (text files
I can write and test). Some steps need clicks in the editor (creating a
MetaHuman, checking a material): you do those, or I talk you through them.
About 3-5 hours a week of your time on the PC is realistic.

---

## 2. Integration of the existing C++ engine

Already prepared (commit `023d8c8`, **untested in Unreal**): `CricketCore`
plugin, copy script, compile check for Unreal's rules (no exceptions, no
RTTI, no clashing macro names), match recordings with ball paths.

| Step | What | Where |
|---|---|---|
| 1 | Build the plugin in Unreal on Windows (MSVC compiler), fix small errors | PC |
| 2 | Check MSVC gives the same physics numbers as Linux (the `PathHash` test) | PC |
| 3 | `AMatchDirector` calls `MatchSession` exactly as the phone host does today | laptop (code) + PC (test) |
| 4 | Per-ball **event timeline** from the physics: release time and point, bounce, arrival at the bat, contact point and time, where and when a fielder gathers, throw, run times | laptop (C++, tested) |
| 5 | Phone controller inside the game: the local web server needs a Windows network port (Winsock) | laptop (can be compile-checked with a free Windows cross-compiler) |
| 6 | Online play later: the 3D game becomes a client of the existing server | later stage |

---

## 3. Realistic human players (MetaHuman)

**MetaHuman** (free with Unreal; built into the editor since UE 5.6) makes
photo-realistic faces and bodies, with hair and many body shapes.

| Need | Plan |
|---|---|
| 22 fictional players + 2 umpires | MetaHuman Creator: different faces, skin tones, heights, builds (fast bowler tall and strong, keeper smaller...). Fictional only, no real cricketer's face |
| Faces for close-ups | MetaHuman full face rig (expressions: appeal, celebration, disappointment) |
| Performance | each MetaHuman has detail levels (LODs): full detail for close-ups, simple versions for far fielders |
| Alternatives | Character Creator 4 (paid, about ₹40,000+) - only if MetaHuman can't do something; Blender + free scanned heads - more work, lower quality |

**What MetaHuman does NOT include:** cricket kit. Pads, gloves, helmet,
thigh guard, cricket shoes with spikes, coloured T20 kit, caps and sun hats
must be **made in Blender** (free, slow) or **bought on Fab** (needs your
approval, and the right licence). Plain team colours and a number, no board
or sponsor logos.

---

## 4. Cricket body rigging and animation

All MetaHumans share one skeleton (bone rig), so one animation works on
every player. Unreal tools: **Motion Matching** (picks the best movement
clip each frame - smooth running), **Motion Warping** (bends a clip so a
foot or hand lands on an exact spot at an exact time), **Control Rig / IK**
(keeps hands on the bat, feet on the ground), montages (one-off actions).

The animation list (minimum for a realistic T20):

| Who | Animations |
|---|---|
| Bowler (pace) | walk back, run-up (3 speeds), gather/jump, delivery stride, release (stock, yorker, bouncer, slower), follow-through, appeal, celebration, fielding off own bowling |
| Bowler (spin) | short run-up, off-spin and leg-spin actions, follow-through, appeal |
| Batter | guard / stance, trigger movement, front-foot and back-foot defence, leave, drive (cover/straight/on), loft, cut, pull, hook, flick, sweep, step-out, running (start, run, slide bat in, turn), being hit, getting out, raising the bat (50/100), walking off |
| Keeper | crouch, rise and take (both sides), dive, standing up to spin, stumping, catch, throw |
| Fielders | ready walk-in, chase, slide stop, pick-up and throw (overarm, side-arm, flick), dive stop, catches (high, low, diving, boundary relay), backing up, celebrations, appeals |
| Umpires | signals: out, four, six, wide, no-ball, bye, leg-bye, free hit, TV referral |

**Where animations come from (cheapest first):**

| Source | Cost | Quality | Notes |
|---|---|---|---|
| Epic's free **Game Animation Sample** (hundreds of walk/run/turn clips) | ₹0 | very good | movement only, no cricket |
| **Mixamo** (Adobe, free) | ₹0 | fair | throws, catches, celebrations; generic |
| **Phone video of a real club cricketer → AI motion capture** (e.g. Move AI, Rokoko Video, DeepMotion; free tiers are limited) | ₹0 free tier, or roughly ₹1,700-4,500/month for a paid month **(approval)** | good after clean-up | needs a willing cricketer with written consent, good light, 2+ phones ideally |
| Hand-made in Blender / Cascadeur (free tier) | ₹0 | fair, slow | for small fixes and missing frames |
| Bought animation packs on Fab | about ₹2,000-25,000 per pack **(approval)** | varies | few real cricket packs exist; check before buying |
| Professional motion-capture studio day | about ₹1-5 lakh **(approval)** | best | only if the game needs it later |

Recommended: free sample + Mixamo for movement, **phone-video capture of a
club cricketer** for batting and bowling (start with free tiers).

---

## 5. Batter, bowler, wicketkeeper, fielder models

| Model | Built from | Extra parts |
|---|---|---|
| Batter | MetaHuman + kit | helmet with grille, pads, gloves, thigh guard, bat in hand (socket on the hand bone) |
| Bowler | MetaHuman + kit | cap/headband, spikes, ball in hand until release |
| Keeper | MetaHuman + kit | keeping gloves, keeping pads, helmet for spin |
| Fielders | MetaHuman + kit | caps / sun hats, sunglasses (some) |
| Umpires | MetaHuman | hat, coat or shirt, counter |

Each team kit = one set of materials with team colours (India blue,
Pakistan green...), name and number on the back.

---

## 6. Pitch, stumps, bat, ball, stadium assets

| Asset | Plan | Laptop? |
|---|---|---|
| Ball (red / white, seam) | Blender model + material | ✅ |
| Stumps, bails (bails fly off when hit - simple physics) | Blender | ✅ |
| Bat (willow grain, grip, sticker-free) | Blender | ✅ |
| Pitch (20.12 m, creases, wear and footmarks that match our physics' wear) | Unreal material + decals | PC |
| Outfield grass, mowing stripes, 30-yard circle, rope | Unreal landscape + grass, free scanned textures | PC |
| Sight screens, boundary boards (no real sponsors), dugouts | Blender/Unreal | PC |
| Stands, roof, floodlight towers, big screen | modular kit we build, or a Fab stadium pack **(approval)** | PC |
| Crowd (10,000s of people) | low-cost animated people (instanced) + MetaHumans near the cameras | PC |
| Sky, sun, floodlights, day-night | Unreal Sky Atmosphere, Lumen lighting | PC |
| Sound | bat hits, crowd, appeals; free (CC0) sounds first | ✅ |

Fictional stadium names only; no real stadium copies.

---

## 7. Ball physics and animation synchronisation

The physics already gives the ball's exact path every 2 ms. The rule: **the
ball never lies; animations bend to meet it.**

| Moment | How it is synced |
|---|---|
| Release | the bowler's run-up length is chosen so the release frame happens at the exact time and hand position of the physics release point (motion warping) |
| Flight and bounce | the ball actor plays the physics samples; dust and a mark appear at the bounce point |
| Batter | the shot clip is picked from the physics result (shot, front/back foot, edge or middle); its contact frame is lined up with the physics contact time and the bat face is moved onto the contact point (IK). Misses and edges use the same clip with the bat slightly off the line |
| LBW / bowled | pad position from the physics pad box; stumps break at the physics hit time |
| Fielding | the physics says which fielder gets the ball, where and when; the fielder runs, dives or catches so the hands reach that spot at that time |
| Throw and run-outs | throw timing from the physics; batters' running times from the physics; the TV umpire replay shows the real margin |
| Replays | re-played from the recording (identical every time) |

Check at each stage: an automated test compares where the bat / hand is at
contact with where the physics says the ball is (target: within 3 cm).

---

## 8. Cinematic and broadcast cameras

| Camera | Use |
|---|---|
| Main broadcast (high, behind the bowler) | every delivery |
| Batter's end reverse | left-hand view, some balls |
| Side-on (square leg) | run-outs, replays |
| Follow-the-ball (ground cams) | after the shot |
| Spider-cam (cable cam over the pitch) | replays, between overs |
| Stump cam | bowled, yorkers |
| Close-up / player cam | celebrations, appeals, walking off |
| Replay system | slow motion from several angles |
| TV-style graphics | Hawk-Eye ball tracking (from physics), edge detector for DRS, speed gun, wagon wheel, pitch map |

A camera director chooses cuts by event (four, wicket, review...), like a
TV director. Free tools: Unreal cameras, Sequencer, Cine Camera (lens,
depth of field).

---

## 9. Graphics quality settings

| Preset | For | Settings |
|---|---|---|
| **Ultra** | RTX 4070-class and above | Lumen lighting with hardware ray tracing, Nanite, full-detail MetaHumans near camera, dense crowd, 1440p/4K with upscaling (TSR/DLSS) |
| **High** (main target) | RTX 3060 / 4060 class | Lumen (software), medium crowd, 1080p 60 fps |
| **Medium** | older GTX / RTX 2060 class | simpler shadows, lighter crowd |
| **Low** | weak PCs | pre-baked lighting, simple players far away |

**Android TV:** Android TV boxes have phone-class graphics. Unreal's mobile
mode has no Lumen or Nanite, and full MetaHumans are too heavy. Options for
later (your decision then):
1. A separate "TV" build: baked lighting, low-detail players, smaller crowd,
   30 fps. Looks clearly less realistic. Several weeks of extra work.
2. **Cloud streaming** (the game runs on a PC/server, the TV shows video):
   full quality, but needs a good internet line and costs money per hour of
   server time **(approval)**.
Recommended: finish Windows first, decide Android TV after Stage 6.

---

## 10. Licensing: free vs paid

| Tool / asset | Cost | Licence notes |
|---|---|---|
| Unreal Engine 5 | ₹0 | Epic takes 5% of gross revenue only above USD 1 million per product (check the current EULA before release) |
| MetaHuman | ₹0 | free for use with Unreal projects; fictional people only |
| Game Animation Sample, City Sample (crowds) | ₹0 | free for Unreal projects |
| Blender | ₹0 | free, open source |
| Visual Studio 2022 Community | ₹0 | free for individuals/small teams |
| Mixamo | ₹0 | free Adobe account; animations usable in games |
| Fab (Epic marketplace) | free items each month; paid items **(approval)** | read each item's licence; record every asset in `ASSET_REGISTER.md` |
| Free sounds (Freesound CC0) | ₹0 | prefer CC0; CC-BY needs credits |
| AI motion capture (Move AI, Rokoko, DeepMotion) | free tiers; paid ≈ ₹1,700-4,500/month **(approval)** | check that game use is allowed on the tier used |
| Character Creator 4, paid stadium packs | ≈ ₹3,000-40,000 **(approval)** | only if needed |
| Large-file storage | ₹0 options exist | 3D assets are many GB: GitHub's free large-file allowance is small. Plan: keep `Content/` on the PC + an external drive backup, or a free self-hosted Git server / Perforce free tier on the PC. Code stays on GitHub |

Never: real player faces or names, board logos, sponsor logos, real
stadium copies, broadcast music we don't own.

---

## 11. Hardware

### Your current laptop (i3 11th gen, 8 GB RAM, Intel UHD)

| Can do on the laptop (₹0) | Needs a powerful PC |
|---|---|
| All C++ engine work, tests, the per-ball event timeline, Windows port of the phone link | opening Unreal Editor and building the game |
| Fixing the open balance bug (KNOWN_ISSUES 45) | creating MetaHumans |
| Simple Blender props: ball, stumps, bails, bat | animation retargeting and clean-up |
| Filming / collecting cricket video for motion capture, sending it to free AI capture tiers (they run in the cloud) | stadium, lighting, crowd |
| Planning, docs, sound collection | screenshots, videos, packaged demos |

### PC needed for the 3D work

| | Minimum (Stages 1-3) | Recommended (Stages 4-6: 22 players, stadium, crowd) |
|---|---|---|
| Processor | 6-8 cores (Ryzen 5 7600 / Core i5-13400 class) | 8+ cores (Ryzen 7 7700 / Core i7 class) |
| Memory | 32 GB | 64 GB |
| Graphics card | 12 GB video memory (RTX 3060 12 GB / RTX 4060 Ti 16 GB / RTX 5060 Ti 16 GB class) | 12-16 GB+ (RTX 4070 Super / RTX 5070 class or better) |
| Disk | 1 TB NVMe SSD | 2 TB NVMe SSD (+ external drive for backups) |
| Windows | 10/11 64-bit | 11 64-bit |

To check any PC you can use: `tools/pc_check/check_pc.bat` (free, read-only).

### Hardware cost estimates (INR, approximate, **approval needed before any purchase**)

| Option | Approx. cost | Notes |
|---|---|---|
| A. Use a PC you can access (friend, family, college lab, office after hours) | ₹0 | must meet the minimum; needs permission to install Unreal (~100 GB) |
| B. Recommended desktop (8-core, 64 GB, RTX 4070 Super/5070-class, 2 TB) | ≈ ₹1.5-2.0 lakh | memory prices rose sharply in 2025-26; check current prices |
| C. Minimum desktop (6-core, 32 GB, 16 GB-class card, 1 TB) | ≈ ₹1.0-1.3 lakh | fine for Stages 1-3; tight for full matches |
| D. Used-parts desktop (used RTX 3060 12 GB etc.) | ≈ ₹65,000-90,000 | cheapest, some risk (no warranty) |
| E. Gaming laptop (RTX 4060/4070 laptop, 32 GB) | ≈ ₹1.3-1.9 lakh | portable; 8 GB video memory limits MetaHuman/stadium work |
| F. Cloud GPU workstation (pay per hour) | ≈ ₹80-200 per hour; ≈ ₹10,000-25,000 per month at part-time use | no upfront cost; must be switched off after use; slow over Indian home internet for big files |

---

## 12. Development order, timeline and what you will see

Times assume one capable PC, me working through Claude Code on it, and a
few hours a week of your help. Ranges are honest: animation can take longer.

| Stage | What gets built | Time | What you get to see |
|---|---|---|---|
| **0. Setup** | PC checked, Unreal + Visual Studio installed, `CricketCore` plugin compiled, physics numbers checked on Windows | 1 week | build log, plugin test results, first empty-level screenshot |
| **1. One batter, one bowler, pitch, stumps** | 2 MetaHumans in basic kit (pads, gloves, helmet - first versions), bat, ball, stumps, a realistic pitch and a patch of outfield, daylight | 2-4 weeks | screenshots (close-ups + broadcast view) and a turn-around video. Players stand in idle poses: no batting yet |
| **2. Batting and bowling animations** | run-up and delivery (pace + spin), 8-10 batting shots, defence, leave; from free sources + phone-video capture | 4-8 weeks | video of each animation; side-by-side with the reference video |
| **3. Real physics + a complete delivery** | ball follows our physics, release/contact sync, bowled/LBW/edge/miss, runs scored by our engine, simple cameras, minimal scoreboard | 2-4 weeks | **first playable demo** (Windows build you can run): bowl or bat one over with keyboard or phone |
| **4. Fielders and keeper** | 11 fielders + keeper, chasing, diving, catching, throwing, run-outs, umpires | 4-8 weeks | playable demo + video of catches and run-outs |
| **5. Stadium and broadcast** | stands, crowd, floodlights, day-night, sky; broadcast camera director, replays, Hawk-Eye/DRS graphics, full scoreboard | 6-10 weeks | trailer-style video, screenshots, playable demo |
| **6. Full 22-player T20** | full match flow (toss → 20 overs → result), all 10 teams' kits, AI captaincy, phone controller, performance tuning, quality presets | 6-10 weeks | full T20 you can play and watch; recorded highlight video |

**Total: about 6-11 months** from having the PC to a complete playable T20
with realistic players. Stage 3 (first playable delivery) is about
**2-4 months** in.

Every stage ends with: screenshots + video in the repo/release, honest
status (COMPLETE / PARTIAL / NOT STARTED), and the existing 279 engine tests
still passing.

---

## 13. Total cost summary (INR)

| Path | Software | Animation | Hardware | Total |
|---|---|---|---|---|
| **Free path** (borrowed/available PC, free tools and tiers) | ₹0 | ₹0 (free tiers, volunteer cricketer) | ₹0 | **₹0** (slower, animation quality limited) |
| **Recommended low-cost path** | ₹0 | ≈ ₹5,000-15,000 (2-3 months of an AI capture tool) | ≈ ₹1.0-2.0 lakh PC (or ₹0 if borrowed) | **≈ ₹5,000-15,000 + PC** |
| Optional extras | assets ≈ ₹10,000-50,000 (kit, stadium packs) | studio mocap ≈ ₹1-5 lakh | | only if needed, all with approval |

---

## 14. What I can do now, before any PC (₹0, on the laptop/cloud)

1. Fix the open balance bug (KNOWN_ISSUES 45).
2. Build and test the **per-ball event timeline** (section 2, step 4) that
   the animations will need.
3. Windows network port for the phone controller (compile-checked).
4. A shot-and-delivery "animation needs" list with exact timings from our
   physics (so motion capture records the right things).
5. A filming guide for capturing a club cricketer with phones (angles,
   light, consent form).
6. Simple Blender props (ball, stumps, bails, bat) - only if you want, with
   renders as screenshots.

## 15. Decisions needed from you

1. **PC:** which option in section 11 (A is ₹0 if you can access a suitable PC).
2. **Animation source:** free path first (recommended), paid AI capture later?
3. **A cricketer to film** (friend / club player, with written consent)?
4. **Big-file storage:** PC + external drive backup (₹0, recommended).
5. Your time: can you give about 3-5 hours a week on the PC for editor steps?
6. Start the ₹0 laptop work in section 14 now?
