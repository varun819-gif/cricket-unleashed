// MatchHost: runs a match whose sides are controlled by paired phones.
//
//  * Captaincy controls (toss, openers, batter, bowler, field, review,
//    declare, follow-on, new ball, ready after a break) become MatchSession
//    commands - the rules engine still decides what is legal.
//  * Gameplay controls run each ball in stages: the fielding side chooses
//    the delivery ("bowl"), the ball is released, and the batting side has a
//    timing window to play a shot ("bat"). Fielders can react meanwhile.
//  * Every phone gets its own state view, including the exact list of
//    controls it may use right now (role-based controls).
//  * If every phone of a human side drops out, that side's timers pause
//    until a phone reconnects.
//
// Each ball is played out by the ball physics (BallOutcome / BallPhysics):
// the delivery the bowler chose flies, swings, bounces and spins; the shot
// the batter chose meets it (or misses); the fielders chase the result. The
// computer plays its side's bowling and batting through the same physics.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cricket/AutoCaptain.h"
#include "cricket/BallOutcome.h"
#include "cricket/MatchSession.h"
#include "cricket/controller/ControllerHub.h"

namespace cricket::ctl {

struct BowlInput {
    std::string variation = "stock";  // stock, yorker, bouncer, slower, inswing, outswing, offbreak, legbreak, googly, armball
    double line = 0.0;    // -1 (wide outside off) .. 0 (stumps) .. +1 (down leg)
    double length = 0.5;  // 0 (yorker) .. 1 (short)
    int effort = 70;      // 0..100
};

struct BatInput {
    std::string shot = "defend";  // leave, defend, drive, cut, pull, sweep, flick, loft
    int direction = 0;            // degrees, 0 = straight, + off side, - leg side
    int timingMs = 0;             // tap time minus ball arrival as the phone saw it
    bool provided = false;
};

struct HostConfig {
    std::array<bool, 2> human{true, false};  // which sides are played with phones
    int64_t bowlWindowMs = 20000;            // time to choose a delivery
    int64_t ballFlightMs = 1400;             // release -> arrival, as shown on the phone
    int64_t batWindowMs = 1200;              // extra time after arrival to play a shot
    uint64_t seed = 1;
    // Online: judge batting timing on the server (anti-cheat) and allow
    // for network round trips in the batting window.
    bool serverTimingCheck = false;
    int64_t timingToleranceMs = 150;
};

class MatchHost {
public:
    MatchHost(MatchRules rules, std::array<Team, 2> teams, ControlChannel& hub, HostConfig cfg);
    // Continue a match rebuilt from its journal (server crash recovery).
    // `resumeSalt` varies the simulator after a restart.
    MatchHost(std::unique_ptr<MatchSession> recovered, ControlChannel& hub, HostConfig cfg, uint64_t resumeSalt);
    // The AI captain takes over a side (online casual: player never came back).
    Status HandToAi(int side);
    bool IsHuman(int side) const { return cfg_.human.at(side); }
    int TimingCorrections() const { return timingCorrections_; }

    // Call often (e.g. every 50 ms) with the current time.
    void Process(int64_t nowMs);

    MatchSession& Session() { return *session_; }
    const MatchSession& Session() const { return *session_; }
    // The view a given phone receives.
    Json ViewFor(int side, Role role, int64_t nowMs) const;
    // Controls this phone may use right now.
    std::vector<std::string> AllowedControls(int side, Role role) const;
    // Human side with no connected phone (-1 if none): the game waits for it.
    int WaitingForController() const;
    const std::string& Stage() const { return stage_; }
    // Hub events (paired, connected, lost...) seen during the last Process.
    const std::vector<HubEvent>& RecentHubEvents() const { return recentEvents_; }

    // The physics report of the last ball (null before the first ball).
    const BallReport* LastBall() const { return lastBall_ ? &*lastBall_ : nullptr; }
    // Real (physics) flight time of the ball in the air now, in ms.
    int64_t RealFlightMs() const { return realFlightMs_; }

private:
    Status Apply(const ControllerAction& a, int64_t now);
    Status Captaincy(int side, const std::string& what, const Json& a, int64_t now);
    Status Submit(int side, CommandType t, int a, int b, int64_t now);
    void AdvanceDelivery(int64_t now);
    void ReleaseBall(int64_t now);
    void ResolveBall(int64_t now);
    void PublishStates(int64_t now);
    bool SideConnected(int side) const;
    static std::string PlayerId(int side) { return "side-" + std::to_string(side); }

    ControlChannel& hub_;
    HostConfig cfg_;
    std::unique_ptr<MatchSession> session_;
    int timingCorrections_ = 0;
    QuickSim sim_;          // players' perception on appeals only (never outcomes)
    BallOutcome outcome_;   // the physics
    std::optional<BallReport> lastBall_;
    phys::DeliverySpec spec_;
    phys::DeliveryPath path_;  // the ball in the air now
    int64_t realFlightMs_ = 0;
    std::array<uint64_t, 2> seq_{0, 0};
    // Delivery stages: "" (none), "bowl", "ball".
    std::string stage_;
    int64_t deadline_ = 0, arriveAt_ = 0;
    int64_t pausedAt_ = 0;
    BowlInput bowl_;
    BatInput bat_;
    std::vector<std::string> fieldingCalls_;
    std::map<std::string, std::string> lastSent_;  // device -> last state JSON
    std::map<std::string, int64_t> lastSentAt_;
    std::vector<HubEvent> recentEvents_;
    std::string message_;                          // last notable event text
    uint64_t lastEvent_ = 0;
};

}  // namespace cricket::ctl
