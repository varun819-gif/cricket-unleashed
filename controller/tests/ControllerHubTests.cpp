// ControllerHub: pairing, authentication, replay protection, reconnection,
// limits. A C++ "phone" implements the client half of the protocol exactly
// as controller/web/protocol.js does.
#include <deque>
#include <map>
#include <memory>

#include "../../core/tests/Test.h"
#include "cricket/controller/ControllerHub.h"
#include "PhoneSim.h"

using namespace cricket;
using namespace cricket::ctl;
using namespace cricket::ctl::testing;

namespace {

Json Tap(const std::string& what) { return Json::Object().Set("do", what); }

bool HasEvent(const std::vector<HubEvent>& ev, HubEvent::Kind k) {
    for (const auto& e : ev)
        if (e.kind == k) return true;
    return false;
}

}  // namespace

TEST(ctl_hub_pairs_a_phone_and_tv_proves_itself) {
    ControllerHub hub({}, Counter(1));
    Net net{hub};
    PairingOffer o = hub.CreatePairing(0, Role::Captain, 1000);
    CHECK_EQ(o.pairId.size(), size_t{12});
    CHECK_EQ(o.secret.size(), size_t{32});
    const std::string link = o.Link("192.168.1.20", 47321);
    CHECK(link.rfind("http://192.168.1.20:47321/#p=" + o.pairId + ".", 0) == 0);
    CHECK(link.size() < 106);  // fits a version-6 QR code
    Phone p(net);
    CHECK(p.Pair(o, 2000));
    const DeviceInfo* d = hub.Device(p.dev);
    CHECK(d != nullptr);
    if (d) {
        CHECK_EQ(d->side, 0);
        CHECK(d->role == Role::Captain);
        CHECK(d->connected);
        CHECK_EQ(d->name, std::string("Test phone"));
    }
    CHECK_EQ(hub.PendingPairings(), size_t{0});  // single use
    CHECK(HasEvent(hub.TakeEvents(), HubEvent::Kind::Paired));
    // The QR code can't be used again.
    Phone q(net);
    CHECK(!q.Pair(o, 3000));
    CHECK(q.lastError.find("unknown") != std::string::npos);
}

TEST(ctl_hub_wrong_secret_burns_the_code_after_three_tries) {
    ControllerHub hub({}, Counter(2));
    Net net{hub};
    PairingOffer o = hub.CreatePairing(1, Role::Captain, 0);
    for (int i = 0; i < 3; ++i) {
        Phone bad(net);
        CHECK(!bad.Pair(o, 10, std::string(32, 'x')));
        CHECK(bad.closedByServer);
        CHECK(bad.lastError.find("proof failed") != std::string::npos);
    }
    CHECK_EQ(hub.PendingPairings(), size_t{0});
    Phone good(net);
    CHECK(!good.Pair(o, 20));  // too late: burned
    CHECK(hub.Devices().empty());
}

TEST(ctl_hub_expired_code_and_bad_nonce) {
    ControllerHub hub({}, Counter(3));
    Net net{hub};
    PairingOffer o = hub.CreatePairing(0, Role::Captain, 0);
    Phone late(net);
    CHECK(!late.Pair(o, 120001));
    CHECK(late.lastError.find("expired") != std::string::npos);
    PairingOffer o2 = hub.CreatePairing(0, Role::Captain, 0);
    Phone p(net);
    CHECK(p.Open(0));
    hub.Receive(p.conn, Json::Object().Set("t", "hello").Set("pair", o2.pairId).Set("nc", "zz").Dump(), 0);
    p.Next();
    CHECK(p.closedByServer);
    CHECK_EQ(p.lastError, std::string("bad nonce"));
    hub.Tick(200000);
    CHECK_EQ(hub.PendingPairings(), size_t{0});  // expired codes are cleaned up
}

TEST(ctl_hub_authenticated_actions_and_signed_replies) {
    ControllerHub hub({}, Counter(4));
    Net net{hub};
    Phone p(net);
    CHECK(p.Pair(hub.CreatePairing(1, Role::Captain, 0), 0));
    const int64_t id = p.Act(Tap("drive"), 10);
    auto acts = hub.TakeActions();
    CHECK_EQ(acts.size(), size_t{1});
    if (!acts.empty()) {
        CHECK_EQ(acts[0].device, p.dev);
        CHECK_EQ(acts[0].side, 1);
        CHECK_EQ(acts[0].id, id);
        CHECK_EQ(acts[0].action.Str("do"), std::string("drive"));
    }
    hub.Ack(p.dev, id, false, "not your turn");
    auto ack = p.Body();
    CHECK(ack.has_value());
    if (ack) {
        CHECK_EQ(ack->Str("k"), std::string("ack"));
        CHECK(!ack->Flag("ok", true));
        CHECK_EQ(ack->Str("err"), std::string("not your turn"));
    }
    Json ping = Json::Object();
    ping.Set("k", "ping").Set("t", 1234.0);
    p.Send(ping, 50);
    auto pong = p.Body();
    CHECK(pong && pong->Str("k") == "pong" && pong->Num("t") == 1234.0 && pong->Num("st") == 50.0);
}

TEST(ctl_hub_rejects_tampering_and_replays) {
    ControllerHub hub({}, Counter(5));
    Net net{hub};
    {
        Phone p(net);
        CHECK(p.Pair(hub.CreatePairing(0, Role::Captain, 0), 0));
        std::string msg = p.Signed(Json::Object().Set("k", "action").Set("id", 1).Set("a", Tap("drive")), 1, p.sessKey);
        msg.replace(msg.find("drive"), 5, "slogg");  // changed in transit
        hub.Receive(p.conn, msg, 1);
        p.Next();
        CHECK(p.closedByServer);
        CHECK(hub.TakeActions().empty());
        CHECK(HasEvent(hub.TakeEvents(), HubEvent::Kind::Security));
    }
    {
        Phone p(net);
        CHECK(p.Pair(hub.CreatePairing(0, Role::Captain, 0), 0));
        const std::string msg = p.Signed(Json::Object().Set("k", "action").Set("id", 1).Set("a", Tap("drive")), 1, p.sessKey);
        hub.Receive(p.conn, msg, 1);
        hub.Receive(p.conn, msg, 2);  // replayed
        p.Next();
        CHECK(p.closedByServer);
        CHECK_EQ(hub.TakeActions().size(), size_t{1});
    }
    {
        Phone p(net);  // a message signed as if from the TV is not accepted from a phone
        CHECK(p.Pair(hub.CreatePairing(0, Role::Captain, 0), 0));
        const std::string b = Json::Object().Set("k", "ping").Dump();
        Json m = Json::Object();
        m.Set("t", "m").Set("s", 1).Set("b", b).Set("mac", HmacHex(p.sessKey, "s2c|1|" + b));
        hub.Receive(p.conn, m.Dump(), 1);
        p.Next();
        CHECK(p.closedByServer);
    }
    {
        Phone p(net);  // nothing but a handshake is accepted before pairing
        CHECK(p.Open(0));
        hub.Receive(p.conn, Json::Object().Set("t", "m").Set("s", 1).Dump(), 0);
        p.Next();
        CHECK(p.closedByServer);
        Phone q(net);
        CHECK(q.Open(0));
        hub.Receive(q.conn, "not json", 0);
        q.Next();
        CHECK(q.closedByServer);
    }
}

TEST(ctl_hub_reconnect_with_device_key_new_session) {
    ControllerHub hub({}, Counter(6));
    Net net{hub};
    Phone p(net);
    CHECK(p.Pair(hub.CreatePairing(0, Role::Player, 0), 0));
    const std::string oldSession = p.sessKey;
    p.Act(Tap("drive"), 1);
    hub.Closed(p.conn, 100);
    hub.TakeActions();
    auto ev = hub.TakeEvents();
    CHECK(HasEvent(ev, HubEvent::Kind::Disconnected));
    CHECK(!hub.Device(p.dev)->connected);
    CHECK(p.Resume(5000));  // same device, fresh session
    CHECK(p.sessKey != oldSession);
    CHECK(hub.Device(p.dev)->connected);
    CHECK(hub.Device(p.dev)->role == Role::Player);
    // A message from the old session can't be replayed into the new one.
    hub.Receive(p.conn, p.Signed(Json::Object().Set("k", "action").Set("id", 99).Set("a", Tap("x")), 50, oldSession), 5001);
    p.Next();
    CHECK(p.closedByServer);
    CHECK(hub.TakeActions().empty());
    // Someone who only knows the device id can't resume.
    Phone thief(net);
    thief.dev = p.dev;
    thief.devKey = std::string(32, 'k');
    CHECK(!thief.Resume(6000));
    CHECK(thief.lastError.find("proof failed") != std::string::npos);
}

TEST(ctl_hub_actions_exactly_once_across_reconnects) {
    ControllerHub hub({}, Counter(7));
    Net net{hub};
    Phone p(net);
    CHECK(p.Pair(hub.CreatePairing(0, Role::Captain, 0), 0));
    p.Act(Tap("a"), 1);
    p.Act(Tap("b"), 2);
    CHECK_EQ(hub.TakeActions().size(), size_t{2});
    hub.Closed(p.conn, 3);
    CHECK(p.Resume(4));
    // The phone didn't see the ack for action 2, so it sends it again.
    p.Act(Tap("b"), 5, 2);
    CHECK(hub.TakeActions().empty());
    auto ack = p.Body();
    CHECK(ack && ack->Flag("dup") && ack->Flag("ok"));
    p.Act(Tap("c"), 6, 3);
    auto acts = hub.TakeActions();
    CHECK(acts.size() == 1 && acts[0].id == 3);
}

TEST(ctl_hub_newer_connection_replaces_older) {
    ControllerHub hub({}, Counter(8));
    Net net{hub};
    Phone p(net);
    CHECK(p.Pair(hub.CreatePairing(0, Role::Captain, 0), 0));
    const ConnId old = p.conn;
    CHECK(p.Resume(10));  // e.g. Wi-Fi switched; the old socket is half-open
    CHECK(p.conn != old);
    net.Pump();
    bool replacedSent = false;
    for (auto& o : net.inbox[old]) replacedSent |= o.close && o.text.find("replaced") != std::string::npos;
    CHECK(replacedSent);
    hub.Receive(old, "{}", 11);  // the old connection is gone
    p.Act(Tap("x"), 12);
    CHECK_EQ(hub.TakeActions().size(), size_t{1});
}

TEST(ctl_hub_grace_period_lost_and_back) {
    HubConfig cfg;
    cfg.reconnectGraceMs = 30000;
    ControllerHub hub(cfg, Counter(9));
    Net net{hub};
    Phone p(net);
    CHECK(p.Pair(hub.CreatePairing(1, Role::Captain, 0), 0));
    hub.TakeEvents();
    hub.Closed(p.conn, 1000);
    hub.Tick(30999);
    CHECK(!HasEvent(hub.TakeEvents(), HubEvent::Kind::Lost));
    hub.Tick(31000);
    auto ev = hub.TakeEvents();
    CHECK(HasEvent(ev, HubEvent::Kind::Lost));
    CHECK(hub.Device(p.dev)->lost);
    CHECK(p.Resume(90000));  // still paired: can come back
    ev = hub.TakeEvents();
    CHECK(HasEvent(ev, HubEvent::Kind::Connected));
    CHECK(!hub.Device(p.dev)->lost);
}

TEST(ctl_hub_revoke) {
    ControllerHub hub({}, Counter(10));
    Net net{hub};
    Phone p(net);
    CHECK(p.Pair(hub.CreatePairing(0, Role::Captain, 0), 0));
    CHECK_OK(hub.Revoke(p.dev));
    p.Next();
    CHECK(p.closedByServer);
    CHECK(hub.Device(p.dev) == nullptr);
    CHECK(!p.Resume(10));
    CHECK(p.lastError.find("pair again") != std::string::npos);
    CHECK_ERR(hub.Revoke("nobody"));
}

TEST(ctl_hub_limits) {
    HubConfig cfg;
    cfg.maxConnections = 3;
    cfg.maxDevices = 2;
    cfg.maxMessagesPerSecond = 5;
    ControllerHub hub(cfg, Counter(11));
    Net net{hub};
    Phone a(net), b(net), c(net);
    CHECK(a.Pair(hub.CreatePairing(0, Role::Captain, 0), 0));
    CHECK(b.Pair(hub.CreatePairing(1, Role::Captain, 0), 0));
    CHECK(!c.Pair(hub.CreatePairing(1, Role::Player, 0), 0));  // device limit
    CHECK(c.lastError.find("too many") != std::string::npos);
    // Connection limit.
    CHECK(hub.Open(0).has_value());
    CHECK(!hub.Open(0).has_value());
    // Oversized message.
    hub.Receive(a.conn, std::string(9000, ' '), 1);
    a.Next();
    CHECK(a.closedByServer);
    // Flooding.
    for (int i = 0; i < 6; ++i) b.Act(Tap("x"), 100);
    b.Next();
    CHECK(b.closedByServer);
    CHECK(b.lastError.find("too many messages") != std::string::npos);
}

TEST(ctl_hub_handshake_timeout) {
    ControllerHub hub({}, Counter(12));
    Net net{hub};
    Phone p(net);
    CHECK(p.Open(0));
    hub.Tick(10001);
    p.Next();
    CHECK(p.closedByServer);
    CHECK(p.lastError.find("timed out") != std::string::npos);
}

TEST(ctl_hub_devices_survive_a_tv_restart) {
    ControllerHub hub({}, Counter(13));
    Net net{hub};
    Phone p(net);
    CHECK(p.Pair(hub.CreatePairing(1, Role::Captain, 0), 0));
    p.Act(Tap("a"), 1);
    hub.TakeActions();
    const std::string saved = hub.ExportDevices();
    ControllerHub restarted({}, Counter(14));
    CHECK_OK(restarted.ImportDevices(saved));
    Net net2{restarted};
    Phone same(net2);
    same.dev = p.dev;
    same.devKey = p.devKey;
    same.actionId = p.actionId;
    CHECK(same.Resume(100));
    CHECK_EQ(restarted.Device(p.dev)->side, 1);
    same.Act(Tap("a"), 101, 1);  // already done before the restart
    CHECK(restarted.TakeActions().empty());
    CHECK_ERR(restarted.ImportDevices("garbage"));
    CHECK_ERR(restarted.ImportDevices("cu-devices 1\n{\"id\":\"x\",\"key\":\"short\",\"role\":\"captain\"}\n"));
}

TEST(ctl_hub_spectator_pairing_has_no_side) {
    ControllerHub hub({}, Counter(15));
    Net net{hub};
    Phone p(net);
    CHECK(p.Pair(hub.CreatePairing(0, Role::Spectator, 0), 0));
    CHECK_EQ(hub.Device(p.dev)->side, -1);
    CHECK(RoleFromString("player") == Role::Player);
    CHECK(!RoleFromString("umpire").has_value());
}
