# Rain, Revised Targets and No-Result

> ## ⚠️ NON-OFFICIAL rain rule
> Revised targets in Cricket Unleashed come from the **CU Rain Rule**, our
> own method. It is **not** the official Duckworth-Lewis-Stern (DLS) method.
> It does not use the ICC's DLS tables, parameters or software, and its
> targets **will differ** from official DLS targets. Wherever it decides a
> target or result, the game shows "(rain rule)", and the revised-target
> event says "non-official". If an official DLS licence is obtained later,
> only `core/src/RainRule.cpp` needs to be replaced.

Code: `RainRule.h/.cpp` (the method), `Match::RainDelay / ReduceOvers /
Abandon / ParScoreNow / LoseRestOfDay`, `MatchSession` weather actions.
Tests: `core/tests/RainTests.cpp`.

## The CU Rain Rule

**Idea (publicly described; resource-based methods share it):** a batting
side's run-scoring "resources" depend on how many overs and wickets it has
left. When rain takes overs away, it loses resources, and the target is
scaled to the resources each side actually had.

**Our resource curve:**
`R(u, w) = 100 × F(w) × (1 − e^(−b·u / F(w))) / (1 − e^(−50·b))`
- `u` = overs left (fractions allowed), `w` = wickets lost.
- `b = 0.0272` per over, chosen by us so that a 20-over innings has about
  56% of the resources of a 50-over innings.
- `F(w)` = wicket factors, solved by us so that with 50 overs left the
  resources are **100, 93, 85, 75, 63, 50, 36, 23, 12, 5 %** for 0-9
  wickets lost. These anchor points are our own round numbers.

**Revised target** (`RainTarget`), with S = first-innings total, R1 and R2 the
resources each side had:
- R2 ≤ R1: par = S × R2 / R1
- R2 > R1: par = S + G × (R2 − R1) / 100, where G is our "full innings
  value": 245 for 50-over matches, 275 for 20-over matches (our parameters).
- Target = par rounded down, + 1. Ending exactly on par − 1 = **tie**.

**Resources lost** at each cut = R(overs left before, wickets) − R(overs
left after, wickets), charged to the innings in progress. A side whose
innings is shortened before it starts simply begins with R(new overs, 0).

## Limited-overs rules implemented

| Rule | Behaviour |
|---|---|
| Lost time → overs | minutes are first absorbed by a buffer (ODI 30, T20 10 minutes), then converted at 4.2 (ODI) / 4.25 (T20) minutes per over, rounded up |
| Rain in the 1st innings | overs taken off both innings **equally**. If the first side has already batted beyond the new length, its innings ends now and the second side absorbs the rest of the lost overs |
| Rain in the 2nd innings | overs come off the chase, and the target is revised at once. If the revised target is already reached, the chasing side wins |
| Chase cut to the overs already bowled | the innings ends and the result is decided on the revised target |
| `ReduceOvers(a, b)` | sets innings lengths directly (umpires' announcement). Lengths can only go down, and never below the overs already started |
| Bowler limits | one fifth of the new innings length, rounded up (a bowler who already bowled more simply can't bowl again) |
| Powerplays / fielding phases | shrink in proportion to the new length (approximation of the official tables) |
| **Minimum overs** for a result | T20 5, ODI 20 (custom: up to 5) for the side batting second. If fewer are possible → **No result** |
| **Abandoned** during the chase | after the minimum overs → result on the **par score** at that moment (win by wickets / tie / lose by runs). Before that → No result. In the first innings → No result |
| Penalty runs to the first side during a rain-affected chase | S changes, and the target is recomputed |

## Test matches

- Rain takes **time off the day**: `ceil(minutes / 4)` overs (15 overs an
  hour), never more than is left in the day. If it rains through lunch or
  tea, those breaks are recorded as taken.
- Lost time is **made up on later days**, up to 98 overs a day (8 extra).
  Anything beyond that is lost for good.
- `LoseRestOfDay()` ends the day (stumps), even mid-over. The over is
  finished next morning.
- A **first day lost without a ball** lowers the follow-on margin (Law
  14.1.3: 5-day match → 150).
- Washed out on the last day → **draw**. `Abandon()` after play started →
  draw; with no ball bowled → abandoned (no result).

## Approximations (documented, not hidden)

1. The whole rain method is our own (see the banner). It is not DLS.
2. The buffer minutes and minutes-per-over values are typical round numbers,
   not a specific competition's playing conditions. All are settings in
   `MatchRules::weather`.
3. Real playing conditions also shorten intervals, set cut-off times and use
   per-format powerplay tables for reduced matches. We use proportional
   scaling and simple time-to-overs conversion.
4. The par-score margin is reported in runs (par − score), the usual way of
   expressing it.
