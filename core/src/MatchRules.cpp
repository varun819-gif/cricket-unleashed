#include "cricket/MatchRules.h"

#include <algorithm>
#include <cstdio>

namespace cricket {

DrsRules DrsRules::Icc() { return DrsRules{}; }

DrsRules DrsRules::NoPlayerReviews() {
    DrsRules d;
    d.playerReviews = false;
    d.reviewsPerInnings = 0;
    d.reviewTimerSeconds = 0;
    return d;
}

DrsRules DrsRules::NoTechnology() {
    DrsRules d = NoPlayerReviews();
    d.thirdUmpire = false;
    return d;
}

DrsRules DrsRules::Custom(int reviews, int seconds, bool umpiresCallRetains) {
    DrsRules d;
    d.reviewsPerInnings = std::max(0, reviews);
    d.playerReviews = d.reviewsPerInnings > 0;
    d.reviewTimerSeconds = std::max(0, seconds);
    d.umpiresCallRetainsReview = umpiresCallRetains;
    return d;
}

bool DrsRules::FromName(const std::string& name, DrsRules* out) {
    if (name == "icc") return *out = Icc(), true;
    if (name == "no-reviews") return *out = NoPlayerReviews(), true;
    if (name == "no-tech") return *out = NoTechnology(), true;
    int reviews = 0, seconds = 0;
    char lose[8] = {0};
    int n = std::sscanf(name.c_str(), "custom:%d:%d:%7s", &reviews, &seconds, lose);
    if (n >= 2 && reviews >= 0 && reviews <= 10 && seconds >= 0 && seconds <= 120) {
        *out = Custom(reviews, seconds, std::string(lose) != "lose");
        return true;
    }
    return false;
}

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
