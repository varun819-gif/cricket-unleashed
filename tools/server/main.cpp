// cricket_server: the self-hosted online game server (free to run).
//
// Serves the online web page and the game WebSocket (/ws) on one port. On
// the internet, put it behind a free TLS front end (Caddy with Let's Encrypt,
// or a free tunnel) so players connect with https:// and wss:// - see
// docs/ONLINE_SERVER_SETUP.md. /tv.json answers only on this machine and
// shows a health summary.
//
// Usage: cricket_server [--port N] [--bind ADDR] [--data DIR] [--web DIR]
//          [--region in] [--grace-sec N] [--ranked-unlock N] [--max-connections N] [--quiet]
//          [--exit-after-ms N]
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "cricket/controller/LocalServer.h"
#include "cricket/online/OnlineServer.h"

using namespace cricket;
using namespace cricket::online;

namespace {

volatile std::sig_atomic_t gStop = 0;
void OnSignal(int) { gStop = 1; }

// Wall-clock time: match journals survive restarts, so the clock must keep
// counting across them (a per-match offset covers a clock that went back).
int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

int main(int argc, char** argv) {
    int port = 8080;
    std::string bind = "0.0.0.0", data = "cu-data", web = std::string(CRICKET_SOURCE_DIR) + "/online/web";
    std::string controllerWeb = std::string(CRICKET_SOURCE_DIR) + "/controller/web";
    OnlineConfig cfg;
    bool quiet = false;
    int64_t exitAfter = 0;
    size_t maxConnections = 2000;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--port") port = std::atoi(next().c_str());
        else if (a == "--bind") bind = next();
        else if (a == "--data") data = next();
        else if (a == "--web") web = next();
        else if (a == "--controller-web") controllerWeb = next();
        else if (a == "--region") cfg.region = next();
        else if (a == "--grace-sec") cfg.graceMs = std::atoll(next().c_str()) * 1000;
        else if (a == "--ranked-unlock") cfg.rankedUnlockMatches = std::atoi(next().c_str());
        else if (a == "--dev-short-matches") cfg.formats = {"t20", "overs=1", "overs=2", "overs=5"};  // automated tests only
        else if (a == "--dev-fast") {  // automated tests only: quicker balls and windows
            cfg.host.ballFlightMs = 700;
            cfg.host.batWindowMs = 700;
            cfg.host.fieldWindowMs = 500;
        }
        else if (a == "--max-connections") maxConnections = static_cast<size_t>(std::atoll(next().c_str()));
        else if (a == "--quiet") quiet = true;
        else if (a == "--exit-after-ms") exitAfter = std::atoll(next().c_str());
        else {
            std::cerr << "usage: cricket_server [--port N] [--bind ADDR] [--data DIR] [--web DIR] [--region in]\n"
                         "         [--grace-sec N] [--ranked-unlock N] [--max-connections N] [--quiet] [--exit-after-ms N]\n";
            return 2;
        }
    }
    cfg.dataDir = data;
    cfg.maxConnections = maxConnections;
    // Only one server may use a data folder (two would corrupt it).
    std::error_code ec;
    std::filesystem::create_directories(data, ec);
    const int lockFd = ::open((data + "/.lock").c_str(), O_CREAT | O_RDWR, 0600);
    if (lockFd < 0 || ::flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        std::cerr << "another cricket_server is already using " << data << "\n";
        return 1;
    }
    Store store;
    if (Status s = store.Open(data); !s) {
        std::cerr << "cannot open data folder: " << s.error << "\n";
        return 1;
    }
    // Admin token for the moderation page (/admin). Created once, readable
    // only by this user on this machine. Anyone with it can ban players.
    {
        const std::string tokenFile = data + "/admin.token";
        std::ifstream in(tokenFile);
        std::getline(in, cfg.adminToken);
        if (cfg.adminToken.size() < 32) {
            const std::string fresh = ctl::Base64Url(ctl::SystemRandom()(32));
            const int fd = ::open(tokenFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
            if (fd >= 0) {
                const std::string line = fresh + "\n";
                if (::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size())) cfg.adminToken = fresh;
                ::close(fd);
            }
        }
    }
    OnlineServer game(store, cfg);
    const int64_t start = NowMs();
    const size_t recovered = game.RecoverLiveMatches(start);

    ctl::ServerConfig sc;
    sc.port = port;
    sc.bindAddress = bind;
    sc.maxClients = maxConnections;
    sc.routes = {
        {"/", {web + "/index.html", "text/html; charset=utf-8"}},
        {"/index.html", {web + "/index.html", "text/html; charset=utf-8"}},
        {"/online.js", {web + "/online.js", "text/javascript; charset=utf-8"}},
        {"/lobby.js", {web + "/lobby.js", "text/javascript; charset=utf-8"}},
        {"/lobby.css", {web + "/lobby.css", "text/css; charset=utf-8"}},
        {"/manifest.json", {web + "/manifest.json", "application/manifest+json"}},
        {"/admin", {web + "/admin.html", "text/html; charset=utf-8"}},
        {"/admin.js", {web + "/admin.js", "text/javascript; charset=utf-8"}},
        {"/app.js", {controllerWeb + "/app.js", "text/javascript; charset=utf-8"}},
        {"/protocol.js", {controllerWeb + "/protocol.js", "text/javascript; charset=utf-8"}},
        {"/style.css", {controllerWeb + "/style.css", "text/css; charset=utf-8"}},
        {"/icon.svg", {controllerWeb + "/icon.svg", "image/svg+xml"}},
    };
    for (const char* f : {"IN", "PK", "AU", "ENG", "ZA", "NZ", "LK", "BD", "WI", "AF"})
        sc.routes["/flags/" + std::string(f) + ".svg"] = {web + "/flags/" + f + ".svg", "image/svg+xml"};
    ctl::LocalServer server(game, sc);
    server.SetTvPage([] { return std::string("<!doctype html><title>Cricket Unleashed server</title><p>See /tv.json"); },
                     [&] { return game.Health(NowMs()).Dump(); });
    if (Status s = server.Start(); !s) {
        std::cerr << "cannot start: " << s.error << "\n";
        return 1;
    }
    std::cout << "LISTENING " << server.Port() << std::endl;
    if (!quiet) {
        std::cout << "Cricket Unleashed online server - region " << cfg.region << ", data in " << data << "\n"
                  << "  accounts: " << store.AccountCount() << ", live matches recovered: " << recovered << "\n"
                  << "  open http://localhost:" << server.Port() << "/ (use https via a TLS proxy on the internet)\n"
                  << "  health: http://localhost:" << server.Port() << "/tv.json\n"
                  << "  moderation: /admin with the token in " << data << "/admin.token (keep it secret)\n"
                  << "  stop with Ctrl+C (live matches are journaled and resume on restart)" << std::endl;
    }
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    while (!gStop) {
        const int64_t now = NowMs();
        if (exitAfter > 0 && now - start > exitAfter) break;
        server.Poll(20, now);
        game.Tick(NowMs());
        server.Poll(0, NowMs());
    }
    server.Stop();
    if (!quiet) std::cout << "stopped" << std::endl;
    return 0;
}
