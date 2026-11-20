# Running the online server for ₹0 (and the real internet test)

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

You need: the server running (step 1 + 2), and **two phones on different
internet connections** - for example Phone 1 on home Wi-Fi, Phone 2 on
mobile data (Wi-Fi switched off), ideally in a different place.

1. Phone 1: open the `https://` address in Chrome. Name "Player One",
   country India, tap **Play as guest**.
2. Phone 2: same address. Name "Player Two", country Pakistan.
3. Phone 1: team **Ganga Royals**, match **2 overs**, tap **Create private
   room**. Read the 6-letter code to Phone 2's owner.
4. Phone 2: team **Indus Falcons**, type the code, tap **Join**.
5. Phone 1: tap **Start match**. Play the whole match.
6. During the match, on Phone 2: turn mobile data **off for 20 seconds**,
   then on. It should say "Reconnecting…" and come back to the same ball.
7. At the end both phones must show the **same result**. Check
   **History** and **Leaderboard** on both.
8. Optional: both tap **Ranked match** with the same match length - they
   should be paired and ratings should change after the match.

Please send back: the result shown on each phone, whether step 6 worked,
phone models / Android versions, and any errors. A screenshot from each
phone at the end is ideal.

## 4. Costs and limits

- Cost: ₹0 (your electricity and internet only).
- One home PC easily handles the development stage (a few hundred players
  at once). The server is single-threaded; see ONLINE_MULTIPLAYER.md for
  the cloud path when you decide to pay for hosting.
- If the PC is off, the game is offline.
