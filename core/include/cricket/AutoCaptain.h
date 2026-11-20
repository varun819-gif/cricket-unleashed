// AI captaincy (bowling changes, batting order, fields, follow-on, toss) and a
// statistical delivery simulator.
//
// QuickSim is a stand-in for the Unreal ball-physics + batting/bowling input
// pipeline. It lets the server, AI-vs-AI fixtures and tests produce realistic
// deliveries before the 3D game exists. It is NOT the final gameplay.
#pragma once

#include <cstdint>
#include <random>

#include "cricket/FieldPlacement.h"
#include "cricket/Match.h"
#include "cricket/Toss.h"

namespace cricket {

enum class Difficulty { Beginner, Easy, Medium, Hard, WorldClass, Adaptive };
const char* ToString(Difficulty d);

class AutoCaptain {
public:
    explicit AutoCaptain(uint64_t seed, Difficulty d = Difficulty::Medium) : rng_(seed), difficulty_(d) {}

    TossChoice ChooseAtToss(const MatchRules& r);
    std::pair<int, int> ChooseOpeners(const Match& m) const;
    int ChooseNextBatter(const Match& m) const;
    int ChooseBowler(const Match& m);  // -1 if nobody can legally bowl
    FieldPreset ChooseFieldPreset(const Match& m, BowlingKind kind) const;
    FieldSetup ChooseLegalField(const Match& m, BowlingKind kind) const;
    bool ShouldEnforceFollowOn(const Match& m);
    bool ShouldDeclare(const Match& m) const;

private:
    std::mt19937_64 rng_;
    Difficulty difficulty_;
};

class QuickSim {
public:
    explicit QuickSim(uint64_t seed) : rng_(seed) {}
    // Produces a legal delivery for the current state of the match.
    Delivery Next(const Match& m);

private:
    double U() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_); }
    std::mt19937_64 rng_;
};

}  // namespace cricket
