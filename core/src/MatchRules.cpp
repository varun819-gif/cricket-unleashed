#include "cricket/MatchRules.h"

#include <algorithm>

namespace cricket {

MatchRules MatchRules::T20() {
    MatchRules r;
    r.format = Format::T20;
    r.oversPerInnings = 20;
    r.maxOversPerBowler = 4;
    r.phases = {{1, 6, 2}};
    r.defaultMaxOutsideCircle = 5;
    return r;
}

MatchRules MatchRules::ODI() {
    MatchRules r;
    r.format = Format::ODI;
    r.oversPerInnings = 50;
    r.maxOversPerBowler = 10;
    r.phases = {{1, 10, 2}, {11, 40, 4}, {41, 50, 5}};
    r.defaultMaxOutsideCircle = 5;
    return r;
}

MatchRules MatchRules::Test(int days) {
    MatchRules r;
    r.format = Format::Test;
    r.oversPerInnings = 0;
    r.maxOversPerBowler = 0;
    r.inningsPerSide = 2;
    r.freeHitOnNoBall = false;
    r.followOnLead = days >= 5 ? 200 : days >= 3 ? 150 : 100;
    r.matchOverLimit = days * 90;
    r.reviewsPerInnings = 2;
    r.defaultMaxOutsideCircle = -1;
    return r;
}

MatchRules MatchRules::Custom(int overs) {
    MatchRules r;
    r.format = Format::Custom;
    r.oversPerInnings = std::max(1, overs);
    // Bowler quota: one fifth of the overs, rounded up (same as T20/ODI).
    r.maxOversPerBowler = (r.oversPerInnings + 4) / 5;
    // Powerplay ~30% of the innings, like T20.
    int pp = std::max(1, (r.oversPerInnings * 3 + 9) / 10);
    r.phases = {{1, pp, 2}};
    r.defaultMaxOutsideCircle = 5;
    return r;
}

int MatchRules::MaxOutsideCircle(int overNumber) const {
    for (const auto& p : phases) {
        if (overNumber >= p.firstOver && overNumber <= p.lastOver) return p.maxOutsideCircle;
    }
    return defaultMaxOutsideCircle;
}

bool MatchRules::IsPowerplay(int overNumber) const {
    int limit = MaxOutsideCircle(overNumber);
    return limit >= 0 && limit <= 2;
}

}  // namespace cricket
