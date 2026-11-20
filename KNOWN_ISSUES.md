# Known Issues and Limitations

1. **No 3D game yet.** The Unreal Engine project needs a Windows/Linux PC with
   a GPU. This cloud workspace has neither (see DEVELOPMENT_PLAN.md).
2. **`QuickSim` is a placeholder.** Ball outcomes come from simple
   probabilities, not physics. Test-match scoring rates (~5-6 an over) are too
   high. Physics will replace it.
3. **Run-out ends are simplified.** The new batter takes the dismissed
   batter's end based on runs completed. The real law (which end the wicket
   was broken at, whether batters had crossed) needs the physics position data.
4. **No-ball byes/leg-byes**: runs off a no-ball are always credited to the
   batter. Byes off a no-ball are not modelled yet. When DRS finds a no-ball on
   a bye/leg-bye/wide ball, those runs are dropped and only the penalty counts.
5. **Shortened-match rules**: rain-reduced overs (DLS) are not implemented.
   Custom-overs games use a T20-style ~30% powerplay.
6. **DRS limits**: the evidence (edge, grounding, ball tracking) comes from the
   server's simulation. Until Unreal physics exists, `QuickSim` makes it up
   with simple probabilities. Not modelled yet: reviews for other modes
   (hit wicket, obstructing), "soft" checks of boundaries / short runs by the
   TV umpire, and the 15-second review timer itself (the server must call
   `ExpireReviewWindow()`). Players can't challenge a wide or no-ball call.
   The AI's review choice peeks at the evidence with a difficulty-based error
   rate, which approximates "what the players saw".
7. **Test match session breaks** (lunch/tea/stumps), new-ball availability
   after 80 overs and save/resume are not implemented.
8. **Penalty runs, retired hurt, substitutes and concussion replacements** are
   not implemented.
9. **Bowler change mid-over** (injury) is not supported.
10. The project specification text was **cut off** at section 25
    ("Test: ... Multiplaye"). Any later sections still need to be shared.
