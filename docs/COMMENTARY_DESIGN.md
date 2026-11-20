# Commentary Design

## Rules

- Commentary is built **only from verified events** (`DeliveryRecord`,
  match result). It never guesses.
- Two voices: **Lead** (describes the ball) and **Expert** (analysis on big
  moments: wickets, some boundaries, chase pressure in the last 6 overs).
- No back-to-back repeats: each category remembers its last line.
- In multiplayer both players get the same events from the server. The
  commentary seed is part of the match, so both hear the same lines.

## Implemented (`core/src/Commentary.cpp`)

- Languages: **English** and **Hindi** (Devanagari). More Indian languages
  are added as new pools with the same category keys.
- Categories: dot, single, runs, four, six, wide, noball, freehit, bye,
  legbye, wicket.{bowled,caught,lbw,stumped,runout,other}, fifty, hundred,
  maiden, over, innings, result, expert.{wicket,boundary,chase}.
- Decisions (`DescribeDecision`): appeal.{out,notout}, review.{taken,
  overturned,upheld,umpirescall}, thirdumpire.{out,notout}, noball.check,
  expert.review (the expert explains the replay reason in the commentary
  language: every DRS reason code has English and Hindi text).
- Also: call.{signalled,notsignalled,changed,stands}, boundary.{check,six,
  four}, penalty, newball, shortrun, interval.{lunch,tea,dinner,stumps,
  innings}.
- Each output line has `category` + `lineId`, which map 1:1 to **recorded
  audio clips**. Text is also the subtitle and the fallback when a clip is
  missing.

## Planned

- **Styles**: Broadcast (neutral), Energetic Indian (more excitement, Hinglish
  phrases), Calm Analytical (more expert lines). Each style is a separate pool.
- Name call-outs: recorded name clips for fictional players. For licensed
  players, only with rights.
- Toss, team walk-out, innings break, partnership and milestone summaries,
  DRS calls, last-over drama, presentation ceremony.
- Settings: commentary / crowd / effects volume, language, personality.
- Optional AI-generated commentary later. It would be fed the same verified
  events, and the recorded lines stay as fallback.
- All voice recordings must be licensed and listed in ASSET_REGISTER.md.
