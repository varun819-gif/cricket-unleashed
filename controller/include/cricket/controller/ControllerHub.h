// ControllerHub: pairs phones with the game and authenticates every message.
// It does no networking itself - the local server (LocalServer) feeds it
// WebSocket text frames and sends what it puts in the outbox - so the whole
// security protocol is unit-testable. See docs/MOBILE_CONTROLLER.md.
//
// Protocol summary (all MACs are HMAC-SHA256, lower-case hex):
//   QR link  http://<tv-ip>:<port>/#p=<pairId>.<secret>
//            The part after '#' is never sent over the network by browsers,
//            so the 32-byte secret only travels screen -> camera.
//   phone -> {"t":"hello","pair":pairId,"nc":<32 hex>}        (new phone)
//         or {"t":"resume","dev":deviceId,"nc":<32 hex>}       (reconnect)
//   tv    -> {"t":"challenge","ns":<32 hex>}
//   phone -> {"t":"prove","mac":HMAC(K,"cu-pair|"+pairId+"|"+nc+"|"+ns),"name":...}
//            (resume: HMAC(deviceKey,"cu-resume|"+dev+"|"+nc+"|"+ns))
//   tv    -> {"t":"welcome","dev":..,"side":..,"role":..,"mac":HMAC(sessionKey,"cu-welcome|"+dev)}
//            proves the TV knows the secret too (no fake TV on the network)
//   deviceKey  = HMAC(K, "cu-device|"+deviceId)       (never transmitted)
//   sessionKey = HMAC(deviceKey, "cu-session|"+nc+"|"+ns)
//   then both ways {"t":"m","s":<seq>,"b":"<json text>","mac":HMAC(sessionKey, dir+"|"+seq+"|"+b)}
//   with dir "c2s" or "s2c" and seq strictly increasing (replays rejected).
// Messages are authenticated, not encrypted (see the security notes in the doc).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "cricket/controller/Crypto.h"
#include "cricket/Types.h"
#include "cricket/controller/Json.h"

namespace cricket::ctl {

enum class Role { Captain, Player, Spectator };
const char* ToString(Role r);
std::optional<Role> RoleFromString(const std::string& s);

using ConnId = uint64_t;

struct HubConfig {
    int64_t pairingTtlMs = 120000;      // QR code valid for 2 minutes
    int maxPairAttempts = 3;            // wrong proofs before a QR code is burned
    int64_t reconnectGraceMs = 60000;   // after this, a dropped phone is reported "lost"
    int64_t handshakeTimeoutMs = 10000; // unauthenticated sockets are closed after this
    size_t maxDevices = 8;
    size_t maxConnections = 16;
    size_t maxMessageBytes = 8192;
    int maxMessagesPerSecond = 60;
};

struct PairingOffer {
    std::string pairId;
    std::string secret;  // raw 32 bytes - shown only inside the QR code
    int side = -1;       // 0 / 1, or -1 for a spectator
    Role role = Role::Captain;
    int64_t expiresMs = 0;
    // The link the QR code encodes.
    std::string Link(const std::string& host, int port) const;
};

struct DeviceInfo {
    std::string id;
    std::string name;
    int side = -1;
    Role role = Role::Spectator;
    bool connected = false;
    bool lost = false;               // gone longer than the grace period
    int64_t lastSeenMs = 0;
    int64_t lastActionId = 0;        // for exactly-once delivery across reconnects
};

// An authenticated control input from a phone.
struct ControllerAction {
    std::string device;
    int side = -1;
    Role role = Role::Spectator;
    int64_t id = 0;   // the phone's action number (monotonic, survives reconnects)
    Json action;
};

struct HubEvent {
    enum class Kind { Paired, Connected, Disconnected, Lost, Revoked, Security } kind;
    std::string device;
    std::string detail;
};

struct Outgoing {
    ConnId conn = 0;
    std::string text;
    bool close = false;  // close the socket after sending
};

class ControllerHub {
public:
    explicit ControllerHub(HubConfig cfg = {}, RandomSource rnd = SystemRandom());

    // ---- TV / game side -----------------------------------------------
    PairingOffer CreatePairing(int side, Role role, int64_t nowMs);
    void CancelPairing(const std::string& pairId);
    size_t PendingPairings() const { return pairings_.size(); }
    // Still unused and not expired (as of the last Tick).
    bool IsPending(const std::string& pairId) const { return pairings_.count(pairId) > 0; }
    // Sends a message body to one phone / to every connected phone of a side
    // (side -2 = everyone). Bodies are JSON objects.
    bool SendTo(const std::string& device, const Json& body);
    void Broadcast(int side, const Json& body);
    // Tells the phone whether its action was accepted.
    void Ack(const std::string& device, int64_t actionId, bool ok, const std::string& error = "");
    Status Revoke(const std::string& device);
    std::vector<DeviceInfo> Devices() const;
    const DeviceInfo* Device(const std::string& id) const;

    // Paired devices survive a TV/game restart: export their keys to local
    // storage and import them on start-up (the secret file must stay private).
    std::string ExportDevices() const;
    Status ImportDevices(const std::string& text);

    // ---- transport side -------------------------------------------------
    std::optional<ConnId> Open(int64_t nowMs);  // nullopt: too many connections
    void Receive(ConnId conn, const std::string& text, int64_t nowMs);
    void Closed(ConnId conn, int64_t nowMs);
    void Tick(int64_t nowMs);

    std::vector<Outgoing> TakeOutbox();
    std::vector<ControllerAction> TakeActions();
    std::vector<HubEvent> TakeEvents();

private:
    struct Pairing {
        PairingOffer offer;
        int attempts = 0;
    };
    struct DeviceRec {
        DeviceInfo info;
        std::string key;  // device key (32 bytes)
        ConnId conn = 0;  // 0 = not connected
        int64_t disconnectedAt = 0;
    };
    enum class Stage { New, Challenged, Ready };
    struct Conn {
        Stage stage = Stage::New;
        int64_t openedAt = 0;
        std::string pairId, device, nc, ns;
        std::string sessionKey;
        int64_t inSeq = 0, outSeq = 0;
        int64_t windowStart = 0;
        int windowCount = 0;
    };

    void Fail(ConnId c, const std::string& why, const std::string& device = "");
    void SendRaw(ConnId c, const Json& j, bool close = false);
    void SendBody(ConnId c, const Json& body);
    void OnHello(ConnId c, Conn& k, const Json& j, int64_t now);
    void OnProve(ConnId c, Conn& k, const Json& j, int64_t now);
    void OnMessage(ConnId c, Conn& k, const Json& j, int64_t now);
    std::string NewId(size_t bytes);
    static bool IsHex(const std::string& s, size_t len);

    HubConfig cfg_;
    RandomSource rnd_;
    std::map<std::string, Pairing> pairings_;
    std::map<std::string, DeviceRec> devices_;
    std::map<ConnId, Conn> conns_;
    ConnId nextConn_ = 1;
    std::vector<Outgoing> outbox_;
    std::vector<ControllerAction> actions_;
    std::vector<HubEvent> events_;
};

}  // namespace cricket::ctl
