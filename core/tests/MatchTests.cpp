#include <cmath>

#include "Test.h"
#include "cricket/AutoCaptain.h"
#include "cricket/Commentary.h"
#include "cricket/Lbw.h"
#include "cricket/Match.h"
#include "cricket/TeamFactory.h"
#include "cricket/Toss.h"

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

// Bowl `d` repeatedly handling bowler changes / new batters with simple choices.
void Feed(Match& m, const Delivery& d, int times) {
    for (int i = 0; i < times && m.Phase() == MatchPhase::InProgress; ++i) {
        auto& inn = m.Current();
        if (inn.State() == InningsState::AwaitingOpeners) m.SetOpeners(0, 1);
        if (inn.State() == InningsState::AwaitingBatter) {
            for (int b = 0; b < 11; ++b)
                if (!inn.Batter(b).batted) {
                    m.SetNextBatter(b);
                    break;
                }
        }
        if (m.Current().State() == InningsState::AwaitingBowler) {
            for (int b = 10; b >= 0; --b)
                if (m.Current().CanBowlNextOver(b) && b != 4) {
                    m.SetBowler(b);
                    break;
                }
        }
        CHECK_OK(m.Bowl(d));
    }
}

void AllOut(Match& m) { Feed(m, Bowled(), 10); }

// Invariants every completed innings must satisfy.
void CheckInvariants(const Innings& inn, const MatchRules& r) {
    int batRuns = 0, bowlBalls = 0, bowlWkts = 0, outs = 0;
    for (int i = 0; i < r.squadSize; ++i) {
        batRuns += inn.Batter(i).runs;
        bowlBalls += inn.Bowler(i).legalBalls;
        bowlWkts += inn.Bowler(i).wickets;
        outs += inn.Batter(i).out ? 1 : 0;
        if (r.maxOversPerBowler > 0) CHECK(inn.Bowler(i).legalBalls <= r.maxOversPerBowler * r.ballsPerOver);
    }
    CHECK_EQ(batRuns + inn.GetExtras().Total(), inn.Total());
    CHECK_EQ(bowlBalls, inn.LegalBalls());
    CHECK_EQ(outs, inn.Wickets());
    CHECK(bowlWkts <= inn.Wickets());
    // No bowler bowled two consecutive overs.
    int prevOver = -1, prevBowler = -1;
    for (const auto& b : inn.Balls()) {
        if (b.over != prevOver) {
            CHECK(b.bowler != prevBowler);
            prevOver = b.over;
        }
        if (b.overCompleted) prevBowler = b.bowler;
    }
}

}  // namespace

TEST(t20_chase_win_by_wickets) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    Feed(m, Runs(1), 30);  // 30 off 5 overs
    AllOut(m);
    CHECK_EQ(m.AllInnings().size(), size_t{2});
    CHECK_EQ(*m.Current().Target(), 31);
    Feed(m, Runs(4, true), 8);  // 32
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK(m.Result().kind == MatchResult::Kind::Win);
    CHECK_EQ(m.Result().winner, 1);
    CHECK(m.Result().margin == MatchResult::Margin::Wickets);
    CHECK_EQ(m.Result().value, 10);
}

TEST(t20_defend_win_by_runs_and_tie) {
    {
        Match m(MatchRules::Custom(2), TwoTeams(), 0);
        Feed(m, Runs(4, true), 12);  // 48 off 2 overs
        CHECK_EQ(m.AllInnings().size(), size_t{2});
        Feed(m, Runs(2), 12);  // 24
        CHECK(m.Phase() == MatchPhase::Complete);
        CHECK_EQ(m.Result().winner, 0);
        CHECK(m.Result().margin == MatchResult::Margin::Runs);
        CHECK_EQ(m.Result().value, 24);
    }
    {
        Match m(MatchRules::Custom(1), TwoTeams(), 1);
        Feed(m, Runs(2), 6);  // 12
        Feed(m, Runs(2), 6);  // 12
        CHECK(m.Result().kind == MatchResult::Kind::Tie);
    }
}

TEST(test_follow_on_and_innings_victory) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Feed(m, Runs(4, true), 100);  // 400
    m.Declare();
    CHECK_EQ(m.AllInnings()[0].Total(), 400);
    AllOut(m);  // side 1 all out for 0
    CHECK(m.Phase() == MatchPhase::AwaitingFollowOnDecision);
    CHECK_ERR(m.Bowl(Runs(1)));
    CHECK_OK(m.DecideFollowOn(true));
    CHECK_EQ(m.BattingSide(), 1);
    Feed(m, Runs(1), 50);
    AllOut(m);
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK(m.Result().margin == MatchResult::Margin::InningsAndRuns);
    CHECK_EQ(m.Result().winner, 0);
    CHECK_EQ(m.Result().value, 350);
}

TEST(test_fourth_innings_chase) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Feed(m, Runs(1), 100);
    AllOut(m);       // A 100
    Feed(m, Runs(1), 50);
    AllOut(m);       // B 50, lead 50 < 200 -> no follow-on
    CHECK_EQ(m.BattingSide(), 0);
    Feed(m, Runs(1), 20);
    AllOut(m);       // A 20 -> target 100+20-50+1 = 71
    CHECK_EQ(*m.Current().Target(), 71);
    Feed(m, Runs(1), 70);
    AllOut(m);       // B 70 all out, one short -> tie
    CHECK(m.Result().kind == MatchResult::Kind::Tie);
}

TEST(test_draw_when_time_runs_out) {
    MatchRules r = MatchRules::Test(1);  // 90 overs
    Match m(r, TwoTeams(), 0);
    Feed(m, Runs(0), 90 * 6);
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK(m.Result().kind == MatchResult::Kind::Draw);
}

TEST(team_validation) {
    auto t = TwoTeams();
    t[0].xi.pop_back();
    CHECK_ERR(Match::ValidateTeam(t[0], 11));
    auto u = TwoTeams();
    u[1].xi[2].name = u[1].xi[1].name;
    CHECK_ERR(Match::ValidateTeam(u[1], 11));
    bool threw = false;
    try {
        Match m(MatchRules::T20(), t, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(toss_permissions_and_fairness) {
    Toss t(42, 1);
    CHECK_ERR(t.Call(0, CoinFace::Heads));  // home captain can't call
    CHECK_OK(t.Call(1, CoinFace::Heads));
    CHECK_ERR(t.Call(1, CoinFace::Tails));  // only once
    CHECK_ERR(t.Decide(1 - t.Winner(), TossChoice::Bat));
    CHECK_OK(t.Decide(t.Winner(), TossChoice::Bowl));
    CHECK_EQ(t.BattingFirst(), 1 - t.Winner());

    int wins = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        Toss x(static_cast<uint64_t>(i) * 7919 + 1, 1);
        x.Call(1, CoinFace::Heads);
        wins += x.Winner() == 1;
    }
    double p = static_cast<double>(wins) / n;
    CHECK(std::fabs(p - 0.5) < 0.02);
}

TEST(lbw_law) {
    LbwEvidence e;
    CHECK(LbwIsOut(e));
    e.pitched = PitchZone::OutsideLeg;
    CHECK(!LbwIsOut(e));
    e = {};
    e.impact = ImpactZone::OutsideOff;
    CHECK(!LbwIsOut(e));
    e.shotOffered = false;
    CHECK(LbwIsOut(e));  // no shot, hit outside off -> can be out
    e = {};
    e.hitBatFirst = true;
    CHECK(!LbwIsOut(e));
    e = {};
    e.wickets = WicketsZone::Missing;
    CHECK(!LbwIsOut(e));
}

TEST(drs_review_outcomes) {
    LbwEvidence hitting;
    auto r = ReviewLbw(false, hitting);
    CHECK(r.outcome == ReviewOutcome::Overturned && r.finalOut && !r.reviewLost);
    r = ReviewLbw(true, hitting);
    CHECK(r.outcome == ReviewOutcome::Upheld && r.finalOut && r.reviewLost);

    LbwEvidence clipping;
    clipping.wickets = WicketsZone::UmpiresCall;
    r = ReviewLbw(true, clipping);
    CHECK(r.outcome == ReviewOutcome::UmpiresCall && r.finalOut && !r.reviewLost);
    r = ReviewLbw(false, clipping);
    CHECK(r.outcome == ReviewOutcome::UmpiresCall && !r.finalOut && !r.reviewLost);

    LbwEvidence outsideLeg;
    outsideLeg.pitched = PitchZone::OutsideLeg;
    r = ReviewLbw(true, outsideLeg);
    CHECK(r.outcome == ReviewOutcome::Overturned && !r.finalOut);
}

// Full AI-vs-AI matches in every format must finish and keep the books balanced.
static void SimulateFull(MatchRules rules, uint64_t seed) {
    Match m(rules, TwoTeams(), static_cast<int>(seed % 2));
    AutoCaptain c0(seed), c1(seed + 1);
    QuickSim sim(seed + 2);
    CommentaryEngine com(seed % 2 ? Language::Hindi : Language::English, seed);
    int guard = 0;
    while (m.Phase() != MatchPhase::Complete && guard++ < 20000) {
        AutoCaptain& bat = m.BattingSide() == 0 ? c0 : c1;
        AutoCaptain& fld = m.BattingSide() == 0 ? c1 : c0;
        if (m.Phase() == MatchPhase::AwaitingFollowOnDecision) {
            CHECK_OK(m.DecideFollowOn(fld.ShouldEnforceFollowOn(m)));
            continue;
        }
        auto& inn = m.Current();
        switch (inn.State()) {
            case InningsState::AwaitingOpeners: {
                auto [a, b] = bat.ChooseOpeners(m);
                CHECK_OK(m.SetOpeners(a, b));
                break;
            }
            case InningsState::AwaitingBatter: CHECK_OK(m.SetNextBatter(bat.ChooseNextBatter(m))); break;
            case InningsState::AwaitingBowler: {
                int b = fld.ChooseBowler(m);
                CHECK(b >= 0);
                CHECK_OK(m.SetBowler(b));
                FieldSetup f = fld.ChooseLegalField(m, m.GetTeam(m.FieldingSide()).xi[b].bowls);
                CHECK_OK(ValidateField(f, {&m.Rules(), inn.CurrentOverNumber()}).status);
                break;
            }
            case InningsState::InPlay: {
                if (bat.ShouldDeclare(m)) {
                    CHECK_OK(m.Declare());
                    break;
                }
                DeliveryRecord rec;
                CHECK_OK(m.Bowl(sim.Next(m), &rec));
                auto lines = com.Describe(m, rec);
                CHECK(!lines.empty());
                for (auto& l : lines) CHECK(!l.text.empty());
                break;
            }
            case InningsState::Complete: CHECK(false); break;
        }
    }
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK(!m.Result().summary.empty());
    for (const auto& inn : m.AllInnings()) CheckInvariants(inn, m.Rules());
}

TEST(full_t20_matches_complete) {
    for (uint64_t s = 1; s <= 30; ++s) SimulateFull(MatchRules::T20(), s);
}

TEST(full_odi_matches_complete) {
    for (uint64_t s = 100; s < 110; ++s) SimulateFull(MatchRules::ODI(), s);
}

TEST(full_test_matches_complete) {
    for (uint64_t s = 200; s < 206; ++s) SimulateFull(MatchRules::Test(), s);
}

TEST(commentary_does_not_repeat_back_to_back) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    m.SetOpeners(0, 1);
    m.SetBowler(10);
    CommentaryEngine com(Language::English, 7);
    std::string prev;
    for (int i = 0; i < 6; ++i) {
        DeliveryRecord rec;
        m.Bowl(Runs(4, true), &rec);
        auto lines = com.Describe(m, rec);
        CHECK(lines[0].text != prev);
        prev = lines[0].text;
    }
}
