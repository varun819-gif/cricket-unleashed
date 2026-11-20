// Decision Review System (DRS) and third-umpire referrals.
//
// Ways a close call reaches the TV umpire:
//   1. PlayerAppeal on a DISMISSAL (caught, LBW, hit wicket): the on-field
//      umpire gives OUT / NOT OUT and the side that lost the call may REVIEW
//      it (limited number of unsuccessful reviews per innings).
//   2. UmpireReferral on a DISMISSAL (run out, stumping, clean catch, hit
//      wicket, obstructing the field): the umpires send it upstairs. No team
//      review is used.
//   3. SCORING CALLS (no dismissal): wide and above-waist ("height") no-ball
//      calls may be reviewed by players when the competition allows it
//      (DrsRules::reviewWidesAndNoBalls); boundary checks (four or six?
//      did the fielder touch the rope?) are umpire referrals.
//
// On dismissal decisions the TV umpire also checks for a front-foot no-ball.
//
// IMPORTANT: the evidence (edge detection, ball tracking, replays) is a
// SIMULATED PLACEHOLDER today (QuickSim::MakeCase rolls dice). It is not real
// DRS technology (no Hawk-Eye / UltraEdge or similar). Later it will come from
// the game's own server-side ball physics. This file only judges evidence.
// All Judge* functions are pure: same input, same answer.
#pragma once

#include <array>
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

// A non-dismissal call that can go to the TV umpire.
enum class ScoringCall {
    None,          // this case is about a dismissal
    Wide,          // "is it a wide?"
    HeightNoBall,  // "was it a full toss above waist height?" (Law 41.7)
    Boundary,      // "is it the higher boundary outcome?" (six vs four, four vs runs)
};
const char* ToString(ScoringCall c);

struct DrsEvidence {
    // Checked on every dismissal decision (the TV umpire watches the front line).
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
    // Hit wicket (Law 35): did the batter (or their bat/equipment) break the
    // wicket while playing at the ball or setting off for a first run?
    Evidence wicketBrokenByBatter = Evidence::Yes;
    // Obstructing the field (Law 37): was the obstruction wilful?
    Evidence wilfulObstruction = Evidence::Yes;
    // Scoring calls: did the called event happen? (it IS a wide / it WAS
    // above waist height / it IS the higher boundary outcome)
    Evidence callTrue = Evidence::Inconclusive;
};

// Why a decision went the way it did. Used to explain it in any language.
enum class ReasonCode {
    OnField,
    FrontFootNoBall,
    BallTracking,
    UmpiresCall,
    NoTrackingData,
    BallGrounded,
    CatchNotConclusive,
    EdgeDetected,
    NoEdge,
    NoEdgeButLbw,
    NoEdgeLbwUmpiresCall,
    NoEdgeLbwNotOut,
    EdgeInconclusive,
    ShortOfCrease,
    MadeGround,
    BenefitOfDoubt,
    NoBatInvolved,
    CleanCatch,
    WicketBrokenByBatter,
    WicketNotBrokenByBatter,
    WilfulObstruction,
    NotWilful,
    CallConfirmed,
    CallReversed,
    CallInconclusive,
    NotReferable,
};
// English explanation (the Hindi version lives in the commentary engine).
const char* ReasonText(ReasonCode r);

struct DecisionCase {
    DecisionKind kind = DecisionKind::PlayerAppeal;
    // ---- dismissal cases (call == None) --------------------------------
    Dismissal mode = Dismissal::Caught;  // the mode of dismissal appealed for
    // On-field umpire's call. For a PlayerAppeal this is what may be reviewed.
    // For an UmpireReferral it is only used when the competition has no third
    // umpire (DrsRules::thirdUmpire == false); then it is final.
    bool onFieldOut = false;
    // How the ball is scored if the final answer is OUT. dismissal must == mode.
    Delivery ifOut;
    // How the ball is scored if the final answer is NOT OUT. dismissal must be None.
    // If the batter was given OUT on the field and that is overturned, the
    // ball was dead from that moment: its runs are dropped (except on a run
    // out, where `ifNotOut.runs` should hold only the runs completed before
    // the wicket was broken).
    Delivery ifNotOut;
    // ---- scoring-call cases (call != None) -----------------------------
    ScoringCall call = ScoringCall::None;
    bool onFieldCalled = false;  // the umpire signalled it (wide / no-ball / the higher boundary)
    Delivery ifCalled;           // scoring if the call is (or stays) made
    Delivery ifNotCalled;        // scoring if it is not
    // ---- shared ----------------------------------------------------------
    DrsEvidence evidence;
    // What each side's players THINK happened, as a probability that the
    // batter is out (dismissal) or that the call is true (scoring call).
    // Produced by the server's perception model from what they could see,
    // never copied from the evidence. The AI captain decides from this alone.
    std::array<double, 2> perceived{0.5, 0.5};
};

struct DrsOutcome {
    ReviewOutcome outcome = ReviewOutcome::NotReviewed;
    bool finalOut = false;
    Dismissal finalMode = Dismissal::None;  // can differ from the appeal (caught -> LBW)
    bool finalCalled = false;               // scoring calls only
    bool noBall = false;                    // front-foot no-ball found
    bool reviewLost = false;                // reviewing side loses one review
    ReasonCode code = ReasonCode::OnField;
    std::string reason;                     // ReasonText(code)
};

// Caught, LBW and hit wicket can always be reviewed by the players. Run outs
// and stumpings too when the competition allows it (see the overload below).
bool IsPlayerReviewable(Dismissal d);
bool IsPlayerReviewable(Dismissal d, bool runOutsAndStumpingsAllowed);
// Run out, stumped, caught (clean catch), hit wicket, obstructing the field.
bool IsUmpireReferable(Dismissal d);
// Wide and height no-ball calls may be reviewed (if the ruleset allows);
// boundary checks are referrals.
bool IsPlayerReviewable(ScoringCall c);
bool IsUmpireReferable(ScoringCall c);

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
    ScoringCall call = ScoringCall::None;
    bool onFieldCalled = false;
    bool reviewed = false;
    int reviewingSide = -1;
    DrsOutcome result;
    int reviewsLeftAfter = -1;  // reviewing side's reviews left (if reviewed)
};

}  // namespace cricket
