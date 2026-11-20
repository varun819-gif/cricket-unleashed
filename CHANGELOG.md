# Changelog

## [0.8.0] - 2026-11-20 - Priority 2: Mobile controller

### Added
- `controller/` library: SHA-256 / HMAC / SHA-1 / base64, strict JSON, QR
  encoder, `ControllerHub` (QR pairing with out-of-band secret, mutual
  proof, device + session keys, signed numbered messages, roles,
  reconnection, exactly-once actions, limits, revoke, device persistence),
  `MatchHost` (phone controls → match, bowl/bat stages, per-phone views,
  timers held while a team's phone is offline), `LocalServer` (static files
  + WebSocket, POSIX).
- Phone app `controller/web/` (Android browsers): toss, captaincy, review
  countdown, bowling (delivery, pitch target, effort), batting (incoming
  ball, direction pad, timing tap), fielding, spectator view, automatic
  reconnection, instructions.
- `cricket_controller_host`: TV/PC program with terminal QR, TV page,
  fresh codes per slot, `--devices` persistence.
- Tests: 36 C++ (217 total), 6 Node (in ctest), Android-emulated browser
  test. Docs: MOBILE_CONTROLLER.md, CONNECT_YOUR_PHONE.md,
  MISSING_DETAILS.md, screenshots.

## [0.7.0] - 2026-11-20 - Run-out and stumping reviews

### Added
- Player reviews of run-out and stumping decisions made on the field,
  controlled by `DrsRules::reviewRunOutsAndStumpings` (on in the
  international preset, `:norunouts` to switch off, off with no player
  reviews). The simulator sometimes leaves them to the on-field umpire.
- Save format version 2 (stores the new setting). Version 1 saves still load.
- 5 new tests (181 total).

## [0.6.1] - 2026-11-20 - Rules milestone: integration and documentation

### Added
- End-to-end integration test (25 seeded matches, every format and DRS
  ruleset, appeals, rain, penalties, save + reload mid-match, identical
  continuation).
- A test that a save made right after a refused command keeps its sequence
  number (replay-attack protection after resume).
- Documentation refreshed: README, KNOWN_ISSUES (rewritten), TEST_REPORT,
  DEVELOPMENT_PLAN, ARCHITECTURE, MULTIPLAYER_DESIGN, COMMENTARY_DESIGN.

## [0.6.0] - 2026-11-20 - Rules milestone, Stage D: save, resume and recovery

### Added
- Action journal in `Match` and input journal in `MatchSession` (refused
  inputs included), with `Apply` / `Replay` and canonical
  `StateFingerprint`.
- Versioned text save format for matches and server sessions
  (`SaveGame.h`): checksum, header / version / kind checks, rules
  validation, replay of every step, accepted/refused agreement for sessions,
  final-state fingerprint check.
- Safe file saves (temp file + `.bak` + atomic rename) and automatic backup
  fallback on load.
- Crash logs (`MatchJournal`, `SessionJournal`) with per-line checksums, and
  `RecoverMatch` / `RecoverSession` that keep every intact entry.
- `Match::ValidateRules`. `docs/SAVE_AND_RESUME.md`. 14 new tests (175
  total).

### Fixed (found by tests before release)
- FNV-1a offset basis constant was one digit short. Checksums now match the
  standard algorithm (reference values tested).

## [0.5.0] - 2026-11-20 - Rules milestone, Stage C: rain and revised targets

### Added
- **CU Rain Rule (non-official)**: our own resource-based method for revised
  targets and par scores (`RainRule.h`). Clearly labelled; not DLS.
- Limited overs: `RainDelay(minutes)` with buffer and minutes-per-over
  conversion, equal cuts in the first innings, termination when the first
  side has already batted longer, `ReduceOvers(a, b)`, revised targets,
  immediate win when the revised target is already reached, minimum overs
  (T20 5 / ODI 20) and no-result, abandonment decided on the par score,
  rescaled bowler limits and powerplays for shortened innings.
- Tests: time lost from the day, make-up up to 98 overs a day, rain through
  lunch and tea, `LoseRestOfDay` (mid-over), first day washed out → lower
  follow-on margin, washed-out last day → draw, abandon → draw / no result.
- Session: `RainDelay`, `ReduceOvers`, `LoseRestOfDay`, `AbandonMatch`;
  events `rain`, `overs`, `target`, `abandoned`. Simulator `--rain`.
- `docs/RAIN_RULE.md`. 21 new tests (161 total).

### Fixed
- A rain-shortened chase that ended at once was judged against the old
  target. The target is now revised first.
- Event order: the innings break is now logged before the next innings
  starts.
- AI, field checks and commentary now use the current innings' own rules
  (shortened innings).

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
