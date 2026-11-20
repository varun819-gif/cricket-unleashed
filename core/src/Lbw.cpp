#include "cricket/Lbw.h"

namespace cricket {

bool LbwIsOut(const LbwEvidence& e) {
    if (e.hitBatFirst) return false;
    if (e.pitched == PitchZone::OutsideLeg) return false;
    if (e.impact == ImpactZone::OutsideLeg) return false;
    if (e.impact == ImpactZone::OutsideOff && e.shotOffered) return false;
    return e.wickets != WicketsZone::Missing;
}

ReviewResult ReviewLbw(bool onFieldOut, const LbwEvidence& e) {
    // Clear-cut "not out" reasons override everything.
    const bool clearlyNotOut = e.hitBatFirst || e.pitched == PitchZone::OutsideLeg ||
                               e.impact == ImpactZone::OutsideLeg ||
                               (e.impact == ImpactZone::OutsideOff && e.shotOffered && !e.impactUmpiresCall) ||
                               e.wickets == WicketsZone::Missing;
    if (clearlyNotOut) {
        if (onFieldOut) return {ReviewOutcome::Overturned, false, false};
        return {ReviewOutcome::Upheld, false, true};
    }
    const bool marginal = e.impactUmpiresCall || e.wickets == WicketsZone::UmpiresCall;
    if (marginal) return {ReviewOutcome::UmpiresCall, onFieldOut, false};  // review retained
    // Clearly out.
    if (onFieldOut) return {ReviewOutcome::Upheld, true, true};
    return {ReviewOutcome::Overturned, true, false};
}

}  // namespace cricket
