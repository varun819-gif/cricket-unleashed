#include "cricket/Toss.h"

namespace cricket {

Toss::Toss(uint64_t seed, int callingSide) : rng_(seed), callingSide_(callingSide == 0 ? 0 : 1) {}

uint64_t Toss::SecureSeed() {
    std::random_device rd;
    return (static_cast<uint64_t>(rd()) << 32) ^ rd();
}

Status Toss::Call(int side, CoinFace call) {
    if (phase_ != TossPhase::AwaitingCall) return Status::Err("toss already called");
    if (side != callingSide_) return Status::Err("only the calling captain may call the toss");
    called_ = call;
    landed_ = (rng_() & 1u) ? CoinFace::Heads : CoinFace::Tails;
    winner_ = (landed_ == called_) ? callingSide_ : 1 - callingSide_;
    phase_ = TossPhase::AwaitingDecision;
    return Status::Ok();
}

Status Toss::Decide(int side, TossChoice choice) {
    if (phase_ != TossPhase::AwaitingDecision) return Status::Err("no toss decision is pending");
    if (side != winner_) return Status::Err("only the captain who won the toss may decide");
    choice_ = choice;
    phase_ = TossPhase::Done;
    return Status::Ok();
}

int Toss::BattingFirst() const {
    if (phase_ != TossPhase::Done) return -1;
    return choice_ == TossChoice::Bat ? winner_ : 1 - winner_;
}

}  // namespace cricket
