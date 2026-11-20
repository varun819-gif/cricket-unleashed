// Accounts, ratings, match history, player statistics and leaderboards.
//
// Development storage is free and self-hosted: append-only JSON-lines files
// in a data folder, every line carrying a checksum (damaged lines are
// skipped on load). The interface is small so a PostgreSQL implementation
// can replace it for cloud scaling (see docs/ONLINE_MULTIPLAYER.md).
//
// Security: guest tokens are 256-bit random secrets kept by the player's
// device. Only their SHA-256 is stored, so a leaked data file can't be used
// to log in.
#pragma once

#include <array>
#include <climits>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "cricket/Types.h"
#include "cricket/controller/Crypto.h"
#include "cricket/controller/Json.h"

namespace cricket::online {

using ctl::Json;

struct PlayerStats {
    int matches = 0, wins = 0, losses = 0, ties = 0, noResults = 0;
    int completed = 0;                   // played to a real result (not forfeit / no result)
    int rankedMatches = 0, rankedWins = 0;
    int runs = 0, wicketsTaken = 0, fours = 0, sixes = 0;
    int highestIndividual = 0;           // best score by a batter on the player's team
    int bestBowlingWickets = 0, bestBowlingRuns = 0;
    int highestTeamTotal = 0;
};

struct Account {
    std::string id;
    std::string name;
    std::string country;    // two-letter code chosen by the player, e.g. "IN"
    std::string tokenHash;  // SHA-256 of the guest token (hex)
    int64_t createdMs = 0;
    int rating = 1200;      // Elo, ranked matches only
    int ratedGames = 0;
    // Moderation: banned until this time (0 = not banned, kPermanentBan = for good).
    int64_t banUntilMs = 0;
    std::string banReason;
    PlayerStats stats;
    bool BannedAt(int64_t nowMs) const { return banUntilMs != 0 && nowMs < banUntilMs; }
};

constexpr int64_t kPermanentBan = INT64_MAX;

// A player's report about another player, reviewed by an admin.
struct Report {
    std::string id, reporter, target;
    std::string reason;   // cheating | abuse | name | quitting | other
    std::string matchId, note;
    int64_t createdMs = 0;
    std::string status = "open";  // open | actioned | dismissed
    std::string resolution;       // admin's note
    int64_t resolvedMs = 0;
};

// One side of a finished match, as computed by the SERVER from its own
// scorebook (never from anything a client sends).
struct SideResult {
    std::string account;   // empty for an AI side
    std::string team;
    int total = 0, wickets = 0, legalBalls = 0;
    int fours = 0, sixes = 0;
    int bestIndividual = 0;
    int wicketsTaken = 0;  // by this side's bowlers
    int bestBowlingWickets = 0, bestBowlingRuns = 0;
};

struct MatchRecord {
    std::string id;
    std::string mode;     // "casual" | "ranked"
    std::string format;   // "t20", "odi", "overs=5", ...
    int64_t startedMs = 0, endedMs = 0;
    std::array<SideResult, 2> sides;
    int winner = -1;      // side, -1 = tie / no result
    std::string outcome;  // "win" | "tie" | "no-result" | "forfeit"
    std::string summary;
    std::array<int, 2> ratingDelta{0, 0};
    std::string saveFile; // replayable session save (evidence for disputes)
};

constexpr int kMinBallsForRatedWin = 12;
constexpr int kProvisionalGames = 5;

class Store {
public:
    // Empty dir = in memory only (tests).
    Status Open(const std::string& dir);

    // Returns the new account and its token (shown to the device once).
    std::pair<Account, std::string> CreateGuest(const std::string& name, const std::string& country, int64_t nowMs,
                                                 ctl::RandomSource& rnd);
    // Constant-time check of a presented token.
    const Account* Verify(const std::string& id, const std::string& token) const;
    const Account* Find(const std::string& id) const;
    Status UpdateProfile(const std::string& id, const std::string& name, const std::string& country);

    // Records a finished match: updates both players' stats and (ranked)
    // ratings. Ratings: Elo, K = 40 for a player's first 10 ranked games
    // then 20. Anti-manipulation rules (see docs/ONLINE_MULTIPLAYER.md):
    //  * a pair meeting more than 3 times in ranked within 24 hours gets no
    //    rating change (win-trading guard);
    //  * a forfeit before kMinBallsForRatedWin legal balls: the leaver still
    //    loses rating, the winner gains nothing (no farming by instant concedes);
    //  * a gain against a provisional opponent (fewer than 5 ranked games)
    //    is halved for an established player (no farming with new accounts).
    // Ratings are always rebuilt from the match records when loading, so the
    // match history is the single source of truth. Returns the stored record.
    MatchRecord RecordMatch(MatchRecord rec);

    std::vector<MatchRecord> History(const std::string& account, size_t limit) const;
    bool PlayedTogether(const std::string& a, const std::string& b) const;

    // ---- moderation (docs/MODERATION.md) ----
    // Rules: reason must be known; you can only report someone you played;
    // at most 5 reports a day per reporter and one per player per day.
    Status AddReport(const std::string& reporter, const std::string& target, const std::string& reason,
                     const std::string& matchId, const std::string& note, int64_t nowMs, ctl::RandomSource& rnd,
                     std::string* idOut = nullptr);
    std::vector<Report> Reports(const std::string& status, size_t limit) const;  // newest first
    std::vector<Report> ReportsAbout(const std::string& target) const;
    // until = kPermanentBan for a permanent ban. Every action is logged with who did it.
    Status Ban(const std::string& id, int64_t untilMs, const std::string& reason, const std::string& by, int64_t nowMs);
    Status Unban(const std::string& id, const std::string& by, int64_t nowMs);
    Status ResolveReport(const std::string& reportId, const std::string& status, const std::string& note,
                         const std::string& by, int64_t nowMs);
    Status ResetName(const std::string& id, const std::string& by, int64_t nowMs);
    const MatchRecord* FindMatch(const std::string& id) const;

    // board: rating | wins | winpct | runs | wickets | sixes | fours | highscore | matches
    // period: all | week | month (by match end time); mode: all | ranked | casual
    // country: "" = global. Win % needs at least 5 matches in the period.
    // Stat boards count only matches played to a result (no forfeits).
    Json Leaderboard(const std::string& board, const std::string& period, const std::string& mode,
                     const std::string& country, int64_t nowMs, size_t limit) const;
    Json StatsJson(const std::string& account) const;

    size_t SkippedLines() const { return skipped_; }
    size_t AccountCount() const { return accounts_.size(); }

    static int EloDelta(int rating, int opponent, double score, int ratedGames);
    static Json MatchJson(const MatchRecord& m) { return ToJson(m); }

private:
    Status Append(const std::string& file, const Json& j);
    void ApplyMatch(const MatchRecord& rec);
    static Json ToJson(const Account& a);
    static Json ToJson(const MatchRecord& m);
    static std::optional<Account> AccountFrom(const Json& j);
    static std::optional<MatchRecord> MatchFrom(const Json& j);

    void LogModeration(const std::string& by, const std::string& action, const std::string& target,
                       const std::string& detail, int64_t nowMs);
    static Json ToJson(const Report& r);

    std::string dir_;
    std::map<std::string, Account> accounts_;
    std::vector<Report> reports_;
    std::vector<MatchRecord> matches_;
    size_t skipped_ = 0;
};

}  // namespace cricket::online
