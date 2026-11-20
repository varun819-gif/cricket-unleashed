// Coin toss. In multiplayer this object only ever lives on the server; the
// seed comes from the server's OS random source, never from a client.
#pragma once

#include <cstdint>
#include <random>

#include "cricket/Types.h"

namespace cricket {

enum class CoinFace { Heads, Tails };
enum class TossChoice { Bat, Bowl };
enum class TossPhase { AwaitingCall, AwaitingDecision, Done };

class Toss {
public:
    // callingSide: the visiting captain calls by convention.
    Toss(uint64_t seed, int callingSide = 1);

    // A fresh seed from the operating system's random device.
    static uint64_t SecureSeed();

    Status Call(int side, CoinFace call);
    Status Decide(int side, TossChoice choice);

    TossPhase Phase() const { return phase_; }
    int CallingSide() const { return callingSide_; }
    CoinFace Landed() const { return landed_; }
    CoinFace Called() const { return called_; }
    int Winner() const { return winner_; }
    TossChoice Choice() const { return choice_; }
    int BattingFirst() const;  // -1 until decided

private:
    std::mt19937_64 rng_;
    int callingSide_;
    TossPhase phase_ = TossPhase::AwaitingCall;
    CoinFace called_ = CoinFace::Heads, landed_ = CoinFace::Heads;
    int winner_ = -1;
    TossChoice choice_ = TossChoice::Bat;
};

}  // namespace cricket
