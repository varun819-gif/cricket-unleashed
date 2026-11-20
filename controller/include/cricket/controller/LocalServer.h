// LocalServer: the small web server the TV/PC game runs on the home Wi-Fi.
//  * Serves the phone controller page (a fixed list of files - nothing else
//    on the TV can be read) with strict browser security headers.
//  * Upgrades /ws to a WebSocket (RFC 6455) and feeds text frames to the
//    ControllerHub; only same-origin pages may connect.
//  * /tv and /tv.json (QR codes and pairing links) answer only requests from
//    the TV/PC itself (loopback), never from other devices on the network.
// Single-threaded, non-blocking, POSIX sockets (Linux / Android). A Windows
// build needs a Winsock port of the socket calls (see MOBILE_CONTROLLER.md).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "cricket/controller/ControllerHub.h"

namespace cricket::ctl {

struct ServerConfig {
    int port = 47321;                   // 0 = any free port (tests)
    std::string bindAddress = "0.0.0.0";
    std::string webRoot;                // folder with the controller files
    size_t maxHttpBytes = 8192;
    size_t maxFrameBytes = 16384;
    size_t maxClients = 32;
    // A connection must finish its HTTP request within this time (stops
    // "slow" connections from filling the server).
    int64_t httpTimeoutMs = 10000;
    // A client that stops reading is dropped once this much is waiting for it.
    size_t maxPendingOutBytes = 1 << 20;
    // URL path -> (file path, content type). Relative file paths are looked
    // up in webRoot. Empty = the phone-controller files (Routes()).
    std::map<std::string, std::pair<std::string, std::string>> routes;
};

class LocalServer {
public:
    LocalServer(ConnectionSink& hub, ServerConfig cfg);
    ~LocalServer();
    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;

    Status Start();
    void Stop();
    int Port() const { return port_; }
    // Waits up to timeoutMs for network activity, handles it, and sends
    // everything the hub has queued.
    void Poll(int timeoutMs, int64_t nowMs);

    // Content for the TV-only pages (set by the game / host tool).
    void SetTvPage(std::function<std::string()> html, std::function<std::string()> json);

    // This machine's IPv4 addresses on the local network (for the QR link).
    static std::vector<std::string> LocalAddresses();

    // Exposed for tests: the files a phone may download.
    static const std::map<std::string, std::pair<std::string, std::string>>& Routes();

private:
    struct Client {
        int fd = -1;
        int64_t openedAt = 0;
        int64_t closingSince = -1;
        bool loopback = false;
        bool websocket = false;
        bool closing = false;
        ConnId conn = 0;
        std::string in, out;
        std::string fragments;  // a message split over several frames
    };
    void Accept(int64_t now);
    void OnReadable(Client& c, int64_t now);
    void OnHttp(Client& c, int64_t now);
    void OnFrames(Client& c, int64_t now);
    void SendHttp(Client& c, int code, const std::string& reason, const std::string& type, const std::string& body,
                  const std::string& extraHeaders = "");
    void SendFrame(Client& c, int opcode, const std::string& payload);
    void CloseWs(Client& c, uint16_t code, int64_t now);
    void Drop(int fd, int64_t now);
    void Flush(Client& c);

    ConnectionSink& hub_;
    ServerConfig cfg_;
    int listenFd_ = -1;
    int port_ = 0;
    std::map<int, Client> clients_;
    std::map<ConnId, int> connToFd_;
    std::function<std::string()> tvHtml_, tvJson_;
};

}  // namespace cricket::ctl
