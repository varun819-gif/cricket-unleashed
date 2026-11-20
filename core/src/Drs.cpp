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

bool IsPlayerReviewable(Dismissal d) { return d == Dismissal::Caught || d == Dismissal::Lbw; }

bool IsUmpireReferable(Dismissal d) {
    return d == Dismissal::RunOut || d == Dismissal::Stumped || d == Dismissal::Caught;
}

namespace {

// A front-foot no-ball cancels every dismissal except a run out
// (and the other "any ball" modes, which are not appealed here).
bool NoBallSaves(const DecisionCase& c) {
    return c.evidence.frontFootNoBall && c.mode != Dismissal::RunOut;
}

// Fill in outcome / reviewLost for a clear verdict on a player review.
DrsOutcome Clear(const DecisionCase& c, bool out, Dismissal mode, std::string reason) {
    DrsOutcome o;
    o.finalOut = out;
    o.finalMode = out ? mode : Dismissal::None;
    o.outcome = (out == c.onFieldOut) ? ReviewOutcome::Upheld : ReviewOutcome::Overturned;
    // The side that reviewed was the one the on-field call went against.
    // If the call stands, their review is gone.
    o.reviewLost = o.outcome == ReviewOutcome::Upheld;
    o.reason = std::move(reason);
    return o;
}

}  // namespace

DrsOutcome JudgeReview(const DecisionCase& c) {
    const DrsEvidence& e = c.evidence;
    if (NoBallSaves(c)) {
        DrsOutcome o = Clear(c, false, Dismissal::None, "front-foot no-ball");
        o.noBall = true;
        return o;
    }

    DrsOutcome o;
    if (c.mode == Dismissal::Lbw) {
        if (!e.lbw) return Clear(c, c.onFieldOut, c.mode, "no ball-tracking data, on-field decision stands");
        ReviewResult r = ReviewLbw(c.onFieldOut, *e.lbw);
        o.outcome = r.outcome;
        o.finalOut = r.finalOut;
        o.finalMode = r.finalOut ? Dismissal::Lbw : Dismissal::None;
        o.reviewLost = r.reviewLost;
        o.reason = r.outcome == ReviewOutcome::UmpiresCall ? "umpire's call" : "ball tracking";
    } else if (c.mode == Dismissal::Caught) {
        if (e.groundedBeforeCatch == Evidence::Yes) {
            o = Clear(c, false, Dismissal::None, "the ball touched the ground before the catch");
        } else if (e.groundedBeforeCatch == Evidence::Inconclusive) {
            // No soft signal: if the TV umpire can't be sure it was clean, it's not out.
            o = Clear(c, false, Dismissal::None, "not conclusive that the catch was clean");
        } else if (e.batContact == Evidence::Yes) {
            o = Clear(c, true, Dismissal::Caught, "edge detected");
        } else if (e.batContact == Evidence::No) {
            // No bat. The TV umpire then checks LBW, with the on-field call
            // for LBW treated as not out.
            if (e.lbw) {
                ReviewResult r = ReviewLbw(false, *e.lbw);
                if (r.finalOut) return Clear(c, true, Dismissal::Lbw, "no edge, but out LBW");
                o = Clear(c, false, Dismissal::None,
                          r.outcome == ReviewOutcome::UmpiresCall ? "no edge; LBW umpire's call, not out"
                                                                   : "no edge; LBW not out");
            } else {
                o = Clear(c, false, Dismissal::None, "no edge");
            }
        } else {
            // Edge inconclusive: not enough to overturn the umpire.
            o = Clear(c, c.onFieldOut, Dismissal::Caught, "inconclusive evidence, on-field decision stands");
        }
    } else {
        o = Clear(c, c.onFieldOut, c.mode, "not reviewable, on-field decision stands");
    }
    return o;
}

DrsOutcome JudgeReferral(const DecisionCase& c) {
    const DrsEvidence& e = c.evidence;
    DrsOutcome o;
    o.outcome = ReviewOutcome::ThirdUmpire;
    o.noBall = e.frontFootNoBall;
    if (NoBallSaves(c)) {
        o.reason = "front-foot no-ball";
        return o;
    }
    switch (c.mode) {
        case Dismissal::RunOut:
        case Dismissal::Stumped:
            if (e.batterGrounded == Evidence::No) {
                o.finalOut = true;
                o.reason = "short of the crease";
            } else {
                o.reason = e.batterGrounded == Evidence::Yes ? "made the ground in time"
                                                              : "inconclusive, benefit of the doubt to the batter";
            }
            break;
        case Dismissal::Caught:
            if (e.batContact != Evidence::Yes) {
                o.reason = "no bat involved";
            } else if (e.groundedBeforeCatch == Evidence::No) {
                o.finalOut = true;
                o.reason = "clean catch";
            } else {
                o.reason = e.groundedBeforeCatch == Evidence::Yes ? "the ball touched the ground"
                                                                   : "not conclusive that the catch was clean";
            }
            break;
        default:
            o.reason = "not a referable decision";
            break;
    }
    if (o.finalOut) o.finalMode = c.mode;
    return o;
}

DrsOutcome JudgeWithoutReview(const DecisionCase& c) {
    DrsOutcome o;
    o.outcome = ReviewOutcome::NotReviewed;
    o.noBall = c.evidence.frontFootNoBall;
    if (NoBallSaves(c)) {
        o.reason = "front-foot no-ball";
        return o;
    }
    o.finalOut = c.onFieldOut;
    o.finalMode = c.onFieldOut ? c.mode : Dismissal::None;
    o.reason = "on-field decision";
    return o;
}

Delivery FinalDelivery(const DecisionCase& c, const DrsOutcome& o) {
    Delivery d;
    if (o.finalOut) {
        d = c.ifOut;
        d.dismissal = o.finalMode;
        if (o.finalMode != c.mode) d.fielder = -1;  // e.g. caught appeal ends as LBW
    } else {
        d = c.ifNotOut;
        d.dismissal = Dismissal::None;
        if (c.kind == DecisionKind::PlayerAppeal && c.onFieldOut) {
            // Given out on the field: the ball was dead at that moment.
            d.runs = 0;
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
