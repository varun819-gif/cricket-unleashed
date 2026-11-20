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
    int reviews = 0, seconds = 0, used = 0;
    if (std::sscanf(name.c_str(), "custom:%d:%d%n", &reviews, &seconds, &used) < 2) return false;
    if (reviews < 0 || reviews > 10 || seconds < 0 || seconds > 120) return false;
    bool lose = false, calls = false;
    std::string rest = name.substr(static_cast<size_t>(used));
    while (!rest.empty()) {
        if (rest.rfind(":lose", 0) == 0) lose = true, rest = rest.substr(5);
        else if (rest.rfind(":calls", 0) == 0) calls = true, rest = rest.substr(6);
        else return false;
    }
    *out = Custom(reviews, seconds, !lose);
    out->reviewWidesAndNoBalls = calls;
    return true;
}

MatchRules MatchRules::T20() {
    MatchRules r;
    r.format = Format::T20;
    r.oversPerInnings = 20;
    r.maxOversPerBowler = 4;
    r.phases = {{1, 6, 2}};
    r.defaultMaxOutsideCircle = 5;
    r.weather = {5, 4.25, 10, 275};
    return r;
}

MatchRules MatchRules::ODI() {
    MatchRules r;
    r.format = Format::ODI;
    r.oversPerInnings = 50;
    r.maxOversPerBowler = 10;
    r.phases = {{1, 10, 2}, {11, 40, 4}, {41, 50, 5}};
    r.defaultMaxOutsideCircle = 5;
    r.weather = {20, 4.2, 30, 245};
    return r;
}

MatchRules MatchRules::Test(int days) {
    MatchRules r;
    r.format = Format::Test;
    r.oversPerInnings = 0;
    r.maxOversPerBowler = 0;
    r.inningsPerSide = 2;
    r.freeHitOnNoBall = false;
    r.followOnLead = FollowOnLeadForDays(days);
    r.schedule.enabled = true;
    r.schedule.days = std::max(1, days);
    r.defaultMaxOutsideCircle = -1;
    r.newBallAfterOvers = 80;
    r.weather = {0, 4.0, 0, 0};
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
    r.weather = {std::min(5, r.oversPerInnings), 4.25, 0, 275};
    return r;
}

int MatchRules::FollowOnLeadForDays(int days) {
    if (days >= 5) return 200;
    if (days >= 3) return 150;
    if (days == 2) return 100;
    return 75;
}

MatchRules MatchRules::Scaled(int overs) const {
    MatchRules r = *this;
    if (oversPerInnings <= 0 || overs <= 0) return r;
    const int full = oversPerInnings;
    r.oversPerInnings = overs;
    if (maxOversPerBowler > 0) r.maxOversPerBowler = std::max(1, (overs + 4) / 5);
    // Each phase keeps its share of the innings: boundaries scale with the
    // overs and are rounded to the nearest over (at least 1 over long).
    r.phases.clear();
    int prevLast = 0;
    for (const auto& p : phases) {
        int last = static_cast<int>(static_cast<double>(p.lastOver) * overs / full + 0.5);
        last = std::min(overs, std::max(prevLast + 1, last));
        if (prevLast >= overs) break;
        r.phases.push_back({prevLast + 1, last, p.maxOutsideCircle});
        prevLast = last;
    }
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
