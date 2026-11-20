#include "cricket/Drs.h"

namespace cricket {

const char* ToString(Evidence e) {
    switch (e) {
        case Evidence::No: return "no";
        case Evidence::Yes: return "yes";
        case Evidence::Inconclusive: return "inconclusive";
    }
    return "?";
}

const char* ToString(ScoringCall c) {
    switch (c) {
        case ScoringCall::None: return "none";
        case ScoringCall::Wide: return "wide";
        case ScoringCall::HeightNoBall: return "height no-ball";
        case ScoringCall::Boundary: return "boundary";
    }
    return "?";
}

const char* ReasonText(ReasonCode r) {
    switch (r) {
        case ReasonCode::OnField: return "on-field decision";
        case ReasonCode::FrontFootNoBall: return "front-foot no-ball";
        case ReasonCode::BallTracking: return "ball tracking";
        case ReasonCode::UmpiresCall: return "umpire's call";
        case ReasonCode::NoTrackingData: return "no ball-tracking data, on-field decision stands";
        case ReasonCode::BallGrounded: return "the ball touched the ground before the catch";
        case ReasonCode::CatchNotConclusive: return "not conclusive that the catch was clean";
        case ReasonCode::EdgeDetected: return "edge detected";
        case ReasonCode::NoEdge: return "no edge";
        case ReasonCode::NoEdgeButLbw: return "no edge, but out LBW";
        case ReasonCode::NoEdgeLbwUmpiresCall: return "no edge; LBW umpire's call, not out";
        case ReasonCode::NoEdgeLbwNotOut: return "no edge; LBW not out";
        case ReasonCode::EdgeInconclusive: return "inconclusive evidence, on-field decision stands";
        case ReasonCode::ShortOfCrease: return "short of the crease";
        case ReasonCode::MadeGround: return "made the ground in time";
        case ReasonCode::BenefitOfDoubt: return "inconclusive, benefit of the doubt to the batter";
        case ReasonCode::NoBatInvolved: return "no bat involved";
        case ReasonCode::CleanCatch: return "clean catch";
        case ReasonCode::WicketBrokenByBatter: return "the batter broke the wicket";
        case ReasonCode::WicketNotBrokenByBatter: return "the batter did not break the wicket";
        case ReasonCode::WilfulObstruction: return "wilful obstruction";
        case ReasonCode::NotWilful: return "obstruction not shown to be wilful";
        case ReasonCode::CallConfirmed: return "replays confirm the call";
        case ReasonCode::CallReversed: return "replays show the call was wrong";
        case ReasonCode::CallInconclusive: return "inconclusive, on-field call stands";
        case ReasonCode::NotReferable: return "not a referable decision";
    }
    return "?";
}

bool IsPlayerReviewable(Dismissal d) {
    return d == Dismissal::Caught || d == Dismissal::Lbw || d == Dismissal::HitWicket;
}

bool IsUmpireReferable(Dismissal d) {
    return d == Dismissal::RunOut || d == Dismissal::Stumped || d == Dismissal::Caught ||
           d == Dismissal::HitWicket || d == Dismissal::ObstructingField;
}

bool IsPlayerReviewable(ScoringCall c) { return c == ScoringCall::Wide || c == ScoringCall::HeightNoBall; }
bool IsUmpireReferable(ScoringCall c) { return c == ScoringCall::Boundary; }

namespace {

// A front-foot no-ball cancels every dismissal except a run out or
// obstructing the field (both possible off any ball, Law 21.18).
bool NoBallSaves(const DecisionCase& c) {
    return c.call == ScoringCall::None && c.evidence.frontFootNoBall && c.mode != Dismissal::RunOut &&
           c.mode != Dismissal::ObstructingField;
}

DrsOutcome WithReason(DrsOutcome o, ReasonCode r) {
    o.code = r;
    o.reason = ReasonText(r);
    return o;
}

// Fill in outcome / reviewLost for a clear verdict on a player review.
DrsOutcome Clear(const DecisionCase& c, bool out, Dismissal mode, ReasonCode r) {
    DrsOutcome o;
    o.finalOut = out;
    o.finalMode = out ? mode : Dismissal::None;
    o.outcome = (out == c.onFieldOut) ? ReviewOutcome::Upheld : ReviewOutcome::Overturned;
    // The side that reviewed was the one the on-field call went against.
    // If the call stands, their review is gone.
    o.reviewLost = o.outcome == ReviewOutcome::Upheld;
    return WithReason(o, r);
}

// Yes -> out, No -> not out, Inconclusive -> `onDoubt`.
bool Verdict(Evidence e, bool onDoubt) { return e == Evidence::Yes ? true : e == Evidence::No ? false : onDoubt; }

DrsOutcome JudgeCall(const DecisionCase& c, ReviewOutcome kindIfClear, bool review) {
    DrsOutcome o;
    const Evidence e = c.evidence.callTrue;
    o.finalCalled = Verdict(e, c.onFieldCalled);
    if (review) {
        o.outcome = o.finalCalled == c.onFieldCalled ? ReviewOutcome::Upheld : ReviewOutcome::Overturned;
        o.reviewLost = o.outcome == ReviewOutcome::Upheld;
    } else {
        o.outcome = kindIfClear;
    }
    ReasonCode r = e == Evidence::Inconclusive ? ReasonCode::CallInconclusive
                   : o.finalCalled == c.onFieldCalled ? ReasonCode::CallConfirmed
                                                      : ReasonCode::CallReversed;
    return WithReason(o, r);
}

}  // namespace

DrsOutcome JudgeReview(const DecisionCase& c) {
    if (c.call != ScoringCall::None) return JudgeCall(c, ReviewOutcome::Upheld, true);
    const DrsEvidence& e = c.evidence;
    if (NoBallSaves(c)) {
        DrsOutcome o = Clear(c, false, Dismissal::None, ReasonCode::FrontFootNoBall);
        o.noBall = true;
        return o;
    }

    switch (c.mode) {
        case Dismissal::Lbw: {
            if (!e.lbw) return Clear(c, c.onFieldOut, c.mode, ReasonCode::NoTrackingData);
            ReviewResult r = ReviewLbw(c.onFieldOut, *e.lbw);
            DrsOutcome o;
            o.outcome = r.outcome;
            o.finalOut = r.finalOut;
            o.finalMode = r.finalOut ? Dismissal::Lbw : Dismissal::None;
            o.reviewLost = r.reviewLost;
            return WithReason(o, r.outcome == ReviewOutcome::UmpiresCall ? ReasonCode::UmpiresCall
                                                                        : ReasonCode::BallTracking);
        }
        case Dismissal::Caught:
            if (e.groundedBeforeCatch == Evidence::Yes) return Clear(c, false, Dismissal::None, ReasonCode::BallGrounded);
            // No soft signal: if the TV umpire can't be sure it was clean, it's not out.
            if (e.groundedBeforeCatch == Evidence::Inconclusive)
                return Clear(c, false, Dismissal::None, ReasonCode::CatchNotConclusive);
            if (e.batContact == Evidence::Yes) return Clear(c, true, Dismissal::Caught, ReasonCode::EdgeDetected);
            if (e.batContact == Evidence::No) {
                // No bat. The TV umpire then checks LBW, with the on-field
                // call for LBW treated as not out.
                if (!e.lbw) return Clear(c, false, Dismissal::None, ReasonCode::NoEdge);
                ReviewResult r = ReviewLbw(false, *e.lbw);
                if (r.finalOut) return Clear(c, true, Dismissal::Lbw, ReasonCode::NoEdgeButLbw);
                return Clear(c, false, Dismissal::None,
                             r.outcome == ReviewOutcome::UmpiresCall ? ReasonCode::NoEdgeLbwUmpiresCall
                                                                     : ReasonCode::NoEdgeLbwNotOut);
            }
            // Edge inconclusive: not enough to overturn the umpire.
            return Clear(c, c.onFieldOut, Dismissal::Caught, ReasonCode::EdgeInconclusive);
        case Dismissal::HitWicket: {
            if (e.wicketBrokenByBatter == Evidence::Inconclusive)
                return Clear(c, c.onFieldOut, Dismissal::HitWicket, ReasonCode::EdgeInconclusive);
            const bool out = e.wicketBrokenByBatter == Evidence::Yes;
            return Clear(c, out, Dismissal::HitWicket,
                         out ? ReasonCode::WicketBrokenByBatter : ReasonCode::WicketNotBrokenByBatter);
        }
        default:
            return Clear(c, c.onFieldOut, c.mode, ReasonCode::OnField);
    }
}

DrsOutcome JudgeReferral(const DecisionCase& c) {
    if (c.call != ScoringCall::None) return JudgeCall(c, ReviewOutcome::ThirdUmpire, false);
    const DrsEvidence& e = c.evidence;
    DrsOutcome o;
    o.outcome = ReviewOutcome::ThirdUmpire;
    o.noBall = e.frontFootNoBall;
    if (NoBallSaves(c)) return WithReason(o, ReasonCode::FrontFootNoBall);
    ReasonCode r = ReasonCode::NotReferable;
    switch (c.mode) {
        case Dismissal::RunOut:
        case Dismissal::Stumped:
            // Doubt goes to the batter.
            o.finalOut = e.batterGrounded == Evidence::No;
            r = e.batterGrounded == Evidence::No    ? ReasonCode::ShortOfCrease
                : e.batterGrounded == Evidence::Yes ? ReasonCode::MadeGround
                                                    : ReasonCode::BenefitOfDoubt;
            break;
        case Dismissal::Caught:
            if (e.batContact != Evidence::Yes) {
                r = ReasonCode::NoBatInvolved;
            } else if (e.groundedBeforeCatch == Evidence::No) {
                o.finalOut = true;
                r = ReasonCode::CleanCatch;
            } else {
                r = e.groundedBeforeCatch == Evidence::Yes ? ReasonCode::BallGrounded : ReasonCode::CatchNotConclusive;
            }
            break;
        case Dismissal::HitWicket:
            o.finalOut = e.wicketBrokenByBatter == Evidence::Yes;
            r = o.finalOut ? ReasonCode::WicketBrokenByBatter
                : e.wicketBrokenByBatter == Evidence::No ? ReasonCode::WicketNotBrokenByBatter
                                                         : ReasonCode::BenefitOfDoubt;
            break;
        case Dismissal::ObstructingField:
            o.finalOut = e.wilfulObstruction == Evidence::Yes;
            r = o.finalOut ? ReasonCode::WilfulObstruction : ReasonCode::NotWilful;
            break;
        default:
            break;
    }
    if (o.finalOut) o.finalMode = c.mode;
    return WithReason(o, r);
}

DrsOutcome JudgeWithoutReview(const DecisionCase& c) {
    DrsOutcome o;
    o.outcome = ReviewOutcome::NotReviewed;
    if (c.call != ScoringCall::None) {
        o.finalCalled = c.onFieldCalled;
        return WithReason(o, ReasonCode::OnField);
    }
    o.noBall = c.evidence.frontFootNoBall;
    if (NoBallSaves(c)) return WithReason(o, ReasonCode::FrontFootNoBall);
    o.finalOut = c.onFieldOut;
    o.finalMode = c.onFieldOut ? c.mode : Dismissal::None;
    return WithReason(o, ReasonCode::OnField);
}

Delivery FinalDelivery(const DecisionCase& c, const DrsOutcome& o) {
    if (c.call != ScoringCall::None) return o.finalCalled ? c.ifCalled : c.ifNotCalled;
    Delivery d;
    if (o.finalOut) {
        d = c.ifOut;
        d.dismissal = o.finalMode;
        if (o.finalMode != c.mode) d.fielder = -1;  // e.g. caught appeal ends as LBW
    } else {
        d = c.ifNotOut;
        d.dismissal = Dismissal::None;
        // Given out on the field: the ball was dead at that moment, so later
        // runs don't count. On a run out the batters' runs completed before
        // the wicket was broken still count (they are all ifNotOut holds).
        if (c.kind == DecisionKind::PlayerAppeal && c.onFieldOut && c.mode != Dismissal::RunOut) {
            d.runs = 0;
            d.shortRuns = 0;
            d.boundary = false;
        }
    }
    if (o.noBall && d.extra != ExtraType::NoBall) {
        // The ball becomes a no-ball; the runs keep their origin (Law 21.12):
        //   off the bat -> striker, byes -> byes, leg-byes -> leg-byes.
        // A ball that was called wide is a no-ball instead (no-ball takes
        // precedence, Law 21.14), and runs completed off it were not off the
        // bat, so they are byes.
        switch (d.extra) {
            case ExtraType::Bye: d.noBallRunsAs = ExtraType::Bye; break;
            case ExtraType::LegBye: d.noBallRunsAs = ExtraType::LegBye; break;
            case ExtraType::Wide: d.noBallRunsAs = ExtraType::Bye; break;
            default: d.noBallRunsAs = ExtraType::None; break;
        }
        d.extra = ExtraType::NoBall;
    }
    return d;
}

}  // namespace cricket
