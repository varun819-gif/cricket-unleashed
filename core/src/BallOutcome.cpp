#include "cricket/BallOutcome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cricket {

using namespace phys;

namespace {

double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::string Fmt(const char* f, double a) {
    char b[64];
    std::snprintf(b, sizeof b, f, a);
    return b;
}

const Player& StrikerOf(const Match& m) {
    return m.GetTeam(m.BattingSide()).xi.at(static_cast<size_t>(std::max(0, m.Current().Striker())));
}

// The fielding XI mapped onto the eleven fielding spots: the nine positions of
// the field setup, then the wicketkeeper, then the bowler.
struct FieldMap {
    std::vector<Fielder> fielders;
    std::vector<std::string> names;  // by fielder slot
};

FieldMap MapField(const Match& m, const FieldSetup& setup, bool lefty, bool spinner) {
    const Team& t = m.GetTeam(m.FieldingSide());
    const int bowler = m.Current().CurrentBowler();
    const int keeper = t.keeper;
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(t.xi.size()); ++i)
        if (i != bowler && i != keeper) order.push_back(i);
    std::vector<int> idx;
    std::vector<double> skill;
    for (size_t k = 0; k < setup.fielders.size(); ++k) {
        const int p = k < order.size() ? order[k] : -1;
        idx.push_back(p);
        skill.push_back(p >= 0 ? t.xi[static_cast<size_t>(p)].fielding / 100.0 : 0.5);
    }
    idx.push_back(keeper);
    skill.push_back(t.xi.at(static_cast<size_t>(keeper)).fielding / 100.0);
    idx.push_back(bowler);
    skill.push_back(bowler >= 0 ? t.xi.at(static_cast<size_t>(bowler)).fielding / 100.0 : 0.5);
    FieldMap fm;
    fm.fielders = FieldersFor(setup, skill, idx, lefty, spinner);
    for (const auto& f : setup.fielders) fm.names.push_back(f.name);
    fm.names.push_back("wicketkeeper");
    fm.names.push_back("the bowler");
    return fm;
}

std::string NameOf(const FieldMap& fm, int playerIndex) {
    for (size_t k = 0; k < fm.fielders.size(); ++k)
        if (fm.fielders[k].index == playerIndex && k < fm.names.size()) return fm.names[k];
    return "a fielder";
}

// Does the ball (after an inside / bottom edge) roll back onto the stumps?
bool PlayedOn(const std::vector<Sample>& s) {
    for (size_t i = 1; i < s.size(); ++i)
        if (s[i - 1].p.y > 0 && s[i].p.y <= 0) {
            const double f = s[i - 1].p.y / (s[i - 1].p.y - s[i].p.y);
            const double x = s[i - 1].p.x + (s[i].p.x - s[i - 1].p.x) * f;
            const double z = s[i - 1].p.z + (s[i].p.z - s[i - 1].p.z) * f;
            return std::fabs(x) <= kStumpHalfWidth + kBallRadius && z - kBallRadius <= kStumpHeight;
        }
    return false;
}

}  // namespace

// ---------------------------------------------------------------- DetRandom
uint64_t DetRandom::Next() {
    uint64_t z = (s_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
double DetRandom::Uniform() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
double DetRandom::Normal(double sigma) {
    // Sum of four uniforms: mean 2, variance 1/3 -> scaled to sigma.
    const double s = Uniform() + Uniform() + Uniform() + Uniform() - 2.0;
    return s * sigma * 1.7320508075688772;
}

// ---------------------------------------------------------------- bowling
bool BallOutcome::IsSpinner(const Match& m) {
    const int b = m.Current().CurrentBowler();
    return b >= 0 && m.GetTeam(m.FieldingSide()).xi.at(static_cast<size_t>(b)).bowls == BowlingKind::Spin;
}

DeliverySpec BallOutcome::SpecFor(const Match& m, const BowlChoice& c) {
    const Innings& inn = m.Current();
    const int b = inn.CurrentBowler();
    const Player& p = m.GetTeam(m.FieldingSide()).xi.at(static_cast<size_t>(std::max(0, b)));
    const double skill = p.bowling / 100.0;
    const double effort = Clamp(c.effort, 0, 100) / 100.0;
    const bool spin = p.bowls == BowlingKind::Spin;
    const double away = StrikerOf(m).bats == Handedness::Left ? -1.0 : 1.0;  // striker's off side
    DeliverySpec s;
    s.releaseX = 0.30;  // right-arm over the wicket
    double length = 0.8 + 10.2 * Clamp(c.length, 0, 1);
    double line = Clamp(c.line, -1, 1) * 0.8;
    const std::string& v = c.variation;
    if (spin) {
        s.speedKph = (76 + 18 * skill) * (0.95 + 0.08 * effort);
        s.releaseHeight = 2.0;
        s.spinRps = 20 + 8 * skill;
        s.spinTop = 0.35;
        s.seamWobble = 0.5;
        if (v == "legbreak") s.spinTurn = 0.85 * away;
        else if (v == "googly") s.spinTurn = -0.8 * away, s.spinTop = 0.3;
        else if (v == "armball") s.spinRps = 8, s.spinTurn = 0, s.seamAngle = -12 * away, s.seamWobble = 0.2,
                                 s.speedKph *= 1.06;
        else s.spinTurn = -0.85 * away;  // offbreak / stock / slower
        if (v == "slower") s.speedKph *= 0.88, s.spinTop = 0.5;  // more flight
    } else {
        s.speedKph = (118 + 32 * skill) * (0.93 + 0.10 * effort);
        s.releaseHeight = 2.15;
        // The ball's condition: shiny when new, rough after ~30-40 overs.
        s.shine = Clamp(1.0 - inn.OversWithCurrentBall() / 40.0, 0, 1);
        s.seamAngle = 8 * away;  // natural slight away movement
        s.seamWobble = 0.25;
        if (v == "outswing") s.seamAngle = 20 * away, s.seamWobble = 0.08;
        else if (v == "inswing") s.seamAngle = -20 * away, s.seamWobble = 0.08;
        else if (v == "yorker") length = 1.0;
        else if (v == "bouncer") length = std::max(length, 10.0);
        else if (v == "slower") s.speedKph *= 0.80, s.seamWobble = 0.7, s.seamAngle = 0;
    }
    // Execution: nobody hits the same spot every ball.
    const double spread = 0.8 + 0.4 * effort;
    length += rng_.Normal((0.55 - 0.35 * skill) * spread);
    line += rng_.Normal((0.16 - 0.08 * skill) * spread);
    if (rng_.Uniform() < 0.06 - 0.03 * skill) line += rng_.Uniform() < 0.6 ? -0.7 : 0.8;  // the occasional stray one
    s.targetLength = Clamp(length, -2.0, 13.0);
    s.targetLine = line * away;
    s.seamNip = rng_.Uniform() * 2 - 1;  // which way the seam bites on landing
    // Where the front foot lands: effort and tiredness push it over the line.
    s.overstep = rng_.Normal(0.035) + (-0.10 + 0.08 * effort - 0.04 * skill);
    return s;
}

BowlChoice BallOutcome::AiBowl(const Match& m) {
    const Innings& inn = m.Current();
    BowlChoice c;
    c.effort = 75 + static_cast<int>(rng_.Uniform() * 15);
    const double u = rng_.Uniform();
    if (IsSpinner(m)) {
        const bool wrist = inn.CurrentBowler() % 2 == 1;
        c.variation = wrist ? (u < 0.2 ? "googly" : "legbreak") : (u < 0.15 ? "armball" : "offbreak");
        c.length = 0.34 + rng_.Uniform() * 0.12;
        c.line = 0.10 + rng_.Uniform() * 0.25;
        return c;
    }
    const int over = inn.CurrentOverNumber();
    const int maxOvers = inn.MaxOvers();
    const bool death = maxOvers > 0 && over > maxOvers - 4;
    const bool newBall = inn.OversWithCurrentBall() < 8;
    c.length = 0.42 + rng_.Uniform() * 0.14;
    c.line = 0.08 + rng_.Uniform() * 0.30;
    if (death && u < 0.30) c.variation = "yorker", c.line = 0.0 + rng_.Uniform() * 0.15;
    else if (death && u < 0.40) c.variation = "yorker", c.line = 0.95;  // wide yorker outside off
    else if (death && u < 0.55) c.variation = "slower";
    else if (newBall && u < 0.50) c.variation = rng_.Uniform() < 0.65 ? "outswing" : "inswing";
    else if (u < 0.08) c.variation = "bouncer", c.length = 0.95, c.line = -0.1;
    return c;
}

// ---------------------------------------------------------------- batting
BatChoice BallOutcome::AiShot(const Match& m, const DeliveryPath& path, const DeliverySpec& spec) {
    const Innings& inn = m.Current();
    const Player& p = StrikerOf(m);
    const double skill = p.batting / 100.0 * (0.8 + 0.2 * p.form / 100.0);
    const double flip = p.bats == Handedness::Left ? -1.0 : 1.0;
    // How aggressive: format and match situation.
    double a = 0.15;
    bool death = false;
    if (inn.MaxOvers() > 0) {
        a = inn.MaxOvers() <= 20 ? 0.55 : 0.33;
        const int ballsLeft = inn.MaxOvers() * inn.Rules().ballsPerOver - inn.LegalBalls();
        if (ballsLeft <= 24) a += 0.2;
        death = ballsLeft <= 30;
        if (inn.Target()) {
            const double rrr = inn.RequiredRunRate();
            a += Clamp((rrr - 8.0) / 10.0, -0.25, 0.35);
        }
        a -= 0.05 * std::max(0, inn.Wickets() - 5);
    }
    a = Clamp(a, 0.05, 0.9);
    // What the batter can see early: the length and line.
    const double pitchY = path.pitched ? path.pitchPoint.y : 0.0;
    const double x = path.atCrease.x * flip;
    const double z = path.atCrease.z;
    const bool spin = spec.spinRps > 12;
    BatChoice b;
    b.provided = true;
    auto pick = [&](const char* shot, double dir) {
        b.shot = shot;
        b.direction = dir;
    };
    const bool attack = rng_.Uniform() < a;
    // A push into a gap for a quick single (soft hands).
    auto push = [&] { pick("defend", rng_.Uniform() < 0.5 ? -70 + rng_.Uniform() * 35 : 35 + rng_.Uniform() * 35); };
    if (z > 1.75) pick("leave", 0);
    else if (x > 0.95) {
        if (pitchY > 6 && attack) pick("cut", 95);
        else pick("leave", 0);
    } else if (pitchY > 8.0) {  // short
        if (z > 1.0) {
            if (x < 0.3) pick(attack || rng_.Uniform() < 0.3 ? "pull" : "defend", -70);
            else pick(attack ? "cut" : "leave", 95);
        } else if (x > 0.35) pick("cut", 90);
        else if (attack) pick("pull", -60);
        else push();
    } else if (!path.pitched || pitchY < 3.0) {  // full
        if (x < -0.2) pick("flick", -50);
        else if (attack && rng_.Uniform() < 0.5) pick("loft", Clamp(10 + 40 * x, -30, 50));
        else pick("drive", Clamp(15 + 60 * x, -20, 70));
    } else {  // good length
        if (x > 0.45) {
            if (!attack && a < 0.35) pick("leave", 0);
            else if (pitchY > 6.0) pick("cut", 95);
            else pick("drive", 45);
        } else if (x < -0.25) pick("flick", -55);
        else if (spin && pitchY < 6 && attack && rng_.Uniform() < 0.3) pick("sweep", -80);
        else if (spin && attack && rng_.Uniform() < 0.25) {
            pick(rng_.Uniform() < 0.6 ? "loft" : "drive", Clamp(10 + 40 * x, -30, 50));  // down the pitch
            b.advance = true;
        }
        else if (attack) {
            if (pitchY < 5.5) pick(rng_.Uniform() < a * (death ? 0.9 : 0.5) ? "loft" : "drive", Clamp(10 + 50 * x, -20, 60));
            else if (z > 0.8 && x < 0.15) pick("pull", -60);
            else if (x > 0.1) pick("drive", Clamp(30 + 50 * x, 10, 70));  // punch off the back foot
            else pick("flick", -35);
        } else if (rng_.Uniform() < (inn.MaxOvers() == 0 ? 0.3 : inn.MaxOvers() <= 20 ? 0.8 : 0.6)) {  // push for a single
            push();
        } else {
            pick("defend", 0);
        }
    }
    if (b.shot == "leave") {
        b.provided = false;
        return b;
    }
    // Execution: the timing of a real player varies with skill and pace.
    const double sigma = (86 - 59 * skill) * (0.8 + 0.4 * (spec.speedKph / 150.0));
    b.timingMs = rng_.Normal(sigma);
    b.direction += rng_.Normal(6);
    return b;
}

// ---------------------------------------------------------------- resolve
// ---------------------------------------------------------------- conditions
Pitch BallOutcome::PitchFor(const Match& m) const {
    Pitch p = conditions.pitch;
    int balls = 0;
    for (const auto& inn : m.AllInnings()) balls += inn.LegalBalls();
    const double overs = balls / 6.0;
    if (m.Rules().oversPerInnings > 0) p.wear = 0.15 * overs / (2.0 * m.Rules().oversPerInnings);
    else p.wear = Clamp(overs / 430.0, 0, 1);  // ~5 days of 86-90 overs
    p.cracks = conditions.pitch.cracks;
    return p;
}

Environment BallOutcome::EnvFor(const Match& m) const {
    Environment e;
    e.wind = conditions.wind;
    if (conditions.dayNight && m.Rules().oversPerInnings > 0 && m.Current().Index() >= 1) {
        // Dew settles as the evening goes on.
        const double overs = m.Current().LegalBalls() / 6.0;
        e.dew = conditions.dewStrength * Clamp((overs + 5.0) / 15.0, 0.3, 1.0);
    }
    return e;
}

// ---------------------------------------------------------------- resolve
BallReport BallOutcome::Resolve(const Match& m, const FieldSetup& field, const BowlChoice& bowl, const BatChoice& bat,
                                QuickSim* perceive) {
    return ResolveSpec(m, field, SpecFor(m, bowl), bat, perceive);
}

namespace {

// Third-umpire evidence from a time margin (+ = the batter was short).
Evidence GroundedFromMargin(double margin) {
    if (margin > 0.02) return Evidence::No;
    if (margin < -0.02) return Evidence::Yes;
    return Evidence::Inconclusive;  // too close to call: benefit of the doubt
}

}  // namespace

BallReport BallOutcome::ResolveSpec(const Match& m, const FieldSetup& field, const DeliverySpec& spec,
                                    const BatChoice& bat, QuickSim* perceive, const DeliveryPath* given,
                                    const FieldingChoice& fc) {
    const Innings& inn = m.Current();
    const Team& battingTeam = m.GetTeam(m.BattingSide());
    const Player& striker = StrikerOf(m);
    const Player& nonStriker = battingTeam.xi.at(static_cast<size_t>(std::max(0, inn.NonStriker())));
    const bool lefty = striker.bats == Handedness::Left;
    const double flip = lefty ? -1.0 : 1.0;
    const bool limited = m.Rules().oversPerInnings > 0;
    const bool freeHit = inn.NextBallIsFreeHit();
    const bool spinner = IsSpinner(m);
    const Pitch pitch = PitchFor(m);
    const Environment env = EnvFor(m);
    BallReport r;
    r.spec = spec;
    r.shot = bat;
    r.path = given ? *given : SimulateDelivery(spec, pitch, env);
    const DeliveryPath& path = r.path;
    if (path.pitched) r.pitchSpot = PitchAt(pitch, path.pitchPoint.x, path.pitchPoint.y);

    ShotInput in;
    in.shot = bat.provided ? ShotFromString(bat.shot).value_or(Shot::Defend) : Shot::Leave;
    in.direction = bat.direction;
    in.timingMs = bat.timingMs;
    in.skill = striker.batting / 100.0 * (0.8 + 0.2 * striker.form / 100.0);
    in.leftHanded = lefty;
    in.advance = bat.advance && in.shot != Shot::Leave;
    r.bat = PlayShot(path, spec, pitch, in, env);

    const FieldMap fm = MapField(m, field, lefty, spinner);
    const Fielder& keeper = fm.fielders[fm.fielders.size() - 2];
    // ---- umpire's calls from the path ----
    const double xc = path.atCrease.x * flip;
    const bool heightNoBall = !path.pitched && path.atCrease.z > 1.0;  // full toss above waist (Law 41.7)
    const bool clearOverstep = spec.overstep > 0.02;                    // the umpire sees it
    const bool marginalOverstep = spec.overstep > 0 && !clearOverstep;  // only a TV replay shows it
    const bool noBall = heightNoBall || clearOverstep;
    const double wideOff = limited ? kWideGuideline : 1.25;
    const bool wideLine = limited ? (xc > wideOff || xc < -(kStumpHalfWidth + kBallRadius + 0.35) ||
                                     path.atCrease.z > 1.8)
                                  : (std::fabs(xc) > wideOff || path.atCrease.z > 2.2);
    // Running: the batters judge the run from the field, the fielders throw
    // to the end that gives the best chance; direct hits; overthrows.
    const double batSkill = (striker.batting + nonStriker.batting) / 200.0;
    const double fit = (striker.fitness + nonStriker.fitness) / 200.0;
    const double paceFactor = 1.10 - 0.20 * fit;
    auto tRun = [&](int n) { return paceFactor * (3.0 + 2.7 * (n - 1)); };
    const double aggressionMargin =
        !limited ? 0.40 : (inn.MaxOvers() * inn.Rules().ballsPerOver - inn.LegalBalls() <= 18 ? 0.10 : 0.25);
    struct Running {
        int runs = 0;
        bool out = false, strikerOut = false, close = false;
        double margin = 0;
        int fielder = -1;
        int overthrows = 0;
        bool overthrowBoundary = false;
    };
    auto run = [&](const FieldResult& fr) {
        Running ru;
        if (fr.boundary || fr.caught || fr.slot < 0) return ru;
        // Batters judge against the fastest the ball could get there (a
        // direct hit), with a misjudgement that shrinks with experience.
        const double arrival = std::min(fr.atStrikerEnd, fr.atBowlerEnd) - 0.4;
        const double estimate = arrival + rng_.Normal(0.42 - 0.22 * batSkill);
        int n = 0;
        while (n < 4 && tRun(n + 1) + aggressionMargin <= estimate) ++n;
        if (n == 0) return ru;
        int end = fc.throwEnd;
        if (end < 0) end = fr.atStrikerEnd <= fr.atBowlerEnd ? 0 : 1;
        double atEnd = end == 0 ? fr.atStrikerEnd : fr.atBowlerEnd;
        double wicketAt = atEnd;
        ru.fielder = fr.fielder;
        // A close one: the fielder shies at the stumps.
        if (std::fabs(atEnd - tRun(n)) < 0.7) {
            const V3 from = fr.gatheredPoint;
            const double dist = Length(V3{from.x, from.y - (end == 0 ? 0.0 : kPitchLength), 0});
            const double hitChance = (0.20 + 0.40 * fr.nearestFielderSkill) * Clamp(1.3 - dist / 40.0, 0.3, 1.0);
            if (rng_.Uniform() < hitChance) {
                wicketAt = atEnd - 0.4;  // direct hit: no gathering at the stumps
            } else {
                const bool backup = end == 0 || rng_.Uniform() < 0.7;  // keeper always backs up
                if (backup) {
                    wicketAt = atEnd + 0.4;
                } else {
                    // Overthrows: the throw runs on past the stumps.
                    wicketAt = 1e9;
                    const double ey = end == 0 ? 0.0 : kPitchLength;
                    V3 dir{0 - from.x, ey - from.y, 0};
                    const double dl = Length(dir);
                    if (dl > 1e-6) {
                        const V3 vel = dir * (fr.throwSpeed * 0.55 / dl);
                        std::vector<Fielder> others;
                        for (size_t k = 0; k < fm.fielders.size(); ++k)
                            if (static_cast<int>(k) != fr.slot) others.push_back(fm.fielders[k]);
                        const FieldResult over = PlayOutField({0, ey, 0.3}, vel, others, ground, env);
                        if (over.boundary) ru.overthrows = 4, ru.overthrowBoundary = true;
                        else ru.overthrows = std::min(over.runs, 2);
                    }
                }
            }
        }
        ru.strikerOut = end == 0 ? (n % 2 == 0) : (n % 2 == 1);  // who is running to that end
        ru.margin = tRun(n) - wicketAt;
        ru.close = std::fabs(ru.margin) < 0.4;
        if (ru.margin > 0) {
            ru.out = true;
            ru.runs = n - 1;
        } else {
            ru.runs = n + ru.overthrows;
        }
        r.runsAttempted = n;
        return ru;
    };
    // Builds the third-umpire case for a run out (out or close).
    auto runOutCase = [&](const Running& ru, Delivery base) {
        r.runOutChance = true;
        r.runOutMargin = ru.margin;
        DecisionCase k;
        k.kind = DecisionKind::UmpireReferral;
        k.mode = Dismissal::RunOut;
        k.evidence.batterGrounded = GroundedFromMargin(ru.margin);
        k.onFieldOut = ru.margin > 0;
        Delivery out = base;
        out.runs = std::max(0, r.runsAttempted - 1);
        out.boundary = false;
        out.dismissal = Dismissal::RunOut;
        out.strikerOut = ru.strikerOut;
        out.fielder = ru.fielder;
        Delivery notOut = base;
        notOut.runs = r.runsAttempted;
        notOut.dismissal = Dismissal::None;
        k.ifOut = out;
        k.ifNotOut = notOut;
        return k;
    };

    Delivery d;
    std::string what;
    const Contact c = r.bat.contact;
    const bool inPlayOffBat = c == Contact::Middled || c == Contact::OutsideEdge || c == Contact::InsideEdge ||
                              c == Contact::TopEdge || c == Contact::BottomEdge;
    if (inPlayOffBat) {
        const bool edge = c != Contact::Middled;
        r.field = PlayOutField(r.bat.ballAtContact, r.bat.exitVelocity, fm.fielders, ground, env, edge, fc.assist);
        what = std::string(ToString(c)) + " (" + Fmt("%.0f km/h", r.bat.exitSpeedKph) + ")";
        if ((c == Contact::InsideEdge || c == Contact::BottomEdge) && PlayedOn(r.field.path)) {
            d.dismissal = Dismissal::Bowled;
            what += ", played on";
        } else if (r.field.caught) {
            d.dismissal = Dismissal::Caught;
            d.fielder = r.field.fielder;
            what += std::string(r.field.boundaryCatch ? ", caught on the boundary by " : ", caught by ") +
                    NameOf(fm, r.field.fielder);
            DecisionCase k;
            k.mode = Dismissal::Caught;
            k.evidence.batContact = Evidence::Yes;
            k.evidence.groundedBeforeCatch = Evidence::No;
            k.ifOut = d;
            k.ifNotOut = Delivery{};
            if (r.field.catchHeight < 0.10 || r.field.boundaryCatch) {
                // Low or boundary catch: the umpires ask the TV umpire.
                k.kind = DecisionKind::UmpireReferral;
                k.evidence.groundedBeforeCatch = r.field.catchHeight < 0.03   ? Evidence::Yes
                                                 : r.field.catchHeight < 0.06 ? Evidence::Inconclusive
                                                                              : Evidence::No;
                k.onFieldOut = true;
                r.appeal = k;
                what += r.field.catchHeight < 0.10 ? Fmt(" (low: %.0f cm off the grass)", r.field.catchHeight * 100) : "";
            } else if (edge && c != Contact::TopEdge) {
                // A thin edge to the keeper or slips: the umpire may not hear it.
                k.kind = DecisionKind::PlayerAppeal;
                k.onFieldOut = r.bat.edgeFraction <= 0.85;
                r.appeal = k;
            }
        } else if (r.field.boundary) {
            d.runs = r.field.runs;
            d.boundary = true;
            if (r.field.steppedOnRope) what += ", caught but the fielder stepped on the rope";
            what += d.runs == 6 ? " - SIX" : " - FOUR";
            what += Fmt(" (%.0f m)", r.field.distance);
            // Did it clear the rope on the full? Close ones go to the TV umpire.
            if (d.runs == 6 && !r.field.path.empty() && r.field.path.back().p.z < 0.6) {
                DecisionCase k;
                k.kind = DecisionKind::UmpireReferral;
                k.call = ScoringCall::Boundary;
                k.onFieldCalled = r.field.path.back().p.z > 0.15;
                k.evidence.callTrue = Evidence::Yes;
                k.ifCalled = d;
                Delivery four = d;
                four.runs = 4;
                k.ifNotCalled = four;
                r.appeal = k;
            }
        } else {
            if (r.field.dropped) what += ", dropped by " + NameOf(fm, r.field.fielder);
            const Running ru = run(r.field);
            if (r.field.savedAtBoundary) what += ", saved on the boundary by " + NameOf(fm, r.field.fielder);
            if (ru.out || ru.close) {
                r.appeal = runOutCase(ru, d);
                what += ru.out ? ", RUN OUT attempt" : ", close run-out call";
            } else {
                d.runs = ru.runs;
                r.overthrows = ru.overthrows;
                what += Fmt(", %.0f run(s)", ru.runs);
                if (ru.overthrows) what += Fmt(" (incl. %.0f overthrows)", ru.overthrows);
            }
            what += Fmt(" (%.0f m)", r.field.distance);
        }
    } else if (c == Contact::Pad) {
        // Ball tracking from the pad impact: LBW evidence (Law 36).
        LbwEvidence e;
        const double hw = kStumpHalfWidth, rad = kBallRadius;
        if (!path.pitched) e.pitched = PitchZone::Full;
        else {
            const double px = path.pitchPoint.x * flip;
            e.pitched = px < -(hw + rad) ? PitchZone::OutsideLeg : px > hw + rad ? PitchZone::OutsideOff : PitchZone::InLine;
        }
        const double ix = r.bat.padImpact.x * flip;
        e.impact = std::fabs(ix) <= hw + rad ? ImpactZone::InLine : ix > 0 ? ImpactZone::OutsideOff : ImpactZone::OutsideLeg;
        e.impactUmpiresCall = std::fabs(ix) > hw - rad && std::fabs(ix) <= hw + rad;
        e.shotOffered = in.shot != Shot::Leave;
        e.hitBatFirst = false;
        DeliveryPath tracked;
        tracked.samples = r.bat.trackedToStumps;
        e.wickets = WicketsZone::Missing;
        if (auto s = CrossingAt(tracked, 0.0)) {
            const double sx = std::fabs(s->p.x), sz = s->p.z;
            if (sx <= hw && sz <= kStumpHeight) e.wickets = WicketsZone::Hitting;
            else if (sx <= hw + rad && sz - rad <= kStumpHeight) e.wickets = WicketsZone::UmpiresCall;
        }
        what = "hit the pad";
        // Leg byes only if a shot was offered (or the batter tried to get out
        // of the way) - Law 23. The pad deflects the ball, slowly.
        Delivery legBye;
        if (e.shotOffered) {
            const V3 vin = r.bat.velocityAtPad;
            // Off the pads the ball usually glances away behind square on the leg side.
            const double sp = Length(vin);
            const V3 out{-flip * sp * 0.30 + vin.x * 0.2, vin.y * 0.35, 0.3};
            r.field = PlayOutField(r.bat.padImpact, out, fm.fielders, ground, env, false, fc.assist);
            if (r.field.boundary) {
                legBye.extra = ExtraType::LegBye;
                legBye.runs = 4;
                legBye.boundary = true;
            } else {
                const Running ru = run(r.field);
                if (!ru.out && ru.runs > 0) legBye.extra = ExtraType::LegBye, legBye.runs = ru.runs;
            }
        }
        d = legBye;
        if (!freeHit && !noBall && e.wickets != WicketsZone::Missing) {
            DecisionCase k;
            k.kind = DecisionKind::PlayerAppeal;
            k.mode = Dismissal::Lbw;
            k.evidence.lbw = e;
            k.onFieldOut = e.wickets == WicketsZone::Hitting && LbwIsOut(e);
            k.ifOut.dismissal = Dismissal::Lbw;
            k.ifNotOut = legBye;
            r.appeal = k;
            what += ", big LBW appeal";
        }
        if (legBye.runs > 0) what += legBye.boundary ? ", 4 leg byes" : Fmt(", %.0f leg bye(s)", legBye.runs);
    } else if (c == Contact::Bowled) {
        what = "beaten";
        if (!freeHit && !noBall) {
            d.dismissal = Dismissal::Bowled;
            what += ", BOWLED";
        }
    } else {
        // Missed: through to the keeper. Byes if they can't take it cleanly;
        // stumped if the batter is out of the crease and the keeper is up.
        auto at = CrossingAt(path, 0.0);
        what = in.shot == Shot::Leave ? "left alone" : "beaten";
        if (wideLine) d.extra = ExtraType::Wide;
        if (at) {
            r.field = PlayOutField(at->p, at->v, fm.fielders, ground, env, false, fc.assist);
            const bool collected = r.field.caught && r.field.slot == static_cast<int>(fm.fielders.size() - 2);
            if (r.bat.regainGroundS > 0 && spinner && collected && !freeHit && !noBall) {
                // Stumping: keeper gathers and breaks the wicket vs batter scrambling back.
                const auto pastBat = CrossingAt(path, r.bat.contactY);
                const double regain = (pastBat ? pastBat->t : at->t) + r.bat.regainGroundS;
                const double broken = at->t + r.field.path.back().t + 0.25 + (1.0 - keeper.skill) * 0.2;
                const double margin = regain - broken;
                if (margin > -0.3) {
                    r.stumpingChance = true;
                    DecisionCase k;
                    k.kind = DecisionKind::UmpireReferral;
                    k.mode = Dismissal::Stumped;
                    k.evidence.batterGrounded = GroundedFromMargin(margin);
                    k.onFieldOut = margin > 0;
                    Delivery out = d;
                    out.dismissal = Dismissal::Stumped;
                    out.fielder = keeper.index;
                    k.ifOut = out;
                    k.ifNotOut = d;
                    r.appeal = k;
                    what += margin > 0 ? ", STUMPED attempt" : ", stumping chance";
                }
            } else if (!collected) {
                const Running ru = run(r.field);
                if (ru.out || ru.close) {
                    Delivery base = d;
                    if (base.extra == ExtraType::None) base.extra = ExtraType::Bye;
                    r.appeal = runOutCase(ru, base);
                    what += ", run-out chance";
                } else if (ru.runs > 0) {
                    d.runs = ru.runs;
                    if (d.extra == ExtraType::None) d.extra = ExtraType::Bye;
                    what += Fmt(", %.0f bye(s)", ru.runs);
                }
            }
            if (r.field.boundary && !collected) {
                d.runs = 4;
                d.boundary = true;
                if (d.extra == ExtraType::None) d.extra = ExtraType::Bye;
                what += ", 4 byes";
            }
        }
        if (wideLine) what += ", WIDE";
    }
    // ---- no-balls ----
    const bool wicketFell = d.dismissal != Dismissal::None ||
                            (r.appeal && r.appeal->call == ScoringCall::None && r.appeal->onFieldOut);
    r.frontFootNoBall = clearOverstep || (marginalOverstep && wicketFell);
    if (noBall || (marginalOverstep && wicketFell)) {
        // Only runs completed stand (byes from a missed ball count as no-ball byes).
        d.extra = ExtraType::NoBall;
        d.noBallRunsAs = c == Contact::Missed ? ExtraType::Bye : c == Contact::Pad ? ExtraType::LegBye : ExtraType::None;
        if (d.dismissal != Dismissal::None && d.dismissal != Dismissal::RunOut) d.dismissal = Dismissal::None, d.fielder = -1;
        if (r.appeal && r.appeal->mode != Dismissal::RunOut) r.appeal.reset();
        if (r.appeal) {  // a run out off a no-ball still counts
            r.appeal->ifOut.extra = ExtraType::NoBall;
            r.appeal->ifNotOut.extra = ExtraType::NoBall;
            r.appeal->ifOut.noBallRunsAs = r.appeal->ifNotOut.noBallRunsAs = d.noBallRunsAs;
        }
        what += heightNoBall ? ", NO-BALL (above waist)"
                             : (clearOverstep ? ", NO-BALL (front foot)" : ", NO-BALL (front foot, on TV replay)");
    }
    if (freeHit && d.dismissal != Dismissal::None && d.dismissal != Dismissal::RunOut) {
        d.dismissal = Dismissal::None;
        d.fielder = -1;
        what += " (free hit: not out)";
        if (r.appeal && r.appeal->mode != Dismissal::RunOut) r.appeal.reset();
    }
    if (r.appeal) {
        r.appeal->evidence.frontFootNoBall = false;  // handled above from the real front-foot position
        if (r.appeal->call == ScoringCall::None && r.appeal->mode == Dismissal::Caught) r.appeal->ifOut = d;
        if (perceive) perceive->PerceiveCase(*r.appeal, m.BattingSide());
    }
    // Close wide calls can be reviewed where the competition allows it.
    if (!r.appeal && d.extra == ExtraType::Wide && m.Rules().drs.reviewWidesAndNoBalls &&
        std::fabs(xc - wideOff) < 0.05) {
        DecisionCase k;
        k.kind = DecisionKind::PlayerAppeal;
        k.call = ScoringCall::Wide;
        k.onFieldCalled = true;
        k.evidence.callTrue = xc > wideOff ? Evidence::Yes : Evidence::No;
        k.ifCalled = d;
        Delivery legal = d;
        legal.extra = d.runs > 0 ? ExtraType::Bye : ExtraType::None;
        k.ifNotCalled = legal;
        if (perceive) perceive->PerceiveCase(k, m.BattingSide());
        r.appeal = k;
    }
    r.delivery = d;  // when there is an appeal, the case decides instead
    // Summary for the players.
    std::string s = Fmt("%.0f km/h", spec.speedKph);
    if (path.pitched) s += Fmt(", pitched %.1f m", path.pitchPoint.y);
    else s += ", full toss";
    if (r.pitchSpot.inRough && pitch.wear > 0.2) s += " (in the rough)";
    const double swing = path.swingBeforePitch * flip;
    if (std::fabs(swing) >= 0.03) s += Fmt(swing > 0 ? ", swung away %.0f cm" : ", swung in %.0f cm", std::fabs(swing) * 100);
    const double dev = path.deviationOffPitch * flip;
    if (std::fabs(dev) >= 0.02)
        s += Fmt(dev > 0 ? (spec.spinRps > 12 ? ", turned away %.0f cm" : ", seamed away %.0f cm")
                         : (spec.spinRps > 12 ? ", turned in %.0f cm" : ", seamed in %.0f cm"),
                 std::fabs(dev) * 100);
    if (bat.provided) s += std::string(" - ") + (in.advance ? "down the pitch, " : "") + ToString(in.shot) +
                           Fmt(" (%+.0f ms)", bat.timingMs);
    r.summary = s + ": " + what;
    return r;
}

BallReport BallOutcome::PlayComputerBall(MatchSession& session, QuickSim* perceive) {
    const Match& m = session.GetMatch();
    const DeliverySpec spec = SpecFor(m, AiBowl(m));
    const DeliveryPath path = SimulateDelivery(spec, PitchFor(m), EnvFor(m));
    const BatChoice bat = AiShot(m, path, spec);
    BallReport r = ResolveSpec(m, session.Field(m.FieldingSide()), spec, bat, perceive, &path);
    if (!r.appeal || !session.ResolveAppeal(*r.appeal)) session.ResolveDelivery(r.delivery);
    return r;
}

}  // namespace cricket
