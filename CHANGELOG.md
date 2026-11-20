# Changelog

## [0.4.0] - 2026-11-20 - Rules milestone, Stage B: Test match sessions

### Added
- Test match schedule (`SessionSchedule`): 90 overs a day in 3 sessions,
  lunch / tea / stumps (tea / dinner for day-night), day progression, 2 overs
  lost per change of innings, early interval when an innings ends within 10
  minutes of a break (Law 11.4), match drawn at the last day's stumps.
- Follow-on margin by days left when play started (Law 14.1: 200/150/100/75).
- `IntervalRecord` history in every format (innings breaks included).
  `DayClock`, `BallsLeftInSession`, `BallsLeftToday`.
- Session: pauses for lunch / tea / dinner / stumps when humans play.
  `ResumePlay` command (all humans ready) and server `ResumeAfterInterval`.
  Events `lunch`, `tea`, `dinner`, `stumps`, `innings-break`, `ready`,
  `resume`. Commentary for breaks (English + Hindi).
- `docs/TEST_SESSIONS.md`. 15 new tests (140 total).

### Changed
- `MatchRules::Test()` uses the session schedule instead of the old flat
  `matchOverLimit` (still available as an optional hard cap). A 1-day Test
  still draws after 90 overs as before.

## [0.3.0] - 2026-11-20 - Rules milestone, Stage A: DRS completed

### Added
- Hit-wicket reviews and referrals. Obstructing-the-field referrals (still
  out off a no-ball).
- Scoring-call decisions: wide and height no-ball reviews (ruleset flag
  `reviewWidesAndNoBalls`, name suffix `:calls`) and TV boundary checks.
- Reason codes for every DRS decision, explained in English and **Hindi**.
- Perception-based AI reviews (`ReviewAI`): belief × value vs cost,
  personalities, difficulty = judgement noise. Never reads the evidence
  (enforced by a source-scan test).
- Short runs (Law 18.4), penalty runs (Law 41) with target recomputation,
  new ball after 80 overs (Test), AI takes the new ball.
- `MatchRules::Scaled(overs)`. Each innings keeps its own copy of the rules
  (preparing for rain-reduced innings).
- Session: `TakeNewBall` command, `AwardPenaltyRuns`, events `penalty` and
  `new-ball`. Commentary for calls, boundary checks, penalties, new ball and
  short runs.

## [0.2.1] - 2026-11-20 - Step 1 DRS: review corrections

### Fixed
- **No-ball runs** now follow Law 21.12 (MCC Laws 2017). New
  `Delivery::noBallRunsAs` (None = off the bat, Bye, LegBye). Byes and
  leg-byes off a no-ball are recorded as byes / leg-byes and not charged to
  the bowler. Bat runs go to the striker.
- **DRS no longer throws runs away** when it finds a no-ball. Bat runs, byes
  and leg-byes keep their origin. A ball called wide becomes a no-ball with
  its runs as byes. (Runs after an OUT that is overturned are still dropped:
  the ball was dead at the original decision.)
- Without a third umpire, run outs / stumpings were refused and the ball
  was lost. Now the on-field umpire decides.

### Added
- `DrsRules` (configurable per competition): player reviews on/off, third
  umpire on/off, reviews per innings, umpire's call keeps or loses the
  review, review timer length. Presets `Icc`, `NoPlayerReviews`,
  `NoTechnology`, `Custom`, plus a name parser. Simulator option `--drs`.
- Server-side **15-second review timer**: `MatchSession::Tick`, `SubmitAt`,
  `ReviewMsLeft`, `Pending::deadlineMs`, and the `review-timeout` event. On
  expiry the on-field decision stands, no review is used, and the no-ball
  check still applies.
- Documentation: DRS evidence clearly marked as a simulated placeholder. AI
  review limitations and replacement plan (DRS_DESIGN.md, KNOWN_ISSUES.md,
  code comments).
- 33 new tests (98 total): 17 no-ball scoring, 16 rulesets + timer.

### Changed
- `MatchRules::reviewsPerInnings` replaced by `MatchRules::drs.reviewsPerInnings`.
- `ExpireReviewWindow()` kept for manual use. `Tick()` is now the normal path.

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
