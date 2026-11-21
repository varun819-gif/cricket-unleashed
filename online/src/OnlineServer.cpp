#include "cricket/online/OnlineServer.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cricket/TeamFactory.h"
#include "cricket/online/Moderation.h"

namespace cricket::online {

namespace fs = std::filesystem;

namespace {

constexpr char kCodeAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // no 0/O, 1/I

Json Obj() { return Json::Object(); }

const char* OutcomeName(MatchResult::Kind k) {
    switch (k) {
        case MatchResult::Kind::Win: return "win";
        case MatchResult::Kind::Tie: return "tie";
        default: return "no-result";
    }
}

// The scorebook figures the server records (from its own match state).
std::array<SideResult, 2> Summarise(const Match& m) {
    std::array<SideResult, 2> r;
    std::array<bool, 2> bowled{false, false};
    for (const Innings& inn : m.AllInnings()) {
        const int bat = inn.BattingSide(), bowl = 1 - bat;
        SideResult& b = r[bat];
        b.total += inn.Total();
        b.wickets += inn.Wickets();
        b.legalBalls += inn.LegalBalls();
        for (size_t i = 0; i < m.GetTeam(bat).xi.size(); ++i) {
            const BatterCard& c = inn.Batter(static_cast<int>(i));
            b.fours += c.fours;
            b.sixes += c.sixes;
            b.bestIndividual = std::max(b.bestIndividual, c.runs);
        }
        SideResult& f = r[bowl];
        for (size_t i = 0; i < m.GetTeam(bowl).xi.size(); ++i) {
            const BowlerCard& c = inn.Bowler(static_cast<int>(i));
            f.wicketsTaken += c.wickets;
            if (c.legalBalls == 0) continue;
            // Best figures: most wickets, then fewest runs.
            if (!bowled[bowl] || c.wickets > f.bestBowlingWickets ||
                (c.wickets == f.bestBowlingWickets && c.runs < f.bestBowlingRuns)) {
                f.bestBowlingWickets = c.wickets;
                f.bestBowlingRuns = c.runs;
                bowled[bowl] = true;
            }
        }
    }
    return r;
}

}  // namespace

// ============================================================ OnlineMatch

std::vector<ctl::DeviceInfo> OnlineMatch::Devices() const {
    std::vector<ctl::DeviceInfo> out;
    for (int s = 0; s < 2; ++s) {
        if (players[s].empty()) continue;  // the computer's side (single player)
        ctl::DeviceInfo d;
        d.id = players[s];
        d.side = s;
        d.role = ctl::Role::Captain;
        d.connected = server_->Connected(players[s]);
        d.lost = goneSince[s] >= 0 && server_->now_ - goneSince[s] >= server_->cfg_.graceMs;
        d.lastActionId = lastActionId[s];
        out.push_back(d);
    }
    return out;
}

std::vector<ctl::ControllerAction> OnlineMatch::TakeActions() { return std::exchange(actions_, {}); }
std::vector<ctl::HubEvent> OnlineMatch::TakeEvents() { return std::exchange(events_, {}); }

void OnlineMatch::Ack(const std::string& device, int64_t actionId, bool ok, const std::string& error) {
    Json a = Obj();
    a.Set("k", "ack").Set("id", actionId).Set("ok", ok);
    if (!ok) a.Set("err", error);
    SendTo(device, a);
}

bool OnlineMatch::SendTo(const std::string& device, const Json& body) {
    if (device != players[0] && device != players[1]) return false;
    if (!server_->Connected(device)) return false;
    server_->SendToAccount(device, body);
    return true;
}

void OnlineMatch::Broadcast(int side, const Json& body) {
    for (int s = 0; s < 2; ++s)
        if (side == -2 || side == s) SendTo(players[s], body);
}

int64_t OnlineMatch::RttMs(const std::string& device) const { return server_->RttOf(device); }

// ============================================================ OnlineServer

OnlineServer::OnlineServer(Store& store, OnlineConfig cfg, ctl::RandomSource rnd)
    : store_(store), cfg_(std::move(cfg)), rnd_(std::move(rnd)) {
    cfg_.host.human = {true, true};
    cfg_.host.serverTimingCheck = true;
}

std::optional<MatchRules> OnlineServer::RulesFor(const std::string& f) {
    if (f == "t20") return MatchRules::T20();
    if (f == "odi") return MatchRules::ODI();
    if (f.rfind("overs=", 0) == 0 && f.size() <= 8) {
        const std::string n = f.substr(6);
        if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
        const int overs = std::stoi(n);
        if (overs >= 1 && overs <= 50 && std::to_string(overs) == n) return MatchRules::Custom(overs);
    }
    return std::nullopt;
}

std::vector<std::string> OnlineServer::TeamNames() {
    std::vector<std::string> out;
    for (const auto& t : NationalTeams()) out.push_back(t.name);
    return out;
}

size_t OnlineServer::Queued() const { return queue_.size(); }

bool OnlineServer::Connected(const std::string& account) const {
    auto it = presence_.find(account);
    return it != presence_.end() && it->second.conn != 0;
}

int64_t OnlineServer::RttOf(const std::string& account) const {
    auto it = presence_.find(account);
    if (it == presence_.end() || !it->second.conn) return -1;
    auto c = conns_.find(it->second.conn);
    if (c == conns_.end() || c->second.rtt.empty()) return -1;
    // The smallest recent sample: a client can't make it smaller than its
    // real network delay, and MatchHost caps the allowance at 1.5 s.
    return *std::min_element(c->second.rtt.begin(), c->second.rtt.end());
}

const OnlineMatch* OnlineServer::MatchOf(const std::string& account) const {
    auto it = presence_.find(account);
    if (it == presence_.end() || it->second.match.empty()) return nullptr;
    auto m = matches_.find(it->second.match);
    return m == matches_.end() ? nullptr : m->second.get();
}

OnlineMatch* OnlineServer::MatchById(const std::string& id) {
    auto m = matches_.find(id);
    return m == matches_.end() ? nullptr : m->second.get();
}

Json OnlineServer::Health(int64_t now) const {
    Json h = Obj();
    h.Set("ok", true).Set("region", cfg_.region).Set("time", now);
    h.Set("connections", static_cast<int64_t>(conns_.size())).Set("matches", static_cast<int64_t>(matches_.size()));
    h.Set("rooms", static_cast<int64_t>(rooms_.size())).Set("queued", static_cast<int64_t>(queue_.size()));
    h.Set("accounts", static_cast<int64_t>(store_.AccountCount()));
    return h;
}

// ---------------------------------------------------------------- transport

std::optional<ConnId> OnlineServer::Open(int64_t now) {
    if (conns_.size() >= cfg_.maxConnections) return std::nullopt;
    const ConnId id = nextConn_++;
    conns_[id].openedAt = now;
    conns_[id].windowStart = now;
    return id;
}

void OnlineServer::Closed(ConnId c, int64_t now) {
    auto it = conns_.find(c);
    if (it == conns_.end()) return;
    const std::string account = it->second.account;
    conns_.erase(it);
    if (account.empty()) return;
    auto p = presence_.find(account);
    if (p == presence_.end() || p->second.conn != c) return;  // an older, replaced connection
    p->second.conn = 0;
    Dequeue(account);
    if (OnlineMatch* m = MatchById(p->second.match)) {
        const int side = m->players[0] == account ? 0 : 1;
        m->goneSince[side] = now;
        m->events_.push_back({ctl::HubEvent::Kind::Disconnected, account, ""});
        Json n = Obj();
        n.Set("k", "notice").Set("text", "Opponent disconnected - waiting for them to come back");
        n.Set("graceMs", cfg_.graceMs);
        m->SendTo(m->players[1 - side], n);
    }
}

std::vector<Outgoing> OnlineServer::TakeOutbox() { return std::exchange(outbox_, {}); }

void OnlineServer::Send(ConnId c, const Json& body, bool close) {
    if (!conns_.count(c)) return;
    outbox_.push_back({c, body.Dump(), close});
    if (close) conns_[c].closing = true;
}

void OnlineServer::SendToAccount(const std::string& account, const Json& body) {
    auto p = presence_.find(account);
    if (p != presence_.end() && p->second.conn) Send(p->second.conn, body);
}

void OnlineServer::Reply(ConnId c, int64_t seq, bool ok, const std::string& err, Json extra) {
    extra.Set("k", "res").Set("re", seq).Set("ok", ok);
    if (!ok) extra.Set("err", err);
    Send(c, extra);
}

void OnlineServer::Fail(ConnId c, const std::string& why) {
    Send(c, Obj().Set("k", "error").Set("why", why), true);
}

void OnlineServer::Receive(ConnId c, const std::string& text, int64_t now) {
    now_ = std::max(now_, now);
    auto it = conns_.find(c);
    if (it == conns_.end() || it->second.closing) return;
    Conn& k = it->second;
    if (text.size() > cfg_.maxMessageBytes) return Fail(c, "message too large");
    if (now - k.windowStart >= 1000) {
        k.windowStart = now;
        k.windowCount = 0;
    }
    if (++k.windowCount > cfg_.maxMessagesPerSecond) return Fail(c, "too many messages");
    auto j = Json::Parse(text);
    if (!j || !j->IsObject()) return Fail(c, "malformed message");
    // Duplicate-command protection: each message must be the next number.
    const int64_t seq = j->Int("s", -1);
    if (seq != k.inSeq + 1) return Fail(c, "duplicate or out-of-order message");
    k.inSeq = seq;
    OnRequest(c, k, *j, now);
}

// ---------------------------------------------------------------- requests

void OnlineServer::OnRequest(ConnId c, Conn& k, const Json& j, int64_t now) {
    const std::string kind = j.Str("k");
    const int64_t seq = j.Int("s");
    if (kind == "ping") {
        Send(c, Obj().Set("k", "pong").Set("t", j.Num("t")).Set("st", static_cast<double>(now)));
        return;
    }
    if (kind == "spong") {
        // Only the latest server ping counts (a client can't invent a faster one).
        if (j.Int("t", -1) == k.lastPingAt && k.lastPingAt > 0) {
            k.rtt.push_back(now - k.lastPingAt);
            while (k.rtt.size() > 5) k.rtt.pop_front();
            k.lastPingAt = -k.lastPingAt;  // answered
        }
        return;
    }
    if (k.account.empty()) {
        if (kind == "guest") {
            while (!guestTimes_.empty() && now - guestTimes_.front() > 60000) guestTimes_.pop_front();
            if (static_cast<int>(guestTimes_.size()) >= cfg_.maxGuestsPerMinute)
                return Reply(c, seq, false, "server busy - try again in a minute");
            if (std::string why = NameProblem(j.Str("name")); !why.empty()) return Reply(c, seq, false, why);
            guestTimes_.push_back(now);
            auto [acc, token] = store_.CreateGuest(j.Str("name"), j.Str("country"), now, rnd_);
            return OnLogin(c, k, acc, seq, now, token);
        }
        if (kind == "login") {
            const Account* a = store_.Verify(j.Str("id"), j.Str("token"));
            if (!a) {
                Reply(c, seq, false, "login failed - unknown player or wrong token");
                Send(c, Obj().Set("k", "error").Set("why", "login failed"), true);
                return;
            }
            if (a->BannedAt(now)) {
                Reply(c, seq, false, a->banUntilMs == kPermanentBan ? "this player is permanently banned"
                                                                     : "this player is banned for now",
                      BanJson(*a));
                Send(c, Obj().Set("k", "error").Set("why", "banned"), true);
                return;
            }
            return OnLogin(c, k, *a, seq, now, "");
        }
        if (kind == "admin-login") {
            while (!adminFailures_.empty() && now - adminFailures_.front() > 600000) adminFailures_.pop_front();
            // 256-bit token, compared in constant time; 5 wrong tries lock admin out for 10 minutes.
            if (cfg_.adminToken.size() < 32 || adminFailures_.size() >= 5 ||
                !ctl::ConstantTimeEqual(j.Str("token"), cfg_.adminToken)) {
                adminFailures_.push_back(now);
                Reply(c, seq, false, "admin login failed");
                Send(c, Obj().Set("k", "error").Set("why", "admin login failed"), true);
                return;
            }
            k.admin = true;
            k.account = "@admin";
            return Reply(c, seq, true, "", Obj().Set("admin", true));
        }
        return Fail(c, "log in first");
    }
    if (k.admin) return OnAdmin(c, j, now);
    const std::string& me = k.account;
    if (kind == "action") return OnAction(c, k, j, now);
    if (kind == "report") return OnReport(c, k, j, now);
    if (kind == "teams") {
        Json t = Json::Array();
        for (const auto& n : TeamNames()) t.Push(Json::String(n));
        Json f = Json::Array();
        for (const char* x : {"overs=2", "overs=5", "overs=10", "t20", "odi"}) f.Push(Json::String(x));
        Json flags = Obj();  // team -> flag picture (/flags/<code>.svg)
        for (const auto& n : NationalTeamList()) flags.Set(n.name, n.flag);
        return Reply(c, seq, true, "", Obj().Set("teams", t).Set("formats", f).Set("flags", flags));
    }
    if (kind == "profile") {
        if (std::string why = NameProblem(j.Str("name")); !why.empty()) return Reply(c, seq, false, why);
        Status s = store_.UpdateProfile(me, j.Str("name"), j.Str("country"));
        return Reply(c, seq, s.ok, s.error, Obj().Set("stats", store_.StatsJson(me)));
    }
    if (kind == "stats") {
        const std::string who = j.Str("id", me);
        if (!store_.Find(who)) return Reply(c, seq, false, "no such player");
        return Reply(c, seq, true, "", Obj().Set("stats", store_.StatsJson(who)));
    }
    if (kind == "history") {
        const int64_t limit = std::clamp<int64_t>(j.Int("limit", 20), 1, 50);
        Json arr = Json::Array();
        for (const auto& m : store_.History(me, static_cast<size_t>(limit))) arr.Push(Store::MatchJson(m));
        return Reply(c, seq, true, "", Obj().Set("matches", arr));
    }
    if (kind == "leaderboard") {
        static const std::set<std::string> boards = {"rating", "wins", "winpct", "runs", "wickets",
                                                     "sixes", "fours", "highscore", "matches"};
        const std::string board = j.Str("board", "rating"), period = j.Str("period", "all"),
                          mode = j.Str("mode", "all"), country = j.Str("country");
        if (!boards.count(board)) return Reply(c, seq, false, "unknown leaderboard");
        if (period != "all" && period != "week" && period != "month") return Reply(c, seq, false, "bad period");
        if (mode != "all" && mode != "ranked" && mode != "casual") return Reply(c, seq, false, "bad mode");
        if (!country.empty() && country.size() != 2) return Reply(c, seq, false, "bad country");
        const int64_t limit = std::clamp<int64_t>(j.Int("limit", 50), 1, 100);
        // Cached briefly: leaderboards scan the whole history.
        const std::string key = board + "|" + period + "|" + mode + "|" + country + "|" + std::to_string(limit);
        auto hit = boardCache_.find(key);
        if (hit == boardCache_.end() || now - hit->second.first > cfg_.leaderboardCacheMs) {
            if (boardCache_.size() > 500) boardCache_.clear();
            boardCache_[key] = {now, store_.Leaderboard(board, period, mode, country, now, static_cast<size_t>(limit))};
            hit = boardCache_.find(key);
        }
        return Reply(c, seq, true, "", Obj().Set("board", hit->second.second));
    }
    if (kind == "room-create") return RoomCreate(c, k, j, now);
    if (kind == "room-join") return RoomJoin(c, k, j, now);
    if (kind == "room-start") return RoomStart(c, k, j, now);
    if (kind == "room-leave") {
        RoomLeave(me, true);
        return Reply(c, seq, true, "");
    }
    if (kind == "queue") return Enqueue(c, k, j, now);
    if (kind == "solo") {
        // Single player: you against the computer captain (same match engine,
        // no opponent, no matchmaking). Not counted in ratings, stats or leaderboards.
        if (!presence_[me].match.empty()) return Reply(c, seq, false, "finish your match first");
        const std::string format = j.Str("format", "overs=2"), team = j.Str("team");
        std::string opp = j.Str("opponent");
        const auto teams = TeamNames();
        if (std::find(teams.begin(), teams.end(), team) == teams.end()) return Reply(c, seq, false, "unknown team");
        if (opp.empty()) opp = team == "Pakistan" ? "India" : "Pakistan";
        if (std::find(teams.begin(), teams.end(), opp) == teams.end()) return Reply(c, seq, false, "unknown opponent");
        if (opp == team) return Reply(c, seq, false, "pick a different team for the computer");
        const std::string pitch = j.Str("pitch", "random");
        static const std::set<std::string> pitches = {"random", "standard", "green", "dusty", "bouncy", "flat"};
        if (!pitches.count(pitch)) return Reply(c, seq, false, "unknown pitch");
        Status st = StartMatch("solo", format, {me, ""}, {team, opp}, now, pitch);
        return Reply(c, seq, st.ok, st.error);
    }
    if (kind == "queue-cancel") {
        Dequeue(me);
        return Reply(c, seq, true, "");
    }
    if (kind == "concede") {
        OnlineMatch* m = MatchById(presence_[me].match);
        if (!m) return Reply(c, seq, false, "not in a match");
        const int side = m->players[0] == me ? 0 : 1;
        Reply(c, seq, true, "");
        FinishMatch(*m, 1 - side, "forfeit", m->teamNames[side] + " conceded the match", now);
        return;
    }
    Reply(c, seq, false, "unknown request");
}

void OnlineServer::OnLogin(ConnId c, Conn& k, const Account& a, int64_t seq, int64_t now, const std::string& token) {
    k.account = a.id;
    Presence& p = presence_[a.id];
    if (p.conn && p.conn != c) {
        // Logged in again from somewhere else: the newest connection wins.
        Send(p.conn, Obj().Set("k", "replaced"), true);
        auto old = conns_.find(p.conn);
        if (old != conns_.end()) old->second.account.clear();
    }
    p.conn = c;
    Json r = Obj();
    r.Set("id", a.id).Set("name", a.name).Set("country", a.country).Set("rating", a.rating);
    r.Set("region", cfg_.region);
    if (!token.empty()) r.Set("token", token);  // new guest: the only time it is sent
    Reply(c, seq, true, "", r);
    if (auto room = rooms_.find(p.room); room != rooms_.end()) Send(c, RoomJson(room->second));
    if (OnlineMatch* m = MatchById(p.match)) AttachMatch(a.id, *m);
    k.lastPingAt = 0;
    (void)now;
}

void OnlineServer::AttachMatch(const std::string& account, OnlineMatch& m) {
    const int side = m.players[0] == account ? 0 : 1;
    m.goneSince[side] = -1;
    Json j = Obj();
    j.Set("k", "match").Set("id", m.id).Set("mode", m.mode).Set("format", m.format).Set("side", side);
    j.Set("role", "captain").Set("last", m.lastActionId[side]);
    j.Set("pitch", m.conditions.pitchName).Set("dayNight", m.conditions.dayNight);
    Json teams = Json::Array();
    for (int s = 0; s < 2; ++s) {
        const Account* a = store_.Find(m.players[s]);
        Json t = Obj();
        t.Set("team", m.teamNames[s]).Set("player", a ? a->name : m.players[s].empty() ? "Computer" : "?");
        t.Set("rating", a ? a->rating : 0);
        t.Set("ai", m.aiTakeover[s]);
        teams.Push(t);
    }
    j.Set("sides", teams);
    SendToAccount(account, j);
    m.events_.push_back({ctl::HubEvent::Kind::Connected, account, ""});  // resend the full state
    if (m.players[1 - side].empty()) return;
    Json n = Obj();
    n.Set("k", "notice").Set("text", "Opponent connected");
    m.SendTo(m.players[1 - side], n);
}

void OnlineServer::OnAction(ConnId c, Conn& k, const Json& j, int64_t now) {
    OnlineMatch* m = MatchById(presence_[k.account].match);
    const int64_t id = j.Int("id", -1);
    const Json* a = j.Get("a");
    auto ack = [&](bool ok, const std::string& err, bool dup) {
        Json r = Obj();
        r.Set("k", "ack").Set("id", id).Set("ok", ok);
        if (!ok) r.Set("err", err);
        if (dup) r.Set("dup", true);
        Send(c, r);
    };
    if (!m) return ack(false, "not in a match", false);
    if (id <= 0 || !a || !a->IsObject()) return ack(false, "malformed action", false);
    const int side = m->players[0] == k.account ? 0 : 1;
    if (m->aiTakeover[side]) return ack(false, "the AI is playing this side now", false);
    if (id <= m->lastActionId[side]) return ack(true, "", true);  // re-sent after a reconnect: already handled
    m->lastActionId[side] = id;
    if (m->actsLog) {
        // Remembered on disk, so after a server crash a re-sent control is
        // still recognised as already handled (never applied twice).
        std::fprintf(m->actsLog, "%d %lld\n", side, static_cast<long long>(id));
        std::fflush(m->actsLog);
    }
    ctl::ControllerAction act;
    act.device = k.account;
    act.side = side;
    act.role = ctl::Role::Captain;
    act.id = id;
    act.action = *a;
    act.receivedMs = now + m->clockOffset;
    m->actions_.push_back(std::move(act));
}

// ---------------------------------------------------------------- rooms

std::string OnlineServer::NewRoomCode() {
    for (;;) {
        const std::string b = rnd_(6);
        std::string code;
        for (unsigned char ch : b) code += kCodeAlphabet[ch % 32];  // 256 % 32 == 0: uniform
        if (!rooms_.count(code)) return code;
    }
}

Json OnlineServer::RoomJson(const Room& r) const {
    Json j = Obj();
    j.Set("k", "room").Set("code", r.code).Set("format", r.format).Set("mode", "casual");
    auto who = [&](const std::string& id, const std::string& team) {
        Json p = Obj();
        const Account* a = store_.Find(id);
        p.Set("id", id).Set("name", a ? a->name : "").Set("team", team);
        return p;
    };
    j.Set("host", who(r.host, r.hostTeam));
    if (!r.guest.empty()) j.Set("guest", who(r.guest, r.guestTeam));
    return j;
}

void OnlineServer::RoomCreate(ConnId c, Conn& k, const Json& j, int64_t now) {
    const int64_t seq = j.Int("s");
    Presence& p = presence_[k.account];
    if (!p.match.empty()) return Reply(c, seq, false, "finish your match first");
    if (rooms_.size() >= cfg_.maxRooms) return Reply(c, seq, false, "server busy - try again later");
    const std::string format = j.Str("format", "overs=5"), team = j.Str("team");
    if (!RulesFor(format)) return Reply(c, seq, false, "unknown format");
    const auto teams = TeamNames();
    if (std::find(teams.begin(), teams.end(), team) == teams.end()) return Reply(c, seq, false, "unknown team");
    RoomLeave(k.account, true);
    Dequeue(k.account);
    Room r;
    r.code = NewRoomCode();
    r.host = k.account;
    r.hostTeam = team;
    r.format = format;
    r.createdMs = r.touchedMs = now;
    rooms_[r.code] = r;
    p.room = r.code;
    Reply(c, seq, true, "", Obj().Set("code", r.code));
    Send(c, RoomJson(r));
}

void OnlineServer::RoomJoin(ConnId c, Conn& k, const Json& j, int64_t now) {
    const int64_t seq = j.Int("s");
    Presence& p = presence_[k.account];
    if (!p.match.empty()) return Reply(c, seq, false, "finish your match first");
    std::string code;
    for (char ch : j.Str("code"))
        if (ch != ' ' && ch != '-') code += static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    while (!p.wrongCodes.empty() && now - p.wrongCodes.front() > 60000) p.wrongCodes.pop_front();
    if (static_cast<int>(p.wrongCodes.size()) >= cfg_.maxWrongRoomCodesPerMinute)
        return Reply(c, seq, false, "too many wrong codes - wait a minute");
    auto it = rooms_.find(code);
    if (it == rooms_.end()) {
        p.wrongCodes.push_back(now);
        return Reply(c, seq, false, "no room with that code");
    }
    Room& r = it->second;
    if (r.host == k.account) return Reply(c, seq, false, "that is your own room");
    if (!r.guest.empty() && r.guest != k.account) return Reply(c, seq, false, "room is full");
    const std::string team = j.Str("team");
    const auto teams = TeamNames();
    if (std::find(teams.begin(), teams.end(), team) == teams.end()) return Reply(c, seq, false, "unknown team");
    if (team == r.hostTeam) return Reply(c, seq, false, "the host already picked " + team + " - choose another team");
    if (p.room != code) RoomLeave(k.account, true);
    Dequeue(k.account);
    r.guest = k.account;
    r.guestTeam = team;
    r.touchedMs = now;
    p.room = code;
    Reply(c, seq, true, "", Obj().Set("code", code));
    const Json rj = RoomJson(r);
    SendToAccount(r.host, rj);
    SendToAccount(r.guest, rj);
}

void OnlineServer::RoomLeave(const std::string& account, bool notify) {
    auto p = presence_.find(account);
    if (p == presence_.end() || p->second.room.empty()) return;
    auto it = rooms_.find(p->second.room);
    p->second.room.clear();
    if (it == rooms_.end()) return;
    Room& r = it->second;
    if (r.host == account) {
        // Host leaves: the room closes.
        if (!r.guest.empty()) {
            presence_[r.guest].room.clear();
            if (notify) SendToAccount(r.guest, Obj().Set("k", "room-closed").Set("code", r.code));
        }
        rooms_.erase(it);
    } else if (r.guest == account) {
        r.guest.clear();
        r.guestTeam.clear();
        if (notify) SendToAccount(r.host, RoomJson(r));
    }
}

void OnlineServer::RoomStart(ConnId c, Conn& k, const Json& j, int64_t now) {
    const int64_t seq = j.Int("s");
    auto it = rooms_.find(presence_[k.account].room);
    if (it == rooms_.end()) return Reply(c, seq, false, "not in a room");
    Room r = it->second;
    if (r.host != k.account) return Reply(c, seq, false, "only the host can start");
    if (r.guest.empty()) return Reply(c, seq, false, "waiting for a friend to join");
    if (!Connected(r.guest)) return Reply(c, seq, false, "your friend is offline");
    Status s = StartMatch("casual", r.format, {r.host, r.guest}, {r.hostTeam, r.guestTeam}, now);
    if (s.ok) {
        rooms_.erase(r.code);
        presence_[r.host].room.clear();
        presence_[r.guest].room.clear();
    }
    Reply(c, seq, s.ok, s.error);
}

// ---------------------------------------------------------------- matchmaking

void OnlineServer::Enqueue(ConnId c, Conn& k, const Json& j, int64_t now) {
    const int64_t seq = j.Int("s");
    Presence& p = presence_[k.account];
    if (!p.match.empty()) return Reply(c, seq, false, "finish your match first");
    QueueEntry e;
    e.account = k.account;
    e.mode = j.Str("mode", "casual");
    e.format = j.Str("format", "overs=5");
    e.team = j.Str("team");
    if (e.mode != "casual" && e.mode != "ranked") return Reply(c, seq, false, "mode must be casual or ranked");
    if (!RulesFor(e.format)) return Reply(c, seq, false, "unknown format");
    const auto teams = TeamNames();
    if (std::find(teams.begin(), teams.end(), e.team) == teams.end()) return Reply(c, seq, false, "unknown team");
    const Account* acc = store_.Find(k.account);
    if (e.mode == "ranked" && acc->stats.completed < cfg_.rankedUnlockMatches)
        return Reply(c, seq, false,
                     "play " + std::to_string(cfg_.rankedUnlockMatches - acc->stats.completed) +
                         " more full match(es) to unlock ranked");
    RoomLeave(k.account, true);
    Dequeue(k.account);
    e.rating = acc->rating;
    e.joinedMs = now;
    queue_.push_back(e);
    Reply(c, seq, true, "");
    Send(c, Obj().Set("k", "queued").Set("mode", e.mode).Set("format", e.format).Set("team", e.team));
}

void OnlineServer::Dequeue(const std::string& account) {
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                [&](const QueueEntry& e) { return e.account == account; }),
                 queue_.end());
}

void OnlineServer::Matchmake(int64_t now) {
    const auto teams = TeamNames();
    for (size_t i = 0; i < queue_.size(); ++i) {
        for (size_t j = i + 1; j < queue_.size(); ++j) {
            const QueueEntry& a = queue_[i];  // joined earlier (the queue is in arrival order)
            const QueueEntry& b = queue_[j];
            if (a.mode != b.mode || a.format != b.format) continue;
            if (a.mode == "ranked") {
                // The longer someone waits, the wider the rating band (100 .. 500).
                const int64_t waitSec = (now - a.joinedMs) / 1000;
                const int64_t band = std::min<int64_t>(500, 100 + 25 * waitSec);
                if (std::abs(a.rating - b.rating) > band) continue;
            }
            std::string teamB = b.team;
            if (teamB == a.team) {
                // Same team picked: the later player gets the next team in the list.
                auto at = std::find(teams.begin(), teams.end(), teamB);
                const size_t idx = static_cast<size_t>(at - teams.begin());
                teamB = teams[(idx + 1) % teams.size()];
            }
            const QueueEntry ea = a, eb = b;
            queue_.erase(queue_.begin() + static_cast<long>(j));
            queue_.erase(queue_.begin() + static_cast<long>(i));
            Status s = StartMatch(ea.mode, ea.format, {ea.account, eb.account}, {ea.team, teamB}, now);
            if (!s.ok) {
                for (const auto* e : {&ea, &eb}) SendToAccount(e->account, Obj().Set("k", "notice").Set("text", s.error));
            }
            --i;  // re-check from the same position
            break;
        }
    }
}

// ---------------------------------------------------------------- matches

std::string OnlineServer::NewMatchId(int64_t now) {
    return cfg_.region + "-" + std::to_string(now) + "-" + std::to_string(++matchCounter_) + "-" +
           ctl::ToHex(rnd_(3));
}

std::string OnlineServer::LiveDir() const { return cfg_.dataDir.empty() ? "" : cfg_.dataDir + "/live"; }

void OnlineServer::WriteMeta(const OnlineMatch& m) {
    if (LiveDir().empty()) return;
    Json j = Obj();
    j.Set("id", m.id).Set("mode", m.mode).Set("format", m.format).Set("start", m.startedMs);
    j.Set("p0", m.players[0]).Set("p1", m.players[1]).Set("t0", m.teamNames[0]).Set("t1", m.teamNames[1]);
    j.Set("ai0", m.aiTakeover[0]).Set("ai1", m.aiTakeover[1]);
    j.Set("seed", static_cast<double>(m.host->Session().Seed()));
    j.Set("pitch", m.conditions.pitchName).Set("cracks", ctl::ToHex(std::string(reinterpret_cast<const char*>(&m.conditions.pitch.cracks), 8)));
    j.Set("windX", m.conditions.wind.x).Set("windY", m.conditions.wind.y).Set("dayNight", m.conditions.dayNight);
    SaveFileAtomic(LiveDir() + "/" + m.id + ".meta", j.Dump());
}

Conditions OnlineServer::MakeConditions(const std::string& format, const std::string& pitchName) {
    auto u = [&] {
        uint64_t x = 0;
        for (unsigned char ch : rnd_(8)) x = (x << 8) | ch;
        return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
    };
    static const char* kPitches[] = {"standard", "standard", "green", "dusty", "bouncy", "flat"};
    Conditions c;
    std::string name = pitchName;
    const double pick = u();
    if (name.empty() || name == "random") name = kPitches[static_cast<int>(pick * 6) % 6];
    c.pitchName = name;
    c.pitch = phys::Pitch::ByName(name);
    uint64_t cracks = 0;
    for (unsigned char ch : rnd_(8)) cracks = (cracks << 8) | ch;
    c.pitch.cracks = cracks | 1;
    const double speed = u() * 5.0, angle = u() * 2 * phys::kPi;  // a breeze, 0-18 km/h
    c.wind = {speed * phys::DetCos(angle), speed * phys::DetSin(angle), 0};
    c.dayNight = format != "test" && u() < 0.4;
    return c;
}

Status OnlineServer::StartMatch(const std::string& mode, const std::string& format,
                                std::array<std::string, 2> accounts, std::array<std::string, 2> teamNames,
                                int64_t now, const std::string& pitchChoice) {
    if (matches_.size() >= cfg_.maxMatches) return Status::Err("server full - try again soon");
    auto rules = RulesFor(format);
    if (!rules) return Status::Err("unknown format");
    std::array<Team, 2> teams;
    const auto all = NationalTeams();
    for (int s = 0; s < 2; ++s) {
        auto it = std::find_if(all.begin(), all.end(), [&](const Team& t) { return t.name == teamNames[s]; });
        if (it == all.end()) return Status::Err("unknown team");
        teams[s] = *it;
    }
    auto m = std::make_unique<OnlineMatch>();
    m->server_ = this;
    m->id = NewMatchId(now);
    m->mode = mode;
    m->format = format;
    m->players = accounts;
    m->teamNames = teamNames;
    m->startedMs = now;
    m->lastProgressMs = now;
    ctl::HostConfig h = cfg_.host;
    for (int s = 0; s < 2; ++s) {
        // An empty account = the computer captain plays this side (single player).
        m->aiTakeover[s] = accounts[s].empty();
        h.human[s] = !accounts[s].empty();
    }
    uint64_t seed = 0;
    for (unsigned char ch : rnd_(8)) seed = (seed << 8) | ch;
    h.seed = seed | 1;
    m->conditions = MakeConditions(format, pitchChoice);
    h.conditions = m->conditions;
    m->host = std::make_unique<ctl::MatchHost>(std::move(*rules), std::move(teams), *m, h);
    if (!LiveDir().empty()) {
        std::error_code ec;
        fs::create_directories(LiveDir(), ec);
        m->journal.Start(LiveDir() + "/" + m->id + ".journal", m->host->Session());
        m->actsLog = std::fopen((LiveDir() + "/" + m->id + ".acts").c_str(), "w");
        WriteMeta(*m);
    }
    OnlineMatch& ref = *m;
    matches_[m->id] = std::move(m);
    for (int s = 0; s < 2; ++s) {
        if (accounts[s].empty()) continue;
        Dequeue(accounts[s]);
        RoomLeave(accounts[s], false);
        presence_[accounts[s]].match = ref.id;
        if (Connected(accounts[s])) AttachMatch(accounts[s], ref);
        else ref.goneSince[s] = now;
    }
    return Status::Ok();
}

void OnlineServer::AbandonSide(OnlineMatch& m, int side, const std::string& why, int64_t now) {
    if (m.goneSince[0] == -2 || m.aiTakeover[side]) return;
    const bool otherGone = m.aiTakeover[1 - side] || m.goneSince[1 - side] >= 0;
    if (otherGone) return FinishMatch(m, -1, "no-result", "Both players left - no result", now);
    if (m.mode == "ranked")
        return FinishMatch(m, 1 - side, "forfeit", m.teamNames[side] + " forfeited (" + why + ")", now);
    m.aiTakeover[side] = true;
    m.host->HandToAi(side);
    m.goneSince[side] = -1;
    WriteMeta(m);
    Json n = Obj();
    n.Set("k", "notice").Set("text", "Your opponent left (" + why + "). The computer captain takes over.");
    m.SendTo(m.players[1 - side], n);
}

void OnlineServer::RunMatch(OnlineMatch& m, int64_t now) {
    auto abandon = [&](int side, const std::string& why) { AbandonSide(m, side, why, now); };
    for (int s = 0; s < 2; ++s)
        if (!m.aiTakeover[s] && m.goneSince[s] >= 0 && now - m.goneSince[s] >= cfg_.graceMs) {
            abandon(s, "did not reconnect in time");
            if (m.goneSince[0] == -2) return;
        }
    if (m.goneSince[0] == -2) return;  // finished
    MatchSession& session = m.host->Session();
    if (session.LastEventId() != m.lastInputs) {  // something happened in the match
        m.lastInputs = session.LastEventId();
        m.lastProgressMs = now;
    }
    const Pending p = session.WaitingFor();
    if (p.side >= 0 && !m.aiTakeover[p.side] && m.goneSince[p.side] < 0 && p.what != "review" &&
        p.what != "delivery" && p.what != "none" && now - m.lastProgressMs > cfg_.idleLimitMs) {
        abandon(p.side, "no decision for too long");
        if (m.goneSince[0] == -2) return;
        m.lastProgressMs = now;
    }
    m.host->Process(now + m.clockOffset);
    if (m.journal.Written() > 0 || !LiveDir().empty()) m.journal.Sync(session);
    if (session.HasMatch() && session.GetMatch().Result().kind != MatchResult::Kind::None) {
        const MatchResult& r = session.GetMatch().Result();
        FinishMatch(m, r.kind == MatchResult::Kind::Win ? r.winner : -1, OutcomeName(r.kind), r.summary, now);
    }
}

void OnlineServer::FinishMatch(OnlineMatch& m, int winner, const std::string& outcome, const std::string& summary,
                               int64_t now) {
    if (m.goneSince[0] == -2) return;
    MatchRecord rec;
    rec.id = m.id;
    rec.mode = m.mode;
    rec.format = m.format;
    rec.startedMs = m.startedMs;
    rec.endedMs = now;
    const MatchSession& session = m.host->Session();
    if (session.HasMatch()) rec.sides = Summarise(session.GetMatch());
    for (int s = 0; s < 2; ++s) {
        rec.sides[s].account = m.players[s];
        rec.sides[s].team = m.teamNames[s];
    }
    rec.winner = winner;
    rec.outcome = outcome;
    rec.summary = summary;
    if (!cfg_.dataDir.empty()) {
        std::error_code ec;
        fs::create_directories(cfg_.dataDir + "/matches", ec);
        rec.saveFile = "matches/" + m.id + ".save";
        SaveSessionFile(session, cfg_.dataDir + "/" + rec.saveFile);
    }
    rec = store_.RecordMatch(rec);
    boardCache_.clear();
    for (int s = 0; s < 2; ++s) {
        if (m.players[s].empty()) continue;
        Json e = Obj();
        e.Set("k", "end").Set("record", Store::MatchJson(rec)).Set("side", s);
        const Account* a = store_.Find(m.players[s]);
        if (a) e.Set("rating", a->rating);
        e.Set("delta", rec.ratingDelta[s]);
        SendToAccount(m.players[s], e);
        auto p = presence_.find(m.players[s]);
        if (p != presence_.end() && p->second.match == m.id) p->second.match.clear();
    }
    if (!LiveDir().empty()) {
        std::error_code ec;
        fs::remove(LiveDir() + "/" + m.id + ".meta", ec);
        fs::remove(LiveDir() + "/" + m.id + ".meta.bak", ec);
        fs::remove(LiveDir() + "/" + m.id + ".journal", ec);
        if (m.actsLog) {
            std::fclose(m.actsLog);
            m.actsLog = nullptr;
        }
        fs::remove(LiveDir() + "/" + m.id + ".acts", ec);
    }
    m.goneSince = {-2, -2};  // marks the match finished; Tick removes it
}

size_t OnlineServer::RecoverLiveMatches(int64_t now) {
    if (LiveDir().empty() || !fs::exists(LiveDir())) return 0;
    size_t recovered = 0;
    for (const auto& entry : fs::directory_iterator(LiveDir())) {
        if (entry.path().extension() != ".meta") continue;
        std::ifstream f(entry.path());
        std::stringstream ss;
        ss << f.rdbuf();
        auto meta = Json::Parse(ss.str());
        if (!meta) continue;
        const std::string id = meta->Str("id");
        Recovery info;
        SessionLoad load = RecoverSession(LiveDir() + "/" + id + ".journal", &info);
        if (!load.status.ok || !load.session) continue;
        auto m = std::make_unique<OnlineMatch>();
        m->server_ = this;
        m->id = id;
        m->mode = meta->Str("mode");
        m->format = meta->Str("format");
        m->startedMs = meta->Int("start");
        m->players = {meta->Str("p0"), meta->Str("p1")};
        m->teamNames = {meta->Str("t0"), meta->Str("t1")};
        m->aiTakeover = {meta->Flag("ai0"), meta->Flag("ai1")};
        m->lastProgressMs = now;
        ctl::HostConfig h = cfg_.host;
        h.seed = load.session->Seed();
        h.human = {!m->aiTakeover[0], !m->aiTakeover[1]};
        // The same venue as before the restart.
        m->conditions.pitchName = meta->Str("pitch", "standard");
        m->conditions.pitch = phys::Pitch::ByName(m->conditions.pitchName);
        {
            const std::string hex = meta->Str("cracks");
            uint64_t cracks = 1;
            if (hex.size() == 16) {
                std::string raw;
                for (size_t i = 0; i < 16; i += 2) raw += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
                std::memcpy(&cracks, raw.data(), 8);
            }
            m->conditions.pitch.cracks = cracks;
        }
        m->conditions.wind = {meta->Num("windX"), meta->Num("windY"), 0};
        m->conditions.dayNight = meta->Flag("dayNight");
        h.conditions = m->conditions;
        uint64_t salt = 0;
        for (unsigned char ch : rnd_(8)) salt = (salt << 8) | ch;
        m->host = std::make_unique<ctl::MatchHost>(std::move(load.session), *m, h, salt);
        m->lastInputs = m->host->Session().LastEventId();
        m->clockOffset = std::max<int64_t>(0, m->host->Session().NowMs() - now);
        m->journal.Start(LiveDir() + "/" + id + ".journal", m->host->Session());
        {
            std::ifstream acts(LiveDir() + "/" + id + ".acts");
            int side = 0;
            long long aid = 0;
            while (acts >> side >> aid)
                if (side == 0 || side == 1) m->lastActionId[side] = std::max<int64_t>(m->lastActionId[side], aid);
        }
        m->actsLog = std::fopen((LiveDir() + "/" + id + ".acts").c_str(), "a");
        for (int s = 0; s < 2; ++s) {
            if (m->players[s].empty()) continue;  // computer side
            presence_[m->players[s]].match = id;
            m->goneSince[s] = m->aiTakeover[s] ? -1 : now;  // both must reconnect within the grace period
        }
        matches_[id] = std::move(m);
        ++recovered;
    }
    return recovered;
}

// ---------------------------------------------------------------- timers

void OnlineServer::Tick(int64_t now) {
    now_ = std::max(now_, now);
    for (auto& [id, k] : conns_) {
        if (k.closing) continue;
        if (k.account.empty() && now - k.openedAt > cfg_.loginTimeoutMs) {
            Fail(id, "log in timed out");
            continue;
        }
        if (!k.account.empty() && now - std::abs(k.lastPingAt) >= cfg_.serverPingMs) {
            k.lastPingAt = now;
            Send(id, Obj().Set("k", "sping").Set("t", now));
        }
    }
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        if (now - it->second.touchedMs > cfg_.roomTtlMs) {
            const Room r = it->second;
            ++it;
            RoomLeave(r.host, true);
        } else {
            ++it;
        }
    }
    Matchmake(now);
    std::vector<std::string> done;
    for (auto& [id, m] : matches_) {
        RunMatch(*m, now);
        if (m->goneSince[0] == -2) done.push_back(id);
    }
    for (const auto& id : done) matches_.erase(id);
}

// ---------------------------------------------------------------- moderation

Json OnlineServer::BanJson(const Account& a) const {
    Json j = Obj();
    j.Set("banned", true).Set("permanent", a.banUntilMs == kPermanentBan).Set("reason", a.banReason);
    if (a.banUntilMs != kPermanentBan) j.Set("until", static_cast<double>(a.banUntilMs));
    return j;
}

void OnlineServer::OnReport(ConnId c, Conn& k, const Json& j, int64_t now) {
    const int64_t seq = j.Int("s");
    const std::string target = j.Str("id");
    // Only someone you have played (now or before) can be reported.
    const OnlineMatch* live = MatchOf(k.account);
    const bool inMatchTogether = live && (live->players[0] == target || live->players[1] == target);
    if (!inMatchTogether && !store_.PlayedTogether(k.account, target))
        return Reply(c, seq, false, "you can only report players you have played");
    std::string id;
    Status s = store_.AddReport(k.account, target, j.Str("reason"), j.Str("match"), j.Str("note"), now, rnd_, &id);
    Reply(c, seq, s.ok, s.error, Obj().Set("report", id));
}

void OnlineServer::EnforceBan(const std::string& account, int64_t now) {
    Dequeue(account);
    RoomLeave(account, true);
    if (OnlineMatch* m = MatchById(presence_[account].match)) {
        const int side = m->players[0] == account ? 0 : 1;
        if (m->mode == "ranked")
            FinishMatch(*m, 1 - side, "forfeit", m->teamNames[side] + " was removed by a moderator", now);
        else
            AbandonSide(*m, side, "removed by a moderator", now);
        presence_[account].match.clear();
    }
    const Account* a = store_.Find(account);
    auto p = presence_.find(account);
    if (a && p != presence_.end() && p->second.conn) {
        Json b = BanJson(*a);
        b.Set("k", "banned");
        Send(p->second.conn, b, true);
        p->second.conn = 0;
    }
}

void OnlineServer::OnAdmin(ConnId c, const Json& j, int64_t now) {
    const int64_t seq = j.Int("s");
    const std::string kind = j.Str("k");
    auto player = [&](const std::string& id) {
        Json p = Obj();
        const Account* a = store_.Find(id);
        if (!a) return p;
        p.Set("id", a->id).Set("name", a->name).Set("country", a->country).Set("rating", a->rating);
        p.Set("matches", a->stats.matches).Set("reportsAgainst", static_cast<int64_t>(store_.ReportsAbout(id).size()));
        if (a->BannedAt(now)) p.Set("ban", BanJson(*a));
        return p;
    };
    auto reportJson = [&](const Report& r) {
        Json x = Obj();
        x.Set("id", r.id).Set("reason", r.reason).Set("note", r.note).Set("match", r.matchId);
        x.Set("created", r.createdMs).Set("status", r.status).Set("resolution", r.resolution);
        x.Set("reporter", player(r.reporter)).Set("target", player(r.target));
        return x;
    };
    if (kind == "admin-reports") {
        Json arr = Json::Array();
        for (const auto& r : store_.Reports(j.Str("status", "open"), 100)) arr.Push(reportJson(r));
        return Reply(c, seq, true, "", Obj().Set("reports", arr));
    }
    if (kind == "admin-player") {
        const std::string id = j.Str("id");
        if (!store_.Find(id)) return Reply(c, seq, false, "no such player");
        Json reps = Json::Array();
        for (const auto& r : store_.ReportsAbout(id)) reps.Push(reportJson(r));
        Json hist = Json::Array();
        for (const auto& m : store_.History(id, 20)) hist.Push(Store::MatchJson(m));
        return Reply(c, seq, true, "", Obj().Set("player", player(id)).Set("reports", reps).Set("history", hist));
    }
    if (kind == "admin-ban") {
        const std::string id = j.Str("id");
        const int64_t hours = j.Int("hours", -1);
        if (hours < 0 || hours > 24 * 365) return Reply(c, seq, false, "hours: 1..8760, or 0 for a permanent ban");
        const std::string reason = j.Str("reason");
        if (reason.empty()) return Reply(c, seq, false, "give a reason (the player sees it)");
        Status s = store_.Ban(id, hours == 0 ? kPermanentBan : now + hours * 3600 * 1000, reason, "admin", now);
        if (!s.ok) return Reply(c, seq, false, s.error);
        if (!j.Str("report").empty()) store_.ResolveReport(j.Str("report"), "actioned", reason, "admin", now);
        EnforceBan(id, now);
        boardCache_.clear();
        return Reply(c, seq, true, "", Obj().Set("player", player(id)));
    }
    if (kind == "admin-unban") {
        Status s = store_.Unban(j.Str("id"), "admin", now);
        boardCache_.clear();
        return Reply(c, seq, s.ok, s.error);
    }
    if (kind == "admin-resolve") {
        Status s = store_.ResolveReport(j.Str("report"), j.Str("status"), j.Str("note"), "admin", now);
        return Reply(c, seq, s.ok, s.error);
    }
    if (kind == "admin-rename") {
        Status s = store_.ResetName(j.Str("id"), "admin", now);
        if (s.ok && !j.Str("report").empty()) store_.ResolveReport(j.Str("report"), "actioned", "name reset", "admin", now);
        boardCache_.clear();
        return Reply(c, seq, s.ok, s.error, Obj().Set("player", player(j.Str("id"))));
    }
    Reply(c, seq, false, "unknown admin request");
}

}  // namespace cricket::online
