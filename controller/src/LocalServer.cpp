#include "cricket/controller/LocalServer.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace cricket::ctl {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string ReadFile(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    *ok = f.good();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

constexpr const char* kSecurityHeaders =
    "X-Content-Type-Options: nosniff\r\n"
    "X-Frame-Options: DENY\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
    "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n";

}  // namespace

const std::map<std::string, std::pair<std::string, std::string>>& LocalServer::Routes() {
    // URL path -> (file in webRoot, content type). Nothing else is served.
    static const std::map<std::string, std::pair<std::string, std::string>> r = {
        {"/", {"index.html", "text/html; charset=utf-8"}},
        {"/index.html", {"index.html", "text/html; charset=utf-8"}},
        {"/app.js", {"app.js", "text/javascript; charset=utf-8"}},
        {"/protocol.js", {"protocol.js", "text/javascript; charset=utf-8"}},
        {"/style.css", {"style.css", "text/css; charset=utf-8"}},
        {"/manifest.json", {"manifest.json", "application/manifest+json"}},
        {"/icon.svg", {"icon.svg", "image/svg+xml"}},
        {"/tv.js", {"tv.js", "text/javascript; charset=utf-8"}},
        {"/tv.css", {"tv.css", "text/css; charset=utf-8"}},
    };
    return r;
}

LocalServer::LocalServer(ConnectionSink& hub, ServerConfig cfg) : hub_(hub), cfg_(std::move(cfg)) {
    if (cfg_.routes.empty()) cfg_.routes = Routes();
}

LocalServer::~LocalServer() { Stop(); }

void LocalServer::SetTvPage(std::function<std::string()> html, std::function<std::string()> json) {
    tvHtml_ = std::move(html);
    tvJson_ = std::move(json);
}

Status LocalServer::Start() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) return Status::Err(std::string("socket: ") + std::strerror(errno));
    int one = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg_.port));
    if (inet_pton(AF_INET, cfg_.bindAddress.c_str(), &addr.sin_addr) != 1) return Status::Err("bad bind address");
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        Status s = Status::Err("cannot use port " + std::to_string(cfg_.port) + ": " + std::strerror(errno));
        Stop();
        return s;
    }
    if (listen(listenFd_, 16) != 0 || !SetNonBlocking(listenFd_)) {
        Stop();
        return Status::Err("listen failed");
    }
    socklen_t len = sizeof addr;
    getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    return Status::Ok();
}

void LocalServer::Stop() {
    for (auto& [fd, c] : clients_) {
        if (c.websocket) hub_.Closed(c.conn, 0);
        close(fd);
    }
    clients_.clear();
    connToFd_.clear();
    if (listenFd_ >= 0) close(listenFd_);
    listenFd_ = -1;
}

std::vector<std::string> LocalServer::LocalAddresses() {
    std::vector<std::string> out;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (a->ifa_flags & IFF_LOOPBACK || !(a->ifa_flags & IFF_UP)) continue;
        char buf[INET_ADDRSTRLEN];
        auto* in = reinterpret_cast<sockaddr_in*>(a->ifa_addr);
        if (inet_ntop(AF_INET, &in->sin_addr, buf, sizeof buf)) out.emplace_back(buf);
    }
    freeifaddrs(list);
    // Home networks first (192.168.x, 10.x, 172.16-31.x).
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        auto rank = [](const std::string& s) { return s.rfind("192.168.", 0) == 0 ? 0 : s.rfind("10.", 0) == 0 ? 1 : 2; };
        return rank(a) < rank(b);
    });
    return out;
}

void LocalServer::Accept(int64_t now) {
    while (true) {
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        int fd = accept(listenFd_, reinterpret_cast<sockaddr*>(&peer), &len);
        if (fd < 0) return;
        if (clients_.size() >= cfg_.maxClients || !SetNonBlocking(fd)) {
            close(fd);
            continue;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);  // controls must feel instant
        Client c;
        c.fd = fd;
        c.openedAt = now;
        c.loopback = (ntohl(peer.sin_addr.s_addr) >> 24) == 127;
        clients_[fd] = c;
    }
}

void LocalServer::SendHttp(Client& c, int code, const std::string& reason, const std::string& type,
                           const std::string& body, const std::string& extra) {
    std::string r = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n";
    r += "Content-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    r += kSecurityHeaders + extra + "Connection: close\r\n\r\n" + body;
    c.out += r;
    c.closing = true;
}

void LocalServer::OnHttp(Client& c, int64_t now) {
    const size_t end = c.in.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (c.in.size() > cfg_.maxHttpBytes) SendHttp(c, 431, "Request Header Fields Too Large", "text/plain", "too large");
        return;
    }
    const std::string head = c.in.substr(0, end);
    c.in.erase(0, end + 4);
    std::istringstream lines(head);
    std::string requestLine;
    std::getline(lines, requestLine);
    std::istringstream rl(requestLine);
    std::string method, target, version;
    rl >> method >> target >> version;
    std::map<std::string, std::string> h;
    for (std::string line; std::getline(lines, line);) {
        const size_t colon = line.find(':');
        if (colon != std::string::npos) h[Lower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
    }
    if (method != "GET") return SendHttp(c, 405, "Method Not Allowed", "text/plain", "GET only");
    const std::string path = target.substr(0, target.find('?'));

    if (path == "/ws") {
        const std::string key = h["sec-websocket-key"];
        const std::string host = h["host"];
        if (Lower(h["upgrade"]) != "websocket" || h["sec-websocket-version"] != "13" || key.size() != 24)
            return SendHttp(c, 400, "Bad Request", "text/plain", "WebSocket upgrade required");
        // Only the controller page itself may open the socket (blocks other
        // websites in the phone's browser from talking to the TV).
        // (behind a TLS proxy on the internet the page origin is https://)
        if (h.count("origin") && h["origin"] != "http://" + host && h["origin"] != "https://" + host)
            return SendHttp(c, 403, "Forbidden", "text/plain", "wrong origin");
        auto conn = hub_.Open(now);
        if (!conn) return SendHttp(c, 503, "Service Unavailable", "text/plain", "too many controllers");
        auto digest = Sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        c.out += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: " +
                 Base64(std::string(digest.begin(), digest.end())) + "\r\n\r\n";
        c.websocket = true;
        c.conn = *conn;
        connToFd_[c.conn] = c.fd;
        if (!c.in.empty()) OnFrames(c, now);
        return;
    }
    if (path == "/tv" || path == "/tv.json") {
        if (!c.loopback) return SendHttp(c, 403, "Forbidden", "text/plain", "the TV page is only shown on the TV itself");
        if (path == "/tv" && tvHtml_) return SendHttp(c, 200, "OK", "text/html; charset=utf-8", tvHtml_());
        if (path == "/tv.json" && tvJson_) return SendHttp(c, 200, "OK", "application/json", tvJson_());
        return SendHttp(c, 404, "Not Found", "text/plain", "no TV page");
    }
    auto r = cfg_.routes.find(path);
    if (r == cfg_.routes.end()) return SendHttp(c, 404, "Not Found", "text/plain", "not found");
    bool ok = false;
    const std::string& file = r->second.first;
    std::string body = ReadFile(!file.empty() && file[0] == '/' ? file : cfg_.webRoot + "/" + file, &ok);
    if (!ok) return SendHttp(c, 404, "Not Found", "text/plain", "missing file");
    SendHttp(c, 200, "OK", r->second.second, body);
}

void LocalServer::SendFrame(Client& c, int opcode, const std::string& payload) {
    std::string f;
    f += static_cast<char>(0x80 | opcode);
    const size_t n = payload.size();
    if (n < 126) {
        f += static_cast<char>(n);
    } else if (n < 65536) {
        f += static_cast<char>(126);
        f += static_cast<char>((n >> 8) & 0xff);
        f += static_cast<char>(n & 0xff);
    } else {
        f += static_cast<char>(127);
        for (int i = 7; i >= 0; --i) f += static_cast<char>((static_cast<uint64_t>(n) >> (8 * i)) & 0xff);
    }
    c.out += f + payload;
}

void LocalServer::CloseWs(Client& c, uint16_t code, int64_t now) {
    if (c.closing) return;
    std::string p;
    p += static_cast<char>(code >> 8);
    p += static_cast<char>(code & 0xff);
    SendFrame(c, 8, p);
    c.closing = true;
    hub_.Closed(c.conn, now);
    connToFd_.erase(c.conn);
}

void LocalServer::OnFrames(Client& c, int64_t now) {
    while (!c.closing && c.in.size() >= 2) {
        const uint8_t b0 = static_cast<uint8_t>(c.in[0]), b1 = static_cast<uint8_t>(c.in[1]);
        const bool fin = b0 & 0x80;
        const int opcode = b0 & 0x0f;
        const bool masked = b1 & 0x80;
        uint64_t len = b1 & 0x7f;
        size_t pos = 2;
        if ((b0 & 0x70) != 0) return CloseWs(c, 1002, now);  // no extensions negotiated
        if (!masked) return CloseWs(c, 1002, now);           // clients must mask (RFC 6455 5.1)
        if (len == 126) {
            if (c.in.size() < 4) return;
            len = (static_cast<uint8_t>(c.in[2]) << 8) | static_cast<uint8_t>(c.in[3]);
            pos = 4;
        } else if (len == 127) {
            if (c.in.size() < 10) return;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | static_cast<uint8_t>(c.in[2 + i]);
            pos = 10;
        }
        if (len > cfg_.maxFrameBytes || c.fragments.size() + len > cfg_.maxFrameBytes) return CloseWs(c, 1009, now);
        if (c.in.size() < pos + 4 + len) return;  // wait for the rest
        const std::string mask = c.in.substr(pos, 4);
        std::string payload = c.in.substr(pos + 4, len);
        for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i % 4];
        c.in.erase(0, pos + 4 + len);
        switch (opcode) {
            case 0x0:  // continuation
            case 0x1:  // text
                if (opcode == 0x1 && !c.fragments.empty()) return CloseWs(c, 1002, now);
                c.fragments += payload;
                if (fin) {
                    const std::string msg = std::move(c.fragments);
                    c.fragments.clear();
                    hub_.Receive(c.conn, msg, now);
                }
                break;
            case 0x8: return CloseWs(c, 1000, now);  // phone said goodbye
            case 0x9: SendFrame(c, 0xA, payload); break;  // ping -> pong
            case 0xA: break;                              // pong
            default: return CloseWs(c, 1003, now);        // binary / unknown: not used
        }
    }
}

void LocalServer::OnReadable(Client& c, int64_t now) {
    char buf[4096];
    while (true) {
        ssize_t n = recv(c.fd, buf, sizeof buf, 0);
        if (n > 0) {
            c.in.append(buf, static_cast<size_t>(n));
            if (!c.websocket && c.in.size() > cfg_.maxHttpBytes + 4) break;
            continue;
        }
        if (n == 0) {
            c.closing = true;
            c.out.clear();
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        c.closing = true;
        c.out.clear();
        return;
    }
    if (c.websocket) OnFrames(c, now);
    else OnHttp(c, now);
}

void LocalServer::Flush(Client& c) {
    while (!c.out.empty()) {
        ssize_t n = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0) {
            c.out.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        c.out.clear();
        c.closing = true;
        return;
    }
}

void LocalServer::Drop(int fd, int64_t now) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    if (it->second.websocket && connToFd_.count(it->second.conn)) {
        hub_.Closed(it->second.conn, now);
        connToFd_.erase(it->second.conn);
    }
    close(fd);
    clients_.erase(it);
}

void LocalServer::Poll(int timeoutMs, int64_t now) {
    if (listenFd_ < 0) return;
    // Queue whatever the hub wants to send.
    auto pump = [&] {
        for (auto& o : hub_.TakeOutbox()) {
            auto f = connToFd_.find(o.conn);
            if (f == connToFd_.end()) continue;
            Client& c = clients_[f->second];
            SendFrame(c, 0x1, o.text);
            if (o.close) CloseWs(c, 1008, now);
        }
    };
    pump();
    std::vector<pollfd> fds;
    fds.push_back({listenFd_, POLLIN, 0});
    for (auto& [fd, c] : clients_) fds.push_back({fd, static_cast<short>(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
    if (poll(fds.data(), fds.size(), timeoutMs) < 0) return;
    if (fds[0].revents & POLLIN) Accept(now);
    for (size_t i = 1; i < fds.size(); ++i) {
        auto it = clients_.find(fds[i].fd);
        if (it == clients_.end()) continue;
        if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) OnReadable(it->second, now);
    }
    pump();
    std::vector<int> done;
    for (auto& [fd, c] : clients_) {
        Flush(c);
        if (c.closing && c.closingSince < 0) c.closingSince = now;
        const bool stalledHttp = !c.websocket && now - c.openedAt > cfg_.httpTimeoutMs;
        const bool notReading = c.out.size() > cfg_.maxPendingOutBytes;
        const bool closeStuck = c.closing && now - c.closingSince > 5000;  // peer never read our last bytes
        if ((c.closing && c.out.empty()) || stalledHttp || notReading || closeStuck) done.push_back(fd);
    }
    for (int fd : done) Drop(fd, now);
}

}  // namespace cricket::ctl
