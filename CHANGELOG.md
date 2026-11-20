# Changelog

## [0.2.0] - 2026-11-20 - Priority 1, Step 1: DRS

### Added
- `Drs.h/.cpp`: player reviews for caught and LBW. Edge, clean-catch and
  ball-tracking evidence. Caught→LBW secondary check. Inconclusive evidence
  rules (no soft signal).
- Third-umpire referrals: run out, stumping (including off a wide), clean
  catch. Benefit of the doubt to the batter.
- Automatic front-foot no-ball check on every decision (run outs still
  stand).
- `Match::Appeal / ReviewDecision / AcceptDecision`: 2 unsuccessful reviews
  per side per innings, reset each innings. Umpire's call keeps the review.
  Dead-ball run correction. Play held while a decision is pending.
  `Decisions()` history.
- `MatchSession::ResolveAppeal`, `ExpireReviewWindow`, `CommandType::Review`.
  New events `appeal`, `review`, `decision`, `third-umpire`.
- AI review decisions by difficulty. `QuickSim::MakeCase` produces realistic
  appeals.
- DRS commentary (English + Hindi). The simulator shows appeals and reviews.
- 26 new tests (65 total). `docs/DRS_DESIGN.md`.

### Changed (no behaviour change)
- `Innings::Check()` split out of `Innings::Bowl()` so a delivery can be
  validated before it is applied.
- `ReviewOutcome` gained `ThirdUmpire` and `NotReviewed` (appended values).

## [0.1.0] - 2026-11-20 - Milestone 1 (gameplay core)

### Added
- `cricket_core` C++20 library with no engine dependencies.
- Format rules: T20, ODI, Test (3/4/5 day), custom overs. Powerplay phases,
  bowler quotas, free hits, follow-on thresholds, Test time limit.
- Innings scorer: runs, boundaries, wides, no-balls, byes, leg-byes, maidens,
  milestones, all 11 dismissal types with legality rules, strike rotation,
  over completion, consecutive-over and quota checks, declarations.
- Match flow and results: win by runs / wickets / innings and runs, tie, draw,
  no result.
- Toss with server-seeded coin and captain permissions.
- LBW decision (Law 36) and DRS review outcomes including umpire's call.
- Field placement: 26 named positions, 10 presets, manual moves, circle
  restrictions, leg-side rule, pitch and boundary checks.
- AI captain: bowling changes, batting order, fields, follow-on, declarations,
  toss.
- Commentary engine: lead + expert, English and Hindi, no back-to-back repeats.
- Server-authoritative `MatchSession`: sides, turn checks, sequence-number
  replay protection, event log with catch-up after reconnect, AI fill-in.
- 10 fictional teams.
- `cricket_sim` text-mode simulator and 39 tests.
- Design docs: architecture, plan, multiplayer, commentary, leaderboard,
  asset register, test report, known issues.
