# DRS and Third-Umpire Design

Code: `core/include/cricket/Drs.h`, `core/src/Drs.cpp`, wired in through
`Match` and `MatchSession`. Settings: `DrsRules` in `MatchRules.h`.
Tests: `core/tests/DrsTests.cpp`, `DrsConfigTests.cpp`, `NoBallTests.cpp`.

> ## ⚠️ Simulated placeholder: this is NOT real DRS technology
>
> Cricket Unleashed does **not** contain real ball tracking (such as
> Hawk-Eye), real edge detection (such as UltraEdge / Snickometer) or real
> broadcast replays. It has no licence for any of them and does not copy or
> imitate any of them.
>
> What exists today is the **decision logic**: given a piece of evidence
> ("the ball pitched in line", "there was an edge"), apply the cricket rules
> correctly. The evidence itself is **made up by a placeholder**:
>
> - `QuickSim::MakeCase` (in `AutoCaptain.cpp`) rolls dice to decide whether
>   there was an edge, where the ball pitched, whether it would hit the
>   stumps, whether the batter made their ground, and whether it was a
>   no-ball. These are **random numbers with rough probabilities**. They are
>   not measurements of anything.
> - Later, the game's own ball physics (Unreal, server-side) will compute the
>   evidence from the simulated ball's real path in the game world. Even then
>   it will be the game's **own** in-game tracking, not a real-world DRS
>   product, and the screens will not use real DRS brand names unless
>   licensed (see ASSET_REGISTER.md).
>
> So the DRS **rules** are tested and reliable. The DRS **evidence** is a
> stand-in until the physics exists.

## Ways a close call reaches the TV umpire

| Path | Used for | Who starts it | Uses a team review? |
|---|---|---|---|
| **Player review** of a dismissal | Caught, LBW, hit wicket | the side the on-field call went against | yes, if unsuccessful |
| **Umpire referral** of a dismissal | Run out, stumping, clean catch, hit wicket, obstructing the field | the umpires | never |
| **Player review** of a scoring call | Wide, above-waist (height) no-ball (only if `reviewWidesAndNoBalls`) | the side the call went against (wide called → fielders) | yes, if unsuccessful |
| **Umpire referral** of a scoring call | Boundary check (six or four? did the fielder touch the rope?) | the umpires | never |

On every **dismissal** decision the TV umpire also checks for a **front-foot
no-ball**, when the competition has a third umpire. It cancels any dismissal
except run out and obstructing the field (Law 21.18).

Run outs and stumpings: usually the umpires refer them. If an umpire decides
one on the field, players may review it when the competition allows
(`DrsRules::reviewRunOutsAndStumpings`; on in the international preset,
off with `:norunouts`). Doubt goes to the batter either way. If an OUT is
overturned, runs completed before the wicket was broken still count on a run
out. A stumping is a dead ball (wide penalty only).

## Rulesets (configurable per competition)

`MatchRules::drs` is a `DrsRules` value, so each competition picks its own:

| Field | Meaning | International default |
|---|---|---|
| `playerReviews` | teams may review | true |
| `thirdUmpire` | TV umpire for run outs / stumpings / clean catches / no-ball checks | true |
| `reviewsPerInnings` | **unsuccessful** reviews per side per innings | 2 |
| `umpiresCallRetainsReview` | umpire's call does not cost a review | true |
| `reviewTimerSeconds` | time to ask for a review (0 = no timer) | 15 |
| `reviewWidesAndNoBalls` | players may review wide and height no-ball calls | false |
| `reviewRunOutsAndStumpings` | players may review run-out / stumping decisions the umpires didn't refer | true |

Presets: `DrsRules::Icc()`, `NoPlayerReviews()` (TV umpire only),
`NoTechnology()` (on-field umpires decide everything), and
`Custom(reviews, seconds, umpiresCallRetains)`. Config files and the
simulator use names: `icc`, `no-reviews`, `no-tech`,
`custom:<reviews>:<seconds>[:lose][:calls]` (for example
`cricket_sim --drs custom:1:10:lose:calls`).

The presets describe *kinds* of setups, not any real tournament's playing
conditions. Each real competition's numbers should be checked against its
published playing conditions before they are entered.

Behaviour per setting:
- **No player reviews**: an appeal is decided by the on-field umpire straight
  away. There is no review window, and `ReviewDecision` is refused.
- **No third umpire**: referrals become the on-field umpire's call
  (`DecisionCase::onFieldOut`). A missed front-foot no-ball stays missed.
- **Umpire's call costs a review** (`:lose`): an umpire's-call review is
  counted as unsuccessful.

## Flow

```
ball bowled ──► close call?
                 │ no  ─► ResolveDelivery (normal scoring)
                 │ yes ─► ResolveAppeal(DecisionCase)
                           │
            UmpireReferral ┴ PlayerAppeal
                 │              │ on-field OUT / NOT OUT, play held,
     JudgeReferral (instant)    │ review window opens (timer starts)
                 │              ├─ Review command (a=1) ─► JudgeReview
                 │              ├─ Review command (a=0) ─► JudgeWithoutReview
                 │              └─ timer runs out        ─► JudgeWithoutReview
                 ▼              ▼
            FinalDelivery ─► scored by Innings (all the usual law checks)
```

`DecisionCase` carries both versions of the ball, as given by the server:
`ifOut` (scored if the final answer is OUT) and `ifNotOut`. Both are checked
against the laws **when the appeal is made**, so a decision can never get
stuck half-applied.

## The 15-second review timer

- The timer lives on the **server** (`MatchSession`), never on a phone or TV.
- The server passes its own monotonic clock in milliseconds to `Tick(nowMs)`
  from its game loop, and before handling every message (`SubmitAt(cmd,
  nowMs)` does both).
- When a player appeal goes pending, `deadline = now + reviewTimerSeconds × 1000`.
  `WaitingFor()` returns it as `deadlineMs`, and `ReviewMsLeft()` gives the
  countdown for the TV and phone UI.
- A review at `deadline − 1 ms` is accepted. When the clock reaches the
  deadline, the window closes:
  - a `review-timeout` event is logged,
  - the **on-field decision stands** (`NotReviewed`), and no review is used,
  - the automatic front-foot no-ball check still applies,
  - any review request arriving after that is refused.
- A clock that goes backwards is refused and changes nothing.
- The timer restarts for each new appeal. No timer runs when the AI is
  the entitled side (it decides instantly), when the ruleset has no player
  reviews, or when `reviewTimerSeconds = 0`.

## Rules implemented

**Caught (review)**
- Ball touched the ground, or not sure it was a clean catch → NOT OUT
  (no soft signal, so doubt goes to the batter).
- Clear edge → OUT. Clear no edge → NOT OUT.
- No edge **and** ball-tracking data present → the TV umpire also checks
  LBW, with the on-field LBW call treated as NOT OUT. Hitting → OUT LBW.
- Edge inconclusive → the on-field decision stands, and the review is lost.

**LBW (review)**: Law 36 + ball-tracking zones (`ReviewLbw`). Umpire's call
keeps the review unless the ruleset says otherwise.

**Run out / stumping (referral)**: grounded in time → NOT OUT, short → OUT,
inconclusive → NOT OUT. Stumping off a wide counts the wide **and** the
wicket. A stumping on a no-ball is not out. A run out on a no-ball still
stands.

**Clean catch (referral)**: needs bat contact and the ball not grounded. Any
doubt → NOT OUT.

**Hit wicket (review or referral, Law 35)**: did the batter break the wicket
while playing at the ball or setting off for the first run? Review:
inconclusive → on-field call stands. Referral: doubt → NOT OUT.

**Obstructing the field (referral, Law 37)**: out only if the obstruction was
wilful. Not wilful or not sure → NOT OUT. Possible off a no-ball.

**Scoring calls**: replays say the event happened (Yes), didn't (No), or
can't tell (Inconclusive → the on-field call stands, and a player review is
lost). A wide that is overturned becomes a fair ball (it counts in the over).
A height no-ball that is overturned removes the penalty and the free hit. A
boundary check can turn a signalled six into a four.

**Review counts**: `reviewsPerInnings` unsuccessful reviews per side, reset
at the start of every innings.

## Runs when DRS changes a decision

| Situation | Runs that count |
|---|---|
| NOT OUT stands (reviewed, accepted or timed out) | all runs of `ifNotOut`: bat runs, byes, leg-byes |
| NOT OUT overturned to OUT | `ifOut` (no runs on a catch / LBW / stumping; completed runs on a run out) |
| OUT overturned to NOT OUT | **none**: the ball was dead when the umpire gave it out (ICC DRS playing conditions). A no-ball penalty still counts |
| No-ball found, batter was NOT OUT on the field | penalty + **runs keep their origin**: off the bat → striker; byes → byes; leg-byes → leg-byes (Law 21.12) |
| No-ball found on a ball called wide | no-ball takes precedence over wide (Law 21.14). Runs completed are byes (not off the bat) |
| No-ball found, batter was given OUT on the field | penalty only (dead ball, as above) |

## No-ball scoring (Law 21.12, MCC Laws 2017)

`Delivery::noBallRunsAs` records where a no-ball's runs came from:

| `noBallRunsAs` | Team | Striker | Extras | Bowler charged |
|---|---|---|---|---|
| `None` (off the bat) | 1 + runs | runs (and the 4 / 6) | nb 1 | 1 + runs |
| `Bye` | 1 + runs | 0 | nb 1, byes = runs | 1 |
| `LegBye` | 1 + runs | 0 | nb 1, leg-byes = runs | 1 |

In every case the striker is credited with a ball faced, the ball is not
legal, and the next ball is a free hit (limited overs). A six can only come
off the bat.

## AI reviews (perception-based)

**How it works** (`core/src/ReviewAI.cpp`):
1. **Belief, not truth.** For every appeal the server's perception model
   fills `DecisionCase::perceived[side]`: how likely each side's players
   think it is that the batter is out (or that the call is true). Today the
   placeholder model (`QuickSim::Perceive`) starts from the true answer and
   adds noise (standard deviation 0.18) and optimism bias (fielders lean
   towards "out", batters towards "not out"). It points the right way on
   average but varies from appeal to appeal.
2. **Decision value.** The AI reviews when
   *belief × value of success > (1 − belief) × cost of losing a review*.
   Value grows with batter quality and runs scored, and shrinks for
   tail-enders and the last two overs. Cost grows as reviews run out and
   with how much of the innings is left. For a wide / no-ball call, the
   value is the runs (and extra ball) at stake.
3. **Personalities.** Cautious (cost × 1.4), Balanced, Gambler (cost × 0.6).
4. **Difficulty** blurs the AI's own judgement of its players' belief:
   Beginner ±0.30 … World Class ±0.03 (standard deviation).
5. **Guard-rail.** `drsc_ai_review_code_never_reads_evidence` scans
   `ReviewAI.cpp` and fails if it mentions the evidence, any judge function or
   the simulator. `drsc_ai_decision_ignores_evidence_and_follows_belief`
   checks that flipping the evidence while keeping the beliefs the same never
   changes the AI's choice.

**Remaining limitation:** the perception *model* is a placeholder. It is
built from the truth plus noise, not from what each player could physically
see. When ball physics exists, `Perceive` will be replaced by one built from
player positions, viewing angles and the ball's path. The AI code itself
won't need to change.

## Multiplayer

- The **server** creates the `DecisionCase` from its own physics and replays.
  Clients never send evidence or decisions.
- `CommandType::Review` (`a=1` review, `a=0` accept) is accepted only from
  the entitled side, inside the time window, with sequence-number
  duplicate protection.
- Events: `appeal` (includes the timer length), `review`, `decision`,
  `third-umpire`, `review-timeout`, `penalty`, `new-ball`. Each carries a `DecisionRecord` for
  replays and commentary. Both players see the same events.

## Related scoring laws added with DRS

- **Short runs (Law 18.4)**: `Delivery::shortRuns`. Those runs are not
  scored (bat, byes, leg-byes, wides or no-ball runs), but the batters' ends
  follow the runs *attempted*. Not allowed on a boundary. Deliberate short
  running (Law 18.5, penalty runs) is an umpire decision. The server applies
  it through penalty runs.
- **Penalty runs (Law 41)**: `Match::AwardPenaltyRuns(side, runs, reason)`
  (server/umpire only). To the batting side → current innings. To the
  fielding side → its most recent innings, or its next innings if it hasn't
  batted yet (Law 41.17). Targets are recomputed, and penalty runs can win a
  match. Shown as `extras.penalty`.
- **New ball (Law 4 / Test playing conditions)**: `TakeNewBall()` between
  overs once 80 overs have been bowled with the current ball. The AI takes it
  as soon as it is due. Human captains use the `TakeNewBall` command.
