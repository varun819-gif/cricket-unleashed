// Decision Review System (DRS) and third-umpire referrals.
//
// Two ways a close decision reaches the TV umpire:
//   1. PlayerAppeal: the on-field umpire gives OUT or NOT OUT on an appeal for
//      caught or LBW. The side that lost the call may REVIEW it (limited
//      number of unsuccessful reviews per innings).
//   2. UmpireReferral: run-outs, stumpings and clean-catch checks are sent
//      upstairs by the umpires themselves. No team review is used.
//
// In both cases the TV umpire also checks for a front-foot no-ball.
//
// The evidence (edge detection, ball tracking, replays of the crease) is
// produced by the game's physics on the server. This file only judges it.
// All functions here are pure: same input, same answer.
#pragma once

#include <optional>
#include <string>

#include "cricket/Innings.h"
#include "cricket/Lbw.h"
#include "cricket/Types.h"

namespace cricket {

// What the replays show.
enum class Evidence { No, Yes, Inconclusive };
const char* ToString(Evidence e);

enum class DecisionKind { PlayerAppeal, UmpireReferral };

struct DrsEvidence {
    // Checked on every decision (the TV umpire watches the front line).
    bool frontFootNoBall = false;
    // Caught: did the ball touch the bat (or the glove holding the bat)?
    Evidence batContact = Evidence::Yes;
    // Caught: did the ball touch the ground before the fielder controlled it?
    Evidence groundedBeforeCatch = Evidence::No;
    // LBW ball tracking. Required for an LBW appeal. Optional on a caught
    // appeal: if there was no edge, the TV umpire also checks LBW.
    std::optional<LbwEvidence> lbw;
    // Run out / stumping: was the batter's bat or body grounded behind the
    // popping crease before the wicket was put down?
    Evidence batterGrounded = Evidence::No;
};

struct DecisionCase {
    DecisionKind kind = DecisionKind::PlayerAppeal;
    Dismissal mode = Dismissal::Caught;  // the mode of dismissal appealed for
    bool onFieldOut = false;             // on-field umpire's call (PlayerAppeal only)
    // How the ball is scored if the final answer is OUT. dismissal must == mode.
    Delivery ifOut;
    // How the ball is scored if the final answer is NOT OUT. dismissal must be None.
    // (If the batter was given out on the field and that is overturned, the
    // ball was already dead, so its runs are dropped automatically.)
    Delivery ifNotOut;
    DrsEvidence evidence;
};

struct DrsOutcome {
    ReviewOutcome outcome = ReviewOutcome::NotReviewed;
    bool finalOut = false;
    Dismissal finalMode = Dismissal::None;  // can differ from the appeal (caught -> LBW)
    bool noBall = false;                    // front-foot no-ball found
    bool reviewLost = false;                // reviewing side loses one review
    std::string reason;
};

// Caught and LBW can be reviewed by the players.
bool IsPlayerReviewable(Dismissal d);
// Run out, stumped and caught (clean-catch check) can be referred by the umpires.
bool IsUmpireReferable(Dismissal d);

// A team reviewed the on-field decision.
DrsOutcome JudgeReview(const DecisionCase& c);
// The umpires referred it to the TV umpire.
DrsOutcome JudgeReferral(const DecisionCase& c);
// Nobody reviewed: the on-field call stands, except that a front-foot
// no-ball spotted by the TV umpire still cancels a dismissal.
DrsOutcome JudgeWithoutReview(const DecisionCase& c);

// The delivery that actually goes in the scorebook for this outcome.
Delivery FinalDelivery(const DecisionCase& c, const DrsOutcome& o);

// One entry in the match's decision history (for UI, commentary, replays).
struct DecisionRecord {
    int inningsIndex = 0;
    int over = 0;               // 0-based over of the ball
    int ballInOver = 0;         // legal balls already bowled in the over before this ball
    DecisionKind kind = DecisionKind::PlayerAppeal;
    Dismissal appealedFor = Dismissal::None;
    bool onFieldOut = false;
    bool reviewed = false;
    int reviewingSide = -1;
    DrsOutcome result;
    int reviewsLeftAfter = -1;  // reviewing side's reviews left (if reviewed)
};

}  // namespace cricket
