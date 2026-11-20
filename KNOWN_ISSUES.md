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
   batter. Byes off a no-ball are not modelled yet.
5. **Shortened-match rules**: rain-reduced overs (DLS) are not implemented.
   Custom-overs games use a T20-style ~30% powerplay.
6. **DRS is LBW-only** and review counts are not yet tracked per innings in
   `MatchSession`. Caught-behind reviews (UltraEdge) are pending.
7. **Test match session breaks** (lunch/tea/stumps), new-ball availability
   after 80 overs and save/resume are not implemented.
8. **Penalty runs, retired hurt, substitutes and concussion replacements** are
   not implemented.
9. **Bowler change mid-over** (injury) is not supported.
10. The project specification text was **cut off** at section 25
    ("Test: ... Multiplaye"). Any later sections still need to be shared.
