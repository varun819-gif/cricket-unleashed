// Ball physics: flight, swing, spin, bounce, bat impact, fielding, and the
// physics-driven scoring of a ball. Ranges are realistic cricket values
// (see docs/BALL_PHYSICS.md for sources). Everything here is deterministic.
#include <cmath>
#include <memory>

#include "Test.h"
#include "cricket/BallOutcome.h"
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

DeliverySpec Spinner(double turn) {
    DeliverySpec s;
    s.speedKph = 86;
    s.targetLength = 4.6;
    s.targetLine = turn < 0 ? 0.25 : -0.10;
    s.releaseHeight = 2.0;
    s.spinRps = 24;
    s.spinTurn = turn;
    s.spinTop = 0.4;
    return s;
}

std::vector<Fielder> NoField() { return {}; }

std::unique_ptr<Match> TestMatch(int bowler) {
    auto all = NationalTeams();
    for (auto& p : all[0].xi) p.bats = Handedness::Right;  // the tests use right-hander coordinates
    auto m = std::make_unique<Match>(MatchRules::T20(), std::array<Team, 2>{all[0], all[1]}, 0);
    m->SetOpeners(0, 1);
    m->SetBowler(bowler);
    return m;
}

}  // namespace

// ---------------------------------------------------------------- determinism

TEST(phys_same_inputs_same_path_bit_for_bit) {
    DeliverySpec s = Pace(138, 5.5, 0.15);
    s.seamAngle = 18, s.seamWobble = 0.1, s.seamNip = 0.4;
    const auto a = SimulateDelivery(s, Pitch::Green());
    const auto b = SimulateDelivery(s, Pitch::Green());
    CHECK_EQ(PathHash(a.samples), PathHash(b.samples));
    // Golden values: identical on GCC and Clang, debug and release builds
    // (no fused multiply-add, our own sine/cosine). A different number on
    // another machine means the physics would disagree between players.
    CHECK_EQ(PathHash(a.samples), 1477018463051376057ULL);
    const auto c = SimulateDelivery(Spinner(-0.8), Pitch::Dusty());
    CHECK_EQ(PathHash(c.samples), 11064938497984393955ULL);
    // A tiny change of input changes the path.
    s.speedKph += 0.001;
    CHECK(PathHash(SimulateDelivery(s, Pitch::Green()).samples) != PathHash(a.samples));
}

TEST(phys_deterministic_trig_matches_library) {
    for (double x = -10; x <= 10; x += 0.37) {
        CHECK(std::fabs(DetSin(x) - std::sin(x)) < 1e-12);
        CHECK(std::fabs(DetCos(x) - std::cos(x)) < 1e-12);
    }
}

TEST(phys_gravity_only_matches_the_formula) {
    // No air, no spin: the integrator must give the textbook parabola.
    Environment vacuum;
    vacuum.airDensity = 0;
    DeliverySpec s;  // no swing / spin
    const V3 p0{0, 18, 2}, v0{0.5, -30, 2};
    const auto path = Integrate(p0, v0, {}, s, Pitch{}, vacuum, 5.0, 2.0, false);
    const Sample& last = path.back();
    const double t = last.t;
    CHECK(std::fabs(last.p.x - (0.5 * t)) < 1e-9);
    CHECK(std::fabs(last.p.y - (18 - 30 * t)) < 1e-9);
    CHECK(std::fabs(last.p.z - (2 + 2 * t - 0.5 * 9.81 * t * t)) < 1e-9);
}

// ---------------------------------------------------------------- flight

TEST(phys_speed_and_flight_time_are_realistic) {
    const auto fast = SimulateDelivery(Pace(145, 6.0, 0.1), Pitch::Standard());
    // ~145 km/h: about 0.5 s from hand to the batter.
    CHECK(fast.timeAtCrease > 0.42 && fast.timeAtCrease < 0.55);
    // Air drag: ~5-12% slower by the bounce; the bounce takes more.
    const double airLoss = 1.0 - fast.speedAtBounceKph / 145.0;
    CHECK(airLoss > 0.05 && airLoss < 0.12);
    CHECK(fast.speedAfterBounceKph < fast.speedAtBounceKph);
    CHECK(fast.speedAfterBounceKph > 0.75 * 145);
    const auto slow = SimulateDelivery(Spinner(-0.8), Pitch::Standard());
    CHECK(slow.timeAtCrease > 0.75 && slow.timeAtCrease < 1.0);
}

TEST(phys_bowler_hits_the_spot_it_aims_at) {
    for (double kph : {80.0, 120.0, 150.0})
        for (double len : {1.0, 3.0, 5.5, 8.0, 11.0})
            for (double line : {-0.3, 0.0, 0.4}) {
                DeliverySpec s = Pace(kph, len, line);
                s.seamAngle = 15;  // even with swing
                const auto d = SimulateDelivery(s, Pitch::Standard());
                CHECK(d.pitched);
                CHECK(std::fabs(d.pitchPoint.y - len) < 0.02);
                CHECK(std::fabs(d.pitchPoint.x - line) < 0.03);
            }
}

TEST(phys_lengths_behave_like_cricket) {
    const auto yorker = SimulateDelivery(Pace(140, 1.0, 0.0), Pitch::Standard());
    CHECK(yorker.atStumps.z < 0.3);
    CHECK(yorker.wouldHitStumps);
    const auto good = SimulateDelivery(Pace(140, 6.0, 0.0), Pitch::Standard());
    CHECK(good.atStumps.z > 0.45 && good.atStumps.z < 0.85);  // around the top of the stumps
    const auto bouncer = SimulateDelivery(Pace(140, 11.0, 0.0), Pitch::Standard());
    CHECK(bouncer.atCrease.z > 1.3);  // chest / head height
    CHECK(!bouncer.wouldHitStumps);
    // Bouncier pitch -> higher at the batter.
    const auto bouncy = SimulateDelivery(Pace(140, 6.0, 0.0), Pitch::Bouncy());
    CHECK(bouncy.atCrease.z > good.atCrease.z + 0.05);
    // Full toss: never touches the pitch before the batter.
    const auto full = SimulateDelivery(Pace(130, -1.5, 0.0), Pitch::Standard());
    CHECK(!full.pitched);
}

// ---------------------------------------------------------------- swing

TEST(phys_conventional_swing) {
    DeliverySpec out = Pace(130, 6.0, 0.2);
    out.seamAngle = 20, out.seamWobble = 0.05, out.shine = 1.0;
    const auto o = SimulateDelivery(out, Pitch::Standard());
    CHECK(o.swingBeforePitch > 0.12 && o.swingBeforePitch < 0.45);  // outswinger: towards the slips
    DeliverySpec in = out;
    in.seamAngle = -20;
    const auto i = SimulateDelivery(in, Pitch::Standard());
    CHECK(std::fabs(i.swingBeforePitch + o.swingBeforePitch) < 0.02);  // (almost) a mirror image
    // Wobbly seam: much less swing.
    out.seamWobble = 0.8;
    CHECK(SimulateDelivery(out, Pitch::Standard()).swingBeforePitch < o.swingBeforePitch * 0.4);
    // Upright seam pointing straight: no swing.
    DeliverySpec straight = Pace(130, 6.0, 0.2);
    straight.seamAngle = 0;
    CHECK(std::fabs(SimulateDelivery(straight, Pitch::Standard()).swingBeforePitch) < 1e-9);
    // Too slow to swing much (the air flow doesn't separate unevenly).
    DeliverySpec slow = Pace(60, 6.0, 0.2);
    slow.seamAngle = 20, slow.seamWobble = 0.05;
    CHECK(std::fabs(SimulateDelivery(slow, Pitch::Standard()).swingBeforePitch) < o.swingBeforePitch / 3);
}

TEST(phys_reverse_swing_needs_pace_and_an_old_ball) {
    DeliverySpec old = Pace(148, 6.0, 0.2);
    old.seamAngle = 20, old.seamWobble = 0.05, old.shine = 0.1;
    const auto r = SimulateDelivery(old, Pitch::Standard());
    CHECK(r.swingBeforePitch < -0.08);  // same seam, swings the OTHER way
    old.speedKph = 115;  // not quick enough: conventional again
    CHECK(SimulateDelivery(old, Pitch::Standard()).swingBeforePitch > 0.05);
    CHECK(SwingCoefficient(41, 20, 0, 0.0) < 0);
    CHECK(SwingCoefficient(31, 20, 0, 1.0) > 0);
}

// ---------------------------------------------------------------- bounce, seam, spin

TEST(phys_seam_movement_off_the_pitch) {
    DeliverySpec s = Pace(135, 6.0, 0.1);
    s.seamNip = 1.0, s.seamWobble = 0.0;
    const auto green = SimulateDelivery(s, Pitch::Green());
    const auto flat = SimulateDelivery(s, Pitch::Flat());
    CHECK(green.deviationOffPitch > 0.03 && green.deviationOffPitch < 0.15);
    CHECK(flat.deviationOffPitch < green.deviationOffPitch * 0.3);
    s.seamNip = -1.0;
    CHECK(SimulateDelivery(s, Pitch::Green()).deviationOffPitch < -0.03);
}

TEST(phys_spin_turns_after_the_bounce) {
    const auto off = SimulateDelivery(Spinner(-0.8), Pitch::Standard());
    const auto offDusty = SimulateDelivery(Spinner(-0.8), Pitch::Dusty());
    const auto leg = SimulateDelivery(Spinner(+0.8), Pitch::Dusty());
    CHECK(off.deviationOffPitch < -0.03);       // off-break turns into a right-hander
    CHECK(leg.deviationOffPitch > 0.03);        // leg-break turns away
    CHECK(offDusty.deviationOffPitch < off.deviationOffPitch);  // more grip, more turn
    // A few degrees of turn, not a right angle.
    CHECK(std::fabs(offDusty.deviationOffPitch) < 0.30);
    // Spin takes pace off at the bounce on a gripping pitch.
    CHECK(offDusty.speedAfterBounceKph < off.speedAfterBounceKph);
    // Top-spin dips and bounces higher than back-spin.
    DeliverySpec top = Spinner(0), back = Spinner(0);
    top.spinTop = 1, back.spinTop = -1;
    const auto t = SimulateDelivery(top, Pitch::Standard());
    const auto b = SimulateDelivery(back, Pitch::Standard());
    CHECK(t.atCrease.z > b.atCrease.z);
}

// ---------------------------------------------------------------- batting

TEST(phys_timing_decides_how_hard_it_goes) {
    const auto d = SimulateDelivery(Pace(135, 3.2, 0.15), Pitch::Standard());
    ShotInput in;
    in.shot = Shot::Drive;
    in.direction = 15;
    double prev = 1e9;
    for (double t : {0.0, 60.0, 120.0}) {
        in.timingMs = t;
        const auto r = PlayShot(d, Pace(135, 3.2, 0.15), Pitch::Standard(), in);
        CHECK(r.exitSpeedKph < prev);  // worse timing, slower off the bat
        prev = r.exitSpeedKph;
    }
    in.timingMs = 0;
    const auto best = PlayShot(d, Pace(135, 3.2, 0.15), Pitch::Standard(), in);
    CHECK_EQ(std::string(ToString(best.contact)), std::string("middled"));
    CHECK(best.exitSpeedKph > 100 && best.exitSpeedKph < 160);
    // Late = squarer on the off side.
    in.timingMs = 50;
    const auto late = PlayShot(d, Pace(135, 3.2, 0.15), Pitch::Standard(), in);
    CHECK(late.exitVelocity.x / late.exitVelocity.y > best.exitVelocity.x / best.exitVelocity.y);
    // Far too late: the ball is past the bat.
    in.timingMs = 200;
    CHECK(PlayShot(d, Pace(135, 3.2, 0.15), Pitch::Standard(), in).contact != Contact::Middled);
}

TEST(phys_shots_go_where_they_should) {
    const auto half = SimulateDelivery(Pace(135, 3.2, 0.15), Pitch::Standard());
    ShotInput loft;
    loft.shot = Shot::Loft;
    loft.direction = -10;
    const auto l = PlayShot(half, Pace(135, 3.2, 0.15), Pitch::Standard(), loft);
    const auto lf = PlayOutField(l.ballAtContact, l.exitVelocity, NoField());
    CHECK(lf.boundary && lf.runs == 6);  // a clean loft clears an empty 70 m ground
    ShotInput drive;
    drive.shot = Shot::Drive;
    drive.direction = 20;
    const auto dr = PlayShot(half, Pace(135, 3.2, 0.15), Pitch::Standard(), drive);
    const auto df = PlayOutField(dr.ballAtContact, dr.exitVelocity, NoField());
    CHECK(df.carry < 15);  // a drive is kept along the ground
    CHECK(df.boundary && df.runs == 4);
    ShotInput block;
    block.shot = Shot::Defend;
    const auto b = PlayShot(half, Pace(135, 3.2, 0.15), Pitch::Standard(), block);
    CHECK(b.exitSpeedKph < 30);  // dead bat
    // Short ball: the pull goes to the leg side, the cut to the off side.
    const auto shortBall = SimulateDelivery(Pace(140, 9.5, 0.0), Pitch::Standard());
    ShotInput pull;
    pull.shot = Shot::Pull;
    pull.direction = -70;
    const auto p = PlayShot(shortBall, Pace(140, 9.5, 0.0), Pitch::Standard(), pull);
    CHECK(p.contact == Contact::Middled);
    CHECK(p.exitVelocity.x < 0);
    const auto wideShort = SimulateDelivery(Pace(140, 9.0, 0.7), Pitch::Standard());
    ShotInput cut;
    cut.shot = Shot::Cut;
    cut.direction = 95;
    const auto c = PlayShot(wideShort, Pace(140, 9.0, 0.7), Pitch::Standard(), cut);
    CHECK(c.contact == Contact::Middled);
    CHECK(c.exitVelocity.x > 0);
}

TEST(phys_late_movement_finds_the_edge) {
    // A big late outswinger beats a weak batter's defence more than a good one's.
    DeliverySpec s = Pace(136, 6.2, 0.10);
    s.seamAngle = 20, s.seamWobble = 0.02, s.seamNip = 1.0;
    const auto d = SimulateDelivery(s, Pitch::Green());
    ShotInput weak;
    weak.shot = Shot::Defend;
    weak.skill = 0.1;
    ShotInput strong = weak;
    strong.skill = 1.0;
    const auto w = PlayShot(d, s, Pitch::Green(), weak);
    const auto g = PlayShot(d, s, Pitch::Green(), strong);
    CHECK(w.readError > g.readError);
    CHECK(w.contact == Contact::OutsideEdge || w.contact == Contact::Missed);
    // Straight ball, no movement: both read it well.
    const auto plain = SimulateDelivery(Pace(136, 6.2, 0.0), Pitch::Standard());
    CHECK(PlayShot(plain, Pace(136, 6.2, 0.0), Pitch::Standard(), weak).readError < 0.02);
}

TEST(phys_left_hander_is_a_mirror_image) {
    DeliverySpec s = Pace(135, 3.2, 0.15);
    DeliverySpec m = s;
    m.targetLine = -0.15;
    m.releaseX = -s.releaseX;
    const auto dr = SimulateDelivery(s, Pitch::Standard());
    const auto dl = SimulateDelivery(m, Pitch::Standard());
    ShotInput r;
    r.shot = Shot::Drive;
    r.direction = 20;
    ShotInput l = r;
    l.leftHanded = true;
    const auto a = PlayShot(dr, s, Pitch::Standard(), r);
    const auto b = PlayShot(dl, m, Pitch::Standard(), l);
    CHECK(std::fabs(a.exitVelocity.x + b.exitVelocity.x) < 1e-9);
    CHECK(std::fabs(a.exitVelocity.y - b.exitVelocity.y) < 1e-9);
}

// ---------------------------------------------------------------- fielding

TEST(phys_fielders_catch_stop_and_runs_come_from_the_gaps) {
    // Straight at mid-off along the ground: no run.
    std::vector<Fielder> f(1);
    f[0].pos = {5.4, 25.4, 0};  // mid-off
    f[0].skill = 0.8;
    f[0].index = 7;
    const auto at = PlayOutField({0, 2, 0.3}, {5.4 * 0.5, 23.4 * 0.5, 0.2}, f);
    CHECK(!at.boundary);
    CHECK_EQ(at.runs, 0);
    CHECK_EQ(at.fielder, 7);
    // Same shot with nobody there: four.
    const auto gap = PlayOutField({0, 2, 0.3}, {5.4 * 1.3, 23.4 * 1.3, 0.2}, NoField());
    CHECK(gap.boundary);
    CHECK_EQ(gap.runs, 4);
    // A slow push into the empty off side: a quick single.
    std::vector<Fielder> ring(1);
    ring[0].pos = {22, 0, 0};  // point
    const auto push = PlayOutField({0, 2, 0.3}, {-3, 5, 0}, ring);
    CHECK(push.runs >= 1);
    // A skier to long-on: caught.
    std::vector<Fielder> deep(1);
    deep[0].pos = {-19, 72, 0};
    deep[0].skill = 0.7;
    deep[0].index = 3;
    // Find a launch that lands near long-on.
    bool caughtOne = false;
    for (double vy = 20; vy <= 30 && !caughtOne; vy += 0.5) {
        const auto sky = PlayOutField({0, 2, 1}, {-vy * 0.26, vy, 18}, deep);
        caughtOne = sky.caught && sky.fielder == 3;
    }
    CHECK(caughtOne);
}

TEST(phys_edges_carry_to_the_slips) {
    DeliverySpec s = Pace(138, 6.0, 0.15);
    s.seamAngle = 20, s.seamWobble = 0.02, s.seamNip = 1.0;
    const auto d = SimulateDelivery(s, Pitch::Green());
    ShotInput in;
    in.shot = Shot::Drive;
    in.direction = 30;
    in.skill = 0.6;
    // Search the batter / timing that produces a thick outside edge (deterministic).
    std::optional<BatResult> edge;
    for (double skill = 0; skill <= 1.0 && !edge; skill += 0.05)
        for (double t = -60; t <= 60 && !edge; t += 5) {
            in.skill = skill;
            in.timingMs = t;
            const auto r = PlayShot(d, s, Pitch::Green(), in);
            if (r.contact == Contact::OutsideEdge && r.edgeFraction < 0.6) edge = r;
        }
    CHECK(edge.has_value());
    if (!edge) return;
    CHECK(edge->exitVelocity.y < 0);  // backwards, behind the batter
    CHECK(edge->exitVelocity.x > 0);  // towards the slips (off side)
    const FieldSetup attacking = FieldSetup::FromPreset(FieldPreset::Attacking);
    const auto fielders = FieldersFor(attacking, {}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, false, false);
    const auto fr = PlayOutField(edge->ballAtContact, edge->exitVelocity, fielders, {}, {}, true);
    CHECK(fr.caught || fr.dropped || fr.fielder >= 0);  // the cordon gets to it
}

// ---------------------------------------------------------------- scoring through the rules engine

TEST(phys_outcome_bowled_lbw_wide_noball) {
    auto mp = TestMatch(8);
    Match& m = *mp;
    const FieldSetup field = FieldSetup::FromPreset(FieldPreset::Balanced);
    BallOutcome o(1);
    BatChoice leave;
    // Yorker at off stump, no shot: past the pads, bowled.
    auto r = o.ResolveSpec(m, field, Pace(140, 1.0, 0.12), leave, nullptr);
    CHECK(r.path.wouldHitStumps);
    CHECK(r.delivery.dismissal == Dismissal::Bowled);
    // Inswingers: find one that hits the pad on the way to the stumps.
    std::optional<BallReport> pad;
    for (double line = 0.0; line <= 0.5 && !pad; line += 0.02) {
        DeliverySpec in = Pace(135, 4.5, line);
        in.seamAngle = -20, in.seamWobble = 0.05;
        auto x = o.ResolveSpec(m, field, in, leave, nullptr);
        if (x.bat.contact == Contact::Pad && x.appeal) pad = x;
    }
    CHECK(pad.has_value());
    if (pad) {
        CHECK(pad->appeal->mode == Dismissal::Lbw);
        CHECK(pad->appeal->evidence.lbw.has_value());
        CHECK(!pad->appeal->evidence.lbw->shotOffered);
        CHECK(pad->appeal->evidence.lbw->wickets != WicketsZone::Missing);  // ball tracking says it was going on
        CHECK(!pad->bat.trackedToStumps.empty());
    }
    // Way outside off, no shot: wide (limited overs).
    r = o.ResolveSpec(m, field, Pace(135, 6.0, 1.3), leave, nullptr);
    CHECK(r.delivery.extra == ExtraType::Wide);
    // Waist-high full toss: no-ball.
    DeliverySpec beamer = Pace(140, -6.0, 0.0);
    r = o.ResolveSpec(m, field, beamer, leave, nullptr);
    CHECK(!r.path.pitched);
    if (r.path.atCrease.z > 1.0) CHECK(r.delivery.extra == ExtraType::NoBall);
    // The rules engine accepts every physics outcome.
    CHECK(m.Current().Check(r.delivery).ok);
}

TEST(phys_outcome_is_pure_given_the_ball_and_the_shot) {
    auto mp = TestMatch(8);
    Match& m = *mp;
    const FieldSetup field = FieldSetup::FromPreset(FieldPreset::Powerplay);
    BatChoice drive{"drive", 25, 12, true};
    for (double len : {2.0, 4.0, 6.0, 9.0}) {
        // The physics (flight, bat, field) never depends on the seed ...
        BallOutcome a(1), b(999), a2(1);
        const auto ra = a.ResolveSpec(m, field, Pace(138, len, 0.2), drive, nullptr);
        const auto rb = b.ResolveSpec(m, field, Pace(138, len, 0.2), drive, nullptr);
        CHECK_EQ(PathHash(ra.path.samples), PathHash(rb.path.samples));
        CHECK(ra.bat.contact == rb.bat.contact);
        CHECK_EQ(PathHash(ra.field.path), PathHash(rb.field.path));
        // ... only the batters' judgement of a run does (a player decision),
        // and the same seed gives exactly the same ball.
        const auto ra2 = a2.ResolveSpec(m, field, Pace(138, len, 0.2), drive, nullptr);
        CHECK_EQ(ra.summary, ra2.summary);
        CHECK_EQ(ra.delivery.runs, ra2.delivery.runs);
    }
}

TEST(phys_ai_bowler_and_batter_are_seeded) {
    auto mp = TestMatch(8);
    Match& m = *mp;
    BallOutcome a(77), b(77), c(78);
    std::string sa, sb, sc;
    for (int i = 0; i < 20; ++i) {
        const auto specA = a.SpecFor(m, a.AiBowl(m));
        const auto specB = b.SpecFor(m, b.AiBowl(m));
        const auto specC = c.SpecFor(m, c.AiBowl(m));
        sa += std::to_string(specA.targetLength) + "|";
        sb += std::to_string(specB.targetLength) + "|";
        sc += std::to_string(specC.targetLength) + "|";
    }
    CHECK_EQ(sa, sb);
    CHECK(sa != sc);
}
