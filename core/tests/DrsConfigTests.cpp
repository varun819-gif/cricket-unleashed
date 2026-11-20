// Configurable DRS rulesets and the server-side review timer.
#include "Test.h"
#include "cricket/AutoCaptain.h"
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

MatchRules WithDrs(DrsRules d, MatchRules r = MatchRules::T20()) {
    r.drs = d;
    return r;
}

DecisionCase LbwCase(bool onFieldOut, WicketsZone w) {
    DecisionCase c;
    c.mode = Dismissal::Lbw;
    c.onFieldOut = onFieldOut;
    c.ifOut.dismissal = Dismissal::Lbw;
    LbwEvidence e;
    e.wickets = w;
    c.evidence.lbw = e;
    return c;
}

DecisionCase CaughtOut(Evidence edge) {
    DecisionCase c;
    c.mode = Dismissal::Caught;
    c.onFieldOut = true;
    c.ifOut.dismissal = Dismissal::Caught;
    c.evidence.batContact = edge;
    return c;
}

DecisionCase RunOutReferral(Evidence grounded, bool onFieldOut) {
    DecisionCase c;
    c.kind = DecisionKind::UmpireReferral;
    c.mode = Dismissal::RunOut;
    c.onFieldOut = onFieldOut;
    c.ifOut.dismissal = Dismissal::RunOut;
    c.evidence.batterGrounded = grounded;
    return c;
}

struct Game {
    Match m;
    explicit Game(MatchRules r) : m(r, TwoTeams(), 0) {
        m.SetOpeners(0, 1);
        m.SetBowler(10);
    }
};

}  // namespace

// ---------------------------------------------------------------- rulesets

TEST(drs_presets_and_names) {
    DrsRules icc = DrsRules::Icc();
    CHECK(icc.playerReviews && icc.thirdUmpire && icc.umpiresCallRetainsReview);
    CHECK_EQ(icc.reviewsPerInnings, 2);
    CHECK_EQ(icc.reviewTimerSeconds, 15);
    CHECK(!DrsRules::NoPlayerReviews().playerReviews && DrsRules::NoPlayerReviews().thirdUmpire);
    CHECK(!DrsRules::NoTechnology().thirdUmpire);
    // Every built-in format uses the international standard by default.
    for (const MatchRules& r : {MatchRules::T20(), MatchRules::ODI(), MatchRules::Test(), MatchRules::Custom(5)})
        CHECK_EQ(r.drs.reviewsPerInnings, 2);

    DrsRules d;
    CHECK(DrsRules::FromName("custom:3:20", &d));
    CHECK_EQ(d.reviewsPerInnings, 3);
    CHECK_EQ(d.reviewTimerSeconds, 20);
    CHECK(d.umpiresCallRetainsReview);
    CHECK(DrsRules::FromName("custom:1:10:lose", &d));
    CHECK(!d.umpiresCallRetainsReview);
    CHECK(DrsRules::FromName("custom:0:0", &d));
    CHECK(!d.playerReviews);
    CHECK(DrsRules::FromName("no-tech", &d) && !d.thirdUmpire);
    CHECK(!DrsRules::FromName("custom:-1:15", &d));
    CHECK(!DrsRules::FromName("custom:2", &d));
    CHECK(!DrsRules::FromName("hawkeye-max", &d));
}

TEST(drs_review_count_follows_ruleset) {
    for (int allowed : {1, 3}) {
        Game g(WithDrs(DrsRules::Custom(allowed, 15)));
        CHECK_EQ(g.m.ReviewsRemaining(0), allowed);
        for (int i = 0; i < allowed; ++i) {
            CHECK_OK(g.m.Appeal(LbwCase(true, WicketsZone::Hitting)));
            CHECK_OK(g.m.ReviewDecision(0));  // unsuccessful
            CHECK_OK(g.m.SetNextBatter(2 + i));
        }
        CHECK_EQ(g.m.ReviewsRemaining(0), 0);
        CHECK_OK(g.m.Appeal(LbwCase(true, WicketsZone::Hitting)));
        CHECK_ERR(g.m.ReviewDecision(0));
        CHECK_OK(g.m.AcceptDecision());
    }
}

TEST(drs_umpires_call_can_cost_a_review_if_configured) {
    {
        Game g(WithDrs(DrsRules::Custom(2, 15, true)));
        CHECK_OK(g.m.Appeal(LbwCase(true, WicketsZone::UmpiresCall)));
        CHECK_OK(g.m.ReviewDecision(0));
        CHECK_EQ(g.m.ReviewsRemaining(0), 2);
    }
    {
        Game g(WithDrs(DrsRules::Custom(2, 15, false)));
        DecisionRecord d;
        CHECK_OK(g.m.Appeal(LbwCase(true, WicketsZone::UmpiresCall)));
        CHECK_OK(g.m.ReviewDecision(0, &d));
        CHECK(d.result.outcome == ReviewOutcome::UmpiresCall && d.result.reviewLost);
        CHECK_EQ(g.m.ReviewsRemaining(0), 1);
    }
}

TEST(drs_no_player_reviews_on_field_call_is_final) {
    Game g(WithDrs(DrsRules::NoPlayerReviews()));
    CHECK_EQ(g.m.ReviewsRemaining(0), 0);
    DecisionRecord d;
    CHECK_OK(g.m.Appeal(CaughtOut(Evidence::No), &d));  // wrong decision...
    CHECK(!g.m.HasPendingDecision());                     // ...but final at once
    CHECK(d.result.outcome == ReviewOutcome::NotReviewed && d.result.finalOut);
    CHECK_EQ(g.m.Current().Wickets(), 1);
    CHECK_ERR(g.m.ReviewDecision(0));
    // The TV umpire still handles run outs.
    CHECK_OK(g.m.SetNextBatter(2));
    CHECK_OK(g.m.Appeal(RunOutReferral(Evidence::Yes, true), &d));
    CHECK(d.result.outcome == ReviewOutcome::ThirdUmpire && !d.result.finalOut);
}

TEST(drs_no_technology_on_field_umpire_decides_everything) {
    Game g(WithDrs(DrsRules::NoTechnology()));
    DecisionRecord d;
    // Batter actually short of the crease, umpire says not out: stands.
    CHECK_OK(g.m.Appeal(RunOutReferral(Evidence::No, false), &d));
    CHECK(d.result.outcome == ReviewOutcome::NotReviewed && !d.result.finalOut);
    CHECK_EQ(g.m.Current().Wickets(), 0);
    // A front-foot no-ball the umpire missed is not picked up by a TV umpire.
    DecisionCase c = CaughtOut(Evidence::Yes);
    c.evidence.frontFootNoBall = true;
    CHECK_OK(g.m.Appeal(c, &d));
    CHECK(d.result.finalOut && !d.result.noBall);
    CHECK_EQ(g.m.Current().Wickets(), 1);
}

TEST(drs_negative_settings_rejected) {
    MatchRules r = MatchRules::T20();
    r.drs.reviewsPerInnings = -1;
    bool threw = false;
    try {
        Match m(r, TwoTeams(), 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(drs_full_matches_under_every_ruleset) {
    for (const char* name : {"icc", "no-reviews", "no-tech", "custom:1:10:lose", "custom:3:20"}) {
        DrsRules d;
        CHECK(DrsRules::FromName(name, &d));
        MatchSession s(WithDrs(d), TwoTeams(), 31);
        s.RunAi();
        QuickSim sim(32);
        int guard = 0, balls = 0;
        while (s.WaitingFor().what == "delivery" && guard++ < 5000) {
            Delivery del = sim.Next(s.GetMatch());
            auto c = sim.MakeCase(s.GetMatch(), del);
            if (c) CHECK_OK(s.ResolveAppeal(*c));
            else CHECK_OK(s.ResolveDelivery(del));
            ++balls;
        }
        const Match& m = s.GetMatch();
        CHECK(m.Phase() == MatchPhase::Complete);
        for (const auto& dec : m.Decisions()) {
            if (!d.playerReviews) CHECK(!dec.reviewed);
            if (!d.thirdUmpire) CHECK(dec.result.outcome != ReviewOutcome::ThirdUmpire);
        }
        // Every delivery reached the scorebook (none lost to a refused appeal).
        int recorded = 0;
        for (const auto& inn : m.AllInnings()) recorded += static_cast<int>(inn.Balls().size());
        CHECK_EQ(recorded, balls);
    }
}

// ---------------------------------------------------------------- 15-second timer

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

struct Online {
    MatchSession s;
    uint64_t seq[2] = {0, 0};
    std::string name[2] = {"p-india", "p-pakistan"};
    int batting = -1;
    explicit Online(DrsRules d = DrsRules::Icc()) : s(WithDrs(d), TwoTeams(), 77) {
        s.Join(name[0], 0);
        s.Join(name[1], 1);
        Send(1, CommandType::TossCall, 0);
        Send(s.GetToss().Winner(), CommandType::TossDecision, 0);
        batting = s.GetMatch().BattingSide();
        Send(batting, CommandType::SetOpeners, 0, 1);
        Send(1 - batting, CommandType::SetBowler, 10);
    }
    Status Send(int side, CommandType t, int a = 0, int b = 0) { return s.Submit(Cmd(name[side], ++seq[side], t, a, b)); }
    Status SendAt(int64_t ms, int side, CommandType t, int a = 0) {
        return s.SubmitAt(Cmd(name[side], ++seq[side], t, a), ms);
    }
    bool Logged(const std::string& kind) const {
        for (const auto& e : s.EventsSince(0))
            if (e.kind == kind) return true;
        return false;
    }
};
}  // namespace

TEST(drs_timer_review_inside_window_accepted) {
    Online o;
    CHECK_OK(o.s.Tick(100000));
    CHECK_OK(o.s.ResolveAppeal(CaughtOut(Evidence::No)));
    Pending p = o.s.WaitingFor();
    CHECK(p.what == "review");
    CHECK_EQ(p.deadlineMs, 115000);
    CHECK_EQ(o.s.ReviewMsLeft(), 15000);
    CHECK_OK(o.s.Tick(110000));
    CHECK_EQ(o.s.ReviewMsLeft(), 5000);  // countdown for the TV/phone UI
    CHECK_OK(o.SendAt(114999, o.batting, CommandType::Review, 1));  // last millisecond
    CHECK_EQ(o.s.GetMatch().Current().Wickets(), 0);                // overturned
    CHECK(!o.Logged("review-timeout"));
    CHECK_EQ(o.s.ReviewMsLeft(), -1);
}

TEST(drs_timer_expiry_on_field_out_stands) {
    Online o;
    CHECK_OK(o.s.Tick(5000));
    CHECK_OK(o.s.ResolveAppeal(CaughtOut(Evidence::No)));  // wrong OUT, reviewable
    CHECK_OK(o.s.Tick(19999));
    CHECK(o.s.WaitingFor().what == "review");
    CHECK_OK(o.s.Tick(20000));  // exactly 15 s: window closed
    CHECK(o.Logged("review-timeout"));
    CHECK(o.s.WaitingFor().what == "batter");
    CHECK_EQ(o.s.GetMatch().Current().Wickets(), 1);  // on-field OUT stands
    CHECK_EQ(o.s.GetMatch().ReviewsRemaining(o.batting), 2);  // no review used
    CHECK_EQ(o.s.GetMatch().Decisions().size(), size_t{1});
    if (!o.s.GetMatch().Decisions().empty()) {
        const auto& dec = o.s.GetMatch().Decisions().back();
        CHECK(!dec.reviewed && dec.result.outcome == ReviewOutcome::NotReviewed);
    }
    // A review request arriving now is refused.
    CHECK_ERR(o.SendAt(20500, o.batting, CommandType::Review, 1));
}

TEST(drs_timer_expiry_on_field_not_out_stands_and_runs_count) {
    Online o;
    DecisionCase c = LbwCase(false, WicketsZone::Hitting);
    c.ifNotOut.extra = ExtraType::LegBye;
    c.ifNotOut.runs = 1;
    CHECK_OK(o.s.ResolveAppeal(c));
    CHECK_OK(o.s.Tick(15000));
    CHECK_EQ(o.s.GetMatch().Current().Wickets(), 0);
    CHECK_EQ(o.s.GetMatch().Current().Total(), 1);
    CHECK(o.s.WaitingFor().what == "delivery");
}

TEST(drs_timer_late_review_without_tick_is_refused) {
    // Even if the game loop has not ticked, a late message is judged by its time.
    Online o;
    CHECK_OK(o.s.ResolveAppeal(CaughtOut(Evidence::No)));  // at t = 0
    Status s = o.SendAt(15001, o.batting, CommandType::Review, 1);
    CHECK_ERR(s);
    CHECK_EQ(o.s.GetMatch().Current().Wickets(), 1);
    CHECK(o.Logged("review-timeout"));
}

TEST(drs_timer_expiry_still_applies_noball_check) {
    Online o;
    DecisionCase c = CaughtOut(Evidence::Yes);
    c.evidence.frontFootNoBall = true;
    CHECK_OK(o.s.ResolveAppeal(c));
    CHECK_OK(o.s.Tick(16000));
    CHECK_EQ(o.s.GetMatch().Current().Wickets(), 0);
    CHECK_EQ(o.s.GetMatch().Current().GetExtras().noBalls, 1);
    CHECK(o.s.GetMatch().Current().NextBallIsFreeHit());
}

TEST(drs_timer_custom_length_and_disabled) {
    {
        Online o(DrsRules::Custom(2, 10));
        CHECK_OK(o.s.ResolveAppeal(CaughtOut(Evidence::No)));
        CHECK_EQ(o.s.WaitingFor().deadlineMs, 10000);
        CHECK_OK(o.s.Tick(10000));
        CHECK(o.Logged("review-timeout"));
    }
    {
        Online o(DrsRules::Custom(2, 0));  // no timer
        CHECK_OK(o.s.ResolveAppeal(CaughtOut(Evidence::No)));
        CHECK_EQ(o.s.WaitingFor().deadlineMs, -1);
        CHECK_EQ(o.s.ReviewMsLeft(), -1);
        CHECK_OK(o.s.Tick(10'000'000));
        CHECK(o.s.WaitingFor().what == "review");  // waits for the player
        CHECK_OK(o.Send(o.batting, CommandType::Review, 1));
    }
}

TEST(drs_timer_clock_cannot_go_backwards) {
    Online o;
    CHECK_OK(o.s.Tick(50000));
    CHECK_OK(o.s.ResolveAppeal(CaughtOut(Evidence::No)));
    CHECK_ERR(o.s.Tick(1000));
    CHECK_EQ(o.s.NowMs(), 50000);
    CHECK_EQ(o.s.ReviewMsLeft(), 15000);
    CHECK_ERR(o.SendAt(1000, o.batting, CommandType::Review, 1));  // stale clock refused
    CHECK(o.s.WaitingFor().what == "review");
}

TEST(drs_timer_restarts_for_each_appeal) {
    Online o;
    CHECK_OK(o.s.Tick(1000));
    CHECK_OK(o.s.ResolveAppeal(LbwCase(false, WicketsZone::Missing)));
    CHECK_OK(o.SendAt(3000, 1 - o.batting, CommandType::Review, 0));  // fielders accept
    CHECK_OK(o.s.Tick(40000));
    CHECK_OK(o.s.ResolveAppeal(LbwCase(false, WicketsZone::Missing)));
    CHECK_EQ(o.s.WaitingFor().deadlineMs, 55000);
    CHECK_OK(o.s.Tick(54999));
    CHECK(o.s.WaitingFor().what == "review");
}

TEST(drs_timer_not_used_when_ai_or_no_reviews) {
    {
        MatchSession s(MatchRules::T20(), TwoTeams(), 4);  // AI decides instantly
        s.RunAi();
        CHECK_OK(s.ResolveAppeal(CaughtOut(Evidence::No)));
        CHECK_EQ(s.ReviewMsLeft(), -1);
        CHECK(s.WaitingFor().what != "review");
    }
    {
        Online o(DrsRules::NoPlayerReviews());
        CHECK_OK(o.s.ResolveAppeal(CaughtOut(Evidence::No)));
        CHECK(o.s.WaitingFor().what == "batter");
        CHECK_EQ(o.s.ReviewMsLeft(), -1);
    }
}
