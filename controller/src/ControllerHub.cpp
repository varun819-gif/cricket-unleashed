#include "cricket/controller/ControllerHub.h"

#include <sstream>

namespace cricket::ctl {

const char* ToString(Role r) {
    switch (r) {
        case Role::Captain: return "captain";
        case Role::Player: return "player";
        case Role::Spectator: return "spectator";
    }
    return "?";
}

std::optional<Role> RoleFromString(const std::string& s) {
    if (s == "captain") return Role::Captain;
    if (s == "player") return Role::Player;
    if (s == "spectator") return Role::Spectator;
    return std::nullopt;
}

std::string PairingOffer::Link(const std::string& host, int port) const {
    return "http://" + host + ":" + std::to_string(port) + "/#p=" + pairId + "." + Base64Url(secret);
}

ControllerHub::ControllerHub(HubConfig cfg, RandomSource rnd) : cfg_(cfg), rnd_(std::move(rnd)) {}

std::string ControllerHub::NewId(size_t bytes) { return Base64Url(rnd_(bytes)); }

bool ControllerHub::IsHex(const std::string& s, size_t len) {
    if (s.size() != len) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// ---- TV side ----------------------------------------------------------------

PairingOffer ControllerHub::CreatePairing(int side, Role role, int64_t now) {
    PairingOffer o;
    o.pairId = NewId(9);  // 12 characters
    o.secret = rnd_(32);
    o.side = role == Role::Spectator ? -1 : side;
    o.role = role;
    o.expiresMs = now + cfg_.pairingTtlMs;
    pairings_[o.pairId] = {o, 0};
    return o;
}

void ControllerHub::CancelPairing(const std::string& pairId) { pairings_.erase(pairId); }

void ControllerHub::SendRaw(ConnId c, const Json& j, bool close) { outbox_.push_back({c, j.Dump(), close}); }

void ControllerHub::SendBody(ConnId c, const Json& body) {
    auto it = conns_.find(c);
    if (it == conns_.end() || it->second.stage != Stage::Ready) return;
    Conn& k = it->second;
    const int64_t seq = ++k.outSeq;
    const std::string b = body.Dump();
    Json m = Json::Object();
    m.Set("t", "m").Set("s", seq).Set("b", b).Set("mac", HmacHex(k.sessionKey, "s2c|" + std::to_string(seq) + "|" + b));
    SendRaw(c, m);
}

bool ControllerHub::SendTo(const std::string& device, const Json& body) {
    auto it = devices_.find(device);
    if (it == devices_.end() || it->second.conn == 0) return false;
    SendBody(it->second.conn, body);
    return true;
}

void ControllerHub::Broadcast(int side, const Json& body) {
    for (auto& [id, d] : devices_)
        if (d.conn != 0 && (side == -2 || d.info.side == side)) SendBody(d.conn, body);
}

void ControllerHub::Ack(const std::string& device, int64_t actionId, bool ok, const std::string& error) {
    Json a = Json::Object();
    a.Set("k", "ack").Set("id", actionId).Set("ok", ok);
    if (!ok) a.Set("err", error);
    SendTo(device, a);
}

Status ControllerHub::Revoke(const std::string& device) {
    auto it = devices_.find(device);
    if (it == devices_.end()) return Status::Err("no such device");
    if (it->second.conn) {
        SendRaw(it->second.conn, Json::Object().Set("t", "revoked"), true);
        conns_.erase(it->second.conn);
    }
    devices_.erase(it);
    events_.push_back({HubEvent::Kind::Revoked, device, "unpaired by the TV"});
    return Status::Ok();
}

std::vector<DeviceInfo> ControllerHub::Devices() const {
    std::vector<DeviceInfo> v;
    for (const auto& [id, d] : devices_) v.push_back(d.info);
    return v;
}

const DeviceInfo* ControllerHub::Device(const std::string& id) const {
    auto it = devices_.find(id);
    return it == devices_.end() ? nullptr : &it->second.info;
}

std::string ControllerHub::ExportDevices() const {
    std::string out = "cu-devices 1\n";
    for (const auto& [id, d] : devices_) {
        Json j = Json::Object();
        j.Set("id", id).Set("key", Base64Url(d.key)).Set("name", d.info.name).Set("side", d.info.side);
        j.Set("role", ToString(d.info.role)).Set("last", d.info.lastActionId);
        out += j.Dump() + "\n";
    }
    return out;
}

Status ControllerHub::ImportDevices(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "cu-devices 1") return Status::Err("not a device list");
    std::map<std::string, DeviceRec> loaded;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto j = Json::Parse(line);
        std::string key;
        auto role = j ? RoleFromString(j->Str("role")) : std::nullopt;
        if (!j || !role || !Base64UrlDecode(j->Str("key"), &key) || key.size() != 32 || j->Str("id").empty())
            return Status::Err("damaged device list");
        DeviceRec d;
        d.key = key;
        d.info.id = j->Str("id");
        d.info.name = j->Str("name");
        d.info.side = static_cast<int>(j->Int("side", -1));
        d.info.role = *role;
        d.info.lastActionId = j->Int("last", 0);
        loaded[d.info.id] = d;
    }
    if (loaded.size() > cfg_.maxDevices) return Status::Err("too many devices");
    devices_ = std::move(loaded);
    return Status::Ok();
}

// ---- transport side ------------------------------------------------------------

std::optional<ConnId> ControllerHub::Open(int64_t now) {
    if (conns_.size() >= cfg_.maxConnections) return std::nullopt;
    const ConnId id = nextConn_++;
    Conn k;
    k.openedAt = now;
    k.windowStart = now;
    conns_[id] = k;
    return id;
}

void ControllerHub::Fail(ConnId c, const std::string& why, const std::string& device) {
    SendRaw(c, Json::Object().Set("t", "error").Set("why", why), true);
    events_.push_back({HubEvent::Kind::Security, device, why});
    auto it = conns_.find(c);
    if (it != conns_.end()) {
        if (it->second.stage == Stage::Ready && !it->second.device.empty()) {
            auto d = devices_.find(it->second.device);
            if (d != devices_.end() && d->second.conn == c) d->second.conn = 0;
        }
        conns_.erase(it);
    }
}

void ControllerHub::Receive(ConnId c, const std::string& text, int64_t now) {
    auto it = conns_.find(c);
    if (it == conns_.end()) return;
    Conn& k = it->second;
    if (text.size() > cfg_.maxMessageBytes) return Fail(c, "message too large", k.device);
    // Rate limit per connection (one-second windows).
    if (now - k.windowStart >= 1000) {
        k.windowStart = now;
        k.windowCount = 0;
    }
    if (++k.windowCount > cfg_.maxMessagesPerSecond) return Fail(c, "too many messages", k.device);
    std::string err;
    auto j = Json::Parse(text, &err);
    if (!j || !j->IsObject()) return Fail(c, "malformed message", k.device);
    const std::string t = j->Str("t");
    if (k.stage == Stage::New && (t == "hello" || t == "resume")) return OnHello(c, k, *j, now);
    if (k.stage == Stage::Challenged && t == "prove") return OnProve(c, k, *j, now);
    if (k.stage == Stage::Ready && t == "m") return OnMessage(c, k, *j, now);
    return Fail(c, "unexpected message", k.device);
}

void ControllerHub::OnHello(ConnId c, Conn& k, const Json& j, int64_t now) {
    const std::string nc = j.Str("nc");
    if (!IsHex(nc, 32)) return Fail(c, "bad nonce");
    if (j.Str("t") == "hello") {
        auto p = pairings_.find(j.Str("pair"));
        if (p == pairings_.end()) return Fail(c, "unknown or used pairing code");
        if (now > p->second.offer.expiresMs) {
            pairings_.erase(p);
            return Fail(c, "pairing code expired");
        }
        if (devices_.size() >= cfg_.maxDevices) return Fail(c, "too many paired devices");
        k.pairId = p->first;
    } else {
        auto d = devices_.find(j.Str("dev"));
        if (d == devices_.end()) return Fail(c, "unknown device: pair again");
        k.device = d->first;
    }
    k.nc = nc;
    k.ns = ToHex(rnd_(16));
    k.stage = Stage::Challenged;
    SendRaw(c, Json::Object().Set("t", "challenge").Set("ns", k.ns));
}

void ControllerHub::OnProve(ConnId c, Conn& k, const Json& j, int64_t now) {
    const std::string mac = j.Str("mac");
    std::string deviceKey;
    if (!k.pairId.empty()) {
        auto p = pairings_.find(k.pairId);
        if (p == pairings_.end()) return Fail(c, "pairing code no longer valid");
        const std::string want = HmacHex(p->second.offer.secret, "cu-pair|" + k.pairId + "|" + k.nc + "|" + k.ns);
        if (!ConstantTimeEqual(mac, want)) {
            if (++p->second.attempts >= cfg_.maxPairAttempts) pairings_.erase(p);  // burn the QR code
            return Fail(c, "pairing proof failed");
        }
        // Paired: create the device. The QR code is single-use.
        DeviceRec d;
        d.info.id = NewId(9);
        std::string name = j.Str("name").substr(0, 40);
        d.info.name = name.empty() ? "Phone" : name;
        d.info.side = p->second.offer.side;
        d.info.role = p->second.offer.role;
        d.key = HmacSha256(p->second.offer.secret, "cu-device|" + d.info.id);
        pairings_.erase(p);
        k.device = d.info.id;
        deviceKey = d.key;
        devices_[d.info.id] = d;
        events_.push_back({HubEvent::Kind::Paired, d.info.id, d.info.name});
    } else {
        auto d = devices_.find(k.device);
        if (d == devices_.end()) return Fail(c, "device was unpaired");
        const std::string want = HmacHex(d->second.key, "cu-resume|" + k.device + "|" + k.nc + "|" + k.ns);
        if (!ConstantTimeEqual(mac, want)) return Fail(c, "reconnect proof failed", k.device);
        deviceKey = d->second.key;
    }
    DeviceRec& d = devices_[k.device];
    // A newer connection replaces an older (half-open) one.
    if (d.conn != 0 && d.conn != c) {
        SendRaw(d.conn, Json::Object().Set("t", "replaced"), true);
        conns_.erase(d.conn);
    }
    const bool wasLost = d.info.lost;
    d.conn = c;
    d.info.connected = true;
    d.info.lost = false;
    d.info.lastSeenMs = now;
    k.sessionKey = HmacSha256(deviceKey, "cu-session|" + k.nc + "|" + k.ns);
    k.stage = Stage::Ready;
    Json w = Json::Object();
    w.Set("t", "welcome").Set("dev", k.device).Set("side", d.info.side).Set("role", ToString(d.info.role));
    w.Set("name", d.info.name).Set("last", d.info.lastActionId);
    w.Set("mac", HmacHex(k.sessionKey, "cu-welcome|" + k.device));
    SendRaw(c, w);
    events_.push_back({HubEvent::Kind::Connected, k.device, wasLost ? "back after being lost" : ""});
}

void ControllerHub::OnMessage(ConnId c, Conn& k, const Json& j, int64_t now) {
    const int64_t seq = j.Int("s", -1);
    const std::string b = j.Str("b");
    const std::string mac = j.Str("mac");
    if (seq <= k.inSeq) return Fail(c, "replayed or out-of-order message", k.device);
    if (!ConstantTimeEqual(mac, HmacHex(k.sessionKey, "c2s|" + std::to_string(seq) + "|" + b)))
        return Fail(c, "message signature invalid", k.device);
    k.inSeq = seq;
    auto body = Json::Parse(b);
    if (!body || !body->IsObject()) return Fail(c, "malformed body", k.device);
    DeviceRec& d = devices_[k.device];
    d.info.lastSeenMs = now;
    const std::string kind = body->Str("k");
    if (kind == "ping") {
        Json p = Json::Object();
        p.Set("k", "pong").Set("t", body->Num("t")).Set("st", static_cast<double>(now));
        SendBody(c, p);
        return;
    }
    if (kind == "action") {
        const int64_t id = body->Int("id", -1);
        const Json* a = body->Get("a");
        if (id <= 0 || !a || !a->IsObject()) {
            Json r = Json::Object();
            r.Set("k", "ack").Set("id", id).Set("ok", false).Set("err", "malformed action");
            SendBody(c, r);
            return;
        }
        if (id <= d.info.lastActionId) {
            // Already handled (the phone re-sent it after reconnecting): don't run it twice.
            Json r = Json::Object();
            r.Set("k", "ack").Set("id", id).Set("ok", true).Set("dup", true);
            SendBody(c, r);
            return;
        }
        d.info.lastActionId = id;
        actions_.push_back({k.device, d.info.side, d.info.role, id, *a, now});
        return;
    }
    if (kind == "bye") {
        conns_.erase(c);
        d.conn = 0;
        d.info.connected = false;
        d.disconnectedAt = now;
        events_.push_back({HubEvent::Kind::Disconnected, k.device, "left"});
        return;
    }
    Json r = Json::Object();
    r.Set("k", "error").Set("err", "unknown message kind");
    SendBody(c, r);
}

void ControllerHub::Closed(ConnId c, int64_t now) {
    auto it = conns_.find(c);
    if (it == conns_.end()) return;
    const std::string device = it->second.device;
    const bool ready = it->second.stage == Stage::Ready;
    conns_.erase(it);
    if (!ready) return;
    auto d = devices_.find(device);
    if (d == devices_.end() || d->second.conn != c) return;
    d->second.conn = 0;
    d->second.info.connected = false;
    d->second.disconnectedAt = now;
    events_.push_back({HubEvent::Kind::Disconnected, device, "connection dropped"});
}

void ControllerHub::Tick(int64_t now) {
    for (auto it = pairings_.begin(); it != pairings_.end();)
        it = now > it->second.offer.expiresMs ? pairings_.erase(it) : std::next(it);
    std::vector<ConnId> stale;
    for (const auto& [id, k] : conns_)
        if (k.stage != Stage::Ready && now - k.openedAt > cfg_.handshakeTimeoutMs) stale.push_back(id);
    for (ConnId id : stale) Fail(id, "handshake timed out");
    for (auto& [id, d] : devices_)
        if (d.conn == 0 && !d.info.lost && d.disconnectedAt > 0 && now - d.disconnectedAt >= cfg_.reconnectGraceMs) {
            d.info.lost = true;
            events_.push_back({HubEvent::Kind::Lost, id, "did not reconnect in time"});
        }
}

std::vector<Outgoing> ControllerHub::TakeOutbox() { return std::exchange(outbox_, {}); }
std::vector<ControllerAction> ControllerHub::TakeActions() { return std::exchange(actions_, {}); }
std::vector<HubEvent> ControllerHub::TakeEvents() { return std::exchange(events_, {}); }

}  // namespace cricket::ctl
