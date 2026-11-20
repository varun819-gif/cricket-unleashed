// "CU Rain Rule" - Cricket Unleashed's OWN, NON-OFFICIAL method for revising
// targets in rain-affected limited-overs matches.
//
// It is NOT the official Duckworth-Lewis-Stern (DLS) method and does NOT use
// the ICC's DLS tables or software. It follows the publicly described idea
// behind resource-based methods - a team's run-scoring "resources" depend on
// overs left and wickets lost, modelled with an exponential curve - but every
// number here was chosen by us (see docs/RAIN_RULE.md). Results will differ
// from official DLS targets. Labelled "(rain rule)" wherever it is shown.
#pragma once

namespace cricket {

struct RainRuleParams {
    double decay = 0.0272;   // per over, with no wickets lost
    // Share of the long-run resources left with w wickets lost (w = 0..9),
    // calibrated by us so that 50 overs left with w wickets down gives
    // 100, 93, 85, 75, 63, 50, 36, 23, 12, 5 percent.
    double wicketFactor[10] = {1.0, 0.8777, 0.7577, 0.6304, 0.5016, 0.3826, 0.2693, 0.171, 0.0892, 0.0372};
};

// Resources left (percent of a full 50-over innings with no wickets lost)
// with `oversLeft` overs (fractions allowed) and `wicketsLost` wickets down.
double RainResources(double oversLeft, int wicketsLost, const RainRuleParams& p = {});

// Target for the side batting second: their resources r2 vs the first side's
// r1 (both percent). S = first innings total. G = the score a full 50-over
// innings is worth on average in this format (used only when r2 > r1).
//   r2 <= r1: par = S * r2 / r1
//   r2 >  r1: par = S + G * (r2 - r1) / 100
// The target is par rounded down, plus one.
double RainPar(int firstInnings, double r1, double r2, double g);
int RainTarget(int firstInnings, double r1, double r2, double g);

}  // namespace cricket
