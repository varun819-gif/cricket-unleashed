#include "cricket/FieldPlacement.h"

#include <cmath>

namespace cricket {

namespace {

constexpr double kPi = 3.14159265358979323846;

// angleDeg: 0 = straight down the ground, +90 = square on the off side,
// 180 = directly behind the striker, -90 = square on the leg side.
FieldPosition Polar(const char* name, double angleDeg, double dist) {
    double a = angleDeg * kPi / 180.0;
    return {name, {dist * std::sin(a), dist * std::cos(a)}};
}

double Dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

}  // namespace

const std::vector<FieldPosition>& StandardPositions() {
    static const std::vector<FieldPosition> kAll = {
        // Close catchers
        Polar("First slip", 165, 18), Polar("Second slip", 158, 18), Polar("Third slip", 151, 18),
        Polar("Gully", 128, 20), Polar("Leg slip", -165, 14), Polar("Short leg", -70, 5),
        Polar("Silly point", 70, 5), Polar("Silly mid-off", 15, 9), Polar("Silly mid-on", -15, 9),
        // Inside the circle
        Polar("Point", 90, 22), Polar("Cover", 55, 26), Polar("Extra cover", 40, 27),
        Polar("Mid-off", 12, 26), Polar("Mid-on", -12, 26), Polar("Midwicket", -50, 26),
        Polar("Square leg", -85, 22), Polar("Short fine leg", -145, 15),
        // Outfield
        Polar("Third man", 140, 55), Polar("Deep point", 95, 62), Polar("Deep cover", 60, 68),
        Polar("Long-off", 15, 75), Polar("Long-on", -15, 75), Polar("Cow corner", -40, 72),
        Polar("Deep midwicket", -55, 70), Polar("Deep square leg", -95, 62), Polar("Fine leg", -160, 55),
    };
    return kAll;
}

std::optional<FieldPosition> FindPosition(const std::string& name) {
    for (const auto& p : StandardPositions())
        if (p.name == name) return p;
    return std::nullopt;
}

const char* ToString(FieldPreset p) {
    switch (p) {
        case FieldPreset::Attacking: return "Attacking";
        case FieldPreset::Defensive: return "Defensive";
        case FieldPreset::Powerplay: return "Powerplay";
        case FieldPreset::DeathOvers: return "Death overs";
        case FieldPreset::Spin: return "Spin bowling";
        case FieldPreset::Pace: return "Pace bowling";
        case FieldPreset::OffSideAttack: return "Off-side attack";
        case FieldPreset::LegSideAttack: return "Leg-side attack";
        case FieldPreset::BoundaryProtection: return "Boundary protection";
        case FieldPreset::Balanced: return "Balanced";
    }
    return "?";
}

const std::vector<FieldPreset>& AllPresets() {
    static const std::vector<FieldPreset> kAll = {
        FieldPreset::Attacking, FieldPreset::Defensive,     FieldPreset::Powerplay,     FieldPreset::DeathOvers,
        FieldPreset::Spin,      FieldPreset::Pace,          FieldPreset::OffSideAttack, FieldPreset::LegSideAttack,
        FieldPreset::BoundaryProtection, FieldPreset::Balanced};
    return kAll;
}

FieldSetup FieldSetup::FromNames(const std::vector<std::string>& names) {
    FieldSetup f;
    for (const auto& n : names)
        if (auto p = FindPosition(n)) f.fielders.push_back(*p);
    return f;
}

FieldSetup FieldSetup::FromPreset(FieldPreset p) {
    switch (p) {
        case FieldPreset::Attacking:
            return FromNames({"First slip", "Second slip", "Third slip", "Gully", "Point", "Cover", "Mid-off",
                              "Mid-on", "Fine leg"});
        case FieldPreset::Pace:
            return FromNames({"First slip", "Second slip", "Gully", "Point", "Cover", "Mid-off", "Mid-on",
                              "Square leg", "Fine leg"});
        case FieldPreset::Powerplay:
            return FromNames({"First slip", "Point", "Cover", "Mid-off", "Mid-on", "Midwicket", "Square leg",
                              "Third man", "Fine leg"});
        case FieldPreset::Defensive:
            return FromNames({"Point", "Cover", "Mid-off", "Mid-on", "Midwicket", "Third man", "Fine leg",
                              "Deep cover", "Long-on"});
        case FieldPreset::DeathOvers:
            return FromNames({"Point", "Cover", "Mid-off", "Mid-on", "Third man", "Fine leg", "Deep square leg",
                              "Long-on", "Deep cover"});
        case FieldPreset::Spin:
            return FromNames({"First slip", "Point", "Cover", "Mid-off", "Mid-on", "Midwicket", "Short fine leg",
                              "Long-on", "Deep midwicket"});
        case FieldPreset::OffSideAttack:
            return FromNames({"First slip", "Second slip", "Gully", "Point", "Cover", "Extra cover", "Mid-off",
                              "Mid-on", "Fine leg"});
        case FieldPreset::LegSideAttack:
            return FromNames({"Short leg", "Square leg", "Midwicket", "Mid-on", "Fine leg", "Deep square leg",
                              "Deep midwicket", "Point", "Mid-off"});
        case FieldPreset::BoundaryProtection:
            return FromNames({"Point", "Mid-off", "Mid-on", "Short fine leg", "Deep point", "Deep cover",
                              "Long-off", "Long-on", "Deep midwicket"});
        case FieldPreset::Balanced:
            return FromNames({"First slip", "Point", "Cover", "Mid-off", "Mid-on", "Midwicket", "Square leg",
                              "Fine leg", "Deep cover"});
    }
    return {};
}

Status FieldSetup::Move(int i, Vec2 newPos, const std::string& label) {
    if (i < 0 || i >= static_cast<int>(fielders.size())) return Status::Err("no such fielder");
    fielders[i] = {label, newPos};
    return Status::Ok();
}

bool InsideCircle(Vec2 p) {
    // The fielding circle is two 30-yard semicircles centred on the middle
    // stumps, joined by straight lines: i.e. distance to the stump-to-stump
    // segment is at most 30 yards.
    double t = std::fmax(0.0, std::fmin(kPitchLength, p.y));
    return Dist(p, {0, t}) <= kCircleRadius;
}

bool OnPitch(Vec2 p) {
    return std::fabs(p.x) < kPitchHalfWidth && p.y > -kPoppingCrease && p.y < kPitchLength + kPoppingCrease;
}

bool BehindSquareLeg(Vec2 p) { return p.x < 0 && p.y < kPoppingCrease; }

double DistanceFromCentre(Vec2 p) { return Dist(p, {0, kPitchLength / 2}); }

FieldCheck ValidateField(const FieldSetup& f, const FieldContext& ctx) {
    FieldCheck c;
    if (f.fielders.size() != 9) {
        c.status = Status::Err("a field needs exactly 9 fielders (plus bowler and wicketkeeper)");
        return c;
    }
    for (size_t i = 0; i < f.fielders.size(); ++i) {
        const auto& fp = f.fielders[i];
        if (OnPitch(fp.pos)) {
            c.status = Status::Err(fp.name + " is standing on the pitch");
            return c;
        }
        if (DistanceFromCentre(fp.pos) > ctx.boundaryRadius - 1.0) {
            c.status = Status::Err(fp.name + " is beyond the boundary");
            return c;
        }
        for (size_t j = 0; j < i; ++j) {
            if (Dist(fp.pos, f.fielders[j].pos) < 1.0) {
                c.status = Status::Err(fp.name + " and " + f.fielders[j].name + " are in the same spot");
                return c;
            }
        }
        if (!InsideCircle(fp.pos)) c.outsideCircle++;
        if (BehindSquareLeg(fp.pos)) c.legSideBehindSquare++;
    }
    if (c.legSideBehindSquare > 2) {
        c.status = Status::Err("only 2 fielders are allowed behind square on the leg side");
        return c;
    }
    if (ctx.rules) {
        int max = ctx.rules->MaxOutsideCircle(ctx.overNumber);
        if (max >= 0 && c.outsideCircle > max) {
            c.status = Status::Err("only " + std::to_string(max) + " fielders are allowed outside the circle in over " +
                                   std::to_string(ctx.overNumber));
            return c;
        }
    }
    return c;
}

}  // namespace cricket
