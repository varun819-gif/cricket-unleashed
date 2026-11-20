#include "Test.h"
#include "cricket/Innings.h"

using namespace cricket;

namespace {
Delivery Runs(int r, bool boundary = false) {
    Delivery d;
    d.runs = r;
    d.boundary = boundary;
    return d;
}
Delivery Extra(ExtraType e, int r = 0, bool boundary = false) {
    Delivery d;
    d.extra = e;
    d.runs = r;
    d.boundary = boundary;
    return d;
}
Delivery Out(Dismissal how, bool strikerOut = true, int runs = 0) {
    Delivery d;
    d.dismissal = how;
    d.strikerOut = strikerOut;
    d.runs = runs;
    return d;
}
struct Fixture {
    MatchRules rules = MatchRules::T20();
    Innings inn{rules, 0, 0, std::nullopt};
    Fixture() {
        inn.SetOpeners(0, 1);
        inn.SetBowler(10);
    }
};
}  // namespace

TEST(single_rotates_strike_boundary_does_not) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(Runs(1)));
    CHECK_EQ(f.inn.Striker(), 1);
    CHECK_OK(f.inn.Bowl(Runs(4, true)));
    CHECK_EQ(f.inn.Striker(), 1);
    CHECK_EQ(f.inn.Batter(1).fours, 1);
    CHECK_OK(f.inn.Bowl(Runs(6, true)));
    CHECK_EQ(f.inn.Batter(1).sixes, 1);
    CHECK_EQ(f.inn.Total(), 11);
    CHECK_EQ(f.inn.Bowler(10).runs, 11);
}

TEST(over_completion_swaps_ends_and_needs_new_bowler) {
    Fixture f;
    for (int i = 0; i < 6; ++i) CHECK_OK(f.inn.Bowl(Runs(0)));
    CHECK_EQ(f.inn.CompletedOvers(), 1);
    CHECK_EQ(f.inn.Striker(), 1);
    CHECK(f.inn.State() == InningsState::AwaitingBowler);
    CHECK_EQ(f.inn.Bowler(10).maidens, 1);
    CHECK_ERR(f.inn.Bowl(Runs(0)));
    CHECK_ERR(f.inn.SetBowler(10));  // consecutive overs
    CHECK_OK(f.inn.SetBowler(9));
}

TEST(wide_and_no_ball_are_extra_deliveries) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(Extra(ExtraType::Wide)));
    CHECK_EQ(f.inn.Total(), 1);
    CHECK_EQ(f.inn.LegalBalls(), 0);
    CHECK_EQ(f.inn.Batter(0).balls, 0);
    CHECK_OK(f.inn.Bowl(Extra(ExtraType::Wide, 4, true)));  // wide to the boundary = 5
    CHECK_EQ(f.inn.GetExtras().wides, 6);
    CHECK_OK(f.inn.Bowl(Extra(ExtraType::NoBall, 4, true)));  // 1 + 4 off the bat
    CHECK_EQ(f.inn.Batter(0).runs, 4);
    CHECK_EQ(f.inn.Batter(0).balls, 1);
    CHECK_EQ(f.inn.GetExtras().noBalls, 1);
    CHECK_EQ(f.inn.Total(), 11);
    CHECK_EQ(f.inn.Bowler(10).runs, 11);
    CHECK_EQ(f.inn.LegalBalls(), 0);
}

TEST(free_hit_rules) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(Extra(ExtraType::NoBall)));
    CHECK(f.inn.NextBallIsFreeHit());
    CHECK_ERR(f.inn.Bowl(Out(Dismissal::Bowled)));
    CHECK_ERR(f.inn.Bowl(Out(Dismissal::Caught)));
    CHECK_OK(f.inn.Bowl(Extra(ExtraType::Wide)));
    CHECK(f.inn.NextBallIsFreeHit());  // carries over a wide
    CHECK_ERR(f.inn.Bowl(Out(Dismissal::Lbw)));
    CHECK_OK(f.inn.Bowl(Runs(2)));
    CHECK(!f.inn.NextBallIsFreeHit());
    CHECK_OK(f.inn.Bowl(Out(Dismissal::Bowled)));
}

TEST(run_out_allowed_on_free_hit) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(Extra(ExtraType::NoBall)));
    CHECK_OK(f.inn.Bowl(Out(Dismissal::RunOut, true, 1)));
    CHECK_EQ(f.inn.Wickets(), 1);
    CHECK_EQ(f.inn.Bowler(10).wickets, 0);  // run outs are not the bowler's
}

TEST(dismissal_legality_on_extras) {
    CHECK(Innings::DismissalAllowed(Dismissal::Stumped, ExtraType::Wide, false));
    CHECK(!Innings::DismissalAllowed(Dismissal::Stumped, ExtraType::Wide, true));
    CHECK(!Innings::DismissalAllowed(Dismissal::Lbw, ExtraType::NoBall, false));
    CHECK(!Innings::DismissalAllowed(Dismissal::Caught, ExtraType::Wide, false));
    CHECK(Innings::DismissalAllowed(Dismissal::RunOut, ExtraType::Wide, true));
    CHECK(!Innings::DismissalAllowed(Dismissal::Caught, ExtraType::LegBye, false));
    CHECK(!Innings::DismissalAllowed(Dismissal::TimedOut, ExtraType::None, false));
}

TEST(invalid_deliveries_rejected) {
    Fixture f;
    CHECK_ERR(f.inn.Bowl(Extra(ExtraType::Bye, 6, true)));  // six off byes
    CHECK_ERR(f.inn.Bowl(Runs(5, true)));                  // 5 is not a boundary value
    CHECK_ERR(f.inn.Bowl(Out(Dismissal::Caught, true, 2))); // runs on a catch
    CHECK_ERR(f.inn.Bowl(Out(Dismissal::Bowled, false)));   // non-striker bowled?
    CHECK_ERR(f.inn.Bowl(Runs(-1)));
    CHECK_EQ(f.inn.Total(), 0);
    CHECK_EQ(f.inn.LegalBalls(), 0);
}

TEST(byes_do_not_spoil_maiden_but_wides_do) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(Extra(ExtraType::Bye, 1)));
    for (int i = 0; i < 5; ++i) CHECK_OK(f.inn.Bowl(Runs(0)));
    CHECK_EQ(f.inn.Bowler(10).maidens, 1);
    CHECK_EQ(f.inn.Bowler(10).runs, 0);
    CHECK_OK(f.inn.SetBowler(9));
    CHECK_OK(f.inn.Bowl(Extra(ExtraType::Wide)));
    for (int i = 0; i < 6; ++i) CHECK_OK(f.inn.Bowl(Runs(0)));
    CHECK_EQ(f.inn.Bowler(9).maidens, 0);
}

TEST(caught_new_batter_on_strike) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(Out(Dismissal::Caught)));
    CHECK(f.inn.State() == InningsState::AwaitingBatter);
    CHECK_EQ(f.inn.Bowler(10).wickets, 1);
    CHECK_ERR(f.inn.SetNextBatter(1));  // already in
    CHECK_OK(f.inn.SetNextBatter(2));
    CHECK_EQ(f.inn.Striker(), 2);
    CHECK_EQ(f.inn.NonStriker(), 1);
}

TEST(run_out_non_striker_after_a_run) {
    Fixture f;
    // Striker 0 hits, they complete 1 run, non-striker (1) run out going for 2nd.
    CHECK_OK(f.inn.Bowl(Out(Dismissal::RunOut, false, 1)));
    CHECK(f.inn.Batter(1).out);
    CHECK_EQ(f.inn.Batter(0).runs, 1);
    // After crossing once, 0 is at the non-striker's end; new batter faces.
    CHECK_EQ(f.inn.NonStriker(), 0);
    CHECK_OK(f.inn.SetNextBatter(2));
    CHECK_EQ(f.inn.Striker(), 2);
}

TEST(wicket_on_last_ball_of_over) {
    Fixture f;
    for (int i = 0; i < 5; ++i) CHECK_OK(f.inn.Bowl(Runs(0)));
    CHECK_OK(f.inn.Bowl(Out(Dismissal::Bowled)));
    CHECK(f.inn.State() == InningsState::AwaitingBatter);
    CHECK_OK(f.inn.SetBowler(9));  // can pick bowler before batter arrives
    CHECK_OK(f.inn.SetNextBatter(2));
    // New batter goes to the non-striker's end because the over changed ends.
    CHECK_EQ(f.inn.Striker(), 1);
    CHECK_EQ(f.inn.NonStriker(), 2);
}

TEST(all_out_after_ten_wickets) {
    Fixture f;
    for (int w = 0; w < 10; ++w) {
        if (f.inn.State() == InningsState::AwaitingBowler) f.inn.SetBowler(f.inn.LastOverBowler() == 10 ? 9 : 10);
        CHECK_OK(f.inn.Bowl(Out(Dismissal::Bowled)));
        if (w < 9) CHECK_OK(f.inn.SetNextBatter(w + 2));
    }
    CHECK(f.inn.IsComplete());
    CHECK(f.inn.EndReason() == InningsEnd::AllOut);
    CHECK_ERR(f.inn.Bowl(Runs(1)));
}

TEST(bowler_quota_enforced) {
    MatchRules r = MatchRules::T20();
    Innings inn(r, 0, 0, std::nullopt);
    inn.SetOpeners(0, 1);
    for (int over = 0; over < 8; ++over) {
        CHECK_OK(inn.SetBowler(over % 2 == 0 ? 10 : 9));
        for (int b = 0; b < 6; ++b) CHECK_OK(inn.Bowl(Runs(0)));
    }
    CHECK_ERR(inn.SetBowler(10));  // 4 overs used
    CHECK(!inn.CanBowlNextOver(9));
    CHECK_OK(inn.SetBowler(8));
}

TEST(target_reached_ends_innings) {
    MatchRules r = MatchRules::T20();
    Innings inn(r, 1, 1, 10);
    inn.SetOpeners(0, 1);
    inn.SetBowler(10);
    CHECK_OK(inn.Bowl(Runs(6, true)));
    CHECK(!inn.IsComplete());
    CHECK_OK(inn.Bowl(Runs(4, true)));
    CHECK(inn.EndReason() == InningsEnd::TargetReached);
}

TEST(fifty_milestone_flag) {
    Fixture f;
    DeliveryRecord rec;
    for (int i = 0; i < 5; ++i) CHECK_OK(f.inn.Bowl(Runs(6, true), &rec));  // 30
    CHECK_OK(f.inn.Bowl(Runs(1), &rec));  // 31, crosses, then over ends -> back on strike
    CHECK_EQ(f.inn.Striker(), 0);
    CHECK_OK(f.inn.SetBowler(9));
    for (int i = 0; i < 3; ++i) CHECK_OK(f.inn.Bowl(Runs(6, true), &rec));  // 49
    CHECK(!rec.batterFifty);
    CHECK_OK(f.inn.Bowl(Runs(6, true), &rec));  // 55
    CHECK(rec.batterFifty);
    CHECK_EQ(f.inn.Batter(0).runs, 55);
}

TEST(declaration_only_in_unlimited_overs) {
    Fixture f;
    CHECK_ERR(f.inn.Declare());
    MatchRules t = MatchRules::Test();
    Innings inn(t, 0, 0, std::nullopt);
    inn.SetOpeners(0, 1);
    CHECK_OK(inn.Declare());
    CHECK(inn.EndReason() == InningsEnd::Declared);
}
