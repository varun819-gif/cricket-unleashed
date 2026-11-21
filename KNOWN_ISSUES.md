# Known Issues and Limitations

Updated at the end of the cricket rules milestone (v0.6.x). Items fixed in
this milestone have been removed (see CHANGELOG.md).

## Placeholders and simulations (not real yet)

1. **No 3D game yet.** Unreal Engine work needs a Windows/Linux PC with a GPU
   (see DEVELOPMENT_PLAN.md). This milestone deliberately did not start it.
2. **Ball outcomes now come from the ball physics** (BALL_PHYSICS.md) in
   every played match (offline, phone controller, online, single player).
   `QuickSim` (statistical, weighted dice) is still used ONLY by the text
   simulator `cricket_sim` and is labelled as such.
3. **DRS evidence**: in played matches, LBW ball tracking and edges come from
   our own physics (the ball's computed path), not from real DRS products
   (no Hawk-Eye, UltraEdge or Snickometer, no licence, no imitation). In
   `cricket_sim` the evidence is still random (`QuickSim::MakeCase`).
   Grounding (run-outs/stumpings) and front-foot no-balls are not modelled
   by the physics yet, so they don't occur in played matches.
4. **Players' perception of a close call is a placeholder model** (the truth
   plus noise and bias, `QuickSim::Perceive`). The AI review logic itself
   never sees the evidence (enforced by a test). When physics exists, the
   perception model should be rebuilt from player positions and sight lines.

## Approximations of the Laws / playing conditions (documented)

5. **Rain targets use our own NON-OFFICIAL "CU Rain Rule"**, not DLS. Targets
   will differ from official DLS targets (see RAIN_RULE.md). Buffer minutes,
   minutes-per-over and the "full innings value" are our own round numbers,
   configurable per competition.
6. **Powerplays for shortened innings** are scaled in proportion, not taken
   from official reduced-match tables.
7. **Test days use overs as the clock** (90 overs, 3 equal sessions, 2 overs
   per changeover, 98-over cap for make-up). There are no real session
   times, extra half hour, last-hour minimum, drinks breaks or over-rate
   penalties (see TEST_SESSIONS.md).
8. *(Fixed in 0.7.0: players may review run outs and stumpings when the
   competition's DRS rules allow it.)*
9. **Run-out ends are simplified.** The new batter takes the dismissed
   batter's end based on runs completed. The real law (which end the wicket
   was broken at, whether batters had crossed) needs physics position data.
10. **Deliberate short running (Law 18.5)** is handled by the server
    awarding penalty runs (`AwardPenaltyRuns`); there is no automatic
    detection.
11. **The Laws were implemented from the MCC text** (2017 Code with later
    amendments as understood). A qualified umpire/scorer should review the
    rules engine before release.

## Not implemented yet

12. Retired hurt / retired not out, substitutes, concussion replacements.
13. A bowler leaving the field mid-over (injury) and another finishing the over.
14. Timed out (Law 40) as an umpire action.
15. Save files are **validated but not signed**: a determined user could
    forge a consistent single-player history offline. Ranked online results
    come only from the server's own journal, so this doesn't affect fair
    play (see SAVE_AND_RESUME.md).

## Mobile controller

18. Phone play uses the ball physics. The phone shows the ball over 1.4 s
    (slowed down so people can react); the tap timing is scaled to the real
    flight time (about 0.5 s for a fast bowler).
19. **Messages are signed, not encrypted.** Someone on the same Wi-Fi could
    see which buttons are pressed (not fake them). See MOBILE_CONTROLLER.md.
20. **The local server is POSIX-only** (Linux / Android TV). Windows needs a
    Winsock port of `LocalServer.cpp`.
21. **Tested with Chromium emulating an Android Pixel 7**, not yet on
    physical Android phones or Android TV hardware. iPhones untested.
22. Shot timing is measured on the phone (fine for local play). Online
    ranked play must check timing on the server (Priority 3).
23. The phone app is English-only for now. Screen Wake Lock isn't available
    on plain `http://` local pages, so phones may dim during long waits.

## Online multiplayer

24. **Not yet tested over the internet between two separate devices.** All
    tests run on one machine (real processes and sockets, a slow/cutting
    proxy, emulated phones). Online is a preview until the test in
    docs/ONLINE_SERVER_SETUP.md passes.
25. Guest accounts live in one browser. Clearing browser data or losing
    the phone loses the account (no recovery, no linking to email/Google
    yet).
26. Reports, admin review and bans exist (MODERATION.md), but: one shared
    admin token (no per-moderator names), no automatic cheat or
    multi-account detection, no appeals process, no chat (by design).
27. A TV/PC game cannot yet join online with local phones as controllers;
    online play is phone/PC browser to server. Planned with the Unreal
    client.
28. Test matches are not offered online (multi-day). T20, ODI and 1-50
    over matches are.
29. One server process, single-threaded, data in files: fine for
    development; the PostgreSQL/Redis path is documented but not built.
30. Online outcomes use the same ball physics, computed on the server.
31. No per-IP limits: behind a tunnel / proxy every player arrives from the
    same address, so limits are per connection, per account and per server
    (e.g. 30 new guests a minute for the whole server - a flood can block
    new sign-ups for a while; existing players are unaffected).
32. Shot timing: the server corrects claims more than 150 ms away from what
    it measured. A bot with perfect reactions within that window is not
    detected.
33. The name filter is a word list: new spellings can slip through and an
    unlucky real name could be blocked. Reports + admin rename cover this.
34. The online Node test suite failed once during the review; every
    later run passed (see TEST_REPORT.md for the count) and the failure
    could not be reproduced.
    Watching for it.
35. `/tv.json` (server health counts, no personal data) is visible to
    outsiders when the server is behind a tunnel.

## Ball physics (see BALL_PHYSICS.md)

36. **Simplified, calibrated model, not a research-grade simulation**: the
    swing, seam, bounce, bat and fielding constants come from published
    measurements where they exist and were tuned so whole matches look like
    T20 cricket. Computer v computer T20 averages: ~135 runs, ~5.5 wickets
    per innings (real T20Is: ~150-165 runs, ~6-7 wickets).
37. Now modelled (2026-11-21): run-outs, stumpings, overthrows, byes, leg
    byes, front-foot no-balls, boundary catches and saves, wind, dew, pitch
    wear and footmarks. Still not modelled: hit wicket, obstructing the
    field, humidity, the ball getting softer, rain making the outfield slow,
    mankading, the batter choosing NOT to run on a misfield.
38. The batter's body is simple (bat zone + pads/thigh box); there is no 3D
    body collision (comes with Unreal animation). Running between the wickets
    is decided by the computer for both sides (humans don't call runs).
40. Calibration (computer v computer, see BALL_PHYSICS.md): stumpings
    (<1% of wickets) and run-outs (~5%) are a little rare; Test run rate is
    a little high (~3.8); ODI extras a little high (~12 an innings).
41. Pitch and weather effects use simple rules (wear = overs bowled;
    footmark zones fixed outside the stumps; dew only in the second innings
    of day-night limited-overs games). They are plausible, not measured.
39. Computer players' decisions and execution vary with a seeded generator
    (same seed = same match). Human inputs decide human players' balls.

## Operational notes

16. The DRS review timer and break timers need the server to call
    `MatchSession::Tick()` / `ResumeAfterInterval()`. Without that, a window
    or break stays open. A player who disconnects during a review window
    simply runs out of time.
17. The project specification text was **cut off** at section 25
    ("Test: ... Multiplaye"). Any later sections still need to be shared.
