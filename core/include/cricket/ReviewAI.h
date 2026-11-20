// AI decision: should a captain use a DRS review?
//
// Information hiding: the AI decides ONLY from
//   * what its own players believe happened (DecisionCase::perceived), which
//     the server's perception model produces from what they could see, and
//   * the visible match situation (reviews left, overs left, batter quality...).
// It never reads DrsEvidence or calls the TV-umpire judge functions. A test
// scans ReviewAI.cpp to enforce this (see DrsAiTests.cpp).
#pragma once

#include <random>

namespace cricket {

class Match;

enum class ReviewPersonality { Cautious, Balanced, Gambler };
const char* ToString(ReviewPersonality p);

struct ReviewContext {
    bool isDismissal = true;        // false = wide / height no-ball call
    bool battingSideReviewing = true;
    double successBelief = 0.5;     // reviewing side's belief that the on-field call is wrong
    int reviewsLeft = 0;
    int reviewsPerInnings = 2;
    bool limitedOvers = true;
    int oversLeft = -1;             // overs left in the innings (-1 = unlimited)
    int wicketsDown = 0;
    int batterQuality = 50;         // batting skill (0-100) of the batter involved
    int batterRuns = 0;
    int runsAtStake = 1;            // scoring calls: runs (incl. extra ball) the call is worth
};

// Builds the context for `side` from the pending decision. Uses the
// perception values and public match state only.
ReviewContext BuildReviewContext(const Match& m, int side);

// Expected-value rule: review when
//   belief x value-of-success  >  (1 - belief) x cost-of-losing-a-review x personality
// `judgementNoise` (standard deviation) blurs the belief: lower = better judge.
bool DecideReview(const ReviewContext& c, ReviewPersonality p, double judgementNoise, std::mt19937_64& rng);

}  // namespace cricket
