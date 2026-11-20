# Moderation

Status: **built and tested** (C++ tests, plus a browser test that sends a
report and bans through the admin page). Community rules text, an appeals
contact and moderator staffing are still open (MISSING_DETAILS.md #25, #28).

## 1. Names that are blocked

Checked when a guest player is created and when a name is changed
(`online/src/Moderation.cpp`):

| Rule | Example blocked | Still allowed |
|---|---|---|
| Worst abuse and slurs (English + Hindi), even hidden inside a name | `Ch00tiya`, `F.u.C.k`, `bhen chod` | |
| Milder words, only as a whole word | `big Sh1t`, `paki` | `Sussex Dickson`, `Pakistan Zindabad` |
| Pretending to be staff | `Official Admin`, `Moderator` | |
| Contact details | `call 9876543210`, `www.spam.in` | `Virat Fan 18` |

Look-alike characters are folded first (0→o, 1→i, 3→e, 4/@→a, 5/$→s, 7→t,
8→b). No list is perfect, so a name can still be reported, and an admin can
**reset** it to `Player xxxx`.

## 2. Reporting a player

- Lobby → **History** → **Report** next to a match → choose a reason
  (cheating, abuse, offensive name, keeps quitting, other) and an optional
  note (200 characters).
- Rules (stop spam and revenge reports): you can only report someone you
  have played; one report per player per day; at most 5 reports a day.

## 3. Admin review

- Page: `/admin` on the game address. Sign in with the token from
  `<data>/admin.token` (256-bit random, created by the server, file
  readable only by the server's user). It is not stored in the browser.
- Wrong token: connection closed; after 5 wrong tries in 10 minutes, admin
  sign-in is locked for 10 minutes.
- Admins see open reports (reporter, player, reason, note), the player's
  details, all reports about them and their recent matches.
- Actions: **Ban 24 hours**, **Ban 7 days**, **Permanent ban**, **Lift
  ban**, **Reset name**, **Dismiss report**. A ban needs a reason (the
  player sees it) and closes the report it came from.
- Admin connections cannot play, queue or join rooms.

## 4. What a ban does

| | Temporary ban | Permanent ban |
|---|---|---|
| Player online | disconnected at once, sees "Account suspended" + reason + end time | same, "permanently banned" |
| In a ranked match | counted as a **forfeit** (opponent wins) | same |
| In a casual match | the computer captain takes over their side | same |
| Queue / room | removed | removed |
| Logging in | refused until the ban ends, then works again | always refused |
| Leaderboards | hidden while banned | hidden |
| Account in the browser | **kept** (so they can return after the ban) | kept (shows the ban) |

## 5. Records (audit trail)

- `reports.jsonl`: every report and its outcome.
- `moderation.jsonl`: every admin action (time, action, player, detail).
- Bans are stored on the account (`accounts.jsonl`). All files use a
  checksum per line, like the rest of the data.

## 6. Not built yet

- Community rules page, appeals process / contact email.
- Several moderator accounts with their own names (today: one shared
  admin token, so the log says "admin").
- Automatic detection (cheating patterns, many accounts from one person),
  in-game chat filtering (there is no chat).
- Age rating decision.
