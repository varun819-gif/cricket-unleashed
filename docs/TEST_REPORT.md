# Test Report

**Date:** 2026-11-20 · **Milestone:** cricket rules (DRS, Test sessions, rain,
save/resume), v0.6.x

## Results

| Build | Compiler | Warnings (-Wall -Wextra -Wpedantic) | Tests |
|---|---|---|---|
| Debug + AddressSanitizer + UndefinedBehaviorSanitizer (clean build dir) | GCC 13.3 | 0 | **176 / 176 passed**, no sanitizer errors (~11 s) |
| Release (clean build dir) | GCC 13.3 | 0 | **176 / 176 passed** |
| Release (clean build dir) | Clang | 0 | **176 / 176 passed** |

The 39 original Milestone 1 tests (Innings, Match, Field, Session suites)
are unchanged and pass.

Run: `cmake -S . -B build && cmake --build build && ./build/cricket_tests`

## Suites

| Suite | Tests | What they prove |
|---|---|---|
| Innings | 16 | scoring laws, extras, free hit, strike, overs, bowler limits, all out, target, declarations |
| Match | 13 | results in every format, follow-on, innings wins, ties, draws, toss fairness, LBW, 46 full AI matches |
| Field | 5 | positions, presets, circle and leg-side restrictions, manual placement |
| Session | 5 | server authority, duplicates, turns, reconnect catch-up |
| DRS | 26 | caught / LBW reviews, referrals, no-ball checks, review limits, session review flow |
| No-ball | 17 | Law 21.12: bat runs, byes, leg-byes, DRS-found no-balls keep runs, wide overridden by no-ball |
| DRS rules + timer | 16 | rulesets, umpire's call option, no-reviews / no-tech, 15 s timer edge cases |
| DRS complete | 27 | hit wicket, obstruction, wide / height no-ball reviews, boundary checks, short runs, penalty runs (all destinations + target changes), new ball, **AI never reads evidence (source scan + behaviour)**, perception noise, personalities, Hindi reasons, full matches with all appeal types |
| Test sessions | 15 | lunch / tea / stumps, day change, changeover 2 overs, early interval near a break, day-night, last-day draw, follow-on by days, human pause / resume |
| Rain | 21 | resource curve properties, target formula, rain before / during either innings, terminated first innings, revised target already reached, minimum overs / no-result, par-score abandonment, buffer, rescaled quotas and powerplays, Test time lost / made up / through lunch and tea / mid-over / washed-out day 1 lowers follow-on, session events, **40 random-weather full matches** |
| Save | 14 | round trips in every format and state (pending review, lunch, rain, follow-on, finished), identical continuation after reload, Unicode names and exact doubles, damaged / truncated / edited / impossible / newer-version / wrong-kind files rejected, atomic save + backup fallback, crash-log recovery (cut mid-line, cut at newline, garbage tail, bad header), session saves incl. refused inputs and AI state, tampering detection |
| Integration | 1 | 25 seeded matches across T20 / ODI / 3-day Test × 5 DRS rulesets with appeals, random rain and penalties, **save + reload at a random point, both copies play on and finish identically**, books balance, final match re-saves |

## Mutation checks (do the tests catch real bugs?)

Bugs were planted one at a time in the new code; each was reverted after.

| Stage | Planted | Caught | Notes |
|---|---|---|---|
| Step 1 corrections | 8 | 7 | the 8th targeted unreachable code, which was removed |
| A: DRS | 9 | 9 | 2 gaps found first (AI on wide calls, perception copying truth); tests added |
| B: sessions | 7 | 6 | follow-on-by-days gap closed in Stage C (needs a washed-out day) |
| C: rain | 11 | 11 | one test crashed instead of failing; tests made crash-safe |
| D: save | 12 | 12 | 2 gaps found first (refused inputs, line cut at newline); tests added |

## Real bugs found by these tests during the milestone

1. A rain-shortened chase that ended immediately was judged on the old
   target (fixed: target revised first).
2. Without a third umpire, run-out referrals were refused and the ball was
   lost (fixed: on-field umpire decides).
3. FNV-1a checksum offset constant was one digit short (fixed; reference
   values tested).
4. Event order showed the next innings before the innings break (fixed).
5. Simulator didn't print events when rain ended the match (fixed).

## Not tested (needs later milestones)

Ball physics, batting/bowling input, animation, rendering, Android TV
hardware, network transport, matchmaking, database, mobile controller,
audio.
