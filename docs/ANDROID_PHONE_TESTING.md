# Testing on real Android phones

Everything so far was tested on **emulated** Android phones (Chrome
pretending to be a Pixel 7) on one machine. This guide is for testing with
**your real phones**. It costs ₹0 and needs nothing to be bought.

There are two kinds of test:

| Test | What it checks | Needs |
|---|---|---|
| **A. Phone as controller (same Wi-Fi)** | the laptop runs the match, phones are the remote controls | laptop + 1 or 2 phones on the same Wi-Fi |
| **B. Online (internet)** | two phones play each other through the game server | laptop + 2 phones, one on mobile data |

Phones: Android 8 or newer, **Chrome** browser (other browsers may work,
but test with Chrome first).

---

## Before you start (once)

Do the one-time setup in [RUN_ON_WINDOWS.md](RUN_ON_WINDOWS.md) (WSL,
download, build). Then build the controller program too:

```
wsl
cd ~/cricket-unleashed
git pull
cmake --build build -j4 --target cricket_controller_host cricket_server cricket_sim
```

---

## A. Phone as controller (same Wi-Fi)

### Step 1 - find the laptop's Wi-Fi address
Windows: Start → type `cmd` → Enter → type `ipconfig` → Enter. Under
**Wireless LAN adapter Wi-Fi**, note **IPv4 Address**, e.g. `192.168.1.7`.
(Below we call it `LAPTOP_IP`.)

### Step 2 - let the phone reach the program inside WSL (once)
WSL is a small Linux inside Windows with its own hidden address, so phones
can't reach it directly. Pick **one**:

**Windows 11 (22H2 or newer) - easiest.** In Windows, open Notepad and save
this as `C:\Users\<your name>\.wslconfig` (Save as type: *All files*):
```
[wsl2]
networkingMode=mirrored
```
Then in PowerShell: `wsl --shutdown`, and open `wsl` again.

**Windows 10 (or if the above doesn't work).** PowerShell → right-click →
*Run as administrator*:
```
wsl hostname -I
```
Note the first address (e.g. `172.22.80.5`, called `WSL_IP`). Then:
```
netsh interface portproxy add v4tov4 listenport=47321 listenaddress=0.0.0.0 connectport=47321 connectaddress=WSL_IP
New-NetFirewallRule -DisplayName "Cricket Unleashed controller" -Direction Inbound -LocalPort 47321 -Protocol TCP -Action Allow -Profile Private
```
(`WSL_IP` can change after a restart: if phones stop connecting, run the
`netsh` line again with the new address.)

Also check Windows calls your home Wi-Fi a **Private** network (Settings →
Network → Wi-Fi → your network → *Private*).

### Step 3 - start the game
```
wsl
cd ~/cricket-unleashed
./build/cricket_controller_host --host-ip LAPTOP_IP --humans 1
```
- `--humans 1`: you (phone) vs the computer. `--humans 2`: two phones,
  India vs Pakistan.
- The window prints QR codes. Or open **http://localhost:47321/tv** in
  Chrome on the laptop: big QR codes for each team.

### Step 4 - pair the phone
1. Phone on the **same Wi-Fi** as the laptop (not a *guest* network; mobile
   data **off**; no VPN).
2. Open the camera (or Google Lens), point at the India code, tap the link.
3. The page opens, the dot turns **green**, the top says **India · captain**.

### Step 5 - play and tick the checklist

| # | Check | ✔ / ✘ |
|---|---|---|
| 1 | Toss: call heads/tails or choose bat/bowl | |
| 2 | Pick two openers; pick a bowler each over | |
| 3 | **Bowling:** choose a delivery, tap the pitch, **BOWL** | |
| 4 | **Batting:** drag the arc to aim, tap a shot as the yellow bar fills | |
| 5 | **Run? card** after you hit it into the field: **WAIT / 1 / 2 / 3** (it shows when the throw comes back and how long your batters take) | |
| 6 | **Fielding:** when they hit it to your fielder, tap **CATCH** / **DIVE** as the bar fills; choose the throw end | |
| 7 | **Step out** button against a spinner | |
| 8 | **Review** button after an LBW/caught-behind decision | |
| 9 | **Scorecard** button: batting, bowling, extras, fall of wickets | |
| 10 | The phone vibrates when it is your turn | |
| 11 | **Wi-Fi off for 20 s**, then on → "Reconnecting…", then back at the same ball | |
| 12 | Home button for 10 s, then back to Chrome → continues | |
| 13 | Lock the screen for 10 s, unlock → continues | |
| 14 | Close Chrome completely, open the link from history → same team, no new QR | |
| 15 | Two phones (`--humans 2`): phone 2 Wi-Fi off → phone 1 says "Waiting for the other team's phone" and the match waits | |
| 16 | Match ends: both phones show the **same result** | |
| 17 | Buttons are big enough, nothing cut off at the screen edge | |

### Problems and fixes

| What you see | Fix |
|---|---|
| Camera link doesn't open / "site can't be reached" | Step 2 not done, or wrong `--host-ip`, or phone on guest Wi-Fi / mobile data |
| Program says `NOTE: running inside WSL` and the phone can't connect | Start it with `--host-ip LAPTOP_IP` (step 3) and do step 2 |
| "Could not connect" / "pairing code expired" | Codes last 2 minutes and work once: scan the new code |
| Works on the laptop but not on the phone | Windows firewall: re-run the `New-NetFirewallRule` line (Step 2), network must be *Private* |
| Screen turns off during a long innings | Normal for a plain `http://` page: Android screen timeout. Tap the screen now and then, or set screen timeout to 5-10 minutes while testing |

---

## B. Online (two phones over the internet)

Follow **ONLINE_SERVER_SETUP.md section 3** (start the server, the free
`cloudflared` tunnel, two phones: one on home Wi-Fi, one on **mobile data
with Wi-Fi off**). Use the same checklist above for items 1-14 and 16.
Extra online checks: private room code, quick match, **History** and
**Leaderboard** after the match, the "Opponent disconnected" message.

The online page uses `https://` (through the tunnel), so the browser's
full-screen and secure features all work there.

---

## What to send me

1. The checklist with ✔ / ✘ (and a note for every ✘).
2. Phone models and Android versions, Chrome version (Chrome → ⋮ →
   Settings → About Chrome), Wi-Fi or Jio/Airtel/Vi.
3. Screenshots of anything that looks wrong, and of the final result on
   each phone.
4. How the batting timing **felt** (too easy / too hard / laggy).
5. For a finished online match: the file `~/cu-data/matches/<id>.save`
   (no passwords in it). I can replay and re-check it with
   `./build/cricket_sim --verify ~/cu-data/matches/<id>.save`.

---

## What was checked here (emulated, not real phones)

| Check | How | Result |
|---|---|---|
| Phone screens at 320, 360 and 412 px wide | Chrome phone emulation, every screen of a short match | nothing goes off the edge |
| Plain-http address (like `http://192.168.1.7`, not a "secure" page) | two emulated phones on a network name, not localhost | pairing, signed controls, reconnection all work |
| Two phones: pairing, Wi-Fi drop, page reload, same result | `controller/web/tests/two_phones.e2e.mjs` | passes |
| Full 20-over T20 vs computer on a phone | `online/web/tests/t20_full.e2e.mjs` | passes; scorecard + result re-checked from the saved match |

**Found and fixed while writing this guide:**
- Inside WSL the controller program printed links with WSL's hidden address
  (phones can't reach it). It now warns and tells you to use `--host-ip`;
  Step 2 explains the port forwarding.
- Emulated tests used `127.0.0.1`, which browsers treat as "secure"; real
  phones use a plain http address. The two-phone test now uses a
  non-secure address so this case is covered.

**Not checked yet (needs your real phones):** real touch feel and timing,
real Wi-Fi / mobile-data delays, screen locking, vibration, different
Android versions and screen shapes (notches), battery use.
