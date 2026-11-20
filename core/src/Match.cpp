#include "cricket/Match.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace cricket {

const char* ToString(IntervalKind k) {
    switch (k) {
        case IntervalKind::Lunch: return "lunch";
        case IntervalKind::Tea: return "tea";
        case IntervalKind::Dinner: return "dinner";
        case IntervalKind::Stumps: return "stumps";
        case IntervalKind::InningsBreak: return "innings break";
    }
    return "?";
}

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

Status Match::ValidateRules(const MatchRules& r) {
    if (r.ballsPerOver < 1 || r.ballsPerOver > 10) return Status::Err("balls per over must be 1-10");
    if (r.oversPerInnings < 0 || r.oversPerInnings > 1000) return Status::Err("overs per innings out of range");
    if (r.inningsPerSide != 1 && r.inningsPerSide != 2) return Status::Err("innings per side must be 1 or 2");
    if (r.squadSize < 2 || r.squadSize > 15) return Status::Err("squad size out of range");
    if (r.maxOversPerBowler < 0 || r.followOnLead < 0 || r.matchOverLimit < 0 || r.newBallAfterOvers < 0)
        return Status::Err("negative playing-condition value");
    if (r.widePenalty < 0 || r.noBallPenalty < 0) return Status::Err("negative penalty");
    if (r.oversPerInnings == 0 && r.schedule.enabled == false && r.matchOverLimit == 0 && r.inningsPerSide == 1)
        return Status::Err("an unlimited-overs innings needs a schedule or an over limit");
    if (!(r.weather.minutesPerOver > 0)) return Status::Err("minutes per over must be positive");
    return Status::Ok();
}

Match::Match(MatchRules rules, std::array<Team, 2> teams, int battingFirst)
    : rules_(std::move(rules)), teams_(std::move(teams)), battingFirst_(battingFirst) {
    if (battingFirst != 0 && battingFirst != 1) throw std::invalid_argument("battingFirst must be 0 or 1");
    if (Status s = ValidateRules(rules_); !s) throw std::invalid_argument(s.error);
    if (rules_.drs.reviewsPerInnings < 0 || rules_.drs.reviewTimerSeconds < 0)
        throw std::invalid_argument("DRS settings cannot be negative");
    for (const auto& t : teams_) {
        Status s = ValidateTeam(t, rules_.squadSize);
        if (!s) throw std::invalid_argument(s.error);
    }
    plannedOvers_ = {rules_.oversPerInnings, rules_.oversPerInnings};
    if (rules_.schedule.enabled) {
        const auto& sc = rules_.schedule;
        if (sc.days < 1 || sc.oversPerDay < 1 || sc.sessionsPerDay < 1 || sc.maxOversPerDay < sc.oversPerDay ||
            sc.oversLostPerInningsChange < 0 || sc.intervalNearOvers < 0)
            throw std::invalid_argument("invalid session schedule");
        StartDay(1);
    }
    StartInnings(battingFirst_, std::nullopt);
}

// ---------------------------------------------------------------------------
// Test match schedule (overs-based; see docs/TEST_SESSIONS.md)

void Match::StartDay(int day) {
    const auto& sc = rules_.schedule;
    clock_.day = day;
    clock_.session = 1;
    clock_.ballsToday = 0;
    clock_.ballsLostToday = 0;
    // Lost time from earlier days is made up, up to the daily maximum.
    const int extra = std::min(clock_.makeUpBalls, (sc.maxOversPerDay - sc.oversPerDay) * rules_.ballsPerOver);
    clock_.makeUpBalls -= extra;
    clock_.ballsAvailableToday = sc.oversPerDay * rules_.ballsPerOver + extra;
}

int Match::SessionEndBalls(int session) const {
    // Sessions split the day (including time lost to weather) evenly,
    // ending on an over. Compared against elapsed time = played + lost.
    const int bpo = rules_.ballsPerOver;
    const int overs = (clock_.ballsAvailableToday + clock_.ballsLostToday) / bpo;
    return overs * session / rules_.schedule.sessionsPerDay * bpo;
}

int Match::BallsLeftInSession() const {
    if (!rules_.schedule.enabled) return -1;
    const int elapsed = clock_.ballsToday + clock_.ballsLostToday;
    if (clock_.session >= rules_.schedule.sessionsPerDay) return std::max(0, BallsLeftToday());
    return std::max(0, std::min(SessionEndBalls(clock_.session) - elapsed, BallsLeftToday()));
}

int Match::FollowOnThreshold() const {
    if (!rules_.schedule.enabled) return rules_.followOnLead;
    if (rules_.followOnLead <= 0) return 0;
    const int started = clock_.startDay > 0 ? clock_.startDay : clock_.day;
    return MatchRules::FollowOnLeadForDays(rules_.schedule.days - started + 1);
}

void Match::RecordInterval(IntervalKind k, bool early) {
    IntervalRecord r;
    r.kind = k;
    r.day = clock_.day;
    r.session = clock_.session;
    const Innings& inn = Current();
    r.inningsIndex = inn.Index();
    r.battingSide = inn.BattingSide();
    r.total = inn.Total();
    r.wickets = inn.Wickets();
    r.overs = inn.CompletedOvers();
    r.balls = inn.BallsInOver();
    r.early = early;
    intervals_.push_back(r);
}

void Match::CheckSchedule(bool inningsJustChanged, bool force) {
    if (!rules_.schedule.enabled || phase_ == MatchPhase::Complete) return;
    const auto& sc = rules_.schedule;
    const int bpo = rules_.ballsPerOver;
    // Breaks fall between overs; weather can stop play mid-over (force).
    if (!force && Current().BallsInOver() != 0 && !Current().IsComplete()) return;
    const int elapsed = clock_.ballsToday + clock_.ballsLostToday;
    // Several breaks can pass during a long rain delay (lunch AND tea).
    while (clock_.session < sc.sessionsPerDay) {
        const int left = SessionEndBalls(clock_.session) - elapsed;
        const bool near = inningsJustChanged && left <= sc.intervalNearOvers * bpo + bpo / 2;
        if (left > 0 && !near) break;
        IntervalKind k;
        if (sc.dayNight) k = clock_.session == 1 ? IntervalKind::Tea : IntervalKind::Dinner;
        else k = clock_.session == 1 ? IntervalKind::Lunch : IntervalKind::Tea;
        RecordInterval(k, left > 0);
        clock_.session++;
        inningsJustChanged = false;
    }
    if (clock_.ballsToday >= clock_.ballsAvailableToday) {
        clock_.session = sc.sessionsPerDay;
        RecordInterval(IntervalKind::Stumps, false);
        if (clock_.day >= sc.days) {
            CallDraw();  // out of time on the last day
            return;
        }
        StartDay(clock_.day + 1);
    }
}

void Match::StartInnings(int side, std::optional<int> target) {
    const bool changeover = !innings_.empty();
    if (changeover) {
        RecordInterval(IntervalKind::InningsBreak, false);
        if (rules_.schedule.enabled) {
            // ICC Test playing conditions: 2 overs off the day per change of innings.
            const int cut = rules_.schedule.oversLostPerInningsChange * rules_.ballsPerOver;
            clock_.ballsAvailableToday = std::max(clock_.ballsToday, clock_.ballsAvailableToday - cut);
        }
    }
    const int idx = static_cast<int>(innings_.size());
    MatchRules r = rules_;
    if (LimitedOvers() && idx < 2) {
        if (plannedOvers_[idx] != rules_.oversPerInnings) r = rules_.Scaled(plannedOvers_[idx]);
        resources_[idx] = RainResources(plannedOvers_[idx], 0);
    }
    innings_.emplace_back(r, side, idx, target);
    phase_ = MatchPhase::InProgress;
    if (pendingPenalty_[side] > 0) {  // Law 41.17: awarded before this side batted
        innings_.back().AddPenaltyRuns(pendingPenalty_[side]);
        for (auto& p : penalties_)
            if (p.side == side && p.inningsIndex < 0) p.inningsIndex = innings_.back().Index();
        pendingPenalty_[side] = 0;
    }
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

Status Match::SetOpenersImpl(int s, int n) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    return Current().SetOpeners(s, n);
}

Status Match::SetBowlerImpl(int b) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    return Current().SetBowler(b);
}

Status Match::SetNextBatterImpl(int b) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    return Current().SetNextBatter(b);
}

Status Match::BowlImpl(const Delivery& d, DeliveryRecord* out) {
    if (pending_) return Status::Err("a decision is pending: review it or accept it first");
    return BowlInternal(d, out);
}

Status Match::BowlInternal(const Delivery& d, DeliveryRecord* out) {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    DeliveryRecord rec;
    Status s = Current().Bowl(d, &rec);
    if (!s) return s;
    if (out) *out = rec;
    if (rec.legal) {
        clock_.ballsToday++;
        if (clock_.startDay == 0) clock_.startDay = clock_.day;
    } else if (clock_.startDay == 0) {
        clock_.startDay = clock_.day;
    }
    const size_t before = innings_.size();
    if (Current().IsComplete()) {
        Advance();
    } else if (rules_.matchOverLimit > 0 &&
               TotalLegalBallsInMatch() >= rules_.matchOverLimit * rules_.ballsPerOver) {
        CallDraw();  // out of time
    }
    if (rec.overCompleted || rec.inningsCompleted) CheckSchedule(innings_.size() != before);
    return Status::Ok();
}

Status Match::DeclareImpl() {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    if (pending_) return Status::Err("a decision is pending");
    const size_t before = innings_.size();
    Status s = Current().Declare();
    if (s) {
        Advance();
        CheckSchedule(innings_.size() != before);
    }
    return s;
}

Status Match::DecideFollowOnImpl(bool enforce) {
    if (phase_ != MatchPhase::AwaitingFollowOnDecision) return Status::Err("no follow-on decision is pending");
    followOn_ = enforce;
    StartInnings(enforce ? 1 - battingFirst_ : battingFirst_, std::nullopt);
    CheckSchedule(true);
    return Status::Ok();
}

void Match::CallDrawImpl() {
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

void Match::NoResult(const std::string& why) {
    pending_.reset();
    Current().ForceComplete();
    phase_ = MatchPhase::Complete;
    result_ = {};
    result_.kind = MatchResult::Kind::NoResult;
    result_.summary = why;
}

void Match::AbandonImpl() {
    if (phase_ == MatchPhase::Complete) return;
    pending_.reset();
    if (!LimitedOvers()) {
        // Test: once play has started an abandoned match is drawn.
        if (clock_.startDay > 0 || Current().LegalBalls() > 0) {
            CallDraw();
            result_.summary = "Match drawn (play abandoned)";
        } else {
            NoResult("Match abandoned without a ball bowled");
        }
        return;
    }
    // Limited overs: a result is possible if the side batting second has
    // faced the minimum overs; it is decided on the rain-rule par score.
    Innings& cur = Current();
    const int minBalls = rules_.weather.minOversForResult * rules_.ballsPerOver;
    if (cur.Index() == 1 && cur.LegalBalls() >= minBalls && cur.Target()) {
        const double left = static_cast<double>(cur.MaxOvers() * rules_.ballsPerOver - cur.LegalBalls()) /
                            rules_.ballsPerOver;
        resources_[1] -= RainResources(left, cur.Wickets());
        rainAffected_ = true;
        const int target = RainTarget(innings_[0].Total(), resources_[0], resources_[1], rules_.weather.fullInningsScore);
        cur.ForceComplete();
        // Judge the chase as if its innings had ended here.
        phase_ = MatchPhase::Complete;
        const int chasing = cur.BattingSide(), defending = 1 - chasing;
        if (cur.Total() >= target) {
            int leftW = rules_.squadSize - 1 - cur.Wickets();
            result_ = {MatchResult::Kind::Win, MatchResult::Margin::Wickets, chasing, leftW,
                       Name(chasing) + " won by " + std::to_string(leftW) + (leftW == 1 ? " wicket" : " wickets") +
                           " (rain rule)"};
        } else if (cur.Total() == target - 1) {
            result_ = {MatchResult::Kind::Tie, MatchResult::Margin::None, -1, 0, "Match tied (rain rule)"};
        } else {
            int by = target - 1 - cur.Total();
            result_ = {MatchResult::Kind::Win, MatchResult::Margin::Runs, defending, by,
                       Name(defending) + " won by " + std::to_string(by) + (by == 1 ? " run" : " runs") + " (rain rule)"};
        }
        return;
    }
    NoResult("No result - match abandoned before " + std::to_string(rules_.weather.minOversForResult) +
             " overs were possible for the side batting second");
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
            RecomputeTarget();
            if (Current().IsComplete()) Advance();  // revised target already reached (e.g. penalty runs)
        } else {
            FinishWithChase(innings_[1], *innings_[1].Target());
            if (rainAffected_ && result_.kind != MatchResult::Kind::NoResult) result_.summary += " (rain rule)";
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
            const int threshold = FollowOnThreshold();
            if (threshold > 0 && lead >= threshold) phase_ = MatchPhase::AwaitingFollowOnDecision;
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
// Weather (see docs/RAIN_RULE.md)

double Match::ParScoreNow() const {
    if (!LimitedOvers() || innings_.size() != 2 || phase_ == MatchPhase::Complete) return -1;
    const Innings& cur = innings_[1];
    const double left =
        static_cast<double>(cur.MaxOvers() * rules_.ballsPerOver - cur.LegalBalls()) / rules_.ballsPerOver;
    const double used = resources_[1] - RainResources(left, cur.Wickets());
    return RainPar(innings_[0].Total(), resources_[0], used, rules_.weather.fullInningsScore);
}

Status Match::CutInnings(int idx, int overs) {
    Innings& inn = innings_[idx];
    const int bpo = rules_.ballsPerOver;
    const double before = static_cast<double>(inn.MaxOvers() * bpo - inn.LegalBalls()) / bpo;
    const double after = static_cast<double>(overs * bpo - inn.LegalBalls()) / bpo;
    if (Status s = inn.SetMaxOvers(overs); !s) return s;
    resources_[idx] -= RainResources(before, inn.Wickets()) - RainResources(std::max(0.0, after), inn.Wickets());
    plannedOvers_[idx] = overs;
    return Status::Ok();
}

Status Match::ReduceOversImpl(int oversThis, int oversSecond) {
    if (!LimitedOvers()) return Status::Err("overs are only reduced in limited-overs cricket; Test time comes off the day");
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    if (pending_) return Status::Err("a decision is pending");
    const int idx = Current().Index();
    if (idx > 1) return Status::Err("no innings to shorten");
    if (oversThis < 1 || oversThis > plannedOvers_[idx]) return Status::Err("overs can only be reduced");
    if (idx == 0) {
        const int second = oversSecond > 0 ? oversSecond : oversThis;
        if (second > plannedOvers_[1]) return Status::Err("overs can only be reduced");
        if (Status s = CutInnings(0, oversThis); !s) return s;
        plannedOvers_[1] = second;
    } else {
        if (Status s = CutInnings(1, oversThis); !s) return s;
    }
    rainAffected_ = true;
    if (plannedOvers_[1] < rules_.weather.minOversForResult) {
        NoResult("No result - fewer than " + std::to_string(rules_.weather.minOversForResult) +
                 " overs possible for the side batting second");
        return Status::Ok();
    }
    // Revise the chase target first (if innings 2 is under way), then let
    // the match move on if an innings has just ended.
    RecomputeTarget();
    if (Current().IsComplete()) Advance();
    return Status::Ok();
}

Status Match::RainDelayImpl(int minutes) {
    if (minutes <= 0) return Status::Err("minutes lost must be positive");
    if (phase_ == MatchPhase::Complete) return Status::Err("match is over");
    if (pending_) return Status::Err("a decision is pending");
    const int bpo = rules_.ballsPerOver;
    RainRecord rec;
    rec.inningsIndex = Current().Index();
    rec.minutes = minutes;
    if (!LimitedOvers()) {
        if (!rules_.schedule.enabled) return Status::Err("no schedule to lose time from");
        const int overs = static_cast<int>(std::ceil(minutes / rules_.weather.minutesPerOver - 1e-9));
        const int take = std::min(overs * bpo, std::max(0, clock_.ballsAvailableToday - clock_.ballsToday));
        clock_.ballsAvailableToday -= take;
        clock_.ballsLostToday += take;
        clock_.makeUpBalls += take;
        rec.day = clock_.day;
        rec.oversLost = (take + bpo - 1) / bpo;
        rainLog_.push_back(rec);
        rainAffected_ = true;
        CheckSchedule(false, true);
        return Status::Ok();
    }
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    const int absorb = std::min(minutes, std::max(0, rules_.weather.bufferMinutes - bufferUsed_));
    bufferUsed_ += absorb;
    rec.bufferUsed = absorb;
    const int net = minutes - absorb;
    const int idx = Current().Index();
    const int started = Current().CompletedOvers() + (Current().BallsInOver() > 0 ? 1 : 0);
    if (net <= 0) {
        rec.newOversThis = plannedOvers_[idx];
        rec.newOversSecond = plannedOvers_[1];
        rainLog_.push_back(rec);
        return Status::Ok();
    }
    const int lost = static_cast<int>(std::ceil(net / rules_.weather.minutesPerOver - 1e-9));
    rec.oversLost = lost;
    Status s;
    if (idx == 0) {
        // Equal overs for both sides where possible; if the first innings has
        // already gone past that, it ends now and the second innings absorbs
        // the rest of the lost time.
        const int equal = std::max(1, plannedOvers_[0] - (lost + 1) / 2);
        const int n1 = std::max({equal, started, 1});
        const int n2 = std::max(0, plannedOvers_[1] - (lost - (plannedOvers_[0] - n1)));
        rec.newOversThis = n1;
        rec.newOversSecond = n2;
        if (n2 < 1) {
            rainLog_.push_back(rec);
            NoResult("No result - no time left for the side batting second");
            return Status::Ok();
        }
        s = ReduceOvers(n1, std::min(n2, plannedOvers_[1]));
    } else {
        const int n2 = std::max({plannedOvers_[1] - lost, started, 1});
        rec.newOversThis = n2;
        rec.newOversSecond = n2;
        s = ReduceOvers(n2);
    }
    if (s) rainLog_.push_back(rec);
    return s;
}

Status Match::LoseRestOfDayImpl() {
    if (!rules_.schedule.enabled) return Status::Err("only scheduled (Test) matches have days");
    if (phase_ == MatchPhase::Complete) return Status::Err("match is over");
    const int left = std::max(0, clock_.ballsAvailableToday - clock_.ballsToday);
    if (left == 0) return Status::Err("no play left today");
    const double minutes = static_cast<double>(left) / rules_.ballsPerOver * rules_.weather.minutesPerOver;
    return RainDelay(static_cast<int>(std::ceil(minutes)));
}

// ---------------------------------------------------------------------------
// Decisions and DRS

int Match::ReviewEntitledSide() const {
    if (!pending_) return -1;
    // The side the on-field call went against may review it. A wide or
    // no-ball call helps the batting side, so the fielders may challenge it.
    if (pending_->call != ScoringCall::None) return pending_->onFieldCalled ? FieldingSide() : BattingSide();
    return pending_->onFieldOut ? BattingSide() : FieldingSide();
}

Status Match::AwardPenaltyRunsImpl(int side, int runs, const std::string& reason) {
    if (phase_ == MatchPhase::Complete) return Status::Err("match is over");
    if (pending_) return Status::Err("a decision is pending");
    if (side != 0 && side != 1) return Status::Err("side must be 0 or 1");
    if (runs <= 0 || runs > 20) return Status::Err("penalty runs must be between 1 and 20");
    PenaltyRecord rec{side, runs, -1, reason};
    if (side == Current().BattingSide()) {
        Current().AddPenaltyRuns(runs);
        rec.inningsIndex = Current().Index();
    } else {
        int last = -1;
        for (const auto& i : innings_)
            if (i.BattingSide() == side) last = i.Index();
        if (last >= 0) {
            innings_[last].AddPenaltyRuns(runs);
            rec.inningsIndex = last;
            RecomputeTarget();
        } else {
            pendingPenalty_[side] += runs;
        }
    }
    penalties_.push_back(rec);
    if (phase_ == MatchPhase::InProgress && Current().IsComplete()) {
        const size_t before = innings_.size();
        Advance();
        CheckSchedule(innings_.size() != before);
    }
    return Status::Ok();
}

void Match::RecomputeTarget() {
    Innings& cur = Current();
    if (!cur.Target()) return;
    // A rain-shortened chase may have just ended: its target must still be
    // revised before the result is decided.
    if (LimitedOvers() && rainAffected_ && cur.Index() == 1) {
        cur.SetTarget(RainTarget(innings_[0].Total(), resources_[0], resources_[1], rules_.weather.fullInningsScore));
        return;
    }
    if (cur.IsComplete()) return;
    const int chasing = cur.BattingSide();
    int theirs = 0, ours = 0;
    for (const auto& i : innings_) {
        if (i.Index() == cur.Index()) continue;
        (i.BattingSide() == chasing ? ours : theirs) += i.Total();
    }
    cur.SetTarget(theirs - ours + 1);
}

Status Match::TakeNewBallImpl() {
    if (phase_ != MatchPhase::InProgress) return NotInProgress(phase_);
    if (pending_) return Status::Err("a decision is pending");
    return Current().TakeNewBall();
}

Status Match::ValidateCase(const DecisionCase& c) const {
    const Innings& inn = Current();
    if (c.call != ScoringCall::None) {
        if (c.kind == DecisionKind::PlayerAppeal && !IsPlayerReviewable(c.call))
            return Status::Err(std::string("players cannot review a ") + ToString(c.call) + " call");
        if (c.kind == DecisionKind::UmpireReferral && !IsUmpireReferable(c.call))
            return Status::Err(std::string("umpires do not refer a ") + ToString(c.call) + " call");
        if (c.ifCalled.dismissal != Dismissal::None || c.ifNotCalled.dismissal != Dismissal::None)
            return Status::Err("a scoring-call review cannot include a dismissal");
        if (c.call == ScoringCall::Wide && c.ifCalled.extra != ExtraType::Wide)
            return Status::Err("ifCalled must be a wide");
        if (c.call == ScoringCall::HeightNoBall && c.ifCalled.extra != ExtraType::NoBall)
            return Status::Err("ifCalled must be a no-ball");
        if (Status s = inn.Check(c.ifCalled); !s) return Status::Err("ifCalled: " + s.error);
        if (Status s = inn.Check(c.ifNotCalled); !s) return Status::Err("ifNotCalled: " + s.error);
        return Status::Ok();
    }
    if (c.mode == Dismissal::None) return Status::Err("appeal needs a mode of dismissal");
    if (c.kind == DecisionKind::PlayerAppeal && !IsPlayerReviewable(c.mode))
        return Status::Err(std::string("players review caught, LBW or hit wicket; ") + ToString(c.mode) +
                           " is referred by the umpires");
    if (c.kind == DecisionKind::UmpireReferral && !IsUmpireReferable(c.mode))
        return Status::Err(std::string("umpires do not refer ") + ToString(c.mode));
    if (c.mode == Dismissal::Lbw && !c.evidence.lbw) return Status::Err("an LBW appeal needs ball-tracking evidence");
    if (c.ifOut.dismissal != c.mode) return Status::Err("ifOut must record the appealed mode of dismissal");
    if (c.ifNotOut.dismissal != Dismissal::None) return Status::Err("ifNotOut must not contain a dismissal");
    if (Status s = inn.Check(c.ifOut); !s) return Status::Err("ifOut: " + s.error);
    if (Status s = inn.Check(c.ifNotOut); !s) return Status::Err("ifNotOut: " + s.error);
    if (c.mode == Dismissal::Caught && c.evidence.lbw) {
        Delivery lbw = c.ifOut;
        lbw.dismissal = Dismissal::Lbw;
        if (Status s = inn.Check(lbw); !s) return Status::Err("LBW check: " + s.error);
    }
    return Status::Ok();
}

Status Match::AppealImpl(const DecisionCase& c, DecisionRecord* decision, DeliveryRecord* ball) {
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
    // No player reviews in this competition (or not for this kind of call):
    // the on-field call is final.
    const bool reviewable = rules_.drs.playerReviews &&
                            (c.call == ScoringCall::None || rules_.drs.reviewWidesAndNoBalls);
    if (!reviewable) return ApplyDecision(JudgeWithoutReview(*pending_), false, -1, decision, ball);
    if (decision) *decision = {};
    return Status::Ok();
}

Status Match::ReviewDecisionImpl(int side, DecisionRecord* decision, DeliveryRecord* ball) {
    if (!pending_) return Status::Err("there is no decision to review");
    const int entitled = ReviewEntitledSide();
    if (side != entitled)
        return Status::Err(std::string("only the ") + (entitled == BattingSide() ? "batting" : "fielding") +
                           " side can review this decision");
    if (!rules_.drs.playerReviews) return Status::Err("player reviews are not used in this competition");
    if (reviewsLeft_[side] <= 0) return Status::Err("no reviews left this innings");
    DrsOutcome o = JudgeReview(*pending_);
    if (o.outcome == ReviewOutcome::UmpiresCall && !rules_.drs.umpiresCallRetainsReview) o.reviewLost = true;
    if (o.reviewLost) reviewsLeft_[side]--;
    return ApplyDecision(o, true, side, decision, ball);
}

Status Match::AcceptDecisionImpl(DecisionRecord* decision, DeliveryRecord* ball) {
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
    rec.call = c.call;
    rec.onFieldCalled = c.onFieldCalled;
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

// ---------------------------------------------------------------------------
// Public actions: each successful outermost call is appended to the journal,
// which is what a save file replays (see SaveGame.h).

template <class F>
Status Match::Record(MatchAction a, F&& f) {
    const bool outer = apiDepth_ == 0;
    ++apiDepth_;
    Status s = f();
    --apiDepth_;
    if (outer && s) journal_.push_back(std::move(a));
    return s;
}

namespace {
MatchAction Act(ActionType t, int a = 0, int b = 0) {
    MatchAction x;
    x.type = t;
    x.a = a;
    x.b = b;
    return x;
}
}  // namespace

Status Match::SetOpeners(int s, int n) {
    return Record(Act(ActionType::SetOpeners, s, n), [&] { return SetOpenersImpl(s, n); });
}
Status Match::SetBowler(int b) {
    return Record(Act(ActionType::SetBowler, b), [&] { return SetBowlerImpl(b); });
}
Status Match::SetNextBatter(int b) {
    return Record(Act(ActionType::SetNextBatter, b), [&] { return SetNextBatterImpl(b); });
}
Status Match::Bowl(const Delivery& d, DeliveryRecord* out) {
    MatchAction a = Act(ActionType::Bowl);
    a.delivery = d;
    return Record(std::move(a), [&] { return BowlImpl(d, out); });
}
Status Match::Declare() {
    return Record(Act(ActionType::Declare), [&] { return DeclareImpl(); });
}
Status Match::Appeal(const DecisionCase& c, DecisionRecord* decision, DeliveryRecord* ball) {
    MatchAction a = Act(ActionType::Appeal);
    a.decision = c;
    return Record(std::move(a), [&] { return AppealImpl(c, decision, ball); });
}
Status Match::ReviewDecision(int side, DecisionRecord* decision, DeliveryRecord* ball) {
    return Record(Act(ActionType::Review, side), [&] { return ReviewDecisionImpl(side, decision, ball); });
}
Status Match::AcceptDecision(DecisionRecord* decision, DeliveryRecord* ball) {
    return Record(Act(ActionType::Accept), [&] { return AcceptDecisionImpl(decision, ball); });
}
Status Match::AwardPenaltyRuns(int side, int runs, const std::string& reason) {
    MatchAction a = Act(ActionType::Penalty, side, runs);
    a.text = reason;
    return Record(std::move(a), [&] { return AwardPenaltyRunsImpl(side, runs, reason); });
}
Status Match::TakeNewBall() {
    return Record(Act(ActionType::NewBall), [&] { return TakeNewBallImpl(); });
}
Status Match::DecideFollowOn(bool enforce) {
    return Record(Act(ActionType::FollowOn, enforce ? 1 : 0), [&] { return DecideFollowOnImpl(enforce); });
}
Status Match::RainDelay(int minutes) {
    return Record(Act(ActionType::Rain, minutes), [&] { return RainDelayImpl(minutes); });
}
Status Match::ReduceOvers(int oversThis, int oversSecond) {
    return Record(Act(ActionType::ReduceOvers, oversThis, oversSecond),
                  [&] { return ReduceOversImpl(oversThis, oversSecond); });
}
Status Match::LoseRestOfDay() {
    return Record(Act(ActionType::LoseDay), [&] { return LoseRestOfDayImpl(); });
}
void Match::CallDraw() {
    Record(Act(ActionType::CallDraw), [&] {
        if (phase_ == MatchPhase::Complete) return Status::Err("match is over");
        CallDrawImpl();
        return Status::Ok();
    });
}
void Match::Abandon() {
    Record(Act(ActionType::Abandon), [&] {
        if (phase_ == MatchPhase::Complete) return Status::Err("match is over");
        AbandonImpl();
        return Status::Ok();
    });
}

Status Match::Apply(const MatchAction& a) {
    switch (a.type) {
        case ActionType::SetOpeners: return SetOpeners(a.a, a.b);
        case ActionType::SetBowler: return SetBowler(a.a);
        case ActionType::SetNextBatter: return SetNextBatter(a.a);
        case ActionType::Bowl: return Bowl(a.delivery);
        case ActionType::Declare: return Declare();
        case ActionType::Appeal: return Appeal(a.decision);
        case ActionType::Review: return ReviewDecision(a.a);
        case ActionType::Accept: return AcceptDecision();
        case ActionType::FollowOn: return DecideFollowOn(a.a != 0);
        case ActionType::Penalty: return AwardPenaltyRuns(a.a, a.b, a.text);
        case ActionType::NewBall: return TakeNewBall();
        case ActionType::Rain: return RainDelay(a.a);
        case ActionType::ReduceOvers: return ReduceOvers(a.a, a.b);
        case ActionType::LoseDay: return LoseRestOfDay();
        case ActionType::CallDraw:
            if (phase_ == MatchPhase::Complete) return Status::Err("match is over");
            CallDraw();
            return Status::Ok();
        case ActionType::Abandon:
            if (phase_ == MatchPhase::Complete) return Status::Err("match is over");
            Abandon();
            return Status::Ok();
    }
    return Status::Err("unknown action");
}

std::string Match::StateFingerprint() const {
    // Everything a player could see, in a canonical text form. Two matches
    // with the same fingerprint are indistinguishable to the game.
    std::string f;
    char buf[160];
    auto add = [&](const char* fmt, auto... v) {
        std::snprintf(buf, sizeof buf, fmt, v...);
        f += buf;
    };
    add("P%d R%d W%d V%d %s|", static_cast<int>(phase_), static_cast<int>(result_.kind), result_.winner, result_.value,
        result_.summary.c_str());
    for (const auto& i : innings_) {
        const Extras& e = i.GetExtras();
        add("I%d s%d t%d w%d b%d x%d,%d,%d,%d,%d T%d E%d st%d ns%d bw%d lb%d fh%d mo%d nb%d|", i.Index(), i.BattingSide(),
            i.Total(), i.Wickets(), i.LegalBalls(), e.wides, e.noBalls, e.byes, e.legByes, e.penalty,
            i.Target() ? *i.Target() : -1, static_cast<int>(i.EndReason()), i.Striker(), i.NonStriker(),
            i.CurrentBowler(), i.LastOverBowler(), i.NextBallIsFreeHit() ? 1 : 0, i.MaxOvers(),
            i.OversWithCurrentBall());
        for (int p = 0; p < rules_.squadSize; ++p) {
            const BatterCard& bc = i.Batter(p);
            const BowlerCard& bo = i.Bowler(p);
            add("%d.%d.%d.%d.%d.%d;%d.%d.%d.%d ", bc.runs, bc.balls, bc.out ? 1 : 0, static_cast<int>(bc.how), bc.bowler,
                bc.batted ? 1 : 0, bo.legalBalls, bo.runs, bo.wickets, bo.maidens);
        }
        f += "|";
    }
    add("C%d.%d.%d.%d.%d.%d.%d|", clock_.day, clock_.session, clock_.ballsToday, clock_.ballsAvailableToday,
        clock_.makeUpBalls, clock_.startDay, clock_.ballsLostToday);
    add("D%zu.%zu.%zu.%zu R%d.%d P%d F%d|", decisions_.size(), penalties_.size(), intervals_.size(), rainLog_.size(),
        reviewsLeft_[0], reviewsLeft_[1], pending_ ? 1 : 0, followOn_ ? 1 : 0);
    add("Q%.6f.%.6f.%d.%d.%d|", resources_[0], resources_[1], plannedOvers_[0], plannedOvers_[1], bufferUsed_);
    return f;
}

}  // namespace cricket
