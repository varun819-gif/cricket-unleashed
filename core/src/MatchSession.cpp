#include "cricket/MatchSession.h"

#include <algorithm>

namespace cricket {

namespace {
std::string Face(CoinFace f) { return f == CoinFace::Heads ? "heads" : "tails"; }

std::string BallText(const Match& m, const DeliveryRecord& r) {
    const Innings& inn = m.AllInnings()[r.inningsIndex];
    const Team& bat = m.GetTeam(inn.BattingSide());
    const Team& fld = m.GetTeam(1 - inn.BattingSide());
    std::string s = std::to_string(r.over) + "." + std::to_string(r.ballInOver) + " " + fld.xi[r.bowler].name +
                    " to " + bat.xi[r.striker].name + ": ";
    const Delivery& d = r.delivery;
    if (d.extra != ExtraType::None) s += std::string(ToString(d.extra)) + " ";
    if (d.noBallRunsAs != ExtraType::None && d.runs > 0) s += std::string("+ ") + ToString(d.noBallRunsAs) + "s ";
    if (d.shortRuns > 0) s += "(" + std::to_string(d.shortRuns) + " short) ";
    if (d.dismissal != Dismissal::None) s += std::string("OUT ") + ToString(d.dismissal) + " (" + bat.xi[r.dismissedBatter].name + ") ";
    if (d.boundary) s += d.runs == 6 ? "SIX" : "FOUR";
    else s += std::to_string(r.runsToTeam) + (r.runsToTeam == 1 ? " run" : " runs");
    s += "  [" + std::to_string(r.totalAfter) + "/" + std::to_string(r.wicketsAfter) + "]";
    return s;
}
}  // namespace

MatchSession::MatchSession(MatchRules rules, std::array<Team, 2> teams, uint64_t serverSeed)
    : rules_(std::move(rules)), teams_(std::move(teams)), toss_(serverSeed), ai_(serverSeed ^ 0x9e3779b97f4a7c15ULL) {
    for (const auto& t : teams_) {
        Status s = Match::ValidateTeam(t, rules_.squadSize);
        if (!s) throw std::invalid_argument(s.error);
    }
    fields_[0] = fields_[1] = FieldSetup::FromPreset(FieldPreset::Powerplay);
}

bool MatchSession::IsHuman(int side) const { return side >= 0 && side < 2 && !humans_[side].empty(); }

int MatchSession::SideOf(const std::string& player) const {
    for (int s = 0; s < 2; ++s)
        if (!humans_[s].empty() && humans_[s] == player) return s;
    return -1;
}

Status MatchSession::Join(const std::string& player, int side) {
    if (player.empty()) return Status::Err("player id required");
    if (side != 0 && side != 1) return Status::Err("side must be 0 or 1");
    if (SideOf(player) >= 0) return Status::Err("player already joined");
    if (!humans_[side].empty()) return Status::Err("that side is already taken");
    if (toss_.Phase() != TossPhase::AwaitingCall) return Status::Err("match already started");
    humans_[side] = player;
    Log("join", player + " controls " + teams_[side].name);
    return Status::Ok();
}

void MatchSession::Log(std::string kind, std::string text, std::optional<DeliveryRecord> ball) {
    SessionEvent e;
    e.id = events_.size() + 1;
    e.kind = std::move(kind);
    e.text = std::move(text);
    e.ball = std::move(ball);
    events_.push_back(std::move(e));
}

std::vector<SessionEvent> MatchSession::EventsSince(uint64_t lastSeenId) const {
    if (lastSeenId >= events_.size()) return {};
    return {events_.begin() + static_cast<long>(lastSeenId), events_.end()};
}

Pending MatchSession::WaitingFor() const {
    if (toss_.Phase() == TossPhase::AwaitingCall) return {"toss-call", toss_.CallingSide()};
    if (toss_.Phase() == TossPhase::AwaitingDecision) return {"toss-decision", toss_.Winner()};
    if (!match_) return {"none", -1};
    const Match& m = *match_;
    if (m.Phase() == MatchPhase::Complete) return {"none", -1};
    if (paused_) return {"interval", -1};
    if (m.Phase() == MatchPhase::AwaitingFollowOnDecision) return {"follow-on", m.AllInnings()[0].BattingSide()};
    if (m.HasPendingDecision()) return {"review", m.ReviewEntitledSide(), reviewDeadlineMs_ ? *reviewDeadlineMs_ : -1};
    switch (m.Current().State()) {
        case InningsState::AwaitingOpeners: return {"openers", m.BattingSide()};
        case InningsState::AwaitingBatter: return {"batter", m.BattingSide()};
        case InningsState::AwaitingBowler: return {"bowler", m.FieldingSide()};
        case InningsState::InPlay: return {"delivery", -1};
        case InningsState::Complete: break;
    }
    return {"none", -1};
}

void MatchSession::StartMatch() {
    match_ = std::make_unique<Match>(rules_, teams_, toss_.BattingFirst());
    AfterMatchChange();
}

void MatchSession::AfterMatchChange() {
    if (!match_) return;
    const auto& all = match_->AllInnings();
    auto logNextInnings = [&]() {
        if (lastInningsSeen_ >= all.size()) return;
        const Innings& inn = all[lastInningsSeen_];
        std::string t = "Innings " + std::to_string(lastInningsSeen_ + 1) + ": " + teams_[inn.BattingSide()].name +
                        " to bat";
        if (inn.Target()) t += ", target " + std::to_string(*inn.Target());
        Log("innings", t);
        lastInningsSeen_++;
    };
    if (lastInningsSeen_ == 0) logNextInnings();  // the first innings has no break before it
    const auto& iv = match_->Intervals();
    while (intervalsSeen_ < iv.size()) {
        const IntervalRecord& r = iv[intervalsSeen_++];
        const std::string score = teams_[r.battingSide].name + " " + std::to_string(r.total) + "/" +
                                  std::to_string(r.wickets) + " (" + std::to_string(r.overs) +
                                  (r.balls ? "." + std::to_string(r.balls) : std::string()) + ")";
        if (r.kind == IntervalKind::InningsBreak) {
            Log("innings-break", "Innings break. " + score);
            logNextInnings();  // the innings that follows this break
            continue;
        }
        std::string kind = ToString(r.kind);
        Log(kind, std::string(kind == "stumps" ? "Stumps" : kind == "lunch" ? "Lunch" : kind == "tea" ? "Tea" : "Dinner") +
                      " on day " + std::to_string(r.day) + ": " + score + (r.early ? " (taken early, innings ended)" : ""));
        // Humans get a real break; AI-only matches play straight on.
        if (match_->Phase() != MatchPhase::Complete && (IsHuman(0) || IsHuman(1))) {
            paused_ = true;
            ready_ = {!IsHuman(0), !IsHuman(1)};
        }
    }
    while (lastInningsSeen_ < all.size()) logNextInnings();
    if (match_->Phase() == MatchPhase::AwaitingFollowOnDecision)
        Log("follow-on", teams_[all[0].BattingSide()].name + " may enforce the follow-on");
    if (match_->Phase() == MatchPhase::Complete && !resultLogged_) {
        resultLogged_ = true;
        Log("result", match_->Result().summary);
    }
}

void MatchSession::EnsureLegalField() {
    if (!match_ || match_->Phase() != MatchPhase::InProgress) return;
    const int side = match_->FieldingSide();
    FieldContext ctx{&match_->Current().Rules(), match_->Current().CurrentOverNumber()};
    if (ValidateField(fields_[side], ctx).status) return;
    int bowler = match_->Current().CurrentBowler();
    BowlingKind kind = bowler >= 0 ? teams_[side].xi[bowler].bowls : BowlingKind::Pace;
    fields_[side] = ai_.ChooseLegalField(*match_, kind);
    Log("field", teams_[side].name + " field adjusted to meet fielding restrictions");
}

Status MatchSession::Submit(const Command& c) {
    const int side = SideOf(c.player);
    if (side < 0) return Status::Err("unknown player");
    auto& last = lastSeq_[c.player];
    if (c.seq <= last) return Status::Err("duplicate or out-of-order command");
    last = c.seq;
    Status s = Apply(side, c);
    if (s) RunAi();
    return s;
}

Status MatchSession::Apply(int side, const Command& c) {
    const Pending p = WaitingFor();
    auto mustBe = [&](const char* what, int who) -> Status {
        if (p.what != what) return Status::Err(std::string("not expecting ") + what + " now (waiting for " + p.what + ")");
        if (who != side) return Status::Err("it is not your decision");
        return Status::Ok();
    };
    Status s;
    switch (c.type) {
        case CommandType::TossCall: {
            if (!(s = mustBe("toss-call", p.side))) return s;
            CoinFace call = c.a == 0 ? CoinFace::Heads : CoinFace::Tails;
            if (!(s = toss_.Call(side, call))) return s;
            Log("toss", teams_[side].name + " call " + Face(call) + ". It's " + Face(toss_.Landed()) + "! " +
                            teams_[toss_.Winner()].name + " win the toss.");
            return s;
        }
        case CommandType::TossDecision: {
            if (!(s = mustBe("toss-decision", p.side))) return s;
            TossChoice ch = c.a == 0 ? TossChoice::Bat : TossChoice::Bowl;
            if (!(s = toss_.Decide(side, ch))) return s;
            Log("decision", teams_[side].name + " choose to " + (ch == TossChoice::Bat ? "bat" : "bowl") + " first");
            StartMatch();
            return s;
        }
        case CommandType::SetOpeners:
            if (!(s = mustBe("openers", p.side))) return s;
            return match_->SetOpeners(c.a, c.b);
        case CommandType::SetNextBatter:
            if (!(s = mustBe("batter", p.side))) return s;
            return match_->SetNextBatter(c.a);
        case CommandType::SetBowler:
            if (!(s = mustBe("bowler", p.side))) return s;
            if (!(s = match_->SetBowler(c.a))) return s;
            EnsureLegalField();
            return s;
        case CommandType::SetFieldPreset:
        case CommandType::SetField: {
            if (!match_ || match_->Phase() != MatchPhase::InProgress) return Status::Err("no innings in progress");
            if (side != match_->FieldingSide()) return Status::Err("only the fielding captain sets the field");
            if (c.type == CommandType::SetFieldPreset &&
                (c.a < 0 || c.a >= static_cast<int>(AllPresets().size())))
                return Status::Err("unknown field preset");
            FieldSetup f = c.type == CommandType::SetFieldPreset
                               ? FieldSetup::FromPreset(AllPresets()[c.a])
                               : c.field;
            FieldContext ctx{&match_->Current().Rules(), match_->Current().CurrentOverNumber()};
            FieldCheck chk = ValidateField(f, ctx);
            if (!chk.status) return chk.status;
            fields_[side] = f;
            Log("field", teams_[side].name + " change the field");
            return s;
        }
        case CommandType::Declare:
            if (!match_ || match_->Phase() != MatchPhase::InProgress) return Status::Err("no innings in progress");
            if (side != match_->BattingSide()) return Status::Err("only the batting captain can declare");
        {
            const std::string score = match_->Current().ScoreString();
            if (!(s = match_->Declare())) return s;
            Log("declare", teams_[side].name + " declare at " + score);
        }
            AfterMatchChange();
            return s;
        case CommandType::Review: {
            // A late request never gets here: the clock only moves through
            // Tick(), which closes an expired window before any command is read.
            if (!(s = mustBe("review", p.side))) return s;
            DecisionRecord dec;
            DeliveryRecord ball;
            return FinishDecision(c.a ? match_->ReviewDecision(side, &dec, &ball) : match_->AcceptDecision(&dec, &ball),
                                  dec, ball);
        }
        case CommandType::TakeNewBall:
            if (!match_ || match_->Phase() != MatchPhase::InProgress) return Status::Err("no innings in progress");
            if (side != match_->FieldingSide()) return Status::Err("only the fielding captain takes the new ball");
            if (!(s = match_->TakeNewBall())) return s;
            Log("new-ball", teams_[side].name + " take the new ball after " +
                                std::to_string(match_->Current().CompletedOvers()) + " overs");
            return s;
        case CommandType::ResumePlay:
            if (p.what != "interval") return Status::Err("play is not paused");
            ready_[side] = true;
            Log("ready", teams_[side].name + " ready to resume");
            if (ready_[0] && ready_[1]) Resume();
            return Status::Ok();
        case CommandType::FollowOn:
            if (!(s = mustBe("follow-on", p.side))) return s;
            if (!(s = match_->DecideFollowOn(c.a != 0))) return s;
            Log("follow-on", c.a ? "Follow-on enforced" : "Follow-on not enforced");
            AfterMatchChange();
            return s;
    }
    return Status::Err("unknown command");
}

Status MatchSession::ResolveDelivery(const Delivery& d) {
    if (WaitingFor().what != "delivery") return Status::Err("not ready for a delivery");
    EnsureLegalField();
    DeliveryRecord rec;
    Status s = match_->Bowl(d, &rec);
    if (!s) return s;
    LogBall(rec);
    RunAi();
    return s;
}

void MatchSession::LogBall(const DeliveryRecord& rec) {
    Log("ball", BallText(*match_, rec), rec);
    if (rec.inningsCompleted) Log("innings-end", match_->AllInnings()[rec.inningsIndex].ScoreString());
    AfterMatchChange();
}

namespace {
const char* OutcomeText(ReviewOutcome o) {
    switch (o) {
        case ReviewOutcome::Upheld: return "decision stands";
        case ReviewOutcome::Overturned: return "OVERTURNED";
        case ReviewOutcome::UmpiresCall: return "umpire's call";
        case ReviewOutcome::ThirdUmpire: return "third umpire";
        case ReviewOutcome::NotReviewed: return "not reviewed";
    }
    return "?";
}
}  // namespace

void MatchSession::LogDecision(const DecisionRecord& d) {
    std::string t;
    const bool onFieldOnly = d.result.outcome == ReviewOutcome::NotReviewed;
    if (d.call != ScoringCall::None) {
        t = std::string(d.kind == DecisionKind::UmpireReferral ? "TV check (" : "Call (") + ToString(d.call) +
            "), on-field " + (d.onFieldCalled ? "signalled" : "not signalled") + ". ";
        if (d.reviewed) t += teams_[d.reviewingSide].name + " review: " + OutcomeText(d.result.outcome) + ". ";
        t += d.result.finalCalled ? "Call stands as signalled" : "Not given";
        if (d.result.finalCalled != d.onFieldCalled) t += " (changed)";
        t += " - " + d.result.reason;
        if (d.reviewed) t += " [" + std::to_string(d.reviewsLeftAfter) + " review(s) left]";
        Log(d.reviewed ? "review" : (d.kind == DecisionKind::UmpireReferral && !onFieldOnly ? "third-umpire" : "decision"), t);
        events_.back().decision = d;
        return;
    }
    if (d.kind == DecisionKind::UmpireReferral && onFieldOnly) {
        t = std::string("On-field decision, no third umpire (") + ToString(d.appealedFor) + "): ";
    } else if (d.kind == DecisionKind::UmpireReferral) {
        t = std::string("Referral (") + ToString(d.appealedFor) + "): ";
    } else {
        t = std::string("Appeal (") + ToString(d.appealedFor) + "), on-field " + (d.onFieldOut ? "OUT" : "NOT OUT") +
            ". ";
        if (d.reviewed)
            t += teams_[d.reviewingSide].name + " review: " + OutcomeText(d.result.outcome) + ". ";
    }
    t += d.result.finalOut ? std::string("OUT ") + ToString(d.result.finalMode) : std::string("NOT OUT");
    if (d.result.noBall) t += " (no-ball)";
    t += " - " + d.result.reason;
    if (d.reviewed) t += " [" + std::to_string(d.reviewsLeftAfter) + " review(s) left]";
    Log(d.reviewed ? "review" : (d.kind == DecisionKind::UmpireReferral && !onFieldOnly ? "third-umpire" : "decision"), t);
    events_.back().decision = d;
}

Status MatchSession::FinishDecision(Status s, const DecisionRecord& d, const DeliveryRecord& ball) {
    if (!s) return s;
    reviewDeadlineMs_.reset();
    LogDecision(d);
    LogBall(ball);
    return s;
}

Status MatchSession::ResolveAppeal(const DecisionCase& c) {
    if (WaitingFor().what != "delivery") return Status::Err("not ready for a delivery");
    EnsureLegalField();
    DecisionRecord dec;
    DeliveryRecord ball;
    Status s = match_->Appeal(c, &dec, &ball);
    if (!s) return s;
    if (c.kind == DecisionKind::UmpireReferral) {
        FinishDecision(s, dec, ball);
    } else if (match_->HasPendingDecision()) {
        const int secs = match_->Rules().drs.reviewTimerSeconds;
        if (secs > 0) reviewDeadlineMs_ = nowMs_ + static_cast<int64_t>(secs) * 1000;
        const std::string what = c.call != ScoringCall::None
                                     ? std::string(ToString(c.call)) + " call: " + (c.onFieldCalled ? "signalled" : "not signalled")
                                     : std::string("Appeal for ") + ToString(c.mode) + ": on-field " +
                                           (c.onFieldOut ? "OUT" : "NOT OUT");
        Log("appeal", what + ". " + teams_[match_->ReviewEntitledSide()].name +
                          " may review (" + std::to_string(match_->ReviewsRemaining(match_->ReviewEntitledSide())) +
                          " left" + (secs > 0 ? ", " + std::to_string(secs) + " seconds" : std::string()) + ")");
    } else {
        // Competition without player reviews: the on-field call was final.
        FinishDecision(s, dec, ball);
    }
    RunAi();
    return s;
}

Status MatchSession::AwardPenaltyRuns(int side, int runs, const std::string& reason) {
    if (!match_) return Status::Err("match has not started");
    if (Status s = match_->AwardPenaltyRuns(side, runs, reason); !s) return s;
    const auto& p = match_->Penalties().back();
    Log("penalty", std::to_string(runs) + " penalty runs to " + teams_[side].name + " (" + reason + ")" +
                       (p.inningsIndex < 0 ? ", added when they bat" : ""));
    AfterMatchChange();
    RunAi();
    return Status::Ok();
}

void MatchSession::Resume() {
    paused_ = false;
    ready_ = {false, false};
    const DayClock& k = match_->Clock();
    Log("resume", match_->HasSchedule() ? "Play resumes: day " + std::to_string(k.day) + ", session " +
                                              std::to_string(k.session)
                                        : std::string("Play resumes"));
}

Status MatchSession::ResumeAfterInterval() {
    if (!paused_) return Status::Err("play is not paused");
    Resume();
    RunAi();
    return Status::Ok();
}

Status MatchSession::AfterWeather(Status s, std::optional<int> targetBefore, size_t rainBefore) {
    if (!s) return s;
    const Match& m = *match_;
    if (m.RainLog().size() > rainBefore) {
        const RainRecord& r = m.RainLog().back();
        std::string t = "Rain: " + std::to_string(r.minutes) + " minutes lost";
        if (r.bufferUsed > 0) t += " (" + std::to_string(r.bufferUsed) + " absorbed by extra time)";
        if (m.HasSchedule()) t += ", " + std::to_string(r.oversLost) + " overs off day " + std::to_string(r.day);
        else if (r.oversLost > 0)
            t += ". Innings now " + std::to_string(r.newOversThis) + " overs" +
                 (r.inningsIndex == 0 ? ", second innings " + std::to_string(r.newOversSecond) + " overs" : std::string());
        Log("rain", t);
    }
    if (m.Current().Target() && m.Current().Target() != targetBefore)
        Log("target", "Revised target (rain rule, non-official): " + std::to_string(*m.Current().Target()) + " from " +
                          std::to_string(m.Current().MaxOvers()) + " overs");
    AfterMatchChange();
    RunAi();
    return s;
}

Status MatchSession::RainDelay(int minutes) {
    if (!match_) return Status::Err("match has not started");
    const auto before = match_->Current().Target();
    const size_t rain = match_->RainLog().size();
    return AfterWeather(match_->RainDelay(minutes), before, rain);
}

Status MatchSession::ReduceOvers(int oversThis, int oversSecond) {
    if (!match_) return Status::Err("match has not started");
    const auto before = match_->Current().Target();
    const size_t rain = match_->RainLog().size();
    Status s = match_->ReduceOvers(oversThis, oversSecond);
    if (s) Log("overs", "Overs reduced: this innings " + std::to_string(oversThis) + " overs");
    return AfterWeather(s, before, rain);
}

Status MatchSession::LoseRestOfDay() {
    if (!match_) return Status::Err("match has not started");
    const auto before = match_->Current().Target();
    const size_t rain = match_->RainLog().size();
    return AfterWeather(match_->LoseRestOfDay(), before, rain);
}

Status MatchSession::AbandonMatch() {
    if (!match_) return Status::Err("match has not started");
    if (match_->Phase() == MatchPhase::Complete) return Status::Err("match is over");
    match_->Abandon();
    Log("abandoned", "Play abandoned");
    reviewDeadlineMs_.reset();
    paused_ = false;
    AfterMatchChange();
    return Status::Ok();
}

Status MatchSession::TimeOutReview() {
    Log("review-timeout", "Review time (" + std::to_string(match_->Rules().drs.reviewTimerSeconds) +
                              " s) is up: the on-field decision stands");
    return ExpireReviewWindow();
}

Status MatchSession::Tick(int64_t nowMs) {
    if (nowMs < nowMs_) return Status::Err("server clock went backwards; ignored");
    nowMs_ = nowMs;
    if (match_ && match_->HasPendingDecision() && reviewDeadlineMs_ && nowMs_ >= *reviewDeadlineMs_)
        return TimeOutReview();
    return Status::Ok();
}

Status MatchSession::SubmitAt(const Command& c, int64_t nowMs) {
    if (Status s = Tick(nowMs); !s && nowMs < nowMs_) return s;
    return Submit(c);
}

int64_t MatchSession::ReviewMsLeft() const {
    if (!match_ || !match_->HasPendingDecision() || !reviewDeadlineMs_) return -1;
    return std::max<int64_t>(0, *reviewDeadlineMs_ - nowMs_);
}

Status MatchSession::ExpireReviewWindow() {
    if (!match_ || !match_->HasPendingDecision()) return Status::Err("no decision is pending");
    DecisionRecord dec;
    DeliveryRecord ball;
    Status s = FinishDecision(match_->AcceptDecision(&dec, &ball), dec, ball);
    if (s) RunAi();
    return s;
}

void MatchSession::RunAi() {
    for (int guard = 0; guard < 64; ++guard) {
        Pending p = WaitingFor();
        if (p.what == "delivery") {
            // AI batting captain may declare (Test only).
            if (match_->Rules().oversPerInnings == 0 && !IsHuman(match_->BattingSide()) &&
                ai_.ShouldDeclare(*match_)) {
                const int side = match_->BattingSide();
                std::string score = match_->Current().ScoreString();
                if (match_->Declare()) {
                    Log("declare", teams_[side].name + " declare at " + score);
                    AfterMatchChange();
                    continue;
                }
            }
            return;
        }
        if (p.side < 0 || IsHuman(p.side)) return;
        Status s;
        if (p.what == "toss-call") {
            CoinFace f = (ai_.ChooseAtToss(rules_) == TossChoice::Bat) ? CoinFace::Heads : CoinFace::Tails;
            s = toss_.Call(p.side, f);
            if (s) Log("toss", teams_[p.side].name + " call " + Face(f) + ". It's " + Face(toss_.Landed()) + "! " +
                                   teams_[toss_.Winner()].name + " win the toss.");
        } else if (p.what == "toss-decision") {
            TossChoice ch = ai_.ChooseAtToss(rules_);
            s = toss_.Decide(p.side, ch);
            if (s) {
                Log("decision", teams_[p.side].name + " choose to " + (ch == TossChoice::Bat ? "bat" : "bowl") + " first");
                StartMatch();
            }
        } else if (p.what == "openers") {
            auto [a, b] = ai_.ChooseOpeners(*match_);
            s = match_->SetOpeners(a, b);
        } else if (p.what == "batter") {
            s = match_->SetNextBatter(ai_.ChooseNextBatter(*match_));
        } else if (p.what == "bowler") {
            if (ai_.ShouldTakeNewBall(*match_) && match_->TakeNewBall())
                Log("new-ball", teams_[p.side].name + " take the new ball after " +
                                    std::to_string(match_->Current().CompletedOvers()) + " overs");
            int b = ai_.ChooseBowler(*match_);
            s = b >= 0 ? match_->SetBowler(b) : Status::Err("AI found no legal bowler");
            if (s) fields_[p.side] = ai_.ChooseLegalField(*match_, teams_[p.side].xi[b].bowls);
        } else if (p.what == "review") {
            DecisionRecord dec;
            DeliveryRecord ball;
            s = FinishDecision(ai_.ShouldReview(*match_, p.side) ? match_->ReviewDecision(p.side, &dec, &ball)
                                                                  : match_->AcceptDecision(&dec, &ball),
                               dec, ball);
        } else if (p.what == "follow-on") {
            bool enforce = ai_.ShouldEnforceFollowOn(*match_);
            s = match_->DecideFollowOn(enforce);
            if (s) {
                Log("follow-on", enforce ? "Follow-on enforced" : "Follow-on not enforced");
                AfterMatchChange();
            }
        } else {
            return;
        }
        if (!s) {
            Log("error", "AI could not act: " + s.error);
            return;
        }
    }
}

}  // namespace cricket
