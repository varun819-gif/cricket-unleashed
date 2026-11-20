#include "Test.h"
#include "cricket/MatchSession.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

namespace {
std::array<Team, 2> TwoTeams() {
    auto all = FictionalTeams();
    return {all[0], all[1]};
}
Command Cmd(const std::string& p, uint64_t seq, CommandType t, int a = 0, int b = 0) {
    Command c;
    c.player = p;
    c.seq = seq;
    c.type = t;
    c.a = a;
    c.b = b;
    return c;
}
int AnyLegalBowler(const Innings& inn) {
    for (int b = 10; b >= 0; --b)
        if (b != 4 && inn.CanBowlNextOver(b)) return b;
    return -1;
}
}  // namespace

TEST(session_join_rules) {
    MatchSession s(MatchRules::Custom(2), TwoTeams(), 1);
    CHECK_OK(s.Join("alice", 0));
    CHECK_ERR(s.Join("bob", 0));    // taken
    CHECK_ERR(s.Join("alice", 1));  // already in
    CHECK_OK(s.Join("bob", 1));
    CHECK_ERR(s.Submit(Cmd("mallory", 1, CommandType::TossCall)));
}

TEST(session_toss_turns_and_duplicate_protection) {
    MatchSession s(MatchRules::Custom(2), TwoTeams(), 99);
    s.Join("alice", 0);
    s.Join("bob", 1);
    CHECK(s.WaitingFor().what == "toss-call");
    CHECK_EQ(s.WaitingFor().side, 1);
    CHECK_ERR(s.Submit(Cmd("alice", 1, CommandType::TossCall)));  // not her call
    CHECK_OK(s.Submit(Cmd("bob", 1, CommandType::TossCall, 1)));
    CHECK_ERR(s.Submit(Cmd("bob", 1, CommandType::TossCall, 1)));  // replayed seq
    int winner = s.GetToss().Winner();
    std::string w = winner == 0 ? "alice" : "bob";
    std::string l = winner == 0 ? "bob" : "alice";
    CHECK_ERR(s.Submit(Cmd(l, 5, CommandType::TossDecision, 0)));  // loser can't decide
    CHECK_OK(s.Submit(Cmd(w, 10, CommandType::TossDecision, 0)));   // bat
    CHECK(s.HasMatch());
    CHECK_EQ(s.GetMatch().BattingSide(), winner);
}

TEST(session_full_human_vs_human_game) {
    MatchSession s(MatchRules::Custom(2), TwoTeams(), 5);
    s.Join("alice", 0);
    s.Join("bob", 1);
    uint64_t seq[2] = {0, 0};
    auto send = [&](int side, CommandType t, int a = 0, int b = 0) {
        return s.Submit(Cmd(side == 0 ? "alice" : "bob", ++seq[side], t, a, b));
    };
    CHECK_OK(send(1, CommandType::TossCall, 0));
    CHECK_OK(send(s.GetToss().Winner(), CommandType::TossDecision, 1));
    QuickSim sim(3);
    int nextBatter[2] = {2, 2};
    int guard = 0;
    while (s.WaitingFor().what != "none" && guard++ < 1000) {
        Pending p = s.WaitingFor();
        const Match& m = s.GetMatch();
        if (p.what == "openers") {
            CHECK_ERR(send(1 - p.side, CommandType::SetOpeners, 0, 1));  // other captain can't
            CHECK_OK(send(p.side, CommandType::SetOpeners, 0, 1));
        } else if (p.what == "batter") {
            CHECK_OK(send(p.side, CommandType::SetNextBatter, nextBatter[p.side]++));
        } else if (p.what == "bowler") {
            int b = AnyLegalBowler(m.Current());
            CHECK_OK(send(p.side, CommandType::SetBowler, b));
            CHECK_ERR(send(1 - p.side, CommandType::SetFieldPreset, 0));  // batting side can't set field
            CHECK_OK(send(p.side, CommandType::SetFieldPreset, static_cast<int>(FieldPreset::Powerplay)));
        } else if (p.what == "delivery") {
            CHECK_OK(s.ResolveDelivery(sim.Next(m)));
        } else {
            CHECK(false);
            break;
        }
    }
    CHECK(s.GetMatch().Phase() == MatchPhase::Complete);
    bool sawResult = false;
    for (const auto& e : s.EventsSince(0)) sawResult |= e.kind == "result";
    CHECK(sawResult);
}

TEST(session_ai_opponent_and_reconnect_catch_up) {
    MatchSession s(MatchRules::Custom(3), TwoTeams(), 11);
    s.Join("solo", 1);  // side 0 is AI
    uint64_t seq = 0;
    CHECK_OK(s.Submit(Cmd("solo", ++seq, CommandType::TossCall, 0)));
    if (s.WaitingFor().what == "toss-decision") CHECK_OK(s.Submit(Cmd("solo", ++seq, CommandType::TossDecision, 0)));
    QuickSim sim(8);
    uint64_t seen = 0;
    std::vector<SessionEvent> clientCopy;
    int guard = 0;
    while (s.WaitingFor().what != "none" && guard++ < 1000) {
        Pending p = s.WaitingFor();
        if (p.what == "delivery") {
            CHECK_OK(s.ResolveDelivery(sim.Next(s.GetMatch())));
        } else {
            CHECK_EQ(p.side, 1);  // AI never leaves its own decisions pending
            const auto& inn = s.GetMatch().Current();
            if (p.what == "openers") CHECK_OK(s.Submit(Cmd("solo", ++seq, CommandType::SetOpeners, 0, 1)));
            else if (p.what == "batter") {
                for (int b = 0; b < 11; ++b)
                    if (!inn.Batter(b).batted) {
                        CHECK_OK(s.Submit(Cmd("solo", ++seq, CommandType::SetNextBatter, b)));
                        break;
                    }
            } else if (p.what == "bowler") {
                CHECK_OK(s.Submit(Cmd("solo", ++seq, CommandType::SetBowler, AnyLegalBowler(inn))));
            }
        }
        // Client drops every 7th loop and catches up later from its last seen id.
        if (guard % 7 != 0) {
            for (auto& e : s.EventsSince(seen)) clientCopy.push_back(e);
            seen = s.LastEventId();
        }
    }
    for (auto& e : s.EventsSince(seen)) clientCopy.push_back(e);
    CHECK_EQ(clientCopy.size(), static_cast<size_t>(s.LastEventId()));
    for (size_t i = 0; i < clientCopy.size(); ++i) CHECK_EQ(clientCopy[i].id, i + 1);
    CHECK(s.GetMatch().Phase() == MatchPhase::Complete);
}

TEST(session_rejects_delivery_when_not_ready) {
    MatchSession s(MatchRules::Custom(2), TwoTeams(), 2);
    s.Join("a", 0);
    s.Join("b", 1);
    CHECK_ERR(s.ResolveDelivery(Delivery{}));
}
