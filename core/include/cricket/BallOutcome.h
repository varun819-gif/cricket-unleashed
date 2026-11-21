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
};

// What happened to one ball, with the physics numbers to show players.
struct BallReport {
    phys::DeliverySpec spec;
    phys::DeliveryPath path;
    phys::BatResult bat;
    phys::FieldResult field;
    BatChoice shot;
    Delivery delivery;                   // how it is scored (if no appeal / appeal not out handled by case)
    std::optional<DecisionCase> appeal;  // LBW / caught appeal built from the physics evidence
    std::string summary;                 // e.g. "138 km/h, pitched 5.8 m, swung 21 cm - outside edge, caught at First slip"
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

    phys::Pitch pitch = phys::Pitch::Standard();
    phys::GroundSpec ground;
    phys::Environment env;

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
                           const BatChoice& bat, QuickSim* perceive, const phys::DeliveryPath* path = nullptr);

    static bool IsSpinner(const Match& m);

private:
    DetRandom rng_;
};

}  // namespace cricket
