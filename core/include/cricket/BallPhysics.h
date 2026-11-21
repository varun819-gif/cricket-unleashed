// Cricket ball physics: the delivery (release -> flight -> bounce -> batter),
// the bat impact, and the ball after the shot (flight, bounces, roll) against
// the fielders. Engine-independent C++: the same code runs in the offline
// game, the online server and (later) inside Unreal, which only draws it.
//
// Coordinates (metres, seconds), same as FieldPlacement.h, for a RIGHT-handed
// striker (a left-hander is mirrored in x):
//   origin = middle stump at the striker's end, at ground level
//   +y = down the pitch towards the bowler (bowler's stumps at y = 20.12)
//   +x = off side,  +z = up
//
// DETERMINISM: everything here is a pure function of its inputs. The
// integrators use only + - * / and sqrt (exactly rounded in IEEE 754), our
// own sine / cosine (DetSin / DetCos) and fixed time steps, and the core is
// compiled without floating-point contraction (-ffp-contract=off), so the
// same inputs give bit-identical results on every machine with IEEE doubles.
// There are no random numbers in this file.
//
// MODEL SOURCES (simplified, see docs/BALL_PHYSICS.md): drag and swing
// coefficients after R. D. Mehta's wind-tunnel work on cricket balls; Magnus
// lift from spin; rigid-sphere bounce with friction (R. Cross); bat-ball
// collision with coefficient of restitution and effective bat mass.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cricket/FieldPlacement.h"

namespace cricket::phys {

// ---------------------------------------------------------------- maths
struct V3 {
    double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator*(double s, V3 a) { return a * s; }
inline double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Length(V3 a);
// Deterministic sine / cosine (radians), independent of the C library.
double DetSin(double x);
double DetCos(double x);
constexpr double kPi = 3.14159265358979323846;
inline double Deg(double d) { return d * kPi / 180.0; }

// ---------------------------------------------------------------- constants
constexpr double kBallMass = 0.160;        // kg (Law 4: 155.9 - 163 g)
constexpr double kBallRadius = 0.0360;     // m  (circumference 22.4 - 22.9 cm)
constexpr double kStumpHalfWidth = 0.1143; // three stumps = 22.86 cm
constexpr double kStumpHeight = 0.711;     // to the top of the bails
constexpr double kWideGuideline = 0.89;    // limited-overs wide marking, from middle stump

struct Environment {
    double airDensity = 1.2;   // kg/m3 (sea level, ~20 C)
    double gravity = 9.81;
    // Wind (m/s, same axes): drag and swing act on the air speed of the ball.
    V3 wind;
    // Dew 0..1 (evening, second innings of day-night games): a wet ball
    // grips less off the pitch and swings less.
    double dew = 0;
};

// What the pitch does to the ball at the bounce.
struct Pitch {
    double bounce = 0.60;     // vertical coefficient of restitution
    double friction = 0.30;   // sliding friction: grip for spin and pace loss
    double seam = 0.30;       // 0..1: grass / moisture -> movement off the seam
    // Deterioration 0 (fresh) .. 1 (day-5 Test pitch): more grip and turn,
    // less seam, lower and more variable bounce, rough in the bowlers'
    // footmarks. Set per ball by the game (see BallOutcome::PitchFor).
    double wear = 0;
    uint64_t cracks = 1;      // seed of this pitch's crack / rough pattern (deterministic)
    // Presets.
    static Pitch Standard() { return {}; }
    static Pitch Green() { return {0.60, 0.28, 0.85}; }   // seamer-friendly
    static Pitch Dusty() { return {0.54, 0.48, 0.10}; }   // turner (day 4-5)
    static Pitch Bouncy() { return {0.68, 0.28, 0.35}; }  // hard, fast (Perth-style)
    static Pitch Flat() { return {0.57, 0.26, 0.10}; }    // batting road
    static Pitch ByName(const std::string& name);         // standard, green, dusty, bouncy, flat
};

// The pitch as the ball finds it at one spot (wear, footmarks, cracks).
struct PitchSpot {
    double bounce, friction, seam;
    bool inRough;
};
PitchSpot PitchAt(const Pitch& p, double x, double y);

// How the ball leaves the bowler's hand. The physics finds the release
// angles that land the ball on (targetLength, targetLine) - like a bowler
// aiming at a spot - then the flight, swing, bounce and spin do the rest.
struct DeliverySpec {
    double speedKph = 135;      // at release
    double targetLength = 6.0;  // metres from the striker's stumps where it should pitch
    double targetLine = 0.10;   // x where it should pitch (0 = middle stump, + = off)
    double releaseHeight = 2.15;
    double releaseX = 0.35;     // where the hand is at release (+ = off side of middle stump)
    double releaseY = 18.70;    // a little in front of the bowling crease
    // Swing: seam angle to the flight direction in degrees (+ = seam towards
    // the off side -> conventional swing moves the ball towards +x).
    double seamAngle = 0;
    double seamWobble = 0.1;    // 0 = perfectly upright seam, 1 = scrambled
    double shine = 1.0;         // 1 = new shiny ball, 0 = old and rough (reverse swing)
    // Spin in revolutions per second and how the axis is tilted:
    //   turn  +1 = all side-spin turning the ball towards +x after the bounce
    //   (leg-break to a right-hander), -1 = towards -x (off-break)
    //   top   +1 = all top-spin (dips, kicks on), -1 = all back-spin (skids)
    double spinRps = 0;
    double spinTurn = 0;
    double spinTop = 0;
    // Movement off the seam at the bounce: -1..+1 (which way the seam
    // "bites"). Set by the caller (e.g. from the seam orientation).
    double seamNip = 0;
    // Front foot at delivery: metres beyond the popping crease (> 0 = no-ball).
    double overstep = -0.1;
};

struct Sample {
    double t = 0;
    V3 p, v;
    V3 w;  // spin (rad/s)
};

struct DeliveryPath {
    std::vector<Sample> samples;  // every 2 ms from release
    V3 releaseVelocity;
    double releaseSpeedKph = 0;
    bool pitched = false;         // bounced before reaching the batter's end (false = full toss)
    V3 pitchPoint;                // where it bounced
    double pitchTime = 0;
    double speedAtBounceKph = 0, speedAfterBounceKph = 0;
    double swingBeforePitch = 0;  // sideways movement in the air before the bounce (m)
    double deviationOffPitch = 0; // sideways change of direction at the bounce, measured 2 m later (m)
    // Where it crosses the line of the stumps (y = 0) if nothing intercepts it.
    V3 atStumps;
    double timeAtStumps = 0;
    bool wouldHitStumps = false;
    // Crossing the batter's popping crease (y = 1.22).
    V3 atCrease;
    double timeAtCrease = 0;
};

// Position and velocity on the path when it crosses the plane y = planeY.
std::optional<Sample> CrossingAt(const DeliveryPath& d, double planeY);

DeliveryPath SimulateDelivery(const DeliverySpec& spec, const Pitch& pitch, const Environment& env = {});
// Same, but without the bounce search: integrate from a given state (used for
// the batter's prediction and for ball tracking after a pad impact).
std::vector<Sample> Integrate(V3 p, V3 v, V3 spinRadPerSec, const DeliverySpec& spec, const Pitch& pitch,
                              const Environment& env, double untilY, double maxT, bool allowBounce,
                              bool* bounced = nullptr, Sample* bounce = nullptr);

// Lateral (swing) force coefficient for the given air speed and ball state.
double SwingCoefficient(double speed, double seamAngleDeg, double wobble, double shine);

// ---------------------------------------------------------------- batting
enum class Shot { Leave, Defend, Drive, Loft, Cut, Pull, Flick, Sweep };
const char* ToString(Shot s);
std::optional<Shot> ShotFromString(const std::string& s);

struct ShotInput {
    Shot shot = Shot::Defend;
    double direction = 0;   // degrees: 0 = straight back past the bowler, + = off side, - = leg side
    double timingMs = 0;    // + late, - early (relative to the ideal moment)
    double skill = 0.6;     // 0..1 batting ability (reading the ball, bat speed)
    bool leftHanded = false;
    bool advance = false;   // dances down the pitch (meets the ball earlier; can be stumped)
};

enum class Contact { Missed, Middled, OutsideEdge, InsideEdge, TopEdge, BottomEdge, Pad, Bowled };
const char* ToString(Contact c);

struct BatResult {
    Contact contact = Contact::Missed;
    double contactY = 0;          // where along the pitch bat met ball
    V3 ballAtContact;
    double quality = 0;           // 0..1: 1 = sweet spot, perfectly timed
    double edgeFraction = 0;      // 0..1 for edges: 1 = faintest
    double exitSpeedKph = 0;
    V3 exitVelocity;
    double batSpeed = 0;
    double readError = 0;         // how far (m) the batter's read was off at contact (late movement)
    // Pad impact (LBW material).
    V3 padImpact;
    V3 velocityAtPad;
    std::vector<Sample> trackedToStumps;  // ball-tracking path from the pad to the stumps
    // Out of the crease when the ball passed: seconds needed to get the bat
    // back behind the popping crease (0 = never left it).
    double regainGroundS = 0;
};

// The batter plays `in` at `path`. Pure function. The batter "reads" the ball
// some time before it arrives (better batters read later) and puts the bat
// where they expect it - so late swing, seam and spin find edges.
BatResult PlayShot(const DeliveryPath& path, const DeliverySpec& spec, const Pitch& pitch, const ShotInput& in,
                   const Environment& env = {});

// ---------------------------------------------------------------- fielding
struct Fielder {
    V3 pos;               // where they stand (z = 0)
    double skill = 0.6;   // 0..1 fielding
    bool keeper = false;
    bool closeCatcher = false;  // slips / short leg: quicker reactions
    int index = -1;       // player index in the fielding XI (reported back)
};

struct FieldResult {
    bool caught = false;
    bool dropped = false;
    int fielder = -1;           // index of the fielder who caught / fielded it
    int slot = -1;              // which entry of the field vector
    bool boundary = false;
    int runs = 0;               // safe runs (or 4 / 6 when boundary)
    // Catching detail.
    double catchHeight = 0;     // m above the grass when caught (low catches go to the TV umpire)
    bool boundaryCatch = false; // taken within a step of the rope (or relayed back in)
    bool steppedOnRope = false; // fielder carried over the rope with the ball: six
    bool savedAtBoundary = false;
    // Running detail: when the ball is gathered and when it can reach each
    // end's stumps (throw + collection + break), from bat on ball.
    double gatheredAt = 0;
    V3 gatheredPoint;
    double throwSpeed = 25;
    double atStrikerEnd = 1e9, atBowlerEnd = 1e9;
    double nearestFielderSkill = 0.6;
    double distance = 0;        // how far the ball went (m from the striker's stumps)
    double carry = 0;           // first bounce (or catch) distance
    double hangTime = 0;        // time in the air before the first bounce
    std::vector<Sample> path;   // after the shot
    V3 stopPoint;
    std::string where;          // e.g. "caught at slip"
};

// A human fielding side's input for this ball (phone "catch" / "dive").
struct FieldingInput {
    bool provided = false;
    double catchBonus = 0;  // + makes catches easier (good timing), - harder
    double stopBonus = 0;   // + for a well-timed dive
};

struct GroundSpec {
    double boundaryRadius = 70.0;   // from the centre of the pitch (outfielders stand ~65 m)
    double outfieldBounce = 0.35;
    double rollingDecel = 3.2;      // m/s2 on grass
};

// Follows the ball after the shot and works out the catch / fielding / runs.
FieldResult PlayOutField(V3 start, V3 velocity, const std::vector<Fielder>& field, const GroundSpec& ground = {},
                         const Environment& env = {}, bool afterEdge = false, const FieldingInput& assist = {});

// The fielding side's eleven positions for this ball: the nine from the field
// setup, plus the bowler (follow-through) and the wicketkeeper.
std::vector<Fielder> FieldersFor(const FieldSetup& setup, const std::vector<double>& skills,
                                 const std::vector<int>& indices, bool leftHandedStriker, bool spinner);

// Stable 64-bit fingerprint of a path (for determinism tests across machines).
uint64_t PathHash(const std::vector<Sample>& s);

}  // namespace cricket::phys
