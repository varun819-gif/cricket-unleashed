// OnlineServer: the internet game server (server-authoritative).
//
//  * Guest accounts: a device asks for a guest account and gets a random
//    256-bit token; later it logs in with (id, token). No passwords, no
//    social login. Only the token's SHA-256 is stored (see Store).
//  * Every client message carries a per-connection sequence number "s" that
//    must go up by one each time; a repeated or re-ordered message is
//    refused (duplicate-command protection). Match controls also carry an
//    action id that never repeats for that player, so a control re-sent
//    after a reconnect is acknowledged but never applied twice.
//  * Private rooms with 6-letter invitation codes (casual), public
//    matchmaking (casual or ranked; ranked pairs players by rating).
//  * Each live match is a MatchHost (the same game logic the phone
//    controller uses) driven through an OnlineMatch channel. The server runs
//    the rules engine and decides every outcome; clients only send controls.
//    Batting timing is re-measured on the server (anti-cheat).
//  * A player who drops out has `graceMs` to come back (the match waits).
//    After that: ranked = forfeit, casual = the AI captain takes over.
//  * Live matches are journaled to disk and recovered after a server restart.
//
// The wire protocol is documented in docs/ONLINE_MULTIPLAYER.md.
#pragma once

#include <array>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "cricket/SaveGame.h"
#include "cricket/controller/ControllerHub.h"
#include "cricket/controller/MatchHost.h"
#include "cricket/online/Store.h"

namespace cricket::online {

using ctl::ConnId;
using ctl::Outgoing;

struct OnlineConfig {
    std::string region = "in";            // first region: India
    std::string dataDir;                  // "" = no files (tests)
    int64_t graceMs = 120000;             // reconnect window during a match
    int64_t idleLimitMs = 180000;         // a captain who never decides is treated as gone
    int64_t loginTimeoutMs = 15000;       // connections must log in within this
    int64_t roomTtlMs = 30 * 60 * 1000;   // unused rooms close
    int64_t serverPingMs = 3000;          // round-trip measurement interval
    size_t maxConnections = 2000;
    size_t maxMessageBytes = 8192;
    int maxMessagesPerSecond = 30;
    int maxGuestsPerMinute = 30;          // across the whole server
    size_t maxRooms = 1000;
    size_t maxMatches = 200;
    int rankedUnlockMatches = 3;          // full matches needed before ranked (slows fake-account farming)
    int maxWrongRoomCodesPerMinute = 10;  // stops guessing room codes
    int64_t leaderboardCacheMs = 15000;
    ctl::HostConfig host;                 // ball timings (serverTimingCheck is forced on)
};

class OnlineServer;

// One live match. Implements the channel MatchHost talks to.
class OnlineMatch : public ctl::ControlChannel {
public:
    std::vector<ctl::DeviceInfo> Devices() const override;
    std::vector<ctl::ControllerAction> TakeActions() override;
    std::vector<ctl::HubEvent> TakeEvents() override;
    void Ack(const std::string& device, int64_t actionId, bool ok, const std::string& error = "") override;
    bool SendTo(const std::string& device, const Json& body) override;
    void Broadcast(int side, const Json& body) override;
    int64_t RttMs(const std::string& device) const override;

    std::string id, mode, format;
    std::array<std::string, 2> players;   // account ids
    std::array<std::string, 2> teamNames;
    int64_t startedMs = 0;
    std::array<int64_t, 2> lastActionId{0, 0};
    std::array<int64_t, 2> goneSince{-1, -1};  // disconnected at (-1 = connected)
    std::array<bool, 2> aiTakeover{false, false};
    int64_t lastProgressMs = 0;
    // Added to server time for this match: after a restart the machine's
    // clock may be behind the time already recorded in the match journal.
    int64_t clockOffset = 0;
    uint64_t lastInputs = 0;  // last match event seen (idle detection)
    std::unique_ptr<ctl::MatchHost> host;
    SessionJournal journal;
    std::FILE* actsLog = nullptr;         // accepted action ids (survive a crash)
    ~OnlineMatch() {
        if (actsLog) std::fclose(actsLog);
    }

private:
    friend class OnlineServer;
    OnlineServer* server_ = nullptr;
    std::vector<ctl::ControllerAction> actions_;
    std::vector<ctl::HubEvent> events_;
};

class OnlineServer : public ctl::ConnectionSink {
public:
    OnlineServer(Store& store, OnlineConfig cfg, ctl::RandomSource rnd = ctl::SystemRandom());

    // Rebuilds live matches from their journals (call once at start-up).
    // Returns how many were recovered.
    size_t RecoverLiveMatches(int64_t nowMs);

    std::optional<ConnId> Open(int64_t nowMs) override;
    void Receive(ConnId conn, const std::string& text, int64_t nowMs) override;
    void Closed(ConnId conn, int64_t nowMs) override;
    std::vector<Outgoing> TakeOutbox() override;
    // Runs matchmaking, matches, timers. Call often (e.g. every 50 ms).
    void Tick(int64_t nowMs);

    // ---- introspection (health page, tests) ----
    size_t LiveMatches() const { return matches_.size(); }
    size_t Rooms() const { return rooms_.size(); }
    size_t Queued() const;
    size_t Connections() const { return conns_.size(); }
    Json Health(int64_t nowMs) const;
    const OnlineMatch* MatchOf(const std::string& account) const;
    OnlineMatch* MatchById(const std::string& id);

    // Allowed match formats online: t20, odi, overs=1..50 (Test matches are
    // not offered online yet - see docs).
    static std::optional<MatchRules> RulesFor(const std::string& format);
    static std::vector<std::string> TeamNames();

private:
    struct Conn {
        std::string account;
        int64_t openedAt = 0;
        int64_t inSeq = 0;
        int64_t windowStart = 0;
        int windowCount = 0;
        std::deque<int64_t> rtt;          // recent server-measured round trips
        int64_t lastPingAt = 0;
        bool closing = false;
    };
    struct Room {
        std::string code, host, guest, format;
        std::string hostTeam, guestTeam;
        int64_t createdMs = 0, touchedMs = 0;
    };
    struct QueueEntry {
        std::string account, mode, format, team;
        int rating = 1200;
        int64_t joinedMs = 0;
    };
    struct Presence {
        ConnId conn = 0;
        std::deque<int64_t> wrongCodes;
        std::string room;
        std::string match;
    };

    void Send(ConnId c, const Json& body, bool close = false);
    void SendToAccount(const std::string& account, const Json& body);
    void Reply(ConnId c, int64_t seq, bool ok, const std::string& err, Json extra = Json::Object());
    void Fail(ConnId c, const std::string& why);
    void OnRequest(ConnId c, Conn& k, const Json& j, int64_t now);
    void OnLogin(ConnId c, Conn& k, const Account& a, int64_t seq, int64_t now, const std::string& token);
    void OnAction(ConnId c, Conn& k, const Json& j, int64_t now);
    void RoomCreate(ConnId c, Conn& k, const Json& j, int64_t now);
    void RoomJoin(ConnId c, Conn& k, const Json& j, int64_t now);
    void RoomLeave(const std::string& account, bool notify);
    void RoomStart(ConnId c, Conn& k, const Json& j, int64_t now);
    void Enqueue(ConnId c, Conn& k, const Json& j, int64_t now);
    void Dequeue(const std::string& account);
    void Matchmake(int64_t now);
    Json RoomJson(const Room& r) const;
    std::string NewRoomCode();
    std::string NewMatchId(int64_t now);

    Status StartMatch(const std::string& mode, const std::string& format, std::array<std::string, 2> accounts,
                      std::array<std::string, 2> teams, int64_t now);
    void AttachMatch(const std::string& account, OnlineMatch& m);
    void RunMatch(OnlineMatch& m, int64_t now);
    void FinishMatch(OnlineMatch& m, int winner, const std::string& outcome, const std::string& summary,
                     int64_t now);
    void WriteMeta(const OnlineMatch& m);
    std::string LiveDir() const;
    int64_t RttOf(const std::string& account) const;
    bool Connected(const std::string& account) const;

    friend class OnlineMatch;

    Store& store_;
    OnlineConfig cfg_;
    ctl::RandomSource rnd_;
    std::map<ConnId, Conn> conns_;
    ConnId nextConn_ = 1;
    std::vector<Outgoing> outbox_;
    std::map<std::string, Presence> presence_;  // account -> where they are
    std::map<std::string, Room> rooms_;          // code -> room
    std::vector<QueueEntry> queue_;
    std::map<std::string, std::unique_ptr<OnlineMatch>> matches_;
    std::deque<int64_t> guestTimes_;
    uint64_t matchCounter_ = 0;
    std::map<std::string, std::pair<int64_t, Json>> boardCache_;  // request -> (made at, board)
    int64_t now_ = 0;
};

}  // namespace cricket::online
