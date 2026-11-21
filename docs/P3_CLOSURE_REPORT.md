# Priority 3 closure report: online multiplayer

Date: 2026-11-20. Branch `ccr-1fd537b3-uv7e9l`.

> **Online multiplayer is NOT complete.** Everything is built and tested on
> one machine. It is complete only when two separate phones finish a match
> over the internet (Section 4).

## 1. Completed work

**Online play**
- Guest players (no password, no Google), India server region, self-hosted at ₹0.
- Private rooms with 6-letter codes; quick (casual) and ranked matchmaking.
- 10 country teams with national flags (India, Pakistan, ...); players fictional.
- The server decides every score, wicket, result and rating.
- Reconnection: the match waits 2 minutes; then ranked = forfeit, casual =
  computer captain takes over.
- Server crash recovery: live matches continue after a restart.
- History, personal stats, leaderboards (rating, wins, win %, runs,
  wickets, sixes, fours, highest score, matches; all-time / 30 / 7 days;
  ranked / casual; world / country).
- The same phone-controller screens are used for local and online play.

**Review fixes (this round)**
- Web server drops slow or stuck connections (could fill the server).
- Ranked can't be farmed with fake accounts: ranked unlocks after 3 full
  matches; a forfeit in the first 12 balls gives the winner nothing; wins
  over brand-new accounts give half points; same-pair limit (from the
  4th ranked game in 24 h, no rating change).
- Ratings and stats are rebuilt from the match history at every start, so
  edited data files can't change them; stat leaderboards ignore forfeits.
- A control re-sent after a server crash is never applied twice (action ids
  saved to disk).
- Data written safely (fsync); only one server per data folder; room-code
  guessing limited; leaderboards cached.

**Your decisions, now in the project**
1. Real country names + national flags (MIT-licensed flag images, recorded
   in ASSET_REGISTER.md). No board logos, kits or real players.
2. Guest login stays default; Google/email linking designed for later
   (ACCOUNT_LINKING.md) - not built.
3. Moderation built: blocked names (English + Hindi), player reports,
   admin review page, 24 h / 7 day / permanent bans, name reset, audit log
   (MODERATION.md).
4. Self-hosting at ₹0; paid hosting only with your approval (written into
   the setup guide and plan).

**Unreal readiness**
- The core and controller code now compile the way Unreal compiles C++
  (no exceptions, no RTTI), checked on every build.
- UNREAL_DEVELOPMENT_PLAN.md written.

**Compatibility checked**: local phone controller and online browser
client share `app.js`; both browser tests and both Node suites pass.

## 2. Remaining limitations

1. No real two-device internet test yet.
2. Ball outcomes are simulated placeholders (until the ball physics model).
3. A TV/PC game can't yet join online with local phones as controllers
   (planned as Unreal milestone U8).
4. Guest player is lost if browser data is cleared (linking not built).
5. Test matches not offered online.
6. One server process with file storage - fine for development, not for
   thousands of players (cloud path documented, needs approval).
7. One shared admin token; no appeals process or community rules text yet.
8. Tests use emulated Android phones, not physical ones.

## 3. Security risks (what is still open)

| Risk | Level | Notes / next step |
|---|---|---|
| One person with many guest accounts | Medium | Farming is made slow and low-value (Section 1), not impossible. Next: pattern checks, admin review of reports |
| Perfect-reaction bot within 150 ms timing window | Low-Medium | Server re-measures timing; finer detection needs ball physics + stats |
| Sign-up flood (30 new guests/min for the whole server) | Low | Blocks only new sign-ups briefly; per-IP limits don't work behind a tunnel |
| Admin token leaked | High impact | Keep `admin.token` private; delete it to rotate; 5 wrong tries lock admin sign-in |
| Guest token stolen from a phone (e.g. shared phone) | Medium | Token is the only key; linking + "sign out everywhere" planned |
| Plain `http://` used on the internet | High | Always use the `https://` tunnel/Caddy address (setup guide) |
| Health page `/tv.json` visible via tunnel | Low | Counts only, no personal data |
| Word-list name filter gaps | Low | Reports + admin rename |
| Data on one PC (disk failure) | Medium | Back up the data folder |
| Rare test failure seen once (online Node suite) | Low | Not reproduced in 11 later runs; watching |

Not a risk: clients can't send scores, results or ratings; repeated or
re-ordered messages are refused; HTML in names is shown as plain text.

## 4. Exact two-phone internet test

Follow **ONLINE_SERVER_SETUP.md, section 3** (steps A-E). Short version:

1. PC: `./build/cricket_server --port 8080 --data ~/cu-data --ranked-unlock 1`
2. PC: `cloudflared tunnel --url http://localhost:8080` → copy the https address.
3. Phone 1 (home Wi-Fi) and Phone 2 (**mobile data, Wi-Fi off**): open the
   address in Chrome, play as guest (India / Pakistan).
4. Phone 1: India, T20, **Create private room**. Phone 2: Pakistan,
   type the code, **Join**. Phone 1: **Start match**.
5. In the 2nd over, Phone 2: mobile data off 20 s, then on → must say
   "Reconnecting…" and return to the same ball.
6. Phone 1: home screen 10 s, then back → must continue.
7. Finish: both phones show the same result. Check History + Leaderboard.
8. Both tap **Ranked match** (T20), finish, ratings change (+/- same).
9. Send me: both results (screenshots), whether steps 5-6 worked, phone
   models + Android versions + networks, anything odd.

## 5. Action required from you

1. **Run the two-phone internet test** above and send the results.
2. Confirm the **Afghanistan flag** choice (we show the black-red-green
   flag its cricket team uses at ICC events) - MISSING_DETAILS #27.
3. Decide **who moderates** and give a contact/appeals email and short
   community rules - MISSING_DETAILS #28.
4. For Unreal: a **Windows PC with an RTX-class graphics card** and the
   free installs listed in UNREAL_DEVELOPMENT_PLAN.md §3, plus a way for me
   to work on it (or you run steps and send results).
5. Approvals are needed later (not now) for: Google sign-in / email codes,
   any paid hosting, any paid asset or voice recording.

## 6. Commits

See `git log` on the branch; this round: Unreal compile check, review
fixes, country flags + moderation, docs.
