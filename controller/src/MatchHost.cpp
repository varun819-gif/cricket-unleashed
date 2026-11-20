#include "cricket/controller/MatchHost.h"

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
      rng_(cfg.seed * 17 + 3) {
    for (int s = 0; s < 2; ++s)
        if (cfg_.human[s]) session_->Join(PlayerId(s), s);
    session_->RunAi();  // an AI side may need to call the toss
}

MatchHost::MatchHost(std::unique_ptr<MatchSession> recovered, ControlChannel& hub, HostConfig cfg, uint64_t resumeSalt)
    : hub_(hub),
      cfg_(cfg),
      session_(std::move(recovered)),
      sim_(cfg.seed * 31 + 7 + resumeSalt),
      rng_(cfg.seed * 17 + 3 + resumeSalt) {
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
            out.push_back("review");
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
        if (stage_ == "ball" && !batting) out.push_back("fielding");
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
        bat_ = b;
        ResolveBall(now);
        return Status::Ok();
    }
    if (what == "fielding") {
        const std::string f = a.Str("act");
        if (!OneOf(f, {"dive-left", "dive-right", "catch", "throw-keeper", "throw-bowler"}))
            return Status::Err("unknown fielding action");
        fieldingCalls_.push_back(f);
        return Status::Ok();
    }
    Status s = Captaincy(act.side, what, a, now);
    if (s && session_->WaitingFor().what != "delivery") stage_.clear();
    return s;
}

void MatchHost::ReleaseBall(int64_t now) {
    stage_ = "ball";
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

Delivery MatchHost::ResolveWithInputs(const Match& m, QuickSim& sim, const BowlInput& bowl, const BatInput& bat,
                                      std::mt19937_64& rng) {
    // PLACEHOLDER outcome model. Start from the statistical simulation, then
    // let the players' choices push it around in believable directions.
    Delivery d = sim.Next(m);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (d.extra == ExtraType::Wide || d.extra == ExtraType::NoBall) return d;
    // A wildly inaccurate delivery is called wide.
    if (std::fabs(bowl.line) > 0.9 && bowl.effort > 85 && u(rng) < 0.5) {
        Delivery w;
        w.extra = ExtraType::Wide;
        return w;
    }
    const bool freeHit = m.Current().NextBallIsFreeHit();
    const double quality = bat.provided ? std::clamp(1.0 - std::abs(bat.timingMs) / 250.0, 0.0, 1.0) : 0.0;
    const bool out = d.dismissal != Dismissal::None;
    auto noRuns = [&] {
        d.runs = 0;
        d.boundary = false;
        d.shortRuns = 0;
    };
    if (!bat.provided || bat.shot == "leave") {
        // No shot: only bowled / LBW can get you; no runs off the bat.
        if (out && d.dismissal != Dismissal::Bowled && d.dismissal != Dismissal::Lbw) d.dismissal = Dismissal::None;
        if (d.extra == ExtraType::None) noRuns();
        return d;
    }
    if (bat.shot == "defend") {
        if (d.dismissal == Dismissal::Caught && u(rng) < 0.6) d.dismissal = Dismissal::None;
        if (d.dismissal == Dismissal::None && d.extra == ExtraType::None) {
            d.runs = std::min(d.runs, 1);
            d.boundary = false;
        }
        return d;
    }
    // Attacking shots: timing decides.
    if (!out && d.extra == ExtraType::None) {
        if (quality > 0.8 && u(rng) < 0.35) {
            if (d.boundary && d.runs == 4 && bat.shot == "loft") d.runs = 6;
            else if (!d.boundary && d.runs <= 2) {
                d.runs = 4;
                d.boundary = true;
            }
        } else if (quality < 0.3 && !freeHit && u(rng) < (bat.shot == "loft" ? 0.15 : 0.07)) {
            noRuns();
            d.dismissal = Dismissal::Caught;  // mistimed: in the air
        } else if (quality < 0.3) {
            if (d.boundary) {
                d.boundary = false;
                d.runs = 1;
            }
        }
    }
    if (bowl.variation == "yorker" && bowl.length < 0.15 && d.boundary && u(rng) < 0.4) {
        d.boundary = false;
        d.runs = 1;
    }
    return d;
}

void MatchHost::ResolveBall(int64_t now) {
    if (stage_ != "ball") return;
    const Match& m = session_->GetMatch();
    Delivery d = ResolveWithInputs(m, sim_, bowl_, bat_, rng_);
    stage_.clear();
    auto c = sim_.MakeCase(m, d);
    if (!c || !session_->ResolveAppeal(*c)) session_->ResolveDelivery(d);
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
            // AI bowler: a sensible stock delivery with a little variety.
            std::uniform_real_distribution<double> u(0.0, 1.0);
            bowl_ = {};
            bowl_.line = (u(rng_) - 0.6) * 0.4;
            bowl_.length = 0.3 + u(rng_) * 0.4;
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
    for (const auto& e : session_->EventsSince(lastEvent_))
        if (e.kind != "join") message_ = e.text;  // the latest thing that happened
    lastEvent_ = session_->LastEventId();
    PublishStates(now);
}

Json MatchHost::ViewFor(int side, Role role, int64_t now) const {
    Json v = Json::Object();
    v.Set("k", "state");
    const Pending p = session_->WaitingFor();
    v.Set("waiting", p.what).Set("waitingSide", p.side);
    v.Set("yourSide", side).Set("role", ToString(role));
    v.Set("stage", stage_);
    v.Set("placeholderOutcomes", true);  // ball outcomes are simulated until physics exists
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
    if (stage_ == "bowl" || stage_ == "ball") v.Set("stageMsLeft", std::max<int64_t>(0, deadline_ - now));
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
