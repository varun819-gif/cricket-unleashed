# Saving, Resuming and Crash Recovery

Code: `core/include/cricket/SaveGame.h`, `core/src/SaveGame.cpp`, plus the
journals in `Match` (`Journal()`, `Apply()`, `StateFingerprint()`) and
`MatchSession` (`Inputs()`, `Replay()`, `StateFingerprint()`). Tests:
`core/tests/SaveTests.cpp`.

## How it works: the scorebook, not a snapshot

A save does **not** copy hundreds of internal variables. It stores:

1. the **setup**: playing conditions (format, DRS ruleset, Test schedule,
   weather settings), both teams, and who batted first (single match) or the
   server's random seed (online session);
2. the **journal**: every action in order (each ball, appeal, review, field
   change, declaration, rain delay, penalty run...). For an online session,
   every input the server received, **including refused ones** (a refused
   command still uses up its sequence number).

Loading builds a fresh match from the setup and **replays the journal**
through the normal rules engine. So:
- a resumed match is *exactly* the saved match: same scores, review counts,
  free hit, day and session, rain-rule resources, AI random state;
- an edited or impossible action is caught by the same law checks as live
  play;
- the format survives code changes that don't change the rules.

## File format (version 1)

Plain UTF-8 text, one record per line, values separated by spaces, text
percent-encoded (so names in Hindi, quotes and spaces are safe):

```
CRICKET-UNLEASHED-SAVE match 1
rules ...            (every playing-condition value)
team 0 <name> <captain> <keeper> 11
player <name> <bats> <bowls> <canBowl> <batting> <bowling> <fielding> <fitness> <form>   x11
team 1 ...
start <battingFirst>            (session saves: seed <n>)
act ...                         (session saves: in <accepted> ...)
...
end <number of actions> <fingerprint hash>
checksum <FNV-1a 64-bit hash of everything above>
```

Doubles are written with 17 significant digits, so they round-trip exactly.

## Validation on load (in this order)

| Check | Catches |
|---|---|
| `checksum` line present and matching | damaged, truncated or hand-edited files |
| header magic, kind (`match` / `session`), version | wrong file type, saves from a newer game version |
| setup parsing with range checks + `Match::ValidateRules` | impossible settings (e.g. 0 balls per over) |
| every action replays successfully | impossible actions (e.g. 9 runs off a ball) |
| session saves: each input gets the same accepted/refused result | a doctored session history |
| action count + **state fingerprint** match the `end` line | a "cheater" who edits a ball and recomputes the checksum: the replayed scores no longer match |

A failed load returns a clear error and **no match object** (never a
half-loaded match).

## Recovery

- **Safe file writes** (`SaveFileAtomic`): the new save goes to `<path>.tmp`,
  the previous save is kept as `<path>.bak`, and then the new file is
  renamed into place (atomic on the same disk). A crash during saving never
  destroys the last good save.
- **Backup fallback** (`LoadMatchFile` / `LoadSessionFile`): if the main file
  fails validation, the `.bak` copy is loaded and the result says so
  (`fromBackup`, `primaryError`).
- **Crash log** (`MatchJournal` / `SessionJournal`): written as the match is
  played, one line per action, flushed immediately, each line with its own
  checksum. After a crash or power cut, `RecoverMatch` / `RecoverSession`
  replay every intact line and stop at the first damaged or cut-off one. A
  final line without its line break is never trusted. The result reports how
  many entries were recovered and how many were discarded.
- **Multiplayer server crash**: the server keeps a `SessionJournal` per room.
  A restarted server calls `RecoverSession` and gets back the same match,
  event log (so clients can catch up with `EventsSince`), review timers,
  break pauses and duplicate-command protection.

## Known limits

- Save files are **validated, not encrypted or signed**. The checksum stops
  accidents, and the state fingerprint stops simple score edits. A
  determined person can still write a *consistent* fake history offline. For
  **ranked online play this doesn't matter**: results come only from the
  server's own journal (see MULTIPLAYER_DESIGN.md). A server-side signing key
  could be added when the backend exists.
- Format version 1 only. Older versions will need a converter when version
  2 arrives (the loader already refuses files from newer versions).
- Very long Test matches give larger files (about 3,000-4,000 lines). Fine on
  disk; not compressed.
