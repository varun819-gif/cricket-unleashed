# Changelog

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
