// DRS: player reviews (caught, LBW), third-umpire referrals (run out,
// stumping, clean catch), front-foot no-ball checks, review counts and the
// server session flow.
#include "Test.h"
#include "cricket/AutoCaptain.h"
#include "cricket/Commentary.h"
#include "cricket/Drs.h"
#include "cricket/Match.h"
#include "cricket/MatchSession.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

namespace {

std::array<Team, 2> TwoTeams() {
    auto all = FictionalTeams();
    return {all[0], all[1]};
}

// Side 0 bats, side 1 fields, bowler 10 has the ball.
struct Game {
    Match m;
    explicit Game(MatchRules r = MatchRules::T20()) : m(r, TwoTeams(), 0) {
        m.SetOpeners(0, 1);
        m.SetBowler(10);
    }
};

Delivery Runs(int r, bool boundary = false) {
    Delivery d;
    d.runs = r;
    d.boundary = boundary;
    return d;
}

DecisionCase Caught(bool onFieldOut, Evidence edge, Evidence grounded = Evidence::No) {
    DecisionCase c;
    c.kind = DecisionKind::PlayerAppeal;
    c.mode = Dismissal::Caught;
    c.onFieldOut = onFieldOut;
    c.ifOut.dismissal = Dismissal::Caught;
    c.ifOut.fielder = 3;
    c.ifNotOut = Runs(2);  // if not out, they'd have run two
    c.evidence.batContact = edge;
    c.evidence.groundedBeforeCatch = grounded;
    return c;
}

LbwEvidence Lbw(WicketsZone w, PitchZone p = PitchZone::InLine) {
    LbwEvidence e;
    e.wickets = w;
    e.pitched = p;
    return e;
}

DecisionCase LbwCase(bool onFieldOut, LbwEvidence e) {
    DecisionCase c;
    c.kind = DecisionKind::PlayerAppeal;
    c.mode = Dismissal::Lbw;
    c.onFieldOut = onFieldOut;
    c.ifOut.dismissal = Dismissal::Lbw;
    c.ifNotOut = Delivery{};
    c.evidence.lbw = e;
    return c;
}

DecisionCase Referral(Dismissal mode, Evidence grounded) {
    DecisionCase c;
    c.kind = DecisionKind::UmpireReferral;
    c.mode = mode;
    c.ifOut.dismissal = mode;
    c.ifNotOut = Delivery{};
    c.evidence.batterGrounded = grounded;
    return c;
}

const Innings& Inn(const Game& g) { return g.m.Current(); }

}  // namespace

// ---------------------------------------------------------------- caught

TEST(drs_caught_given_out_no_edge_overturned) {
    Game g;
    CHECK_OK(g.m.Appeal(Caught(true, Evidence::No)));
    CHECK(g.m.HasPendingDecision());
    CHECK_EQ(g.m.ReviewEntitledSide(), 0);  // batting side reviews an OUT
    DecisionRecord d;
    DeliveryRecord b;
    CHECK_OK(g.m.ReviewDecision(0, &d, &b));
    CHECK(d.result.outcome == ReviewOutcome::Overturned);
    CHECK(!d.result.finalOut);
    CHECK(!d.result.reviewLost);
    CHECK_EQ(g.m.ReviewsRemaining(0), 2);  // successful review is kept
    CHECK_EQ(Inn(g).Wickets(), 0);
    CHECK_EQ(Inn(g).Total(), 0);  // ball was dead when given out: the 2 runs don't count
    CHECK_EQ(Inn(g).LegalBalls(), 1);
    CHECK(!g.m.HasPendingDecision());
}

TEST(drs_caught_given_out_clear_edge_upheld_review_lost) {
    Game g;
    CHECK_OK(g.m.Appeal(Caught(true, Evidence::Yes)));
    DecisionRecord d;
    CHECK_OK(g.m.ReviewDecision(0, &d));
    CHECK(d.result.outcome == ReviewOutcome::Upheld);
    CHECK(d.result.finalOut && d.result.reviewLost);
    CHECK_EQ(g.m.ReviewsRemaining(0), 1);
    CHECK_EQ(d.reviewsLeftAfter, 1);
    CHECK_EQ(Inn(g).Wickets(), 1);
    CHECK(Inn(g).Batter(0).how == Dismissal::Caught);
    CHECK_EQ(Inn(g).Bowler(10).wickets, 1);
}

TEST(drs_caught_given_not_out_fielders_review_overturned) {
    Game g;
    CHECK_OK(g.m.Appeal(Caught(false, Evidence::Yes)));
    CHECK_EQ(g.m.ReviewEntitledSide(), 1);  // fielding side reviews a NOT OUT
    CHECK_ERR(g.m.ReviewDecision(0));       // batting side can't
    DecisionRecord d;
    CHECK_OK(g.m.ReviewDecision(1, &d));
    CHECK(d.result.outcome == ReviewOutcome::Overturned && d.result.finalOut);
    CHECK_EQ(Inn(g).Wickets(), 1);
    CHECK_EQ(Inn(g).Total(), 0);  // out: the runs never happened
    CHECK_EQ(g.m.ReviewsRemaining(1), 2);
}

TEST(drs_caught_not_out_accepted_keeps_runs) {
    Game g;
    CHECK_OK(g.m.Appeal(Caught(false, Evidence::No)));
    DecisionRecord d;
    CHECK_OK(g.m.AcceptDecision(&d));
    CHECK(d.result.outcome == ReviewOutcome::NotReviewed && !d.reviewed);
    CHECK_EQ(Inn(g).Total(), 2);  // play went on, the 2 runs count
    CHECK_EQ(Inn(g).Striker(), 0);
}

TEST(drs_caught_inconclusive_edge_on_field_stands) {
    {
        Game g;
        CHECK_OK(g.m.Appeal(Caught(true, Evidence::Inconclusive)));
        DecisionRecord d;
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(d.result.outcome == ReviewOutcome::Upheld && d.result.finalOut && d.result.reviewLost);
    }
    {
        Game g;
        CHECK_OK(g.m.Appeal(Caught(false, Evidence::Inconclusive)));
        DecisionRecord d;
        CHECK_OK(g.m.ReviewDecision(1, &d));
        CHECK(d.result.outcome == ReviewOutcome::Upheld && !d.result.finalOut && d.result.reviewLost);
        CHECK_EQ(g.m.ReviewsRemaining(1), 1);
    }
}

TEST(drs_caught_ball_grounded_not_out) {
    Game g;
    CHECK_OK(g.m.Appeal(Caught(true, Evidence::Yes, Evidence::Yes)));
    DecisionRecord d;
    CHECK_OK(g.m.ReviewDecision(0, &d));
    CHECK(d.result.outcome == ReviewOutcome::Overturned && !d.result.finalOut);
    // Doubt about a clean catch goes to the batter (no soft signal).
    Game h;
    CHECK_OK(h.m.Appeal(Caught(true, Evidence::Yes, Evidence::Inconclusive)));
    CHECK_OK(h.m.ReviewDecision(0, &d));
    CHECK(!d.result.finalOut);
}

TEST(drs_caught_no_edge_but_out_lbw) {
    Game g;
    DecisionCase c = Caught(true, Evidence::No);
    c.evidence.lbw = Lbw(WicketsZone::Hitting);
    CHECK_OK(g.m.Appeal(c));
    DecisionRecord d;
    CHECK_OK(g.m.ReviewDecision(0, &d));
    CHECK(d.result.finalOut);
    CHECK(d.result.finalMode == Dismissal::Lbw);
    CHECK(d.result.outcome == ReviewOutcome::Upheld && d.result.reviewLost);
    CHECK(Inn(g).Batter(0).how == Dismissal::Lbw);
    CHECK_EQ(Inn(g).Balls().back().delivery.fielder, -1);
}

TEST(drs_caught_no_edge_lbw_umpires_call_is_not_out) {
    Game g;
    DecisionCase c = Caught(true, Evidence::No);
    c.evidence.lbw = Lbw(WicketsZone::UmpiresCall);  // the LBW on-field call counts as not out
    CHECK_OK(g.m.Appeal(c));
    DecisionRecord d;
    CHECK_OK(g.m.ReviewDecision(0, &d));
    CHECK(!d.result.finalOut);
    CHECK(d.result.outcome == ReviewOutcome::Overturned && !d.result.reviewLost);
    CHECK_EQ(Inn(g).Wickets(), 0);
}

// ---------------------------------------------------------------- LBW

TEST(drs_lbw_reviews_through_match) {
    {
        Game g;
        CHECK_OK(g.m.Appeal(LbwCase(false, Lbw(WicketsZone::Hitting))));
        DecisionRecord d;
        CHECK_OK(g.m.ReviewDecision(1, &d));
        CHECK(d.result.outcome == ReviewOutcome::Overturned && d.result.finalOut);
        CHECK(Inn(g).Batter(0).how == Dismissal::Lbw);
        CHECK_EQ(Inn(g).Bowler(10).wickets, 1);
    }
    {
        Game g;  // umpire's call: out stays out, review kept
        CHECK_OK(g.m.Appeal(LbwCase(true, Lbw(WicketsZone::UmpiresCall))));
        DecisionRecord d;
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(d.result.outcome == ReviewOutcome::UmpiresCall && d.result.finalOut);
        CHECK_EQ(g.m.ReviewsRemaining(0), 2);
    }
    {
        Game g;  // pitched outside leg: overturned to not out
        CHECK_OK(g.m.Appeal(LbwCase(true, Lbw(WicketsZone::Hitting, PitchZone::OutsideLeg))));
        DecisionRecord d;
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(d.result.outcome == ReviewOutcome::Overturned && !d.result.finalOut);
    }
}

// ---------------------------------------------------------------- no-balls

TEST(drs_front_foot_no_ball_cancels_wicket_even_without_review) {
    Game g;
    DecisionCase c = Caught(true, Evidence::Yes);
    c.evidence.frontFootNoBall = true;
    CHECK_OK(g.m.Appeal(c));
    DecisionRecord d;
    CHECK_OK(g.m.AcceptDecision(&d));
    CHECK(!d.result.finalOut && d.result.noBall);
    CHECK_EQ(Inn(g).Wickets(), 0);
    CHECK_EQ(Inn(g).GetExtras().noBalls, 1);
    CHECK_EQ(Inn(g).Total(), 1);       // penalty only: given out, so ball was dead
    CHECK_EQ(Inn(g).LegalBalls(), 0);  // no-ball is not a legal delivery
    CHECK(Inn(g).NextBallIsFreeHit());
}

TEST(drs_no_ball_found_on_review) {
    Game g;
    DecisionCase c = LbwCase(true, Lbw(WicketsZone::Hitting));
    c.evidence.frontFootNoBall = true;
    CHECK_OK(g.m.Appeal(c));
    DecisionRecord d;
    CHECK_OK(g.m.ReviewDecision(0, &d));
    CHECK(d.result.noBall && !d.result.finalOut);
    CHECK(d.result.outcome == ReviewOutcome::Overturned && !d.result.reviewLost);
}

TEST(drs_run_out_stands_on_a_no_ball) {
    Game g;
    DecisionCase c = Referral(Dismissal::RunOut, Evidence::No);
    c.ifOut.runs = 1;
    c.ifOut.strikerOut = false;
    c.evidence.frontFootNoBall = true;
    DecisionRecord d;
    CHECK_OK(g.m.Appeal(c, &d));
    CHECK(d.result.finalOut && d.result.noBall);
    CHECK_EQ(Inn(g).Wickets(), 1);
    CHECK(Inn(g).Batter(1).out);
    CHECK_EQ(Inn(g).Total(), 2);  // 1 no-ball + 1 run completed
    CHECK(Inn(g).NextBallIsFreeHit());
}

// ---------------------------------------------------------------- referrals

TEST(drs_run_out_referrals) {
    for (Evidence e : {Evidence::No, Evidence::Yes, Evidence::Inconclusive}) {
        Game g;
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(Referral(Dismissal::RunOut, e), &d));
        CHECK(d.result.outcome == ReviewOutcome::ThirdUmpire);
        CHECK(!d.reviewed && !d.result.reviewLost);
        CHECK_EQ(d.result.finalOut, e == Evidence::No);  // doubt -> not out
        CHECK(!g.m.HasPendingDecision());                // decided at once
        CHECK_EQ(g.m.ReviewsRemaining(0), 2);
        CHECK_EQ(g.m.ReviewsRemaining(1), 2);
        CHECK_EQ(Inn(g).Bowler(10).wickets, 0);  // never the bowler's wicket
    }
}

TEST(drs_stumping_referrals) {
    {
        Game g;  // stumped off a wide: out, and the wide still counts
        DecisionCase c = Referral(Dismissal::Stumped, Evidence::No);
        c.ifOut.extra = ExtraType::Wide;
        c.ifNotOut.extra = ExtraType::Wide;
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(c, &d));
        CHECK(d.result.finalOut);
        CHECK_EQ(Inn(g).Wickets(), 1);
        CHECK_EQ(Inn(g).GetExtras().wides, 1);
        CHECK_EQ(Inn(g).LegalBalls(), 0);
        CHECK_EQ(Inn(g).Bowler(10).wickets, 1);
    }
    {
        Game g;  // stumping on a no-ball: not out
        DecisionCase c = Referral(Dismissal::Stumped, Evidence::No);
        c.evidence.frontFootNoBall = true;
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(c, &d));
        CHECK(!d.result.finalOut && d.result.noBall);
        CHECK_EQ(Inn(g).GetExtras().noBalls, 1);
    }
    {
        Game g;  // back in time
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(Referral(Dismissal::Stumped, Evidence::Yes), &d));
        CHECK(!d.result.finalOut);
        CHECK_EQ(Inn(g).Wickets(), 0);
    }
}

TEST(drs_clean_catch_referrals) {
    {
        Game g;
        DecisionCase c = Caught(false, Evidence::Yes, Evidence::No);
        c.kind = DecisionKind::UmpireReferral;
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(c, &d));
        CHECK(d.result.finalOut && d.result.outcome == ReviewOutcome::ThirdUmpire);
    }
    {
        Game g;  // inconclusive -> not out, and the batters' runs count
        DecisionCase c = Caught(false, Evidence::Yes, Evidence::Inconclusive);
        c.kind = DecisionKind::UmpireReferral;
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(c, &d));
        CHECK(!d.result.finalOut);
        CHECK_EQ(Inn(g).Total(), 2);
    }
}

// ---------------------------------------------------------------- review counts and flow

TEST(drs_review_limits_per_innings) {
    MatchRules r = MatchRules::T20();
    Game g(r);
    // Two failed reviews by the batting side...
    for (int i = 0; i < 2; ++i) {
        CHECK_OK(g.m.Appeal(LbwCase(true, Lbw(WicketsZone::Hitting))));
        CHECK_OK(g.m.ReviewDecision(0));
        CHECK_OK(g.m.SetNextBatter(2 + i));
    }
    CHECK_EQ(g.m.ReviewsRemaining(0), 0);
    // ...and the third is refused; the decision must then be accepted.
    CHECK_OK(g.m.Appeal(LbwCase(true, Lbw(WicketsZone::Missing))));
    CHECK_ERR(g.m.ReviewDecision(0));
    CHECK_OK(g.m.AcceptDecision());
    CHECK_EQ(Inn(g).Wickets(), 3);  // a wrong decision stood: no reviews left
    // The fielding side still has both of theirs.
    CHECK_EQ(g.m.ReviewsRemaining(1), 2);

    // Next innings: fresh reviews for both sides.
    CHECK_OK(g.m.SetNextBatter(4));
    Delivery bowled;
    bowled.dismissal = Dismissal::Bowled;
    for (int b = 5; g.m.AllInnings().size() == 1; ++b) {
        if (Inn(g).State() == InningsState::AwaitingBowler) g.m.SetBowler(Inn(g).LastOverBowler() == 10 ? 9 : 10);
        CHECK_OK(g.m.Bowl(bowled));
        if (Inn(g).State() == InningsState::AwaitingBatter) g.m.SetNextBatter(b);
    }
    CHECK_EQ(g.m.ReviewsRemaining(0), 2);
    CHECK_EQ(g.m.ReviewsRemaining(1), 2);
}

TEST(drs_pending_decision_blocks_play) {
    Game g(MatchRules::Test());
    CHECK_OK(g.m.Appeal(Caught(true, Evidence::Yes)));
    CHECK_ERR(g.m.Bowl(Runs(1)));
    CHECK_ERR(g.m.Appeal(Caught(true, Evidence::Yes)));
    CHECK_ERR(g.m.Declare());
    CHECK_OK(g.m.AcceptDecision());
    CHECK_ERR(g.m.AcceptDecision());  // nothing pending now
    CHECK_ERR(g.m.ReviewDecision(0));
    // Waiting for a new batter: no appeal possible.
    CHECK_ERR(g.m.Appeal(Caught(true, Evidence::Yes)));
}

TEST(drs_invalid_cases_rejected) {
    Game g;
    DecisionCase c = Caught(true, Evidence::Yes);
    c.ifOut.dismissal = Dismissal::Bowled;
    CHECK_ERR(g.m.Appeal(c));
    c = Caught(true, Evidence::Yes);
    c.ifNotOut.dismissal = Dismissal::Caught;
    CHECK_ERR(g.m.Appeal(c));
    c = LbwCase(true, Lbw(WicketsZone::Hitting));
    c.evidence.lbw.reset();
    CHECK_ERR(g.m.Appeal(c));  // LBW needs ball tracking
    c = Referral(Dismissal::RunOut, Evidence::No);
    c.kind = DecisionKind::PlayerAppeal;
    CHECK_ERR(g.m.Appeal(c));  // run outs are referred, not reviewed
    c = LbwCase(true, Lbw(WicketsZone::Hitting));
    c.kind = DecisionKind::UmpireReferral;
    CHECK_ERR(g.m.Appeal(c));  // umpires don't refer LBW
    c = Caught(true, Evidence::Yes);
    c.ifOut.runs = 1;
    CHECK_ERR(g.m.Appeal(c));  // no runs on a catch
    CHECK(!g.m.HasPendingDecision());

    // Free hit: a catch can't be out, so the appeal is invalid.
    CHECK_OK(g.m.Bowl([] { Delivery d; d.extra = ExtraType::NoBall; return d; }()));
    CHECK_ERR(g.m.Appeal(Caught(true, Evidence::Yes)));
    CHECK_OK(g.m.Appeal(Referral(Dismissal::RunOut, Evidence::No)));  // run out still possible
    CHECK_EQ(Inn(g).Wickets(), 1);
}

TEST(drs_wicket_on_last_ball_of_over_via_review) {
    Game g;
    for (int i = 0; i < 5; ++i) CHECK_OK(g.m.Bowl(Runs(0)));
    CHECK_OK(g.m.Appeal(Caught(false, Evidence::Yes)));
    CHECK_OK(g.m.ReviewDecision(1));
    CHECK_EQ(Inn(g).CompletedOvers(), 1);
    CHECK_EQ(Inn(g).Wickets(), 1);
    CHECK(Inn(g).State() == InningsState::AwaitingBatter);
    CHECK_EQ(Inn(g).Bowler(10).maidens, 1);
    CHECK_EQ(Inn(g).LastOverBowler(), 10);
    CHECK_EQ(g.m.Decisions().size(), size_t{1});
    CHECK_EQ(g.m.Decisions()[0].over, 0);
    CHECK_EQ(g.m.Decisions()[0].ballInOver, 5);
}

TEST(drs_judge_functions_are_pure_and_consistent) {
    DecisionCase c = Caught(true, Evidence::No);
    DrsOutcome a = JudgeReview(c), b = JudgeReview(c);
    CHECK(a.finalOut == b.finalOut && a.outcome == b.outcome && a.reason == b.reason);
    CHECK(!a.reason.empty());
    CHECK(IsPlayerReviewable(Dismissal::Caught) && IsPlayerReviewable(Dismissal::Lbw));
    CHECK(!IsPlayerReviewable(Dismissal::RunOut) && !IsPlayerReviewable(Dismissal::Bowled));
    CHECK(IsUmpireReferable(Dismissal::RunOut) && IsUmpireReferable(Dismissal::Stumped));
    CHECK(!IsUmpireReferable(Dismissal::Lbw));
    // Without a review the on-field call stands, even if wrong.
    DrsOutcome w = JudgeWithoutReview(Caught(true, Evidence::No));
    CHECK(w.finalOut && w.outcome == ReviewOutcome::NotReviewed);
}

// ---------------------------------------------------------------- commentary

TEST(drs_commentary_lines_in_both_languages) {
    for (Language lang : {Language::English, Language::Hindi}) {
        Game g;
        CommentaryEngine com(lang, 3);
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(Caught(true, Evidence::No)));
        CHECK_OK(g.m.ReviewDecision(0, &d));
        auto lines = com.DescribeDecision(g.m, d);
        CHECK(lines.size() >= 3);
        for (auto& l : lines) CHECK(!l.text.empty());
        CHECK(lines[2].category == "review.overturned");

        CHECK_OK(g.m.Appeal(Referral(Dismissal::RunOut, Evidence::No), &d));
        lines = com.DescribeDecision(g.m, d);
        CHECK(!lines.empty() && lines[0].category == "thirdumpire.out");
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

// Two humans, toss done, openers and bowler set: ready for a delivery.
struct HumanGame {
    MatchSession s{MatchRules::T20(), TwoTeams(), 77};
    uint64_t seq[2] = {0, 0};
    std::string name[2] = {"india-player", "pakistan-player"};
    int batting = -1;
    Status Send(int side, CommandType t, int a = 0, int b = 0) {
        return s.Submit(Cmd(name[side], ++seq[side], t, a, b));
    }
    HumanGame() {
        s.Join(name[0], 0);
        s.Join(name[1], 1);
        Send(1, CommandType::TossCall, 0);
        Send(s.GetToss().Winner(), CommandType::TossDecision, 0);
        batting = s.GetMatch().BattingSide();
        Send(batting, CommandType::SetOpeners, 0, 1);
        Send(1 - batting, CommandType::SetBowler, 10);
    }
};
}  // namespace

TEST(drs_session_human_review_flow) {
    HumanGame h;
    CHECK(h.s.WaitingFor().what == "delivery");
    CHECK_OK(h.s.ResolveAppeal(Caught(true, Evidence::No)));
    Pending p = h.s.WaitingFor();
    CHECK(p.what == "review");
    CHECK_EQ(p.side, h.batting);
    CHECK_ERR(h.s.ResolveDelivery(Delivery{}));                      // play is held
    CHECK_ERR(h.Send(1 - h.batting, CommandType::Review, 1));         // not their review
    CHECK_OK(h.Send(h.batting, CommandType::Review, 1));
    CHECK(h.s.WaitingFor().what == "delivery");
    CHECK_EQ(h.s.GetMatch().Current().Wickets(), 0);
    bool sawAppeal = false, sawReview = false;
    for (const auto& e : h.s.EventsSince(0)) {
        sawAppeal |= e.kind == "appeal";
        if (e.kind == "review") {
            sawReview = true;
            CHECK(e.decision.has_value());
            CHECK(e.decision->result.outcome == ReviewOutcome::Overturned);
        }
    }
    CHECK(sawAppeal && sawReview);
    // A replayed review command is rejected as a duplicate.
    CHECK_ERR(h.s.Submit(Cmd(h.name[h.batting], h.seq[h.batting], CommandType::Review, 1)));
}

TEST(drs_session_review_window_expiry_and_accept) {
    HumanGame h;
    CHECK_OK(h.s.ResolveAppeal(Caught(true, Evidence::Yes)));
    CHECK_OK(h.s.ExpireReviewWindow());  // timer ran out: on-field OUT stands
    CHECK_EQ(h.s.GetMatch().Current().Wickets(), 1);
    CHECK_ERR(h.s.ExpireReviewWindow());
    CHECK(h.s.WaitingFor().what == "batter");
    CHECK_OK(h.Send(h.batting, CommandType::SetNextBatter, 2));
    CHECK_OK(h.s.ResolveAppeal(Caught(false, Evidence::No)));
    CHECK_OK(h.Send(1 - h.batting, CommandType::Review, 0));  // fielders accept the not out
    CHECK_EQ(h.s.GetMatch().Current().Wickets(), 1);
    CHECK_EQ(h.s.GetMatch().ReviewsRemaining(1 - h.batting), 2);
}

TEST(drs_session_referral_needs_no_player) {
    HumanGame h;
    CHECK_OK(h.s.ResolveAppeal(Referral(Dismissal::RunOut, Evidence::No)));
    CHECK(h.s.WaitingFor().what == "batter");
    CHECK_EQ(h.s.EventsSince(h.s.LastEventId() - 1).size(), size_t{1});
    bool sawThird = false;
    for (const auto& e : h.s.EventsSince(0)) sawThird |= e.kind == "third-umpire";
    CHECK(sawThird);
}

TEST(drs_session_ai_reviews_on_its_own) {
    MatchSession s(MatchRules::T20(), TwoTeams(), 5);  // both sides AI
    s.RunAi();
    CHECK(s.WaitingFor().what == "delivery");
    CHECK_OK(s.ResolveAppeal(Caught(false, Evidence::Yes)));
    CHECK(s.WaitingFor().what != "review");  // AI answered straight away
    CHECK_EQ(s.GetMatch().Decisions().size(), size_t{1});
}

// Full AI-vs-AI matches where close calls go through appeals and reviews.
static void FullMatchWithDrs(MatchRules rules, uint64_t seed) {
    MatchSession s(rules, TwoTeams(), seed);
    s.RunAi();
    QuickSim sim(seed + 1);
    CommentaryEngine com(seed % 2 ? Language::Hindi : Language::English, seed);
    int guard = 0;
    while (s.WaitingFor().what == "delivery" && guard++ < 20000) {
        Delivery d = sim.Next(s.GetMatch());
        if (auto c = sim.MakeCase(s.GetMatch(), d)) CHECK_OK(s.ResolveAppeal(*c));
        else CHECK_OK(s.ResolveDelivery(d));
        for (int side = 0; side < 2; ++side) CHECK(s.GetMatch().ReviewsRemaining(side) >= 0);
    }
    const Match& m = s.GetMatch();
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK(!m.HasPendingDecision());
    // No side ever lost more reviews in an innings than it was given.
    for (size_t i = 0; i < m.AllInnings().size(); ++i) {
        int lost[2] = {0, 0};
        for (const auto& d : m.Decisions())
            if (d.inningsIndex == static_cast<int>(i) && d.reviewed && d.result.reviewLost) lost[d.reviewingSide]++;
        CHECK(lost[0] <= rules.reviewsPerInnings && lost[1] <= rules.reviewsPerInnings);
    }
    for (const auto& d : m.Decisions()) {
        CHECK(!d.result.reason.empty());
        for (const auto& l : com.DescribeDecision(m, d)) CHECK(!l.text.empty());
    }
    for (const auto& inn : m.AllInnings()) {
        int bat = 0;
        for (int i = 0; i < 11; ++i) bat += inn.Batter(i).runs;
        CHECK_EQ(bat + inn.GetExtras().Total(), inn.Total());
    }
}

TEST(drs_full_matches_with_appeals_complete) {
    int decisions = 0;
    for (uint64_t seed = 1; seed <= 20; ++seed) FullMatchWithDrs(MatchRules::T20(), seed);
    for (uint64_t seed = 50; seed < 54; ++seed) FullMatchWithDrs(MatchRules::ODI(), seed);
    for (uint64_t seed = 80; seed < 83; ++seed) FullMatchWithDrs(MatchRules::Test(), seed);
    // Sanity: the simulator really produces appeals.
    MatchSession s(MatchRules::T20(), TwoTeams(), 9);
    s.RunAi();
    QuickSim sim(10);
    while (s.WaitingFor().what == "delivery") {
        Delivery d = sim.Next(s.GetMatch());
        if (auto c = sim.MakeCase(s.GetMatch(), d)) s.ResolveAppeal(*c);
        else s.ResolveDelivery(d);
    }
    decisions = static_cast<int>(s.GetMatch().Decisions().size());
    CHECK(decisions > 0);
}
