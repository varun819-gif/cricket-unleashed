# Test Report

**Date:** 2026-11-20 (Step 1 DRS corrections, v0.2.1) · **Build:** GCC 13.3,
Debug with AddressSanitizer + UndefinedBehaviorSanitizer, and Release ·
**Result: 98 / 98 passed (39 original + 26 DRS + 17 no-ball + 16 DRS
rules/timer), no sanitizer errors, no compiler warnings (-Wall -Wextra
-Wpedantic).** The 39 original tests are unchanged since Milestone 1.

Run: `cmake -S . -B build && cmake --build build && ./build/cricket_tests`

| Suite | Tests | What they prove |
|---|---|---|
| Innings | 16 | strike rotation, over ends, wides/no-balls/byes, free hit (incl. carry-over on a wide), dismissal legality per ball type, invalid inputs rejected, maidens, caught/run-out new-batter end, wicket on last ball, all out, bowler quota, target reached, fifty flag, declarations |
| Match | 13 | T20 chase/defend/tie, Test follow-on + innings win, 4th-innings chase, draw on time, team validation, toss permissions + 50/50 fairness over 20,000 tosses, LBW law, DRS outcomes, **46 full AI-vs-AI matches (30 T20, 10 ODI, 6 Test)** checking runs = batters + extras, balls = bowler balls, no consecutive overs, quotas respected, commentary never empty, no back-to-back repeated lines |
| Field | 5 | position geometry, all 10 presets legal after powerplay, powerplay/ODI phase limits, max 2 behind square leg, manual placement on pitch / beyond boundary / stacked rejected |
| Session | 5 | join rules, toss turn order, duplicate/replayed command rejected, wrong-side commands rejected, full human-vs-human match, human-vs-AI with dropped updates and reconnect catch-up |
| DRS | 26 | caught: no edge / edge / not-out overturned / accepted keeps runs / inconclusive edge (both ways) / grounded + inconclusive clean catch / no edge but LBW / no edge LBW umpire's call; LBW via match (overturned, umpire's call keeps review, pitched outside leg); front-foot no-ball without review + found on review + run out still stands on no-ball; run-out referrals (out / in / inconclusive); stumping (off a wide, on a no-ball, in time); clean-catch referrals; 2-review limit + reset next innings; play blocked while pending; invalid appeals rejected (incl. caught on free hit); wicket on last ball via review; pure judge functions; commentary EN + HI; session: human review flow, wrong side + duplicate rejected, review timer expiry, referral flow, AI reviews by itself; **27 full AI-vs-AI matches with appeals (20 T20, 4 ODI, 3 Test)** checking reviews never exceed the limit and the books balance |

| No-ball | 17 | dot no-ball; 2 / 4 / 6 off the bat; 1 bye, 4 byes, 2 leg-byes, 4 leg-byes (striker credited with a ball faced, nothing to striker, bowler charged penalty only); invalid combinations (six byes/leg-byes, runs-type on a legal ball or wide); run out with byes off a no-ball; free hit after no-ball byes; commentary says byes not "four"; DRS-found no-ball keeps bat runs / byes / leg-byes; wide becomes no-ball with byes; given-out-then-no-ball = penalty only |
| DRS rules + timer | 16 | presets + name parsing; review count follows ruleset (1 and 3); umpire's call costs a review when configured; no-player-reviews (on-field final, TV umpire still for run outs); no-technology (on-field decides referrals, missed no-ball stays missed); negative settings rejected; full matches under 5 rulesets with no lost deliveries; timer: review at last ms accepted, expiry at exactly 15 s (on-field OUT stands, no review used, late request refused), expiry keeps not-out runs, late message refused, expiry still applies no-ball check, custom 10 s and disabled timer, clock can't go backwards, timer restarts per appeal, no timer for AI / no-review rulesets |

### Mutation check, corrections (v0.2.1)
Seven bugs were planted one at a time in reachable code. Every one made tests fail:
no-ball byes charged to the bowler (3 failures), leg-byes booked as byes (3), wide→no-ball runs
dropped (1), timer closing at 15.001 s instead of 15.000 s (3), umpire's-call setting ignored
(1), review count hard-coded to 2 (2), missed no-ball spotted without a third umpire (1).
An eighth mutation (disabling a second "late review" check) was NOT caught. On inspection
that branch could never run (the clock only moves through `Tick()`, which closes the window
first), so the dead code was removed. It also found a test that crashed instead of failing cleanly. That was fixed.

### Mutation check (do the DRS tests catch real bugs?)
Four bugs were planted in `Drs.cpp` one at a time. Every one made at least one test fail:
review never lost (3 failures), no-ball cancels run outs (1), dead-ball runs kept (2),
inconclusive run out given out (1). The original code was restored afterwards.

## Not tested yet (needs later milestones)

Ball physics, batting/bowling input, animation, rendering performance, Android
TV hardware, network transport, matchmaking, database, mobile controller, audio.
