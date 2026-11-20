# Online Multiplayer (Priority 3)

Status: **built and tested on one machine. Not yet proven over the internet
between two separate devices** - see [ONLINE_SERVER_SETUP.md](ONLINE_SERVER_SETUP.md)
for the real-world test that is still needed.

## What a player sees

1. Open the game website on a phone or PC. Type a player name, pick a
   country, tap **Play as guest**. No email, no password, no Google login.
2. In the lobby choose a team and match length (2, 5, 10 overs, T20, ODI).
3. Then one of:
   - **Quick match** - any opponent, casual (rating doesn't change).
   - **Ranked match** - an opponent with a similar rating; the result moves
     both ratings.
   - **Create private room** - get a 6-letter code (like `MUKJXA`), send it
     to a friend, they tap **Join** and type it. Host taps **Start**.
4. The match uses the same screens as the phone controller (toss, captaincy,
   bowling, batting, reviews).
5. After the match: result, rating change, **My stats**, **History** and
   **Leaderboard** (rating, wins, win %, runs, wickets, sixes, fours,
   highest score, matches - all time / 30 days / 7 days, ranked / casual,
   world or one country).

Screenshots (Pixel 7 emulation): `docs/images/online-lobby.png`,
`online-room.png`, `online-match.png`, `online-result.png`.

## Pieces

| Piece | Where | Job |
|---|---|---|
| `Store` | `online/src/Store.cpp` | guest accounts, match records, ratings, stats, leaderboards; JSON-lines files with a checksum per line |
| `OnlineServer` | `online/src/OnlineServer.cpp` | logins, message numbers, rooms, matchmaking, live matches, timers |
| `OnlineMatch` | same file | connects a live match (`MatchHost`) to the two players |
| `MatchHost` + `MatchSession` | `controller/`, `core/` | the same game logic the phone controller uses - the rules engine decides everything |
| `cricket_server` | `tools/server/main.cpp` | the program you run: web page + WebSocket on one port |
| Web client | `online/web/` (`online.js`, `lobby.js`, `index.html`) | lobby; reuses `controller/web/app.js` for the match screens |

## Server decides everything

- Clients send only **requests** (join a room, queue) and **controls**
  (call heads, bowl a yorker, play a drive). They never send a score, a
  wicket, a result or a rating.
- The scorebook stored for history, stats and leaderboards is built by the
  server from its own match (`Summarise()` in `OnlineServer.cpp`).
- Every finished match also keeps a **replay file** (`data/matches/<id>.save`)
  that rebuilds the whole match exactly - evidence if a result is disputed.

## Anti-cheat and duplicate commands

| Threat | Protection |
|---|---|
| Same message sent twice / replayed / re-ordered | every message has the next number `s`; anything else closes the connection |
| A control re-sent after a reconnect | match controls have an action id; ids already handled are acknowledged but **not applied again** |
| Fake "perfect timing" on a shot | the server re-measures timing from when the tap arrived, minus the measured network delay; if the claim is more than 150 ms off, the server's figure is used (`TimingCorrections`) |
| Faking a slower connection to get more time | round-trip time = the **smallest** of the last 5 server pings (can't be made smaller than reality); extra batting time is capped at 1.5 s |
| Out-of-turn or illegal moves | `MatchSession` turn checks (only the right captain, only legal choices) |
| Flooding | max 30 messages / second per connection, 8 KB per message, 30 new guests / minute per server, room and match limits |
| Stolen data file | only SHA-256 of each guest token is stored; tokens are 256-bit random |
| Win trading | the same two players: from the 4th ranked game within 24 h, no rating change |
| Leaving a lost match | ranked: leaving = **forfeit** after the grace period (also "Concede") |
| Stalling | a captain who makes no decision for 3 minutes is treated as gone |
| HTML in names | names are plain text, shown with `textContent` |

Not covered yet (honest list): automated cheat detection from patterns,
reporting / banning players, device fingerprinting, multiple guest accounts
by one person ("smurfing"). See KNOWN_ISSUES.md.

## Disconnects and reconnection

- The client reconnects by itself (0.5 s, 1 s, 2 s ... up to 8 s between
  tries) and logs in again with its saved token. The server re-sends the
  full match state, so the player is exactly where they were.
- While a player is away, the match **waits** (their timers stop) and the
  opponent sees "Opponent disconnected".
- After **2 minutes** (`--grace-sec`): ranked = forfeit (opponent wins,
  ratings change); casual = the **computer captain** takes over that side
  and the other player can finish the match. If both leave: no result.
- Signing in on another device moves the player there ("Signed in on
  another device" on the old one).

## Server restarts (crash recovery)

Every live match writes a journal line per action (`data/live/<id>.journal`)
plus a small description file. After a crash or power cut, `cricket_server`
rebuilds each live match on start-up (the state is checked to be identical),
and players reconnect automatically. Tested by killing the server mid-match
(`online/web/tests/online.test.mjs`). If the machine clock is behind the
journal after a restart, the match keeps its own clock offset.

## Ratings (Elo)

- Everyone starts at 1200. Only ranked matches count.
- Change = K × (result − expected); K = 40 for a player's first 10 ranked
  games, then 20. Win vs equal player: +20 (new player). Tie = 0.5.
- Matchmaking: ranked pairs players within 100 rating points, widening by
  25 per second of waiting, up to 500. If both chose the same team, the
  later player gets the next team.

## Wire protocol (WebSocket, JSON text)

Client → server: `{"s": n, "k": kind, ...}` with `n` = 1, 2, 3 ... per
connection.

| `k` | Fields | Notes |
|---|---|---|
| `guest` | `name`, `country` | first message; reply has `id`, `token` (only time it is sent) |
| `login` | `id`, `token` | first message |
| `ping` / `spong` | `t` | client clock check / answer to server `sping` |
| `teams` | | teams and formats |
| `profile` | `name`, `country` | |
| `room-create` | `format`, `team` | reply `code` |
| `room-join` | `code`, `team` | code is case/space tolerant |
| `room-start` / `room-leave` | | host only for start |
| `queue` | `mode` (`casual`/`ranked`), `format`, `team` | |
| `queue-cancel` | | |
| `action` | `id`, `a` (control, same as phone controller) | answered with `ack` |
| `concede` | | counts as a loss |
| `stats` | `id` (optional) | |
| `history` | `limit` | |
| `leaderboard` | `board`, `period`, `mode`, `country`, `limit` | |

Server → client: `res` (`re` = request number, `ok`, `err`), `match`
(`id`, `mode`, `side`, `sides`, `last` action id), `state`, `ball`, `ack`,
`notice`, `room`, `room-closed`, `queued`, `end` (`record`, `rating`,
`delta`), `sping`, `pong`, `replaced`, `error` (then the socket closes).

## Ready for cloud scaling (not needed yet)

The pieces are split so each can move to the cloud later, when you approve:

| Today (₹0) | Later |
|---|---|
| One `cricket_server` on your PC | several servers behind a load balancer; a lobby service sends each match to a match server |
| JSON-lines files | PostgreSQL (the `Store` class is the only code that changes) |
| Presence + queues in memory | Redis (shared between servers) |
| Region `in` | more regions (`--region`), players pick the nearest |
| Live-match journal on disk | same journal on shared storage, so another server can take over |

## Tests

- C++ (`online/tests/OnlineTests.cpp`, 15 tests): tokens hashed, damaged /
  tampered files, Elo + win-trading guard, leaderboards, message numbers,
  wrong token, replaced login, limits, rooms, full India-vs-Pakistan-style
  match, matchmaking + team clash, rating band, reconnection + duplicate
  actions, ranked forfeit, casual AI takeover, idle captain, concede,
  server-side timing check, crash recovery.
- Node (`online/web/tests/online.test.mjs`, 3 tests, real processes and
  sockets): private room through a slow (120 ms) connection that is cut 3
  times; ranked match with the server killed mid-match and restarted;
  protocol guards.
- Browser (`online/web/tests/browser.e2e.mjs`): two emulated Android phones,
  sign-in, room code, full match by tapping, reload mid-match, result,
  history, leaderboard (18 checks).

All of these run on **one machine**. They are not a substitute for the
two-device internet test.
