// cricket_controller_host: plays a match on this PC/TV, controlled by phones.
//
// Until the Unreal game exists, this program stands in for the "TV side":
// it runs the rules engine, the local server and the phone pairing, prints a
// QR code in the terminal, and serves a TV page at http://localhost:<port>/tv
// (open it on the TV/PC screen - it is never shown to other devices).
// Ball outcomes are simulated (placeholder) until ball physics exists.
//
// Usage: cricket_controller_host [--port N] [--bind ADDR] [--host-ip IP]
//          [--format t20|odi|test|overs=N] [--humans 1|2] [--seed N]
//          [--web DIR] [--devices FILE] [--print-links] [--quiet] [--exit-after-ms N]
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "cricket/TeamFactory.h"
#include "cricket/controller/LocalServer.h"
#include "cricket/controller/MatchHost.h"
#include "cricket/controller/QrCode.h"

using namespace cricket;
using namespace cricket::ctl;

namespace {

volatile std::sig_atomic_t gStop = 0;
void OnSignal(int) { gStop = 1; }

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string Html(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

struct Slot {
    int side;
    Role role;
    std::string label;
    PairingOffer offer;
};

}  // namespace

int main(int argc, char** argv) {
    int port = 47321, humans = 1;
    std::string bind = "0.0.0.0", hostIp, web = std::string(CRICKET_SOURCE_DIR) + "/controller/web", devicesFile;
    std::string format = "t20";
    uint64_t seed = static_cast<uint64_t>(NowMs());
    bool printLinks = false, quiet = false;
    int64_t exitAfter = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--port") port = std::atoi(next().c_str());
        else if (a == "--bind") bind = next();
        else if (a == "--host-ip") hostIp = next();
        else if (a == "--format") format = next();
        else if (a == "--humans") humans = std::atoi(next().c_str());
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--web") web = next();
        else if (a == "--devices") devicesFile = next();
        else if (a == "--print-links") printLinks = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--exit-after-ms") exitAfter = std::atoll(next().c_str());
        else {
            std::cerr << "unknown option " << a << "\n";
            return 2;
        }
    }
    MatchRules rules = MatchRules::T20();
    if (format == "odi") rules = MatchRules::ODI();
    else if (format == "test") rules = MatchRules::Test();
    else if (format.rfind("overs=", 0) == 0) rules = MatchRules::Custom(std::atoi(format.c_str() + 6));
    auto all = FictionalTeams();

    ControllerHub hub;
    if (!devicesFile.empty()) {
        std::ifstream f(devicesFile);
        std::stringstream ss;
        ss << f.rdbuf();
        if (!ss.str().empty() && !hub.ImportDevices(ss.str())) std::cerr << "ignoring damaged device file\n";
    }
    HostConfig cfg;
    cfg.human = {true, humans >= 2};
    cfg.seed = seed;
    MatchHost host(rules, {all[0], all[1]}, hub, cfg);

    ServerConfig sc;
    sc.port = port;
    sc.bindAddress = bind;
    sc.webRoot = web;
    LocalServer server(hub, sc);
    if (Status s = server.Start(); !s) {
        std::cerr << "Could not start: " << s.error << "\n";
        return 1;
    }
    if (hostIp.empty()) {
        auto addrs = LocalServer::LocalAddresses();
        hostIp = addrs.empty() ? "127.0.0.1" : addrs[0];
    }

    std::vector<Slot> slots = {{0, Role::Captain, all[0].name + " captain", {}}};
    if (humans >= 2) slots.push_back({1, Role::Captain, all[1].name + " captain", {}});
    slots.push_back({-1, Role::Spectator, "Spectator", {}});
    int version = 0;

    auto refresh = [&](int64_t now) {
        bool changed = false;
        for (Slot& s : slots) {
            if (!s.offer.pairId.empty() && hub.IsPending(s.offer.pairId)) continue;
            s.offer = hub.CreatePairing(s.side, s.role, now);
            changed = true;
            const std::string link = s.offer.Link(hostIp, server.Port());
            if (printLinks) {
                Json j = Json::Object();
                j.Set("slot", s.label).Set("side", s.side).Set("role", ToString(s.role)).Set("link", link);
                std::cout << "LINK " << j.Dump() << std::endl;
            }
            if (!quiet && s.side == 0) {
                if (auto q = QrCode::Encode(link)) std::cout << "\n" << q->ToTerminal();
                std::cout << "Scan with the phone for " << s.label << " (valid 2 minutes):\n" << link << "\n";
                std::cout << "TV page with all codes: http://localhost:" << server.Port() << "/tv\n\n" << std::flush;
            }
        }
        if (changed) ++version;
    };

    server.SetTvPage(
        [&] {
            std::string h = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Cricket Unleashed - Controllers</title>"
                            "<link rel=\"stylesheet\" href=\"tv.css\"></head><body><h1>Connect your phone</h1>"
                            "<p class=\"muted\">Same Wi-Fi as this screen. Open the phone camera, scan a code, tap the link. "
                            "Each code works once, for 2 minutes.</p><div class=\"codes\">";
            for (const Slot& s : slots) {
                const std::string link = s.offer.Link(hostIp, server.Port());
                auto q = QrCode::Encode(link);
                h += "<div class=\"code\"><h2>" + Html(s.label) + "</h2>" + (q ? q->ToSvg(6) : std::string()) +
                     "<div class=\"link\">" + Html(link) + "</div></div>";
            }
            h += "</div><div id=\"score\"></div><div id=\"msg\"></div><h2>Phones</h2><ul class=\"devices\">";
            for (const auto& d : hub.Devices())
                h += "<li>" + Html(d.name) + " - " + (d.side >= 0 ? Html(all[d.side].name) : std::string("spectator")) + " " +
                     ToString(d.role) + (d.connected ? " (connected)" : " (offline)") + "</li>";
            h += "</ul><p class=\"muted\">Ball outcomes are simulated until the 3D game's ball physics is ready.</p>"
                 "<script src=\"tv.js\"></script></body></html>";
            return h;
        },
        [&] {
            Json j = Json::Object();
            j.Set("version", version);
            const auto& s = host.Session();
            if (s.HasMatch()) {
                const Innings& inn = s.GetMatch().Current();
                j.Set("score", all[inn.BattingSide()].name + " " + inn.ScoreString());
                if (s.GetMatch().Phase() == MatchPhase::Complete) j.Set("message", s.GetMatch().Result().summary);
            }
            if (s.LastEventId() > 0) {
                auto ev = s.EventsSince(s.LastEventId() - 1);
                if (!ev.empty() && !j.Get("message")) j.Set("message", ev[0].text);
            }
            return j.Dump();
        });

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    if (!quiet)
        std::cout << "Cricket Unleashed controller host on port " << server.Port() << " (" << hostIp
                  << "). Press Ctrl+C to stop.\n";
    const int64_t start = NowMs();
    uint64_t seen = 0;
    while (!gStop) {
        const int64_t now = NowMs();
        if (exitAfter >= 0 && now - start > exitAfter) break;
        hub.Tick(now);
        refresh(now);
        server.Poll(20, now);
        host.Process(now);
        bool devicesChanged = false;
        for (const auto& e : host.RecentHubEvents()) {
            devicesChanged |= e.kind == HubEvent::Kind::Paired || e.kind == HubEvent::Kind::Revoked;
            if (!quiet && e.kind != HubEvent::Kind::Security)
                std::cout << "[phone] " << (e.kind == HubEvent::Kind::Paired ? "paired " : e.kind == HubEvent::Kind::Connected ? "connected " : e.kind == HubEvent::Kind::Disconnected ? "disconnected " : e.kind == HubEvent::Kind::Lost ? "lost " : "") << e.device << " " << e.detail << "\n";
            if (e.kind == HubEvent::Kind::Paired) ++version;
        }
        if (devicesChanged && !devicesFile.empty()) std::ofstream(devicesFile) << hub.ExportDevices();
        for (const auto& e : host.Session().EventsSince(seen))
            if (!quiet && e.kind != "join") std::cout << (e.kind == "ball" ? "  " : "** ") << e.text << "\n";
        seen = host.Session().LastEventId();
        std::cout << std::flush;
    }
    server.Stop();
    return 0;
}
