# Cricket Creator Studio (future) - what exists now

**Requirement (from you):** record matches, watch replays, and make
YouTube highlight videos.

**Status: groundwork only.** No video is recorded or made yet - that needs
the 3D game (Unreal) to draw the pictures. What exists is the **data** the
Studio will use, and it is tested.

## What is built (2026-11-21)

| Piece | What it does | Where |
|---|---|---|
| Match recording | every ball the physics played: speed, delivery type, the ball's flight every 10 ms, bounce point, the shot, bat contact, exit speed, the ball's path after the shot every 50 ms, who fielded it, the batters' run call, plus the official result of the ball (runs, extras, wicket, score) | `core/include/cricket/Recording.h` |
| Saved automatically online | each finished online match writes `data/matches/<id>.replay.jsonl` next to its `.save` | `OnlineServer.cpp` |
| From the simulator | `cricket_sim --record match.replay.jsonl` | `tools/sim/main.cpp` |
| Highlight finder | wickets, sixes, fours, fifties, hundreds, DRS reviews (overturned ones rank higher), close run-out chances, maidens, the finish; late moments in a chase rank higher | `FindHighlights()` |
| Highlights package | the best 12 moments with a **mix** (not only wickets), always the finish, one clip per ball, in match order | `HighlightPackage()` |
| Unreal reader | `LoadRecordedBall()` turns a recorded ball into a path a 3D ball can follow | `unreal/.../CricketPhysicsLibrary` (untested) |

Example (`cricket_sim --seed 5`):
```
HIGHLIGHTS (12 clips):
  3.4 WICKET! Aarav Sethi caught, bowled by Talha Baig (30/1)
  6.4 SIX! Rohan Kulkarni (51/2)
  14.5 REVIEW: OUT (decision stands)
  12.5 FIFTY for Faraz Durrani
  17.6 The finish: Pakistan won by 7 wickets
  ...
```

File format: JSON lines (one JSON object per line). Line 1 = match
(teams, result, conditions), then one line per ball, last line =
highlights (`all` and `package`). Size: about 0.8 MB for a whole T20.

Tests: `gp_recording_and_highlights` (every ball lines up with the
scorebook, every wicket / six / four is found, package is a mix and
includes the finish) and the phone test checks the recording of a real
host match parses line by line.

## Plan for the Studio (after the 3D prototype)

1. **Replays in the 3D game:** any ball from the recording, from any
   camera, slow motion (the replay actor already has a play rate).
2. **Broadcast replay cameras** for wickets, boundaries and DRS.
3. **Highlights video:** play the package's clips one after another with
   score graphics, record to an MP4 file on the PC (Unreal's Movie Render
   Queue - free).
4. **YouTube:** first by saving the MP4 and uploading by hand (free).
   Automatic upload would use YouTube's free API with your Google sign-in -
   only with your approval, and never with real players, logos or music we
   don't have rights to.
5. Commentary on the highlights uses our existing text commentary (voice
   needs a later decision).

Limits today: no pictures or video; online matches recorded only from
now on; a match recovered after a server crash keeps its full scorebook
but its recording has no physics paths (the file then says
`"physicsTraces": false`).
