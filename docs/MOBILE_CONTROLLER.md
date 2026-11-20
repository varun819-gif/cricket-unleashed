# Mobile Controller (Priority 2)

A phone becomes the game controller. Nothing is installed from an app store:
the phone opens a web page served by the TV/PC game over the home Wi-Fi.

![Bowling](images/phone-bowling.png) ![Batting](images/phone-batting.png)

## What is built

| Part | Code | What it does |
|---|---|---|
| Secure pairing hub | `controller/src/ControllerHub.cpp` | QR pairing, proof of the secret in both directions, device keys, signed and numbered messages, roles, reconnection, limits, revoking a phone |
| Match host | `controller/src/MatchHost.cpp` | turns phone controls into game actions, runs each ball in stages (bowl, then bat), sends each phone its own screen state |
| Local server | `controller/src/LocalServer.cpp` | small web server + WebSocket on the TV/PC (POSIX sockets) |
| QR codes | `controller/src/QrCode.cpp` | QR code generator (verified against two reference encoders) |
| Crypto | `controller/src/Crypto.cpp`, `controller/web/protocol.js` | SHA-256, HMAC-SHA256, SHA-1 (WebSocket handshake), base64. Checked against standard test vectors in both languages |
| Phone app | `controller/web/` | `protocol.js` (secure client), `app.js` (screens), `index.html`, `style.css` |
| TV/PC program | `tools/controller_host/main.cpp` → `cricket_controller_host` | runs a match, prints the QR code, serves the TV page (stands in for the Unreal game) |

Player instructions: [CONNECT_YOUR_PHONE.md](CONNECT_YOUR_PHONE.md).

## How a phone connects

```
TV/PC game                                   Phone (any modern browser)
──────────                                   ─────
1. makes a one-time pairing: id + 32-byte secret, valid 2 minutes
2. shows QR:  http://192.168.1.20:47321/#p=<id>.<secret>
                                     ──camera──►  3. opens the page; the part after '#'
                                                     is NEVER sent over the network
                                     ◄─────────   4. {"hello", id, phone nonce}
5. {"challenge", TV nonce}           ─────────►
                                     ◄─────────   6. proof = HMAC(secret, id|nonces)
7. checks proof, creates the device,
   {"welcome", TV proof}             ─────────►   8. checks the TV's proof (no fake TV)
   both sides compute (never send):
     device key  = HMAC(secret, "cu-device|"+device id)   - kept for reconnecting
     session key = HMAC(device key, both nonces)          - this connection only
9. every message both ways: {"m", number, body, HMAC(session key, direction|number|body)}
```

Reconnecting uses the same challenge with the saved **device key** instead of
the QR secret, so no new scan is needed. A new session key is made each time.

## Security model (honest)

**Protected against** (each has a test):
- Someone else on the Wi-Fi watching traffic **cannot pair or control** the
  game. The QR secret never crosses the network.
- **Forged, changed or replayed messages** are rejected, and that connection
  is closed. Every message is signed and numbered.
- A **copied or photographed QR code** is useless once used. It also expires
  after 2 minutes and is burned after 3 wrong attempts.
- **A fake "TV"** on the network can't fool the phone; the TV must prove it
  knows the secret too.
- **Other websites** open in the phone's browser can't talk to the TV.
  WebSockets are accepted only from the controller page's own origin.
- **The TV page** (it shows pairing secrets) is served only to the TV/PC
  itself, never to other devices.
- **Flooding / oversized messages / slow handshakes** are cut off, with
  limits on connections, devices, message size and message rate.
- **A refused action never runs twice**: each action has a number that
  survives reconnects (exactly-once).

**Not provided (by design, documented):**
- Messages are **authenticated, not encrypted**. Someone on the same Wi-Fi
  could *see* which buttons are pressed, but not fake them. Controller
  inputs aren't sensitive. HTTPS on a home network would need certificates
  that phone browsers warn about.
- The phone reports its own **shot timing**. That's fine for local play (it's
  your own phone). **Ranked online play must check timing on the server**;
  that's part of Priority 3.
- The saved **device list file** (`--devices`) contains device keys and
  must stay private on the TV/PC.

## Controls (role-based)

Every state the TV sends includes `controls`: the exact list this phone may
use right now. The phone only shows those buttons, and the TV rejects
anything else with a reason.

| Who | Controls |
|---|---|
| **Captain** (one team) | toss call / decision, openers, next batter, bowler, field preset, review / accept (with countdown), declare, follow-on, new ball, ready after a break, plus everything a player can do |
| **Player** (one team) | bowl (delivery type, tap-the-pitch line and length, effort), bat (shot, direction pad, timing tap), fielding reactions (dive left/right, catch, throw to keeper/bowler), ready after a break |
| **Spectator** | watches the live score, no controls |

**Each ball:** the fielding side chooses the delivery (20 s, then a stock ball
is bowled for them). The batting phone shows the ball coming, and the player
taps a shot as it arrives; the timing is measured on the phone. No shot in
time → no shot played.

⚠️ **Placeholder outcomes:** there is no ball physics yet. The result of each
ball comes from the statistical simulator, nudged by the player's choices: a
leave can't be caught, defending gives no boundaries, good timing gives more
boundaries, and bad timing gives more catches. The phone and TV say so.
Unreal ball physics will replace this; the controls and protocol stay the
same.

## Reconnection

- The phone reconnects by itself (0.5 s → 8 s back-off) and when it comes back
  to the foreground. It shows "Reconnecting… (the TV is keeping your place)".
- The TV keeps the phone's team and role, re-sends the full screen state, and
  ignores actions that were already done (exactly-once).
- While **every phone of a human team is disconnected**, the TV **holds all
  timers** (bowling choice, batting window, review timer), so nobody loses a
  ball to a Wi-Fi blip. After 60 s the TV reports the phone as "lost" (the
  game can show a message). The phone can still come back.
- Closing the browser or reloading also resumes. The device key is saved on
  the phone.
- The TV can **unpair** a phone. Paired phones survive a TV restart with
  `--devices <file>`.

## Platform notes

- **Phones:** any current Android phone browser (Chrome, Samsung Internet,
  Firefox). Tested with Chromium emulating a Pixel 7 (see the test report).
  iPhones should work too (standard web APIs only) but have not been tested.
- **TV/PC side:** the server uses POSIX sockets: **Linux and Android** (Android
  TV) as written. **Windows** needs a small Winsock port of the socket calls
  in `LocalServer.cpp` (planned with the Unreal integration).
- Works offline: no internet, no cloud, nothing bought.

## How it plugs into Unreal later

`ControllerHub` and `MatchHost` have no networking or rendering inside. In
Unreal, a game-mode component will:
1. own a `ControllerHub` + `MatchHost` (or the game's own match driver),
2. run `LocalServer` (or bridge Unreal's sockets to `ControllerHub::Receive`),
3. render the QR code from `QrCode` on screen (the TV shows it in the lobby),
4. replace `MatchHost::ResolveWithInputs` with the physics result of the
   player's bat and ball inputs.

## Tests

- C++ (in `cricket_tests`): utilities 8, hub 14, match host 7, server 7
  (real sockets) = 36.
- JavaScript (`ctest` runs it): `controller/web/tests/protocol.test.mjs`.
  Hash vectors, link parsing, and end to end against the real host: two
  phones play a full India vs Pakistan match, a phone reconnects on its own,
  and used / forged QR codes are refused.
- Browser: `node controller/web/tests/browser.e2e.mjs [screenshotDir]`.
  Chromium emulating an Android Pixel 7 taps through the real page: pairing,
  bowling, batting, the reconnect message, resume after reload, no
  JavaScript errors, and the TV QR code decodes to its link (with
  `CU_JSQR_DIR` pointing at a folder with `jsqr` + `pngjs`, development
  only).
