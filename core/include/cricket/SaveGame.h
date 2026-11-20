// Saving and resuming matches and server sessions.
//
// Format: versioned, line-based text (UTF-8, strings percent-encoded).
//   * A save holds the setup (rules, teams, batting side or server seed)
//     plus the JOURNAL of every action / input in order. Loading replays the
//     journal, so a resumed match is exactly the match that was saved.
//   * Validation: header + version, a checksum over the whole file, every
//     replayed step must succeed (session saves also check each step gives
//     the same accept/refuse result), and the final state fingerprint must
//     match the one stored at save time.
//   * Recovery: SaveFileAtomic writes to a temp file and keeps the previous
//     save as "<path>.bak"; Load*File falls back to the backup if the main
//     file is damaged. The crash log (MatchJournal / SessionJournal) appends
//     one checksummed line per action, so after a crash or power cut
//     everything up to the last good line is recovered.
#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "cricket/Match.h"
#include "cricket/MatchSession.h"

namespace cricket {

constexpr int kSaveFormatVersion = 1;

// ---- Whole-file saves ----------------------------------------------------
std::string SaveMatch(const Match& m);
std::string SaveSession(const MatchSession& s);

struct MatchLoad {
    std::unique_ptr<Match> match;
    Status status;
    size_t steps = 0;           // actions replayed
    bool fromBackup = false;    // file loads: the .bak copy was used
    std::string primaryError;   // why the main file was rejected (if it was)
};
struct SessionLoad {
    std::unique_ptr<MatchSession> session;
    Status status;
    size_t steps = 0;
    bool fromBackup = false;
    std::string primaryError;
};

MatchLoad LoadMatch(const std::string& text);
SessionLoad LoadSession(const std::string& text);

// ---- Files -----------------------------------------------------------------
// Write `content` to `path` safely: temp file first, previous version kept as
// "<path>.bak", then an atomic rename.
Status SaveFileAtomic(const std::string& path, const std::string& content);
Status SaveMatchFile(const Match& m, const std::string& path);
Status SaveSessionFile(const MatchSession& s, const std::string& path);
MatchLoad LoadMatchFile(const std::string& path);      // falls back to <path>.bak
SessionLoad LoadSessionFile(const std::string& path);  // falls back to <path>.bak

// ---- Crash log (append-only journal) ----------------------------------------
// Call Start once, then Sync after every action; each new action is appended
// and flushed with its own checksum.
class MatchJournal {
public:
    Status Start(const std::string& path, const Match& m);
    Status Sync(const Match& m);
    size_t Written() const { return written_; }

private:
    std::ofstream out_;
    size_t written_ = 0;
};

class SessionJournal {
public:
    Status Start(const std::string& path, const MatchSession& s);
    Status Sync(const MatchSession& s);
    size_t Written() const { return written_; }

private:
    std::ofstream out_;
    size_t written_ = 0;
};

struct Recovery {
    size_t recovered = 0;   // journal entries replayed
    size_t discarded = 0;   // damaged / incomplete lines dropped at the end
};
// Rebuild from a crash log: replays every intact entry, stops at the first
// damaged or unreplayable one.
MatchLoad RecoverMatch(const std::string& journalPath, Recovery* info = nullptr);
SessionLoad RecoverSession(const std::string& journalPath, Recovery* info = nullptr);

// Exposed for tests.
uint64_t Fnv1a64(const std::string& s);

}  // namespace cricket
