// LBW decision logic (Law 36) and DRS review outcomes.
// Ball tracking (physics) produces an LbwEvidence; this file only judges it.
#pragma once

#include "cricket/Types.h"

namespace cricket {

enum class PitchZone { OutsideLeg, InLine, OutsideOff, Full /* full toss: judge impact only */ };
enum class ImpactZone { InLine, OutsideOff, OutsideLeg };
enum class WicketsZone { Missing, UmpiresCall, Hitting };

struct LbwEvidence {
    bool hitBatFirst = false;
    PitchZone pitched = PitchZone::InLine;
    ImpactZone impact = ImpactZone::InLine;
    bool impactUmpiresCall = false;  // impact marginal (ball straddles the line)
    bool shotOffered = true;
    WicketsZone wickets = WicketsZone::Hitting;
};

// What a perfect umpire would give.
bool LbwIsOut(const LbwEvidence& e);

enum class ReviewOutcome { Upheld, Overturned, UmpiresCall };

struct ReviewResult {
    ReviewOutcome outcome;
    bool finalOut;
    bool reviewLost;  // the reviewing side loses one of its reviews
};

// Review of an on-field LBW decision with ball tracking.
ReviewResult ReviewLbw(bool onFieldOut, const LbwEvidence& e);

}  // namespace cricket
