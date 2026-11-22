// Match recording and highlights: the data bridge to the 3D game (Unreal) and
// the future Cricket Creator Studio (replays, YouTube highlights).
//
// While a match is played, MatchRecorder keeps a compact trace of every ball
// the physics played (the ball's flight every 10 ms, the bounce, the bat
// contact, where the ball went after the shot and who fielded it). At the end
// it is joined with the official scorebook (Match) and written as JSON lines:
//   line 1: {"type":"match", teams, conditions, result, version}
//   then one {"type":"ball", ...} line per delivery, in order,
//   then {"type":"highlights", ...}.
// Unreal can replay any ball from this file (move the ball along the samples,
// time animations to release / bounce / contact), and the Creator Studio can
// cut highlight packages from it. Nothing here draws or records video: it is
// the data a renderer needs. Coordinates as in BallPhysics.h (metres, origin
// at the striker's middle stump, +y towards the bowler, +x off side, +z up).
#pragma once

#include <string>
#include <vector>

#include "cricket/BallOutcome.h"
#include "cricket/Match.h"

namespace cricket {

struct TracePoint {
    float t = 0;          // seconds from release
    float x = 0, y = 0, z = 0;
};

// One ball as the physics played it (compact).
struct BallTrace {
    double speedKph = 0;
    std::string variation;
    bool pitched = false;
    TracePoint bounce{};
    double releaseHeight = 0;
    std::vector<TracePoint> flight;     // release -> batter (every 10 ms)
    std::string shot;                   // shot played ("leave" if none)
    std::string contact;                // middled, outside edge, missed, pad ...
    double exitKph = 0;
    std::vector<TracePoint> afterShot;  // the ball after the shot (every 50 ms)
    int fielder = -1;                   // fielding-side player who fielded it
    bool caught = false;
    std::string summary;                // physics summary text
    int runsCalled = -1;                // the batters' call from the phone (-1 = computer judged)
    bool runOutChance = false, stumpingChance = false;
};

BallTrace TraceOf(const BallReport& r);

enum class HighlightKind { Wicket, Six, Four, Review, Fifty, Hundred, Finish, Maiden, CloseRunOut };
const char* ToString(HighlightKind k);

struct Highlight {
    HighlightKind kind = HighlightKind::Four;
    int ballIndex = 0;      // index into the match's deliveries (all innings, in order)
    int inningsIndex = 0;
    int over = 0, ballInOver = 0;
    int importance = 0;     // higher = more worth showing (0-100)
    std::string text;       // e.g. "WICKET! Kabir Chauhan c First slip b Asad Qureshi 23"
};

// Every highlight-worthy moment, in match order.
std::vector<Highlight> FindHighlights(const Match& m, const std::vector<BallTrace>* traces = nullptr);
// A highlights package: the best `maxClips` moments with a mix of kinds
// (wickets, sixes, fours, reviews...), always the finish; in match order.
std::vector<Highlight> HighlightPackage(const std::vector<Highlight>& all, size_t maxClips);

class MatchRecorder {
public:
    // Call once per ball the physics played, before it is scored.
    void Add(const BallReport& r, const std::string& variation = "") {
        traces_.push_back(TraceOf(r));
        traces_.back().variation = variation;
    }
    const std::vector<BallTrace>& Traces() const { return traces_; }
    void Clear() { traces_.clear(); }
    // JSON lines (see the top of this file). `conditions` may be empty.
    std::string ToJsonLines(const Match& m, const std::string& conditions = "") const;

private:
    std::vector<BallTrace> traces_;
};

// All the deliveries of a match in order (all innings).
std::vector<const DeliveryRecord*> AllDeliveries(const Match& m);

}  // namespace cricket
