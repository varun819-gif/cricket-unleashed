# Test Report

**Date:** 2026-11-20 · **Build:** GCC 13.3, Debug with AddressSanitizer +
UndefinedBehaviorSanitizer · **Result: 39 / 39 passed, no sanitizer errors,
no compiler warnings (-Wall -Wextra -Wpedantic).**

Run: `cmake -S . -B build && cmake --build build && ./build/cricket_tests`

| Suite | Tests | What they prove |
|---|---|---|
| Innings | 16 | strike rotation, over ends, wides/no-balls/byes, free hit (incl. carry-over on a wide), dismissal legality per ball type, invalid inputs rejected, maidens, caught/run-out new-batter end, wicket on last ball, all out, bowler quota, target reached, fifty flag, declarations |
| Match | 13 | T20 chase/defend/tie, Test follow-on + innings win, 4th-innings chase, draw on time, team validation, toss permissions + 50/50 fairness over 20,000 tosses, LBW law, DRS outcomes, **46 full AI-vs-AI matches (30 T20, 10 ODI, 6 Test)** checking runs = batters + extras, balls = bowler balls, no consecutive overs, quotas respected, commentary never empty, no back-to-back repeated lines |
| Field | 5 | position geometry, all 10 presets legal after powerplay, powerplay/ODI phase limits, max 2 behind square leg, manual placement on pitch / beyond boundary / stacked rejected |
| Session | 5 | join rules, toss turn order, duplicate/replayed command rejected, wrong-side commands rejected, full human-vs-human match, human-vs-AI with dropped updates and reconnect catch-up |

## Not tested yet (needs later milestones)

Ball physics, batting/bowling input, animation, rendering performance, Android
TV hardware, network transport, matchmaking, database, mobile controller, audio.
