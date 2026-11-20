// A whole match: owns the innings sequence and decides the result for
// limited-overs and Test cricket (follow-on, innings victories, draws).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "cricket/Drs.h"
#include "cricket/Innings.h"
#include "cricket/MatchRules.h"
#include "cricket/RainRule.h"

namespace cricket {

enum class MatchPhase { InProgress, AwaitingFollowOnDecision, Complete };

// Breaks in play. Lunch / tea / dinner / stumps happen in Test matches with a
// session schedule; an innings break is recorded in every format.
enum class IntervalKind { Lunch, Tea, Dinner, Stumps, InningsBreak };
const char* ToString(IntervalKind k);

struct IntervalRecord {
    IntervalKind kind = IntervalKind::Lunch;
    int day = 1;
    int session = 1;          // the session that just ended
    int inningsIndex = 0;     // innings in progress (or just finished)
    int battingSide = 0;
    int total = 0, wickets = 0, overs = 0, balls = 0;
    bool early = false;       // taken early because an innings ended close to it
};

// Where we are in a scheduled (Test) match.
struct DayClock {
    int day = 1;
    int session = 1;
    int ballsToday = 0;            // legal balls bowled today
    int ballsAvailableToday = 0;   // after changeovers / lost time
    int makeUpBalls = 0;           // lost time still to be made up on later days
    int startDay = 0;              // day of the first ball (0 = not started)
    int ballsLostToday = 0;        // to weather / bad light (Stage C)
};

// One weather stoppage.
struct RainRecord {
    int inningsIndex = 0;
    int minutes = 0;          // time lost
    int bufferUsed = 0;       // minutes absorbed by extra time (limited overs)
    int oversLost = 0;        // overs taken off (limited: total over both innings; Test: off the day)
    int day = 0;              // Test day (0 for limited overs)
    int newOversThis = 0;     // limited overs: innings lengths after the stoppage
    int newOversSecond = 0;
};

// Law 41 penalty runs (5 by default), e.g. ball strikes a fielder's helmet
// on the ground, illegal fielding, fielder wilfully obstructing a batter.
struct PenaltyRecord {
    int side = -1;           // side awarded the runs
    int runs = 0;
    int inningsIndex = -1;   // innings the runs were added to (-1 = waiting for that side's next innings)
    std::string reason;
};

struct MatchResult {
    enum class Kind { None, Win, Tie, Draw, NoResult } kind = Kind::None;
    enum class Margin { None, Runs, Wickets, InningsAndRuns } margin = Margin::None;
    int winner = -1;
    int value = 0;
    std::string summary;
};

class Match {
public:
    // Throws std::invalid_argument if a team is not a valid XI.
    Match(MatchRules rules, std::array<Team, 2> teams, int battingFirst);
    // A Match is a single live object (sessions and saves refer to it).
    Match(const Match&) = delete;
    Match& operator=(const Match&) = delete;

    static Status ValidateTeam(const Team& t, int squadSize);

    // Pass-through actions on the current innings (with extra team checks).
    Status SetOpeners(int striker, int nonStriker);
    Status SetBowler(int bowler);
    Status SetNextBatter(int batter);
    Status Bowl(const Delivery& d, DeliveryRecord* out = nullptr);
    Status Declare();

    // ---- Decisions and DRS --------------------------------------------
    // An appeal on the next ball. A PlayerAppeal (caught / LBW) waits for
    // ReviewDecision() or AcceptDecision(). An UmpireReferral (run out,
    // stumping, clean catch) is judged by the TV umpire straight away.
    // While a decision is pending, Bowl() is refused.
    Status Appeal(const DecisionCase& c, DecisionRecord* decision = nullptr, DeliveryRecord* ball = nullptr);
    // `side` asks the TV umpire to review the pending on-field call.
    Status ReviewDecision(int side, DecisionRecord* decision = nullptr, DeliveryRecord* ball = nullptr);
    // Nobody reviews (or the review timer ran out).
    Status AcceptDecision(DecisionRecord* decision = nullptr, DeliveryRecord* ball = nullptr);
    bool HasPendingDecision() const { return pending_.has_value(); }
    const DecisionCase* PendingDecision() const { return pending_ ? &*pending_ : nullptr; }
    // Which side may review the pending decision (-1 if none).
    int ReviewEntitledSide() const;
    // Unsuccessful reviews still available to `side` in the current innings.
    int ReviewsRemaining(int side) const { return reviewsLeft_.at(side); }
    const std::vector<DecisionRecord>& Decisions() const { return decisions_; }

    // ---- Penalty runs and the new ball -----------------------------------
    // Law 41: runs to the batting side go into the current innings; runs to
    // the fielding side go into its most recent innings, or its next innings
    // if it has not batted yet (Law 41.17). Targets are updated.
    Status AwardPenaltyRuns(int side, int runs, const std::string& reason);
    const std::vector<PenaltyRecord>& Penalties() const { return penalties_; }
    // Fielding captain takes the new ball (Test: after 80 overs).
    Status TakeNewBall();

    // ---- Weather (rain, bad light) -----------------------------------
    // Server/umpire actions. Limited overs: lost time (after any buffer) is
    // converted to overs, which are taken off equally in the first innings
    // or off the second innings, and the target is revised with the
    // NON-OFFICIAL rain rule. Test: the time comes off the day and is made up
    // on later days (up to 98 overs a day).
    Status RainDelay(int minutes);
    // Set innings lengths directly (what the umpires announce). Limited overs.
    // oversSecondInnings = 0 means "same as this innings" during innings 1.
    Status ReduceOvers(int oversThisInnings, int oversSecondInnings = 0);
    // The rest of today's play is lost (Test).
    Status LoseRestOfDay();
    const std::vector<RainRecord>& RainLog() const { return rainLog_; }
    bool RainAffected() const { return rainAffected_; }
    // Rain-rule resources (percent) available to each limited-overs innings.
    double RainResourcesFor(int inningsIndex) const { return resources_.at(inningsIndex); }
    // Par score for the side batting second right now (rain rule); -1 if not chasing.
    double ParScoreNow() const;
    int PlannedOvers(int inningsIndex) const { return plannedOvers_.at(inningsIndex); }

    // ---- Test match schedule ------------------------------------------
    bool HasSchedule() const { return rules_.schedule.enabled; }
    const DayClock& Clock() const { return clock_; }
    const std::vector<IntervalRecord>& Intervals() const { return intervals_; }
    // Balls left before the current session ends / before stumps today.
    int BallsLeftInSession() const;
    int BallsLeftToday() const { return clock_.ballsAvailableToday - clock_.ballsToday; }
    // Law 14.1: depends on the days left when play actually started.
    int FollowOnThreshold() const;

    // Test only: the side that batted first decides after innings 2.
    Status DecideFollowOn(bool enforce);
    // Time ran out (Test) or the game was called off.
    void CallDraw();
    void Abandon();

    MatchPhase Phase() const { return phase_; }
    const MatchResult& Result() const { return result_; }
    const MatchRules& Rules() const { return rules_; }
    const Team& GetTeam(int side) const { return teams_.at(side); }
    Innings& Current() { return innings_.back(); }
    const Innings& Current() const { return innings_.back(); }
    const std::vector<Innings>& AllInnings() const { return innings_; }
    int BattingSide() const { return Current().BattingSide(); }
    int FieldingSide() const { return 1 - Current().BattingSide(); }
    int RunsForSide(int side) const;
    bool FollowOnEnforced() const { return followOn_; }

private:
    void Advance();
    Status BowlInternal(const Delivery& d, DeliveryRecord* out);
    Status ValidateCase(const DecisionCase& c) const;
    Status ApplyDecision(const DrsOutcome& o, bool reviewed, int side, DecisionRecord* decision,
                         DeliveryRecord* ball);
    void StartInnings(int side, std::optional<int> target);
    void FinishWithChase(const Innings& chase, int target);
    std::string Name(int side) const { return teams_[side].name; }
    int TotalLegalBallsInMatch() const;
    void RecomputeTarget();
    void RecordInterval(IntervalKind k, bool early);
    // Called after an over ends, an innings changes or time is lost.
    void CheckSchedule(bool inningsJustChanged, bool force = false);
    void NoResult(const std::string& why);
    bool LimitedOvers() const { return rules_.oversPerInnings > 0; }
    // Shorten innings `idx` to `overs`, charging the lost resources.
    Status CutInnings(int idx, int overs);
    int SessionEndBalls(int session) const;
    void StartDay(int day);

    MatchRules rules_;
    std::array<Team, 2> teams_;
    int battingFirst_;
    std::vector<Innings> innings_;
    MatchPhase phase_ = MatchPhase::InProgress;
    MatchResult result_;
    bool followOn_ = false;
    std::optional<DecisionCase> pending_;
    std::array<int, 2> reviewsLeft_{0, 0};
    std::vector<DecisionRecord> decisions_;
    std::vector<PenaltyRecord> penalties_;
    std::array<int, 2> pendingPenalty_{0, 0};
    DayClock clock_;
    std::vector<RainRecord> rainLog_;
    bool rainAffected_ = false;
    int bufferUsed_ = 0;
    std::array<int, 2> plannedOvers_{0, 0};
    std::array<double, 2> resources_{0, 0};
    std::vector<IntervalRecord> intervals_;
};

}  // namespace cricket
