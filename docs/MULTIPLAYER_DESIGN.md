# Multiplayer Design

> What is built today (Priority 3 online server, rooms, matchmaking, ratings,
> reconnection, crash recovery) is described in [ONLINE_MULTIPLAYER.md](ONLINE_MULTIPLAYER.md).
> This page is the longer-term design (Unreal netcode etc.).

## Golden rule

**The server decides.** A phone, TV or PC never reports a score, a wicket or a
result. Clients send *intents* (what the player wants). The server runs the
physics and the rules (`MatchSession` + `cricket_core`) and broadcasts what
happened.

## Two separate networks (do not mix them up)

| | Local controller link | Internet multiplayer |
|---|---|---|
| Between | Phone ↔ TV game, same Wi-Fi | Game client ↔ dedicated server |
| Purpose | Phone acts as a gamepad | Two players in different homes |
| Transport | WebSocket on LAN, pairing via QR code | UDP (Unreal netcode) + HTTPS/WebSocket for lobby |
| Trust | Phone is just an input device for that TV | Server is authoritative |

In online play the phone still talks only to its own TV/PC. That client then
sends intents to the server.

## Match session (implemented in `core/`)

- `Join(player, side)`: a human claims a side. Unclaimed sides are run by the
  AI captain.
- `Submit(Command)`: every command carries a per-player **sequence number**.
  A repeated or older number is rejected (duplicate/replay protection).
- **Turn checks**: `WaitingFor()` says what decision is pending and whose it
  is. For example only the calling captain calls the toss, only the toss
  winner decides, only the fielding captain sets the field or picks the bowler,
  and only the batting captain declares.
- `ResolveDelivery(Delivery)`: called only by server physics.
- `ResolveAppeal(DecisionCase)`: server-only close call with replay evidence.
  Player appeals wait for a `Review` command from the entitled side (review
  or accept) within the ruleset's review window (15 s by default). The
  server drives the timer with its own clock: `Tick(nowMs)` from the game
  loop, and `SubmitAt(cmd, nowMs)` for each message. Clients only display
  the countdown (`Pending::deadlineMs`).
  Referrals (run out, stumping, clean catch) are decided at once. See
  DRS_DESIGN.md.
- **Event log**: every accepted change gets an increasing id. A reconnecting
  client calls `EventsSince(lastSeenId)` and catches up without missing or
  repeating anything. This is tested by dropping updates on purpose.
- **Fields**: an illegal field (for example after the powerplay ends) is
  auto-corrected and logged.
- **Breaks**: in a human Test match the session pauses at lunch, tea, dinner
  and stumps until every player sends `ResumePlay`, or the server's break
  timer calls `ResumeAfterInterval()`.
- **Weather and umpire actions** (server only): `RainDelay`, `ReduceOvers`,
  `LoseRestOfDay`, `AbandonMatch`, `AwardPenaltyRuns`.
- **Crash recovery (implemented)**: every input, accepted or refused, is
  journaled. A `SessionJournal` file per room lets a restarted server rebuild
  the room exactly (`RecoverSession`), including the AI's random state, timers
  and duplicate protection (see SAVE_AND_RESUME.md).

## Planned around the core

1. **Rooms**: a 6-character room code (no confusing characters like 0/O, 1/I),
   invite links with a signed token that expires, and a host who picks
   format/stadium.
2. **Matchmaking**: Redis queues per mode (ranked/casual) and format. Players
   are paired by rating band that widens over time, with region/latency
   checks.
3. **Latency**: the client predicts animations locally, and the server result
   corrects them. Bowling and batting inputs carry client timestamps, and the
   server checks them against its own clock window (fair across different
   hardware).
4. **Reconnection**: a dropped player has 90 s to come back (AI holds a
   "safe" plan meanwhile). After that, it is a forfeit in ranked or AI takeover
   in casual.
5. **Recovery**: done in the core (session journal + replay). Still needed:
   wiring it into the real server process and storage.
6. **Anti-cheat**: server-only results, input rate limits, signed builds,
   ranked results written only by servers (never by clients), and anomaly
   checks (impossible timings, reaction times).
7. **Toss fairness**: the coin is flipped with a server OS-random seed. For
   extra transparency (later) the server can publish a hash of the seed before
   the call and reveal the seed after.
8. **Future**: spectators get the same event stream read-only. Tournaments and
   leagues schedule rooms through the same session API.
