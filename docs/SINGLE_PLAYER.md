# Single player vs computer

## What it is

In the browser app's lobby: **Single player → Computer plays [team] → Play
vs computer**. You play one team (for example India); the computer captain
plays the other (for example Pakistan). No second player, phone, internet
opponent or matchmaking.

## How it works (reuses the existing engine)

- The server starts a normal match with `MatchHost`, the same game logic
  used by the phone controller and online play, with your side marked
  human and the other side marked AI (`HostConfig::human = {true, false}`).
- The **rules engine (`MatchSession`)** keeps the score, decides
  wickets, extras, overs, innings and the result. There is no separate
  scoring code.
- The **AI captain** (`AutoCaptain`, already used for AI sides) does the
  computer's toss call/choice, batting order, bowling changes, field
  settings, reviews, and plays its deliveries and shots.
- **Every ball is played by the ball physics** (BALL_PHYSICS.md): your
  delivery or shot, the computer's delivery or shot, the swing, bounce and
  spin, the bat impact, and then the 9 fielders, wicketkeeper and bowler
  chasing, catching or stopping the ball. Runs come from how far and where
  the ball goes and how fast the fielders get there. There are no animated
  fielders or keeper yet (that is the 3D work).

## What you control

- Toss (call or bat/bowl choice when it is yours), your openers, next
  batter, your bowler for each over, field preset.
- Bowling: delivery type, where it pitches (tap the pitch), BOWL.
- Batting: shot direction, shot type, timing (tap when the ball arrives).
- Scoreboard at the top: team score `runs/wickets (overs)`, batters,
  bowler, target and required rate, then the result card.

## Rules for records

Matches vs the computer appear in **History** as "vs computer". They do
not change your rating, personal stats or leaderboards, and do not count
towards unlocking ranked (otherwise they could be farmed).

## Run it

See the Windows steps in RUN_ON_WINDOWS.md. You need only the server
(Window 1) and Chrome at `http://localhost:8080` - no tunnel, no phone.
