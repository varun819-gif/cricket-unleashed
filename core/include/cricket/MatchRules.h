// Playing conditions for each format. All format differences live here so the
// rest of the engine never hard-codes "20 overs" etc.
#pragma once

#include <string>
#include <vector>

#include "cricket/Types.h"

namespace cricket {

// Overs are 1-based and inclusive: {1, 6, 2} = overs 1..6 allow 2 outside circle.
struct FieldingPhase {
    int firstOver;
    int lastOver;
    int maxOutsideCircle;
};

// Decision Review System settings. These vary by competition, so they are
// data, not code. Pick a preset or build a custom one.
struct DrsRules {
    bool playerReviews = true;             // teams may review on-field decisions
    bool thirdUmpire = true;               // TV umpire: run outs, stumpings, clean catches, no-ball checks
    int reviewsPerInnings = 2;             // UNSUCCESSFUL reviews allowed per side per innings
    bool umpiresCallRetainsReview = true;  // "umpire's call" does not cost a review
    int reviewTimerSeconds = 15;           // time to ask for a review; 0 = no timer
    bool reviewWidesAndNoBalls = false;    // players may review wide / height no-ball calls

    // International standard playing conditions (2 per innings, umpire's call
    // keeps the review, 15-second timer).
    static DrsRules Icc();
    // TV umpire available, but no player reviews (many domestic matches).
    static DrsRules NoPlayerReviews();
    // No technology at all: on-field umpires decide everything.
    static DrsRules NoTechnology();
    static DrsRules Custom(int reviewsPerInnings, int reviewTimerSeconds, bool umpiresCallRetainsReview = true);
    // "icc", "no-reviews", "no-tech" or "custom:<reviews>:<seconds>[:lose][:calls]"
    // (":lose" = umpire's call costs a review, ":calls" = wides / height no-balls reviewable).
    static bool FromName(const std::string& name, DrsRules* out);
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
    DrsRules drs = DrsRules::Icc();
    int widePenalty = 1;
    int noBallPenalty = 1;
    int squadSize = 11;
    std::vector<FieldingPhase> phases;
    int defaultMaxOutsideCircle = 5;  // -1 = unrestricted
    int newBallAfterOvers = 0;    // Test: 80 (Law 4.3 / playing conditions); 0 = never

    static MatchRules T20();
    static MatchRules ODI();
    static MatchRules Test(int days = 5);
    // Shortened limited-overs game (handy for testing / quick match).
    static MatchRules Custom(int overs);

    // Max fielders allowed outside the 30-yard circle for a 1-based over number.
    // Returns -1 when there is no restriction.
    int MaxOutsideCircle(int overNumber) const;
    // The same limited-overs rules for an innings of `overs` overs (rain):
    // bowler limit = one fifth of the overs rounded up; fielding phases
    // shrink in proportion (see docs/RAIN_RULE.md for the approximation).
    MatchRules Scaled(int overs) const;
    bool IsPowerplay(int overNumber) const;
};

}  // namespace cricket
