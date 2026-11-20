#include "cricket/online/Store.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cricket::online {

namespace {

std::string Crc(const std::string& s) {
    auto d = ctl::Sha256(s);
    return ctl::ToHex(std::string(d.begin(), d.begin() + 4));
}

std::string Clean(const std::string& s, size_t max) {
    // Display names: printable, trimmed, no control characters.
    std::string o;
    for (unsigned char c : s)
        if (c >= 0x20 && c != 0x7f) o += static_cast<char>(c);
    while (!o.empty() && o.back() == ' ') o.pop_back();
    while (!o.empty() && o.front() == ' ') o.erase(o.begin());
    if (o.size() > max) {
        o.resize(max);
        while (!o.empty() && (static_cast<unsigned char>(o.back()) & 0xC0) == 0x80) o.pop_back();  // whole UTF-8 chars
        if (!o.empty() && static_cast<unsigned char>(o.back()) >= 0xC0) o.pop_back();
    }
    return o;
}

std::string CleanCountry(const std::string& c) {
    std::string o;
    for (char ch : c)
        if (ch >= 'a' && ch <= 'z') o += static_cast<char>(ch - 32);
        else if (ch >= 'A' && ch <= 'Z') o += ch;
    return o.size() == 2 ? o : "";
}

Json SideJson(const SideResult& s) {
    Json j = Json::Object();
    j.Set("account", s.account).Set("team", s.team).Set("total", s.total).Set("wickets", s.wickets);
    j.Set("balls", s.legalBalls).Set("fours", s.fours).Set("sixes", s.sixes).Set("best", s.bestIndividual);
    j.Set("taken", s.wicketsTaken).Set("bbW", s.bestBowlingWickets).Set("bbR", s.bestBowlingRuns);
    return j;
}

SideResult SideFrom(const Json* j) {
    SideResult s;
    if (!j) return s;
    s.account = j->Str("account");
    s.team = j->Str("team");
    s.total = static_cast<int>(j->Int("total"));
    s.wickets = static_cast<int>(j->Int("wickets"));
    s.legalBalls = static_cast<int>(j->Int("balls"));
    s.fours = static_cast<int>(j->Int("fours"));
    s.sixes = static_cast<int>(j->Int("sixes"));
    s.bestIndividual = static_cast<int>(j->Int("best"));
    s.wicketsTaken = static_cast<int>(j->Int("taken"));
    s.bestBowlingWickets = static_cast<int>(j->Int("bbW"));
    s.bestBowlingRuns = static_cast<int>(j->Int("bbR"));
    return s;
}

}  // namespace

Json Store::ToJson(const Account& a) {
    Json j = Json::Object();
    j.Set("id", a.id).Set("name", a.name).Set("country", a.country).Set("tok", a.tokenHash);
    j.Set("created", a.createdMs).Set("rating", a.rating).Set("rated", a.ratedGames);
    return j;
}

std::optional<Account> Store::AccountFrom(const Json& j) {
    Account a;
    a.id = j.Str("id");
    a.name = j.Str("name");
    a.country = j.Str("country");
    a.tokenHash = j.Str("tok");
    a.createdMs = j.Int("created");
    a.rating = static_cast<int>(j.Int("rating", 1200));
    a.ratedGames = static_cast<int>(j.Int("rated"));
    if (a.id.empty() || a.tokenHash.size() != 64) return std::nullopt;
    return a;
}

Json Store::ToJson(const MatchRecord& m) {
    Json j = Json::Object();
    j.Set("id", m.id).Set("mode", m.mode).Set("format", m.format).Set("start", m.startedMs).Set("end", m.endedMs);
    j.Set("s0", SideJson(m.sides[0])).Set("s1", SideJson(m.sides[1])).Set("winner", m.winner);
    j.Set("outcome", m.outcome).Set("summary", m.summary).Set("d0", m.ratingDelta[0]).Set("d1", m.ratingDelta[1]);
    j.Set("save", m.saveFile);
    return j;
}

std::optional<MatchRecord> Store::MatchFrom(const Json& j) {
    MatchRecord m;
    m.id = j.Str("id");
    m.mode = j.Str("mode");
    m.format = j.Str("format");
    m.startedMs = j.Int("start");
    m.endedMs = j.Int("end");
    m.sides[0] = SideFrom(j.Get("s0"));
    m.sides[1] = SideFrom(j.Get("s1"));
    m.winner = static_cast<int>(j.Int("winner", -1));
    m.outcome = j.Str("outcome");
    m.summary = j.Str("summary");
    m.ratingDelta = {static_cast<int>(j.Int("d0")), static_cast<int>(j.Int("d1"))};
    m.saveFile = j.Str("save");
    if (m.id.empty() || (m.mode != "casual" && m.mode != "ranked")) return std::nullopt;
    return m;
}

Status Store::Open(const std::string& dir) {
    dir_ = dir;
    accounts_.clear();
    matches_.clear();
    skipped_ = 0;
    if (dir.empty()) return Status::Ok();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return Status::Err("cannot create " + dir + ": " + ec.message());
    auto load = [&](const std::string& file, auto onRecord) {
        std::ifstream f(dir + "/" + file);
        for (std::string line; std::getline(f, line);) {
            const size_t tab = line.rfind("\t#");
            if (tab == std::string::npos || line.substr(tab + 2) != Crc(line.substr(0, tab))) {
                ++skipped_;  // damaged (e.g. power cut mid-write): skip it
                continue;
            }
            auto j = Json::Parse(line.substr(0, tab));
            if (!j || !onRecord(*j)) ++skipped_;
        }
    };
    load("accounts.jsonl", [&](const Json& j) {
        auto a = AccountFrom(j);
        if (!a) return false;
        const auto old = accounts_.find(a->id);
        if (old != accounts_.end()) a->stats = old->second.stats;
        accounts_[a->id] = *a;  // the latest line for an account wins
        return true;
    });
    load("matches.jsonl", [&](const Json& j) {
        auto m = MatchFrom(j);
        if (!m) return false;
        matches_.push_back(*m);
        return true;
    });
    // Stats are derived from the match history (one source of truth).
    for (auto& [id, a] : accounts_) a.stats = {};
    for (const auto& m : matches_) ApplyMatch(m);
    return Status::Ok();
}

Status Store::Append(const std::string& file, const Json& j) {
    if (dir_.empty()) return Status::Ok();
    std::ofstream f(dir_ + "/" + file, std::ios::app);
    const std::string body = j.Dump();
    f << body << "\t#" << Crc(body) << "\n";
    f.flush();
    return f ? Status::Ok() : Status::Err("write failed: " + file);
}

std::pair<Account, std::string> Store::CreateGuest(const std::string& name, const std::string& country, int64_t now,
                                                   ctl::RandomSource& rnd) {
    Account a;
    do {
        a.id = "g" + ctl::Base64Url(rnd(9));
    } while (accounts_.count(a.id));
    const std::string token = ctl::Base64Url(rnd(32));
    auto h = ctl::Sha256(token);
    a.tokenHash = ctl::ToHex(std::string(h.begin(), h.end()));
    a.name = Clean(name, 24);
    if (a.name.empty()) a.name = "Guest " + a.id.substr(1, 4);
    a.country = CleanCountry(country);
    a.createdMs = now;
    accounts_[a.id] = a;
    Append("accounts.jsonl", ToJson(a));
    return {a, token};
}

const Account* Store::Verify(const std::string& id, const std::string& token) const {
    auto it = accounts_.find(id);
    auto h = ctl::Sha256(token);
    const std::string presented = ctl::ToHex(std::string(h.begin(), h.end()));
    if (it == accounts_.end()) {
        ctl::ConstantTimeEqual(presented, std::string(64, '0'));  // same work either way
        return nullptr;
    }
    return ctl::ConstantTimeEqual(presented, it->second.tokenHash) ? &it->second : nullptr;
}

const Account* Store::Find(const std::string& id) const {
    auto it = accounts_.find(id);
    return it == accounts_.end() ? nullptr : &it->second;
}

Status Store::UpdateProfile(const std::string& id, const std::string& name, const std::string& country) {
    auto it = accounts_.find(id);
    if (it == accounts_.end()) return Status::Err("no such player");
    const std::string n = Clean(name, 24);
    if (!n.empty()) it->second.name = n;
    if (!country.empty()) it->second.country = CleanCountry(country);
    return Append("accounts.jsonl", ToJson(it->second));
}

int Store::EloDelta(int rating, int opponent, double score, int ratedGames) {
    const double expected = 1.0 / (1.0 + std::pow(10.0, (opponent - rating) / 400.0));
    const double k = ratedGames < 10 ? 40.0 : 20.0;
    return static_cast<int>(std::lround(k * (score - expected)));
}

void Store::ApplyMatch(const MatchRecord& m) {
    for (int s = 0; s < 2; ++s) {
        auto it = accounts_.find(m.sides[s].account);
        if (it == accounts_.end()) continue;
        PlayerStats& st = it->second.stats;
        const SideResult& mine = m.sides[s];
        st.matches++;
        if (m.mode == "ranked") st.rankedMatches++;
        if (m.winner == s) {
            st.wins++;
            if (m.mode == "ranked") st.rankedWins++;
        } else if (m.winner == 1 - s) {
            st.losses++;
        } else if (m.outcome == "tie") {
            st.ties++;
        } else {
            st.noResults++;
        }
        st.runs += mine.total;
        st.wicketsTaken += mine.wicketsTaken;
        st.fours += mine.fours;
        st.sixes += mine.sixes;
        st.highestIndividual = std::max(st.highestIndividual, mine.bestIndividual);
        st.highestTeamTotal = std::max(st.highestTeamTotal, mine.total);
        if (mine.bestBowlingWickets > st.bestBowlingWickets ||
            (mine.bestBowlingWickets == st.bestBowlingWickets && mine.bestBowlingWickets > 0 &&
             mine.bestBowlingRuns < st.bestBowlingRuns)) {
            st.bestBowlingWickets = mine.bestBowlingWickets;
            st.bestBowlingRuns = mine.bestBowlingRuns;
        }
    }
}

MatchRecord Store::RecordMatch(MatchRecord rec) {
    rec.ratingDelta = {0, 0};
    Account* a = accounts_.count(rec.sides[0].account) ? &accounts_[rec.sides[0].account] : nullptr;
    Account* b = accounts_.count(rec.sides[1].account) ? &accounts_[rec.sides[1].account] : nullptr;
    if (rec.mode == "ranked" && a && b && rec.outcome != "no-result") {
        // Win-trading guard: the same two players, ranked, 24 hours.
        int recent = 0;
        for (const auto& m : matches_)
            if (m.mode == "ranked" && rec.endedMs - m.endedMs < 24LL * 3600 * 1000 &&
                ((m.sides[0].account == a->id && m.sides[1].account == b->id) ||
                 (m.sides[0].account == b->id && m.sides[1].account == a->id)))
                ++recent;
        if (recent < 3) {
            const double sa = rec.winner == 0 ? 1.0 : rec.winner == 1 ? 0.0 : 0.5;
            rec.ratingDelta[0] = EloDelta(a->rating, b->rating, sa, a->ratedGames);
            rec.ratingDelta[1] = EloDelta(b->rating, a->rating, 1.0 - sa, b->ratedGames);
            a->rating += rec.ratingDelta[0];
            b->rating += rec.ratingDelta[1];
        }
        a->ratedGames++;
        b->ratedGames++;
        Append("accounts.jsonl", ToJson(*a));
        Append("accounts.jsonl", ToJson(*b));
    }
    matches_.push_back(rec);
    ApplyMatch(rec);
    Append("matches.jsonl", ToJson(rec));
    return rec;
}

std::vector<MatchRecord> Store::History(const std::string& account, size_t limit) const {
    std::vector<MatchRecord> out;
    for (auto it = matches_.rbegin(); it != matches_.rend() && out.size() < limit; ++it)
        if (it->sides[0].account == account || it->sides[1].account == account) out.push_back(*it);
    return out;
}

const MatchRecord* Store::FindMatch(const std::string& id) const {
    for (const auto& m : matches_)
        if (m.id == id) return &m;
    return nullptr;
}

Json Store::StatsJson(const std::string& id) const {
    const Account* a = Find(id);
    Json j = Json::Object();
    if (!a) return j;
    const PlayerStats& s = a->stats;
    j.Set("id", a->id).Set("name", a->name).Set("country", a->country).Set("rating", a->rating);
    j.Set("matches", s.matches).Set("wins", s.wins).Set("losses", s.losses).Set("ties", s.ties);
    j.Set("noResults", s.noResults).Set("rankedMatches", s.rankedMatches).Set("rankedWins", s.rankedWins);
    j.Set("winPct", s.matches ? std::round(1000.0 * s.wins / s.matches) / 10 : 0.0);
    j.Set("runs", s.runs).Set("wicketsTaken", s.wicketsTaken).Set("fours", s.fours).Set("sixes", s.sixes);
    j.Set("highestIndividual", s.highestIndividual).Set("highestTeamTotal", s.highestTeamTotal);
    j.Set("bestBowling", std::to_string(s.bestBowlingWickets) + "/" + std::to_string(s.bestBowlingRuns));
    return j;
}

Json Store::Leaderboard(const std::string& board, const std::string& period, const std::string& mode,
                        const std::string& country, int64_t now, size_t limit) const {
    const int64_t day = 24LL * 3600 * 1000;
    const int64_t since = period == "week" ? now - 7 * day : period == "month" ? now - 30 * day : INT64_MIN;
    struct Row {
        std::string id;
        double value = 0;
        int matches = 0, wins = 0;
    };
    std::map<std::string, Row> rows;
    for (const auto& m : matches_) {
        if (m.endedMs < since) continue;
        if (mode != "all" && m.mode != mode) continue;
        for (int s = 0; s < 2; ++s) {
            const SideResult& r = m.sides[s];
            if (r.account.empty() || !accounts_.count(r.account)) continue;
            Row& row = rows[r.account];
            row.id = r.account;
            row.matches++;
            if (m.winner == s) row.wins++;
            if (board == "runs") row.value += r.total;
            else if (board == "wickets") row.value += r.wicketsTaken;
            else if (board == "sixes") row.value += r.sixes;
            else if (board == "fours") row.value += r.fours;
            else if (board == "highscore") row.value = std::max(row.value, static_cast<double>(r.bestIndividual));
        }
    }
    std::vector<Row> list;
    for (auto& [id, row] : rows) {
        const Account& a = accounts_.at(id);
        if (!country.empty() && a.country != country) continue;
        if (board == "rating") {
            if (a.ratedGames == 0) continue;
            row.value = a.rating;
        } else if (board == "wins") {
            row.value = row.wins;
        } else if (board == "winpct") {
            if (row.matches < 5) continue;
            row.value = std::round(1000.0 * row.wins / row.matches) / 10;
        } else if (board == "matches") {
            row.value = row.matches;
        }
        list.push_back(row);
    }
    std::stable_sort(list.begin(), list.end(), [](const Row& x, const Row& y) {
        return x.value != y.value ? x.value > y.value : x.id < y.id;
    });
    Json out = Json::Object();
    out.Set("board", board).Set("period", period).Set("mode", mode).Set("country", country);
    Json arr = Json::Array();
    for (size_t i = 0; i < list.size() && i < limit; ++i) {
        const Account& a = accounts_.at(list[i].id);
        Json r = Json::Object();
        r.Set("rank", static_cast<int>(i + 1)).Set("id", a.id).Set("name", a.name).Set("country", a.country);
        r.Set("value", list[i].value).Set("matches", list[i].matches);
        arr.Push(r);
    }
    out.Set("rows", arr);
    return out;
}

}  // namespace cricket::online
