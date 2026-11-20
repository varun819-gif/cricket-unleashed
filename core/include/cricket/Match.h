// A whole match: owns the innings sequence and decides the result for
// limited-overs and Test cricket (follow-on, innings victories, draws).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "cricket/Drs.h"
#include "cricket/Innings.h"
#include "cricket/MatchRules.h"

namespace cricket {

enum class MatchPhase { InProgress, AwaitingFollowOnDecision, Complete };

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
    // Innings keep a pointer to rules_, so a Match must stay where it was built.
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
};

}  // namespace cricket
