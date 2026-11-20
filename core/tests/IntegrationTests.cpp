// End-to-end: every feature of the rules milestone at once, across many
// seeded matches - formats, DRS rulesets, appeals, rain, penalties, Test
// sessions, and a save + reload at a random moment followed by both copies
// playing on and finishing identically.
#include "Test.h"
#include "cricket/AutoCaptain.h"
#include "cricket/SaveGame.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

namespace {

struct World {
    QuickSim sim;
    std::mt19937_64 weather;
    World(uint64_t seed) : sim(seed * 13 + 1), weather(seed * 17 + 3) {}
    double U() { return std::uniform_real_distribution<double>(0, 1)(weather); }
    // One step of the "real world" around the server.
    void Step(MatchSession& s) {
        const Pending p = s.WaitingFor();
        if (p.what == "interval") {
            CHECK_OK(s.ResumeAfterInterval());
            return;
        }
        if (p.what != "delivery") return;
        const double r = U();
        if (r < 0.003) {
            CHECK_OK(s.RainDelay(static_cast<int>(10 + U() * 150)));
            return;
        }
        if (r < 0.004) {
            const int side = U() < 0.5 ? 0 : 1;
            CHECK_OK(s.AwardPenaltyRuns(side, 5, "ball hit a fielder's helmet"));
            return;
        }
        Delivery d = sim.Next(s.GetMatch());
        auto c = sim.MakeCase(s.GetMatch(), d);
        if (!c || !s.ResolveAppeal(*c)) CHECK_OK(s.ResolveDelivery(d));
    }
};

}  // namespace

TEST(integration_everything_with_save_and_reload_midstream) {
    const char* drs[] = {"icc", "custom:2:15:calls", "no-reviews", "no-tech", "custom:1:10:lose"};
    int finished = 0, rainy = 0, reloaded = 0;
    for (uint64_t seed = 1; seed <= 25; ++seed) {
        MatchRules rules = seed % 3 == 0 ? MatchRules::ODI() : seed % 3 == 1 ? MatchRules::Test(3) : MatchRules::T20();
        CHECK(DrsRules::FromName(drs[seed % 5], &rules.drs));
        auto all = FictionalTeams();
        MatchSession a(rules, {all[seed % 10], all[(seed + 3) % 10]}, seed * 1000);
        a.RunAi();
        World wa(seed);
        // Play to a random point, save, reload.
        std::mt19937_64 pick(seed);
        const int cut = std::uniform_int_distribution<int>(10, 600)(pick);
        for (int i = 0; i < cut && a.WaitingFor().what != "none"; ++i) wa.Step(a);
        SessionLoad l = LoadSession(SaveSession(a));
        CHECK_OK(l.status);
        if (!l.session) continue;
        CHECK_EQ(l.session->StateFingerprint(), a.StateFingerprint());
        ++reloaded;
        // Both copies play on with identical worlds.
        World wb = wa;
        for (int i = 0; i < 30000 && a.WaitingFor().what != "none"; ++i) wa.Step(a);
        for (int i = 0; i < 30000 && l.session->WaitingFor().what != "none"; ++i) wb.Step(*l.session);
        CHECK_EQ(l.session->StateFingerprint(), a.StateFingerprint());
        const Match& m = a.GetMatch();
        CHECK(m.Phase() == MatchPhase::Complete);
        finished += m.Phase() == MatchPhase::Complete;
        rainy += m.RainAffected();
        // The books always balance.
        for (const auto& inn : m.AllInnings()) {
            int bat = 0;
            for (int i = 0; i < 11; ++i) bat += inn.Batter(i).runs;
            CHECK_EQ(bat + inn.GetExtras().Total(), inn.Total());
            if (inn.MaxOvers() > 0) CHECK(inn.LegalBalls() <= inn.MaxOvers() * inn.Rules().ballsPerOver);
        }
        // And the finished match saves and loads too.
        MatchLoad ml = LoadMatch(SaveMatch(m));
        CHECK_OK(ml.status);
        if (ml.match) CHECK_EQ(ml.match->StateFingerprint(), m.StateFingerprint());
    }
    CHECK_EQ(finished, 25);
    CHECK_EQ(reloaded, 25);
    CHECK(rainy > 0);
}
