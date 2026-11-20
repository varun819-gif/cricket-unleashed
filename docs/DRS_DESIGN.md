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

## Two paths to the TV umpire

| Path | Used for | Who starts it | Uses a team review? |
|---|---|---|---|
| **Player review** | Caught, LBW | the side the on-field call went against | yes, if unsuccessful |
| **Umpire referral** | Run out, stumping, clean-catch check | the umpires | never |

On **every** decision the TV umpire also checks for a **front-foot no-ball**.
It cancels any dismissal except a run out, even when nobody reviews. (This
only happens when the competition has a third umpire.)

## Rulesets (configurable per competition)

`MatchRules::drs` is a `DrsRules` value, so each competition picks its own:

| Field | Meaning | International default |
|---|---|---|
| `playerReviews` | teams may review | true |
| `thirdUmpire` | TV umpire for run outs / stumpings / clean catches / no-ball checks | true |
| `reviewsPerInnings` | **unsuccessful** reviews per side per innings | 2 |
| `umpiresCallRetainsReview` | umpire's call does not cost a review | true |
| `reviewTimerSeconds` | time to ask for a review (0 = no timer) | 15 |

Presets: `DrsRules::Icc()`, `NoPlayerReviews()` (TV umpire only),
`NoTechnology()` (on-field umpires decide everything), and
`Custom(reviews, seconds, umpiresCallRetains)`. Config files and the
simulator use names: `icc`, `no-reviews`, `no-tech`,
`custom:<reviews>:<seconds>[:lose]` (for example
`cricket_sim --drs custom:1:10:lose`).

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

## AI reviews: current limitations and replacement plan

**How it works today (a stop-gap).** `AutoCaptain::ShouldReview` calls the
real judge (`JudgeReview`) on the true evidence to find out whether a review
would succeed. Then it adds deliberate mistakes: it "reads the replay"
correctly 55% (Beginner) to 92% (World Class) of the time, and holds back
more with its last review.

**Why that is not good enough:**
1. **It peeks at hidden information.** The AI effectively sees the replay
   before deciding. No real captain can. The error rate hides this, but the
   AI's mistakes are random coin flips, not believable misjudgements.
2. **No context.** It ignores the match situation (a key batter vs a
   tail-ender, last over of a chase), how many reviews are left (beyond a
   simple rule), and who is asking (bowlers are famously over-optimistic,
   keepers are better judges).
3. **Same personality for everyone.** All AI captains review the same way.
4. **Fairness in multiplayer.** In a human vs AI match, the AI's review
   quality comes from information the human player doesn't have.

**Replacement plan** (once ball physics exists, Milestones 4-5):
1. **Belief, not truth.** Each fielder and batter gets a *perception* of the
   ball. It is built only from what that person could see or hear: their
   position and angle, the ball's speed, the deflection they saw, a "sound"
   cue for edges. It includes noise based on their skill and distance. The
   AI decides from this belief and never reads `DrsEvidence` at all.
2. **Who advises the captain.** Combine the bowler's view (optimistic bias),
   the keeper's view (better line, less bias) and the batter's own feel (for
   batting reviews).
3. **Decision value.** Review when *P(success) × value of the wicket* is
   greater than *the cost of losing a review*, given reviews left, overs
   left, batter quality and match pressure.
4. **Personalities.** Cautious, balanced and gambler captains. Difficulty
   changes perception noise, not dice.
5. **Guard-rail.** A test will check that the AI review code never
   includes or calls the evidence-judging functions. Information hiding gets
   enforced, not just promised.

Until then, the current AI is clearly marked in code and in KNOWN_ISSUES.md.
It is fine for single-player testing, but it must not be used for ranked
online play.

## Multiplayer

- The **server** creates the `DecisionCase` from its own physics and replays.
  Clients never send evidence or decisions.
- `CommandType::Review` (`a=1` review, `a=0` accept) is accepted only from
  the entitled side, inside the time window, with sequence-number
  duplicate protection.
- Events: `appeal` (includes the timer length), `review`, `decision`,
  `third-umpire`, `review-timeout`. Each carries a `DecisionRecord` for
  replays and commentary. Both players see the same events.
