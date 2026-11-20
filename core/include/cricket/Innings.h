// One innings: the scorer. Applies resolved deliveries and enforces the laws
// about extras, dismissals, strike rotation, overs and bowler restrictions.
#pragma once

#include <optional>
#include <vector>

#include "cricket/MatchRules.h"
#include "cricket/Types.h"

namespace cricket {

// A delivery as resolved by the (server-side) physics / gameplay simulation.
struct Delivery {
    ExtraType extra = ExtraType::None;
    // Runs completed by running, or the boundary value (4 or 6) when boundary == true.
    // Off the bat for None/NoBall; extras for Wide/Bye/LegBye.
    int runs = 0;
    bool boundary = false;
    Dismissal dismissal = Dismissal::None;
    // Which batter is out, relative to who was on strike when the ball was bowled.
    // Only a run out (or obstruction) can dismiss the non-striker.
    bool strikerOut = true;
    int fielder = -1;  // fielding-side index (catcher, run-out thrower...), informational
};

struct BatterCard {
    int runs = 0, balls = 0, fours = 0, sixes = 0;
    bool batted = false, out = false;
    Dismissal how = Dismissal::None;
    int bowler = -1;
};

struct BowlerCard {
    int legalBalls = 0, runs = 0, wickets = 0, maidens = 0, wides = 0, noBalls = 0;
};

struct Extras {
    int wides = 0, noBalls = 0, byes = 0, legByes = 0;
    int Total() const { return wides + noBalls + byes + legByes; }
};

// Everything a presenter (UI, commentary, replay, network) needs about one ball.
struct DeliveryRecord {
    int inningsIndex = 0;
    int over = 0;          // 0-based over in which the ball was bowled
    int ballInOver = 0;    // legal balls completed in the over after this ball
    int bowler = -1, striker = -1, nonStriker = -1;
    Delivery delivery;
    bool legal = true;
    bool wasFreeHit = false;
    int runsToTeam = 0;    // everything added to the total
    int runsToBatter = 0;
    int totalAfter = 0, wicketsAfter = 0;
    int dismissedBatter = -1;
    bool overCompleted = false;
    bool maiden = false;
    bool inningsCompleted = false;
    bool batterFifty = false, batterHundred = false;
};

enum class InningsState { AwaitingOpeners, AwaitingBowler, AwaitingBatter, InPlay, Complete };

enum class InningsEnd { None, AllOut, OversComplete, TargetReached, Declared, Forced };

class Innings {
public:
    Innings(const MatchRules& rules, int battingSide, int index, std::optional<int> target);

    Status SetOpeners(int striker, int nonStriker);
    Status SetBowler(int bowler);
    Status SetNextBatter(int batter);
    Status Bowl(const Delivery& d, DeliveryRecord* out = nullptr);
    // Would this delivery be accepted right now (ignoring whose turn it is)?
    Status Check(const Delivery& d) const;
    Status Declare();
    void ForceComplete();  // rain / time / match result reached elsewhere

    // Is this dismissal possible on this kind of ball (and on a free hit)?
    static bool DismissalAllowed(Dismissal d, ExtraType e, bool freeHit);

    InningsState State() const;
    InningsEnd EndReason() const { return end_; }
    bool IsComplete() const { return end_ != InningsEnd::None; }
    int BattingSide() const { return battingSide_; }
    int Index() const { return index_; }
    int Total() const { return total_; }
    int Wickets() const { return wickets_; }
    int LegalBalls() const { return legalBalls_; }
    int CompletedOvers() const { return legalBalls_ / rules_->ballsPerOver; }
    int BallsInOver() const { return legalBalls_ % rules_->ballsPerOver; }
    int CurrentOverNumber() const { return CompletedOvers() + 1; }  // 1-based
    std::optional<int> Target() const { return target_; }
    int Striker() const { return striker_; }
    int NonStriker() const { return nonStriker_; }
    int CurrentBowler() const { return bowler_; }
    int LastOverBowler() const { return lastOverBowler_; }
    bool NextBallIsFreeHit() const { return freeHit_; }
    const Extras& GetExtras() const { return extras_; }
    const BatterCard& Batter(int i) const { return batters_.at(i); }
    const BowlerCard& Bowler(int i) const { return bowlers_.at(i); }
    const std::vector<DeliveryRecord>& Balls() const { return balls_; }
    std::string ScoreString() const;  // e.g. "145/6 (18.3)"
    double RunRate() const;
    // Required rate for a chase, or -1 when not chasing / unlimited overs.
    double RequiredRunRate() const;
    bool CanBowlNextOver(int bowler) const;

private:
    void CheckCompletion();

    const MatchRules* rules_;
    int battingSide_;
    int index_;
    std::optional<int> target_;
    std::vector<BatterCard> batters_;
    std::vector<BowlerCard> bowlers_;
    std::vector<DeliveryRecord> balls_;
    Extras extras_;
    int total_ = 0, wickets_ = 0, legalBalls_ = 0;
    int striker_ = -1, nonStriker_ = -1, bowler_ = -1, lastOverBowler_ = -1;
    bool openersSet_ = false;
    bool needBatter_ = false;
    bool freeHit_ = false;
    int overRunsVsBowler_ = 0;
    InningsEnd end_ = InningsEnd::None;
};

}  // namespace cricket
