// Stage A: completing DRS and related scoring laws.
//  * hit-wicket reviews, obstruction referrals
//  * wide / height no-ball reviews (when the ruleset allows), TV boundary checks
//  * short runs (Law 18.4), penalty runs (Law 41), the new ball (Law 4)
//  * AI reviews that never see the TV evidence (perception-based)
//  * DRS reasons in Hindi
#include <fstream>
#include <sstream>

#include "Test.h"
#include "cricket/AutoCaptain.h"
#include "cricket/Commentary.h"
#include "cricket/Drs.h"
#include "cricket/Match.h"
#include "cricket/MatchSession.h"
#include "cricket/ReviewAI.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

namespace {

std::array<Team, 2> TwoTeams() {
    auto all = FictionalTeams();
    return {all[0], all[1]};
}

MatchRules WithDrs(DrsRules d, MatchRules r = MatchRules::T20()) {
    r.drs = d;
    return r;
}

DrsRules CallsAllowed() {
    DrsRules d = DrsRules::Icc();
    d.reviewWidesAndNoBalls = true;
    return d;
}

struct Game {
    Match m;
    explicit Game(MatchRules r = MatchRules::T20(), int battingFirst = 0) : m(r, TwoTeams(), battingFirst) {
        m.SetOpeners(0, 1);
        m.SetBowler(10);
    }
    const Innings& I() const { return m.Current(); }
};

Delivery Runs(int r, bool boundary = false) {
    Delivery d;
    d.runs = r;
    d.boundary = boundary;
    return d;
}

Delivery Wide(int runs = 0) {
    Delivery d;
    d.extra = ExtraType::Wide;
    d.runs = runs;
    return d;
}

DecisionCase WideCall(bool signalled, Evidence isWide, DecisionKind kind = DecisionKind::PlayerAppeal) {
    DecisionCase c;
    c.kind = kind;
    c.call = ScoringCall::Wide;
    c.onFieldCalled = signalled;
    c.ifCalled = Wide();
    c.ifNotCalled = Delivery{};
    c.evidence.callTrue = isWide;
    return c;
}

DecisionCase HitWicket(bool onFieldOut, Evidence broken, DecisionKind kind = DecisionKind::PlayerAppeal) {
    DecisionCase c;
    c.kind = kind;
    c.mode = Dismissal::HitWicket;
    c.onFieldOut = onFieldOut;
    c.ifOut.dismissal = Dismissal::HitWicket;
    c.evidence.wicketBrokenByBatter = broken;
    return c;
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

}  // namespace

// ---------------------------------------------------------------- hit wicket / obstruction

TEST(drsc_hit_wicket_review) {
    {
        Game g;
        CHECK_OK(g.m.Appeal(HitWicket(true, Evidence::No)));  // the keeper broke it, not the batter
        DecisionRecord d;
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(d.result.outcome == ReviewOutcome::Overturned && !d.result.finalOut);
        CHECK(d.result.code == ReasonCode::WicketNotBrokenByBatter);
        CHECK_EQ(g.I().Wickets(), 0);
    }
    {
        Game g;
        CHECK_OK(g.m.Appeal(HitWicket(true, Evidence::Inconclusive)));
        DecisionRecord d;
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(d.result.finalOut && d.result.reviewLost);
        CHECK(g.I().Batter(0).how == Dismissal::HitWicket);
        CHECK_EQ(g.I().Bowler(10).wickets, 1);  // hit wicket is the bowler's
    }
    {
        Game g;  // umpires can refer it too
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(HitWicket(false, Evidence::Inconclusive, DecisionKind::UmpireReferral), &d));
        CHECK(!d.result.finalOut && d.result.code == ReasonCode::BenefitOfDoubt);
    }
}

TEST(drsc_obstructing_field_referral) {
    for (Evidence wilful : {Evidence::Yes, Evidence::No, Evidence::Inconclusive}) {
        Game g;
        DecisionCase c;
        c.kind = DecisionKind::UmpireReferral;
        c.mode = Dismissal::ObstructingField;
        c.ifOut.dismissal = Dismissal::ObstructingField;
        c.ifOut.strikerOut = false;
        c.evidence.wilfulObstruction = wilful;
        c.evidence.frontFootNoBall = true;  // Law 21.18: still out off a no-ball
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(c, &d));
        CHECK_EQ(d.result.finalOut, wilful == Evidence::Yes);
        CHECK_EQ(g.I().GetExtras().noBalls, 1);
        if (wilful == Evidence::Yes) {
            CHECK(g.I().Batter(1).out);
            CHECK_EQ(g.I().Bowler(10).wickets, 0);
        }
    }
    // Players cannot review obstruction; it is referred.
    Game g;
    DecisionCase c;
    c.mode = Dismissal::ObstructingField;
    c.ifOut.dismissal = Dismissal::ObstructingField;
    CHECK_ERR(g.m.Appeal(c));
}

// ---------------------------------------------------------------- scoring calls

TEST(drsc_wide_review_overturned_becomes_legal_ball) {
    Game g(WithDrs(CallsAllowed()));
    CHECK_OK(g.m.Appeal(WideCall(true, Evidence::No)));
    CHECK_EQ(g.m.ReviewEntitledSide(), 1);  // fielders challenge a wide
    CHECK_ERR(g.m.ReviewDecision(0));
    DecisionRecord d;
    CHECK_OK(g.m.ReviewDecision(1, &d));
    CHECK(d.result.outcome == ReviewOutcome::Overturned && !d.result.finalCalled && !d.result.reviewLost);
    CHECK_EQ(g.I().GetExtras().wides, 0);
    CHECK_EQ(g.I().LegalBalls(), 1);
    CHECK_EQ(g.m.ReviewsRemaining(1), 2);
}

TEST(drsc_wide_review_upheld_or_inconclusive_costs_review) {
    for (Evidence e : {Evidence::Yes, Evidence::Inconclusive}) {
        Game g(WithDrs(CallsAllowed()));
        CHECK_OK(g.m.Appeal(WideCall(true, e)));
        DecisionRecord d;
        CHECK_OK(g.m.ReviewDecision(1, &d));
        CHECK(d.result.outcome == ReviewOutcome::Upheld && d.result.finalCalled && d.result.reviewLost);
        CHECK_EQ(g.I().GetExtras().wides, 1);
        CHECK_EQ(g.m.ReviewsRemaining(1), 1);
    }
}

TEST(drsc_batters_can_ask_for_a_wide) {
    Game g(WithDrs(CallsAllowed()));
    CHECK_OK(g.m.Appeal(WideCall(false, Evidence::Yes)));
    CHECK_EQ(g.m.ReviewEntitledSide(), 0);
    CHECK_OK(g.m.ReviewDecision(0));
    CHECK_EQ(g.I().GetExtras().wides, 1);
    CHECK_EQ(g.I().LegalBalls(), 0);
}

TEST(drsc_wide_calls_not_reviewable_under_default_rules) {
    Game g;  // ICC default: wides and no-balls are not player-reviewable
    DecisionRecord d;
    CHECK_OK(g.m.Appeal(WideCall(true, Evidence::No), &d));
    CHECK(!g.m.HasPendingDecision());
    CHECK(d.result.outcome == ReviewOutcome::NotReviewed && d.result.finalCalled);
    CHECK_EQ(g.I().GetExtras().wides, 1);  // the (wrong) on-field call stands
}

TEST(drsc_height_no_ball_review) {
    Game g(WithDrs(CallsAllowed()));
    DecisionCase c;
    c.call = ScoringCall::HeightNoBall;
    c.onFieldCalled = true;
    c.ifCalled.extra = ExtraType::NoBall;
    c.ifCalled.runs = 2;
    c.ifNotCalled = Runs(2);
    c.evidence.callTrue = Evidence::No;  // it was below the waist
    CHECK_OK(g.m.Appeal(c));
    CHECK_OK(g.m.ReviewDecision(1));
    CHECK_EQ(g.I().GetExtras().noBalls, 0);
    CHECK_EQ(g.I().Batter(0).runs, 2);
    CHECK(!g.I().NextBallIsFreeHit());
    CHECK_EQ(g.I().LegalBalls(), 1);
}

TEST(drsc_boundary_check_referral) {
    DecisionCase c;
    c.kind = DecisionKind::UmpireReferral;
    c.call = ScoringCall::Boundary;
    c.onFieldCalled = true;  // signalled six
    c.ifCalled = Runs(6, true);
    c.ifNotCalled = Runs(4, true);
    {
        Game g;
        c.evidence.callTrue = Evidence::No;  // fielder's foot on the rope while the ball was in the air
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(c, &d));
        CHECK(d.result.outcome == ReviewOutcome::ThirdUmpire && !d.result.finalCalled);
        CHECK_EQ(g.I().Batter(0).fours, 1);
        CHECK_EQ(g.I().Total(), 4);
    }
    {
        Game g;
        c.evidence.callTrue = Evidence::Inconclusive;  // stays as signalled
        CHECK_OK(g.m.Appeal(c));
        CHECK_EQ(g.I().Batter(0).sixes, 1);
    }
    {
        Game g(WithDrs(DrsRules::NoTechnology()));  // no TV umpire: on-field signal is final
        c.evidence.callTrue = Evidence::No;
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(c, &d));
        CHECK(d.result.outcome == ReviewOutcome::NotReviewed);
        CHECK_EQ(g.I().Total(), 6);
    }
}

TEST(drsc_invalid_scoring_call_cases) {
    Game g(WithDrs(CallsAllowed()));
    DecisionCase c = WideCall(true, Evidence::No);
    c.kind = DecisionKind::UmpireReferral;
    CHECK_ERR(g.m.Appeal(c));  // umpires don't refer wides
    DecisionCase b;
    b.call = ScoringCall::Boundary;
    b.ifCalled = Runs(6, true);
    b.ifNotCalled = Runs(4, true);
    CHECK_ERR(g.m.Appeal(b));  // players can't review a boundary
    c = WideCall(true, Evidence::No);
    c.ifCalled = Runs(1);
    CHECK_ERR(g.m.Appeal(c));  // "called" version must be a wide
    c = WideCall(true, Evidence::No);
    c.ifNotCalled.dismissal = Dismissal::Bowled;
    CHECK_ERR(g.m.Appeal(c));
    CHECK(!g.m.HasPendingDecision());
}

// ---------------------------------------------------------------- short runs (Law 18.4)

TEST(drsc_short_runs) {
    {
        Game g;
        Delivery d = Runs(2);
        d.shortRuns = 1;  // two attempted, one short
        CHECK_OK(g.m.Bowl(d));
        CHECK_EQ(g.I().Total(), 1);
        CHECK_EQ(g.I().Batter(0).runs, 1);
        CHECK_EQ(g.I().Striker(), 0);  // two runs attempted: back at the same ends
        CHECK_EQ(g.I().Bowler(10).runs, 1);
    }
    {
        Game g;
        Delivery d = Runs(3);
        d.shortRuns = 1;
        CHECK_OK(g.m.Bowl(d));
        CHECK_EQ(g.I().Total(), 2);
        CHECK_EQ(g.I().Striker(), 1);  // three attempted: ends changed
    }
    {
        Game g;  // short run off a wide: wide penalty still counts
        Delivery d = Wide(2);
        d.shortRuns = 1;
        CHECK_OK(g.m.Bowl(d));
        CHECK_EQ(g.I().GetExtras().wides, 2);
    }
    {
        Game g;  // all runs short
        Delivery d = Runs(1);
        d.shortRuns = 1;
        CHECK_OK(g.m.Bowl(d));
        CHECK_EQ(g.I().Total(), 0);
        CHECK_EQ(g.I().Striker(), 1);
    }
    Game g;
    Delivery bad = Runs(1);
    bad.shortRuns = 2;
    CHECK_ERR(g.m.Bowl(bad));
    bad = Runs(4, true);
    bad.shortRuns = 1;
    CHECK_ERR(g.m.Bowl(bad));
    bad = Runs(1);
    bad.shortRuns = -1;
    CHECK_ERR(g.m.Bowl(bad));
}

// ---------------------------------------------------------------- penalty runs (Law 41)

TEST(drsc_penalty_runs_to_batting_side) {
    Game g;
    CHECK_OK(g.m.AwardPenaltyRuns(0, 5, "ball hit a fielder's helmet on the ground"));
    CHECK_EQ(g.I().Total(), 5);
    CHECK_EQ(g.I().GetExtras().penalty, 5);
    CHECK_EQ(g.I().GetExtras().Total(), 5);
    CHECK_EQ(g.m.Penalties().size(), size_t{1});
    CHECK_EQ(g.m.Penalties()[0].inningsIndex, 0);
    CHECK_ERR(g.m.AwardPenaltyRuns(0, 0, "x"));
    CHECK_ERR(g.m.AwardPenaltyRuns(2, 5, "x"));
}

TEST(drsc_penalty_to_side_yet_to_bat_added_to_their_innings) {
    Match m(MatchRules::Custom(1), TwoTeams(), 0);
    CHECK_OK(m.AwardPenaltyRuns(1, 5, "batter damaged the pitch"));  // fielding side hasn't batted
    CHECK_EQ(m.Penalties()[0].inningsIndex, -1);
    Feed(m, Runs(1), 6);  // side 0: 6
    CHECK_EQ(m.AllInnings().size(), size_t{2});
    CHECK_EQ(m.Current().Total(), 5);  // side 1 starts on 5
    CHECK_EQ(m.Penalties()[0].inningsIndex, 1);
    CHECK_EQ(*m.Current().Target(), 7);
}

TEST(drsc_penalty_to_fielding_side_raises_target) {
    Match m(MatchRules::Custom(2), TwoTeams(), 0);
    Feed(m, Runs(1), 12);  // 12
    CHECK_EQ(*m.Current().Target(), 13);
    CHECK_OK(m.AwardPenaltyRuns(0, 5, "fielding side wasted time"));
    CHECK_EQ(m.AllInnings()[0].Total(), 17);
    CHECK_EQ(*m.Current().Target(), 18);
}

TEST(drsc_penalty_runs_can_win_the_match) {
    Match m(MatchRules::Custom(1), TwoTeams(), 0);
    Feed(m, Runs(1), 6);  // target 7
    Feed(m, Runs(1), 3);  // 3
    CHECK_OK(m.AwardPenaltyRuns(1, 5, "illegal fielding"));
    CHECK(m.Phase() == MatchPhase::Complete);
    CHECK_EQ(m.Result().winner, 1);
    CHECK_ERR(m.AwardPenaltyRuns(1, 5, "after the match"));
}

TEST(drsc_penalty_in_fourth_innings_recomputes_target) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Delivery out;
    out.dismissal = Dismissal::Bowled;
    Feed(m, Runs(1), 100);
    Feed(m, out, 10);  // A 100
    Feed(m, Runs(1), 80);
    Feed(m, out, 10);  // B 80
    Feed(m, Runs(1), 50);
    Feed(m, out, 10);  // A 50 -> target 71
    CHECK_EQ(*m.Current().Target(), 71);
    CHECK_OK(m.AwardPenaltyRuns(0, 5, "fielders' helmet"));  // to A (fielding, batted twice)
    CHECK_EQ(*m.Current().Target(), 76);
}

TEST(drsc_penalty_blocked_while_decision_pending) {
    Game g;
    DecisionCase c = HitWicket(true, Evidence::Yes);
    CHECK_OK(g.m.Appeal(c));
    CHECK_ERR(g.m.AwardPenaltyRuns(0, 5, "x"));
}

// ---------------------------------------------------------------- new ball

TEST(drsc_new_ball_after_80_overs) {
    Match m(MatchRules::Test(), TwoTeams(), 0);
    Feed(m, Runs(0), 79 * 6);
    CHECK(!m.Current().NewBallDue());
    CHECK_ERR(m.TakeNewBall());
    Feed(m, Runs(0), 6);
    CHECK(m.Current().NewBallDue());
    Feed(m, Runs(0), 1);
    CHECK_ERR(m.TakeNewBall());  // mid-over
    Feed(m, Runs(0), 5);
    CHECK_OK(m.TakeNewBall());
    CHECK_EQ(m.Current().OversWithCurrentBall(), 0);
    CHECK(!m.Current().NewBallDue());
    Game t20;
    CHECK_ERR(t20.m.TakeNewBall());
}

TEST(drsc_ai_takes_new_ball_in_session) {
    MatchSession s(MatchRules::Test(), TwoTeams(), 3);
    s.RunAi();
    QuickSim sim(4);
    while (s.WaitingFor().what == "delivery" && s.GetMatch().Current().CompletedOvers() < 82 &&
           s.GetMatch().AllInnings().size() == 1) {
        Delivery d = Runs(0);  // defensive batting so the innings lasts
        CHECK_OK(s.ResolveDelivery(d));
    }
    (void)sim;
    bool taken = false;
    for (const auto& e : s.EventsSince(0)) taken |= e.kind == "new-ball";
    CHECK(taken);
    CHECK(s.GetMatch().Current().OversWithCurrentBall() < 80);
}

// ---------------------------------------------------------------- AI information hiding

TEST(drsc_ai_review_code_never_reads_evidence) {
    std::ifstream f(std::string(CRICKET_SOURCE_DIR) + "/core/src/ReviewAI.cpp");
    CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();
    // Ignore comment lines, then look for anything that reveals the replay.
    std::string code;
    std::istringstream lines(src);
    for (std::string line; std::getline(lines, line);) {
        auto pos = line.find("//");
        code += (pos == std::string::npos ? line : line.substr(0, pos)) + "\n";
    }
    for (const char* banned : {"evidence", "Evidence", "Judge", "FinalDelivery", "LbwIsOut", "ReviewLbw", "callTrue",
                               "batContact", "Grounded", "QuickSim"}) {
        if (code.find(banned) != std::string::npos) std::cerr << "  forbidden token in ReviewAI.cpp: " << banned << "\n";
        CHECK(code.find(banned) == std::string::npos);
    }
}

TEST(drsc_ai_decision_ignores_evidence_and_follows_belief) {
    // Same perception + situation, opposite evidence -> identical decisions.
    int sameDecisions = 0, reviewsWhenSure = 0, reviewsWhenDoubtful = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        bool decided[2];
        for (int variant = 0; variant < 2; ++variant) {
            Game g;
            DecisionCase c;
            c.mode = Dismissal::Caught;
            c.onFieldOut = true;
            c.ifOut.dismissal = Dismissal::Caught;
            c.evidence.batContact = variant == 0 ? Evidence::Yes : Evidence::No;
            c.perceived = {0.3, 0.9};  // batters think "probably not out"
            CHECK_OK(g.m.Appeal(c));
            AutoCaptain ai(seed);
            decided[variant] = ai.ShouldReview(g.m, 0);
        }
        sameDecisions += decided[0] == decided[1];
    }
    CHECK_EQ(sameDecisions, 200);
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        for (double belief : {0.1, 0.9}) {
            Game g;
            DecisionCase c;
            c.mode = Dismissal::Caught;
            c.onFieldOut = true;
            c.ifOut.dismissal = Dismissal::Caught;
            c.perceived = {1.0 - belief, 0.5};
            CHECK_OK(g.m.Appeal(c));
            AutoCaptain ai(seed);
            (belief > 0.5 ? reviewsWhenSure : reviewsWhenDoubtful) += ai.ShouldReview(g.m, 0);
        }
    }
    CHECK(reviewsWhenSure > 180);
    CHECK(reviewsWhenDoubtful < 20);
}

TEST(drsc_ai_context_for_scoring_calls) {
    Game g(WithDrs(CallsAllowed()));
    DecisionCase c = WideCall(true, Evidence::No);
    c.perceived = {0.9, 0.05};  // fielders are sure it was NOT a wide
    CHECK_OK(g.m.Appeal(c));
    ReviewContext ctx = BuildReviewContext(g.m, 1);
    CHECK(!ctx.isDismissal);
    CHECK(!ctx.battingSideReviewing);
    CHECK(ctx.successBelief > 0.9);
    CHECK(ctx.runsAtStake >= 2);
    AutoCaptain ai(1, Difficulty::WorldClass);
    CHECK(ai.ShouldReview(g.m, 1));
    CHECK(!ai.ShouldReview(g.m, 0));  // not the batters' review
}

TEST(drsc_ai_personality_and_context) {
    std::mt19937_64 rng(1);
    ReviewContext c;
    c.successBelief = 0.45;
    c.reviewsLeft = 1;
    c.reviewsPerInnings = 2;
    c.oversLeft = 15;
    c.batterQuality = 40;
    int cautious = 0, gambler = 0;
    for (int i = 0; i < 300; ++i) {
        cautious += DecideReview(c, ReviewPersonality::Cautious, 0.1, rng);
        gambler += DecideReview(c, ReviewPersonality::Gambler, 0.1, rng);
    }
    CHECK(gambler > cautious);
    // Star batter on 90 is worth a punt; a tail-ender at 0 much less so.
    ReviewContext star = c, tail = c;
    star.batterQuality = 90;
    star.batterRuns = 90;
    tail.batterQuality = 15;
    tail.wicketsDown = 9;
    CHECK(DecideReview(star, ReviewPersonality::Balanced, 0.0, rng));
    CHECK(!DecideReview(tail, ReviewPersonality::Balanced, 0.0, rng));
    // No reviews left: never.
    ReviewContext none = star;
    none.reviewsLeft = 0;
    CHECK(!DecideReview(none, ReviewPersonality::Gambler, 0.0, rng));
}

TEST(drsc_sim_perception_is_informative_but_noisy) {
    // Over many simulated appeals, players' beliefs point the right way on
    // average but are not a copy of the truth.
    std::vector<double> outs, notOuts;
    for (uint64_t seed = 12; seed < 20; ++seed) {
        MatchSession s(MatchRules::T20(), TwoTeams(), seed);
        s.RunAi();
        QuickSim sim(seed + 100);
        for (int guard = 0; guard < 3000 && s.WaitingFor().what == "delivery"; ++guard) {
            Delivery d = sim.Next(s.GetMatch());
            auto c = sim.MakeCase(s.GetMatch(), d);
            if (c && c->call == ScoringCall::None)
                (JudgeReview(*c).finalOut ? outs : notOuts).push_back(c->perceived[s.GetMatch().FieldingSide()]);
            if (!c || !s.ResolveAppeal(*c)) s.ResolveDelivery(d);
        }
    }
    auto mean = [](const std::vector<double>& v) {
        double m = 0;
        for (double x : v) m += x;
        return v.empty() ? 0.0 : m / v.size();
    };
    auto variance = [&](const std::vector<double>& v) {
        double m = mean(v), q = 0;
        for (double x : v) q += (x - m) * (x - m);
        return v.empty() ? 0.0 : q / v.size();
    };
    CHECK(outs.size() >= 10 && notOuts.size() >= 10);
    CHECK(mean(outs) > mean(notOuts) + 0.2);   // informative
    CHECK(variance(outs) > 0.01);              // noisy within the "out" group
    CHECK(variance(notOuts) > 0.01);           // and within the "not out" group
    for (double p : outs) CHECK(p > 0.0 && p < 1.0);
}

// ---------------------------------------------------------------- commentary / rulesets

TEST(drsc_hindi_reason_and_call_commentary) {
    Game g(WithDrs(CallsAllowed()));
    CommentaryEngine hi(Language::Hindi, 2), en(Language::English, 2);
    DecisionRecord d;
    DecisionCase c;
    c.mode = Dismissal::Caught;
    c.onFieldOut = true;
    c.ifOut.dismissal = Dismissal::Caught;
    c.evidence.batContact = Evidence::No;
    CHECK_OK(g.m.Appeal(c));
    CHECK_OK(g.m.ReviewDecision(0, &d));
    auto lines = hi.DescribeDecision(g.m, d);
    bool hindiReason = false;
    for (auto& l : lines) hindiReason |= l.text.find("बल्ला नहीं लगा") != std::string::npos;
    CHECK(hindiReason);
    CHECK_EQ(hi.Reason(ReasonCode::NoEdge), std::string("बल्ला नहीं लगा"));
    CHECK_EQ(en.Reason(ReasonCode::NoEdge), std::string("no edge"));

    CHECK_OK(g.m.Appeal(WideCall(true, Evidence::No)));
    CHECK_OK(g.m.ReviewDecision(1, &d));
    lines = en.DescribeDecision(g.m, d);
    CHECK(lines.size() >= 3);
    CHECK(lines[0].category == "call.signalled");
    CHECK(lines[2].category == "call.changed");
    for (auto& l : lines) CHECK(!l.text.empty() && l.text.find('{') == std::string::npos);
}

TEST(drsc_ruleset_name_with_calls_flag) {
    DrsRules d;
    CHECK(DrsRules::FromName("custom:2:15:calls", &d));
    CHECK(d.reviewWidesAndNoBalls && d.umpiresCallRetainsReview);
    CHECK(DrsRules::FromName("custom:2:15:lose:calls", &d));
    CHECK(d.reviewWidesAndNoBalls && !d.umpiresCallRetainsReview);
    CHECK(!DrsRules::FromName("custom:2:15:bogus", &d));
    CHECK(!DrsRules::Icc().reviewWidesAndNoBalls);
}

TEST(drsc_full_matches_with_all_appeal_types) {
    for (const char* name : {"icc", "custom:2:15:calls", "no-tech"}) {
        DrsRules dr;
        CHECK(DrsRules::FromName(name, &dr));
        for (uint64_t seed = 40; seed < 46; ++seed) {
            MatchSession s(WithDrs(dr), TwoTeams(), seed);
            s.RunAi();
            QuickSim sim(seed * 7);
            int balls = 0, guard = 0;
            while (s.WaitingFor().what == "delivery" && guard++ < 5000) {
                Delivery d = sim.Next(s.GetMatch());
                auto c = sim.MakeCase(s.GetMatch(), d);
                if (c) CHECK_OK(s.ResolveAppeal(*c));
                else CHECK_OK(s.ResolveDelivery(d));
                ++balls;
            }
            const Match& m = s.GetMatch();
            CHECK(m.Phase() == MatchPhase::Complete);
            int recorded = 0;
            for (const auto& inn : m.AllInnings()) {
                recorded += static_cast<int>(inn.Balls().size());
                int bat = 0;
                for (int i = 0; i < 11; ++i) bat += inn.Batter(i).runs;
                CHECK_EQ(bat + inn.GetExtras().Total(), inn.Total());
            }
            CHECK_EQ(recorded, balls);
        }
    }
}

TEST(drsc_scaled_rules_for_shorter_innings) {
    MatchRules odi = MatchRules::ODI().Scaled(20);
    CHECK_EQ(odi.oversPerInnings, 20);
    CHECK_EQ(odi.maxOversPerBowler, 4);
    CHECK_EQ(odi.phases.size(), size_t{3});
    CHECK_EQ(odi.phases[0].lastOver, 4);
    CHECK_EQ(odi.phases[1].lastOver, 16);
    CHECK_EQ(odi.phases[2].lastOver, 20);
    MatchRules t = MatchRules::T20().Scaled(7);
    CHECK_EQ(t.maxOversPerBowler, 2);
    CHECK_EQ(t.phases[0].lastOver, 2);
    MatchRules five = MatchRules::T20().Scaled(5);
    CHECK_EQ(five.maxOversPerBowler, 1);
    CHECK(five.phases[0].lastOver >= 1);
}

// ---------------------------------------------------------------- run-out / stumping reviews

namespace {
DecisionCase RunOutAppeal(bool onFieldOut, Evidence grounded, int runsCompleted = 0) {
    DecisionCase c;
    c.kind = DecisionKind::PlayerAppeal;
    c.mode = Dismissal::RunOut;
    c.onFieldOut = onFieldOut;
    c.ifOut.dismissal = Dismissal::RunOut;
    c.ifOut.runs = runsCompleted;
    c.ifNotOut.runs = runsCompleted;
    c.evidence.batterGrounded = grounded;
    return c;
}
}  // namespace

TEST(drsc_run_out_review_allowed_by_icc_rules) {
    CHECK(DrsRules::Icc().reviewRunOutsAndStumpings);
    CHECK(!DrsRules::NoPlayerReviews().reviewRunOutsAndStumpings);
    {
        Game g;  // given out on the field, batter actually home: overturned
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(RunOutAppeal(true, Evidence::Yes, 1)));
        CHECK_EQ(g.m.ReviewEntitledSide(), 0);
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(d.result.outcome == ReviewOutcome::Overturned && !d.result.finalOut);
        CHECK(d.result.code == ReasonCode::MadeGround);
        CHECK_EQ(g.I().Wickets(), 0);
        CHECK_EQ(g.I().Total(), 1);  // the run completed before the wicket was broken counts
        CHECK_EQ(g.m.ReviewsRemaining(0), 2);
    }
    {
        Game g;  // inconclusive: benefit of the doubt to the batter
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(RunOutAppeal(true, Evidence::Inconclusive)));
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(!d.result.finalOut && d.result.code == ReasonCode::BenefitOfDoubt);
    }
    {
        Game g;  // given not out, fielders review, batter short: out
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(RunOutAppeal(false, Evidence::No)));
        CHECK_EQ(g.m.ReviewEntitledSide(), 1);
        CHECK_OK(g.m.ReviewDecision(1, &d));
        CHECK(d.result.finalOut && d.result.finalMode == Dismissal::RunOut);
        CHECK_EQ(g.I().Wickets(), 1);
        CHECK_EQ(g.I().Bowler(10).wickets, 0);
    }
    {
        Game g;  // given out, correctly: review lost
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(RunOutAppeal(true, Evidence::No)));
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(d.result.outcome == ReviewOutcome::Upheld && d.result.reviewLost);
        CHECK_EQ(g.m.ReviewsRemaining(0), 1);
    }
}

TEST(drsc_stumping_review_off_a_wide) {
    Game g;
    DecisionCase c;
    c.kind = DecisionKind::PlayerAppeal;
    c.mode = Dismissal::Stumped;
    c.onFieldOut = true;
    c.ifOut.extra = ExtraType::Wide;
    c.ifOut.dismissal = Dismissal::Stumped;
    c.ifNotOut.extra = ExtraType::Wide;
    c.ifNotOut.runs = 1;  // they ran one after the appeal: dead ball once given out
    c.evidence.batterGrounded = Evidence::Yes;
    DecisionRecord d;
    CHECK_OK(g.m.Appeal(c));
    CHECK_OK(g.m.ReviewDecision(0, &d));
    CHECK(!d.result.finalOut);
    CHECK_EQ(g.I().GetExtras().wides, 1);  // wide penalty only
    CHECK_EQ(g.I().Total(), 1);
}

TEST(drsc_run_out_reviews_switched_off_by_competition) {
    DrsRules d;
    CHECK(DrsRules::FromName("custom:2:15:norunouts", &d));
    CHECK(!d.reviewRunOutsAndStumpings);
    CHECK(DrsRules::FromName("custom:2:15", &d));
    CHECK(d.reviewRunOutsAndStumpings);
    Game g(WithDrs(DrsRules::Custom(2, 15)));
    MatchRules off = MatchRules::T20();
    off.drs.reviewRunOutsAndStumpings = false;
    Game h(off);
    CHECK_ERR(h.m.Appeal(RunOutAppeal(true, Evidence::Yes)));
    CHECK(!h.m.HasPendingDecision());
    CHECK(IsPlayerReviewable(Dismissal::RunOut, true));
    CHECK(!IsPlayerReviewable(Dismissal::RunOut, false));
    CHECK(!IsPlayerReviewable(Dismissal::Bowled, true));
}

TEST(drsc_sim_produces_reviewable_run_outs) {
    int playerRunOutAppeals = 0;
    for (uint64_t seed = 60; seed < 80 && playerRunOutAppeals == 0; ++seed) {
        MatchSession s(MatchRules::T20(), TwoTeams(), seed);
        s.RunAi();
        QuickSim sim(seed);
        while (s.WaitingFor().what == "delivery") {
            Delivery d = sim.Next(s.GetMatch());
            auto c = sim.MakeCase(s.GetMatch(), d);
            if (!c || !s.ResolveAppeal(*c)) CHECK_OK(s.ResolveDelivery(d));
        }
        for (const auto& dec : s.GetMatch().Decisions())
            playerRunOutAppeals += dec.kind == DecisionKind::PlayerAppeal &&
                                   (dec.appealedFor == Dismissal::RunOut || dec.appealedFor == Dismissal::Stumped);
    }
    CHECK(playerRunOutAppeals > 0);
}
