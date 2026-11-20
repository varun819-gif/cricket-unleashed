# Run Cricket Unleashed on a Windows laptop

The game server runs on Linux, so on Windows it runs inside **WSL**
(Windows' free built-in Linux). Everything here is free.

## One-time setup

1. **Install Ubuntu (WSL).** Start → type PowerShell → right-click → *Run as
   administrator*, then:
   ```
   wsl --install -d Ubuntu
   ```
   Restart the laptop. The Ubuntu window opens: choose a username and password.
2. **Download and build.** Open normal PowerShell, type `wsl` and press
   Enter, then paste one line at a time:
   ```
   sudo apt update
   sudo apt install -y git cmake g++ make curl
   cd ~
   git clone -b ccr-1fd537b3-uv7e9l https://github.com/varun819-gif/cricket-unleashed.git
   cd cricket-unleashed
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j4 --target cricket_server
   ```
   If git asks for a password, use a GitHub fine-grained token (read-only
   "Contents" on this repository), not your GitHub password.
   Already cloned before? Update instead:
   ```
   cd ~/cricket-unleashed
   git pull
   cmake --build build -j4 --target cricket_server
   ```
3. (Only for playing with a phone) install the free tunnel:
   ```
   cd ~
   curl -L -o cloudflared.deb https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb
   sudo dpkg -i cloudflared.deb
   ```

## Single player vs computer (no phone, no internet opponent)

1. PowerShell → `wsl` → Enter, then:
   ```
   cd ~/cricket-unleashed
   ./build/cricket_server --port 8080 --data ~/cu-data
   ```
   Wait for `LISTENING 8080`. Keep this window open.
2. Chrome on the laptop: **http://localhost:8080**
3. Type your name, country India, **Play as guest** (first time only - the
   player is remembered in this browser).
4. Team **India**, Match **2 overs (quick)**, Computer plays **Pakistan**,
   click **Play vs computer**.
5. Play: toss → choose bat/bowl if you win → pick openers → batting
   (choose direction, click a shot when the ball arrives) / bowling (pick
   delivery, click the pitch where it should land, **BOWL**), pick your
   bowler each over.
6. At the end the result card appears. **History** shows it as "vs computer".
7. Stop: **Ctrl + C** in the PowerShell window.

## Two players with an Android phone

Start the server as above, then in a second PowerShell → `wsl`:
```
cloudflared tunnel --url http://localhost:8080
```
Open the printed `https://....trycloudflare.com` address on the phone in
Chrome. Laptop: **Create private room**; phone: type the code, **Join**;
laptop: **Start match**. Full steps: ONLINE_SERVER_SETUP.md.

## Problems

| What you see | Fix |
|---|---|
| `wsl` not recognised | do step 1 again and restart |
| `cannot use port 8080` / `another cricket_server is already using` | a server is already running in another window - use it or close it |
| Chrome can't open localhost:8080 | check the server window still shows `LISTENING 8080` |
| Old page after updating | press Ctrl + F5 in Chrome |
