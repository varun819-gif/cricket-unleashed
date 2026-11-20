#include "cricket/RainRule.h"

#include <cmath>

namespace cricket {

namespace {
double Z(double u, int w, const RainRuleParams& p) {
    if (w >= 10 || u <= 0) return 0.0;
    if (w < 0) w = 0;
    const double f = p.wicketFactor[w];
    return f * (1.0 - std::exp(-p.decay * u / f));
}
}  // namespace

double RainResources(double oversLeft, int wicketsLost, const RainRuleParams& p) {
    return 100.0 * Z(oversLeft, wicketsLost, p) / Z(50.0, 0, p);
}

double RainPar(int firstInnings, double r1, double r2, double g) {
    if (r1 <= 0) return firstInnings;
    if (r2 <= r1) return firstInnings * r2 / r1;
    return firstInnings + g * (r2 - r1) / 100.0;
}

int RainTarget(int firstInnings, double r1, double r2, double g) {
    // A tiny epsilon keeps exact integers (e.g. equal resources) exact.
    return static_cast<int>(std::floor(RainPar(firstInnings, r1, r2, g) + 1e-9)) + 1;
}

}  // namespace cricket
