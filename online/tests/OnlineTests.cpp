// Online multiplayer server: guest accounts, duplicate-message protection,
// rooms, matchmaking, full matches between two clients, reconnection,
// forfeits / AI takeover, anti-cheat timing, crash recovery, stats,
// ratings and leaderboards. Everything here runs in-process (no network);
// the real-socket tests are in online/web/tests.
#include <filesystem>
#include <set>
#include <fstream>
#include <random>
#include <sstream>

#include "../../controller/tests/PhoneSim.h"
#include "../../core/tests/Test.h"
#include "cricket/online/Moderation.h"
#include "cricket/online/OnlineServer.h"

using namespace cricket;
using namespace cricket::online;
using cricket::ctl::testing::Counter;

namespace {

namespace fs = std::filesystem;

std::string TempDir(const std::string& name) {
    const std::string d = (fs::temp_directory_path() / ("cu_online_" + name)).string();
    fs::remove_all(d);
    return d;
}

std::string ReadAll(const std::string& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

struct Client;

// The server plus a fake network that delivers outbox messages to clients.
struct World {
    Store store;
    OnlineServer server;
    int64_t now = 1'000'000;
    std::map<ctl::ConnId, Client*> clients;
    explicit World(OnlineConfig cfg = Cfg(), std::string dir = "", uint64_t seed = 7)
        : server((store.Open(dir), store), std::move(cfg), Counter(seed)) {}
    static OnlineConfig Cfg() {
        OnlineConfig c;
        c.host.bowlWindowMs = 3000;
        c.rankedUnlockMatches = 0;  // tests that need it turn it on
        return c;
    }
    void Pump();
    void Step(int64_t ms = 100) {
        now += ms;
        server.Tick(now);
        Pump();
    }
};

struct Client {
    World& w;
    ctl::ConnId conn = 0;
    int64_t seq = 0;
    bool closed = false;
    std::vector<Json> inbox;
    std::string id, token, closeReason;
    explicit Client(World& world) : w(world) { Connect(); }
    ~Client() { w.clients.erase(conn); }
    void Connect() {
        w.clients.erase(conn);
        conn = *w.server.Open(w.now);
        w.clients[conn] = this;
        seq = 0;
        closed = false;
    }
    void Drop() {
        w.server.Closed(conn, w.now);
        w.clients.erase(conn);
        closed = true;
    }
    int64_t Raw(Json j) {
        w.server.Receive(conn, j.Dump(), w.now);
        w.Pump();
        return j.Int("s");
    }
    int64_t Req(const std::string& kind, Json j = Json::Object()) {
        j.Set("s", ++seq).Set("k", kind);
        return Raw(j);
    }
    std::optional<Json> Res(int64_t s) const {
        for (auto it = inbox.rbegin(); it != inbox.rend(); ++it)
            if (it->Str("k") == "res" && it->Int("re") == s) return *it;
        return std::nullopt;
    }
    bool Ok(int64_t s) const {
        auto r = Res(s);
        return r && r->Flag("ok");
    }
    std::string Err(int64_t s) const {
        auto r = Res(s);
        return r ? r->Str("err") : "(no reply)";
    }
    std::optional<Json> Last(const std::string& kind) const {
        for (auto it = inbox.rbegin(); it != inbox.rend(); ++it)
            if (it->Str("k") == kind) return *it;
        return std::nullopt;
    }
    int Count(const std::string& kind) const {
        int n = 0;
        for (const auto& j : inbox) n += j.Str("k") == kind;
        return n;
    }
    void Guest(const std::string& name, const std::string& country = "IN") {
        const int64_t s = Req("guest", Json::Object().Set("name", name).Set("country", country));
        auto r = Res(s);
        if (r) {
            id = r->Str("id");
            token = r->Str("token");
        }
    }
    bool Login() {
        Connect();
        const int64_t s = Req("login", Json::Object().Set("id", id).Set("token", token));
        return Ok(s);
    }
};

void World::Pump() {
    for (auto& o : server.TakeOutbox()) {
        auto it = clients.find(o.conn);
        if (it == clients.end()) continue;
        Client* c = it->second;
        auto j = Json::Parse(o.text);
        if (j) {
            c->inbox.push_back(*j);
            // Answer the server's round-trip pings straight away.
            if (j->Str("k") == "sping" && !o.close) {
                Json r = Json::Object();
                r.Set("s", ++c->seq).Set("k", "spong").Set("t", j->Int("t"));
                server.Receive(c->conn, r.Dump(), now);
            }
            if (j->Str("k") == "error") c->closeReason = j->Str("why");
        }
        if (o.close) {
            server.Closed(o.conn, now);
            clients.erase(o.conn);
            c->closed = true;
        }
    }
}

// A player that decides only from the state the server sends.
struct Bot {
    Client& c;
    std::mt19937_64 rng;
    int64_t actionId = 0;
    std::string lastSig;
    bool lateBat = false;  // claims perfect timing but taps late (cheat test)
    Bot(Client& client, uint64_t seed) : c(client), rng(seed) {}
    std::optional<Json> State() const { return c.Last("state"); }
    bool Has(const Json& s, const std::string& what) const {
        if (const Json* l = s.Get("controls"))
            for (const auto& x : l->Items())
                if (x.AsString() == what) return true;
        return false;
    }
    void Act(Json a) {
        Json j = Json::Object();
        j.Set("id", ++actionId).Set("a", a);
        c.Req("action", j);
    }
    void Play() {
        auto st = State();
        if (!st || c.closed) return;
        const Json& s = *st;
        const std::string sig = s.Str("waiting") + "|" + s.Str("stage") + "|" + s.Get("controls")->Dump() +
                                (s.Get("score") ? s.Get("score")->Dump() : "");
        if (sig == lastSig) return;
        lastSig = sig;
        auto u = [&] { return std::uniform_real_distribution<double>(0, 1)(rng); };
        auto first = [&](const char* list) {
            const Json* l = s.Get("options") ? s.Get("options")->Get(list) : nullptr;
            return l && !l->Items().empty() ? l->Items()[0].Int("id") : -1;
        };
        Json a = Json::Object();
        if (Has(s, "toss-call")) a.Set("do", "toss-call").Set("face", "heads");
        else if (Has(s, "toss-decide")) a.Set("do", "toss-decide").Set("choice", "bat");
        else if (Has(s, "openers")) {
            const Json& l = *s.Get("options")->Get("batters");
            a.Set("do", "openers").Set("striker", l.Items()[0].Int("id")).Set("nonStriker", l.Items()[1].Int("id"));
        } else if (Has(s, "batter")) a.Set("do", "batter").Set("player", first("batters"));
        else if (Has(s, "bowler")) a.Set("do", "bowler").Set("player", first("bowlers"));
        else if (Has(s, "review")) a.Set("do", "accept");
        else if (Has(s, "ready")) a.Set("do", "ready");
        else if (Has(s, "bowl")) a.Set("do", "bowl").Set("variation", "stock").Set("line", 0.0).Set("length", u());
        else if (Has(s, "bat") && !lateBat)
            a.Set("do", "bat").Set("shot", u() < 0.5 ? "drive" : "defend").Set("dir", 0).Set("timing", 0);
        else return;
        Act(a);
    }
};

// Both bots play until the match ends (or the step budget runs out).
bool PlayOut(World& w, std::vector<Bot*> bots, int maxSteps = 200000) {
    for (int i = 0; i < maxSteps; ++i) {
        for (Bot* b : bots) b->Play();
        w.Step(50);
        bool ended = true;
        for (Bot* b : bots) ended = ended && b->c.Last("end").has_value();
        if (ended || w.server.LiveMatches() == 0) return true;
    }
    return false;
}

// Two guests in a private room, match started.
struct RoomMatch {
    World& w;
    Client a, b;
    RoomMatch(World& world, const std::string& format = "overs=1") : w(world), a(world), b(world) {
        a.Guest("Asha", "IN");
        b.Guest("Bilal", "PK");
        const int64_t s = a.Req("room-create", Json::Object().Set("format", format).Set("team", "India"));
        const std::string code = a.Res(s)->Str("code");
        b.Req("room-join", Json::Object().Set("code", code).Set("team", "Pakistan"));
        a.Req("room-start");
    }
};

}  // namespace

// ---------------------------------------------------------------- Store

TEST(online_store_guest_tokens_are_hashed) {
    const std::string dir = TempDir("store1");
    std::string id, token;
    {
        Store s;
        CHECK_OK(s.Open(dir));
        auto rnd = Counter(3);
        auto [acc, tok] = s.CreateGuest("  Asha\x01  ", "in", 5, rnd);
        id = acc.id;
        token = tok;
        CHECK_EQ(acc.name, std::string("Asha"));
        CHECK_EQ(acc.country, std::string("IN"));
        CHECK(s.Verify(id, token) != nullptr);
        CHECK(s.Verify(id, token + "x") == nullptr);
        CHECK(s.Verify("nobody", token) == nullptr);
        auto [unnamed, t2] = s.CreateGuest("", "India", 6, rnd);
        CHECK(unnamed.name.rfind("Guest ", 0) == 0);
        CHECK_EQ(unnamed.country, std::string(""));  // not a 2-letter code
        (void)t2;
    }
    // The data file has the hash, never the token itself.
    const std::string file = ReadAll(dir + "/accounts.jsonl");
    CHECK(file.find(token) == std::string::npos);
    CHECK(file.find(id) != std::string::npos);
    // Reload, plus a damaged line (power cut) that is skipped.
    std::ofstream(dir + "/accounts.jsonl", std::ios::app) << "{\"id\":\"half";
    Store s2;
    CHECK_OK(s2.Open(dir));
    CHECK(s2.Verify(id, token) != nullptr);
    CHECK_EQ(s2.SkippedLines(), static_cast<size_t>(1));
    CHECK_EQ(s2.AccountCount(), static_cast<size_t>(2));
    // A tampered line fails its checksum.
    std::string text = ReadAll(dir + "/accounts.jsonl");
    text.replace(text.find("\"rating\":1200"), 13, "\"rating\":2900");
    std::ofstream(dir + "/accounts.jsonl") << text;
    Store s3;
    CHECK_OK(s3.Open(dir));
    CHECK(s3.SkippedLines() >= 2);
}

TEST(online_store_elo_and_win_trading_guard) {
    CHECK_EQ(Store::EloDelta(1200, 1200, 1.0, 0), 20);   // K 40, expected 0.5
    CHECK_EQ(Store::EloDelta(1200, 1200, 1.0, 10), 10);  // K 20 after 10 games
    CHECK_EQ(Store::EloDelta(1200, 1600, 1.0, 0), 36);   // upset win
    CHECK_EQ(Store::EloDelta(1200, 1200, 0.5, 0), 0);
    Store s;
    CHECK_OK(s.Open(""));
    auto rnd = Counter(9);
    auto a = s.CreateGuest("A", "IN", 0, rnd).first.id;
    auto b = s.CreateGuest("B", "PK", 0, rnd).first.id;
    auto rec = [&](const std::string& mode, int winner, int64_t t) {
        MatchRecord m;
        m.id = "m" + std::to_string(t);
        m.mode = mode;
        m.endedMs = t;
        m.sides[0].account = a;
        m.sides[1].account = b;
        m.sides[0].total = 150;
        m.sides[0].sixes = 3;
        m.sides[1].total = 120;
        m.winner = winner;
        m.outcome = winner >= 0 ? "win" : "tie";
        return s.RecordMatch(m);
    };
    auto r1 = rec("ranked", 0, 1000);
    CHECK_EQ(r1.ratingDelta[0], 20);
    CHECK_EQ(r1.ratingDelta[1], -20);
    CHECK_EQ(s.Find(a)->rating, 1220);
    rec("ranked", 0, 2000);
    rec("ranked", 0, 3000);
    auto r4 = rec("ranked", 0, 4000);  // 4th ranked game between them in 24 h
    CHECK_EQ(r4.ratingDelta[0], 0);
    CHECK_EQ(r4.ratingDelta[1], 0);
    auto rc = rec("casual", 1, 5000);  // casual never changes ratings
    CHECK_EQ(rc.ratingDelta[0], 0);
    const int before = s.Find(a)->rating;
    auto r5 = rec("ranked", 1, 4000 + 25LL * 3600 * 1000);  // a day later: counts again
    CHECK(r5.ratingDelta[1] > 0);
    CHECK(s.Find(a)->rating < before);
    auto st = s.StatsJson(a);
    CHECK_EQ(st.Int("matches"), 6);
    CHECK_EQ(st.Int("wins"), 4);
    CHECK_EQ(st.Int("losses"), 2);
    CHECK_EQ(st.Int("rankedMatches"), 5);
    CHECK_EQ(st.Int("runs"), 900);
    CHECK_EQ(st.Int("sixes"), 18);
    CHECK_EQ(s.History(a, 2).size(), static_cast<size_t>(2));
    CHECK_EQ(s.History(a, 2)[0].id, r5.id);  // newest first
}

TEST(online_store_leaderboards) {
    Store s;
    CHECK_OK(s.Open(""));
    auto rnd = Counter(11);
    std::vector<std::string> ids;
    for (const char* c : {"IN", "IN", "PK", "AU"}) ids.push_back(s.CreateGuest("P", c, 0, rnd).first.id);
    const int64_t day = 24LL * 3600 * 1000, now = 100 * day;
    int n = 0;
    auto rec = [&](int x, int y, int winner, int64_t t, int runsX, const std::string& mode = "casual") {
        MatchRecord m;
        m.id = "m" + std::to_string(++n);
        m.mode = mode;
        m.endedMs = t;
        m.sides[0].account = ids[x];
        m.sides[1].account = ids[y];
        m.sides[0].total = runsX;
        m.sides[0].bestIndividual = runsX / 2;
        m.sides[0].wicketsTaken = 4;
        m.winner = winner;
        m.outcome = "win";
        s.RecordMatch(m);
    };
    for (int i = 0; i < 5; ++i) rec(0, 1, 0, now - day, 100);       // P0 wins 5 this week
    rec(2, 3, 0, now - 20 * day, 400);                                // P2 big total, 20 days ago
    rec(1, 3, 0, now - day, 10, "ranked");
    auto top = [&](const Json& b) { return b.Get("rows")->Items()[0].Str("id"); };
    CHECK_EQ(top(s.Leaderboard("runs", "all", "all", "", now, 10)), ids[0]);   // 500 vs 400
    CHECK_EQ(top(s.Leaderboard("highscore", "all", "all", "", now, 10)), ids[2]);
    CHECK_EQ(top(s.Leaderboard("runs", "week", "all", "", now, 10)), ids[0]);
    CHECK_EQ(top(s.Leaderboard("runs", "month", "all", "PK", now, 10)), ids[2]);
    CHECK(s.Leaderboard("runs", "week", "all", "PK", now, 10).Get("rows")->Items().empty());
    CHECK_EQ(top(s.Leaderboard("wins", "all", "all", "IN", now, 10)), ids[0]);
    // Win % only for 5+ matches: P0 (5/5) and P1 (1/6) qualify.
    auto wp = s.Leaderboard("winpct", "all", "all", "", now, 10);
    CHECK_EQ(wp.Get("rows")->Items().size(), static_cast<size_t>(2));
    CHECK_EQ(wp.Get("rows")->Items()[0].Num("value"), 100.0);
    // Ratings: only players with ranked games.
    auto rt = s.Leaderboard("rating", "all", "ranked", "", now, 10);
    CHECK_EQ(rt.Get("rows")->Items().size(), static_cast<size_t>(2));
    CHECK_EQ(top(rt), ids[1]);
    CHECK_EQ(s.Leaderboard("wickets", "all", "all", "", now, 1).Get("rows")->Items().size(), static_cast<size_t>(1));
}

// ---------------------------------------------------------------- protocol

TEST(online_guest_login_and_sequence_numbers) {
    World w;
    Client a(w);
    // Nothing but guest/login before logging in.
    a.Req("teams");
    CHECK(a.closed);
    CHECK_EQ(a.closeReason, std::string("log in first"));

    Client b(w);
    b.Guest("Bilal", "PK");
    CHECK(!b.id.empty());
    CHECK_EQ(b.token.size(), static_cast<size_t>(43));  // 32 random bytes, base64url
    const int64_t s = b.Req("teams");
    CHECK(b.Ok(s));
    CHECK_EQ(b.Res(s)->Get("teams")->Items().size(), static_cast<size_t>(10));
    // A repeated message number is refused and the connection closed.
    Json dup = Json::Object();
    dup.Set("s", b.seq).Set("k", "teams");
    b.Raw(dup);
    CHECK(b.closed);
    CHECK_EQ(b.closeReason, std::string("duplicate or out-of-order message"));
    // Skipping numbers is refused too.
    CHECK(b.Login());
    Json skip = Json::Object();
    skip.Set("s", b.seq + 2).Set("k", "teams");
    b.Raw(skip);
    CHECK(b.closed);

    // Wrong token.
    Client c(w);
    c.id = b.id;
    c.token = "not-the-token";
    CHECK(!c.Login());
    CHECK(c.closed);

    // Logging in again elsewhere replaces the old connection.
    Client d1(w), d2(w);
    d1.Guest("Dev");
    d2.id = d1.id;
    d2.token = d1.token;
    CHECK(d2.Login());
    CHECK(d1.closed);
    CHECK_EQ(d1.Count("replaced"), 1);
    CHECK(!d2.closed);
}

TEST(online_limits_and_timeouts) {
    OnlineConfig cfg = World::Cfg();
    cfg.maxGuestsPerMinute = 2;
    cfg.maxMessagesPerSecond = 5;
    World w(cfg);
    Client a(w), b(w), c(w);
    a.Guest("A");
    b.Guest("B");
    c.Guest("C");
    CHECK(!a.id.empty());
    CHECK(!b.id.empty());
    CHECK(c.id.empty());  // guest creation is rate limited
    CHECK(c.Err(1).find("busy") != std::string::npos);
    for (int i = 0; i < 6; ++i) a.Req("teams");
    CHECK(a.closed);
    CHECK_EQ(a.closeReason, std::string("too many messages"));
    // Oversized message.
    Json big = Json::Object();
    big.Set("s", ++b.seq).Set("k", "profile").Set("name", std::string(9000, 'x'));
    b.Raw(big);
    CHECK(b.closed);
    // A connection that never logs in is closed.
    Client idle(w);
    w.Step(cfg.loginTimeoutMs + 100);
    CHECK(idle.closed);
}

TEST(online_private_room_codes_and_rules) {
    World w;
    Client a(w), b(w), c(w);
    a.Guest("Asha", "IN");
    b.Guest("Bilal", "PK");
    c.Guest("Chris", "AU");
    CHECK(!a.Ok(a.Req("room-create", Json::Object().Set("format", "test").Set("team", "India"))));
    CHECK(!a.Ok(a.Req("room-create", Json::Object().Set("format", "overs=5").Set("team", "Mumbai XI"))));
    CHECK(!a.Ok(a.Req("room-create", Json::Object().Set("format", "overs=51").Set("team", "India"))));
    const int64_t s = a.Req("room-create", Json::Object().Set("format", "overs=5").Set("team", "India"));
    CHECK(a.Ok(s));
    const std::string code = a.Res(s)->Str("code");
    CHECK_EQ(code.size(), static_cast<size_t>(6));
    CHECK(code.find_first_not_of("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") == std::string::npos);
    CHECK_EQ(a.Err(a.Req("room-start")), std::string("waiting for a friend to join"));
    CHECK_EQ(b.Err(b.Req("room-join", Json::Object().Set("code", "ZZZZZZ").Set("team", "Pakistan"))),
             std::string("no room with that code"));
    CHECK(b.Err(b.Req("room-join", Json::Object().Set("code", code).Set("team", "India")))
              .find("choose another team") != std::string::npos);
    // Codes are forgiving: lower case and spaces work.
    std::string typed = code;
    for (auto& ch : typed) ch = static_cast<char>(std::tolower(ch));
    typed.insert(3, " ");
    CHECK(b.Ok(b.Req("room-join", Json::Object().Set("code", typed).Set("team", "Pakistan"))));
    CHECK_EQ(a.Last("room")->Get("guest")->Str("team"), std::string("Pakistan"));
    CHECK_EQ(c.Err(c.Req("room-join", Json::Object().Set("code", code).Set("team", "Australia"))),
             std::string("room is full"));
    CHECK_EQ(b.Err(b.Req("room-start")), std::string("only the host can start"));
    // Host leaves: the room closes for the guest.
    a.Req("room-leave");
    CHECK_EQ(b.Count("room-closed"), 1);
    CHECK_EQ(w.server.Rooms(), static_cast<size_t>(0));
    // Unused rooms expire.
    c.Req("room-create", Json::Object().Set("format", "t20").Set("team", "Australia"));
    CHECK_EQ(w.server.Rooms(), static_cast<size_t>(1));
    w.Step(30 * 60 * 1000 + 1000);
    CHECK_EQ(w.server.Rooms(), static_cast<size_t>(0));
}

TEST(online_full_match_india_vs_pakistan_in_private_room) {
    World w;
    RoomMatch rm(w);
    CHECK_EQ(w.server.LiveMatches(), static_cast<size_t>(1));
    auto ma = rm.a.Last("match");
    auto mb = rm.b.Last("match");
    CHECK(ma && mb);
    CHECK_EQ(ma->Int("side"), 0);
    CHECK_EQ(mb->Int("side"), 1);
    CHECK_EQ(ma->Str("mode"), std::string("casual"));
    CHECK_EQ(ma->Get("sides")->Items()[1].Str("team"), std::string("Pakistan"));
    Bot ba(rm.a, 1), bb(rm.b, 2);
    CHECK(PlayOut(w, {&ba, &bb}));
    auto ea = rm.a.Last("end");
    auto eb = rm.b.Last("end");
    CHECK(ea && eb);
    const Json& rec = *ea->Get("record");
    CHECK_EQ(rec.Str("mode"), std::string("casual"));
    CHECK(rec.Str("outcome") == "win" || rec.Str("outcome") == "tie");
    CHECK_EQ(ea->Int("delta"), 0);
    // The server's own scorebook is what gets recorded.
    const MatchRecord* stored = w.store.FindMatch(rec.Str("id"));
    CHECK(stored != nullptr);
    CHECK(stored->sides[0].total + stored->sides[1].total > 0);
    CHECK(stored->sides[0].legalBalls <= 6);
    CHECK_EQ(w.server.LiveMatches(), static_cast<size_t>(0));
    // History, stats and leaderboard requests.
    const int64_t h = rm.a.Req("history");
    CHECK_EQ(rm.a.Res(h)->Get("matches")->Items().size(), static_cast<size_t>(1));
    const int64_t st = rm.b.Req("stats", Json::Object().Set("id", rm.a.id));
    CHECK_EQ(rm.b.Res(st)->Get("stats")->Int("matches"), 1);
    const int64_t lb = rm.a.Req("leaderboard", Json::Object().Set("board", "runs").Set("country", "PK"));
    CHECK_EQ(rm.a.Res(lb)->Get("board")->Get("rows")->Items().size(), static_cast<size_t>(1));
    CHECK(!rm.a.Ok(rm.a.Req("leaderboard", Json::Object().Set("board", "secret"))));
    // No duplicate states: every action was acknowledged exactly once.
    CHECK(rm.a.Count("ack") >= static_cast<int>(ba.actionId));
}

TEST(online_matchmaking_casual_ranked_and_team_clash) {
    World w;
    Client a(w), b(w), c(w);
    a.Guest("A");
    b.Guest("B");
    c.Guest("C");
    CHECK(!a.Ok(a.Req("queue", Json::Object().Set("mode", "silly").Set("team", "India"))));
    a.Req("queue", Json::Object().Set("mode", "casual").Set("format", "overs=2").Set("team", "India"));
    b.Req("queue", Json::Object().Set("mode", "ranked").Set("format", "overs=2").Set("team", "India"));
    w.Step();
    CHECK_EQ(w.server.Queued(), static_cast<size_t>(2));  // different modes don't mix
    c.Req("queue", Json::Object().Set("mode", "casual").Set("format", "overs=2").Set("team", "India"));
    w.Step();
    CHECK_EQ(w.server.Queued(), static_cast<size_t>(1));
    auto m = c.Last("match");
    CHECK(m.has_value());
    // Both picked India: the later player gets the next team.
    CHECK_EQ(m->Get("sides")->Items()[0].Str("team"), std::string("India"));
    CHECK_EQ(m->Get("sides")->Items()[1].Str("team"), std::string("Pakistan"));
    CHECK_EQ(m->Int("side"), 1);
    // Can't queue during a match; cancelling works.
    CHECK(!a.Ok(a.Req("queue", Json::Object().Set("team", "India"))));
    b.Req("queue-cancel");
    CHECK_EQ(w.server.Queued(), static_cast<size_t>(0));
}

TEST(online_ranked_rating_band_widens_with_waiting) {
    World w;
    Client a(w), b(w);
    a.Guest("Strong");
    b.Guest("New");
    // Give A a high rating through recorded ranked wins against others.
    auto rnd = Counter(77);
    for (int i = 0; i < 20; ++i) {
        auto opp = w.store.CreateGuest("X", "", 0, rnd).first.id;
        MatchRecord m;
        m.id = "seed" + std::to_string(i);
        m.mode = "ranked";
        m.endedMs = i;
        m.sides[0].account = a.id;
        m.sides[1].account = opp;
        m.winner = 0;
        m.outcome = "win";
        w.store.RecordMatch(m);
    }
    const int gap = w.store.Find(a.id)->rating - 1200;
    CHECK(gap > 150 && gap < 450);
    a.Req("queue", Json::Object().Set("mode", "ranked").Set("format", "overs=2").Set("team", "India"));
    b.Req("queue", Json::Object().Set("mode", "ranked").Set("format", "overs=2").Set("team", "Pakistan"));
    w.Step(100);
    CHECK_EQ(w.server.Queued(), static_cast<size_t>(2));  // too far apart at first
    const int64_t needSec = (gap - 100 + 24) / 25;
    w.Step((needSec - 1) * 1000 - 200);
    CHECK_EQ(w.server.Queued(), static_cast<size_t>(2));
    w.Step(1500);
    CHECK_EQ(w.server.Queued(), static_cast<size_t>(0));
    CHECK_EQ(a.Last("match")->Str("mode"), std::string("ranked"));
}

TEST(online_reconnect_resumes_and_duplicate_actions_ignored) {
    World w;
    RoomMatch rm(w, "overs=5");
    Bot ba(rm.a, 1), bb(rm.b, 2);
    for (int i = 0; i < 20; ++i) {
        ba.Play();
        bb.Play();
        w.Step(50);
    }
    CHECK(w.server.MatchOf(rm.b.id) != nullptr);
    if (!w.server.MatchOf(rm.b.id)) return;
    // A's internet drops.
    auto ballsBowled = [&] {
        const auto& ses = w.server.MatchOf(rm.b.id)->host->Session();
        return ses.HasMatch() ? ses.GetMatch().Current().Balls().size() : size_t{0};
    };
    const auto ballsBefore = ballsBowled();
    rm.a.Drop();
    w.Step(100);
    CHECK(rm.b.Last("notice")->Str("text").find("disconnected") != std::string::npos);
    for (int i = 0; i < 100; ++i) {
        bb.Play();
        w.Step(100);
    }
    CHECK_EQ(rm.b.Last("state")->Int("waitingForController"), 0);  // the match waits for A
    CHECK_EQ(ballsBowled(), ballsBefore);  // no balls bowled while A is away
    // A comes back within the grace period.
    rm.a.inbox.clear();
    CHECK(rm.a.Login());
    w.Step();
    auto m = rm.a.Last("match");
    CHECK(m.has_value());
    CHECK_EQ(m->Int("last"), ba.actionId);  // the client can continue its action numbers
    CHECK(rm.a.Last("state").has_value());  // full state sent again
    // Re-sending an action that was already handled: acknowledged, not applied.
    const auto n = w.server.MatchOf(rm.a.id)->host->Session().Inputs().size();
    Json j = Json::Object();
    j.Set("id", ba.actionId).Set("a", Json::Object().Set("do", "toss-call").Set("face", "tails"));
    rm.a.Req("action", j);
    CHECK(rm.a.Last("ack")->Flag("dup"));
    CHECK_EQ(w.server.MatchOf(rm.a.id)->host->Session().Inputs().size(), n);
    ba.lastSig.clear();
    CHECK(PlayOut(w, {&ba, &bb}));
    CHECK(rm.a.Last("end").has_value());
}

TEST(online_ranked_disconnect_is_a_forfeit) {
    World w;
    Client a(w), b(w);
    a.Guest("A");
    b.Guest("B");
    a.Req("queue", Json::Object().Set("mode", "ranked").Set("format", "overs=5").Set("team", "India"));
    b.Req("queue", Json::Object().Set("mode", "ranked").Set("format", "overs=5").Set("team", "Pakistan"));
    w.Step();
    Bot ba(a, 1), bb(b, 2);
    for (int i = 0; i < 4; ++i) {  // drops out right after the toss
        ba.Play();
        bb.Play();
        w.Step(50);
    }
    b.Drop();
    w.Step(119000);
    CHECK_EQ(w.server.LiveMatches(), static_cast<size_t>(1));  // still inside the grace period
    w.Step(2000);
    CHECK_EQ(w.server.LiveMatches(), static_cast<size_t>(0));
    auto e = a.Last("end");
    CHECK(e.has_value());
    CHECK_EQ(e->Get("record")->Str("outcome"), std::string("forfeit"));
    CHECK_EQ(e->Get("record")->Int("winner"), 0);
    // Left within the first 12 balls: the leaver loses rating, but the
    // winner gains nothing (otherwise fake accounts could feed points).
    CHECK_EQ(e->Int("delta"), 0);
    CHECK_EQ(w.store.Find(b.id)->rating, 1180);
    CHECK_EQ(w.store.Find(a.id)->rating, 1200);
    // B logs back in later: no match any more.
    b.inbox.clear();
    CHECK(b.Login());
    CHECK(!b.Last("match").has_value());
}

TEST(online_casual_disconnect_hands_side_to_ai) {
    World w;
    RoomMatch rm(w);
    Bot ba(rm.a, 1);
    rm.b.Drop();
    w.Step(121000);
    CHECK_EQ(w.server.LiveMatches(), static_cast<size_t>(1));
    CHECK(rm.a.Last("notice")->Str("text").find("computer captain") != std::string::npos);
    CHECK(w.server.MatchOf(rm.a.id)->aiTakeover[1]);
    CHECK(PlayOut(w, {&ba}));  // A finishes the match against the AI
    auto e = rm.a.Last("end");
    CHECK(e.has_value());
    CHECK(e->Get("record")->Str("outcome") != "forfeit");
    // B returns later and can't control the AI side.
}

TEST(online_idle_captain_and_concede) {
    OnlineConfig cfg = World::Cfg();
    cfg.idleLimitMs = 10000;
    World w(cfg);
    RoomMatch rm(w);
    // Nobody calls the toss: the side that must decide is treated as gone.
    const int tossSide = w.server.MatchOf(rm.a.id)->host->Session().WaitingFor().side;
    CHECK(tossSide == 0 || tossSide == 1);
    w.Step(1000);
    CHECK(!w.server.MatchOf(rm.a.id)->aiTakeover[tossSide]);
    w.Step(10500);
    CHECK(w.server.MatchOf(rm.a.id)->aiTakeover[tossSide]);

    RoomMatch r2(w);
    const int64_t s = r2.a.Req("concede");
    CHECK(r2.a.Ok(s));
    CHECK_EQ(r2.b.Last("end")->Get("record")->Int("winner"), 1);
    CHECK_EQ(r2.b.Last("end")->Get("record")->Str("outcome"), std::string("forfeit"));
    CHECK(!r2.a.Ok(r2.a.Req("concede")));
}

TEST(online_batting_timing_is_judged_on_the_server) {
    World w;
    RoomMatch rm(w, "overs=2");
    Bot ba(rm.a, 1), bb(rm.b, 2);
    ba.lateBat = bb.lateBat = true;
    const OnlineMatch* m = w.server.MatchOf(rm.a.id);
    int cheats = 0;
    for (int i = 0; i < 4000 && cheats < 3; ++i) {
        ba.Play();
        bb.Play();
        w.Step(50);
        // When the ball is in the air, the batting client claims "perfect
        // timing" (0 ms) but actually taps 700 ms after the ball arrived.
        for (Client* c : {&rm.a, &rm.b}) {
            auto st = c->Last("state");
            if (!st || st->Str("stage") != "ball") continue;
            if (!st->Get("controls")) continue;
            bool canBat = false;
            for (const auto& x : st->Get("controls")->Items()) canBat = canBat || x.AsString() == "bat";
            if (!canBat) continue;
            w.Step(1400 + 700);
            Bot& b = c == &rm.a ? ba : bb;
            b.Act(Json::Object().Set("do", "bat").Set("shot", "loft").Set("dir", 0).Set("timing", 0));
            ++cheats;
            c->inbox.push_back(Json::Object().Set("k", "state").Set("stage", "done").Set("controls", Json::Array()));
            break;
        }
    }
    CHECK(cheats >= 1);
    m = w.server.MatchOf(rm.a.id);
    CHECK(m != nullptr);
    if (m) CHECK(m->host->TimingCorrections() >= 1);
}

TEST(online_server_restart_recovers_live_matches) {
    const std::string dir = TempDir("recover");
    std::string aId, aTok, bId, bTok, fingerprint, matchId;
    int64_t aLast = 0, bLast = 0;
    {
        OnlineConfig cfg = World::Cfg();
        cfg.dataDir = dir;
        World w(cfg, dir);
        RoomMatch rm(w, "overs=5");
        Bot ba(rm.a, 1), bb(rm.b, 2);
        for (int i = 0; i < 40; ++i) {
            ba.Play();
            bb.Play();
            w.Step(50);
        }
        const OnlineMatch* m = w.server.MatchOf(rm.a.id);
        CHECK(m != nullptr);
        CHECK(m->host->Session().Inputs().size() > 10);
        fingerprint = m->host->Session().StateFingerprint();
        matchId = m->id;
        aId = rm.a.id, aTok = rm.a.token, bId = rm.b.id, bTok = rm.b.token;
        aLast = ba.actionId;
        bLast = bb.actionId;
        // The server process dies here (no clean shutdown).
    }
    OnlineConfig cfg = World::Cfg();
    cfg.dataDir = dir;
    World w(cfg, dir);
    CHECK(w.store.Verify(aId, aTok) != nullptr);
    CHECK_EQ(w.server.RecoverLiveMatches(w.now), static_cast<size_t>(1));
    OnlineMatch* m = w.server.MatchById(matchId);
    CHECK(m != nullptr);
    if (!m) return;
    CHECK_EQ(m->host->Session().StateFingerprint(), fingerprint);
    Client a(w), b(w);
    a.id = aId, a.token = aTok, b.id = bId, b.token = bTok;
    CHECK(a.Login());
    CHECK(b.Login());
    CHECK(a.Last("match").has_value());
    Bot ba(a, 3), bb(b, 4);
    ba.actionId = aLast;
    bb.actionId = bLast;
    bool done = PlayOut(w, {&ba, &bb}, 3000);
    if (!done) {
        std::cerr << "A: " << a.Last("state")->Dump() << "\nB: " << b.Last("state")->Dump() << "\n";
        for (auto& j : a.inbox) if (j.Str("k") == "ack" && !j.Flag("ok")) std::cerr << j.Dump() << "\n";
    }
    CHECK(done);
    auto e = a.Last("end");
    CHECK(e.has_value());
    if (!e) return;
    // Finished: live files removed, replay file kept as evidence.
    CHECK(!fs::exists(dir + "/live/" + matchId + ".journal"));
    CHECK(fs::exists(dir + "/" + e->Get("record")->Str("save")));
    auto load = LoadSessionFile(dir + "/" + e->Get("record")->Str("save"));
    CHECK_OK(load.status);
    // Restart again: the finished match is in the history.
    Store s2;
    CHECK_OK(s2.Open(dir));
    CHECK(s2.FindMatch(matchId) != nullptr);
    CHECK_EQ(s2.Find(aId)->stats.matches, 1);
}

// ---------------------------------------------------------------- anti-manipulation

TEST(online_ratings_resist_farming_and_tampering) {
    const std::string dir = TempDir("farm");
    Store s;
    CHECK_OK(s.Open(dir));
    auto rnd = Counter(31);
    const std::string main = s.CreateGuest("Main", "IN", 0, rnd).first.id;
    int n = 0;
    auto rec = [&](const std::string& x, const std::string& y, const std::string& outcome, int balls) {
        MatchRecord m;
        m.id = "f" + std::to_string(++n);
        m.mode = "ranked";
        m.endedMs = n;
        m.sides[0].account = x;
        m.sides[1].account = y;
        m.sides[0].legalBalls = balls;
        m.winner = 0;
        m.outcome = outcome;
        return s.RecordMatch(m);
    };
    // Instant concede by a fresh account: the winner gains nothing.
    const std::string alt1 = s.CreateGuest("Alt", "", 0, rnd).first.id;
    auto r = rec(main, alt1, "forfeit", 3);
    CHECK_EQ(r.ratingDelta[0], 0);
    CHECK(r.ratingDelta[1] < 0);
    // A forfeit after 12+ balls counts normally.
    const std::string alt2 = s.CreateGuest("Alt", "", 0, rnd).first.id;
    CHECK(rec(main, alt2, "forfeit", 12).ratingDelta[0] > 0);
    // Once established (5 ranked games), wins over provisional accounts are halved.
    for (int i = 0; i < 3; ++i) rec(main, s.CreateGuest("Alt", "", 0, rnd).first.id, "win", 30);
    CHECK_EQ(s.Find(main)->ratedGames, 5);
    const std::string fresh = s.CreateGuest("Alt", "", 0, rnd).first.id;
    const int full = Store::EloDelta(s.Find(main)->rating, 1200, 1.0, 5);
    CHECK_EQ(rec(main, fresh, "win", 30).ratingDelta[0], full / 2);
    const int rating = s.Find(main)->rating;
    // Forfeits don't count on the stat leaderboards.
    auto wins = s.Leaderboard("wins", "all", "all", "", 100, 10);
    CHECK_EQ(wins.Get("rows")->Items()[0].Num("value"), 4.0);  // 4 real wins, not the 2 forfeits
    // Editing a rating in the accounts file (even with a valid checksum)
    // changes nothing: ratings are rebuilt from the match history.
    Json fake = Json::Object();
    fake.Set("id", main).Set("name", "Main").Set("country", "IN").Set("tok", s.Find(main)->tokenHash);
    fake.Set("created", 0).Set("rating", 3000).Set("rated", 99);
    const std::string body = fake.Dump();
    auto d = ctl::Sha256(body);
    std::ofstream(dir + "/accounts.jsonl", std::ios::app)
        << body << "\t#" << ctl::ToHex(std::string(d.begin(), d.begin() + 4)) << "\n";
    Store again;
    CHECK_OK(again.Open(dir));
    CHECK_EQ(again.Find(main)->rating, rating);
    CHECK_EQ(again.Find(main)->ratedGames, 6);
}

TEST(online_ranked_unlocks_after_full_matches_and_code_guessing_limited) {
    OnlineConfig cfg = World::Cfg();
    cfg.rankedUnlockMatches = 1;
    World w(cfg);
    RoomMatch rm(w, "overs=1");
    const int64_t q = rm.a.Req("queue", Json::Object().Set("mode", "ranked").Set("team", "India"));
    CHECK(rm.a.Err(q).find("finish your match") != std::string::npos);
    Bot ba(rm.a, 1), bb(rm.b, 2);
    CHECK(PlayOut(w, {&ba, &bb}));
    CHECK(rm.a.Ok(rm.a.Req("queue", Json::Object().Set("mode", "ranked").Set("team", "India"))));
    Client c(w);
    c.Guest("New");
    CHECK(c.Err(c.Req("queue", Json::Object().Set("mode", "ranked").Set("team", "India")))
              .find("unlock ranked") != std::string::npos);
    CHECK(c.Ok(c.Req("queue", Json::Object().Set("mode", "casual").Set("team", "India"))));
    // Guessing room codes: 10 wrong tries a minute, then refused.
    for (int i = 0; i < 10; ++i) c.Req("room-join", Json::Object().Set("code", "AAAAA" + std::string(1, 'A' + i)).Set("team", "Pakistan"));
    CHECK(c.Err(c.Req("room-join", Json::Object().Set("code", "BBBBBB").Set("team", "Pakistan")))
              .find("too many wrong codes") != std::string::npos);
}

TEST(online_action_ids_survive_a_server_crash) {
    const std::string dir = TempDir("acts");
    std::string aId, aTok, matchId;
    int64_t aLast = 0;
    {
        OnlineConfig cfg = World::Cfg();
        cfg.dataDir = dir;
        World w(cfg, dir);
        RoomMatch rm(w, "overs=5");
        Bot ba(rm.a, 1), bb(rm.b, 2);
        for (int i = 0; i < 40; ++i) {
            ba.Play();
            bb.Play();
            w.Step(50);
        }
        aId = rm.a.id, aTok = rm.a.token, aLast = ba.actionId;
        matchId = w.server.MatchOf(aId)->id;
        CHECK(aLast > 0);
    }
    OnlineConfig cfg = World::Cfg();
    cfg.dataDir = dir;
    World w(cfg, dir);
    CHECK_EQ(w.server.RecoverLiveMatches(w.now), static_cast<size_t>(1));
    CHECK_EQ(w.server.MatchById(matchId)->lastActionId[0], aLast);
    Client a(w);
    a.id = aId, a.token = aTok;
    CHECK(a.Login());
    const auto before = w.server.MatchById(matchId)->host->Session().Inputs().size();
    // The phone re-sends its last control after the crash: recognised, not repeated.
    Json j = Json::Object();
    j.Set("id", aLast).Set("a", Json::Object().Set("do", "bowl").Set("variation", "stock"));
    a.Req("action", j);
    CHECK(a.Last("ack")->Flag("dup"));
    w.Step();
    CHECK_EQ(w.server.MatchById(matchId)->host->Session().Inputs().size(), before);
}

// ---------------------------------------------------------------- moderation

TEST(online_name_filter) {
    CHECK_EQ(NameProblem("Asha"), std::string(""));
    CHECK_EQ(NameProblem("Virat Fan 18"), std::string(""));
    CHECK_EQ(NameProblem("Sussex Dickson"), std::string(""));    // whole-word rule avoids false alarms
    CHECK_EQ(NameProblem("Pakistan Zindabad"), std::string(""));
    CHECK(!NameProblem("fuck you").empty());
    CHECK(!NameProblem("F.u.C.k").empty());                      // joined up
    CHECK(!NameProblem("Ch00tiya").empty());                     // look-alike digits
    CHECK(!NameProblem("bhen chod").empty());
    CHECK(!NameProblem("big Sh1t").empty());
    CHECK(!NameProblem("paki").empty());
    CHECK(!NameProblem("Official Admin").empty());
    CHECK(!NameProblem("call 9876543210").empty());
    CHECK(!NameProblem("www.spam.in").empty());
}

TEST(online_reports_bans_and_admin_review) {
    OnlineConfig cfg = World::Cfg();
    cfg.adminToken = std::string(43, 'k');
    World w(cfg);
    Client bad(w);
    bad.Guest("Ch00tiya");
    CHECK(bad.id.empty());  // blocked name
    RoomMatch rm(w, "overs=1");
    Bot ba(rm.a, 1), bb(rm.b, 2);
    CHECK(PlayOut(w, {&ba, &bb}));
    CHECK(!rm.a.Ok(rm.a.Req("profile", Json::Object().Set("name", "Admin Asha"))));
    // Reports: only players you have played; one per player per day.
    Client stranger(w);
    stranger.Guest("Stranger");
    CHECK(stranger.Err(stranger.Req("report", Json::Object().Set("id", rm.b.id).Set("reason", "abuse")))
              .find("only report players you have played") != std::string::npos);
    CHECK(!rm.a.Ok(rm.a.Req("report", Json::Object().Set("id", rm.b.id).Set("reason", "because"))));
    const int64_t r1 = rm.a.Req("report", Json::Object().Set("id", rm.b.id).Set("reason", "cheating").Set("note", "<script>x</script>"));
    CHECK(rm.a.Ok(r1));
    const std::string reportId = rm.a.Res(r1)->Str("report");
    CHECK(!rm.a.Ok(rm.a.Req("report", Json::Object().Set("id", rm.b.id).Set("reason", "abuse"))));
    // Admin: wrong token is refused; 5 failures lock admin out.
    Client wrong(w);
    CHECK(!wrong.Ok(wrong.Req("admin-login", Json::Object().Set("token", "guess"))));
    CHECK(wrong.closed);
    Client admin(w);
    CHECK(admin.Ok(admin.Req("admin-login", Json::Object().Set("token", cfg.adminToken))));
    CHECK(!admin.Ok(admin.Req("queue", Json::Object().Set("team", "India"))));  // admins don't play
    const int64_t lr = admin.Req("admin-reports");
    CHECK_EQ(admin.Res(lr)->Get("reports")->Items().size(), static_cast<size_t>(1));
    CHECK_EQ(admin.Res(lr)->Get("reports")->Items()[0].Get("target")->Str("name"), std::string("Bilal"));
    // Temporary ban: B is disconnected and can't log in; hidden from leaderboards.
    CHECK(!admin.Ok(admin.Req("admin-ban", Json::Object().Set("id", rm.b.id).Set("hours", 24))));  // reason needed
    CHECK(admin.Ok(admin.Req("admin-ban", Json::Object().Set("id", rm.b.id).Set("hours", 24).Set("reason", "cheating").Set("report", reportId))));
    w.Step();
    CHECK(rm.b.closed);
    CHECK_EQ(rm.b.Count("banned"), 1);
    CHECK_EQ(w.store.Reports("open", 10).size(), static_cast<size_t>(0));
    rm.b.inbox.clear();
    CHECK(!rm.b.Login());
    CHECK(rm.b.Last("res")->Flag("banned"));
    const int64_t lb = rm.a.Req("leaderboard", Json::Object().Set("board", "matches"));
    CHECK_EQ(rm.a.Res(lb)->Get("board")->Get("rows")->Items().size(), static_cast<size_t>(1));
    // The ban ends by itself.
    w.now += 25LL * 3600 * 1000;
    CHECK(rm.b.Login());
    // Permanent ban during a ranked match: forfeit, can never log in again.
    Client c(w), d(w);
    c.Guest("Cheat");
    d.Guest("Honest");
    c.Req("queue", Json::Object().Set("mode", "ranked").Set("team", "India"));
    d.Req("queue", Json::Object().Set("mode", "ranked").Set("team", "Pakistan"));
    w.Step();
    CHECK(w.server.MatchOf(c.id) != nullptr);
    CHECK(admin.Ok(admin.Req("admin-ban", Json::Object().Set("id", c.id).Set("hours", 0).Set("reason", "bot"))));
    w.Step();
    CHECK_EQ(d.Last("end")->Get("record")->Str("outcome"), std::string("forfeit"));
    CHECK_EQ(d.Last("end")->Get("record")->Int("winner"), 1);
    w.now += 400LL * 24 * 3600 * 1000;
    CHECK(!c.Login());
    CHECK(c.Last("res")->Flag("permanent"));
    // Unban and rename, all logged.
    CHECK(admin.Ok(admin.Req("admin-unban", Json::Object().Set("id", c.id))));
    CHECK(c.Login());
    CHECK(admin.Ok(admin.Req("admin-rename", Json::Object().Set("id", c.id))));
    CHECK(w.store.Find(c.id)->name.rfind("Player ", 0) == 0);
}

TEST(online_moderation_survives_restart) {
    const std::string dir = TempDir("mod");
    auto rnd = Counter(5);
    std::string a, b, rid;
    {
        Store s;
        CHECK_OK(s.Open(dir));
        a = s.CreateGuest("A", "IN", 0, rnd).first.id;
        b = s.CreateGuest("B", "PK", 0, rnd).first.id;
        CHECK_OK(s.AddReport(a, b, "abuse", "", "rude", 10, rnd, &rid));
        CHECK_OK(s.Ban(b, kPermanentBan, "abuse", "admin", 20));
        CHECK_OK(s.ResolveReport(rid, "actioned", "banned", "admin", 30));
        CHECK(!s.Ban(a, 5, "past", "admin", 40).ok);
    }
    Store s;
    CHECK_OK(s.Open(dir));
    CHECK(s.Find(b)->BannedAt(INT64_MAX - 1));
    CHECK_EQ(s.ReportsAbout(b).size(), static_cast<size_t>(1));
    CHECK_EQ(s.ReportsAbout(b)[0].status, std::string("actioned"));
    const std::string log = ReadAll(dir + "/moderation.jsonl");
    CHECK(log.find("ban-permanent") != std::string::npos);
    CHECK(log.find("report-actioned") != std::string::npos);
}

// ---------------------------------------------------------------- single player

TEST(online_single_player_vs_computer) {
    OnlineConfig cfg = World::Cfg();
    cfg.rankedUnlockMatches = 1;
    World w(cfg);
    Client me(w);
    me.Guest("Solo", "IN");
    CHECK(!me.Ok(me.Req("solo", Json::Object().Set("team", "India").Set("opponent", "India"))));
    CHECK(!me.Ok(me.Req("solo", Json::Object().Set("team", "Narnia"))));
    CHECK(me.Ok(me.Req("solo", Json::Object().Set("team", "India").Set("format", "overs=2"))));
    auto m = me.Last("match");
    CHECK(m.has_value());
    CHECK_EQ(m->Str("mode"), std::string("solo"));
    CHECK_EQ(m->Int("side"), 0);
    CHECK_EQ(m->Get("sides")->Items()[1].Str("team"), std::string("Pakistan"));  // default opponent
    CHECK_EQ(m->Get("sides")->Items()[1].Str("player"), std::string("Computer"));
    CHECK(m->Get("sides")->Items()[1].Flag("ai"));
    CHECK(!me.Ok(me.Req("solo", Json::Object().Set("team", "India"))));  // one match at a time
    // Play only my side; the computer captain does everything for the other side.
    Bot bot(me, 9);
    std::set<std::string> controlsSeen;
    bool sawScore = false;
    for (int i = 0; i < 20000 && !me.Last("end"); ++i) {
        bot.Play();
        if (auto st = me.Last("state")) {
            if (const Json* c = st->Get("controls"))
                for (const auto& x : c->Items()) controlsSeen.insert(x.AsString());
            sawScore = sawScore || st->Get("score") != nullptr;
        }
        w.Step(50);
    }
    auto e = me.Last("end");
    CHECK(e.has_value());
    if (!e) return;
    CHECK(sawScore);                       // live scoreboard was sent
    CHECK(controlsSeen.count("bowl") == 1);  // I bowled ...
    CHECK(controlsSeen.count("bat") == 1);   // ... and batted
    CHECK(controlsSeen.count("toss-call") + controlsSeen.count("toss-decide") >= 1);
    const Json& rec = *e->Get("record");
    CHECK_EQ(rec.Str("mode"), std::string("solo"));
    CHECK(rec.Str("outcome") == "win" || rec.Str("outcome") == "tie");
    CHECK(rec.Get("s0")->Int("balls") > 0);
    CHECK(rec.Get("s1")->Int("balls") > 0);  // the computer batted too
    CHECK(rec.Get("s1")->Int("balls") <= 12);
    CHECK(!rec.Str("summary").empty());
    // Practice: history yes; stats, ratings, leaderboards and ranked unlock no.
    CHECK_EQ(me.Res(me.Req("history"))->Get("matches")->Items().size(), static_cast<size_t>(1));
    CHECK_EQ(w.store.Find(me.id)->stats.matches, 0);
    CHECK_EQ(w.store.Find(me.id)->rating, 1200);
    CHECK(me.Res(me.Req("leaderboard", Json::Object().Set("board", "matches")))->Get("board")->Get("rows")->Items().empty());
    CHECK(me.Err(me.Req("queue", Json::Object().Set("mode", "ranked").Set("team", "India"))).find("unlock") != std::string::npos);
    // Online play still works afterwards.
    CHECK(me.Ok(me.Req("room-create", Json::Object().Set("team", "India"))));
}

TEST(online_single_player_leaving_ends_without_result) {
    World w;
    Client me(w);
    me.Guest("Solo");
    CHECK(me.Ok(me.Req("solo", Json::Object().Set("team", "Australia").Set("opponent", "England"))));
    me.Drop();
    w.Step(121000);
    CHECK_EQ(w.server.LiveMatches(), static_cast<size_t>(0));
    me.inbox.clear();
    CHECK(me.Login());
    CHECK(!me.Last("match").has_value());
}
