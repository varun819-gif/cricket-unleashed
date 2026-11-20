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

## Mobile controller (Priority 2) results

| Suite | Tests | What they prove |
|---|---|---|
| Controller utilities (C++) | 8 | SHA-256 (FIPS vectors incl. 1 million 'a'), SHA-1 + RFC 6455 handshake value, HMAC (RFC 4231), base64 (RFC 4648), strict JSON accept/reject, QR matrices identical to two reference encoders (all masks, versions 1-10) |
| Controller hub (C++) | 14 | pairing + TV proof, wrong secret burns the code, expiry, bad nonce, signed actions + replies, tampering / replay / wrong-direction / pre-auth messages rejected, reconnect with new session key, old-session replay rejected, exactly-once actions across reconnects, newer connection replaces older, grace period + lost + back, revoke, limits, handshake timeout, devices survive a TV restart |
| Match host (C++) | 7 | full T20 phone vs AI, full two-phone match, role-based controls, bowl + bat timeouts, timers held while a team's phone is offline, bad inputs rejected + field change applied, placeholder outcome rules |
| Local server (C++, real sockets) | 7 | only listed files served (path tricks refused), security headers, 405 / 431, TV page only to the TV itself (tested via the machine's network address), WebSocket handshake (RFC accept value, origin, missing key), frame rules (unmasked, binary, oversize, ping), full pairing over the wire with fragmented frames, disconnect noticed |
| Phone JavaScript (Node, in `ctest`) | 6 | hash vectors match C++, link parsing, **two phones play a full India vs Pakistan match through the real host**, automatic reconnection, used / forged QR codes refused |
| Browser (Chromium emulating Pixel 7) | 14 checks | Android user agent, pairing secret removed from the address bar, header, scoreboard, bowling by tapping the pitch + BOWL, batting with a shot tap, reconnect message + automatic reconnection, reload resumes the same phone, **no JavaScript errors**, instructions for unpaired phones, TV page QR codes **decode to their links** |

Builds: Debug + sanitizers, Release GCC, Release Clang: **0 warnings, 217/217
C++ tests, 6/6 JS tests**, browser test all passed.

Bugs found by these tests and fixed before release: the opener "Send" button
stayed disabled; pitch labels blocked by the page's own security policy;
vibration attempted before the first tap; header showed "Team 1" instead of
the team name; three uninitialised-variable warnings in GCC release builds.

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
