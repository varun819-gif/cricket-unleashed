// MatchHost: phones playing real matches through the secure hub - role-based
// controls, the bowl/bat stages, timeouts, waiting for a disconnected phone,
// and the (placeholder) way inputs shape outcomes.
#include "../../core/tests/Test.h"
#include "PhoneSim.h"
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

TEST(ctl_host_placeholder_outcomes_follow_inputs) {
    // Statistical check of the PLACEHOLDER resolver (not physics).
    Match m(MatchRules::T20(), TwoTeams(), 0);
    m.SetOpeners(0, 1);
    m.SetBowler(10);
    QuickSim sim(3);
    std::mt19937_64 rng(4);
    BowlInput bowl;
    int leaveRuns = 0, leaveCaught = 0, defendBoundaries = 0, goodBoundaries = 0, badBoundaries = 0, goodCaught = 0,
        badCaught = 0;
    for (int i = 0; i < 4000; ++i) {
        BatInput leave{"leave", 0, 0, true}, defend{"defend", 0, 0, true};
        BatInput good{"drive", 20, 10, true}, bad{"drive", 20, 400, true};
        Delivery d = MatchHost::ResolveWithInputs(m, sim, bowl, leave, rng);
        if (d.extra == ExtraType::None) leaveRuns += d.runs;
        leaveCaught += d.dismissal == Dismissal::Caught;
        d = MatchHost::ResolveWithInputs(m, sim, bowl, defend, rng);
        defendBoundaries += d.boundary && d.extra == ExtraType::None;
        d = MatchHost::ResolveWithInputs(m, sim, bowl, good, rng);
        goodBoundaries += d.boundary;
        goodCaught += d.dismissal == Dismissal::Caught;
        d = MatchHost::ResolveWithInputs(m, sim, bowl, bad, rng);
        badBoundaries += d.boundary;
        badCaught += d.dismissal == Dismissal::Caught;
    }
    CHECK_EQ(leaveRuns, 0);
    CHECK_EQ(leaveCaught, 0);
    CHECK_EQ(defendBoundaries, 0);
    CHECK(goodBoundaries > badBoundaries * 2);
    CHECK(badCaught > goodCaught);
}
