#include "cricket/BallPhysics.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cricket::phys {

namespace {

constexpr double kDt = 0.002;                   // integration step (s)
constexpr double kArea = kPi * kBallRadius * kBallRadius;
constexpr double kCd = 0.45;                    // drag coefficient (Mehta: ~0.4-0.5 for a cricket ball)
constexpr double kInertiaFactor = 0.4;          // I = k m r^2 (solid sphere)
constexpr double kRollRatio = kInertiaFactor / (1.0 + kInertiaFactor);  // 2/7: slip removed when rolling
constexpr double kPitchCentreY = kPitchLength / 2;

double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
double Abs(double v) { return v < 0 ? -v : v; }
double Sq(double v) { return v * v; }
double Lerp(double a, double b, double t) { return a + (b - a) * t; }

// Piecewise-linear interpolation through (x, y) points (x increasing).
double Piecewise(double x, const double* xs, const double* ys, int n) {
    if (x <= xs[0]) return ys[0];
    for (int i = 1; i < n; ++i)
        if (x <= xs[i]) return Lerp(ys[i - 1], ys[i], (x - xs[i - 1]) / (xs[i] - xs[i - 1]));
    return ys[n - 1];
}

struct Forces {
    const DeliverySpec* spec;
    const Environment* env;
    bool swing = true;    // swing + Magnus on
    double cd = kCd;
};

V3 Accel(const Forces& f, V3 vGround, V3 w) {
    // Air forces depend on the ball's speed through the air (wind).
    const V3 v = vGround - f.env->wind;
    const double s = Length(v);
    V3 a{0, 0, -f.env->gravity};
    if (s < 1e-9) return a;
    const double q = 0.5 * f.env->airDensity * kArea * s / kBallMass;  // per unit velocity
    a = a + v * (-q * f.cd);
    if (!f.swing) return a;
    // Swing: sideways force perpendicular to the flight, in the horizontal
    // plane. A wet (dewy) ball swings less.
    const double cs = SwingCoefficient(s, f.spec->seamAngle, f.spec->seamWobble, f.spec->shine) *
                      (1.0 - 0.5 * Clamp(f.env->dew, 0, 1));
    const double hx = v.x, hy = v.y;
    const double hl = Length({hx, hy, 0});
    if (hl > 1e-9 && cs != 0) {
        const V3 lateral{-hy / hl, hx / hl, 0};  // ball moving -y -> +x
        a = a + lateral * (q * s * cs);
    }
    // Magnus lift from spin: along (w x v), coefficient from the spin parameter.
    const double wl = Length(w);
    if (wl > 1e-9) {
        const double spinParam = kBallRadius * wl / s;
        const double cl = std::min(0.25, 0.6 * spinParam);
        V3 dir = Cross(w, v);
        const double dl = Length(dir);
        if (dl > 1e-12) a = a + dir * (q * s * cl / dl);
    }
    return a;
}

// One RK4 step of position + velocity (spin is constant in flight).
void Step(const Forces& f, V3& p, V3& v, V3 w, double dt) {
    const V3 a1 = Accel(f, v, w);
    const V3 v2 = v + a1 * (dt / 2);
    const V3 a2 = Accel(f, v2, w);
    const V3 v3 = v + a2 * (dt / 2);
    const V3 a3 = Accel(f, v3, w);
    const V3 v4 = v + a3 * dt;
    const V3 a4 = Accel(f, v4, w);
    p = p + (v + v2 * 2 + v3 * 2 + v4) * (dt / 6);
    v = v + (a1 + a2 * 2 + a3 * 2 + a4) * (dt / 6);
}

// Rigid sphere bouncing on the pitch with friction (R. Cross).
void Bounce(V3 at, V3& v, V3& w, const Pitch& surface, const DeliverySpec& spec, const Environment& env) {
    const PitchSpot pitch = [&] {
        PitchSpot s = PitchAt(surface, at.x, at.y);
        s.friction *= 1.0 - 0.35 * Clamp(env.dew, 0, 1);  // a wet ball skids on
        return s;
    }();
    const double vz = v.z;  // negative
    v.z = -pitch.bounce * vz;
    // Velocity of the contact point (bottom of the ball) along the ground.
    const V3 vc{v.x - kBallRadius * w.y, v.y + kBallRadius * w.x, 0};
    const double vcl = Length(vc);
    if (vcl > 1e-9) {
        const double maxImpulse = pitch.friction * (1.0 + pitch.bounce) * Abs(vz);  // per unit mass
        const double frac = std::min(maxImpulse / vcl, kRollRatio);
        const V3 dv = vc * (-frac);
        v.x += dv.x;
        v.y += dv.y;
        // Angular impulse from the friction force at the contact point.
        const double k = 1.0 / (kInertiaFactor * kBallRadius);
        w.x += k * dv.y;
        w.y += -k * dv.x;
    }
    // The seam biting the surface (more on green pitches, less if the seam
    // is wobbling): a small sideways kick.
    const double horiz = Length({v.x, v.y, 0});
    v.x += spec.seamNip * pitch.seam * (1.0 - spec.seamWobble) * 0.045 * horiz;
}

DeliverySpec NoSwing(DeliverySpec s) {
    s.seamAngle = 0;
    s.seamNip = 0;
    s.spinRps = 0;
    return s;
}

V3 SpinVector(const DeliverySpec& s) {
    if (s.spinRps <= 0) return {};
    V3 axis{s.spinTop, s.spinTurn, 0};
    const double l = Length(axis);
    if (l < 1e-9) return {};
    return axis * (2.0 * kPi * s.spinRps / l);
}

V3 ReleaseVelocity(double speed, double elevation, double azimuth) {
    const double ce = DetCos(elevation);
    return {speed * DetSin(azimuth) * ce, -speed * DetCos(azimuth) * ce, speed * DetSin(elevation)};
}

// Where the delivery first reaches the ground (for aiming), integrating far
// beyond the stumps if needed (a full toss "pitches" behind the batter).
V3 FirstPitch(const DeliverySpec& s, const Pitch& pitch, const Environment& env, V3 v0) {
    // Integrate to the first ground contact only (no bounce needed for aiming).
    const auto path = Integrate({s.releaseX, s.releaseY, s.releaseHeight}, v0, SpinVector(s), s, pitch, env, -40.0, 4.0,
                                false);
    if (path.size() < 2 || path.back().p.z >= kBallRadius) return {s.releaseX, -40.0, 0};
    const Sample& a = path[path.size() - 2];
    const Sample& b = path.back();
    const double f = Clamp((a.p.z - kBallRadius) / (a.p.z - b.p.z), 0, 1);
    return a.p + (b.p - a.p) * f;
}

// Smooth deterministic noise in [-1, 1] over the pitch (integer hash of a
// 0.25 m grid, bilinear blend). Same pitch seed = same cracks.
double HashUnit(uint64_t seed, int64_t ix, int64_t iy) {
    uint64_t h = seed ^ (static_cast<uint64_t>(ix) * 0x9E3779B97F4A7C15ULL) ^ (static_cast<uint64_t>(iy) * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return static_cast<double>(h >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}
double PitchNoise(uint64_t seed, double x, double y) {
    const double gx = x / 0.25 + 1000, gy = y / 0.25 + 1000;  // keep positive
    const int64_t ix = static_cast<int64_t>(gx), iy = static_cast<int64_t>(gy);
    const double fx = gx - static_cast<double>(ix), fy = gy - static_cast<double>(iy);
    const double a = HashUnit(seed, ix, iy), b = HashUnit(seed, ix + 1, iy);
    const double c = HashUnit(seed, ix, iy + 1), d = HashUnit(seed, ix + 1, iy + 1);
    return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fy);
}

}  // namespace

PitchSpot PitchAt(const Pitch& p, double x, double y) {
    const double wear = Clamp(p.wear, 0, 1);
    PitchSpot s{p.bounce - 0.06 * wear, p.friction + 0.16 * wear, std::max(0.0, p.seam - 0.25 * wear), false};
    // Cracks open up as the pitch wears: variable bounce.
    s.bounce += 0.05 * wear * PitchNoise(p.cracks, x, y);
    // Bowlers' footmarks (rough) outside both sets of stumps, 1-3.5 m in
    // front of the batter: much more grip and up-and-down bounce.
    const double ax = Abs(x);
    if (wear > 0 && ax > 0.25 && ax < 0.85 && y > 1.0 && y < 3.6) {
        s.inRough = true;
        s.friction += 0.22 * wear;
        s.bounce += 0.09 * wear * PitchNoise(p.cracks ^ 0xABCDEF, x * 2, y * 2);
    }
    s.bounce = Clamp(s.bounce, 0.35, 0.80);
    return s;
}

Pitch Pitch::ByName(const std::string& n) {
    if (n == "green") return Green();
    if (n == "dusty") return Dusty();
    if (n == "bouncy") return Bouncy();
    if (n == "flat") return Flat();
    return Standard();
}

// ---------------------------------------------------------------- maths
double Length(V3 a) {
    const double s = Dot(a, a);
    return s > 0 ? std::sqrt(s) : 0.0;  // IEEE: exactly rounded everywhere
}

double DetSin(double x) {
    // Reduce to [-pi, pi] then to [-pi/2, pi/2]; Taylor series to x^23 (error < 1e-16).
    const double twoPi = 2 * kPi;
    while (x > kPi) x -= twoPi;
    while (x < -kPi) x += twoPi;
    if (x > kPi / 2) x = kPi - x;
    if (x < -kPi / 2) x = -kPi - x;
    const double x2 = x * x;
    double term = x, sum = x;
    for (int n = 1; n <= 11; ++n) {
        term *= -x2 / ((2.0 * n) * (2.0 * n + 1));
        sum += term;
    }
    return sum;
}

double DetCos(double x) { return DetSin(x + kPi / 2); }

// ---------------------------------------------------------------- aerodynamics
double SwingCoefficient(double speed, double seamAngleDeg, double wobble, double shine) {
    // Seam angle effect: grows to a peak near 20 degrees (Mehta), then fades.
    const double a = Abs(seamAngleDeg);
    static const double ax[] = {0, 20, 40, 70};
    static const double ay[] = {0, 1, 0.6, 0};
    const double seamFactor = Piecewise(a, ax, ay, 4) * (seamAngleDeg < 0 ? -1.0 : 1.0);
    // Conventional swing vs speed (m/s): appears ~15, peaks ~30-33, gone by ~38.
    // A smooth new ball keeps conventional swing to higher speeds.
    const double cEnd = 36.0 + 5.0 * Clamp(shine, 0, 1);
    const double cx[] = {15, 30, cEnd - 4, cEnd};
    static const double cy[] = {0, 1, 1, 0};
    const double conventional = Piecewise(speed, cx, cy, 4);
    // Reverse swing with a rough ball: above ~35 m/s the ball moves the other way.
    static const double rx[] = {33, 37, 42};
    static const double ry[] = {0, 0.8, 1.0};
    const double reverse = Piecewise(speed, rx, ry, 3) * Clamp(1.0 - shine * 1.6, 0, 1);
    const double peak = 0.18 + 0.06 * shine;  // a shiny new ball swings most
    const double stable = Clamp(1.0 - wobble, 0, 1);
    return peak * stable * seamFactor * (conventional - reverse);
}

// ---------------------------------------------------------------- flight
std::vector<Sample> Integrate(V3 p, V3 v, V3 w, const DeliverySpec& spec, const Pitch& pitch, const Environment& env,
                              double untilY, double maxT, bool allowBounce, bool* bounced, Sample* bounce) {
    Forces f{&spec, &env};
    std::vector<Sample> out;
    out.push_back({0, p, v, w});
    double t = 0;
    bool didBounce = false;
    while (p.y > untilY && t < maxT) {
        V3 np = p, nv = v;
        Step(f, np, nv, w, kDt);
        if (np.z < kBallRadius && nv.z < 0) {
            if (!allowBounce) {
                p = np, v = nv, t += kDt;
                out.push_back({t, p, v, w});
                break;
            }
            // Land exactly on the ground: the fraction of the step before contact.
            const double frac = Clamp((p.z - kBallRadius) / (p.z - np.z), 0, 1);
            V3 cp = p + (np - p) * frac;
            V3 cv = v + (nv - v) * frac;
            cp.z = kBallRadius;
            const double ct = t + kDt * frac;
            if (!didBounce && bounced) {
                *bounced = true;
                if (bounce) *bounce = {ct, cp, cv, w};
            }
            didBounce = true;
            Bounce(cp, cv, w, pitch, spec, env);
            // Finish the step after the bounce.
            const double rest = kDt * (1 - frac);
            if (rest > 0) Step(f, cp, cv, w, rest);
            if (cp.z < kBallRadius) cp.z = kBallRadius;
            p = cp, v = cv, t += kDt;
        } else {
            p = np, v = nv, t += kDt;
        }
        out.push_back({t, p, v, w});
    }
    return out;
}

std::optional<Sample> CrossingAt(const DeliveryPath& d, double planeY) {
    const auto& s = d.samples;
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i - 1].p.y >= planeY && s[i].p.y < planeY) {
            const double f = (s[i - 1].p.y - planeY) / (s[i - 1].p.y - s[i].p.y);
            return Sample{Lerp(s[i - 1].t, s[i].t, f), s[i - 1].p + (s[i].p - s[i - 1].p) * f,
                          s[i - 1].v + (s[i].v - s[i - 1].v) * f, s[i].w};
        }
    }
    return std::nullopt;
}

DeliveryPath SimulateDelivery(const DeliverySpec& spec, const Pitch& pitch, const Environment& env) {
    const double speed = spec.speedKph / 3.6;
    const V3 release{spec.releaseX, spec.releaseY, spec.releaseHeight};
    // Aim like a bowler: find the release angles that land the ball on the
    // target spot. Fuller length = higher release angle (monotonic), so a
    // bisection finds it; the line is then corrected for the swing. Fixed
    // iteration counts keep it deterministic.
    const double dy = spec.releaseY - spec.targetLength;
    double azimuth = (spec.targetLine - spec.releaseX) / (dy > 1 ? dy : 1);
    double elevation = 0;
    for (int round = 0; round < 3; ++round) {
        // Bracketed secant (Illinois method) on the elevation; fixed limits.
        auto err = [&](double el) { return FirstPitch(spec, pitch, env, ReleaseVelocity(speed, el, azimuth)).y - spec.targetLength; };
        double lo = Deg(-30), hi = Deg(15);
        double flo = err(lo), fhi = err(hi);  // flo > 0 (too short), fhi < 0 (too full)
        int side = 0;
        elevation = (lo + hi) / 2;
        for (int i = 0; i < 30; ++i) {
            elevation = (flo == fhi) ? (lo + hi) / 2 : (lo * fhi - hi * flo) / (fhi - flo);
            const double fm = err(elevation);
            if (Abs(fm) < 1e-4) break;
            if ((fm > 0) == (flo > 0)) {
                lo = elevation, flo = fm;
                if (side == -1) fhi /= 2;
                side = -1;
            } else {
                hi = elevation, fhi = fm;
                if (side == 1) flo /= 2;
                side = 1;
            }
        }
        const V3 pp = FirstPitch(spec, pitch, env, ReleaseVelocity(speed, elevation, azimuth));
        const double dist = spec.releaseY - pp.y;
        if (dist > 1) azimuth += (spec.targetLine - pp.x) / dist;
    }
    DeliveryPath d;
    d.releaseVelocity = ReleaseVelocity(speed, elevation, azimuth);
    d.releaseSpeedKph = spec.speedKph;
    bool bounced = false;
    Sample b;
    d.samples = Integrate(release, d.releaseVelocity, SpinVector(spec), spec, pitch, env, -3.0, 4.0, true, &bounced, &b);
    // Only a bounce on the batter's side of the stumps counts as "pitched".
    d.pitched = bounced && b.p.y > 0;
    if (d.pitched) {
        d.pitchPoint = b.p;
        d.pitchTime = b.t;
        d.speedAtBounceKph = Length(b.v) * 3.6;
        // Swing in the air: compare with the same release without swing/spin.
        const DeliverySpec plain = NoSwing(spec);
        bool pb = false;
        Sample ps;
        Integrate(release, d.releaseVelocity, {}, plain, pitch, env, -3.0, 4.0, true, &pb, &ps);
        if (pb) d.swingBeforePitch = b.p.x - ps.p.x;
        // Movement off the pitch: actual x 2 m after the bounce vs the line it was on.
        DeliveryPath tmp;
        tmp.samples = d.samples;
        if (auto after = CrossingAt(tmp, b.p.y - 2.0)) {
            const double slope = b.v.y != 0 ? b.v.x / b.v.y : 0;
            const double straightX = b.p.x + slope * (-2.0);
            d.deviationOffPitch = after->p.x - straightX;
            d.speedAfterBounceKph = Length(after->v) * 3.6;
        }
    }
    if (auto s = CrossingAt(d, 0.0)) {
        d.atStumps = s->p;
        d.timeAtStumps = s->t;
        d.wouldHitStumps = Abs(s->p.x) <= kStumpHalfWidth + kBallRadius && s->p.z - kBallRadius <= kStumpHeight;
    }
    if (auto c = CrossingAt(d, kPoppingCrease)) {
        d.atCrease = c->p;
        d.timeAtCrease = c->t;
    }
    return d;
}

// ---------------------------------------------------------------- batting
const char* ToString(Shot s) {
    switch (s) {
        case Shot::Leave: return "leave";
        case Shot::Defend: return "defend";
        case Shot::Drive: return "drive";
        case Shot::Loft: return "loft";
        case Shot::Cut: return "cut";
        case Shot::Pull: return "pull";
        case Shot::Flick: return "flick";
        case Shot::Sweep: return "sweep";
    }
    return "?";
}

std::optional<Shot> ShotFromString(const std::string& s) {
    for (Shot x : {Shot::Leave, Shot::Defend, Shot::Drive, Shot::Loft, Shot::Cut, Shot::Pull, Shot::Flick, Shot::Sweep})
        if (s == ToString(x)) return x;
    return std::nullopt;
}

const char* ToString(Contact c) {
    switch (c) {
        case Contact::Missed: return "missed";
        case Contact::Middled: return "middled";
        case Contact::OutsideEdge: return "outside edge";
        case Contact::InsideEdge: return "inside edge";
        case Contact::TopEdge: return "top edge";
        case Contact::BottomEdge: return "bottom edge";
        case Contact::Pad: return "hit the pad";
        case Contact::Bowled: return "bowled";
    }
    return "?";
}

namespace {

struct ShotShape {
    bool frontFoot;
    bool horizontalBat;   // pull / cut / sweep: the blade is across the line
    double xMin, xMax;    // where the bat can reach at contact (right-hander)
    double zMin, zMax;
    double batSpeed;      // m/s at the hitting zone
    double elevation;     // launch angle of the bat face (degrees)
    double dirMin, dirMax;  // directions this shot can send the ball
};

ShotShape ShapeOf(Shot s) {
    switch (s) {
        case Shot::Defend: return {true, false, -0.35, 0.45, 0.0, 1.25, 1.5, -10, -75, 75};
        case Shot::Drive: return {true, false, -0.25, 0.90, 0.0, 0.95, 21, -2, -35, 70};
        case Shot::Loft: return {true, false, -0.30, 0.80, 0.0, 1.10, 28.5, 25, -60, 60};
        case Shot::Cut: return {false, true, 0.25, 1.40, 0.35, 1.30, 19, 2, 50, 125};
        case Shot::Pull: return {false, true, -0.60, 0.55, 0.75, 1.75, 22, 14, -125, -25};
        case Shot::Flick: return {true, false, -0.75, 0.20, 0.0, 0.95, 16, 1, -100, -10};
        case Shot::Sweep: return {true, true, -0.45, 0.80, 0.0, 0.65, 16, 12, -125, -40};
        case Shot::Leave: break;
    }
    return {false, false, 0, 0, 0, 0, 0, 0, 0, 0};
}

std::optional<Sample> At(const std::vector<Sample>& s, double planeY) {
    DeliveryPath d;
    d.samples = s;
    return CrossingAt(d, planeY);
}

// Pad check: does the ball cross the pad plane inside the pads?
std::optional<Sample> PadHit(const DeliveryPath& path, double padY, double padX) {
    auto at = CrossingAt(path, padY);
    if (!at) return std::nullopt;
    // Both legs (front and back pad) and the thigh: about 40 cm wide, up to ~1.1 m.
    if (Abs(at->p.x - padX) <= 0.20 + kBallRadius && at->p.z - kBallRadius <= 1.10) return at;
    return std::nullopt;
}

V3 MirrorX(V3 v) { return {-v.x, v.y, v.z}; }

}  // namespace

BatResult PlayShot(const DeliveryPath& pathIn, const DeliverySpec& specIn, const Pitch& pitch, const ShotInput& in,
                   const Environment& env) {
    // Work in right-hander coordinates; mirror a left-hander's delivery.
    DeliveryPath path = pathIn;
    DeliverySpec spec = specIn;
    if (in.leftHanded) {
        for (auto& s : path.samples) s.p = MirrorX(s.p), s.v = MirrorX(s.v);
        path.atStumps = MirrorX(path.atStumps);
        path.pitchPoint = MirrorX(path.pitchPoint);
        spec.seamAngle = -spec.seamAngle;
        spec.seamNip = -spec.seamNip;
        spec.spinTurn = -spec.spinTurn;
    }
    BatResult r;
    auto finish = [&]() {
        if (in.leftHanded) {
            r.ballAtContact = MirrorX(r.ballAtContact);
            r.exitVelocity = MirrorX(r.exitVelocity);
            r.padImpact = MirrorX(r.padImpact);
            for (auto& s : r.trackedToStumps) s.p = MirrorX(s.p), s.v = MirrorX(s.v);
        }
        return r;
    };
    const double skill = Clamp(in.skill, 0, 1);
    const bool fullish = !path.pitched || path.pitchPoint.y < 5.5;
    // Missed the ball (or no shot): the pads, then the stumps.
    auto missed = [&](double padY, double padX) {
        if (auto pad = PadHit(path, padY, padX)) {
            r.contact = Contact::Pad;
            r.padImpact = pad->p;
            r.velocityAtPad = pad->v;
            for (const auto& s : path.samples)
                if (s.p.y <= pad->p.y) r.trackedToStumps.push_back(s);
            return finish();
        }
        r.contact = path.wouldHitStumps ? Contact::Bowled : Contact::Missed;
        return finish();
    };
    if (in.shot == Shot::Leave) return missed(0.85, -0.15);

    ShotShape shape = ShapeOf(in.shot);
    if (in.shot == Shot::Defend && !fullish) {  // back-foot defence to a shorter ball
        shape.frontFoot = false;
        shape.zMin = 0.2, shape.zMax = 1.5;
    }
    double contactY = 0.75;
    if (shape.frontFoot) {
        contactY = path.pitched ? Clamp(path.pitchPoint.y - 0.35, 1.6, 2.5) : 1.9;
        if (in.shot == Shot::Sweep) contactY = 2.4;
        if (in.advance) contactY = path.pitched ? Clamp(path.pitchPoint.y - 0.10, 2.6, 4.5) : 3.0;  // down the pitch
        // Out of the crease: dancing down, or a long stretch forward.
        r.regainGroundS = in.advance ? 1.3 : (contactY >= 2.35 ? 0.35 : 0.0);
    }
    auto at = CrossingAt(path, contactY);
    if (!at) return missed(0.85, -0.15);
    r.contactY = contactY;
    r.ballAtContact = at->p;
    // The batter's read: from the ball's state `readLead` seconds earlier,
    // assuming a normal bounce and no swing, seam or spin. Better batters
    // commit later, so late movement fools them less.
    const double readLead = 0.30 - 0.12 * skill;
    const double readT = std::max(0.0, at->t - readLead);
    size_t ri = 0;
    while (ri + 1 < path.samples.size() && path.samples[ri + 1].t <= readT) ++ri;
    const Sample& rs = path.samples[ri];
    V3 predicted = at->p;
    if (rs.t < at->t) {
        DeliverySpec plain = NoSwing(spec);
        Pitch expected = pitch;
        expected.seam = 0;
        // Before the bounce the batter can't see the bowler's spin; after it,
        // they see how the ball is actually coming off the pitch.
        const bool afterBounce = path.pitched && rs.t >= path.pitchTime;
        auto pred = Integrate(rs.p, rs.v, afterBounce ? rs.w : V3{}, plain, expected, env, contactY - 0.01, 3.0, true);
        if (auto pp = At(pred, contactY)) predicted = pp->p;
    }
    // Good batters also read the bowler's hand and the seam / revolutions,
    // so they anticipate part of the movement they can't see yet.
    // Spin is read partly from the hand and the flight (slower ball, more time).
    const double anticipation = std::min(0.95, 0.35 + 0.45 * skill + (spec.spinRps > 12 ? 0.05 : 0.0));
    predicted = predicted + (at->p - predicted) * anticipation;
    r.readError = Length(V3{at->p.x - predicted.x, 0, at->p.z - predicted.z});
    // Where the bat goes: the read, limited to what this shot can reach.
    const V3 bat{Clamp(predicted.x, shape.xMin, shape.xMax), contactY, Clamp(predicted.z, shape.zMin, shape.zMax)};
    // Pads for a missed shot: the front pad follows the line of the ball.
    const double padY = shape.frontFoot ? contactY - 0.30 : 0.55;
    // The front pad is inside the line of the bat (leg side); the back leg is behind.
    const double padX = shape.frontFoot ? Clamp(predicted.x - 0.26, -0.45, 0.35) : Clamp(predicted.x - 0.15, -0.30, 0.30);
    // A defensive block (little backlift, soft hands, bat close to the pad)
    // is much less sensitive to timing than a full swing or a push into a gap.
    const double t = in.shot != Shot::Defend ? in.timingMs : in.timingMs * (Abs(in.direction) <= 10 ? 0.7 : 0.9);
    if (Abs(t) > 150) return missed(padY, padX);  // far too early / late: the bat is elsewhere
    // Offsets of the ball from the planned contact point.
    double dw = shape.horizontalBat ? at->p.z - bat.z : at->p.x - bat.x;   // across the blade
    const double dl = shape.horizontalBat ? at->p.x - bat.x : at->p.z - bat.z;  // along the blade
    dw += (dw < 0 ? -1.0 : 1.0) * Abs(t) * 0.0007;  // mistimed = the bat comes through in the wrong place
    // The wrong shot for the length (driving a ball that isn't full, pulling
    // one that isn't short...) puts the bat in a worse place: 1 cm per metre.
    {
        double lo = 0, hi = 30;
        switch (in.shot) {
            case Shot::Drive: lo = 0.0, hi = 5.0; break;
            case Shot::Loft: lo = 0.0, hi = 4.5; break;
            case Shot::Pull: lo = 7.5, hi = 30; break;
            case Shot::Cut: lo = 6.5, hi = 30; break;
            case Shot::Flick: lo = 0.0, hi = 7.0; break;
            case Shot::Sweep: lo = 2.0, hi = 6.5; break;
            default: break;
        }
        const double len = path.pitched ? path.pitchPoint.y : 0.0;
        const double mismatch = len < lo ? lo - len : (len > hi ? len - hi : 0.0);
        dw += (dw < 0 ? -1.0 : 1.0) * 0.01 * mismatch;
    }
    const double halfW = 0.054;
    const double wOff = Abs(dw);
    if (Abs(dl) > 0.30 || wOff > halfW + kBallRadius) return missed(padY, padX);
    const double qT = Clamp(1.0 - Sq(t / 150.0), 0, 1);
    const double qL = 1.0 - Clamp((Abs(dl) - 0.04) / 0.26, 0, 1) * 0.7;
    const V3 vin = at->v;
    const double speedIn = Length(vin);
    if (wOff > halfW) {
        // Edge: the ball glances off, keeping much of its pace.
        r.edgeFraction = Clamp((wOff - halfW) / kBallRadius, 0, 1);
        const double keep = 0.55 + 0.35 * r.edgeFraction;
        const double defl = Deg(4 + (1 - r.edgeFraction) * 40);
        if (!shape.horizontalBat) {
            r.contact = dw > 0 ? Contact::OutsideEdge : Contact::InsideEdge;
            const double sign = dw > 0 ? 1.0 : -1.0;
            // Rotate the incoming direction sideways (towards the slips for an outside edge).
            const double c = DetCos(defl), s = DetSin(defl) * sign;
            // Outside edges fly on (slip catches); inside edges are dragged down
            // into the ground / pads.
            V3 v{vin.x * c - vin.y * s, vin.x * s + vin.y * c, sign > 0 ? vin.z + 1.5 : -Abs(vin.z) - 2.0};
            // Incoming y is negative; rotating by +angle moves x positive.
            r.exitVelocity = v * keep;
        } else {
            r.contact = dw > 0 ? Contact::TopEdge : Contact::BottomEdge;
            if (dw > 0) {
                // Skied off the top edge: up and back over the keeper / fine leg.
                const double up = 10 + 8 * (1 - r.edgeFraction);
                r.exitVelocity = {vin.x * 0.3 + (in.shot == Shot::Cut ? 4 : -4), -speedIn * 0.35, up};
            } else {
                r.exitVelocity = {vin.x * 0.5, vin.y * 0.5, -Abs(vin.z) - 2};  // down onto the ground
            }
        }
        r.quality = 0.1 * (1 - r.edgeFraction);
        r.exitSpeedKph = Length(r.exitVelocity) * 3.6;
        return finish();
    }
    // Middled (or near enough): bat-ball collision along the face normal.
    r.contact = Contact::Middled;
    const double qW = 1.0 - 0.5 * Sq(wOff / halfW);
    r.quality = qW * qL * qT;
    const double dir = Clamp(in.direction + t * 0.18, shape.dirMin, shape.dirMax);  // late -> squarer on the off side
    double elev = shape.elevation + Clamp(dl * 30, -15, 20) + Clamp(vin.z, -5, 8) * 0.5;
    if (in.shot == Shot::Defend) elev = std::min(elev, -2.0);
    const double ce = DetCos(Deg(elev));
    const V3 n{DetSin(Deg(dir)) * ce, DetCos(Deg(dir)) * ce, DetSin(Deg(elev))};
    // A defensive push into a gap (direction given) is played firmer than a block.
    const double base = (in.shot == Shot::Defend && Abs(in.direction) > 10) ? 6.0 : shape.batSpeed;
    r.batSpeed = base * (0.85 + 0.15 * skill) * (0.6 + 0.4 * qT);
    const double e = (in.shot == Shot::Defend ? 0.25 : 0.50) * (0.6 + 0.4 * r.quality);
    const double mEff = 0.85 * (0.5 + 0.5 * qL);
    const double uBall = Dot(vin, n);
    const double uBat = r.batSpeed;
    const double vn = (kBallMass * uBall + mEff * uBat + mEff * e * (uBat - uBall)) / (kBallMass + mEff);
    const V3 vt = vin - n * uBall;
    r.exitVelocity = n * vn + vt * 0.30;
    r.exitSpeedKph = Length(r.exitVelocity) * 3.6;
    return finish();
}

// ---------------------------------------------------------------- fielding
std::vector<Fielder> FieldersFor(const FieldSetup& setup, const std::vector<double>& skills,
                                 const std::vector<int>& indices, bool leftHandedStriker, bool spinner) {
    std::vector<Fielder> out;
    auto skillAt = [&](size_t i) { return i < skills.size() ? skills[i] : 0.6; };
    auto indexAt = [&](size_t i) { return i < indices.size() ? indices[i] : -1; };
    for (size_t i = 0; i < setup.fielders.size(); ++i) {
        const auto& fp = setup.fielders[i];
        Fielder f;
        f.pos = {leftHandedStriker ? -fp.pos.x : fp.pos.x, fp.pos.y, 0};
        f.skill = skillAt(i);
        f.index = indexAt(i);
        const double d = Length(f.pos);
        f.closeCatcher = d < 10 || (fp.pos.y < 0 && d < 21);
        out.push_back(f);
    }
    // Wicketkeeper (standing up to spin, back to pace) and the bowler after
    // the follow-through. Their skills/indices follow the nine fielders.
    Fielder keeper;
    keeper.pos = {0, spinner ? -0.8 : -15.0, 0};
    keeper.keeper = true;
    keeper.closeCatcher = true;
    keeper.skill = skillAt(setup.fielders.size());
    keeper.index = indexAt(setup.fielders.size());
    out.push_back(keeper);
    Fielder bowler;
    bowler.pos = {0.6, 15.5, 0};
    bowler.skill = skillAt(setup.fielders.size() + 1);
    bowler.index = indexAt(setup.fielders.size() + 1);
    out.push_back(bowler);
    return out;
}

FieldResult PlayOutField(V3 start, V3 velocity, const std::vector<Fielder>& field, const GroundSpec& ground,
                         const Environment& env, bool afterEdge, const FieldingInput& assist) {
    FieldResult r;
    DeliverySpec still;  // no swing or spin after the shot
    Forces f{&still, &env, false};
    V3 p = start, v = velocity;
    double t = 0;
    bool bounced = false;
    bool rolling = false;
    const double dt = 0.005;
    const double rope = ground.boundaryRadius;
    auto fromCentre = [](V3 q) { return Length(V3{q.x, q.y - kPitchCentreY, 0}); };
    r.path.push_back({0, p, v, {}});
    size_t ropeIndex = SIZE_MAX;  // first sample beyond the rope
    // 1. Follow the ball until it stops or crosses the boundary (a ball in
    //    the air is followed a little beyond the rope, for relay catches).
    while (t < 20) {
        if (!rolling) {
            V3 np = p, nv = v;
            Step(f, np, nv, {}, dt);
            if (np.z < kBallRadius && nv.z < 0) {
                if (!bounced) {
                    r.carry = Length(V3{np.x, np.y, 0});
                    r.hangTime = t + dt;
                }
                bounced = true;
                np.z = kBallRadius;
                nv.z = -nv.z * ground.outfieldBounce;
                nv.x *= 0.80;
                nv.y *= 0.80;
                if (nv.z < 1.0) {
                    rolling = true;
                    nv.z = 0;
                }
            }
            p = np, v = nv;
        } else {
            const double hs = Length(V3{v.x, v.y, 0});
            const double ns = hs - ground.rollingDecel * dt;
            if (ns <= 0.05) {
                v = {0, 0, 0};
                r.path.push_back({t + dt, p, v, {}});
                break;
            }
            v = V3{v.x, v.y, 0} * (ns / hs);
            p = p + v * dt;
        }
        t += dt;
        r.path.push_back({t, p, v, {}});
        if (ropeIndex == SIZE_MAX && fromCentre(p) >= rope) {
            ropeIndex = r.path.size() - 1;
            r.boundary = true;
            r.runs = bounced ? 4 : 6;
            if (bounced || p.z < 0.5) break;
        }
        if (ropeIndex != SIZE_MAX && (fromCentre(p) > rope + 1.5 || p.z < kBallRadius + 0.01)) break;
    }
    if (!bounced) {
        r.carry = Length(V3{p.x, p.y, 0});
        r.hangTime = t;
    }
    r.stopPoint = p;
    // 2. Who gets to it first? Scan the flight in time order.
    const double runFirst = 3.0, runNext = 2.7, margin = 0.3;
    auto reachOf = [](const Fielder& fl) { return fl.keeper ? 1.8 : 1.5; };
    auto reactOf = [&](const Fielder& fl) { return (fl.keeper || fl.closeCatcher) ? 0.12 : 0.25; };
    // Time to cover d metres from standing: accelerate at ~8.5 m/s2 up to top speed.
    auto runTime = [](const Fielder& fl, double d) {
        const double vmax = 6.0 + 2.0 * fl.skill, acc = 8.5;
        const double dAcc = vmax * vmax / (2 * acc);
        if (d <= 0) return 0.0;
        if (d < dAcc) return std::sqrt(2 * d / acc);
        return vmax / acc + (d - dAcc) / vmax;
    };
    auto finishFielded = [&](const Fielder& fl, size_t slot, const Sample& s, size_t i, double fumble) {
        r.boundary = false;  // stopped inside the rope
        r.fielder = fl.index;
        r.slot = static_cast<int>(slot);
        r.nearestFielderSkill = fl.skill;
        // Pick up, turn, throw; collection and breaking the stumps take ~0.5 s.
        r.gatheredAt = s.t + (fl.keeper ? 0.3 : 0.6) + fumble;
        r.gatheredPoint = s.p;
        r.throwSpeed = 22 + 6 * fl.skill;
        r.atStrikerEnd = r.gatheredAt + Length(V3{s.p.x, s.p.y, 0}) / r.throwSpeed + 0.5;
        r.atBowlerEnd = r.gatheredAt + Length(V3{s.p.x, s.p.y - kPitchLength, 0}) / r.throwSpeed + 0.5;
        const double ballAtStumps = std::min(r.atStrikerEnd, r.atBowlerEnd);
        int runs = 0;
        while (runs < 3 && runFirst + runNext * runs + margin <= ballAtStumps) ++runs;
        r.runs = runs;
        r.distance = Length(V3{s.p.x, s.p.y, 0});
        r.stopPoint = s.p;
        r.path.resize(i + 1);
    };
    // A ball hit down into the ground from below waist height can't be caught
    // before it lands (a "bump ball").
    bool inAir = !(velocity.z < -0.5 && start.z < 1.0);
    std::vector<char> beaten(field.size(), 0);  // fielders the ball has already gone past
    for (size_t i = 0; i < r.path.size(); ++i) {
        const Sample& s = r.path[i];
        if (s.t > r.hangTime + 1e-9) inAir = false;
        const bool beyond = ropeIndex != SIZE_MAX && i >= ropeIndex;
        if (beyond && !inAir) break;
        const double dc = fromCentre(s.p);
        int best = -1;
        double bestSpare = -1;
        for (size_t k = 0; k < field.size(); ++k) {
            const Fielder& fl = field[k];
            if (beaten[k]) continue;
            const double d = Length(V3{s.p.x - fl.pos.x, s.p.y - fl.pos.y, 0});
            const double need = reactOf(fl) + runTime(fl, d - reachOf(fl));
            if (need > s.t) continue;
            // Outfielders set at the rope can leap for a ball going over.
            const bool atRope = fromCentre(fl.pos) > rope - 8;
            const double maxH = fl.keeper ? 2.6 : (atRope && s.t - need > 0.35 ? 3.0 : 2.4);
            if (inAir ? (s.p.z > maxH || s.p.z < 0.02) : s.p.z > 1.5) continue;
            const double spare = s.t - need;
            if (spare > bestSpare) best = static_cast<int>(k), bestSpare = spare;
        }
        if (best < 0) continue;
        const Fielder& fl = field[static_cast<size_t>(best)];
        const double d = Length(V3{s.p.x - fl.pos.x, s.p.y - fl.pos.y, 0});
        if (!inAir) {
            // A ground stop: a hard-hit ball that needs a dive can beat the fielder.
            const double speed = Length(V3{s.v.x, s.v.y, 0});
            const double hardness = speed / 30.0 + (d > 0.8 ? 0.3 : 0) - std::min(bestSpare, 0.6) * 0.6 - assist.stopBonus;
            const double ability = 0.45 + 0.5 * fl.skill + (fl.keeper ? 0.1 : 0);
            if (hardness > ability) {
                beaten[static_cast<size_t>(best)] = 1;
                continue;
            }
            r.savedAtBoundary = dc > rope - 3.0;
            finishFielded(fl, static_cast<size_t>(best), s, i, 0);
            return r;
        }
        // A catch: harder when the ball is fast, needs a dive, arrives just as
        // the fielder gets there, or is taken at the rope. Deterministic.
        const bool dive = d > 0.8;
        const double speed = Length(s.v);
        const bool nearRope = beyond || dc > rope - 1.0;
        double hardness = speed / 35.0 + (dive ? 0.35 : 0) - std::min(bestSpare, 0.6) * 0.5 +
                          (afterEdge && !fl.closeCatcher && !fl.keeper ? 0.2 : 0) - assist.catchBonus;
        if (nearRope) hardness += beyond ? 0.30 : 0.12;  // balance on the rope / relay
        const double ability = 0.53 + 0.5 * fl.skill + ((fl.closeCatcher || fl.keeper) ? 0.15 : 0);
        if (hardness <= ability) {
            // Running hard at the rope: the fielder's momentum may carry them over.
            if (!beyond && dc > rope - 1.0 && bestSpare < 0.15 && fl.skill < 0.6) {
                r.steppedOnRope = true;
                r.boundary = true;
                r.runs = 6;
                r.fielder = fl.index;
                r.path.resize(i + 1);
                return r;
            }
            r.caught = true;
            r.boundaryCatch = beyond || dc > rope - 3.0;  // taken on the boundary
            r.catchHeight = s.p.z;
            r.boundary = false;
            r.runs = 0;
            r.fielder = fl.index;
            r.slot = best;
            r.distance = Length(V3{s.p.x, s.p.y, 0});
            r.path.resize(i + 1);
            r.stopPoint = s.p;
            return r;
        }
        if (beyond) {  // a failed relay: the six stands
            beaten[static_cast<size_t>(best)] = 1;
            continue;
        }
        r.dropped = true;
        finishFielded(fl, static_cast<size_t>(best), s, i, 1.0);
        return r;
    }
    // Nobody reached it before it stopped / crossed the rope.
    if (ropeIndex != SIZE_MAX) {
        r.path.resize(std::min(r.path.size(), ropeIndex + 1));
        r.distance = Length(V3{r.path.back().p.x, r.path.back().p.y, 0});
        return r;
    }
    r.distance = Length(V3{p.x, p.y, 0});
    double arrive = 1e9;
    size_t who = 0;
    for (size_t k = 0; k < field.size(); ++k) {
        const Fielder& fl = field[k];
        const double d = Length(V3{p.x - fl.pos.x, p.y - fl.pos.y, 0});
        const double need = reactOf(fl) + runTime(fl, d - reachOf(fl));
        if (need < arrive) arrive = need, who = k;
    }
    if (field.empty()) {
        r.runs = 3;
        return r;
    }
    Sample stop{std::max(t, arrive), p, {}, {}};
    r.path.push_back(stop);
    finishFielded(field[who], who, stop, r.path.size() - 1, 0);
    return r;
}

uint64_t PathHash(const std::vector<Sample>& s) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](double d) {
        uint64_t bits;
        std::memcpy(&bits, &d, sizeof bits);
        for (int i = 0; i < 8; ++i) {
            h ^= (bits >> (8 * i)) & 0xff;
            h *= 1099511628211ULL;
        }
    };
    for (const auto& x : s) {
        mix(x.t);
        mix(x.p.x), mix(x.p.y), mix(x.p.z);
        mix(x.v.x), mix(x.v.y), mix(x.v.z);
    }
    return h;
}

}  // namespace cricket::phys
