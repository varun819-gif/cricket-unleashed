// BallOutcome: turns a bowler's choice and a batter's shot into a scored
// ball using ONLY the ball physics (BallPhysics.h): flight, swing, bounce,
// spin, bat impact, then the fielders. The rules engine (MatchSession) then
// scores the Delivery exactly as before. Used by the offline game, the local
// phone-controller host and the online server (all through MatchHost).
//
// What is random and what is not:
//   * The physics is deterministic: the same delivery and the same shot always
//     give the same result.
//   * Human players' inputs come from the players.
//   * Computer players (AI bowler / AI batter) and the bowler's execution
//     (a bowler can't hit the same spot every time; the seam lands a little
//     differently) vary with a SEEDED generator - the same seed gives the same
//     match. These are player-skill variations, never "roll for runs".
//     Our own generator (SplitMix64) is used so it is identical on every
//     platform (std:: distributions are not).
#pragma once

#include <optional>
#include <string>

#include "cricket/AutoCaptain.h"
#include "cricket/BallPhysics.h"
#include "cricket/Drs.h"
#include "cricket/FieldPlacement.h"
#include "cricket/Match.h"
#include "cricket/MatchSession.h"

namespace cricket {

struct BowlChoice {
    std::string variation = "stock";  // stock, yorker, bouncer, slower, inswing, outswing, offbreak, legbreak, googly, armball
    double line = 0.0;    // -1 (down leg) .. 0 (stumps) .. +1 (wide outside off)
    double length = 0.5;  // 0 (yorker) .. 1 (bouncer)
    int effort = 70;      // 0..100
};

struct BatChoice {
    std::string shot = "leave";  // leave, defend, drive, loft, cut, pull, flick, sweep
    double direction = 0;        // degrees, 0 straight, + off side, - leg side
    double timingMs = 0;         // in real (physics) time; + late
    bool provided = false;       // false = no shot offered
    bool advance = false;        // dance down the pitch (can be stumped if missed)
    int runsCalled = -1;         // the batters' call when the ball is in the field: -1 = they judge it
                                 // themselves (computer), 0 = wait (no run), 1..4 = run that many
};

// The fielding side's live input for this ball (phone): catch / dive timing
// and which end to throw to. Default = the computer fields.
struct FieldingChoice {
    phys::FieldingInput assist;
    int throwEnd = -1;  // -1 automatic, 0 = striker's (keeper's) end, 1 = bowler's end
};

// Where the match is played: pitch type, weather, day-night.
struct Conditions {
    std::string pitchName = "standard";
    phys::Pitch pitch = phys::Pitch::Standard();
    phys::V3 wind;              // m/s (+x off side, +y towards the bowler)
    bool dayNight = false;      // dew in the second innings of a limited-overs game
    double dewStrength = 0.6;   // 0..1 at its heaviest
};

// What happened to one ball, with the physics numbers to show players.
struct BallReport {
    phys::DeliverySpec spec;
    phys::DeliveryPath path;
    phys::BatResult bat;
    phys::FieldResult field;
    BatChoice shot;
    Delivery delivery;                   // how it is scored (if no appeal / appeal not out handled by case)
    std::optional<DecisionCase> appeal;  // appeal / TV-umpire case built from the physics evidence
    std::string summary;                 // e.g. "138 km/h, pitched 5.8 m, swung 21 cm - outside edge, caught at First slip"
    // Gameplay detail.
    int runsAttempted = 0;
    bool runOutChance = false;    // a close run out (or a run out)
    double runOutMargin = 0;      // + = batter short by this many seconds
    int overthrows = 0;
    bool frontFootNoBall = false; // overstepped (called on the field or found on TV)
    bool stumpingChance = false;
    bool runningChance = false;   // the ball stayed in the field: the batters had a run to judge
    double runSafeS = 0;          // seconds before a direct hit could reach the stumps
    double ballBackS = 0;         // seconds until the throw reaches the stumps (best end, normal throw)
    double oneRunS = 0, twoRunsS = 0, threeRunsS = 0;  // how long these batters take
    phys::PitchSpot pitchSpot{};  // the surface where it pitched
};

// Tiny deterministic generator (SplitMix64): same numbers on every platform.
class DetRandom {
public:
    explicit DetRandom(uint64_t seed) : s_(seed) {}
    uint64_t Next();
    double Uniform();                    // [0, 1)
    double Normal(double sigma);         // approx. normal (sum of 4 uniforms), mean 0

private:
    uint64_t s_;
};

class BallOutcome {
public:
    explicit BallOutcome(uint64_t seed) : rng_(seed) {}

    Conditions conditions;
    phys::GroundSpec ground;
    // The pitch and air for the next ball: wear grows through a match (a lot
    // over five Test days); dew comes in the second innings of a day-night
    // limited-overs game.
    phys::Pitch PitchFor(const Match& m) const;
    phys::Environment EnvFor(const Match& m) const;

    // The bowler's choice -> what actually leaves the hand (speed from the
    // bowler's skill and effort; line/length with execution scatter).
    phys::DeliverySpec SpecFor(const Match& m, const BowlChoice& b);
    // The computer bowler's choice for this ball.
    BowlChoice AiBowl(const Match& m);
    // The computer batter's shot, decided from what a batter can see early
    // (line and length), then executed with skill-based timing variation.
    BatChoice AiShot(const Match& m, const phys::DeliveryPath& path, const phys::DeliverySpec& spec);

    // Plays the ball. `field` is the fielding side's setup. `perceive` fills
    // players' beliefs on appeals (may be null in tests).
    BallReport Resolve(const Match& m, const FieldSetup& field, const BowlChoice& bowl, const BatChoice& bat,
                       QuickSim* perceive);
    // Same with a ready-made delivery (tests).
    BallReport ResolveSpec(const Match& m, const FieldSetup& field, const phys::DeliverySpec& spec,
                           const BatChoice& bat, QuickSim* perceive, const phys::DeliveryPath* path = nullptr,
                           const FieldingChoice& fielding = {});
    // A whole computer-v-computer ball on a session (simulator, tests):
    // choose, bowl, bat, field, then score it (with any appeal / referral).
    BallReport PlayComputerBall(MatchSession& session, QuickSim* perceive);

    static bool IsSpinner(const Match& m);

private:
    DetRandom rng_;
};

}  // namespace cricket
