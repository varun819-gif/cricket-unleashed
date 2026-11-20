# DRS and Third-Umpire Design

Code: `core/include/cricket/Drs.h`, `core/src/Drs.cpp`, wired in through
`Match` and `MatchSession`. Tests: `core/tests/DrsTests.cpp`.

## Two paths to the TV umpire

| Path | Used for | Who starts it | Uses a team review? |
|---|---|---|---|
| **Player review** | Caught, LBW | the side the on-field call went against | yes, if unsuccessful |
| **Umpire referral** | Run out, stumping, clean-catch check | the umpires | never |

On **every** decision the TV umpire also checks for a **front-foot no-ball**.
It cancels any dismissal except a run out, even when nobody reviews.

## Flow

```
ball bowled ──► close call?
                 │ no  ─► ResolveDelivery (normal scoring)
                 │ yes ─► ResolveAppeal(DecisionCase)
                           │
            UmpireReferral ┴ PlayerAppeal
                 │              │ on-field OUT / NOT OUT, play held
     JudgeReferral (instant)    │ WaitingFor() = "review", side = who may review
                 │              ├─ Review command (a=1) ─► JudgeReview
                 │              ├─ Review command (a=0) ─► JudgeWithoutReview
                 │              └─ ExpireReviewWindow()  ─► JudgeWithoutReview
                 ▼              ▼
            FinalDelivery ─► scored by Innings (all the usual law checks)
```

`DecisionCase` carries both versions of the ball, as given by the server's
physics: `ifOut` (scored if the final answer is OUT) and `ifNotOut`. Both are
checked against the laws **when the appeal is made**, so a decision can never
get stuck half-applied.

## Rules implemented

**Caught (review)**
- Ball touched the ground, or not sure it was a clean catch → NOT OUT.
  There is no "soft signal", so doubt goes to the batter.
- Clear edge → OUT. Clear no edge → NOT OUT.
- No edge **and** ball-tracking data present → the TV umpire also checks
  LBW, with the on-field LBW call treated as NOT OUT. "Umpire's call" then
  stays not out. Hitting → OUT LBW.
- Edge inconclusive → the on-field decision stands and the review is lost.

**LBW (review)**: the existing Law 36 + ball-tracking logic (`ReviewLbw`).
Umpire's call keeps the review.

**Run out / stumping (referral)**: batter grounded in time → NOT OUT.
Short → OUT. Inconclusive → NOT OUT (benefit of the doubt). Stumping off a
wide counts the wide **and** the wicket. A stumping on a no-ball is not out.
A run out on a no-ball still stands (+1 no-ball).

**Clean catch (referral)**: needs bat contact and the ball not grounded. Any
doubt → NOT OUT.

**Review counts**: `MatchRules::reviewsPerInnings` (2) unsuccessful reviews
per side **per innings**, reset when each innings starts. Overturned and
umpire's call reviews are kept. With 0 left, the review request is refused.

**Runs when a decision changes**
- Given OUT, then overturned → the ball was dead when the finger went up, so
  any runs are dropped. A no-ball penalty still counts.
- Given NOT OUT, then overturned to OUT → scored as `ifOut` (no runs).
- NOT OUT that stands → `ifNotOut` runs count (play carried on).

**Play is held** while a decision is pending: no `Bowl`, no second appeal,
no declaration.

## AI

`AutoCaptain::ShouldReview` reads the replay correctly with a probability
set by difficulty: Beginner 55%, Easy 65%, Medium 75%, Hard 85%, World Class
92%, Adaptive 80%. With its last review it holds back more often.

## Multiplayer

- The **server** creates the `DecisionCase` from its own physics and replays.
  Clients never send evidence or decisions.
- `CommandType::Review` (`a=1` review, `a=0` accept) is accepted only from
  the entitled side, with the usual sequence-number duplicate protection.
- The review timer (15 s in international cricket) runs on the server. When
  it ends, the server calls `ExpireReviewWindow()`.
- Events: `appeal`, `review` / `decision`, `third-umpire`, each with a
  `DecisionRecord` for replays and commentary. Both players see the same
  events.
