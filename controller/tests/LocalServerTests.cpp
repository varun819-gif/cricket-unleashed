// LocalServer over real sockets: HTTP file serving and its limits, the TV-only
// page, the WebSocket handshake, frame rules, and a full pairing over the wire.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "../../core/tests/Test.h"
#include "PhoneSim.h"
#include "cricket/controller/LocalServer.h"

using namespace cricket;
using namespace cricket::ctl;
using namespace cricket::ctl::testing;
namespace fs = std::filesystem;

namespace {

std::string WebRoot() {
    static std::string d = [] {
        auto p = fs::temp_directory_path() / ("cu_web_" + std::to_string(::getpid()));
        fs::create_directories(p);
        for (const char* f : {"index.html", "app.js", "protocol.js", "style.css"}) {
            std::ofstream(p / f) << "content of " << f;
        }
        std::ofstream(p / "secret.txt") << "not for phones";
        return p.string();
    }();
    return d;
}

struct Server {
    ControllerHub hub;
    LocalServer server;
    int64_t now = 0;
    explicit Server(const std::string& bind = "127.0.0.1") : hub({}, Counter(77)), server(hub, [&] {
                   ServerConfig c;
                   c.port = 0;
                   c.bindAddress = bind;
                   c.webRoot = WebRoot();
                   return c;
               }()) {}
    void Poll() { server.Poll(5, now += 5); }
};

struct Client {
    Server& s;
    int fd = -1;
    std::string buf, lastHead;
    explicit Client(Server& srv, const std::string& ip = "127.0.0.1") : s(srv) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<uint16_t>(s.server.Port()));
        inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
        connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
        s.Poll();
    }
    ~Client() {
        if (fd >= 0) close(fd);
    }
    void Send(const std::string& data) {
        ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        for (int i = 0; i < 4; ++i) s.Poll();
    }
    // Reads whatever arrived (polling the server meanwhile). Returns false on EOF.
    bool Read(int rounds = 20) {
        bool open = true;
        for (int i = 0; i < rounds; ++i) {
            s.Poll();
            pollfd p{fd, POLLIN, 0};
            while (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
                char tmp[4096];
                ssize_t n = recv(fd, tmp, sizeof tmp, 0);
                if (n <= 0) {
                    open = false;
                    break;
                }
                buf.append(tmp, static_cast<size_t>(n));
            }
            if (!open) break;
        }
        return open;
    }
    std::string Get(const std::string& path, const std::string& extra = "") {
        Send("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + extra + "\r\n");
        Read();
        return buf;
    }
    // ---- WebSocket ----
    bool Upgrade(const std::string& origin = "") {
        std::string req = "GET /ws HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(s.server.Port()) +
                          "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                          "Sec-WebSocket-Version: 13\r\n";
        req += "Origin: " + (origin.empty() ? "http://127.0.0.1:" + std::to_string(s.server.Port()) : origin) + "\r\n\r\n";
        Send(req);
        Read();
        const size_t end = buf.find("\r\n\r\n");
        if (end == std::string::npos) return false;
        const std::string head = buf.substr(0, end);
        lastHead = head;
        buf.erase(0, end + 4);
        return head.find("101 Switching") != std::string::npos &&
               head.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos;
    }
    static std::string Frame(int opcode, const std::string& payload, bool masked = true, bool fin = true) {
        std::string f;
        f += static_cast<char>((fin ? 0x80 : 0) | opcode);
        const size_t n = payload.size();
        const char m = masked ? static_cast<char>(0x80) : 0;
        if (n < 126) f += static_cast<char>(m | static_cast<char>(n));
        else {
            f += static_cast<char>(m | 126);
            f += static_cast<char>(n >> 8);
            f += static_cast<char>(n & 0xff);
        }
        const char key[4] = {0x12, 0x34, 0x56, 0x78};
        if (masked) f.append(key, 4);
        for (size_t i = 0; i < n; ++i) f += masked ? static_cast<char>(payload[i] ^ key[i % 4]) : payload[i];
        return f;
    }
    void SendText(const std::string& t) { Send(Frame(1, t)); }
    // Next complete server frame: (opcode, payload). opcode -1 if none.
    std::pair<int, std::string> Next() {
        for (int tries = 0; tries < 3; ++tries) {
            if (buf.size() >= 2) {
                const int op = static_cast<uint8_t>(buf[0]) & 0x0f;
                size_t len = static_cast<uint8_t>(buf[1]) & 0x7f, pos = 2;
                if (len == 126 && buf.size() >= 4) {
                    len = (static_cast<uint8_t>(buf[2]) << 8) | static_cast<uint8_t>(buf[3]);
                    pos = 4;
                }
                if (buf.size() >= pos + len) {
                    std::string p = buf.substr(pos, len);
                    buf.erase(0, pos + len);
                    return {op, p};
                }
            }
            Read(5);
        }
        return {-1, ""};
    }
    std::optional<Json> NextJson() {
        auto [op, p] = Next();
        if (op != 1) return std::nullopt;
        return Json::Parse(p);
    }
};

}  // namespace

TEST(ctl_server_serves_only_the_controller_files) {
    Server s;
    CHECK_OK(s.server.Start());
    CHECK(s.server.Port() > 0);
    {
        Client c(s);
        const std::string r = c.Get("/");
        CHECK(r.rfind("HTTP/1.1 200 OK", 0) == 0);
        CHECK(r.find("content of index.html") != std::string::npos);
        CHECK(r.find("Content-Security-Policy: default-src 'self'") != std::string::npos);
        CHECK(r.find("X-Frame-Options: DENY") != std::string::npos);
        CHECK(r.find("Cache-Control: no-store") != std::string::npos);
    }
    {
        Client c(s);
        CHECK(c.Get("/app.js?v=3").find("content of app.js") != std::string::npos);
    }
    for (const char* bad : {"/secret.txt", "/../CMakeLists.txt", "/%2e%2e/etc/passwd", "/app.js/../secret.txt"}) {
        Client c(s);
        const std::string r = c.Get(bad);
        CHECK(r.rfind("HTTP/1.1 404", 0) == 0);
        CHECK(r.find("not for phones") == std::string::npos);
    }
    {
        Client c(s);
        c.Send("POST / HTTP/1.1\r\nHost: x\r\n\r\n");
        c.Read();
        CHECK(c.buf.rfind("HTTP/1.1 405", 0) == 0);
    }
    {
        Client c(s);
        c.Send("GET / HTTP/1.1\r\nX-Big: " + std::string(9000, 'a'));
        c.Read();
        CHECK(c.buf.rfind("HTTP/1.1 431", 0) == 0);
    }
}

TEST(ctl_server_tv_page_only_for_the_tv_itself) {
    Server s;
    CHECK_OK(s.server.Start());
    s.server.SetTvPage([] { return std::string("<h1>Scan me</h1>"); }, [] { return std::string("{\"ok\":true}"); });
    Client c(s);  // connects from 127.0.0.1 = the TV/PC itself
    CHECK(c.Get("/tv").find("<h1>Scan me</h1>") != std::string::npos);
    Client d(s);
    CHECK(d.Get("/tv.json").find("{\"ok\":true}") != std::string::npos);
}

TEST(ctl_server_tv_page_refused_to_other_devices) {
    // Connect through this machine's network address, exactly as a phone on
    // the Wi-Fi would: the TV page (with its pairing secrets) is refused,
    // while the controller page is served.
    const auto addrs = LocalServer::LocalAddresses();
    if (addrs.empty()) {
        std::cerr << "  (skipped: no network address on this machine)\n";
        return;
    }
    Server s("0.0.0.0");
    CHECK_OK(s.server.Start());
    s.server.SetTvPage([] { return std::string("SECRET-QR"); }, [] { return std::string("SECRET"); });
    {
        Client c(s, addrs[0]);
        const std::string r = c.Get("/tv");
        CHECK(r.rfind("HTTP/1.1 403", 0) == 0);
        CHECK(r.find("SECRET") == std::string::npos);
    }
    {
        Client c(s, addrs[0]);
        CHECK(c.Get("/tv.json").find("SECRET") == std::string::npos);
    }
    {
        Client c(s, addrs[0]);
        CHECK(c.Get("/").rfind("HTTP/1.1 200", 0) == 0);
    }
}

TEST(ctl_server_websocket_handshake_rules) {
    Server s;
    CHECK_OK(s.server.Start());
    {
        Client c(s);
        CHECK(c.Upgrade());  // RFC 6455 accept value checked inside
    }
    {
        Client c(s);
        CHECK(!c.Upgrade("http://evil.example"));
        CHECK(c.lastHead.rfind("HTTP/1.1 403", 0) == 0);
    }
    {
        Client c(s);
        const std::string r = c.Get("/ws", "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n");
        CHECK(r.rfind("HTTP/1.1 400", 0) == 0);
    }
}

TEST(ctl_server_frame_rules) {
    Server s;
    CHECK_OK(s.server.Start());
    auto closeCode = [](const std::pair<int, std::string>& f) {
        return f.first == 8 && f.second.size() >= 2
                   ? (static_cast<uint8_t>(f.second[0]) << 8) | static_cast<uint8_t>(f.second[1])
                   : -1;
    };
    {
        Client c(s);
        CHECK(c.Upgrade());
        c.Send(Client::Frame(1, "{}", false));  // unmasked
        CHECK_EQ(closeCode(c.Next()), 1002);
    }
    {
        Client c(s);
        CHECK(c.Upgrade());
        c.Send(Client::Frame(2, "binary"));
        CHECK_EQ(closeCode(c.Next()), 1003);
    }
    {
        Client c(s);
        CHECK(c.Upgrade());
        c.Send(Client::Frame(1, std::string(20000, 'x')));
        CHECK_EQ(closeCode(c.Next()), 1009);
    }
    {
        Client c(s);
        CHECK(c.Upgrade());
        c.Send(Client::Frame(9, "hi"));  // ping
        auto f = c.Next();
        CHECK(f.first == 10 && f.second == "hi");
    }
}

TEST(ctl_server_full_pairing_over_the_wire) {
    Server s;
    CHECK_OK(s.server.Start());
    PairingOffer o = s.hub.CreatePairing(0, Role::Captain, 0);
    Client c(s);
    CHECK(c.Upgrade());
    const std::string nc = std::string(32, 'a');
    // The hello is split over two frames (fragmentation must work).
    const std::string hello = Json::Object().Set("t", "hello").Set("pair", o.pairId).Set("nc", nc).Dump();
    c.Send(Client::Frame(1, hello.substr(0, 10), true, false) + Client::Frame(0, hello.substr(10), true, true));
    auto ch = c.NextJson();
    CHECK(ch && ch->Str("t") == "challenge");
    if (!ch) return;
    const std::string ns = ch->Str("ns");
    c.SendText(Json::Object()
                   .Set("t", "prove")
                   .Set("mac", HmacHex(o.secret, "cu-pair|" + o.pairId + "|" + nc + "|" + ns))
                   .Set("name", "Pixel")
                   .Dump());
    auto w = c.NextJson();
    CHECK(w && w->Str("t") == "welcome");
    if (!w) return;
    const std::string dev = w->Str("dev");
    const std::string devKey = HmacSha256(o.secret, "cu-device|" + dev);
    const std::string sess = HmacSha256(devKey, "cu-session|" + nc + "|" + ns);
    CHECK(ConstantTimeEqual(w->Str("mac"), HmacHex(sess, "cu-welcome|" + dev)));
    const std::string b = Json::Object().Set("k", "action").Set("id", 1).Set("a", Json::Object().Set("do", "drive")).Dump();
    c.SendText(Json::Object().Set("t", "m").Set("s", 1).Set("b", b).Set("mac", HmacHex(sess, "c2s|1|" + b)).Dump());
    auto acts = s.hub.TakeActions();
    CHECK_EQ(acts.size(), size_t{1});
    if (!acts.empty()) CHECK_EQ(acts[0].action.Str("do"), std::string("drive"));
    s.hub.Ack(dev, 1, true);
    auto ack = c.NextJson();
    CHECK(ack && ack->Str("t") == "m");
    // The phone goes away: the hub hears about it.
    close(c.fd);
    c.fd = -1;
    for (int i = 0; i < 5; ++i) s.Poll();
    CHECK(!s.hub.Device(dev)->connected);
}

TEST(ctl_server_local_addresses_exclude_loopback) {
    for (const auto& a : LocalServer::LocalAddresses()) CHECK(a.rfind("127.", 0) != 0);
    CHECK(LocalServer::Routes().count("/") == 1);
    CHECK(LocalServer::Routes().count("/tv") == 0);  // never served as a file
}
