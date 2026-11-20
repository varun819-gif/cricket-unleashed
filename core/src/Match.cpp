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
    if (rules_.drs.reviewsPerInnings < 0 || rules_.drs.reviewTimerSeconds < 0)
        throw std::invalid_argument("DRS settings cannot be negative");
    for (const auto& t : teams_) {
        Status s = ValidateTeam(t, rules_.squadSize);
        if (!s) throw std::invalid_argument(s.error);
    }
    StartInnings(battingFirst_, std::nullopt);
}

void Match::StartInnings(int side, std::optional<int> target) {
    innings_.emplace_back(rules_, side, static_cast<int>(innings_.size()), target);
    phase_ = MatchPhase::InProgress;
    const int reviews = rules_.drs.playerReviews ? rules_.drs.reviewsPerInnings : 0;
    reviewsLeft_ = {reviews, reviews};  // fresh reviews each innings
    pending_.reset();
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
    if (pending_) return Status::Err("a decision is pending: review it or accept it first");
    return BowlInternal(d, out);
}

Status Match::BowlInternal(const Delivery& d, DeliveryRecord* out) {
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
    if (pending_) return Status::Err("a decision is pending");
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
    pending_.reset();
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
    pending_.reset();
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

// ---------------------------------------------------------------------------
// Decisions and DRS

int Match::ReviewEntitledSide() const {
    if (!pending_) return -1;
    // The side the on-field call went against may review it.
    return pending_->onFieldOut ? BattingSide() : FieldingSide();
}

Status Match::ValidateCase(const DecisionCase& c) const {
    if (c.mode == Dismissal::None) return Status::Err("appeal needs a mode of dismissal");
    if (c.kind == DecisionKind::PlayerAppeal && !IsPlayerReviewable(c.mode))
        return Status::Err(std::string("players appeal for caught or LBW; ") + ToString(c.mode) +
                           " is referred by the umpires");
    if (c.kind == DecisionKind::UmpireReferral && !IsUmpireReferable(c.mode))
        return Status::Err(std::string("umpires do not refer ") + ToString(c.mode));
    if (c.mode == Dismissal::Lbw && !c.evidence.lbw) return Status::Err("an LBW appeal needs ball-tracking evidence");
    if (c.ifOut.dismissal != c.mode) return Status::Err("ifOut must record the appealed mode of dismissal");
    if (c.ifNotOut.dismissal != Dismissal::None) return Status::Err("ifNotOut must not contain a dismissal");
    const Innings& inn = Current();
    if (Status s = inn.Check(c.ifOut); !s) return Status::Err("ifOut: " + s.error);
    if (Status s = inn.Check(c.ifNotOut); !s) return Status::Err("ifNotOut: " + s.error);
    if (c.mode == Dismissal::Caught && c.evidence.lbw) {
        Delivery lbw = c.ifOut;
        lbw.dismissal = Dismissal::Lbw;
        if (Status s = inn.Check(lbw); !s) return Status::Err("LBW check: " + s.error);
    }
    return Status::Ok();
}

Status Match::Appeal(const DecisionCase& c, DecisionRecord* decision, DeliveryRecord* ball) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    if (pending_) return Status::Err("a decision is already pending");
    if (Current().State() != InningsState::InPlay) return Status::Err("no ball can be bowled right now");
    if (Status s = ValidateCase(c); !s) return s;
    pending_ = c;
    // Without a TV umpire nobody checks the front line: a missed no-ball
    // stays missed (the on-field umpire calls no-balls through Delivery::extra).
    if (!rules_.drs.thirdUmpire) pending_->evidence.frontFootNoBall = false;
    if (c.kind == DecisionKind::UmpireReferral) {
        // No TV umpire: the on-field umpire's own call (onFieldOut) is final.
        if (!rules_.drs.thirdUmpire) return ApplyDecision(JudgeWithoutReview(*pending_), false, -1, decision, ball);
        return ApplyDecision(JudgeReferral(*pending_), false, -1, decision, ball);
    }
    // No player reviews in this competition: the on-field call is final.
    if (!rules_.drs.playerReviews) return ApplyDecision(JudgeWithoutReview(*pending_), false, -1, decision, ball);
    if (decision) *decision = {};
    return Status::Ok();
}

Status Match::ReviewDecision(int side, DecisionRecord* decision, DeliveryRecord* ball) {
    if (!pending_) return Status::Err("there is no decision to review");
    const int entitled = ReviewEntitledSide();
    if (side != entitled)
        return Status::Err(std::string("only the ") + (pending_->onFieldOut ? "batting" : "fielding") +
                           " side can review this decision");
    if (!rules_.drs.playerReviews) return Status::Err("player reviews are not used in this competition");
    if (reviewsLeft_[side] <= 0) return Status::Err("no reviews left this innings");
    DrsOutcome o = JudgeReview(*pending_);
    if (o.outcome == ReviewOutcome::UmpiresCall && !rules_.drs.umpiresCallRetainsReview) o.reviewLost = true;
    if (o.reviewLost) reviewsLeft_[side]--;
    return ApplyDecision(o, true, side, decision, ball);
}

Status Match::AcceptDecision(DecisionRecord* decision, DeliveryRecord* ball) {
    if (!pending_) return Status::Err("there is no decision to accept");
    return ApplyDecision(JudgeWithoutReview(*pending_), false, -1, decision, ball);
}

Status Match::ApplyDecision(const DrsOutcome& o, bool reviewed, int side, DecisionRecord* decision,
                            DeliveryRecord* ball) {
    const DecisionCase c = *pending_;
    DecisionRecord rec;
    rec.inningsIndex = Current().Index();
    rec.over = Current().CompletedOvers();
    rec.ballInOver = Current().BallsInOver();
    rec.kind = c.kind;
    rec.appealedFor = c.mode;
    rec.onFieldOut = c.onFieldOut;
    rec.reviewed = reviewed;
    rec.reviewingSide = side;
    rec.result = o;
    rec.reviewsLeftAfter = side >= 0 ? reviewsLeft_[side] : -1;

    const Delivery d = FinalDelivery(c, o);
    // Validated when the appeal was made, so this cannot fail; keep the
    // pending decision if it somehow does, rather than losing the ball.
    if (Status s = Current().Check(d); !s) return Status::Err("internal: final delivery rejected: " + s.error);
    pending_.reset();
    decisions_.push_back(rec);
    if (decision) *decision = rec;
    return BowlInternal(d, ball);
}

}  // namespace cricket
