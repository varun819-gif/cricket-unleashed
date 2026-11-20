# Known Issues and Limitations

1. **No 3D game yet.** The Unreal Engine project needs a Windows/Linux PC with
   a GPU. This cloud workspace has neither (see DEVELOPMENT_PLAN.md).
2. **`QuickSim` is a placeholder.** Ball outcomes come from simple
   probabilities, not physics. Test-match scoring rates (~5-6 an over) are too
   high. Physics will replace it.
3. **Run-out ends are simplified.** The new batter takes the dismissed
   batter's end based on runs completed. The real law (which end the wicket
   was broken at, whether batters had crossed) needs the physics position data.
4. **No-ball scoring** follows Law 21.12 (MCC Laws 2017): bat runs → striker,
   byes → byes, leg-byes → leg-byes, plus the 1-run penalty (fixed in 0.2.1).
   Penalty runs for other offences (Law 41) are still not modelled. Laws were
   implemented from the MCC text. A qualified scorer should still check them
   before release.
5. **Shortened-match rules**: rain-reduced overs (DLS) are not implemented.
   Custom-overs games use a T20-style ~30% powerplay.
6. **DRS evidence is a SIMULATED PLACEHOLDER, not real DRS technology.**
   Ball tracking, edge detection, grounding replays and front-foot checks are
   random numbers from `QuickSim::MakeCase`. There is no Hawk-Eye, UltraEdge,
   Snickometer or any licensed system, and no real replays. The decision
   *rules* are real and tested; the *evidence* is not. Server-side ball
   physics will produce it later.
7. **AI review decisions are a stop-gap.** `AutoCaptain::ShouldReview` looks
   at the true simulated evidence and then makes random mistakes by
   difficulty. A real captain can't see the replay first. It also ignores
   match context and has one personality. It must not be used for ranked
   online play. The replacement plan (perception-based, never reads the
   evidence, enforced by a test) is in DRS_DESIGN.md.
8. **Other DRS gaps**: no reviews for hit wicket / obstructing; no TV checks
   of boundaries or short runs; players can't challenge wide / no-ball calls.
   The review timer needs the server to call `Tick()` regularly. If it
   doesn't, the window stays open. A player who disconnects while their
   review window is open is not handled specially: the timer simply expires.
9. **Test match session breaks** (lunch/tea/stumps), new-ball availability
   after 80 overs and save/resume are not implemented.
10. **Penalty runs, retired hurt, substitutes and concussion replacements** are
   not implemented.
11. **Bowler change mid-over** (injury) is not supported.
12. The project specification text was **cut off** at section 25
    ("Test: ... Multiplaye"). Any later sections still need to be shared.
