// Field placement: named positions, presets and legality checks (Law 28 +
// playing-condition circle restrictions).
//
// Coordinates (metres), for a RIGHT-handed striker:
//   origin = middle stump at the striker's end
//   +y     = down the pitch towards the bowler (bowler's stumps at y = 20.12)
//   +x     = off side
// For a left-hander every position is mirrored (x -> -x).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cricket/MatchRules.h"
#include "cricket/Types.h"

namespace cricket {

constexpr double kPitchLength = 20.12;    // stump to stump
constexpr double kPoppingCrease = 1.22;   // in front of the stumps
constexpr double kPitchHalfWidth = 1.525;
constexpr double kCircleRadius = 27.43;   // 30 yards
constexpr double kDefaultBoundary = 70.0; // from the pitch centre

struct Vec2 {
    double x = 0, y = 0;
};

struct FieldPosition {
    std::string name;
    Vec2 pos;  // right-hander coordinates
};

// Catalogue of traditional positions (slip, gully, point, cover, ...).
const std::vector<FieldPosition>& StandardPositions();
std::optional<FieldPosition> FindPosition(const std::string& name);

enum class FieldPreset {
    Attacking,
    Defensive,
    Powerplay,
    DeathOvers,
    Spin,
    Pace,
    OffSideAttack,
    LegSideAttack,
    BoundaryProtection,
    Balanced
};
const char* ToString(FieldPreset p);
const std::vector<FieldPreset>& AllPresets();

// Nine fielders (bowler and wicketkeeper are not part of the setup).
struct FieldSetup {
    std::vector<FieldPosition> fielders;
    static FieldSetup FromPreset(FieldPreset p);
    static FieldSetup FromNames(const std::vector<std::string>& names);  // unknown names are skipped
    // Manual placement helpers for the captain UI.
    Status Move(int fielderIndex, Vec2 newPos, const std::string& label = "custom");
};

struct FieldContext {
    const MatchRules* rules = nullptr;
    int overNumber = 1;             // 1-based
    Handedness striker = Handedness::Right;
    double boundaryRadius = kDefaultBoundary;
};

bool InsideCircle(Vec2 p);           // within the 30-yard "oval"
bool OnPitch(Vec2 p);                // on the pitch strip
bool BehindSquareLeg(Vec2 p);        // leg side, behind popping crease (right-hander coords)
double DistanceFromCentre(Vec2 p);

struct FieldCheck {
    Status status;
    int outsideCircle = 0;
    int legSideBehindSquare = 0;
};

// Validates a field for the given moment in the match. The setup is always
// stored in right-hander coordinates; handedness only matters for display.
FieldCheck ValidateField(const FieldSetup& f, const FieldContext& ctx);

}  // namespace cricket
