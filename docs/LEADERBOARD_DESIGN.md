# Leaderboard Design

## Trust model

Only **server-validated, completed** matches count. The dedicated server
writes the result straight to the database with the match id. Clients cannot
submit results. Each match id can be recorded only once (unique key), so the
same match can't be counted twice. Ranked and casual stats are kept apart.

## Data (PostgreSQL)

```
players(id, username UNIQUE, avatar_url, country, created_at, fair_play_score)
matches(id, mode[ranked|casual|tournament], format, started_at, ended_at,
        server_id, result_kind, winner_player_id NULL, validated BOOL)
match_players(match_id, player_id, side, team_name, rating_before, rating_after,
              completed BOOL, PRIMARY KEY(match_id, player_id))
batting_stats(match_id, player_id, runs, balls, fours, sixes, not_out)
bowling_stats(match_id, player_id, balls, runs, wickets)
tournaments(id, name, format, kind, status, created_by)
tournament_entries(tournament_id, player_id, points, net_run_rate)
achievements(player_id, code, earned_at)
```

Profile totals (wins, losses, draws, win %, highest score, best bowling, etc.)
are **views** built from these tables. They are cached, never edited by hand.

## Leaderboards (Redis sorted sets, rebuilt from Postgres)

- Keys like `lb:{category}:{scope}:{period}`. Category = rating, wins,
  win_pct, sixes, fours, wickets, high_score, pom... Scope = global,
  country:{code}, or friends (computed on request). Period = weekly:{yyyy-ww},
  monthly:{yyyy-mm}, alltime.
- Win % boards need a minimum match count (for example 20) so a single lucky
  win doesn't top the board.

## Ranked rating

Glicko-2 style rating with a confidence value. Points depend on result,
opponent rating, competition type and match completion. Quitting counts as a
loss, plus a fair-play penalty. No purchased item can change gameplay in
ranked.

## Anti-abuse

- The same two accounts farming wins: rating gain from repeat opponents shrinks
  within 24 h.
- Abandoned matches give no positive points.
- Unusual stat jumps get flagged for review before weekly boards are
  finalised.
