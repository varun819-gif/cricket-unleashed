# Test Match Sessions and Days

Code: `SessionSchedule` (`MatchRules.h`), `Match::CheckSchedule / StartDay /
Clock() / Intervals()`, `MatchSession` pause and resume. Tests:
`core/tests/TestSessionTests.cpp`.

## The model (an approximation of the clock)

Real Test cricket runs on **clock time**: fixed session times, a minimum
number of overs in the day, the extra half hour, the last hour, and over-rate
rules. A game has no real clock to follow, so this engine uses **overs as the
clock**:

| Setting (`MatchRules::schedule`) | Default | Based on |
|---|---|---|
| `days` | 5 (`MatchRules::Test(days)`) | match length |
| `oversPerDay` | 90 | ICC Test minimum overs per day |
| `sessionsPerDay` | 3 | morning / afternoon / evening sessions |
| `oversLostPerInningsChange` | 2 | 10-minute changeover = 2 overs (ICC Test playing conditions) |
| `intervalNearOvers` | 2 (plus half an over = 2.5 overs ≈ 10 minutes) | Law 11.4: innings ending within 10 minutes of an interval |
| `maxOversPerDay` | 98 | up to 8 extra overs a day to make up lost time |
| `dayNight` | false | day-night Tests: tea after session 1, dinner after session 2 |

## What happens

- **Sessions** split the day's available overs evenly (30 / 30 / 30). Breaks
  fall **between overs**: lunch (or tea) after session 1, tea (or dinner)
  after session 2, **stumps** when the day's overs are done.
- **The over in progress is completed** before stumps. An innings that
  started part-way through the day's over pattern can run slightly past
  the 90-over mark. The extra balls are not taken off the next day.
- **Changeover**: each change of innings (all out, declaration, follow-on)
  takes 2 overs off the day. An `InningsBreak` interval is recorded in every
  format.
- **Early interval** (Law 11.4): if an innings ends within about 10 minutes
  (2.5 overs) of a scheduled break, the break is taken at once (`early`
  flag).
- **Day progression**: after stumps the next day starts at session 1. Lost
  time (from rain, Stage C) is made up on later days, up to 98 overs a day.
- **Match end**: a result can come at any time. If the last day's stumps
  arrive first, the match is **drawn**.
- **Follow-on (Law 14.1)**: 200 runs in a 5-day match, 150 in 3 or 4 days, 100
  in 2 days, 75 in 1 day. The margin uses the days left **when play actually
  started** (Law 14.1.3), so a washed-out first day of a 5-day Test makes it
  150.
- **New ball**: available after 80 overs with the current ball (see
  DRS_DESIGN.md, related laws).

## Online play

- The rules engine only **records** breaks. It never blocks a ball, so AI
  matches, tools and the original tests are unaffected.
- `MatchSession` **pauses** at lunch, tea, dinner and stumps when at least one
  human is playing. `WaitingFor()` returns `"interval"`, and deliveries and
  turn commands are refused.
- Play resumes when **every human** sends `ResumePlay`, or when the server
  calls `ResumeAfterInterval()` (the server's break timer). Innings breaks
  don't pause.
- Events: `lunch`, `tea`, `dinner`, `stumps`, `innings-break`, `ready`,
  `resume`. Commentary has a line for each break (English + Hindi).

## Approximations (documented, not hidden)

1. Overs stand in for clock time. There are no real session times, no
   extra half hour, no "last hour with 15 overs minimum", and no over-rate
   penalties.
2. Drinks breaks are not modelled (they don't change the cricket).
3. When a break is due, it is taken at the end of the over in progress, not
   at an exact clock time.
4. Session lengths are equal shares of the day's available overs.
