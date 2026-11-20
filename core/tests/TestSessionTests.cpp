// Stage B: Test match sessions - lunch, tea, stumps, days, changeovers,
// early intervals, day-night Tests, follow-on margin by days, match end,
// and the server session pausing for breaks.
#include "Test.h"
#include "cricket/AutoCaptain.h"
#include "cricket/Commentary.h"
#include "cricket/Match.h"
#include "cricket/MatchSession.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

namespace {

std::array<Team, 2> TwoTeams() {
    auto all = FictionalTeams();
    return {all[0], all[1]};
}

Delivery Runs(int r) {
    Delivery d;
    d.runs = r;
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

void Overs(Match& m, int overs) { Feed(m, Runs(0), overs * 6); }

std::vector<IntervalKind> Kinds(const Match& m) {
    std::vector<IntervalKind> k;
    for (const auto& r : m.Intervals()) k.push_back(r.kind);
    return k;
}

int Count(const Match& m, IntervalKind k) {
    int n = 0;
    for (const auto& r : m.Intervals()) n += r.kind == k;
    return n;
}

bool LastIs(const Match& m, IntervalKind k) { return !m.Intervals().empty() && m.Intervals().back().kind == k; }

}  // namespace

TEST(sessions_lunch_tea_stumps_and_next_day) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    CHECK(m.HasSchedule());
    CHECK_EQ(m.Clock().day, 1);
    CHECK_EQ(m.Clock().ballsAvailableToday, 540);
    CHECK_EQ(m.BallsLeftInSession(), 180);
    Overs(m, 29);
    CHECK(m.Intervals().empty());
    Overs(m, 1);  // 30 overs
    CHECK_EQ(m.Intervals().size(), size_t{1});
    CHECK(m.Intervals()[0].kind == IntervalKind::Lunch);
    CHECK_EQ(m.Intervals()[0].day, 1);
    CHECK_EQ(m.Clock().session, 2);
    Overs(m, 30);  // 60
    CHECK(LastIs(m, IntervalKind::Tea));
    CHECK_EQ(m.Clock().session, 3);
    Overs(m, 30);  // 90
    CHECK(LastIs(m, IntervalKind::Stumps));
    CHECK_EQ(m.Clock().day, 2);
    CHECK_EQ(m.Clock().session, 1);
    CHECK_EQ(m.Clock().ballsToday, 0);
    CHECK_EQ(m.BallsLeftToday(), 540);
    CHECK_EQ(m.Clock().startDay, 1);
    CHECK_EQ(m.Current().CompletedOvers(), 90);  // the innings carries on overnight
}

TEST(sessions_changeover_costs_two_overs) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Overs(m, 35);
    Feed(m, Bowled(), 10);  // all out at 36.4 overs -> new innings
    CHECK_EQ(m.AllInnings().size(), size_t{2});
    CHECK(Count(m, IntervalKind::InningsBreak) == 1);
    // 90 overs - 2 for the changeover = 88 overs; 220 balls bowled.
    CHECK_EQ(m.Clock().ballsAvailableToday, 88 * 6);
    CHECK_EQ(m.BallsLeftToday(), 88 * 6 - 220);
    // Stumps once 88 overs of play are done. The second innings started in
    // the middle of the day's 37th over, so the over in progress at the 88-over
    // mark is completed first (stumps after 528 + 4 = 532 balls).
    Feed(m, Runs(0), 88 * 6 - 220);
    CHECK_EQ(m.Clock().day, 1);
    Feed(m, Runs(0), 4);
    CHECK(LastIs(m, IntervalKind::Stumps));
    if (!m.Intervals().empty()) CHECK_EQ(m.Intervals().back().day, 1);
    CHECK_EQ(m.Clock().day, 2);
}

TEST(sessions_innings_ending_near_lunch_brings_lunch_forward) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Overs(m, 27);
    Feed(m, Bowled(), 10);  // all out at 28.4: 1.2 overs before lunch, inside 10 minutes
    CHECK(!m.Intervals().empty());
    if (m.Intervals().empty()) return;
    const IntervalRecord& lunch = m.Intervals().back();
    CHECK(lunch.kind == IntervalKind::Lunch);
    CHECK(lunch.early);
    CHECK_EQ(m.Clock().session, 2);
    CHECK_EQ(m.AllInnings().size(), size_t{2});
}

TEST(sessions_innings_ending_well_before_lunch_does_not) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Overs(m, 20);
    Feed(m, Bowled(), 10);  // 21.4 overs: plenty of time left
    CHECK(LastIs(m, IntervalKind::InningsBreak));
    CHECK_EQ(m.Clock().session, 1);
}

TEST(sessions_day_night_tea_then_dinner) {
    MatchRules r = MatchRules::Test();
    r.schedule.dayNight = true;
    Match m(r, TwoTeams(), 0);
    Overs(m, 90);
    auto k = Kinds(m);
    CHECK_EQ(k.size(), size_t{3});
    CHECK(k[0] == IntervalKind::Tea && k[1] == IntervalKind::Dinner && k[2] == IntervalKind::Stumps);
}

TEST(sessions_last_day_stumps_draws_the_match) {
    Match m(MatchRules::Test(2), TwoTeams(), 0);
    Overs(m, 179);
    CHECK(m.Phase() == MatchPhase::InProgress);
    CHECK_EQ(m.Clock().day, 2);
    Overs(m, 1);
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK(m.Result().kind == MatchResult::Kind::Draw);
    CHECK(LastIs(m, IntervalKind::Stumps));
    CHECK_EQ(Count(m, IntervalKind::Stumps), 2);
    CHECK_ERR(m.Bowl(Runs(1)));
}

TEST(sessions_result_before_time_runs_out) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Feed(m, Runs(1), 30);
    Feed(m, Bowled(), 10);  // A 30
    Feed(m, Runs(1), 10);
    Feed(m, Bowled(), 10);  // B 10 (lead 20: no follow-on)
    Feed(m, Runs(1), 5);
    Feed(m, Bowled(), 10);  // A 5 -> target 26
    Feed(m, Runs(4), 7);    // 28: B win (fours run, not boundaries)
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK(m.Result().kind == MatchResult::Kind::Win);
    CHECK_EQ(m.Clock().day, 1);
    CHECK_EQ(Count(m, IntervalKind::InningsBreak), 3);
}

TEST(sessions_follow_on_margin_by_match_length) {
    CHECK_EQ(MatchRules::FollowOnLeadForDays(5), 200);
    CHECK_EQ(MatchRules::FollowOnLeadForDays(4), 150);
    CHECK_EQ(MatchRules::FollowOnLeadForDays(3), 150);
    CHECK_EQ(MatchRules::FollowOnLeadForDays(2), 100);
    CHECK_EQ(MatchRules::FollowOnLeadForDays(1), 75);
    for (int days : {1, 2, 3, 5}) {
        Match m(MatchRules::Test(days), TwoTeams(), 0);
        CHECK_EQ(m.FollowOnThreshold(), MatchRules::FollowOnLeadForDays(days));
    }
    // 2-day match: a lead of exactly 100 lets the side batting first enforce it.
    Match m(MatchRules::Test(2), TwoTeams(), 0);
    Feed(m, Runs(1), 100);
    Feed(m, Bowled(), 10);
    Feed(m, Bowled(), 10);
    CHECK(m.Phase() == MatchPhase::AwaitingFollowOnDecision);
}

TEST(sessions_declaration_mid_over_and_follow_on_count_as_changeovers) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Feed(m, Runs(1), 63);  // 10.3 overs
    CHECK_OK(m.Declare());
    CHECK_EQ(m.Clock().ballsAvailableToday, 88 * 6);
    Feed(m, Bowled(), 10);  // 63 - 0: follow-on? lead 63 < 200: no
    CHECK_EQ(m.Clock().ballsAvailableToday, 86 * 6);
    CHECK_EQ(Count(m, IntervalKind::InningsBreak), 2);
}

TEST(sessions_limited_overs_have_no_schedule) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    CHECK(!m.HasSchedule());
    CHECK_EQ(m.BallsLeftInSession(), -1);
    Overs(m, 20);
    CHECK_EQ(m.Intervals().size(), size_t{1});
    CHECK(m.Intervals()[0].kind == IntervalKind::InningsBreak);
}

TEST(sessions_invalid_schedule_rejected) {
    MatchRules r = MatchRules::Test();
    r.schedule.maxOversPerDay = 80;  // less than the minimum 90
    bool threw = false;
    try {
        Match m(r, TwoTeams(), 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(sessions_full_ai_tests_have_consistent_schedules) {
    for (uint64_t seed = 300; seed < 306; ++seed) {
        MatchSession s(MatchRules::Test(), TwoTeams(), seed);  // AI-only: never pauses
        s.RunAi();
        QuickSim sim(seed + 1);
        int guard = 0;
        while (s.WaitingFor().what == "delivery" && guard++ < 20000) {
            Delivery d = sim.Next(s.GetMatch());
            auto c = sim.MakeCase(s.GetMatch(), d);
            if (!c || !s.ResolveAppeal(*c)) CHECK_OK(s.ResolveDelivery(d));
        }
        const Match& m = s.GetMatch();
        CHECK(m.Phase() == MatchPhase::Complete);
        CHECK(!s.IsPaused());
        int lastDay = 1, stumps = 0;
        for (const auto& r : m.Intervals()) {
            CHECK(r.day >= lastDay);
            CHECK(r.day <= 5);
            lastDay = r.day;
            stumps += r.kind == IntervalKind::Stumps;
        }
        CHECK(stumps <= 5);
        if (m.Result().kind == MatchResult::Kind::Draw) CHECK_EQ(stumps, 5);
    }
}

// ---------------------------------------------------------------- server session

namespace {
Command Cmd(const std::string& p, uint64_t seq, CommandType t, int a = 0, int b = 0) {
    Command c;
    c.player = p;
    c.seq = seq;
    c.type = t;
    c.a = a;
    c.b = b;
    return c;
}

struct HumanTest {
    MatchSession s{MatchRules::Test(), TwoTeams(), 9};
    uint64_t seq[2] = {0, 0};
    std::string name[2] = {"p-india", "p-pakistan"};
    int batting = -1;
    Status Send(int side, CommandType t, int a = 0, int b = 0) { return s.Submit(Cmd(name[side], ++seq[side], t, a, b)); }
    HumanTest() {
        s.Join(name[0], 0);
        s.Join(name[1], 1);
        Send(1, CommandType::TossCall, 0);
        Send(s.GetToss().Winner(), CommandType::TossDecision, 0);
        batting = s.GetMatch().BattingSide();
        Send(batting, CommandType::SetOpeners, 0, 1);
    }
    // Bowls dot balls, choosing bowlers, until play pauses or `balls` run out.
    void Play(int balls) {
        for (int i = 0; i < balls; ++i) {
            Pending p = s.WaitingFor();
            if (p.what == "bowler") {
                int b = s.GetMatch().Current().CanBowlNextOver(10) ? 10 : 9;
                CHECK_OK(Send(p.side, CommandType::SetBowler, b));
                --i;
                continue;
            }
            if (p.what != "delivery") return;
            CHECK_OK(s.ResolveDelivery(Runs(0)));
        }
    }
};
}  // namespace

TEST(sessions_human_match_pauses_for_lunch_until_both_ready) {
    HumanTest h;
    h.Play(1000);
    CHECK_EQ(h.s.GetMatch().Current().CompletedOvers(), 30);
    CHECK(h.s.IsPaused());
    CHECK(h.s.WaitingFor().what == "interval");
    CHECK_ERR(h.s.ResolveDelivery(Runs(0)));
    CHECK_OK(h.Send(0, CommandType::ResumePlay));
    CHECK(h.s.IsPaused());  // one player isn't enough
    CHECK_OK(h.Send(1, CommandType::ResumePlay));
    CHECK(!h.s.IsPaused());
    bool sawLunch = false, sawResume = false;
    for (const auto& e : h.s.EventsSince(0)) {
        sawLunch |= e.kind == "lunch";
        sawResume |= e.kind == "resume";
    }
    CHECK(sawLunch && sawResume);
    CHECK_ERR(h.Send(0, CommandType::ResumePlay));  // not paused any more
    h.Play(1000);
    CHECK(h.s.WaitingFor().what == "interval");  // tea
    CHECK_OK(h.s.ResumeAfterInterval());          // server timer ends the break
    CHECK(!h.s.IsPaused());
    CHECK_ERR(h.s.ResumeAfterInterval());
    h.Play(1000);  // stumps
    CHECK(h.s.IsPaused());
    CHECK_OK(h.s.ResumeAfterInterval());
    CHECK_EQ(h.s.GetMatch().Clock().day, 2);
}

TEST(sessions_innings_break_does_not_pause) {
    MatchSession s(MatchRules::Custom(1), TwoTeams(), 3);
    s.Join("solo", 0);
    uint64_t seq = 0;
    s.RunAi();  // the AI (side 1) calls the toss
    if (s.WaitingFor().what == "toss-decision") CHECK_OK(s.Submit(Cmd("solo", ++seq, CommandType::TossDecision, 1)));
    CHECK(s.HasMatch());
    if (!s.HasMatch()) return;
    for (int guard = 0; guard < 200 && s.GetMatch().AllInnings().size() == 1; ++guard) {
        Pending p = s.WaitingFor();
        if (p.what == "openers") s.Submit(Cmd("solo", ++seq, CommandType::SetOpeners, 0, 1));
        else if (p.what == "bowler") s.Submit(Cmd("solo", ++seq, CommandType::SetBowler, 10));
        else if (p.what == "delivery") s.ResolveDelivery(Runs(1));
    }
    CHECK_EQ(s.GetMatch().AllInnings().size(), size_t{2});
    CHECK(!s.IsPaused());
}

TEST(sessions_interval_commentary_both_languages) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Overs(m, 30);
    CHECK(!m.Intervals().empty());
    if (m.Intervals().empty()) return;
    for (Language lang : {Language::English, Language::Hindi}) {
        CommentaryEngine com(lang, 1);
        auto lines = com.DescribeInterval(m, m.Intervals().back());
        CHECK_EQ(lines.size(), size_t{1});
        CHECK(lines[0].category == "interval.lunch");
        CHECK(!lines[0].text.empty() && lines[0].text.find('{') == std::string::npos);
    }
}
