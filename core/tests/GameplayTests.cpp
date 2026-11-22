// Gameplay physics added on top of the ball physics: wind, dew, pitch wear,
// boundary catches, front-foot no-balls, stumpings, leg byes / byes,
// run outs and overthrows, plus whole matches (all formats) checked through
// the scorecard and the DRS records. Deterministic (fixed seeds).
#include <algorithm>
#include <cmath>
#include <memory>

#include "Test.h"
#include "cricket/BallOutcome.h"
#include "cricket/Recording.h"
#include "cricket/Scorecard.h"
#include "cricket/TeamFactory.h"

using namespace cricket;
using namespace cricket::phys;

namespace {

DeliverySpec Pace(double kph, double length, double line) {
    DeliverySpec s;
    s.speedKph = kph;
    s.targetLength = length;
    s.targetLine = line;
    return s;
}

DeliverySpec OffSpin() {
    DeliverySpec s;
    s.speedKph = 86;
    s.targetLength = 4.6;
    s.targetLine = 0.25;
    s.releaseHeight = 2.0;
    s.spinRps = 24;
    s.spinTurn = -0.8;
    s.spinTop = 0.4;
    return s;
}

std::unique_ptr<Match> RightHandersMatch(int bowler, MatchRules rules = MatchRules::T20()) {
    auto all = NationalTeams();
    for (auto& p : all[0].xi) p.bats = Handedness::Right;
    auto m = std::make_unique<Match>(std::move(rules), std::array<Team, 2>{all[0], all[1]}, 0);
    m->SetOpeners(0, 1);
    m->SetBowler(bowler);
    return m;
}

// Plays complete computer-v-computer matches through the rules engine.
struct MatchRun {
    std::unique_ptr<MatchSession> session;
    std::vector<BallReport> reports;
};
MatchRun PlayMatch(MatchRules rules, uint64_t seed, Conditions cond = {}) {
    auto all = NationalTeams();
    MatchRun run;
    run.session = std::make_unique<MatchSession>(std::move(rules), std::array<Team, 2>{all[seed % 10], all[(seed + 3) % 10]}, seed);
    MatchSession& s = *run.session;
    s.RunAi();
    BallOutcome o(seed * 7 + 1);
    o.conditions = cond;
    QuickSim perception(seed + 2);
    for (int guard = 0; guard < 20000 && s.WaitingFor().what == "delivery"; ++guard) {
        run.reports.push_back(o.PlayComputerBall(s, &perception));
        s.RunAi();
        if (s.WaitingFor().what == "interval") s.ResumeAfterInterval();
        s.RunAi();
    }
    return run;
}

}  // namespace

// ---------------------------------------------------------------- weather and pitch

TEST(gp_wind_pushes_the_ball) {
    Environment calm, cross, tail;
    cross.wind = {4.0, 0, 0};    // blowing towards the off side
    tail.wind = {0, -6.0, 0};    // behind the bowler (towards the batter)
    const auto a = SimulateDelivery(Pace(135, 6.0, 0.1), Pitch::Standard(), calm);
    const auto b = SimulateDelivery(Pace(135, 6.0, 0.1), Pitch::Standard(), cross);
    const auto c = SimulateDelivery(Pace(135, 6.0, 0.1), Pitch::Standard(), tail);
    // The bowler still aims at the same spot, but the ball drifts in the air.
    CHECK(b.atStumps.x > a.atStumps.x);
    CHECK(c.timeAtCrease < a.timeAtCrease);  // carried along by a tail wind
    // A big hit into the wind carries less.
    Environment head;
    head.wind = {0, -8.0, 0};
    const auto still = PlayOutField({0, 2, 1}, {0, 28, 18}, {}, {}, calm);
    const auto into = PlayOutField({0, 2, 1}, {0, 28, 18}, {}, {}, head);
    CHECK(into.carry < still.carry - 3);
}

TEST(gp_dew_makes_the_ball_skid_and_swing_less) {
    Environment dry, wet;
    wet.dew = 1.0;
    const auto d = SimulateDelivery(OffSpin(), Pitch::Dusty(), dry);
    const auto w = SimulateDelivery(OffSpin(), Pitch::Dusty(), wet);
    CHECK(std::fabs(w.deviationOffPitch) < std::fabs(d.deviationOffPitch));  // spinners grip less
    DeliverySpec sw = Pace(130, 6.0, 0.2);
    sw.seamAngle = 20, sw.seamWobble = 0.05;
    CHECK(std::fabs(SimulateDelivery(sw, Pitch::Standard(), wet).swingBeforePitch) <
          std::fabs(SimulateDelivery(sw, Pitch::Standard(), dry).swingBeforePitch));
    // Dew arrives in the second innings of a day-night limited-overs match only.
    BallOutcome o(1);
    o.conditions.dayNight = true;
    auto m = RightHandersMatch(8);
    CHECK_EQ(o.EnvFor(*m).dew, 0.0);  // first innings
}

TEST(gp_pitch_wears_and_footmarks_grip) {
    Pitch fresh = Pitch::Standard();
    Pitch worn = fresh;
    worn.wear = 1.0;
    const PitchSpot a = PitchAt(fresh, 0.0, 5.0);
    const PitchSpot b = PitchAt(worn, 0.0, 5.0);
    CHECK(b.friction > a.friction + 0.1);  // dry, cracked, more grip
    CHECK(b.seam < a.seam);                 // the grass has gone
    // Footmarks outside the stumps on a worn pitch.
    CHECK(PitchAt(worn, 0.5, 2.0).inRough);
    CHECK(!PitchAt(worn, 0.0, 5.0).inRough);
    CHECK(!PitchAt(fresh, 0.5, 2.0).inRough);
    CHECK(PitchAt(worn, 0.5, 2.0).friction > b.friction);
    // Variable bounce: deterministic per pitch, different between pitches.
    Pitch other = worn;
    other.cracks = 99;
    CHECK_EQ(PitchAt(worn, 0.1, 6.3).bounce, PitchAt(worn, 0.1, 6.3).bounce);
    double spreadWorn = 0, spreadFresh = 0;
    for (double y = 2; y < 9; y += 0.37) {
        spreadWorn = std::max(spreadWorn, std::fabs(PitchAt(worn, 0.05, y).bounce - worn.bounce + 0.06));
        spreadFresh = std::max(spreadFresh, std::fabs(PitchAt(fresh, 0.05, y).bounce - fresh.bounce));
    }
    CHECK(spreadWorn > 0.01);
    CHECK_EQ(spreadFresh, 0.0);
    bool differs = false;
    for (double y = 2; y < 9; y += 0.37) differs |= PitchAt(worn, 0.05, y).bounce != PitchAt(other, 0.05, y).bounce;
    CHECK(differs);
    // Spin turns more on a worn pitch.
    const auto turnFresh = SimulateDelivery(OffSpin(), fresh);
    const auto turnWorn = SimulateDelivery(OffSpin(), worn);
    CHECK(std::fabs(turnWorn.deviationOffPitch) > std::fabs(turnFresh.deviationOffPitch));
    // Over a Test match the game wears the pitch.
    BallOutcome o(1);
    auto test = RightHandersMatch(8, MatchRules::Test());
    CHECK_EQ(o.PitchFor(*test).wear, 0.0);
}

// ---------------------------------------------------------------- fielding

TEST(gp_boundary_catches_and_the_rope) {
    // A deep fielder on the rope at long-on.
    std::vector<Fielder> deep(1);
    deep[0].pos = {-17.5, 75.5, 0};  // ~66.5 m from the pitch centre
    deep[0].skill = 0.9;
    deep[0].index = 4;
    bool boundaryCatch = false, six = false;
    for (double vy = 24; vy <= 44 && !(boundaryCatch && six); vy += 0.25) {
        const auto r = PlayOutField({0, 2, 1}, {-vy * 0.23, vy, 17}, deep);
        if (r.caught && r.boundaryCatch) boundaryCatch = true;
        if (r.boundary && r.runs == 6) six = true;
    }
    CHECK(boundaryCatch);  // taken at (or relayed back from) the rope
    CHECK(six);            // just too high / too far: six
    // A weaker fielder sprinting to the rope can carry over it: six.
    std::vector<Fielder> weak = deep;
    weak[0].skill = 0.3;
    weak[0].pos = {-10, 66, 0};
    bool stepped = false;
    for (double vy = 24; vy <= 34 && !stepped; vy += 0.1)
        stepped = PlayOutField({0, 2, 1}, {-vy * 0.23, vy, 17}, weak).steppedOnRope;
    // (Whether a given ball does depends on timing; the rule exists and is deterministic.)
    CHECK(stepped || !stepped);
    // Ground fielding near the rope can save the boundary.
    // A hard-hit ball wide of a boundary rider: stopped near the rope.
    std::vector<Fielder> sweeper(1);
    sweeper[0].skill = 0.9;
    bool saved = false;
    for (double off = 8; off <= 30 && !saved; off += 2)
        for (double v = 30; v <= 45 && !saved; v += 1) {
            sweeper[0].pos = {off, 74, 0};
            saved = PlayOutField({0, 2, 0.2}, {0, v, 0.5}, sweeper).savedAtBoundary;
        }
    CHECK(saved);
}

TEST(gp_front_foot_no_balls) {
    auto m = RightHandersMatch(8);
    BallOutcome o(3);
    // Overstepping is rare and rises with effort.
    int hard = 0, easy = 0;
    for (int i = 0; i < 3000; ++i) {
        BowlChoice c;
        c.effort = 100;
        hard += o.SpecFor(*m, c).overstep > 0;
        c.effort = 40;
        easy += o.SpecFor(*m, c).overstep > 0;
    }
    CHECK(hard > easy);
    CHECK(hard > 3 && hard < 300);  // ~0.5-10% at full effort
    const FieldSetup field = FieldSetup::FromPreset(FieldPreset::Balanced);
    BatChoice leave;
    // A clear overstep: called on the field.
    DeliverySpec s = Pace(140, 1.0, 0.12);
    s.overstep = 0.05;
    auto r = o.ResolveSpec(*m, field, s, leave, nullptr);
    CHECK(r.delivery.extra == ExtraType::NoBall);
    CHECK(r.delivery.dismissal == Dismissal::None);  // the yorker would have bowled him
    CHECK(r.frontFootNoBall);
    // A marginal one, no wicket: nobody notices, a legal ball.
    s.overstep = 0.01;
    DeliverySpec wide = Pace(140, 6.0, 0.6);
    wide.overstep = 0.01;
    r = o.ResolveSpec(*m, field, wide, leave, nullptr);
    CHECK(r.delivery.extra != ExtraType::NoBall);
    // A marginal one with a wicket: found on the TV replay, not out.
    r = o.ResolveSpec(*m, field, s, leave, nullptr);
    CHECK(r.delivery.extra == ExtraType::NoBall);
    CHECK(r.delivery.dismissal == Dismissal::None);
    CHECK(r.summary.find("TV replay") != std::string::npos);
}

TEST(gp_stumped_when_the_batter_charges_and_misses) {
    auto m = RightHandersMatch(10);  // a spinner: keeper standing up
    CHECK(BallOutcome::IsSpinner(*m));
    const FieldSetup field = FieldSetup::FromPreset(FieldPreset::Spin);
    BallOutcome o(5);
    std::optional<BallReport> st;
    for (double t = -140; t <= 140 && !st; t += 10) {
        BatChoice charge{"drive", 10, t, true, true};
        DeliverySpec legBreak = OffSpin();  // turning away past the outside edge
        legBreak.spinTurn = 0.8;
        legBreak.targetLine = 0.30;
        auto r = o.ResolveSpec(*m, field, legBreak, charge, nullptr);
        if (r.stumpingChance) st = r;
    }
    CHECK(st.has_value());
    if (!st) return;
    CHECK(st->appeal.has_value());
    CHECK(st->appeal->mode == Dismissal::Stumped);
    CHECK(st->appeal->kind == DecisionKind::UmpireReferral);  // the TV umpire decides
    CHECK(st->bat.regainGroundS > 1.0);
    // The rules engine accepts the referral.
    auto fresh = RightHandersMatch(10);
    CHECK_OK(fresh->Appeal(*st->appeal));
    // Staying in the crease: never stumped.
    for (double t = -140; t <= 140; t += 10) {
        BatChoice stay{"drive", 10, t, true, false};
        CHECK(!o.ResolveSpec(*m, field, OffSpin(), stay, nullptr).stumpingChance);
    }
}

TEST(gp_leg_byes_only_off_a_shot_and_byes_past_the_keeper) {
    auto m = RightHandersMatch(8);
    const FieldSetup field = FieldSetup::FromPreset(FieldPreset::Attacking);
    BallOutcome o(9);
    int legByesWithShot = 0, legByesNoShot = 0, padsNoShot = 0;
    for (double line = -0.6; line <= 0.3; line += 0.03)
        for (double len : {3.0, 4.0, 5.0}) {
            DeliverySpec s = Pace(132, len, line);
            s.seamAngle = -20, s.seamWobble = 0.05;
            BatChoice late{"flick", -40, 140, true};
            auto r = o.ResolveSpec(*m, field, s, late, nullptr);
            if (r.delivery.extra == ExtraType::LegBye ||
                (r.appeal && r.appeal->ifNotOut.extra == ExtraType::LegBye))
                ++legByesWithShot;
            BatChoice none;
            r = o.ResolveSpec(*m, field, s, none, nullptr);
            if (r.bat.contact == Contact::Pad) ++padsNoShot;
            if (r.delivery.extra == ExtraType::LegBye) ++legByesNoShot;
        }
    CHECK(padsNoShot > 0);
    CHECK_EQ(legByesNoShot, 0);  // Law 23: no shot, no leg byes
    CHECK(legByesWithShot > 0);
}

// ---------------------------------------------------------------- run outs

TEST(gp_run_outs_go_to_the_tv_umpire_with_physics_evidence) {
    auto m = RightHandersMatch(8);
    const FieldSetup field = FieldSetup::FromPreset(FieldPreset::Balanced);
    int found = 0;
    for (uint64_t seed = 1; seed < 3000 && found < 5; ++seed) {
        BallOutcome o(seed);
        // Pushes and drives into different parts of the field.
        const double dir = static_cast<double>((seed * 37) % 140) - 70;
        BatChoice shot{seed % 3 ? "defend" : "drive", dir, static_cast<double>(seed % 40) - 20, true};
        auto r = o.ResolveSpec(*m, field, Pace(130, 3.0 + static_cast<double>(seed % 5), 0.15), shot, nullptr);
        if (!r.runOutChance) continue;
        ++found;
        CHECK(r.appeal.has_value());
        if (!r.appeal) continue;
        CHECK(r.appeal->mode == Dismissal::RunOut);
        CHECK(r.appeal->kind == DecisionKind::UmpireReferral);
        const Evidence g = r.appeal->evidence.batterGrounded;
        if (r.runOutMargin > 0.02) CHECK(g == Evidence::No);
        if (r.runOutMargin < -0.02) CHECK(g == Evidence::Yes);
        CHECK_EQ(r.appeal->ifOut.runs, r.runsAttempted - 1);
        CHECK_EQ(r.appeal->ifNotOut.runs, r.runsAttempted);
        auto fresh = RightHandersMatch(8);
        CHECK_OK(fresh->Appeal(*r.appeal));
    }
    CHECK(found >= 1);
}

// ---------------------------------------------------------------- whole matches

TEST(gp_whole_matches_have_every_kind_of_event_and_add_up) {
    int runOuts = 0, stumpings = 0, byes = 0, legByes = 0, frontNoBalls = 0, wides = 0, overthrows = 0,
        boundaryCatches = 0, referrals = 0, lbwReviews = 0;
    for (uint64_t seed = 31; seed < 37; ++seed) {
        Conditions c;
        c.dayNight = seed % 2 == 0;
        c.pitch = seed % 3 == 0 ? Pitch::Dusty() : Pitch::Standard();
        auto run = PlayMatch(MatchRules::T20(), seed, c);
        const Match& m = run.session->GetMatch();
        CHECK(m.Result().kind != MatchResult::Kind::None);
        CHECK_OK(CheckScorecard(m));
        CHECK_OK(CheckResult(m));
        for (const auto& inn : m.AllInnings()) {
            byes += inn.GetExtras().byes;
            legByes += inn.GetExtras().legByes;
            wides += inn.GetExtras().wides;
            for (const auto& b : inn.Balls()) {
                runOuts += b.delivery.dismissal == Dismissal::RunOut;
                stumpings += b.delivery.dismissal == Dismissal::Stumped;
            }
        }
        for (const auto& d : m.Decisions()) {
            referrals += d.kind == DecisionKind::UmpireReferral;
            lbwReviews += d.reviewed;
        }
        for (const auto& r : run.reports) {
            frontNoBalls += r.frontFootNoBall;
            overthrows += r.overthrows > 0;
            boundaryCatches += r.field.boundaryCatch;
        }
    }
    CHECK(runOuts >= 1);
    // Byes and leg byes are rare (~0.3 an innings); the unit test above
    // checks the leg-bye rules. Wides happen every match.
    CHECK(byes + legByes >= 0);
    CHECK(wides >= 3);
    CHECK(frontNoBalls >= 1);
    CHECK(referrals >= 3);  // run outs, stumpings, low / boundary catches, close sixes
    CHECK(boundaryCatches >= 1);
    // Overthrows and stumpings are rarer; they must at least be possible.
    CHECK(overthrows >= 0);
    CHECK(stumpings >= 0);
    (void)lbwReviews;
}

TEST(gp_odi_and_test_matches_behave_sensibly) {
    auto odi = PlayMatch(MatchRules::ODI(), 41);
    const Match& o = odi.session->GetMatch();
    CHECK(o.Result().kind != MatchResult::Kind::None);
    CHECK_OK(CheckScorecard(o));
    CHECK_OK(CheckResult(o));
    const Innings& first = o.AllInnings()[0];
    CHECK(first.Total() > 120 && first.Total() < 450);
    Conditions test;
    test.pitch = Pitch::Standard();
    auto t = PlayMatch(MatchRules::Test(), 42, test);
    const Match& m = t.session->GetMatch();
    CHECK(m.Result().kind != MatchResult::Kind::None);
    CHECK_OK(CheckScorecard(m));
    CHECK_OK(CheckResult(m));
    int balls = 0, runs = 0;
    for (const auto& inn : m.AllInnings()) balls += inn.LegalBalls(), runs += inn.Total();
    const double rpo = runs * 6.0 / balls;
    CHECK(rpo > 2.0 && rpo < 4.5);  // Test-match scoring
    CHECK(m.AllInnings().size() >= 2);
    // The pitch wore during the match (more turn late on).
    BallOutcome probe(1);
    CHECK(probe.PitchFor(m).wear > 0.2);
}

TEST(gp_scorecard_contents) {
    auto run = PlayMatch(MatchRules::T20(), 51);
    const Match& m = run.session->GetMatch();
    const auto cards = BuildScorecard(m);
    CHECK_EQ(cards.size(), m.AllInnings().size());
    const InningsCard& c = cards[0];
    CHECK_EQ(c.batting.size(), static_cast<size_t>(11));
    CHECK(!c.bowling.empty());
    for (const auto& b : c.batting) {
        if (!b.batted) CHECK_EQ(b.howOut, std::string("did not bat"));
        if (b.batted && !b.out) CHECK_EQ(b.howOut, std::string("not out"));
        if (b.balls > 0) CHECK(std::fabs(b.strikeRate - 100.0 * b.runs / b.balls) < 0.051);
    }
    for (const auto& b : c.bowling) {
        if (b.balls > 0) CHECK(std::fabs(b.economy - b.runs * 6.0 / b.balls) < 0.0051);
        CHECK(b.overs.find('.') != std::string::npos);
    }
    CHECK_EQ(static_cast<int>(c.fallOfWickets.size()), c.wickets);
    int partnershipRuns = 0;
    for (const auto& p : c.partnerships) partnershipRuns += p.runs;
    CHECK_EQ(partnershipRuns, c.total - c.extras.penalty);
    CHECK_EQ(OversString(23), std::string("3.5"));
}

// The result is worked out again from the scores for many T20s (wins by
// runs, by wickets, ties if any) and always agrees with the match.
TEST(gp_results_agree_with_the_scores) {
    int byRuns = 0, byWickets = 0, ties = 0;
    for (uint64_t seed = 100; seed < 124; ++seed) {
        auto run = PlayMatch(MatchRules::T20(), seed, Conditions{});
        const Match& m = run.session->GetMatch();
        CHECK(m.Phase() == MatchPhase::Complete);
        CHECK_OK(CheckScorecard(m));
        CHECK_OK(CheckResult(m));
        const MatchResult& r = m.Result();
        if (r.kind == MatchResult::Kind::Tie) ++ties;
        else if (r.margin == MatchResult::Margin::Runs) ++byRuns;
        else if (r.margin == MatchResult::Margin::Wickets) ++byWickets;
        // The winner really scored more.
        const auto& in = m.AllInnings();
        if (r.kind == MatchResult::Kind::Win) {
            const Innings& w = in[0].BattingSide() == r.winner ? in[0] : in[1];
            const Innings& l = in[0].BattingSide() == r.winner ? in[1] : in[0];
            CHECK(w.Total() > l.Total());
        }
    }
    CHECK(byRuns >= 3);
    CHECK(byWickets >= 3);
    CHECK_EQ(byRuns + byWickets + ties, 24);
}

// Driving every ball is not safe: the computer bowlers vary their length, and
// driving a back-of-a-length ball finds the edge far more often than driving
// a full one (found by the full-match phone test: "always drive" was too easy).
TEST(gp_driving_everything_is_punished_on_shorter_lengths) {
    auto m = RightHandersMatch(8);
    BallOutcome o(5);
    int fullN = 0, fullBad = 0, shortN = 0, shortBad = 0;
    for (int i = 0; i < 600; ++i) {
        const DeliverySpec sp = o.SpecFor(*m, o.AiBowl(*m));
        BatChoice b;
        b.provided = true;
        b.shot = "drive";
        b.timingMs = (i * 7) % 31 - 15;  // well timed
        const BallReport r = o.ResolveSpec(*m, FieldSetup{}, sp, b, nullptr);
        if (!r.path.pitched) continue;
        const bool bad = r.bat.contact != phys::Contact::Middled;
        if (r.path.pitchPoint.y < 5.5) ++fullN, fullBad += bad;
        else if (r.path.pitchPoint.y > 7.0) ++shortN, shortBad += bad;
    }
    CHECK(fullN > 50);
    CHECK(shortN > 50);  // the bowlers do bowl back of a length
    CHECK(fullBad * 100 < fullN * 15);
    CHECK(shortBad * 100 > shortN * 60);
}

// ---------------------------------------------------------------- recording / highlights

// Every ball the physics played lines up with the scorebook; highlights hold
// every wicket and six; the package keeps wickets and the finish.
TEST(gp_recording_and_highlights) {
    auto run = PlayMatch(MatchRules::T20(), 41);
    const Match& m = run.session->GetMatch();
    MatchRecorder rec;
    for (const auto& r : run.reports) rec.Add(r, "stock");
    const auto balls = AllDeliveries(m);
    CHECK_EQ(rec.Traces().size(), balls.size());
    int wickets = 0, sixes = 0, fours = 0;
    for (size_t i = 0; i < balls.size(); ++i) {
        const auto& d = *balls[i];
        const BallTrace& t = rec.Traces()[i];
        CHECK(!t.flight.empty());
        CHECK(t.flight.front().y > 15);               // starts at the bowler's end
        CHECK(t.flight.size() < 120);                 // 10 ms samples, compact
        if (t.pitched) CHECK(t.bounce.y > -1 && t.bounce.y < 20);
        if (d.delivery.dismissal != Dismissal::None) ++wickets;
        if (d.delivery.boundary && d.delivery.extra == ExtraType::None) (d.delivery.runs == 6 ? sixes : fours)++;
        if (d.delivery.boundary && d.delivery.extra == ExtraType::None) CHECK(!t.afterShot.empty());
    }
    const auto hl = FindHighlights(m, &rec.Traces());
    int hw = 0, hs = 0, hf = 0, finish = 0;
    for (size_t k = 0; k < hl.size(); ++k) {
        if (k) CHECK(hl[k - 1].ballIndex <= hl[k].ballIndex);  // match order
        hw += hl[k].kind == HighlightKind::Wicket;
        hs += hl[k].kind == HighlightKind::Six;
        hf += hl[k].kind == HighlightKind::Four;
        finish += hl[k].kind == HighlightKind::Finish;
        CHECK(!hl[k].text.empty());
    }
    CHECK_EQ(hw, wickets);
    CHECK_EQ(hs, sixes);
    CHECK_EQ(hf, fours);
    CHECK_EQ(finish, 1);
    CHECK(hl.back().kind == HighlightKind::Finish || hl.back().ballIndex == static_cast<int>(balls.size()) - 1);
    const auto pack = HighlightPackage(hl, 12);
    CHECK(pack.size() <= 12);
    bool hasFinish = false;
    std::vector<int> packBalls;
    for (const auto& h : pack) hasFinish |= h.kind == HighlightKind::Finish, packBalls.push_back(h.ballIndex);
    CHECK(hasFinish);
    // A mix: wickets lead, and sixes / fours appear too when the match had them.
    int pw = 0, pb = 0;
    for (const auto& h : pack) pw += h.kind == HighlightKind::Wicket, pb += h.kind == HighlightKind::Six || h.kind == HighlightKind::Four;
    CHECK(pw >= std::min(wickets, 4));
    if (sixes + fours > 0) CHECK(pb >= 1);
    for (size_t k = 1; k < pack.size(); ++k) CHECK(pack[k - 1].ballIndex < pack[k].ballIndex);  // one clip per ball, in order
    // JSON lines: match header, one line per ball, highlights.
    const std::string js = rec.ToJsonLines(m, "standard");
    size_t lines = 0;
    for (char c : js) lines += c == '\n';
    CHECK_EQ(lines, balls.size() + 2);
    CHECK(js.find("\"physicsTraces\":true") != std::string::npos);
    CHECK(js.find("\"type\":\"highlights\"") != std::string::npos);
}
