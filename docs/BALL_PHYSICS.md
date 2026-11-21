# Ball physics

Status: **built, tested, and used for every ball** in offline single player,
the phone controller and online matches (the server computes it). No 3D
graphics: Unreal will draw the paths this code computes.

Code: `core/include/cricket/BallPhysics.h` (the physics),
`core/include/cricket/BallOutcome.h` (bowler/batter choices → physics →
scored ball). Tests: `core/tests/PhysicsTests.cpp`,
`controller/tests/MatchHostTests.cpp` (`ctl_host_physics_*`).

## 1. What is simulated

| Stage | Model |
|---|---|
| Release | speed from the bowler's skill and effort (pace 118-150 km/h, spin 76-98 km/h); release height 2.0-2.15 m; the bowler aims at a spot (length, line) and the physics solves the release angles to land there (like a real bowler), then execution scatter by skill |
| Flight | gravity; air drag (Cd 0.45, ball 160 g, radius 3.6 cm); **swing** force sideways, from seam angle, seam stability, speed and ball shine: conventional swing from ~15 m/s, peak ~30 m/s, fades by ~41 m/s on a new ball; **reverse swing** above ~35 m/s with an old rough ball; **Magnus** force from spin (drift and dip) |
| Bounce | rigid-sphere bounce with friction: vertical coefficient of restitution per pitch, sliding/rolling friction that takes pace off and turns **spin** into sideways movement; **seam movement** from the seam landing on a grassy pitch |
| Pitches | Standard, Green (seam), Dusty (spin), Bouncy (pace), Flat |
| Batter | the batter "reads" the ball ~0.2-0.3 s before it arrives (better batters later), anticipates part of the movement by skill, and puts the bat where they expect it. Late swing / seam / spin = **edges and misses**. Each shot (defend, drive, loft, cut, pull, flick, sweep) has a reach zone, bat speed and face angle. Front foot to full balls, back foot to short |
| Bat impact | collision along the bat face with coefficient of restitution (~0.5 at the middle) and effective bat mass; off-centre or mistimed = less speed; late = squarer; **outside / inside / top / bottom edges** |
| Missed | pads (LBW with in-game ball tracking to the stumps), stumps (bowled), keeper (byes if not collected) |
| After the shot | flight with drag, outfield bounces and roll; boundary at 70 m from the pitch centre (four or six) |
| Fielders | 9 from the field setting + wicketkeeper + bowler; reaction time, running speed and dive reach by skill; **catches** (harder when fast, diving or arriving late) and **drops**; **ground stops** (hard-hit balls can beat a diving fielder); **runs** from batter running time vs pick-up + throw |
| Umpiring | wides (limited overs guideline 0.89 m, down the leg side, over head height), waist-high full toss = no-ball, LBW evidence (pitched, impact, wickets, shot offered), caught-behind edges (a very faint edge may be missed on the field and go to DRS) |

## 2. Typical numbers it produces

| Delivery | Result |
|---|---|
| 135-145 km/h good length | ~0.5 s to the batter; 5-12% speed lost in the air, more off the pitch; passes around the top of the stumps |
| 140 km/h bouncer (11 m) | chest / head height at the batter |
| 130 km/h new ball, seam 20° | swings 12-45 cm before pitching |
| 148 km/h old ball, same seam | swings the **other** way (reverse) |
| 86 km/h off-break, dusty pitch | a few degrees of turn |
| Perfectly timed half-volley drive | 110-120 km/h off the bat, along the ground |
| Clean loft | 130-140 km/h, carries ~80 m: six |

## 3. Calibration (whole matches)

Computer v computer T20s, both sides played through the physics:

| | This model (40 matches) | Real T20 internationals (typical) |
|---|---|---|
| Runs per innings | ~135 | ~150-165 |
| Wickets per innings | ~5.5 | ~6-7 |
| Dot balls | ~49% | ~38-42% |
| Fours / sixes per innings | ~15 / ~3 | ~13 / ~6 |
| Dismissals | caught 71%, bowled 16%, LBW 12% | caught ~65%, bowled ~15%, LBW ~10%, run out ~7% |

The real values are approximate public averages, used only as a sanity
band. The automated test requires 100-220 runs, 2-10 wickets and 30-65% dot
balls per innings.

## 4. Determinism (same inputs → same result)

- The physics is a pure function: no random numbers. `BallOutcome::ResolveSpec`
  gives the same result for the same delivery and shot, whatever the seed.
- Fixed time step (2 ms, Runge-Kutta 4), only `+ - * /` and `sqrt` (exactly
  rounded in IEEE 754), our own sine / cosine (`DetSin`, accurate to 1e-12),
  and no fused multiply-add (`-ffp-contract=off`).
- Checked: the same path fingerprint (`PathHash`) on **GCC debug + sanitizers,
  GCC release and Clang release**. The test stores the expected fingerprints,
  so a machine that disagrees fails the test. (MSVC on Windows will be
  checked when the Unreal PC exists; MSVC's default `/fp:precise` does not
  contract.)
- Computer players' choices and execution (and the bowler's scatter, the
  seam landing) use a seeded generator (SplitMix64, identical on every
  platform). Same seed = same match, ball for ball (tested).
- Online, the server runs the physics and is the only authority; clients
  never send outcomes.

## 5. Using it from Unreal (later)

`SimulateDelivery()` returns the ball's path every 2 ms (position, velocity,
spin) plus the bounce point and timings; `PlayShot()` the contact point and
exit velocity; `PlayOutField()` the path after the shot and who fields it.
Unreal moves the ball mesh along these samples and times the animations to
them (release frame, bounce, contact). Coordinates: metres, origin at the
striker's middle stump, +y towards the bowler, +x off side, +z up.

## 6. Limitations (honest)

- Simplified, calibrated physics, not a research model. Constants come from
  published measurements where available (drag, swing range, bounce) and
  were tuned for realistic match statistics.
- Not modelled yet: run-outs, stumpings, hit wicket, overthrows, front-foot
  no-balls, leg byes, boundary-rope catches, wind, humidity / dew, pitch
  wear during a match, the ball softening.
- Batter body is simple (bat zone + pads), no 3D collision.
- Calibration: a little low on runs and sixes, a little high on dot balls.
- The phone UI slows the ball down for reaction; timing is scaled to the
  real flight time.

## 7. Sources (for the model shapes)

- R. D. Mehta, "An overview of cricket ball swing", Sports Engineering (2005)
  and related wind-tunnel studies: drag ~0.4-0.5, swing vs speed and seam
  angle, reverse swing with a rough ball.
- R. Cross, papers on ball bounce with spin and friction (e.g. "Bounce of a
  spinning ball near normal incidence", Am. J. Phys. 2005).
- Laws of Cricket (MCC): ball mass and size, stumps, popping crease, LBW,
  wides and no-balls.
