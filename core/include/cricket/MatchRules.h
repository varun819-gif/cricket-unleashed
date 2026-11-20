// Playing conditions for each format. All format differences live here so the
// rest of the engine never hard-codes "20 overs" etc.
#pragma once

#include <vector>

#include "cricket/Types.h"

namespace cricket {

// Overs are 1-based and inclusive: {1, 6, 2} = overs 1..6 allow 2 outside circle.
struct FieldingPhase {
    int firstOver;
    int lastOver;
    int maxOutsideCircle;
};

struct MatchRules {
    Format format = Format::T20;
    int oversPerInnings = 20;     // 0 = unlimited (Test)
    int ballsPerOver = 6;
    int maxOversPerBowler = 4;    // 0 = no limit
    int inningsPerSide = 1;       // 1 limited overs, 2 Test
    bool freeHitOnNoBall = true;
    int followOnLead = 0;         // 0 = no follow-on
    int matchOverLimit = 0;       // Test: total overs in match before a draw (0 = none)
    int reviewsPerInnings = 2;    // unsuccessful DRS reviews allowed
    int widePenalty = 1;
    int noBallPenalty = 1;
    int squadSize = 11;
    std::vector<FieldingPhase> phases;
    int defaultMaxOutsideCircle = 5;  // -1 = unrestricted

    static MatchRules T20();
    static MatchRules ODI();
    static MatchRules Test(int days = 5);
    // Shortened limited-overs game (handy for testing / quick match).
    static MatchRules Custom(int overs);

    // Max fielders allowed outside the 30-yard circle for a 1-based over number.
    // Returns -1 when there is no restriction.
    int MaxOutsideCircle(int overNumber) const;
    bool IsPowerplay(int overNumber) const;
};

}  // namespace cricket
