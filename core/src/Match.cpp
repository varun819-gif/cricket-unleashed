#include "cricket/Match.h"

#include <set>
#include <stdexcept>

namespace cricket {

Status Match::ValidateTeam(const Team& t, int squadSize) {
    if (t.name.empty()) return Status::Err("team needs a name");
    if (static_cast<int>(t.xi.size()) != squadSize)
        return Status::Err(t.name + ": playing XI must have exactly " + std::to_string(squadSize) + " players");
    std::set<std::string> names;
    for (const auto& p : t.xi) {
        if (p.name.empty()) return Status::Err(t.name + ": every player needs a name");
        if (!names.insert(p.name).second) return Status::Err(t.name + ": duplicate player " + p.name);
    }
    if (t.captain < 0 || t.captain >= squadSize) return Status::Err(t.name + ": captain must be in the XI");
    if (t.keeper < 0 || t.keeper >= squadSize) return Status::Err(t.name + ": wicketkeeper must be in the XI");
    int bowlers = 0;
    for (const auto& p : t.xi) bowlers += p.canBowl ? 1 : 0;
    if (bowlers < 5) return Status::Err(t.name + ": pick at least 5 bowling options");
    return Status::Ok();
}

Match::Match(MatchRules rules, std::array<Team, 2> teams, int battingFirst)
    : rules_(std::move(rules)), teams_(std::move(teams)), battingFirst_(battingFirst) {
    if (battingFirst != 0 && battingFirst != 1) throw std::invalid_argument("battingFirst must be 0 or 1");
    for (const auto& t : teams_) {
        Status s = ValidateTeam(t, rules_.squadSize);
        if (!s) throw std::invalid_argument(s.error);
    }
    StartInnings(battingFirst_, std::nullopt);
}

void Match::StartInnings(int side, std::optional<int> target) {
    innings_.emplace_back(rules_, side, static_cast<int>(innings_.size()), target);
    phase_ = MatchPhase::InProgress;
}

int Match::RunsForSide(int side) const {
    int r = 0;
    for (const auto& i : innings_)
        if (i.BattingSide() == side) r += i.Total();
    return r;
}

int Match::TotalLegalBallsInMatch() const {
    int b = 0;
    for (const auto& i : innings_) b += i.LegalBalls();
    return b;
}

static Status NotInProgress(MatchPhase p) {
    if (p == MatchPhase::Complete) return Status::Err("match is over");
    return Status::Err("waiting for the follow-on decision");
}

Status Match::SetOpeners(int s, int n) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    return Current().SetOpeners(s, n);
}

Status Match::SetBowler(int b) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    return Current().SetBowler(b);
}

Status Match::SetNextBatter(int b) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    return Current().SetNextBatter(b);
}

Status Match::Bowl(const Delivery& d, DeliveryRecord* out) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    Status s = Current().Bowl(d, out);
    if (!s) return s;
    if (Current().IsComplete()) {
        Advance();
    } else if (rules_.matchOverLimit > 0 &&
               TotalLegalBallsInMatch() >= rules_.matchOverLimit * rules_.ballsPerOver) {
        CallDraw();  // out of time
    }
    return Status::Ok();
}

Status Match::Declare() {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    Status s = Current().Declare();
    if (s) Advance();
    return s;
}

Status Match::DecideFollowOn(bool enforce) {
    if (phase_ != MatchPhase::AwaitingFollowOnDecision) return Status::Err("no follow-on decision is pending");
    followOn_ = enforce;
    StartInnings(enforce ? 1 - battingFirst_ : battingFirst_, std::nullopt);
    return Status::Ok();
}

void Match::CallDraw() {
    if (phase_ == MatchPhase::Complete) return;
    Current().ForceComplete();
    phase_ = MatchPhase::Complete;
    if (rules_.format == Format::Test) {
        result_.kind = MatchResult::Kind::Draw;
        result_.summary = "Match drawn";
    } else {
        result_.kind = MatchResult::Kind::NoResult;
        result_.summary = "No result";
    }
}

void Match::Abandon() {
    if (phase_ == MatchPhase::Complete) return;
    Current().ForceComplete();
    phase_ = MatchPhase::Complete;
    result_.kind = MatchResult::Kind::NoResult;
    result_.summary = "Match abandoned - no result";
}

void Match::FinishWithChase(const Innings& chase, int target) {
    phase_ = MatchPhase::Complete;
    const int chasing = chase.BattingSide();
    const int defending = 1 - chasing;
    if (chase.Total() >= target) {
        int left = rules_.squadSize - 1 - chase.Wickets();
        result_ = {MatchResult::Kind::Win, MatchResult::Margin::Wickets, chasing, left,
                   Name(chasing) + " won by " + std::to_string(left) + (left == 1 ? " wicket" : " wickets")};
        return;
    }
    const bool chaseFinished = chase.EndReason() == InningsEnd::AllOut ||
                               chase.EndReason() == InningsEnd::OversComplete;
    if (!chaseFinished) {  // declared / stopped in a Test
        result_.kind = MatchResult::Kind::Draw;
        result_.summary = "Match drawn";
        return;
    }
    if (chase.Total() == target - 1) {
        result_ = {MatchResult::Kind::Tie, MatchResult::Margin::None, -1, 0, "Match tied"};
        return;
    }
    int by = target - 1 - chase.Total();
    result_ = {MatchResult::Kind::Win, MatchResult::Margin::Runs, defending, by,
               Name(defending) + " won by " + std::to_string(by) + (by == 1 ? " run" : " runs")};
}

void Match::Advance() {
    const int done = static_cast<int>(innings_.size());
    const Innings& last = innings_.back();

    if (rules_.inningsPerSide == 1) {
        if (done == 1) {
            StartInnings(1 - last.BattingSide(), last.Total() + 1);
        } else {
            FinishWithChase(innings_[1], *innings_[1].Target());
        }
        return;
    }

    // Two-innings (Test) match.
    switch (done) {
        case 1:
            StartInnings(1 - battingFirst_, std::nullopt);
            break;
        case 2: {
            int lead = innings_[0].Total() - innings_[1].Total();
            if (rules_.followOnLead > 0 && lead >= rules_.followOnLead) phase_ = MatchPhase::AwaitingFollowOnDecision;
            else StartInnings(battingFirst_, std::nullopt);
            break;
        }
        case 3: {
            const int twice = innings_[2].BattingSide();
            const int once = 1 - twice;
            const int t2 = RunsForSide(twice), t1 = RunsForSide(once);
            if (t2 < t1) {
                phase_ = MatchPhase::Complete;
                int by = t1 - t2;
                result_ = {MatchResult::Kind::Win, MatchResult::Margin::InningsAndRuns, once, by,
                           Name(once) + " won by an innings and " + std::to_string(by) + (by == 1 ? " run" : " runs")};
            } else {
                StartInnings(once, t2 - t1 + 1);
            }
            break;
        }
        default:
            FinishWithChase(innings_[3], *innings_[3].Target());
            break;
    }
}

}  // namespace cricket
