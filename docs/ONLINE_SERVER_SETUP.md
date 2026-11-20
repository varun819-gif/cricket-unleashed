# Running the online server for ₹0 (and the real internet test)

**Project rule:** development stays self-hosted at ₹0. Any paid hosting,
domain, cloud service or subscription needs the project owner's explicit
approval first.

No paid hosting is needed. You run the server on your own PC (or an old
laptop) in India, and use a **free** service to give it a secure `https://`
address. Nothing here needs a credit card.

## 1. Build and start the server (Linux, or Windows with WSL)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/cricket_server --port 8080 --data ~/cu-data
```

- Open `http://localhost:8080/` on that PC - the online lobby appears.
- Health check (this PC only): `http://localhost:8080/tv.json`.
- `~/cu-data` keeps accounts, match history and live-match journals.
  **Back it up** (copy the folder). Stop with Ctrl+C; live matches resume
  after a restart.

## 2. Put it on the internet with free HTTPS

Pick **one**:

**A. Cloudflare Tunnel (free, no router changes)** - easiest.
1. Install `cloudflared` (free download from Cloudflare).
2. Run: `cloudflared tunnel --url http://localhost:8080`
3. It prints an address like `https://something-random.trycloudflare.com`.
   That is your game address. (A quick tunnel changes address each time; a
   free Cloudflare account gives a fixed one.)

**B. Caddy + your router (free, needs a domain name or free DNS name)**
1. Forward port 443 on your home router to the PC.
2. Get a free name (e.g. from a free dynamic-DNS service).
3. Install Caddy and use this `Caddyfile`:
   ```
   yourname.example.org {
       reverse_proxy localhost:8080
   }
   ```
   Caddy gets a free Let's Encrypt certificate automatically.

Notes:
- Players must use the `https://` address (the browser then uses secure
  `wss://` for the game). Never send tokens over plain `http://` on the
  internet.
- Behind a tunnel or proxy, all requests reach the server from the same PC,
  so `/tv.json` becomes visible to outsiders. It only shows counts (no
  personal data), but keep it in mind.
- Some ISPs (Jio/Airtel home broadband) use CGNAT, so option B's port
  forwarding may not work - use option A.

## 3. The real two-device internet test (still to be done)

Online multiplayer is **not** called complete until this passes.

**You need**
- The PC running the server (steps 1 and 2) and its `https://` address.
- **Two Android phones on different internet connections.** Phone 1 on
  home Wi-Fi. Phone 2 on **mobile data with Wi-Fi switched OFF** (Jio,
  Airtel, Vi, ...). Ideally Phone 2 is in another house.
- About 20 minutes.

**A. Start**
1. On the PC: `./build/cricket_server --port 8080 --data ~/cu-data --ranked-unlock 1`
   (ranked normally needs 3 full matches; for this test 1 is enough).
2. On the PC: `cloudflared tunnel --url http://localhost:8080`. Copy the
   `https://....trycloudflare.com` address it prints.
3. Send that address to both phones (WhatsApp is fine).

**B. Sign in**
4. Phone 1: open the address in **Chrome**. Name `Player One`, country
   **India**, tap **Play as guest**. The lobby shows "Server: India".
5. Phone 2: same. Name `Player Two`, country **Pakistan**.

**C. Private room match (casual)**
6. Phone 1: Team **India**, Match **2 overs (quick)**, tap **Create
   private room**. A 6-letter code appears.
7. Phone 2: Team **Pakistan**, type the code in **Room code**, tap **Join**.
   Phone 1 now lists both players with flags.
8. Phone 1: tap **Start match**. Both phones switch to the match screen.
9. Play normally: toss, openers, bowler, bowl (tap the pitch, then BOWL),
   bat (tap a shot when the ball arrives).
10. **Internet cut test:** in the 2nd over, on Phone 2 turn mobile data
    **off for 20 seconds**, then on again. Phone 2 should show
    "Reconnecting…", then return to the same ball. Phone 1 should show
    "Opponent disconnected", and the match waits.
11. **App switch test:** on Phone 1 go to the home screen for 10 seconds,
    then come back to Chrome. It should continue by itself.
12. Finish the match. Both phones must show **the same result**
    (e.g. "India won by 12 runs") and "Casual match - rating unchanged".
13. On both phones: **History** shows the match; **Leaderboard** →
    "Matches" lists both players.

**D. Ranked match**
14. Both phones: same match length (2 overs), tap **Ranked match**. Within
    a few seconds both are paired.
15. Finish the match. The winner's rating goes up, the loser's down by the
    same amount (e.g. +20 / -20). **Leaderboard → Rating** shows both.

**E. Send back to me**
- The result line shown on each phone after C and D (screenshots best).
- Whether steps 10 and 11 worked, and how long reconnecting took.
- Phone models, Android versions, network (Wi-Fi / Jio / Airtel ...).
- Anything that looked wrong or slow (especially the batting timing).
- Optional: the PC's `~/cu-data/matches.jsonl` (has no passwords or
  tokens; it has player names and scores).

If anything fails, the server keeps running; just tell me what you saw.

## 4. Costs and limits

- Cost: ₹0 (your electricity and internet only).
- One home PC easily handles the development stage (a few hundred players
  at once). The server is single-threaded; see ONLINE_MULTIPLAYER.md for
  the cloud path when you decide to pay for hosting.
- If the PC is off, the game is offline.

## 5. Moderation (admin page)

- The server creates `~/cu-data/admin.token` the first time it starts.
  It is the admin password: keep it secret, never share it in chat.
- Open `https://<your address>/admin`, paste the token, sign in. You can
  review reports, ban for 24 hours / 7 days / permanently, lift a ban,
  reset a bad name. Every action is written to `~/cu-data/moderation.jsonl`.
- To change the token: stop the server, delete `admin.token`, start again.
- See [MODERATION.md](MODERATION.md).
