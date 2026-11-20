// Stage C: rain interruptions, revised targets with the NON-OFFICIAL rain
// rule, minimum overs, no-result, par-score results, Test time lost.
#include <cmath>

#include "Test.h"
#include "cricket/AutoCaptain.h"
#include "cricket/Match.h"
#include "cricket/MatchSession.h"
#include "cricket/RainRule.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

namespace {

std::array<Team, 2> TwoTeams() {
    auto all = FictionalTeams();
    return {all[0], all[1]};
}

Delivery Runs(int r, bool boundary = false) {
    Delivery d;
    d.runs = r;
    d.boundary = boundary;
    return d;
}

Delivery Bowled() {
    Delivery d;
    d.dismissal = Dismissal::Bowled;
    return d;
}

void Feed(Match& m, const Delivery& d, int balls) {
    for (int i = 0; i < balls && m.Phase() == MatchPhase::InProgress; ++i) {
        Innings& inn = m.Current();
        if (inn.State() == InningsState::AwaitingOpeners) m.SetOpeners(0, 1);
        if (inn.State() == InningsState::AwaitingBatter)
            for (int b = 0; b < 11; ++b)
                if (!inn.Batter(b).batted) {
                    m.SetNextBatter(b);
                    break;
                }
        if (m.Current().State() == InningsState::AwaitingBowler)
            for (int b = 10; b >= 0; --b)
                if (b != 4 && m.Current().CanBowlNextOver(b)) {
                    m.SetBowler(b);
                    break;
                }
        CHECK_OK(m.Bowl(d));
    }
}

bool Near(double a, double b, double eps = 0.05) { return std::fabs(a - b) < eps; }

bool LastIs(const Match& m, IntervalKind k) { return !m.Intervals().empty() && m.Intervals().back().kind == k; }

}  // namespace

// ---------------------------------------------------------------- the rain rule itself

TEST(rain_resource_curve) {
    CHECK(Near(RainResources(50, 0), 100.0, 1e-9));
    const double anchors[10] = {100, 93, 85, 75, 63, 50, 36, 23, 12, 5};
    for (int w = 0; w < 10; ++w) CHECK(Near(RainResources(50, w), anchors[w]));
    CHECK_EQ(RainResources(0, 0), 0.0);
    CHECK_EQ(RainResources(20, 10), 0.0);
    // More overs never means fewer resources; more wickets never means more.
    for (int w = 0; w < 10; ++w)
        for (double u = 0; u < 50; u += 0.5) CHECK(RainResources(u + 0.5, w) >= RainResources(u, w));
    for (double u = 1; u <= 50; u += 1)
        for (int w = 0; w < 9; ++w) CHECK(RainResources(u, w + 1) <= RainResources(u, w));
    CHECK(Near(RainResources(20, 0), 56.4, 0.1));  // a 20-over innings has ~56% of 50-over resources
}

TEST(rain_target_formula) {
    CHECK_EQ(RainTarget(250, 80.0, 80.0, 245), 251);           // equal resources: S + 1
    CHECK_EQ(RainTarget(250, 100.0, 75.0, 245), 188);          // 250 x 0.75 = 187.5 -> 188
    CHECK_EQ(RainTarget(200, 80.0, 90.0, 245), 225);           // 200 + 245 x 10% = 224.5 -> 225
    CHECK_EQ(RainTarget(0, 50.0, 25.0, 245), 1);
    CHECK(Near(RainPar(300, 100, 50, 245), 150.0, 1e-9));
}

// ---------------------------------------------------------------- limited overs

TEST(rain_before_the_start_equal_overs_target_s_plus_one) {
    Match m(MatchRules::ODI(), TwoTeams(), 0);
    CHECK_OK(m.ReduceOvers(40));  // before a ball: both sides 40 overs
    CHECK_EQ(m.Current().MaxOvers(), 40);
    CHECK_EQ(m.Current().Rules().maxOversPerBowler, 8);
    CHECK_EQ(m.PlannedOvers(1), 40);
    Feed(m, Runs(1), 40 * 6);  // 240
    CHECK_EQ(m.AllInnings().size(), size_t{2});
    CHECK_EQ(m.Current().MaxOvers(), 40);
    CHECK_EQ(*m.Current().Target(), 241);
}

TEST(rain_first_innings_interrupted_target_uses_extra_resources) {
    Match m(MatchRules::ODI(), TwoTeams(), 0);
    Feed(m, Runs(1), 20 * 6);
    Feed(m, Bowled(), 2);  // 122/2 after 20.2 overs
    Feed(m, Runs(1), 4);   // 20.6 -> 21 overs, 126/2
    CHECK_OK(m.ReduceOvers(40));
    const double r1 = 100.0 - (RainResources(29, 2) - RainResources(19, 2));
    CHECK(Near(m.RainResourcesFor(0), r1, 1e-6));
    Feed(m, Runs(1), 19 * 6);  // innings 1 ends at 40 overs: 124 + 114 = 238/2
    CHECK_EQ(m.AllInnings()[0].Total(), 238);
    // Side 2 has 40 overs from the start: more resources than side 1 had.
    const double r2 = RainResources(40, 0);
    CHECK(r2 > r1);
    CHECK_EQ(*m.Current().Target(), RainTarget(238, r1, r2, 245));
    CHECK(*m.Current().Target() > 239);
}

TEST(rain_second_innings_interrupted_target_drops) {
    Match m(MatchRules::ODI(), TwoTeams(), 0);
    Feed(m, Runs(1), 50 * 6);  // 300
    CHECK_EQ(*m.Current().Target(), 301);
    Feed(m, Runs(1), 59);
    Feed(m, Bowled(), 1);      // 59/1 after 10 overs
    CHECK_OK(m.ReduceOvers(30));
    const double r2 = 100.0 - (RainResources(40, 1) - RainResources(20, 1));
    CHECK_EQ(*m.Current().Target(), RainTarget(300, 100.0, r2, 245));
    CHECK(*m.Current().Target() < 301);
    CHECK_EQ(m.Current().Rules().maxOversPerBowler, 6);
    CHECK(m.RainAffected());
}

TEST(rain_revised_target_already_reached_wins_at_once) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    Feed(m, Runs(1), 20 * 6);  // 120
    Feed(m, Runs(4, true), 18);  // 72 off 3 overs
    CHECK_OK(m.ReduceOvers(5));  // 5-over chase: target well under 72
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK(m.Result().kind == MatchResult::Kind::Win);
    CHECK_EQ(m.Result().winner, 1);
    CHECK(m.Result().summary.find("(rain rule)") != std::string::npos);
}

TEST(rain_chase_cut_to_overs_bowled_decides_on_target) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    Feed(m, Runs(1), 20 * 6);  // 120
    Feed(m, Runs(1), 8 * 6);   // 48 off 8 overs
    CHECK_OK(m.ReduceOvers(8));  // no more play: innings ends now
    CHECK(m.Phase() == MatchPhase::Complete);
    const double r2 = RainResources(20, 0) - RainResources(12, 0);
    const int target = RainTarget(120, RainResources(20, 0), r2, 275);
    CHECK_EQ(target, 41);  // 120 x 18.8 / 56.4 = 40.0 -> 41
    if (48 >= target) CHECK_EQ(m.Result().winner, 1);
    else if (48 == target - 1) CHECK(m.Result().kind == MatchResult::Kind::Tie);
    else {
        CHECK_EQ(m.Result().winner, 0);
        CHECK_EQ(m.Result().value, target - 1 - 48);
    }
    CHECK(m.Result().summary.find("(rain rule)") != std::string::npos);
}

TEST(rain_minimum_overs_no_result) {
    {
        Match m(MatchRules::T20(), TwoTeams(), 0);
        Feed(m, Runs(1), 20 * 6);
        CHECK_OK(m.ReduceOvers(4));  // T20 needs 5 overs for the chasing side
        CHECK(m.Result().kind == MatchResult::Kind::NoResult);
    }
    {
        Match m(MatchRules::ODI(), TwoTeams(), 0);
        CHECK_OK(m.ReduceOvers(19));
        CHECK(m.Result().kind == MatchResult::Kind::NoResult);
    }
    {
        Match m(MatchRules::ODI(), TwoTeams(), 0);
        CHECK_OK(m.ReduceOvers(20));  // exactly the minimum is fine
        CHECK(m.Phase() == MatchPhase::InProgress);
    }
}

TEST(rain_abandon_limited_overs) {
    {
        Match m(MatchRules::T20(), TwoTeams(), 0);
        Feed(m, Runs(1), 30);
        m.Abandon();  // first innings: no result
        CHECK(m.Result().kind == MatchResult::Kind::NoResult);
    }
    {
        Match m(MatchRules::T20(), TwoTeams(), 0);
        Feed(m, Runs(1), 20 * 6);
        Feed(m, Runs(1), 4 * 6 + 5);  // 4.5 overs: short of the 5-over minimum
        m.Abandon();
        CHECK(m.Result().kind == MatchResult::Kind::NoResult);
    }
    // Ahead of par after 6 overs: chasing side wins.
    {
        Match m(MatchRules::T20(), TwoTeams(), 0);
        Feed(m, Runs(1), 20 * 6);       // 120
        Feed(m, Runs(4, true), 36);     // fours: the 121 target is reached within 6 overs
        CHECK(m.Phase() == MatchPhase::Complete);  // won outright, not by rain
    }
    {
        Match m(MatchRules::T20(), TwoTeams(), 0);
        Feed(m, Runs(1), 20 * 6);       // 120
        Feed(m, Runs(2), 36);           // 72 off 6 overs, 0 down: well ahead of par
        const double par = m.ParScoreNow();
        CHECK(par > 0 && par < 72);
        m.Abandon();
        CHECK(m.Result().kind == MatchResult::Kind::Win);
        CHECK_EQ(m.Result().winner, 1);
        CHECK(m.Result().summary.find("(rain rule)") != std::string::npos);
    }
    {
        Match m(MatchRules::T20(), TwoTeams(), 0);
        Feed(m, Runs(1), 20 * 6);       // 120
        Feed(m, Runs(0), 30);
        Feed(m, Bowled(), 6);           // 0/6 off 6 overs: behind par
        const int par = static_cast<int>(std::floor(m.ParScoreNow()));
        m.Abandon();
        CHECK_EQ(m.Result().winner, 0);
        CHECK_EQ(m.Result().value, par);  // lost by par - 0 runs
    }
}

TEST(rain_delay_buffer_and_equal_cut) {
    Match m(MatchRules::ODI(), TwoTeams(), 0);
    Feed(m, Runs(1), 60);
    CHECK_OK(m.RainDelay(25));  // inside the 30-minute buffer
    CHECK_EQ(m.Current().MaxOvers(), 50);
    CHECK_EQ(m.RainLog().back().bufferUsed, 25);
    CHECK_OK(m.RainDelay(20));  // 5 more absorbed, 15 net -> ceil(15 / 4.2) = 4 overs lost
    CHECK_EQ(m.RainLog().back().oversLost, 4);
    CHECK_EQ(m.Current().MaxOvers(), 48);
    CHECK_EQ(m.PlannedOvers(1), 48);
    CHECK_ERR(m.RainDelay(0));
}

TEST(rain_first_innings_terminated_second_absorbs_lost_time) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    Feed(m, Runs(1), 15 * 6);   // 90 off 15 overs
    CHECK_OK(m.RainDelay(85));  // 10 buffer + 75 net -> 18 overs lost
    // Equal would be 11 each, but 15 already bowled: innings 1 ends at 15,
    // innings 2 gets 20 - (18 - 5) = 7 overs.
    CHECK_EQ(m.AllInnings().size(), size_t{2});
    CHECK_EQ(m.AllInnings()[0].MaxOvers(), 15);
    CHECK_EQ(m.Current().MaxOvers(), 7);
    CHECK_EQ(m.Current().Rules().maxOversPerBowler, 2);
    const double r1 = RainResources(20, 0) - RainResources(5, 0);
    CHECK_EQ(*m.Current().Target(), RainTarget(90, r1, RainResources(7, 0), 275));
}

TEST(rain_no_time_left_for_second_innings) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    Feed(m, Runs(1), 18 * 6);
    CHECK_OK(m.RainDelay(600));
    CHECK(m.Result().kind == MatchResult::Kind::NoResult);
}

TEST(rain_shortened_innings_quota_and_powerplay) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    CHECK_OK(m.ReduceOvers(10));
    const MatchRules& r = m.Current().Rules();
    CHECK_EQ(r.maxOversPerBowler, 2);
    CHECK_EQ(r.phases[0].lastOver, 3);
    CHECK(r.IsPowerplay(3));
    CHECK(!r.IsPowerplay(4));
    // Bowler 10 bowls overs 1 and 3, then is out of overs.
    Feed(m, Runs(0), 4 * 6);
    CHECK(!m.Current().CanBowlNextOver(10));
}

TEST(rain_reduce_overs_errors) {
    Match t(MatchRules::Test(), TwoTeams(), 0);
    CHECK_ERR(t.ReduceOvers(40));
    Match m(MatchRules::T20(), TwoTeams(), 0);
    CHECK_ERR(m.ReduceOvers(25));  // can't increase
    Feed(m, Runs(0), 8 * 6 + 2);   // 8.2 overs
    CHECK_ERR(m.ReduceOvers(8));   // the 9th over has started
    CHECK_OK(m.ReduceOvers(9));
    CHECK_ERR(m.ReduceOvers(0));
}

// ---------------------------------------------------------------- Test matches

TEST(rain_test_time_lost_and_made_up) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Feed(m, Runs(0), 10 * 6);
    CHECK_OK(m.RainDelay(60));  // 15 overs off the day
    CHECK_EQ(m.RainLog().back().oversLost, 15);
    CHECK_EQ(m.Clock().ballsAvailableToday, 75 * 6);
    CHECK_EQ(m.Clock().makeUpBalls, 15 * 6);
    // Lunch comes after 30 overs of *time*: 10 played + 15 lost + 5 more.
    Feed(m, Runs(0), 4 * 6);
    CHECK(m.Intervals().empty());
    Feed(m, Runs(0), 6);
    CHECK(LastIs(m, IntervalKind::Lunch));
    Feed(m, Runs(0), 60 * 6);  // the rest of day 1 (75 overs played)
    CHECK(LastIs(m, IntervalKind::Stumps));
    // Day 2: 90 + 8 make-up overs; 7 overs still owed.
    CHECK_EQ(m.Clock().day, 2);
    CHECK_EQ(m.Clock().ballsAvailableToday, 98 * 6);
    CHECK_EQ(m.Clock().makeUpBalls, 7 * 6);
}

TEST(rain_test_through_lunch_and_tea) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Feed(m, Runs(0), 20 * 6);
    CHECK_OK(m.RainDelay(240));  // 60 overs of time: lunch and tea pass during the rain
    int lunch = 0, tea = 0;
    for (const auto& r : m.Intervals()) {
        lunch += r.kind == IntervalKind::Lunch;
        tea += r.kind == IntervalKind::Tea;
    }
    CHECK_EQ(lunch, 1);
    CHECK_EQ(tea, 1);
    CHECK_EQ(m.Clock().session, 3);
    CHECK_EQ(m.BallsLeftToday(), 10 * 6);
}

TEST(rain_test_lose_rest_of_day_mid_over) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Feed(m, Runs(0), 40 * 6 + 3);  // 40.3 overs
    CHECK_OK(m.LoseRestOfDay());
    CHECK(LastIs(m, IntervalKind::Stumps));
    CHECK_EQ(m.Clock().day, 2);
    // The unfinished over is completed on day 2.
    CHECK_EQ(m.Current().BallsInOver(), 3);
    Feed(m, Runs(0), 3);
    CHECK_EQ(m.Current().CompletedOvers(), 41);
    CHECK_ERR(Match(MatchRules::T20(), TwoTeams(), 0).LoseRestOfDay());
}

TEST(rain_washed_out_first_day_lowers_follow_on_margin) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    CHECK_EQ(m.FollowOnThreshold(), 200);
    CHECK_OK(m.LoseRestOfDay());  // day 1 lost without a ball (Law 14.1.3)
    CHECK_EQ(m.Clock().day, 2);
    CHECK_EQ(m.FollowOnThreshold(), 150);
    Feed(m, Runs(1), 160);
    Feed(m, Bowled(), 10);  // A 160
    Feed(m, Bowled(), 10);  // B 0: lead 160 >= 150
    CHECK(m.Phase() == MatchPhase::AwaitingFollowOnDecision);
    CHECK_EQ(m.FollowOnThreshold(), 150);  // stays fixed once play started
}

TEST(rain_test_last_day_washout_and_abandon) {
    {
        Match m(MatchRules::Test(2), TwoTeams(), 0);
        Feed(m, Runs(1), 30);
        CHECK_OK(m.LoseRestOfDay());
        CHECK_OK(m.LoseRestOfDay());  // day 2 (the last) lost too
        CHECK(m.Result().kind == MatchResult::Kind::Draw);
    }
    {
        Match m(MatchRules::Test(), TwoTeams(), 0);
        Feed(m, Runs(1), 30);
        m.Abandon();
        CHECK(m.Result().kind == MatchResult::Kind::Draw);
    }
    {
        Match m(MatchRules::Test(), TwoTeams(), 0);
        m.Abandon();  // no ball bowled
        CHECK(m.Result().kind == MatchResult::Kind::NoResult);
    }
}

// ---------------------------------------------------------------- server session + fuzz

TEST(rain_session_events) {
    MatchSession s(MatchRules::T20(), TwoTeams(), 21);
    s.RunAi();
    QuickSim sim(22);
    while (s.GetMatch().AllInnings().size() == 1 && s.WaitingFor().what == "delivery")
        CHECK_OK(s.ResolveDelivery(sim.Next(s.GetMatch())));
    for (int i = 0; i < 18 && s.WaitingFor().what == "delivery"; ++i) CHECK_OK(s.ResolveDelivery(Runs(0)));
    CHECK_OK(s.RainDelay(40));
    bool rain = false, target = false, breakBeforeInnings2 = false;
    std::string prev;
    for (const auto& e : s.EventsSince(0)) {
        rain |= e.kind == "rain";
        target |= e.kind == "target" && e.text.find("non-official") != std::string::npos;
        if (e.kind == "innings" && e.text.rfind("Innings 2", 0) == 0) breakBeforeInnings2 = prev == "innings-break";
        prev = e.kind;
    }
    CHECK(rain && target && breakBeforeInnings2);
    CHECK_OK(s.AbandonMatch());
    CHECK(s.GetMatch().Phase() == MatchPhase::Complete);
    CHECK_ERR(s.AbandonMatch());
}

TEST(rain_fuzz_full_matches_with_random_weather) {
    int rainyResults = 0, noResults = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        MatchRules rules = seed % 4 == 0 ? MatchRules::ODI() : seed % 4 == 1 ? MatchRules::Test(3) : MatchRules::T20();
        MatchSession s(rules, TwoTeams(), seed);
        s.RunAi();
        QuickSim sim(seed * 31);
        std::mt19937_64 weather(seed * 97);
        int guard = 0;
        while (s.WaitingFor().what == "delivery" && guard++ < 20000) {
            if (std::uniform_real_distribution<double>(0, 1)(weather) < 0.004) {
                const int minutes = std::uniform_int_distribution<int>(5, 200)(weather);
                CHECK_OK(s.RainDelay(minutes));
                continue;
            }
            Delivery d = sim.Next(s.GetMatch());
            auto c = sim.MakeCase(s.GetMatch(), d);
            if (!c || !s.ResolveAppeal(*c)) CHECK_OK(s.ResolveDelivery(d));
        }
        const Match& m = s.GetMatch();
        CHECK(m.Phase() == MatchPhase::Complete);
        noResults += m.Result().kind == MatchResult::Kind::NoResult;
        rainyResults += m.RainAffected() && m.Result().kind != MatchResult::Kind::NoResult;
        for (const auto& inn : m.AllInnings()) {
            int bat = 0;
            for (int i = 0; i < 11; ++i) {
                bat += inn.Batter(i).runs;
                // Never more than the format's limit. (Overs bowled before a
                // rain cut may exceed the new, smaller limit; the bowler just
                // can't bowl again - see rain_shortened_innings_quota_and_powerplay.)
                if (m.Rules().maxOversPerBowler > 0)
                    CHECK(inn.Bowler(i).legalBalls <= m.Rules().maxOversPerBowler * 6);
            }
            CHECK_EQ(bat + inn.GetExtras().Total(), inn.Total());
            if (inn.MaxOvers() > 0) CHECK(inn.LegalBalls() <= inn.MaxOvers() * 6);
        }
        if (m.HasSchedule()) CHECK(m.Clock().day <= 3);
    }
    CHECK(rainyResults > 0);
    (void)noResults;
}
