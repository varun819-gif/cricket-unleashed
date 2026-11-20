#include "cricket/Innings.h"

#include <cstdio>
#include <utility>

namespace cricket {

const char* ToString(Dismissal d) {
    switch (d) {
        case Dismissal::None: return "not out";
        case Dismissal::Bowled: return "bowled";
        case Dismissal::Caught: return "caught";
        case Dismissal::Lbw: return "lbw";
        case Dismissal::Stumped: return "stumped";
        case Dismissal::RunOut: return "run out";
        case Dismissal::HitWicket: return "hit wicket";
        case Dismissal::ObstructingField: return "obstructing the field";
        case Dismissal::HitBallTwice: return "hit the ball twice";
        case Dismissal::TimedOut: return "timed out";
        case Dismissal::RetiredOut: return "retired out";
    }
    return "?";
}

const char* ToString(ExtraType e) {
    switch (e) {
        case ExtraType::None: return "";
        case ExtraType::Wide: return "wide";
        case ExtraType::NoBall: return "no-ball";
        case ExtraType::Bye: return "bye";
        case ExtraType::LegBye: return "leg-bye";
    }
    return "?";
}

bool CreditedToBowler(Dismissal d) {
    switch (d) {
        case Dismissal::Bowled:
        case Dismissal::Caught:
        case Dismissal::Lbw:
        case Dismissal::Stumped:
        case Dismissal::HitWicket:
            return true;
        default:
            return false;
    }
}

Innings::Innings(const MatchRules& rules, int battingSide, int index, std::optional<int> target)
    : rules_(&rules),
      battingSide_(battingSide),
      index_(index),
      target_(target),
      batters_(rules.squadSize),
      bowlers_(rules.squadSize) {}

bool Innings::DismissalAllowed(Dismissal d, ExtraType e, bool freeHit) {
    if (d == Dismissal::None) return true;
    // Timed out / retired out never happen "off a delivery".
    if (d == Dismissal::TimedOut || d == Dismissal::RetiredOut) return false;
    // Always possible: run out and obstructing the field.
    if (d == Dismissal::RunOut || d == Dismissal::ObstructingField) return true;

    switch (e) {
        case ExtraType::Wide:
            // Law 22: stumped / hit wicket possible off a wide, but not on a free hit.
            return !freeHit && (d == Dismissal::Stumped || d == Dismissal::HitWicket);
        case ExtraType::NoBall:
            return d == Dismissal::HitBallTwice;
        case ExtraType::None:
        case ExtraType::Bye:
        case ExtraType::LegBye:
            if (freeHit) return d == Dismissal::HitBallTwice;
            if (e == ExtraType::LegBye && (d == Dismissal::Caught || d == Dismissal::Stumped)) return false;
            if (e == ExtraType::Bye && d == Dismissal::Caught) return false;
            return true;
    }
    return false;
}

InningsState Innings::State() const {
    if (end_ != InningsEnd::None) return InningsState::Complete;
    if (!openersSet_) return InningsState::AwaitingOpeners;
    if (needBatter_) return InningsState::AwaitingBatter;
    if (bowler_ < 0) return InningsState::AwaitingBowler;
    return InningsState::InPlay;
}

Status Innings::SetOpeners(int striker, int nonStriker) {
    if (openersSet_) return Status::Err("openers already set");
    const int n = rules_->squadSize;
    if (striker < 0 || striker >= n || nonStriker < 0 || nonStriker >= n)
        return Status::Err("opener index out of range");
    if (striker == nonStriker) return Status::Err("openers must be different players");
    striker_ = striker;
    nonStriker_ = nonStriker;
    batters_[striker].batted = batters_[nonStriker].batted = true;
    openersSet_ = true;
    return Status::Ok();
}

bool Innings::CanBowlNextOver(int bowler) const {
    if (bowler < 0 || bowler >= rules_->squadSize) return false;
    if (bowler == lastOverBowler_) return false;  // no consecutive overs
    if (rules_->maxOversPerBowler > 0 &&
        bowlers_[bowler].legalBalls / rules_->ballsPerOver >= rules_->maxOversPerBowler)
        return false;
    return true;
}

Status Innings::SetBowler(int bowler) {
    if (IsComplete()) return Status::Err("innings is complete");
    if (!openersSet_) return Status::Err("set openers first");
    if (bowler_ >= 0) return Status::Err("an over is already in progress");
    if (bowler < 0 || bowler >= rules_->squadSize) return Status::Err("bowler index out of range");
    if (bowler == lastOverBowler_) return Status::Err("a bowler cannot bowl consecutive overs");
    if (!CanBowlNextOver(bowler)) return Status::Err("bowler has used the maximum overs allowed");
    bowler_ = bowler;
    overRunsVsBowler_ = 0;
    return Status::Ok();
}

Status Innings::SetNextBatter(int batter) {
    if (!needBatter_) return Status::Err("no batter is needed");
    if (batter < 0 || batter >= rules_->squadSize) return Status::Err("batter index out of range");
    if (batters_[batter].batted) return Status::Err("that player has already batted");
    batters_[batter].batted = true;
    if (striker_ < 0) striker_ = batter;
    else nonStriker_ = batter;
    needBatter_ = false;
    return Status::Ok();
}

Status Innings::Declare() {
    if (rules_->oversPerInnings != 0) return Status::Err("declarations are only allowed in unlimited-overs cricket");
    if (IsComplete()) return Status::Err("innings is complete");
    if (!openersSet_) return Status::Err("innings has not started");
    end_ = InningsEnd::Declared;
    needBatter_ = false;
    bowler_ = -1;
    return Status::Ok();
}

void Innings::ForceComplete() {
    if (end_ == InningsEnd::None) end_ = InningsEnd::Forced;
    needBatter_ = false;
    bowler_ = -1;
}

void Innings::CheckCompletion() {
    if (end_ != InningsEnd::None) return;
    if (target_ && total_ >= *target_) end_ = InningsEnd::TargetReached;
    else if (wickets_ >= rules_->squadSize - 1) end_ = InningsEnd::AllOut;
    else if (rules_->oversPerInnings > 0 && legalBalls_ >= rules_->oversPerInnings * rules_->ballsPerOver)
        end_ = InningsEnd::OversComplete;
    if (end_ != InningsEnd::None) {
        needBatter_ = false;
        bowler_ = -1;
    }
}

Status Innings::Check(const Delivery& d) const {
    if (d.runs < 0 || d.runs > 7) return Status::Err("runs out of range");
    if (d.noBallRunsAs != ExtraType::None) {
        if (d.extra != ExtraType::NoBall) return Status::Err("noBallRunsAs only applies to a no-ball");
        if (d.noBallRunsAs != ExtraType::Bye && d.noBallRunsAs != ExtraType::LegBye)
            return Status::Err("runs off a no-ball are off the bat, byes or leg-byes");
    }
    const bool offTheBat = d.extra == ExtraType::None ||
                           (d.extra == ExtraType::NoBall && d.noBallRunsAs == ExtraType::None);
    if (d.boundary) {
        if (d.runs != 4 && d.runs != 6) return Status::Err("boundary must be 4 or 6");
        if (d.runs == 6 && !offTheBat) return Status::Err("a six can only come off the bat");
        if (d.dismissal == Dismissal::RunOut) return Status::Err("no run out on a boundary");
    }
    if (!DismissalAllowed(d.dismissal, d.extra, freeHit_))
        return Status::Err(std::string("cannot be out ") + ToString(d.dismissal) +
                           (freeHit_ ? " on a free hit" : "") +
                           (d.extra != ExtraType::None ? std::string(" off a ") + ToString(d.extra) : ""));
    const bool canHitNonStriker =
        d.dismissal == Dismissal::RunOut || d.dismissal == Dismissal::ObstructingField;
    if (d.dismissal != Dismissal::None && !canHitNonStriker) {
        if (!d.strikerOut) return Status::Err("only the striker can be out that way");
        if (d.runs != 0 || d.boundary) return Status::Err("no runs can be scored on that dismissal");
    }

    return Status::Ok();
}

Status Innings::Bowl(const Delivery& d, DeliveryRecord* out) {
    switch (State()) {
        case InningsState::Complete: return Status::Err("innings is complete");
        case InningsState::AwaitingOpeners: return Status::Err("set openers first");
        case InningsState::AwaitingBatter: return Status::Err("waiting for the next batter");
        case InningsState::AwaitingBowler: return Status::Err("choose a bowler for the over");
        case InningsState::InPlay: break;
    }

    if (Status v = Check(d); !v) return v;

    // ---- apply ----------------------------------------------------------
    DeliveryRecord rec;
    rec.inningsIndex = index_;
    rec.over = CompletedOvers();
    rec.bowler = bowler_;
    rec.striker = striker_;
    rec.nonStriker = nonStriker_;
    rec.delivery = d;
    rec.wasFreeHit = freeHit_;
    rec.legal = d.extra != ExtraType::Wide && d.extra != ExtraType::NoBall;

    BatterCard& bat = batters_[striker_];
    BowlerCard& bowl = bowlers_[bowler_];
    const int before = bat.runs;
    int charged = 0;  // runs charged to the bowler on this ball

    auto creditBat = [&](int r) {
        bat.runs += r;
        rec.runsToBatter = r;
        if (d.boundary && r == 4) bat.fours++;
        if (d.boundary && r == 6) bat.sixes++;
    };

    switch (d.extra) {
        case ExtraType::None:
            creditBat(d.runs);
            bat.balls++;
            charged = d.runs;
            rec.runsToTeam = d.runs;
            break;
        case ExtraType::Wide: {
            int w = rules_->widePenalty + d.runs;
            extras_.wides += w;
            bowl.wides++;
            charged = w;
            rec.runsToTeam = w;
            break;
        }
        case ExtraType::NoBall:
            // Law 21.12: penalty is a no-ball extra; runs off the bat go to the
            // striker; other runs are byes / leg-byes. The striker has faced it.
            extras_.noBalls += rules_->noBallPenalty;
            bat.balls++;
            bowl.noBalls++;
            if (d.noBallRunsAs == ExtraType::Bye) {
                extras_.byes += d.runs;
                charged = rules_->noBallPenalty;
            } else if (d.noBallRunsAs == ExtraType::LegBye) {
                extras_.legByes += d.runs;
                charged = rules_->noBallPenalty;
            } else {
                creditBat(d.runs);
                charged = rules_->noBallPenalty + d.runs;
            }
            rec.runsToTeam = rules_->noBallPenalty + d.runs;
            break;
        case ExtraType::Bye:
            extras_.byes += d.runs;
            bat.balls++;
            rec.runsToTeam = d.runs;
            break;
        case ExtraType::LegBye:
            extras_.legByes += d.runs;
            bat.balls++;
            rec.runsToTeam = d.runs;
            break;
    }
    bowl.runs += charged;
    overRunsVsBowler_ += charged;
    total_ += rec.runsToTeam;
    rec.batterFifty = before < 50 && bat.runs >= 50 && bat.runs < 100;
    rec.batterHundred = before < 100 && bat.runs >= 100;

    // Batters cross on odd runs completed by running (not on boundaries).
    if (!d.boundary && (d.runs % 2) == 1) std::swap(striker_, nonStriker_);

    // Free hit follows a no-ball; it carries over a wide.
    if (d.extra == ExtraType::NoBall) freeHit_ = rules_->freeHitOnNoBall;
    else if (d.extra != ExtraType::Wide) freeHit_ = false;

    if (d.dismissal != Dismissal::None) {
        const int outId = d.strikerOut ? rec.striker : rec.nonStriker;
        BatterCard& ob = batters_[outId];
        ob.out = true;
        ob.how = d.dismissal;
        wickets_++;
        if (CreditedToBowler(d.dismissal)) {
            ob.bowler = bowler_;
            bowl.wickets++;
        }
        // The new batter takes the dismissed batter's end.
        if (striker_ == outId) striker_ = -1;
        else nonStriker_ = -1;
        needBatter_ = true;
        rec.dismissedBatter = outId;
    }

    if (rec.legal) {
        legalBalls_++;
        bowl.legalBalls++;
        if (BallsInOver() == 0) {
            rec.overCompleted = true;
            if (overRunsVsBowler_ == 0) {
                bowl.maidens++;
                rec.maiden = true;
            }
            std::swap(striker_, nonStriker_);
            lastOverBowler_ = bowler_;
            bowler_ = -1;
        }
    }
    rec.ballInOver = rec.overCompleted ? rules_->ballsPerOver : BallsInOver();

    CheckCompletion();
    rec.totalAfter = total_;
    rec.wicketsAfter = wickets_;
    rec.inningsCompleted = IsComplete();
    balls_.push_back(rec);
    if (out) *out = rec;
    return Status::Ok();
}

std::string Innings::ScoreString() const {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d/%d (%d.%d)", total_, wickets_, CompletedOvers(), BallsInOver());
    return buf;
}

double Innings::RunRate() const {
    if (legalBalls_ == 0) return 0.0;
    return static_cast<double>(total_) * rules_->ballsPerOver / legalBalls_;
}

double Innings::RequiredRunRate() const {
    if (!target_ || rules_->oversPerInnings == 0) return -1.0;
    int ballsLeft = rules_->oversPerInnings * rules_->ballsPerOver - legalBalls_;
    int need = *target_ - total_;
    if (need <= 0) return 0.0;
    if (ballsLeft <= 0) return -1.0;
    return static_cast<double>(need) * rules_->ballsPerOver / ballsLeft;
}

}  // namespace cricket
