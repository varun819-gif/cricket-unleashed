# Test Report

**Date:** 2026-11-20 (Step 1 DRS) · **Build:** GCC 13.3, Debug with
AddressSanitizer + UndefinedBehaviorSanitizer, and Release · **Result: 65 / 65
passed (39 original + 26 DRS), no sanitizer errors, no compiler warnings
(-Wall -Wextra -Wpedantic).**

Run: `cmake -S . -B build && cmake --build build && ./build/cricket_tests`

| Suite | Tests | What they prove |
|---|---|---|
| Innings | 16 | strike rotation, over ends, wides/no-balls/byes, free hit (incl. carry-over on a wide), dismissal legality per ball type, invalid inputs rejected, maidens, caught/run-out new-batter end, wicket on last ball, all out, bowler quota, target reached, fifty flag, declarations |
| Match | 13 | T20 chase/defend/tie, Test follow-on + innings win, 4th-innings chase, draw on time, team validation, toss permissions + 50/50 fairness over 20,000 tosses, LBW law, DRS outcomes, **46 full AI-vs-AI matches (30 T20, 10 ODI, 6 Test)** checking runs = batters + extras, balls = bowler balls, no consecutive overs, quotas respected, commentary never empty, no back-to-back repeated lines |
| Field | 5 | position geometry, all 10 presets legal after powerplay, powerplay/ODI phase limits, max 2 behind square leg, manual placement on pitch / beyond boundary / stacked rejected |
| Session | 5 | join rules, toss turn order, duplicate/replayed command rejected, wrong-side commands rejected, full human-vs-human match, human-vs-AI with dropped updates and reconnect catch-up |
| DRS | 26 | caught: no edge / edge / not-out overturned / accepted keeps runs / inconclusive edge (both ways) / grounded + inconclusive clean catch / no edge but LBW / no edge LBW umpire's call; LBW via match (overturned, umpire's call keeps review, pitched outside leg); front-foot no-ball without review + found on review + run out still stands on no-ball; run-out referrals (out / in / inconclusive); stumping (off a wide, on a no-ball, in time); clean-catch referrals; 2-review limit + reset next innings; play blocked while pending; invalid appeals rejected (incl. caught on free hit); wicket on last ball via review; pure judge functions; commentary EN + HI; session: human review flow, wrong side + duplicate rejected, review timer expiry, referral flow, AI reviews by itself; **27 full AI-vs-AI matches with appeals (20 T20, 4 ODI, 3 Test)** checking reviews never exceed the limit and the books balance |

### Mutation check (do the DRS tests catch real bugs?)
Four bugs were planted in `Drs.cpp` one at a time. Every one made at least one test fail:
review never lost (3 failures), no-ball cancels run outs (1), dead-ball runs kept (2),
inconclusive run out given out (1). The original code was restored afterwards.

## Not tested yet (needs later milestones)

Ball physics, batting/bowling input, animation, rendering performance, Android
TV hardware, network transport, matchmaking, database, mobile controller, audio.
