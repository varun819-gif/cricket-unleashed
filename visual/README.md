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
