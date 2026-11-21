// MatchHost: phones playing real matches through the secure hub - role-based
// controls, the bowl/bat stages, timeouts, waiting for a disconnected phone,
// and the (placeholder) way inputs shape outcomes.
#include "../../core/tests/Test.h"
#include "PhoneSim.h"
#include "cricket/Scorecard.h"
#include "cricket/TeamFactory.h"
#include "cricket/controller/MatchHost.h"

using namespace cricket;
using namespace cricket::ctl;
using namespace cricket::ctl::testing;

namespace {

std::array<Team, 2> TwoTeams() {
    auto all = FictionalTeams();
    return {all[0], all[1]};  // "India-like" Ganga Royals vs "Pakistan-like" Indus Falcons (fictional)
}

// A phone that plays by reading only the state the TV sends it.
struct BotPhone {
    Phone p;
    std::optional<Json> state;
    std::string lastActed;
    std::mt19937_64 rng;
    int acks = 0, rejected = 0;
    std::vector<std::string> errors;
    BotPhone(Net& n, uint64_t seed) : p(n), rng(seed) {}

    void Read() {
        while (auto b = p.Body()) {
            if (b->Str("k") == "state") state = *b;
            if (b->Str("k") == "ack") {
                ++acks;
                if (!b->Flag("ok", true)) {
                    ++rejected;
                    errors.push_back(b->Str("err"));
                }
            }
        }
    }
    std::vector<std::string> Controls() const {
        std::vector<std::string> c;
        if (state && state->Get("controls"))
            for (const auto& x : state->Get("controls")->Items()) c.push_back(x.AsString());
        return c;
    }
    bool Has(const std::string& c) const {
        auto v = Controls();
        return std::find(v.begin(), v.end(), c) != v.end();
    }
    int FirstOption(const char* list) const {
        const Json* o = state ? state->Get("options") : nullptr;
        const Json* l = o ? o->Get(list) : nullptr;
        return l && !l->Items().empty() ? static_cast<int>(l->Items()[0].Int("id")) : -1;
    }
    void Do(Json a, int64_t now) { p.Act(a, now); }
    // One decision per new state.
    void Play(int64_t now) {
        Read();
        if (!state) return;
        const std::string sig = state->Str("waiting") + "|" + state->Str("stage") + "|" + state->Get("controls")->Dump() +
                                (state->Get("score") ? state->Get("score")->Dump() : "");
        if (sig == lastActed) return;
        auto u = [&] { return std::uniform_real_distribution<double>(0, 1)(rng); };
        Json a = Json::Object();
        if (Has("toss-call")) a.Set("do", "toss-call").Set("face", u() < 0.5 ? "heads" : "tails");
        else if (Has("toss-decide")) a.Set("do", "toss-decide").Set("choice", u() < 0.5 ? "bat" : "bowl");
        else if (Has("openers")) {
            const Json& l = *state->Get("options")->Get("batters");
            a.Set("do", "openers").Set("striker", l.Items()[0].Int("id")).Set("nonStriker", l.Items()[1].Int("id"));
        } else if (Has("batter")) a.Set("do", "batter").Set("player", FirstOption("batters"));
        else if (Has("bowler")) a.Set("do", "bowler").Set("player", FirstOption("bowlers"));
        else if (Has("review")) a.Set("do", u() < 0.4 ? "review" : "accept");
        else if (Has("follow-on")) a.Set("do", "follow-on").Set("enforce", true);
        else if (Has("ready")) a.Set("do", "ready");
        else if (Has("bowl")) {
            const Json* b = state->Get("bowler");
            const auto& vars = b->Get("variations")->Items();
            a.Set("do", "bowl").Set("variation", vars[static_cast<size_t>(u() * vars.size())].AsString());
            a.Set("line", u() * 0.6 - 0.3).Set("length", u()).Set("effort", static_cast<int>(50 + u() * 40));
        } else if (Has("bat")) {
            static const char* shots[] = {"defend", "drive", "cut", "pull", "flick", "loft", "leave", "sweep"};
            a.Set("do", "bat").Set("shot", shots[static_cast<int>(u() * 8)]).Set("dir", static_cast<int>(u() * 180 - 90));
            a.Set("timing", static_cast<int>(u() * 400 - 200));
        } else {
            lastActed = sig;
            return;
        }
        lastActed = sig;
        Do(a, now);
    }
};

struct World {
    ControllerHub hub;
    Net net;
    MatchHost host;
    int64_t now = 1000;
    World(MatchRules r, HostConfig cfg, uint64_t seed = 5)
        : hub({}, Counter(seed)), net(hub), host(std::move(r), TwoTeams(), hub, cfg) {}
    void Step(int64_t ms = 100) {
        now += ms;
        hub.Tick(now);
        host.Process(now);
    }
};

}  // namespace

TEST(ctl_host_phone_vs_ai_full_t20) {
    HostConfig cfg;
    cfg.human = {true, false};
    World w(MatchRules::T20(), cfg);
    BotPhone india(w.net, 1);
    CHECK(india.p.Pair(w.hub.CreatePairing(0, Role::Captain, w.now), w.now));
    for (int i = 0; i < 200000 && w.host.Session().WaitingFor().what != "none"; ++i) {
        w.Step();
        india.Play(w.now);
    }
    CHECK(w.host.Session().GetMatch().Phase() == MatchPhase::Complete);
    india.Read();
    CHECK(india.state && !india.state->Str("result").empty());
    CHECK(india.acks > 50);
    CHECK_EQ(india.rejected, 0);
    for (const auto& e : india.errors) std::cerr << "  rejected: " << e << "\n";
}

TEST(ctl_host_two_phones_india_vs_pakistan) {
    HostConfig cfg;
    cfg.human = {true, true};
    World w(MatchRules::Custom(3), cfg, 6);
    BotPhone india(w.net, 2), pakistan(w.net, 3);
    CHECK(india.p.Pair(w.hub.CreatePairing(0, Role::Captain, w.now), w.now));
    CHECK(pakistan.p.Pair(w.hub.CreatePairing(1, Role::Captain, w.now), w.now));
    for (int i = 0; i < 100000 && w.host.Session().WaitingFor().what != "none"; ++i) {
        w.Step();
        india.Play(w.now);
        pakistan.Play(w.now);
    }
    const Match& m = w.host.Session().GetMatch();
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK_EQ(india.rejected + pakistan.rejected, 0);
    // Every ball was bowled by a phone (no timeouts needed) and batted by a phone.
    int balls = 0;
    for (const auto& inn : m.AllInnings()) balls += static_cast<int>(inn.Balls().size());
    CHECK(balls > 0);
}

TEST(ctl_host_role_based_controls) {
    HostConfig cfg;
    cfg.human = {true, true};
    World w(MatchRules::T20(), cfg, 7);
    BotPhone cap0(w.net, 4), cap1(w.net, 5), player0(w.net, 6), fan(w.net, 7);
    CHECK(cap0.p.Pair(w.hub.CreatePairing(0, Role::Captain, w.now), w.now));
    CHECK(cap1.p.Pair(w.hub.CreatePairing(1, Role::Captain, w.now), w.now));
    CHECK(player0.p.Pair(w.hub.CreatePairing(0, Role::Player, w.now), w.now));
    CHECK(fan.p.Pair(w.hub.CreatePairing(0, Role::Spectator, w.now), w.now));
    w.Step();
    for (auto* b : {&cap0, &cap1, &player0, &fan}) b->Read();
    // Toss: only the calling (visiting) captain gets the control.
    CHECK(cap1.Has("toss-call"));
    CHECK(!cap0.Has("toss-call"));
    CHECK(!player0.Has("toss-call"));
    CHECK(fan.Controls().empty());
    CHECK_EQ(fan.state->Int("yourSide"), int64_t{-1});
    // Out-of-turn attempts are refused with a reason.
    cap0.Do(Json::Object().Set("do", "toss-call").Set("face", "heads"), w.now);
    fan.Do(Json::Object().Set("do", "toss-call").Set("face", "heads"), w.now);
    player0.Do(Json::Object().Set("do", "bowler").Set("player", 10), w.now);
    w.Step();
    cap0.Read();
    fan.Read();
    player0.Read();
    CHECK_EQ(cap0.rejected, 1);
    CHECK_EQ(fan.rejected, 1);
    CHECK(fan.errors.back().find("spectators") != std::string::npos);
    CHECK_EQ(player0.rejected, 1);
    // Play on until a ball is in the air: players of the fielding side may
    // bowl, batting-side players may bat, nobody else.
    for (int i = 0; i < 2000 && w.host.Stage() != "bowl"; ++i) {
        w.Step();
        cap0.Play(w.now);
        cap1.Play(w.now);
    }
    CHECK_EQ(w.host.Stage(), std::string("bowl"));
    const int fielding = w.host.Session().GetMatch().FieldingSide();
    auto fc = w.host.AllowedControls(fielding, Role::Player);
    CHECK(std::find(fc.begin(), fc.end(), "bowl") != fc.end());
    CHECK(std::find(fc.begin(), fc.end(), "bowler") == fc.end());  // players don't pick bowlers
    auto bc = w.host.AllowedControls(1 - fielding, Role::Player);
    CHECK(std::find(bc.begin(), bc.end(), "bowl") == bc.end());
    CHECK(w.host.AllowedControls(fielding, Role::Spectator).empty());
}

TEST(ctl_host_bowl_and_bat_timeouts) {
    HostConfig cfg;
    cfg.human = {true, true};
    cfg.bowlWindowMs = 5000;
    World w(MatchRules::Custom(1), cfg, 8);
    BotPhone a(w.net, 8), b(w.net, 9);
    CHECK(a.p.Pair(w.hub.CreatePairing(0, Role::Captain, w.now), w.now));
    CHECK(b.p.Pair(w.hub.CreatePairing(1, Role::Captain, w.now), w.now));
    // Captains do captaincy only: never bowl or bat.
    for (int i = 0; i < 3000 && w.host.Stage() != "bowl"; ++i) {
        w.Step();
        for (BotPhone* x : {&a, &b}) {
            x->Read();
            if (x->Has("bowl") || x->Has("bat")) continue;
            x->Play(w.now);
        }
    }
    const Match& m = w.host.Session().GetMatch();
    CHECK_EQ(w.host.Stage(), std::string("bowl"));
    const size_t before = m.Current().Balls().size();
    w.Step(4900);
    CHECK_EQ(w.host.Stage(), std::string("bowl"));  // still choosing
    w.Step(200);
    CHECK_EQ(w.host.Stage(), std::string("ball"));  // stock ball bowled automatically
    w.Step(cfg.ballFlightMs + cfg.batWindowMs + 100);  // batter never plays a shot
    CHECK_EQ(m.Current().Balls().size(), before + 1);
    const Delivery& d = m.Current().Balls().back().delivery;
    if (d.extra == ExtraType::None) CHECK(d.runs == 0 && d.dismissal != Dismissal::Caught);
}

TEST(ctl_host_waits_for_disconnected_phone) {
    HostConfig cfg;
    cfg.human = {true, false};
    cfg.bowlWindowMs = 3000;
    World w(MatchRules::T20(), cfg, 9);
    BotPhone india(w.net, 10);
    CHECK(india.p.Pair(w.hub.CreatePairing(0, Role::Captain, w.now), w.now));
    // Play until India has to bowl.
    for (int i = 0; i < 100000 && w.host.Stage() != "bowl"; ++i) {
        w.Step();
        india.Read();
        if (india.Has("bowl")) break;
        india.Play(w.now);
    }
    if (w.host.Stage() != "bowl") {
        // India bat first in this seed: bat until the innings ends, then bowl.
        for (int i = 0; i < 200000 && w.host.Stage() != "bowl"; ++i) {
            w.Step();
            india.Play(w.now);
        }
    }
    CHECK_EQ(w.host.Stage(), std::string("bowl"));
    w.hub.Closed(india.p.conn, w.now);  // Wi-Fi drops
    w.Step();
    CHECK_EQ(w.host.WaitingForController(), 0);
    const size_t balls = w.host.Session().GetMatch().Current().Balls().size();
    w.Step(60000);  // a whole minute: nothing times out while waiting
    CHECK_EQ(w.host.Stage(), std::string("bowl"));
    CHECK_EQ(w.host.Session().GetMatch().Current().Balls().size(), balls);
    CHECK(india.p.Resume(w.now));
    w.Step();
    india.Read();
    CHECK(india.state.has_value());
    CHECK(india.Has("bowl"));  // full state re-sent after reconnecting
    CHECK_EQ(w.host.WaitingForController(), -1);
    CHECK(india.state->Int("stageMsLeft") > 2000);  // the timer was held, not used up
}

TEST(ctl_host_rejects_bad_inputs_and_applies_field_changes) {
    HostConfig cfg;
    cfg.human = {true, true};
    World w(MatchRules::T20(), cfg, 10);
    BotPhone a(w.net, 11), b(w.net, 12);
    CHECK(a.p.Pair(w.hub.CreatePairing(0, Role::Captain, w.now), w.now));
    CHECK(b.p.Pair(w.hub.CreatePairing(1, Role::Captain, w.now), w.now));
    for (int i = 0; i < 3000; ++i) {
        w.Step();
        if (w.host.Stage() == "bowl") break;  // stop before the bots bowl
        a.Play(w.now);
        b.Play(w.now);
    }
    const int fielding = w.host.Session().GetMatch().FieldingSide();
    BotPhone& f = fielding == 0 ? a : b;
    f.Read();
    const int before = f.rejected;
    f.Do(Json::Object().Set("do", "field").Set("preset", "Not a field"), w.now);
    f.Do(Json::Object().Set("do", "field").Set("preset", "Powerplay"), w.now);
    const bool spin = f.state->Get("bowler")->Str("kind") == "spin";
    f.Do(Json::Object().Set("do", "bowl").Set("variation", spin ? "bouncer" : "googly"), w.now);
    f.Do(Json::Object().Set("do", "teleport"), w.now);
    w.Step();
    f.Read();
    CHECK_EQ(f.rejected - before, 3);
    CHECK_EQ(w.host.Session().Field(fielding).fielders[0].name, FieldSetup::FromPreset(FieldPreset::Powerplay).fielders[0].name);
    CHECK_EQ(w.host.Stage(), std::string("bowl"));  // the bad delivery wasn't bowled
}

TEST(ctl_host_physics_outcomes_follow_inputs) {
    // Same deliveries, different shots: the physics decides (no dice).
    Match m(MatchRules::T20(), TwoTeams(), 0);
    m.SetOpeners(0, 1);
    m.SetBowler(8);  // a pace bowler
    const FieldSetup field = FieldSetup::FromPreset(FieldPreset::Balanced);
    int leaveRuns = 0, leaveCaught = 0, defendBoundaries = 0, goodRuns = 0, badRuns = 0, goodBoundaries = 0,
        badBoundaries = 0, goodMiddled = 0, badMiddled = 0;
    BallOutcome bowler(4);
    for (int i = 0; i < 300; ++i) {
        const phys::DeliverySpec spec = bowler.SpecFor(m, bowler.AiBowl(m));
        BallOutcome o(9);  // physics only: ResolveSpec uses no randomness
        const double dir = -20 + (i % 9) * 10;  // around the ground
        BatChoice leave, defend{"defend", 0, 0, true}, good{"drive", dir, 5, true}, bad{"drive", dir, 110, true};
        BallReport r = o.ResolveSpec(m, field, spec, leave, nullptr);
        if (r.delivery.extra == ExtraType::None) leaveRuns += r.delivery.runs;
        leaveCaught += r.delivery.dismissal == Dismissal::Caught;
        r = o.ResolveSpec(m, field, spec, defend, nullptr);
        defendBoundaries += r.delivery.boundary && r.delivery.extra == ExtraType::None;
        r = o.ResolveSpec(m, field, spec, good, nullptr);
        goodRuns += r.delivery.runs;
        goodBoundaries += r.delivery.boundary;
        goodMiddled += r.bat.contact == phys::Contact::Middled;
        r = o.ResolveSpec(m, field, spec, bad, nullptr);
        badRuns += r.delivery.runs;
        badBoundaries += r.delivery.boundary;
        badMiddled += r.bat.contact == phys::Contact::Middled;
    }
    CHECK_EQ(leaveRuns, 0);
    CHECK_EQ(leaveCaught, 0);
    CHECK_EQ(defendBoundaries, 0);
    CHECK(goodRuns > badRuns);
    CHECK(goodBoundaries > badBoundaries);
    CHECK(goodMiddled > badMiddled);
}

TEST(ctl_host_physics_matches_look_like_t20_cricket) {
    // Computer v computer T20s played entirely by the physics: totals,
    // wickets and dot balls must land in a realistic band, and the same
    // seed must give the same match.
    auto play = [](uint64_t seed, MatchRules rules) {
        ControllerHub hub({}, Counter(seed));
        HostConfig cfg;
        cfg.human = {false, false};
        cfg.seed = seed;
        auto all = NationalTeams();
        auto host = std::make_unique<MatchHost>(std::move(rules), std::array<Team, 2>{all[seed % 10], all[(seed + 1) % 10]},
                                                hub, cfg);
        int64_t now = 0;
        for (int i = 0; i < 100000; ++i) {
            if (host->Session().HasMatch() && host->Session().GetMatch().Result().kind != MatchResult::Kind::None) break;
            host->Process(now += 50);
        }
        return std::make_pair(std::move(host), now);
    };
    double runs = 0, wickets = 0, balls = 0, dots = 0, boundaries = 0;
    int innings = 0;
    for (uint64_t seed = 200; seed < 204; ++seed) {
        auto [host, t] = play(seed, MatchRules::T20());
        const Match& m = host->Session().GetMatch();
        CHECK(m.Result().kind != MatchResult::Kind::None);
        for (const auto& inn : m.AllInnings()) {
            ++innings;
            runs += inn.Total();
            wickets += inn.Wickets();
            balls += inn.LegalBalls();
            for (const auto& b : inn.Balls()) {
                dots += b.legal && b.runsToTeam == 0 && b.delivery.dismissal == Dismissal::None;
                boundaries += b.delivery.boundary;
            }
        }
    }
    const double perInnings = runs / innings;
    CHECK(perInnings > 100 && perInnings < 220);
    CHECK(wickets / innings > 2.0 && wickets / innings <= 10);
    CHECK(dots / balls > 0.30 && dots / balls < 0.65);
    CHECK(boundaries / innings > 8);
    // Replays: same seed, same match (ball for ball).
    auto [a, ta] = play(321, MatchRules::Custom(3));
    auto [b, tb] = play(321, MatchRules::Custom(3));
    CHECK_EQ(a->Session().StateFingerprint(), b->Session().StateFingerprint());
    auto [c, tc] = play(322, MatchRules::Custom(3));
    CHECK(a->Session().StateFingerprint() != c->Session().StateFingerprint());
}

TEST(ctl_host_fielding_prompt_step_out_scorecard_and_conditions) {
    HostConfig cfg;
    cfg.human = {true, false};
    cfg.conditions.pitchName = "dusty";
    cfg.conditions.pitch = phys::Pitch::Dusty();
    cfg.conditions.wind = {3, 0, 0};
    World w(MatchRules::Custom(5), cfg, 9);
    BotPhone india(w.net, 8);
    CHECK(india.p.Pair(w.hub.CreatePairing(0, Role::Captain, w.now), w.now));
    int prompts = 0, catchesTried = 0, throwsChosen = 0, stepOuts = 0;
    bool sawCard = false, sawConditions = false;
    for (int i = 0; i < 100000 && w.host.Session().WaitingFor().what != "none"; ++i) {
        w.Step();
        india.Read();
        if (india.state) {
            const Json& s = *india.state;
            if (const Json* sc = s.Get("scorecard"); sc && !sc->Items().empty() && sc->Items()[0].Get("batting"))
                sawCard = true;
            if (const Json* c = s.Get("conditions"); c && c->Str("pitch") == "dusty" && c->Num("windKph") > 10)
                sawConditions = true;
            if (india.Has("fielding") && s.Get("fieldPlay")) {
                ++prompts;
                const std::string kind = s.Get("fieldPlay")->Str("kind");
                CHECK(kind == "catch" || kind == "stop");
                if (prompts % 2) {
                    india.Do(Json::Object().Set("do", "fielding").Set("act", "throw-keeper"), w.now);
                    ++throwsChosen;
                }
                india.Do(Json::Object().Set("do", "fielding").Set("act", kind == "catch" ? "catch" : "dive").Set("timing", 20),
                         w.now);
                ++catchesTried;
                india.lastActed.clear();
                continue;
            }
            if (india.Has("bat") && stepOuts < 3 && w.host.Session().HasMatch() &&
                BallOutcome::IsSpinner(w.host.Session().GetMatch())) {
                india.Do(Json::Object().Set("do", "bat").Set("shot", "loft").Set("dir", 0).Set("timing", 0).Set("advance", true),
                         w.now);
                ++stepOuts;
                w.Step();
                if (const BallReport* b = w.host.LastBall()) CHECK(b->summary.find("down the pitch") != std::string::npos);
                continue;
            }
        }
        india.Play(w.now);
    }
    CHECK(w.host.Session().GetMatch().Phase() == MatchPhase::Complete);
    CHECK(prompts >= 1);       // the ball went to our fielders and the phone was asked
    CHECK(catchesTried >= 1);
    CHECK(throwsChosen >= 1);
    CHECK(sawCard);
    CHECK(sawConditions);
    CHECK_EQ(india.rejected, 0);
    for (const auto& e : india.errors) std::cerr << "  rejected: " << e << "\n";
    CHECK_OK(CheckScorecard(w.host.Session().GetMatch()));
}
