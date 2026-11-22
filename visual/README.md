# 3D visual prototype (Blender, free)

**What this is:** the first real 3D pictures of CRICKET UNLEASHED, made with
free tools while no Unreal-capable PC is available. It renders **one
complete delivery**: a right-arm fast bowler (Pakistan) bowls to a
right-handed batter (India), who drives it through the covers for four.

**What it is NOT (honest status):**
- Not Unreal Engine and not real-time: Blender renders the pictures and video
  frame by frame (about 10 s per frame on 4 CPU cores).
- Not AAA / MetaHuman quality. The people use the free MakeHuman (CC0) body:
  real human proportions and a real skeleton, but **clothes are painted on
  the body** (no cloth folds), no hair (helmet / cap), simple eyes, simple
  pads, gloves, helmet and bat made from basic shapes.
- Movements are **hand-keyed poses** (about 7-12 key poses per player,
  smoothed between), not motion capture: they look stiff and robotic.
- No fielders, umpires, non-striker, stands or crowd yet.

**What is real:**
- The ball's whole path (release, swing, bounce, bat contact, path to the
  rope) is the exact output of our C++ ball physics
  (`data/delivery_cover_drive.json` from `cricket_sim --seed 5 --record`):
  150 km/h, pitched 5.5 m, driven at 116 km/h, four.
- The bowler's hand is placed at the physics release point at release time,
  and the middle of the bat is at the physics contact point at contact time.
- Pitch, creases and stumps use the Laws' sizes (20.12 m, 1.22 m popping
  crease, 71.1 cm stumps, 22.86 cm wide).

## Files

| File | What |
|---|---|
| `blender/cricketer.py` | builds a rigged human from the MakeHuman data files (body shape, skeleton, skin weights, kit colours) |
| `blender/delivery_scene.py` | ground, pitch, stumps, bat, ball, kit, both players, their key poses, cameras, rendering |
| `data/delivery_cover_drive.json` | the physics path of the ball |
| `assets/makehuman/` | MakeHuman CC0 data (licence + source in that folder) |

## Run it (Linux / WSL with Blender 4.x, free)

```
sudo apt install blender
blender -b -P visual/blender/delivery_scene.py -- --still broadcast 0.0 out.png
blender -b -P visual/blender/delivery_scene.py -- --anim broadcast frames/ 960x540 16
ffmpeg -framerate 25 -i frames/f_%04d.png -pix_fmt yuv420p delivery.mp4
```
Cameras: `broadcast`, `batter`, `bowler`, `side`, `drive`. Time is in
seconds from the ball's release (contact is at 0.42 s).

Your laptop can run this (slowly). It is the bridge until the Unreal
prototype: the same physics data and the same coordinates will drive the
Unreal version (see `docs/AAA_3D_PLAN.md`).

---

# Phase 2 (2026-11-22): better players, kit, equipment, animation, scene

New files: `blender/scene_v2.py` (the scene), `blender/equipment.py`
(equipment models), `blender/cricketer.py` → `build_player()` (dressed
players), `blender/edit_video.sh` (joins shots with captions). Phase 1
(`delivery_scene.py`) is kept unchanged. The ball path is still exactly the
physics data in `data/delivery_cover_drive.json`.

```
blender -b -P visual/blender/scene_v2.py -- shot 3_drive_slowmo frames/ 960x540 24
blender -b -P visual/blender/scene_v2.py -- still hero 0.42 hero.png 1280x720 64
visual/blender/edit_video.sh frames/ delivery_v2.mp4
```
Shots: `1_broadcast`, `2_bowler_slowmo`, `3_drive_slowmo`, `4_chase`,
`5_umpire`. Cameras: `broadcast`, `bowler_side`, `batter_front`, `hero`,
`chase`, `umpire`.

## What changed (honest status)

| Area | Status | What exactly |
|---|---|---|
| Separate shirts and trousers | **DONE (simplified)** | separate meshes over the body (body skin underneath removed), short or long sleeves, collar trim, cloth thickness, looser trousers below the knee |
| Cloth folds | **PARTIAL** | fixed crumple/crease detail in the mesh + crease shading; **no cloth simulation** (cloth does not swing or bunch when moving) |
| Fabric texture | **DONE (simple)** | fine weave bump, dye variation, soft sheen |
| Stitching / seams | **NOT DONE** | no seam lines or stitches modelled |
| Faces | **PARTIAL** | 5 different faces from free face-shape data (head shape, nose, chin, mouth), eyelids fixed, skin with subsurface scattering, pores, lip tint. Still smooth "game NPC" faces, no wrinkles or detailed skin textures |
| Eyes | **DONE (simple)** | eyeballs with iris and pupil that turn to follow the ball |
| Hair | **PARTIAL** | short crop (hair cap + fine strands), eyebrows; all players have short hair (long styles not attempted) |
| Body types | **DONE** | tall muscular fast bowler, average batter, heavier non-striker, short stocky keeper, older-built umpire; 5 skin tones |
| Helmet with grille | **DONE** | shell with face opening and ear guards, peak, 4 horizontal + 2 vertical steel bars, chin strap |
| Bat | **DONE** | shaped blade (edges, spine, bow, toe, shoulders), oval handle with ribbed rubber grip, willow grain |
| Batting gloves with fingers | **DONE (simplified)** | separate fingers that grip the handle; no individual finger rolls/padding |
| Leg pads | **DONE** | curved shell, 7 vertical bolsters, knee roll, 3 straps |
| Thigh guard | **DONE, hidden** | worn under the trousers (as in real cricket), so not visible in the video |
| Shoes with spikes | **DONE (simplified)** | shoe shell, rubber sole, 9 metal spikes per shoe |
| Stumps and bails | **DONE** | stumps with ground spikes and grooves, turned bails with spigots |
| Bowling run-up and delivery | **IMPROVED** | strides with foot plants, pelvis bob and sway, arm swing, bound, side-on back-foot landing, braced front leg, arm over the top to the exact release point, follow-through across the body, run-off |
| Cover drive | **IMPROVED** | stance, bat tap, trigger, high backlift, front-foot stride with weight transfer, bat swing on an arc meeting the ball at the exact physics point, high finish, back heel lifting |
| Head and eyes | **DONE** | head and eyes track the bowler, then the ball |
| Motion capture | **NOT USED** | no free cricket motion data with a clear licence was reachable (Mixamo needs an Adobe login; CMU has no cricket). All motion is key poses by script - smoother than phase 1 but still not as natural as real capture |
| Non-striker, keeper, umpire | **DONE** | non-striker backs up and jogs, keeper standing back rises and turns, umpire signals four |
| Boundary rope, advertising boards | **DONE** | rope, 72 boards with fictional sponsors, 30-yard circle |
| Grass, pitch, sky, sun | **IMPROVED** | mowing stripes, patchy grass, pitch cracks and footmarks, Nishita sky, sharper sun shadows |
| Cameras, close-ups, slow motion | **DONE** | broadcast, tracking bowler cam (half speed), front-on batter cam (quarter-speed contact), ball-chase cam, umpire close-up, depth of field |
| Crowd, stands | **NOT DONE** | next phase |
