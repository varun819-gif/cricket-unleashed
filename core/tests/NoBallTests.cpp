// No-ball scoring (Law 21.12, MCC Laws 2017):
//  * the one-run penalty is always a no-ball extra, charged to the bowler;
//  * runs off the bat are credited to the striker and charged to the bowler;
//  * runs not off the bat are byes or leg-byes, NOT charged to the bowler;
//  * the striker is credited with a ball faced; the ball is not legal.
// Plus the same rules when DRS turns a delivery into a no-ball.
#include "Test.h"
#include "cricket/Commentary.h"
#include "cricket/Drs.h"
#include "cricket/Match.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

namespace {

struct Fixture {
    MatchRules rules = MatchRules::T20();
    Innings inn{rules, 0, 0, std::nullopt};
    Fixture() {
        inn.SetOpeners(0, 1);
        inn.SetBowler(10);
    }
};

Delivery NoBall(int runs, ExtraType as = ExtraType::None, bool boundary = false) {
    Delivery d;
    d.extra = ExtraType::NoBall;
    d.runs = runs;
    d.noBallRunsAs = as;
    d.boundary = boundary;
    return d;
}

struct Book {
    int total, noBalls, byes, legByes, batRuns, batBalls, bowlerRuns, legal;
};

void Expect(const Innings& inn, Book b) {
    CHECK_EQ(inn.Total(), b.total);
    CHECK_EQ(inn.GetExtras().noBalls, b.noBalls);
    CHECK_EQ(inn.GetExtras().byes, b.byes);
    CHECK_EQ(inn.GetExtras().legByes, b.legByes);
    CHECK_EQ(inn.Batter(0).runs, b.batRuns);
    CHECK_EQ(inn.Batter(0).balls, b.batBalls);
    CHECK_EQ(inn.Bowler(10).runs, b.bowlerRuns);
    CHECK_EQ(inn.LegalBalls(), b.legal);
}

std::array<Team, 2> TwoTeams() {
    auto all = FictionalTeams();
    return {all[0], all[1]};
}

}  // namespace

TEST(noball_dot_ball_penalty_only) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(0)));
    Expect(f.inn, {1, 1, 0, 0, 0, 1, 1, 0});
    CHECK_EQ(f.inn.Bowler(10).noBalls, 1);
    CHECK(f.inn.NextBallIsFreeHit());
}

TEST(noball_runs_off_the_bat) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(2)));
    // 1 penalty + 2 to the striker; bowler charged 3; batters crossed twice.
    Expect(f.inn, {3, 1, 0, 0, 2, 1, 3, 0});
    CHECK_EQ(f.inn.Striker(), 0);
}

TEST(noball_four_off_the_bat) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(4, ExtraType::None, true)));
    Expect(f.inn, {5, 1, 0, 0, 4, 1, 5, 0});
    CHECK_EQ(f.inn.Batter(0).fours, 1);
}

TEST(noball_six_off_the_bat) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(6, ExtraType::None, true)));
    Expect(f.inn, {7, 1, 0, 0, 6, 1, 7, 0});
    CHECK_EQ(f.inn.Batter(0).sixes, 1);
}

TEST(noball_byes_are_byes_not_bowler_runs) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(1, ExtraType::Bye)));
    // 1 no-ball + 1 bye; striker faced it but scored nothing; bowler charged 1.
    Expect(f.inn, {2, 1, 1, 0, 0, 1, 1, 0});
    CHECK_EQ(f.inn.Striker(), 1);  // one run completed: they crossed
}

TEST(noball_four_byes) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(4, ExtraType::Bye, true)));
    Expect(f.inn, {5, 1, 4, 0, 0, 1, 1, 0});
    CHECK_EQ(f.inn.Batter(0).fours, 0);  // not a four to the batter
}

TEST(noball_leg_byes) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(2, ExtraType::LegBye)));
    Expect(f.inn, {3, 1, 0, 2, 0, 1, 1, 0});
}

TEST(noball_four_leg_byes) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(4, ExtraType::LegBye, true)));
    Expect(f.inn, {5, 1, 0, 4, 0, 1, 1, 0});
}

TEST(noball_invalid_combinations_rejected) {
    Fixture f;
    CHECK_ERR(f.inn.Bowl(NoBall(6, ExtraType::Bye, true)));      // six byes impossible
    CHECK_ERR(f.inn.Bowl(NoBall(6, ExtraType::LegBye, true)));   // six leg-byes impossible
    CHECK_ERR(f.inn.Bowl(NoBall(1, ExtraType::Wide)));           // runs can't be "wides" on a no-ball
    CHECK_ERR(f.inn.Bowl(NoBall(1, ExtraType::NoBall)));
    Delivery legal;
    legal.runs = 1;
    legal.noBallRunsAs = ExtraType::Bye;                          // only meaningful on a no-ball
    CHECK_ERR(f.inn.Bowl(legal));
    Delivery wide;
    wide.extra = ExtraType::Wide;
    wide.noBallRunsAs = ExtraType::LegBye;
    CHECK_ERR(f.inn.Bowl(wide));
    CHECK_EQ(f.inn.Total(), 0);
}

TEST(noball_byes_with_run_out) {
    Fixture f;
    Delivery d = NoBall(1, ExtraType::Bye);
    d.dismissal = Dismissal::RunOut;
    d.strikerOut = false;
    CHECK_OK(f.inn.Bowl(d));
    CHECK_EQ(f.inn.Wickets(), 1);
    CHECK_EQ(f.inn.GetExtras().byes, 1);
    CHECK_EQ(f.inn.Total(), 2);
    CHECK_EQ(f.inn.Bowler(10).wickets, 0);
}

TEST(noball_byes_do_not_spoil_free_hit_or_count_as_legal) {
    Fixture f;
    CHECK_OK(f.inn.Bowl(NoBall(2, ExtraType::LegBye)));
    CHECK(f.inn.NextBallIsFreeHit());
    CHECK_EQ(f.inn.LegalBalls(), 0);
}

TEST(noball_commentary_mentions_byes_not_batter) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    m.SetOpeners(0, 1);
    m.SetBowler(10);
    CommentaryEngine com(Language::English, 1);
    DeliveryRecord r;
    CHECK_OK(m.Bowl(NoBall(4, ExtraType::Bye, true), &r));
    auto lines = com.Describe(m, r);
    bool sawNoBall = false, sawBye = false, sawFour = false;
    for (auto& l : lines) {
        sawNoBall |= l.category == "noball";
        sawBye |= l.category == "bye";
        sawFour |= l.category == "four";
    }
    CHECK(sawNoBall && sawBye && !sawFour);
}

// ---------------------------------------------------------------- DRS-found no-balls

namespace {
struct Game {
    Match m{MatchRules::T20(), TwoTeams(), 0};
    Game() {
        m.SetOpeners(0, 1);
        m.SetBowler(10);
    }
    const Innings& I() const { return m.Current(); }
};
DecisionCase NotOutCaught(Delivery ifNotOut) {
    DecisionCase c;
    c.mode = Dismissal::Caught;
    c.onFieldOut = false;
    c.ifOut.dismissal = Dismissal::Caught;
    c.ifNotOut = ifNotOut;
    c.evidence.batContact = Evidence::Yes;
    c.evidence.frontFootNoBall = true;
    return c;
}
}  // namespace

TEST(drs_noball_keeps_bat_runs) {
    Game g;
    Delivery two;
    two.runs = 2;
    CHECK_OK(g.m.Appeal(NotOutCaught(two)));
    CHECK_OK(g.m.ReviewDecision(1));  // fielders review; TV finds the no-ball
    CHECK_EQ(g.I().Wickets(), 0);
    CHECK_EQ(g.I().Total(), 3);       // 1 no-ball + 2 off the bat
    CHECK_EQ(g.I().Batter(0).runs, 2);
    CHECK_EQ(g.I().Bowler(10).runs, 3);
}

TEST(drs_noball_keeps_byes) {
    Game g;
    Delivery byes;
    byes.extra = ExtraType::Bye;
    byes.runs = 3;
    DecisionCase c;
    c.kind = DecisionKind::UmpireReferral;
    c.mode = Dismissal::RunOut;
    c.ifOut.dismissal = Dismissal::RunOut;
    c.ifNotOut = byes;
    c.evidence.batterGrounded = Evidence::Yes;
    c.evidence.frontFootNoBall = true;
    CHECK_OK(g.m.Appeal(c));
    CHECK_EQ(g.I().GetExtras().noBalls, 1);
    CHECK_EQ(g.I().GetExtras().byes, 3);
    CHECK_EQ(g.I().Total(), 4);
    CHECK_EQ(g.I().Bowler(10).runs, 1);
}

TEST(drs_noball_keeps_leg_byes) {
    Game g;
    Delivery lb;
    lb.extra = ExtraType::LegBye;
    lb.runs = 2;
    DecisionCase c;
    c.mode = Dismissal::Lbw;
    c.onFieldOut = false;  // not out, play went on, they ran 2 leg-byes
    c.ifOut.dismissal = Dismissal::Lbw;
    c.ifNotOut = lb;
    LbwEvidence e;
    c.evidence.lbw = e;
    c.evidence.frontFootNoBall = true;
    CHECK_OK(g.m.Appeal(c));
    CHECK_OK(g.m.AcceptDecision());
    CHECK_EQ(g.I().GetExtras().noBalls, 1);
    CHECK_EQ(g.I().GetExtras().legByes, 2);
    CHECK_EQ(g.I().Total(), 3);
    CHECK_EQ(g.I().Batter(0).balls, 1);
}

TEST(drs_noball_overrides_wide_runs_become_byes) {
    Game g;
    Delivery wide;
    wide.extra = ExtraType::Wide;
    wide.runs = 1;  // they ran one off the wide
    DecisionCase c;
    c.kind = DecisionKind::UmpireReferral;
    c.mode = Dismissal::Stumped;
    c.ifOut = wide;
    c.ifOut.runs = 0;
    c.ifOut.dismissal = Dismissal::Stumped;
    c.ifNotOut = wide;
    c.evidence.batterGrounded = Evidence::No;
    c.evidence.frontFootNoBall = true;
    DecisionRecord d;
    CHECK_OK(g.m.Appeal(c, &d));
    CHECK(!d.result.finalOut);        // no stumping off a no-ball
    CHECK_EQ(g.I().GetExtras().wides, 0);
    CHECK_EQ(g.I().GetExtras().noBalls, 1);
    CHECK_EQ(g.I().GetExtras().byes, 1);
    CHECK_EQ(g.I().Total(), 2);
}

TEST(drs_given_out_then_noball_is_dead_ball_penalty_only) {
    // ICC DRS playing conditions: when an OUT decision is reversed, the ball
    // is treated as dead at the moment of the original decision, so only the
    // no-ball penalty counts. (Contrast with the not-out cases above.)
    Game g;
    Delivery two;
    two.runs = 2;
    DecisionCase c = NotOutCaught(two);
    c.onFieldOut = true;
    CHECK_OK(g.m.Appeal(c));
    CHECK_OK(g.m.AcceptDecision());
    CHECK_EQ(g.I().Total(), 1);
    CHECK_EQ(g.I().Batter(0).runs, 0);
}
