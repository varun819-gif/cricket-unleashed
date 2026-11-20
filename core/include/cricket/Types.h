// Cricket Unleashed - core gameplay types.
// Engine independent: no Unreal headers here so the same logic runs on the
// dedicated server, in unit tests and inside the Unreal client module.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cricket {

enum class Format { T20, ODI, Test, Custom };

enum class ExtraType { None, Wide, NoBall, Bye, LegBye };

enum class Dismissal {
    None,
    Bowled,
    Caught,
    Lbw,
    Stumped,
    RunOut,
    HitWicket,
    ObstructingField,
    HitBallTwice,
    TimedOut,
    RetiredOut
};

enum class BowlingKind { Pace, Spin };
enum class Handedness { Right, Left };

const char* ToString(Dismissal d);
const char* ToString(ExtraType e);

// Did the bowler take this wicket (counts in bowling figures)?
bool CreditedToBowler(Dismissal d);

struct Player {
    std::string name;
    Handedness bats = Handedness::Right;
    BowlingKind bowls = BowlingKind::Pace;
    bool canBowl = false;
    int batting = 50;   // 0..100
    int bowling = 50;   // 0..100
    int fielding = 50;  // 0..100
    int fitness = 80;   // 0..100
    int form = 50;      // 0..100
};

// A playing XI. Index in `xi` doubles as batting order and player id
// inside a match.
struct Team {
    std::string name;
    std::vector<Player> xi;
    int captain = 0;
    int keeper = 0;
};

// Simple success/error result used across the API (no exceptions in the hot path).
struct Status {
    bool ok = true;
    std::string error;
    static Status Ok() { return {}; }
    static Status Err(std::string msg) { return {false, std::move(msg)}; }
    explicit operator bool() const { return ok; }
};

}  // namespace cricket
