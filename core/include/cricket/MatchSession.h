// Server-authoritative match session.
//
// Clients (TV app, phone controller, remote opponent) only send *intents*
// (Commands). The server checks who sent it, whether it is that player's turn
// to decide, and whether the sequence number is new (duplicate / replay
// protection). Ball outcomes come from the server's own simulation via
// ResolveDelivery() - a client can never submit runs or wickets.
//
// Every accepted change is appended to an ordered event log. Clients render
// from events and, after a reconnect, ask for EventsSince(lastSeenId).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cricket/AutoCaptain.h"
#include "cricket/FieldPlacement.h"
#include "cricket/Match.h"
#include "cricket/Toss.h"

namespace cricket {

enum class CommandType {
    TossCall,        // a = 0 heads / 1 tails
    TossDecision,    // a = 0 bat / 1 bowl
    SetOpeners,      // a = striker, b = non-striker
    SetBowler,       // a = bowler
    SetNextBatter,   // a = batter
    SetFieldPreset,  // a = (int)FieldPreset
    SetField,        // field = custom setup
    Declare,
    FollowOn,        // a = 1 enforce / 0 bat again
};

struct Command {
    std::string player;
    uint64_t seq = 0;  // strictly increasing per player
    CommandType type = CommandType::TossCall;
    int a = 0, b = 0;
    FieldSetup field;
};

struct SessionEvent {
    uint64_t id = 0;
    std::string kind;  // "toss", "decision", "ball", "field", "innings", "result", ...
    std::string text;
    std::optional<DeliveryRecord> ball;
};

// What the session is waiting for next, and from which side.
struct Pending {
    std::string what;  // "toss-call", "toss-decision", "openers", "bowler", "batter", "follow-on", "delivery", "none"
    int side = -1;
};

class MatchSession {
public:
    MatchSession(MatchRules rules, std::array<Team, 2> teams, uint64_t serverSeed);

    // A human claims a side. Unclaimed sides are run by the AI captain.
    Status Join(const std::string& player, int side);
    Status Submit(const Command& c);
    // Server-side ball resolution (physics / QuickSim). Never called with client data.
    Status ResolveDelivery(const Delivery& d);
    // Lets AI captains take any decisions that are theirs to take.
    void RunAi();

    Pending WaitingFor() const;
    bool HasMatch() const { return match_ != nullptr; }
    const Match& GetMatch() const { return *match_; }
    const Toss& GetToss() const { return toss_; }
    const FieldSetup& Field(int side) const { return fields_.at(side); }
    std::vector<SessionEvent> EventsSince(uint64_t lastSeenId) const;
    uint64_t LastEventId() const { return events_.empty() ? 0 : events_.back().id; }

private:
    void Log(std::string kind, std::string text, std::optional<DeliveryRecord> ball = std::nullopt);
    Status Apply(int side, const Command& c);
    void StartMatch();
    void EnsureLegalField();
    void AfterMatchChange();
    bool IsHuman(int side) const;
    int SideOf(const std::string& player) const;

    MatchRules rules_;
    std::array<Team, 2> teams_;
    Toss toss_;
    std::unique_ptr<Match> match_;
    std::array<FieldSetup, 2> fields_;
    std::array<std::string, 2> humans_;
    std::map<std::string, uint64_t> lastSeq_;
    std::vector<SessionEvent> events_;
    AutoCaptain ai_;
    size_t lastInningsSeen_ = 0;
    bool resultLogged_ = false;
};

}  // namespace cricket
