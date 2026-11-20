// AI review decisions. This file must never look at the TV umpire's
// replay data - only at players' perceptions and the public match state.
// A test enforces this by scanning this file for forbidden names.
#include "cricket/ReviewAI.h"

#include <algorithm>
#include <cmath>

#include "cricket/AutoCaptain.h"
#include "cricket/Match.h"

namespace cricket {

const char* ToString(ReviewPersonality p) {
    switch (p) {
        case ReviewPersonality::Cautious: return "cautious";
        case ReviewPersonality::Balanced: return "balanced";
        case ReviewPersonality::Gambler: return "gambler";
    }
    return "?";
}

ReviewContext BuildReviewContext(const Match& m, int side) {
    ReviewContext ctx;
    const DecisionCase* c = m.PendingDecision();
    if (!c) return ctx;
    const Innings& inn = m.Current();
    const double believesTrue = std::clamp(c->perceived.at(side), 0.0, 1.0);  // P(out) or P(call true)
    ctx.isDismissal = c->call == ScoringCall::None;
    const bool onField = ctx.isDismissal ? c->onFieldOut : c->onFieldCalled;
    ctx.successBelief = onField ? 1.0 - believesTrue : believesTrue;
    ctx.battingSideReviewing = side == m.BattingSide();
    ctx.reviewsLeft = m.ReviewsRemaining(side);
    ctx.reviewsPerInnings = std::max(1, m.Rules().drs.reviewsPerInnings);
    ctx.limitedOvers = inn.MaxOvers() > 0;
    ctx.oversLeft = ctx.limitedOvers ? inn.MaxOvers() - inn.CompletedOvers() : -1;
    ctx.wicketsDown = inn.Wickets();
    const int batter = inn.Striker();
    if (batter >= 0) {
        ctx.batterQuality = m.GetTeam(m.BattingSide()).xi.at(batter).batting;
        ctx.batterRuns = inn.Batter(batter).runs;
    }
    if (!ctx.isDismissal) {
        auto worth = [](const Delivery& d) {
            int w = d.runs - d.shortRuns;
            if (d.extra == ExtraType::Wide || d.extra == ExtraType::NoBall) w += 2;  // penalty + extra ball
            return w;
        };
        ctx.runsAtStake = std::max(1, std::abs(worth(c->ifCalled) - worth(c->ifNotCalled)));
    }
    return ctx;
}

bool DecideReview(const ReviewContext& c, ReviewPersonality p, double judgementNoise, std::mt19937_64& rng) {
    if (c.reviewsLeft <= 0) return false;
    std::normal_distribution<double> noise(0.0, std::max(0.0, judgementNoise));
    const double belief = std::clamp(c.successBelief + (judgementNoise > 0 ? noise(rng) : 0.0), 0.0, 1.0);

    // What a successful review is worth.
    double value;
    if (c.isDismissal) {
        value = 1.0 + c.batterQuality / 50.0 + std::min(c.batterRuns, 100) / 50.0;  // 1 .. 5
        if (c.wicketsDown >= 8) value *= 0.75;                                      // tail-ender
        if (c.limitedOvers && c.oversLeft >= 0 && c.oversLeft <= 2) value *= 0.7;   // little time left to matter
    } else {
        value = 0.4 * c.runsAtStake;
    }

    // What losing a review costs: scarcer and earlier = dearer.
    double cost = 1.5 * static_cast<double>(c.reviewsPerInnings) / c.reviewsLeft;
    if (c.limitedOvers && c.oversLeft >= 0) cost *= std::clamp(c.oversLeft / 20.0, 0.3, 1.5);
    else if (!c.limitedOvers) cost *= 1.5;  // long Test innings ahead
    switch (p) {
        case ReviewPersonality::Cautious: cost *= 1.4; break;
        case ReviewPersonality::Balanced: break;
        case ReviewPersonality::Gambler: cost *= 0.6; break;
    }
    return belief * value > (1.0 - belief) * cost;
}

// AutoCaptain's entry point lives here so that every line of the AI review
// path is covered by the information-hiding scan.
bool AutoCaptain::ShouldReview(const Match& m, int side) {
    if (!m.HasPendingDecision() || m.ReviewEntitledSide() != side || m.ReviewsRemaining(side) <= 0) return false;
    double noise = 0.12;
    switch (difficulty_) {
        case Difficulty::Beginner: noise = 0.30; break;
        case Difficulty::Easy: noise = 0.20; break;
        case Difficulty::Medium: noise = 0.12; break;
        case Difficulty::Hard: noise = 0.07; break;
        case Difficulty::WorldClass: noise = 0.03; break;
        case Difficulty::Adaptive: noise = 0.10; break;
    }
    return DecideReview(BuildReviewContext(m, side), personality_, noise, rng_);
}

}  // namespace cricket
