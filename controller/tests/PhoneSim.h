// Test helpers: a C++ "phone" implementing the client half of the controller
// protocol exactly as controller/web/protocol.js does.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <optional>

#include "cricket/controller/ControllerHub.h"

namespace cricket::ctl::testing {

// Deterministic randomness for repeatable tests.
inline RandomSource Counter(uint64_t seed) {
    auto state = std::make_shared<uint64_t>(seed);
    return [state](size_t n) {
        std::string out;
        while (out.size() < n) {
            *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
            for (int i = 0; i < 8 && out.size() < n; ++i) out += static_cast<char>((*state >> (i * 8)) & 0xff);
        }
        return out;
    };
}

struct Net {
    ControllerHub& hub;
    std::map<ConnId, std::deque<Outgoing>> inbox;
    explicit Net(ControllerHub& h) : hub(h) {}
    void Pump() {
        for (auto& o : hub.TakeOutbox()) inbox[o.conn].push_back(o);
    }
};

struct Phone {
    Net& net;
    ConnId conn = 0;
    std::string pairId, secret, dev, devKey, sessKey, nc, ns;
    int64_t seq = 0, inSeq = 0, actionId = 0;
    bool closedByServer = false;
    std::string lastError;
    int nonceCounter = 0;

    explicit Phone(Net& n) : net(n) {}
    std::string Nonce() { return ToHex(Counter(1000 + ++nonceCounter + reinterpret_cast<uintptr_t>(this) % 997)(16)); }

    std::optional<Json> Next() {
        net.Pump();
        auto& q = net.inbox[conn];
        if (q.empty()) return std::nullopt;
        Outgoing o = q.front();
        q.pop_front();
        if (o.close) closedByServer = true;
        auto j = Json::Parse(o.text);
        if (j && j->Str("t") == "error") lastError = j->Str("why");
        return j;
    }
    bool Open(int64_t now) {
        auto c = net.hub.Open(now);
        if (!c) return false;
        conn = *c;
        closedByServer = false;
        seq = inSeq = 0;
        return true;
    }
    bool Handshake(const Json& hello, const std::string& macKey, const std::string& macText, int64_t now) {
        net.hub.Receive(conn, hello.Dump(), now);
        auto ch = Next();
        if (!ch || ch->Str("t") != "challenge") return false;
        ns = ch->Str("ns");
        Json prove = Json::Object();
        prove.Set("t", "prove").Set("mac", HmacHex(macKey, macText + "|" + nc + "|" + ns)).Set("name", "Test phone");
        net.hub.Receive(conn, prove.Dump(), now);
        auto w = Next();
        if (!w || w->Str("t") != "welcome") return false;
        dev = w->Str("dev");
        if (devKey.empty()) devKey = HmacSha256(secret, "cu-device|" + dev);
        sessKey = HmacSha256(devKey, "cu-session|" + nc + "|" + ns);
        // The phone checks the TV is genuine.
        return ConstantTimeEqual(w->Str("mac"), HmacHex(sessKey, "cu-welcome|" + dev));
    }
    bool Pair(const PairingOffer& o, int64_t now, const std::string& useSecret = "") {
        if (!Open(now)) return false;
        pairId = o.pairId;
        secret = useSecret.empty() ? o.secret : useSecret;
        nc = Nonce();
        Json h = Json::Object();
        h.Set("t", "hello").Set("pair", pairId).Set("nc", nc);
        return Handshake(h, secret, "cu-pair|" + pairId, now);
    }
    bool Resume(int64_t now) {
        if (!Open(now)) return false;
        nc = Nonce();
        Json h = Json::Object();
        h.Set("t", "resume").Set("dev", dev).Set("nc", nc);
        return Handshake(h, devKey, "cu-resume|" + dev, now);
    }
    std::string Signed(const Json& body, int64_t s, const std::string& key) {
        const std::string b = body.Dump();
        Json m = Json::Object();
        m.Set("t", "m").Set("s", s).Set("b", b).Set("mac", HmacHex(key, "c2s|" + std::to_string(s) + "|" + b));
        return m.Dump();
    }
    void Send(const Json& body, int64_t now) { net.hub.Receive(conn, Signed(body, ++seq, sessKey), now); }
    int64_t Act(const Json& action, int64_t now, int64_t id = 0) {
        Json b = Json::Object();
        actionId = id ? id : actionId + 1;
        b.Set("k", "action").Set("id", actionId).Set("a", action);
        Send(b, now);
        return actionId;
    }
    // Next verified body from the TV (checks the TV's signature and order).
    std::optional<Json> Body() {
        auto m = Next();
        if (!m || m->Str("t") != "m") return std::nullopt;
        const int64_t s = m->Int("s");
        const std::string b = m->Str("b");
        if (s <= inSeq || !ConstantTimeEqual(m->Str("mac"), HmacHex(sessKey, "s2c|" + std::to_string(s) + "|" + b)))
            return std::nullopt;
        inSeq = s;
        return Json::Parse(b);
    }
};


}  // namespace cricket::ctl::testing
