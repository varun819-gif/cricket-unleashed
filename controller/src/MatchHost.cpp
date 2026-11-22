#include "cricket/controller/MatchHost.h"

#include <cmath>
#include <cstdio>

#include "cricket/Scorecard.h"

#include <algorithm>
#include <cmath>

namespace cricket::ctl {

namespace {

bool OneOf(const std::string& s, std::initializer_list<const char*> v) {
    for (const char* x : v)
        if (s == x) return true;
    return false;
}

const char* kPaceVariations[] = {"stock", "yorker", "bouncer", "slower", "inswing", "outswing"};
const char* kSpinVariations[] = {"stock", "offbreak", "legbreak", "googly", "armball", "slower"};

}  // namespace

MatchHost::MatchHost(MatchRules rules, std::array<Team, 2> teams, ControlChannel& hub, HostConfig cfg)
    : hub_(hub),
      cfg_(cfg),
      session_(std::make_unique<MatchSession>(std::move(rules), std::move(teams), cfg.seed)),
      sim_(cfg.seed * 31 + 7),
      outcome_(cfg.seed * 17 + 3) {
    outcome_.conditions = cfg_.conditions;
    for (int s = 0; s < 2; ++s)
        if (cfg_.human[s]) session_->Join(PlayerId(s), s);
    session_->RunAi();  // an AI side may need to call the toss
}

MatchHost::MatchHost(std::unique_ptr<MatchSession> recovered, ControlChannel& hub, HostConfig cfg, uint64_t resumeSalt)
    : hub_(hub),
      cfg_(cfg),
      session_(std::move(recovered)),
      sim_(cfg.seed * 31 + 7 + resumeSalt),
      outcome_(cfg.seed * 17 + 3 + resumeSalt) {
    outcome_.conditions = cfg_.conditions;
    // Continue the command numbers after those already in the journal.
    for (const auto& in : session_->Inputs())
        if (in.type == InputType::Submit || in.type == InputType::SubmitAt)
            for (int s = 0; s < 2; ++s)
                if (in.command.player == PlayerId(s)) seq_[s] = std::max<uint64_t>(seq_[s], in.command.seq);
}

Status MatchHost::HandToAi(int side) {
    if (side != 0 && side != 1) return Status::Err("side must be 0 or 1");
    if (!cfg_.human[side]) return Status::Err("already played by the AI");
    cfg_.human[side] = false;
    stage_.clear();  // restart the current ball with the AI
    return session_->ReleaseSide(side);
}

bool MatchHost::SideConnected(int side) const {
    for (const auto& d : hub_.Devices())
        if (d.side == side && d.connected && d.role != Role::Spectator) return true;
    return false;
}

int MatchHost::WaitingForController() const {
    if (session_->WaitingFor().what == "none") return -1;
    for (int s = 0; s < 2; ++s)
        if (cfg_.human[s] && !SideConnected(s)) return s;
    return -1;
}

Status MatchHost::Submit(int side, CommandType t, int a, int b, int64_t now) {
    Command c;
    c.player = PlayerId(side);
    c.seq = ++seq_[side];
    c.type = t;
    c.a = a;
    c.b = b;
    return session_->SubmitAt(c, now);
}

std::vector<std::string> MatchHost::AllowedControls(int side, Role role) const {
    std::vector<std::string> out;
    if (role == Role::Spectator || side < 0 || !cfg_.human[side]) return out;
    const Pending p = session_->WaitingFor();
    if (p.what == "interval") out.push_back("ready");
    if (role == Role::Captain && p.side == side) {
        if (p.what == "toss-call") out.push_back("toss-call");
        if (p.what == "toss-decision") out.push_back("toss-decide");
        if (p.what == "openers") out.push_back("openers");
        if (p.what == "batter") out.push_back("batter");
        if (p.what == "bowler") out.push_back("bowler");
        if (p.what == "review") {
            // Only offer a review the side still has.
            if (session_->HasMatch() && session_->GetMatch().ReviewsRemaining(side) > 0) out.push_back("review");
            out.push_back("accept");
        }
        if (p.what == "follow-on") out.push_back("follow-on");
    }
    if (!session_->HasMatch() || session_->GetMatch().Phase() != MatchPhase::InProgress) return out;
    const Match& m = session_->GetMatch();
    const bool batting = m.BattingSide() == side;
    if (role == Role::Captain && !batting && p.what != "review" && p.what != "interval") {
        out.push_back("field");
        if (m.Current().NewBallDue()) out.push_back("new-ball");
    }
    if (role == Role::Captain && batting && m.Rules().oversPerInnings == 0 && p.what == "delivery" && stage_ != "ball")
        out.push_back("declare");
    if (p.what == "delivery") {
        if (stage_ == "bowl" && !batting) out.push_back("bowl");
        if (stage_ == "ball" && batting && !bat_.provided) out.push_back("bat");
        if (stage_ == "field" && !batting && awaitField_) out.push_back("fielding");
        if (stage_ == "field" && batting && awaitRun_) out.push_back("run");
    }
    return out;
}

Status MatchHost::Captaincy(int side, const std::string& what, const Json& a, int64_t now) {
    if (what == "toss-call") return Submit(side, CommandType::TossCall, a.Str("face") == "tails" ? 1 : 0, 0, now);
    if (what == "toss-decide") return Submit(side, CommandType::TossDecision, a.Str("choice") == "bowl" ? 1 : 0, 0, now);
    if (what == "openers")
        return Submit(side, CommandType::SetOpeners, static_cast<int>(a.Int("striker", -1)),
                      static_cast<int>(a.Int("nonStriker", -1)), now);
    if (what == "batter") return Submit(side, CommandType::SetNextBatter, static_cast<int>(a.Int("player", -1)), 0, now);
    if (what == "bowler") return Submit(side, CommandType::SetBowler, static_cast<int>(a.Int("player", -1)), 0, now);
    if (what == "field") {
        const std::string name = a.Str("preset");
        const auto& all = AllPresets();
        for (size_t i = 0; i < all.size(); ++i)
            if (name == ToString(all[i])) return Submit(side, CommandType::SetFieldPreset, static_cast<int>(i), 0, now);
        return Status::Err("unknown field preset");
    }
    if (what == "review") return Submit(side, CommandType::Review, 1, 0, now);
    if (what == "accept") return Submit(side, CommandType::Review, 0, 0, now);
    if (what == "declare") return Submit(side, CommandType::Declare, 0, 0, now);
    if (what == "follow-on") return Submit(side, CommandType::FollowOn, a.Flag("enforce", true) ? 1 : 0, 0, now);
    if (what == "new-ball") return Submit(side, CommandType::TakeNewBall, 0, 0, now);
    if (what == "ready") return Submit(side, CommandType::ResumePlay, 0, 0, now);
    return Status::Err("unknown control");
}

namespace {
double R1(double x) { return std::round(x * 10) / 10; }
}  // namespace

Status MatchHost::Apply(const ControllerAction& act, int64_t now) {
    const std::string what = act.action.Str("do");
    const auto allowed = AllowedControls(act.side, act.role);
    if (std::find(allowed.begin(), allowed.end(), what) == allowed.end())
        return Status::Err(act.role == Role::Spectator ? "spectators can only watch" : "that control is not available now");
    const Json& a = act.action;
    if (what == "bowl") {
        BowlInput b;
        b.variation = a.Str("variation", "stock");
        const Match& m = session_->GetMatch();
        const int bowler = m.Current().CurrentBowler();
        const bool spin = bowler >= 0 && m.GetTeam(m.FieldingSide()).xi[bowler].bowls == BowlingKind::Spin;
        bool known = false;
        for (const char* v : spin ? kSpinVariations : kPaceVariations) known |= b.variation == v;
        if (!known) return Status::Err("that delivery isn't available for this bowler");
        b.line = std::clamp(a.Num("line", 0.0), -1.0, 1.0);
        b.length = std::clamp(a.Num("length", 0.5), 0.0, 1.0);
        b.effort = static_cast<int>(std::clamp<int64_t>(a.Int("effort", 70), 0, 100));
        bowl_ = b;
        ReleaseBall(now);
        return Status::Ok();
    }
    if (what == "bat") {
        BatInput b;
        b.shot = a.Str("shot", "defend");
        if (!OneOf(b.shot, {"leave", "defend", "drive", "cut", "pull", "sweep", "flick", "loft"}))
            return Status::Err("unknown shot");
        b.direction = static_cast<int>(std::clamp<int64_t>(a.Int("dir", 0), -180, 180));
        b.timingMs = static_cast<int>(std::clamp<int64_t>(a.Int("timing", 0), -1000, 1000));
        if (cfg_.serverTimingCheck && act.receivedMs > 0) {
            // Online anti-cheat: the tap reached us one round trip after the
            // player saw the ball arrive, so the timing they report must be
            // close to what we measured. Otherwise we use our measurement.
            const int64_t rtt = std::max<int64_t>(0, hub_.RttMs(act.device));
            const int64_t measured = act.receivedMs - arriveAt_ - rtt;
            if (std::abs(b.timingMs - measured) > cfg_.timingToleranceMs) {
                b.timingMs = static_cast<int>(std::clamp<int64_t>(measured, -1000, 1000));
                ++timingCorrections_;
            }
        }
        b.provided = true;
        b.advance = a.Flag("advance");
        bat_ = b;
        ResolveBall(now);
        return Status::Ok();
    }
    if (what == "fielding") {
        std::string f = a.Str("act");
        if (f == "dive-left" || f == "dive-right") f = "dive";
        if (!OneOf(f, {"dive", "catch", "throw-keeper", "throw-bowler"})) return Status::Err("unknown fielding action");
        fieldingCalls_.push_back(f);
        if (f == "throw-keeper" || f == "throw-bowler") {
            fielding_.throwEnd = f == "throw-keeper" ? 0 : 1;
            return Status::Ok();
        }
        // Catch / dive timing against the moment the ball reaches the fielder
        // (as the phone showed it), scaled to real time; checked online.
        int64_t timing = std::clamp<int64_t>(a.Int("timing", 0), -2000, 2000);
        if (cfg_.serverTimingCheck && act.receivedMs > 0) {
            const int64_t rtt = std::max<int64_t>(0, hub_.RttMs(act.device));
            const int64_t measured = act.receivedMs - fieldArriveAt_ - rtt;
            if (std::abs(timing - measured) > cfg_.timingToleranceMs) {
                timing = std::clamp<int64_t>(measured, -2000, 2000);
                ++timingCorrections_;
            }
        }
        const double scale = fieldPresentMs_ > 0 ? static_cast<double>(fieldRealMs_) / fieldPresentMs_ : 1.0;
        const double err = std::abs(static_cast<double>(timing) * scale);
        const double bonus = err < 60 ? 0.12 : err < 150 ? 0.05 : err > 300 ? -0.10 : 0.0;
        fielding_.assist.provided = true;
        if (f == "catch") fielding_.assist.catchBonus = bonus;
        else fielding_.assist.stopBonus = bonus;
        awaitField_ = false;
        if (!awaitRun_) FinishBall(now);
        return Status::Ok();
    }
    if (what == "run") {
        // The batters' call: "wait" (no run) or 1, 2, 3 runs.
        const std::string c = a.Str("call");
        int n = -1;
        if (c == "wait" || c == "0") n = 0;
        else if (c == "1" || c == "2" || c == "3") n = c[0] - '0';
        if (n < 0) {
            const int64_t k = a.Int("runs", -1);
            if (k >= 0 && k <= 3) n = static_cast<int>(k);
        }
        if (n < 0) return Status::Err("call wait, 1, 2 or 3");
        pendingBat_.runsCalled = n;
        awaitRun_ = false;
        if (!awaitField_) FinishBall(now);
        return Status::Ok();
    }
    Status s = Captaincy(act.side, what, a, now);
    if (s && session_->WaitingFor().what != "delivery") stage_.clear();
    return s;
}

void MatchHost::ReleaseBall(int64_t now) {
    stage_ = "ball";
    {
        const Match& m = session_->GetMatch();
        BowlChoice c;
        c.variation = bowl_.variation;
        c.line = bowl_.line;
        c.length = bowl_.length;
        c.effort = bowl_.effort;
        spec_ = outcome_.SpecFor(m, c);
        path_ = phys::SimulateDelivery(spec_, outcome_.PitchFor(m), outcome_.EnvFor(m));
        realFlightMs_ = static_cast<int64_t>(path_.timeAtCrease * 1000);
    }
    arriveAt_ = now + cfg_.ballFlightMs;
    // Online, the batter's tap needs a round trip to reach us: allow for it.
    int64_t latency = 0;
    if (cfg_.serverTimingCheck)
        for (const auto& d : hub_.Devices())
            if (d.side == session_->GetMatch().BattingSide()) latency = std::max(latency, hub_.RttMs(d.id));
    deadline_ = arriveAt_ + cfg_.batWindowMs + std::clamp<int64_t>(latency, 0, 1500);
    bat_ = {};
    fieldingCalls_.clear();
    // The batting phone animates the ball and measures the tap against arrival.
    Json ev = Json::Object();
    ev.Set("k", "ball").Set("arriveInMs", cfg_.ballFlightMs).Set("variation", bowl_.variation);
    ev.Set("line", bowl_.line).Set("length", bowl_.length);
    hub_.Broadcast(session_->GetMatch().BattingSide(), ev);
}

void MatchHost::ResolveBall(int64_t now) {
    if (stage_ != "ball") return;
    const Match& m = session_->GetMatch();
    BatChoice bat;
    if (!cfg_.human[m.BattingSide()]) {
        bat = outcome_.AiShot(m, path_, spec_);
    } else if (bat_.provided) {
        bat.provided = bat_.shot != "leave";
        bat.shot = bat_.shot;
        bat.direction = bat_.direction;
        bat.advance = bat_.advance;
        // The phone shows the ball over `ballFlightMs` (slowed down so people
        // can react); its timing error is scaled to the real flight time.
        const double scale = cfg_.ballFlightMs > 0 ? static_cast<double>(realFlightMs_) / cfg_.ballFlightMs : 1.0;
        bat.timingMs = bat_.timingMs * scale;
    }
    pendingBat_ = bat;
    fielding_ = {};
    awaitField_ = awaitRun_ = false;
    const int fielding = m.FieldingSide(), batting = m.BattingSide();
    const bool humanField = cfg_.fieldingControls && cfg_.human[fielding] && SideConnected(fielding);
    const bool humanRun = cfg_.runningControls && cfg_.human[batting] && SideConnected(batting);
    if (humanField || humanRun) {
        // Look ahead (on a copy, so no random numbers are used up) to see if
        // the ball goes to a fielder; if so, the fielding phone gets to react
        // and the batting phone calls the run.
        BallOutcome probe = outcome_;
        const BallReport pre = probe.ResolveSpec(m, session_->Field(fielding), spec_, bat, nullptr, &path_);
        const bool offBat = pre.bat.contact != phys::Contact::Missed && pre.bat.contact != phys::Contact::Pad &&
                            pre.bat.contact != phys::Contact::Bowled;
        const bool inField = offBat && !pre.field.path.empty() && pre.field.slot >= 0 && !pre.field.boundary;
        awaitField_ = humanField && offBat && !pre.field.path.empty() &&
                      (pre.field.caught || pre.field.dropped || inField);
        awaitRun_ = humanRun && inField && !pre.field.caught && pre.runningChance;
        if (awaitField_ || awaitRun_) {
            stage_ = "field";
            fieldKind_ = (pre.field.caught || pre.field.dropped) ? "catch" : "stop";
            const Team& t = m.GetTeam(fielding);
            fieldName_ = pre.field.fielder >= 0 ? t.xi.at(static_cast<size_t>(pre.field.fielder)).name : "a fielder";
            runDistance_ = phys::Length(phys::V3{pre.field.gatheredPoint.x, pre.field.gatheredPoint.y, 0});
            runDeep_ = runDistance_ > 40;
            ballBackS_ = pre.ballBackS;
            oneRunS_ = pre.oneRunS;
            twoRunsS_ = pre.twoRunsS;
            threeRunsS_ = pre.threeRunsS;
            fieldRealMs_ = static_cast<int64_t>(pre.field.path.back().t * 1000);
            const double present = cfg_.ballFlightMs > 0 && realFlightMs_ > 0
                                       ? static_cast<double>(fieldRealMs_) * cfg_.ballFlightMs / realFlightMs_
                                       : static_cast<double>(fieldRealMs_);
            fieldPresentMs_ = std::clamp<int64_t>(static_cast<int64_t>(present), 500, 2500);
            fieldArriveAt_ = now + fieldPresentMs_;
            int64_t latency = 0;
            if (cfg_.serverTimingCheck)
                for (const auto& d : hub_.Devices())
                    if (d.side == fielding || d.side == batting) latency = std::max(latency, hub_.RttMs(d.id));
            deadline_ = fieldArriveAt_ + cfg_.fieldWindowMs + std::clamp<int64_t>(latency, 0, 1500);
            if (awaitField_) {
                Json ev = Json::Object();
                ev.Set("k", "field").Set("kind", fieldKind_).Set("fielder", fieldName_).Set("arriveInMs", fieldPresentMs_);
                hub_.Broadcast(fielding, ev);
            }
            if (awaitRun_) {
                Json ev = Json::Object();
                ev.Set("k", "run").Set("fielder", fieldName_).Set("arriveInMs", fieldPresentMs_);
                ev.Set("distance", std::round(runDistance_)).Set("deep", runDeep_).Set("backS", R1(ballBackS_));
                ev.Set("runS", Json::Array().Push(Json::Number(R1(oneRunS_))).Push(Json::Number(R1(twoRunsS_)))
                                   .Push(Json::Number(R1(threeRunsS_))));
                hub_.Broadcast(batting, ev);
            }
            return;
        }
    }
    FinishBall(now);
}

void MatchHost::FinishBall(int64_t now) {
    const Match& m = session_->GetMatch();
    BallReport rep = outcome_.ResolveSpec(m, session_->Field(m.FieldingSide()), spec_, pendingBat_, &sim_, &path_,
                                          fielding_);
    recorder_.Add(rep, bowl_.variation.empty() ? "stock" : bowl_.variation);
    stage_.clear();
    fieldKind_.clear();
    awaitField_ = awaitRun_ = false;
    if (!rep.appeal || !session_->ResolveAppeal(*rep.appeal)) session_->ResolveDelivery(rep.delivery);
    lastBall_ = std::move(rep);
    (void)now;
}

void MatchHost::AdvanceDelivery(int64_t now) {
    if (session_->WaitingFor().what != "delivery") {
        stage_.clear();
        return;
    }
    const Match& m = session_->GetMatch();
    const int fielding = m.FieldingSide(), batting = m.BattingSide();
    if (stage_.empty()) {
        if (cfg_.human[fielding]) {
            stage_ = "bowl";
            deadline_ = now + cfg_.bowlWindowMs;
        } else {
            // Computer bowler: chooses like a captain would (swing with the new
            // ball, yorkers at the death, spinners flight it).
            const BowlChoice c = outcome_.AiBowl(m);
            bowl_ = {};
            bowl_.variation = c.variation;
            bowl_.line = c.line;
            bowl_.length = c.length;
            bowl_.effort = c.effort;
            ReleaseBall(now);
            if (!cfg_.human[batting]) ResolveBall(now);
        }
        return;
    }
    if (stage_ == "bowl" && now >= deadline_) {
        bowl_ = {};  // too slow: a stock ball is bowled for you
        ReleaseBall(now);
        return;
    }
    if (stage_ == "ball" && (!cfg_.human[batting] || now >= deadline_)) ResolveBall(now);
    if (stage_ == "field" && now >= deadline_) FinishBall(now);  // no tap: the fielders do their best
}

void MatchHost::Process(int64_t now) {
    recentEvents_ = hub_.TakeEvents();
    for (const auto& e : recentEvents_)
        if (e.kind == HubEvent::Kind::Connected) lastSent_.erase(e.device);  // send a full state again
    for (const auto& a : hub_.TakeActions()) {
        Status s = Apply(a, now);
        hub_.Ack(a.device, a.id, s.ok, s.error);
    }
    const int waiting = WaitingForController();
    if (waiting >= 0) {
        // A human side has no phone connected: hold every timer until it's back.
        if (pausedAt_ == 0) pausedAt_ = now;
    } else {
        if (pausedAt_ != 0) {
            const int64_t held = now - pausedAt_;
            deadline_ += held;
            arriveAt_ += held;
            fieldArriveAt_ += held;
            pausedAt_ = 0;
        }
        session_->Tick(now);
        if (session_->WaitingFor().what == "interval") {
            bool anyHuman = false;
            for (int s = 0; s < 2; ++s) anyHuman |= cfg_.human[s];
            if (!anyHuman) session_->ResumeAfterInterval();
        }
        AdvanceDelivery(now);
    }
    // What just happened. Several events can happen in one step (e.g. the
    // computer calls the toss, wins it and chooses): show up to three, so
    // nothing important flashes past unseen.
    std::vector<std::string> fresh;
    for (const auto& e : session_->EventsSince(lastEvent_))
        if (e.kind != "join") fresh.push_back(e.text);
    if (!fresh.empty()) {
        message_.clear();
        for (size_t i = fresh.size() > 3 ? fresh.size() - 3 : 0; i < fresh.size(); ++i)
            message_ += (message_.empty() ? "" : "\n") + fresh[i];
    }
    lastEvent_ = session_->LastEventId();
    PublishStates(now);
}

// The full scorecard for the phone / browser (compact keys).
Json ScorecardJson(const Match& m) {
    Json arr = Json::Array();
    for (const auto& c : BuildScorecard(m)) {
        Json in = Json::Object();
        in.Set("team", c.team).Set("total", c.total).Set("wickets", c.wickets).Set("overs", c.overs);
        in.Set("rr", c.runRate).Set("end", c.end);
        in.Set("extras", Json::Object().Set("total", c.extrasTotal).Set("w", c.extras.wides).Set("nb", c.extras.noBalls)
                                       .Set("b", c.extras.byes).Set("lb", c.extras.legByes).Set("pen", c.extras.penalty));
        Json bat = Json::Array();
        for (const auto& b : c.batting) {
            if (!b.batted) continue;
            Json l = Json::Object();
            l.Set("name", b.name).Set("how", b.howOut).Set("r", b.runs).Set("b", b.balls).Set("4s", b.fours);
            l.Set("6s", b.sixes).Set("sr", b.strikeRate);
            bat.Push(l);
        }
        in.Set("batting", bat);
        Json bowl = Json::Array();
        for (const auto& b : c.bowling) {
            Json l = Json::Object();
            l.Set("name", b.name).Set("o", b.overs).Set("m", b.maidens).Set("r", b.runs).Set("w", b.wickets);
            l.Set("econ", b.economy).Set("wd", b.wides).Set("nb", b.noBalls);
            bowl.Push(l);
        }
        in.Set("bowling", bowl);
        Json fow = Json::Array();
        for (const auto& f : c.fallOfWickets)
            fow.Push(Json::Object().Set("n", f.wicket).Set("score", f.score).Set("name", f.name).Set("over", f.over));
        in.Set("fow", fow);
        Json ps = Json::Array();
        for (const auto& p : c.partnerships)
            ps.Push(Json::Object().Set("n", p.wicket).Set("runs", p.runs).Set("balls", p.balls).Set("a", p.nameA)
                        .Set("b", p.nameB).Set("unbroken", p.unbroken));
        in.Set("partnerships", ps);
        arr.Push(in);
    }
    return arr;
}

Json MatchHost::ViewFor(int side, Role role, int64_t now) const {
    Json v = Json::Object();
    v.Set("k", "state");
    const Pending p = session_->WaitingFor();
    v.Set("waiting", p.what).Set("waitingSide", p.side);
    v.Set("yourSide", side).Set("role", ToString(role));
    v.Set("stage", stage_);
    v.Set("physics", true);  // every ball is played out by the ball physics
    if (stage_ == "field") {
        v.Set("fieldPlay", Json::Object().Set("kind", fieldKind_).Set("fielder", fieldName_));
        if (awaitRun_)
            v.Set("runPlay", Json::Object().Set("fielder", fieldName_).Set("distance", std::round(runDistance_))
                                 .Set("deep", runDeep_).Set("backS", R1(ballBackS_))
                                 .Set("runS", Json::Array().Push(Json::Number(R1(oneRunS_)))
                                                  .Push(Json::Number(R1(twoRunsS_))).Push(Json::Number(R1(threeRunsS_)))));
    }
    if (session_->HasMatch()) {
        const Match& mm = session_->GetMatch();
        const phys::Pitch pitch = outcome_.PitchFor(mm);
        const phys::Environment env = outcome_.EnvFor(mm);
        Json c = Json::Object();
        c.Set("pitch", cfg_.conditions.pitchName).Set("wear", std::round(pitch.wear * 100) / 100);
        c.Set("windKph", std::round(phys::Length(env.wind) * 36) / 10).Set("dew", env.dew > 0.05);
        v.Set("conditions", c);
        // Rebuilt only when something happened (cached between updates).
        const uint64_t key = session_->LastEventId() * 1000003ULL + session_->Inputs().size();
        if (key != cardKey_) {
            cardJson_ = ScorecardJson(mm);
            cardKey_ = key;
        }
        v.Set("scorecard", cardJson_);
    }
    if (lastBall_) {
        const BallReport& b = *lastBall_;
        Json lb = Json::Object();
        lb.Set("summary", b.summary).Set("speedKph", b.spec.speedKph).Set("contact", phys::ToString(b.bat.contact));
        lb.Set("pitched", b.path.pitched).Set("pitchLength", b.path.pitchPoint.y);
        lb.Set("swingCm", b.path.swingBeforePitch * 100).Set("deviationCm", b.path.deviationOffPitch * 100);
        lb.Set("exitKph", b.bat.exitSpeedKph).Set("distance", b.field.distance);
        v.Set("lastBall", lb);
    }
    v.Set("waitingForController", WaitingForController());
    v.Set("message", message_);
    Json controls = Json::Array();
    for (const auto& c : AllowedControls(side, role)) controls.Push(Json::String(c));
    v.Set("controls", controls);
    const auto& teams = session_->Teams();
    Json names = Json::Array();
    for (const auto& t : teams) names.Push(Json::String(t.name));
    v.Set("teams", names);
    if (p.what == "review") v.Set("reviewMsLeft", session_->ReviewMsLeft());
    if (stage_ == "bowl" || stage_ == "ball" || stage_ == "field") v.Set("stageMsLeft", std::max<int64_t>(0, deadline_ - now));
    if (!session_->HasMatch()) {
        v.Set("toss", session_->GetToss().Phase() == TossPhase::AwaitingCall ? "call" : "decide");
        return v;
    }
    const Match& m = session_->GetMatch();
    const Innings& inn = m.Current();
    const Team& bat = m.GetTeam(inn.BattingSide());
    const Team& fld = m.GetTeam(1 - inn.BattingSide());
    Json sc = Json::Object();
    sc.Set("batting", inn.BattingSide()).Set("innings", inn.Index() + 1).Set("total", inn.Total());
    sc.Set("wickets", inn.Wickets()).Set("overs", inn.CompletedOvers()).Set("balls", inn.BallsInOver());
    sc.Set("maxOvers", inn.MaxOvers());
    if (inn.Target()) sc.Set("target", *inn.Target());
    const double rrr = inn.RequiredRunRate();
    if (rrr >= 0) sc.Set("rrr", std::round(rrr * 100) / 100);
    sc.Set("freeHit", inn.NextBallIsFreeHit());
    if (m.HasSchedule()) sc.Set("day", m.Clock().day).Set("session", m.Clock().session);
    v.Set("score", sc);
    auto batterJson = [&](int i) {
        Json b = Json::Object();
        if (i < 0) return b;
        b.Set("id", i).Set("name", bat.xi[i].name).Set("runs", inn.Batter(i).runs).Set("balls", inn.Batter(i).balls);
        return b;
    };
    v.Set("striker", batterJson(inn.Striker())).Set("nonStriker", batterJson(inn.NonStriker()));
    if (inn.CurrentBowler() >= 0) {
        const int b = inn.CurrentBowler();
        const BowlerCard& c = inn.Bowler(b);
        Json bj = Json::Object();
        bj.Set("id", b).Set("name", fld.xi[b].name).Set("kind", fld.xi[b].bowls == BowlingKind::Spin ? "spin" : "pace");
        bj.Set("overs", c.legalBalls / 6).Set("balls", c.legalBalls % 6).Set("runs", c.runs).Set("wickets", c.wickets);
        Json vars = Json::Array();
        for (const char* x : fld.xi[b].bowls == BowlingKind::Spin ? kSpinVariations : kPaceVariations)
            vars.Push(Json::String(x));
        bj.Set("variations", vars);
        v.Set("bowler", bj);
    }
    v.Set("reviewsLeft", Json::Array().Push(Json::Number(m.ReviewsRemaining(0))).Push(Json::Number(m.ReviewsRemaining(1))));
    // Choices for the captain's menus.
    if (side >= 0 && role == Role::Captain) {
        Json opts = Json::Object();
        if (p.what == "bowler" && p.side == side) {
            Json list = Json::Array();
            for (int i = 0; i < static_cast<int>(fld.xi.size()); ++i)
                if (inn.CanBowlNextOver(i) && i != fld.keeper) {
                    Json o = Json::Object();
                    o.Set("id", i).Set("name", fld.xi[i].name);
                    o.Set("kind", fld.xi[i].bowls == BowlingKind::Spin ? "spin" : "pace");
                    o.Set("overs", inn.Bowler(i).legalBalls / 6).Set("specialist", fld.xi[i].canBowl);
                    list.Push(o);
                }
            opts.Set("bowlers", list);
        }
        if ((p.what == "batter" || p.what == "openers") && p.side == side) {
            Json list = Json::Array();
            for (int i = 0; i < static_cast<int>(bat.xi.size()); ++i)
                if (!inn.Batter(i).batted) list.Push(Json::Object().Set("id", i).Set("name", bat.xi[i].name));
            opts.Set("batters", list);
        }
        Json presets = Json::Array();
        for (FieldPreset fp : AllPresets()) presets.Push(Json::String(ToString(fp)));
        opts.Set("fieldPresets", presets);
        v.Set("options", opts);
    }
    if (m.Phase() == MatchPhase::Complete) v.Set("result", m.Result().summary);
    return v;
}

void MatchHost::PublishStates(int64_t now) {
    for (const auto& d : hub_.Devices()) {
        if (!d.connected) continue;
        Json view = ViewFor(d.side, d.role, now);
        // Countdowns change every call: compare without them, and refresh
        // them about once a second.
        const bool countdown = view.Get("stageMsLeft") || view.Get("reviewMsLeft");
        Json key = view;
        if (countdown) key.Set("reviewMsLeft", 0).Set("stageMsLeft", 0);
        const std::string k = key.Dump();
        auto it = lastSent_.find(d.id);
        const bool same = it != lastSent_.end() && it->second == k;
        if (same && (!countdown || now - lastSentAt_[d.id] < 1000)) continue;
        lastSent_[d.id] = k;
        lastSentAt_[d.id] = now;
        hub_.SendTo(d.id, view);
    }
}

}  // namespace cricket::ctl

namespace cricket::ctl {

std::string MatchHost::RecordingJson() const {
    if (!session_->HasMatch()) return "";
    char cond[160];
    std::snprintf(cond, sizeof cond, "%s pitch, wind %.1f km/h%s", cfg_.conditions.pitchName.c_str(),
                  phys::Length(cfg_.conditions.wind) * 3.6, cfg_.conditions.dayNight ? ", day-night" : "");
    return recorder_.ToJsonLines(session_->GetMatch(), cond);
}

}  // namespace cricket::ctl
