# Known Issues and Limitations

Updated at the end of the cricket rules milestone (v0.6.x). Items fixed in
this milestone have been removed (see CHANGELOG.md).

## Placeholders and simulations (not real yet)

1. **No 3D game yet.** Unreal Engine work needs a Windows/Linux PC with a GPU
   (see DEVELOPMENT_PLAN.md). This milestone deliberately did not start it.
2. **`QuickSim` is a statistical placeholder for ball physics.** Ball
   outcomes are weighted dice, not physics. Test scoring rates (~5-6 an over)
   are too high.
3. **DRS evidence is SIMULATED, not real DRS technology.** Ball tracking,
   edge detection, grounding / boundary replays and front-foot checks are
   random values from `QuickSim::MakeCase`. There is no Hawk-Eye, UltraEdge,
   Snickometer or any licensed system. The decision *rules* are real and
   tested; the *evidence* is not.
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
8. **Run outs and stumpings are always umpire referrals.** Players cannot
   review them, although ICC playing conditions allow it when the umpires
   don't refer.
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

## Operational notes

16. The DRS review timer and break timers need the server to call
    `MatchSession::Tick()` / `ResumeAfterInterval()`. Without that, a window
    or break stays open. A player who disconnects during a review window
    simply runs out of time.
17. The project specification text was **cut off** at section 25
    ("Test: ... Multiplaye"). Any later sections still need to be shared.
