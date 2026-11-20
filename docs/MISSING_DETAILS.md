# Missing Details and Working Assumptions

The master specification was cut off in section 25 ("Test: ... Multiplaye").
As agreed, development continues from the existing requirements. This file
lists what is missing or unclear, and the assumption used meanwhile. Each
assumption is easy to change later.

## From the cut-off specification (section 25 onwards)

| # | Missing | Assumption used |
|---|---|---|
| 1 | Rest of the testing list after "Multiplaye…" | Test everything built: multiplayer, controller, leaderboard, security, performance, devices. See TEST_REPORT.md |
| 2 | Any sections after 25 (release, legal, store listing, budgets?) | Not assumed. Only the roadmap items named by the user are planned |
| 3 | Acceptance criteria / definition of "done" per milestone | Tests for every rule + documented limits + pushed to GitHub |

## Mobile controller (Priority 2)

| # | Missing detail | Assumption used |
|---|---|---|
| 4 | Phones per team / roles | 1 captain + any number of players per team, + spectators; max 8 paired phones |
| 5 | Exact batting / bowling control scheme | Delivery type + tap-the-pitch + effort; shot buttons + direction pad + timing tap (MOBILE_CONTROLLER.md) |
| 6 | What happens when a phone disconnects mid-match | Game waits (timers held); "lost" after 60 s; AI does **not** take over automatically |
| 7 | iPhone support required? | Built with standard web APIs; only Android browsers tested |
| 8 | Physical gamepads on TV (spec 16 "gamepad support") | Belongs to the Unreal client (Enhanced Input); not part of the phone app |
| 9 | Languages for the phone app | English UI now; Hindi strings planned with the commentary languages |
| 10 | Port number | 47321 (changeable with `--port`) |

## Multiplayer backend (Priority 3, next)

| # | Missing detail | Assumption to be used |
|---|---|---|
| 11 | Hosting provider and budget | **No paid services without approval.** Design runs locally / self-hosted first |
| 12 | Login method (Google, phone OTP, email?) | Design for Google sign-in on Android TV + a guest mode; to confirm |
| 13 | Regions / expected number of players | Start with one region (India), design to scale out |
| 14 | Ranked rules: forfeits, disconnect penalties, rating system | Glicko-2 style rating; forfeit after the reconnect window (MULTIPLAYER_DESIGN.md) |
| 15 | Anti-cheat requirements beyond server authority | Server decides all outcomes; server-side timing checks; signed builds |
| 16 | Age rating / parental controls / chat | No text or voice chat initially (reduces moderation needs) |

## Long-term roadmap items without a specification yet

| # | Item | What's needed from the user |
|---|---|---|
| 17 | **Unreal Engine 3D gameplay** | Dev PC with GPU + UE5 install (DEVELOPMENT_PLAN.md); asset budget approval |
| 18 | **Broadcast-style scoreboards** | Visual style references (which broadcasts to look like), information per screen |
| 19 | **Commentary audio** | Voice actors / licensed recordings budget; languages beyond English + Hindi |
| 20 | **Cricket Creator Studio** (record matches, publish highlights to YouTube) | Recording format, highlight rules, YouTube account/API use (needs Google API project + OAuth consent; free tier, but must be approved), content ownership and music licensing |
| 21 | Licensed teams / players | Which licences, if any, will be pursued (currently fictional teams only) |
| 22 | Monetisation | Not specified. Ranked play must not depend on purchases (spec section 19) |
| 23 | ~~India vs Pakistan naming~~ | **Decided 2026-11-20:** real country names and national flags allowed (permitted assets). Players stay fictional. Done. |
| 24 | ~~Account recovery~~ | **Decided:** guest login stays the default; optional Google/email linking is a future feature. Design: [ACCOUNT_LINKING.md](ACCOUNT_LINKING.md). Not built. |
| 25 | ~~Online moderation~~ | **Decided:** blocked names, reports, admin review, temporary + permanent bans. Built: [MODERATION.md](MODERATION.md). Still open: age rating, written community rules, appeals contact. |
| 26 | ~~Server hosting~~ | **Decided:** self-hosting at ₹0 for now. Any paid hosting needs the user's explicit approval. |
| 27 | Afghanistan flag | We show the black-red-green flag the Afghanistan cricket team uses at ICC events. Confirm, or choose to show no flag. |
| 28 | Admin people | Who will be moderators, and the community rules / appeal email shown to banned players |
