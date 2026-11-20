// AI captaincy (bowling changes, batting order, fields, follow-on, toss) and a
// statistical delivery simulator.
//
// QuickSim is a stand-in for the Unreal ball-physics + batting/bowling input
// pipeline. It lets the server, AI-vs-AI fixtures and tests produce realistic
// deliveries before the 3D game exists. It is NOT the final gameplay.
#pragma once

#include <cstdint>
#include <optional>
#include <random>

#include "cricket/FieldPlacement.h"
#include "cricket/Match.h"
#include "cricket/ReviewAI.h"
#include "cricket/Toss.h"

namespace cricket {

enum class Difficulty { Beginner, Easy, Medium, Hard, WorldClass, Adaptive };
const char* ToString(Difficulty d);

class AutoCaptain {
public:
    explicit AutoCaptain(uint64_t seed, Difficulty d = Difficulty::Medium,
                         ReviewPersonality p = ReviewPersonality::Balanced)
        : rng_(seed), difficulty_(d), personality_(p) {}

    TossChoice ChooseAtToss(const MatchRules& r);
    std::pair<int, int> ChooseOpeners(const Match& m) const;
    int ChooseNextBatter(const Match& m) const;
    int ChooseBowler(const Match& m);  // -1 if nobody can legally bowl
    FieldPreset ChooseFieldPreset(const Match& m, BowlingKind kind) const;
    FieldSetup ChooseLegalField(const Match& m, BowlingKind kind) const;
    bool ShouldEnforceFollowOn(const Match& m);
    bool ShouldDeclare(const Match& m) const;
    // Should `side` review the pending on-field decision? Decides from the
    // players' perception (DecisionCase::perceived) and the match situation
    // only, never from the TV evidence. Implemented in ReviewAI.cpp.
    bool ShouldReview(const Match& m, int side);
    // Takes the new ball as soon as it is due (Test).
    bool ShouldTakeNewBall(const Match& m) const;

private:
    std::mt19937_64 rng_;
    Difficulty difficulty_;
    ReviewPersonality personality_;
};

class QuickSim {
public:
    explicit QuickSim(uint64_t seed) : rng_(seed) {}
    // Produces a legal delivery for the current state of the match.
    Delivery Next(const Match& m);
    // Turns a close delivery into an appeal with replay evidence.
    // PLACEHOLDER: the "evidence" (edge, ball tracking, grounding, no-ball)
    // is random numbers, NOT real ball tracking or edge detection. Server-side
    // ball physics will replace it. Returns nullopt when there is nothing to
    // appeal (the delivery is then bowled normally).
    std::optional<DecisionCase> MakeCase(const Match& m, const Delivery& d);

private:
    LbwEvidence RandomLbw();
    // What each side's players believe, from the truth plus noise and bias
    // (a stand-in for a real perception model; see DRS_DESIGN.md).
    void Perceive(DecisionCase& c);
    double U() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_); }
    std::mt19937_64 rng_;
    int battingSide_ = 0;
};

}  // namespace cricket
