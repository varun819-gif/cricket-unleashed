#include "cricket/SaveGame.h"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace cricket {

uint64_t Fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;  // FNV-1a 64-bit offset basis
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

namespace {

constexpr const char* kMagic = "CRICKET-UNLEASHED-SAVE";
constexpr const char* kJournalMagic = "CU-JOURNAL";

std::string Hex(uint64_t v, int digits = 16) {
    char b[24];
    std::snprintf(b, sizeof b, "%0*" PRIx64, digits, v);
    return b;
}

// Strings: percent-encode everything except [A-Za-z0-9._~]; "-" = empty.
std::string Enc(const std::string& s) {
    if (s.empty()) return "-";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
            c == '~') {
            o += static_cast<char>(c);
        } else {
            char b[4];
            std::snprintf(b, sizeof b, "%%%02X", c);
            o += b;
        }
    }
    return o;
}

bool Dec(const std::string& t, std::string* out) {
    out->clear();
    if (t == "-") return true;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] != '%') {
            *out += t[i];
            continue;
        }
        if (i + 2 >= t.size()) return false;
        char h[3] = {t[i + 1], t[i + 2], 0};
        char* end = nullptr;
        long v = std::strtol(h, &end, 16);
        if (*end != 0) return false;
        *out += static_cast<char>(v);
        i += 2;
    }
    return true;
}

// ---- writer / reader -------------------------------------------------------

class W {
public:
    explicit W(const char* keyword) : s_(keyword) {}
    W& I(long long v) {
        s_ += ' ';
        s_ += std::to_string(v);
        return *this;
    }
    W& U(uint64_t v) {
        s_ += ' ';
        s_ += std::to_string(v);
        return *this;
    }
    W& B(bool v) { return I(v ? 1 : 0); }
    W& D(double v) {
        char b[40];
        std::snprintf(b, sizeof b, "%.17g", v);
        s_ += ' ';
        s_ += b;
        return *this;
    }
    W& S(const std::string& v) {
        s_ += ' ';
        s_ += Enc(v);
        return *this;
    }
    const std::string& str() const { return s_; }

private:
    std::string s_;
};

class R {
public:
    explicit R(const std::string& line) {
        std::istringstream in(line);
        for (std::string t; in >> t;) t_.push_back(t);
    }
    const std::string Keyword() const { return t_.empty() ? std::string() : t_[0]; }
    long long I(long long lo, long long hi) {
        const std::string* t = Next();
        if (!t) return lo;
        errno = 0;
        char* end = nullptr;
        long long v = std::strtoll(t->c_str(), &end, 10);
        if (errno || *end || v < lo || v > hi) Fail("bad number '" + *t + "'");
        return ok_ ? v : lo;
    }
    uint64_t U() {
        const std::string* t = Next();
        if (!t) return 0;
        errno = 0;
        char* end = nullptr;
        unsigned long long v = std::strtoull(t->c_str(), &end, 10);
        if (errno || *end || (!t->empty() && (*t)[0] == '-')) Fail("bad number '" + *t + "'");
        return ok_ ? v : 0;
    }
    bool B() { return I(0, 1) == 1; }
    double D() {
        const std::string* t = Next();
        if (!t) return 0;
        char* end = nullptr;
        double v = std::strtod(t->c_str(), &end);
        if (*end) Fail("bad number '" + *t + "'");
        return v;
    }
    std::string S() {
        const std::string* t = Next();
        std::string out;
        if (t && !Dec(*t, &out)) Fail("bad text '" + *t + "'");
        return out;
    }
    template <class E>
    E Enum(int max) {
        return static_cast<E>(I(0, max));
    }
    bool Ok() const { return ok_; }
    bool Finished() const { return ok_ && i_ == t_.size(); }
    const std::string& Error() const { return err_; }
    void Fail(const std::string& e) {
        if (ok_) err_ = e;
        ok_ = false;
    }

private:
    const std::string* Next() {
        if (!ok_) return nullptr;
        if (i_ >= t_.size()) {
            Fail("missing value");
            return nullptr;
        }
        return &t_[i_++];
    }
    std::vector<std::string> t_;
    size_t i_ = 1;
    bool ok_ = true;
    std::string err_;
};

// ---- value codecs ------------------------------------------------------------

void Put(W& w, const MatchRules& r) {
    w.I(static_cast<int>(r.format)).I(r.oversPerInnings).I(r.ballsPerOver).I(r.maxOversPerBowler).I(r.inningsPerSide);
    w.B(r.freeHitOnNoBall).I(r.followOnLead).I(r.matchOverLimit).I(r.widePenalty).I(r.noBallPenalty).I(r.squadSize);
    w.I(r.defaultMaxOutsideCircle).I(r.newBallAfterOvers).I(static_cast<long long>(r.phases.size()));
    for (const auto& p : r.phases) w.I(p.firstOver).I(p.lastOver).I(p.maxOutsideCircle);
    const DrsRules& d = r.drs;
    w.B(d.playerReviews).B(d.thirdUmpire).I(d.reviewsPerInnings).B(d.umpiresCallRetainsReview).I(d.reviewTimerSeconds);
    w.B(d.reviewWidesAndNoBalls).B(d.reviewRunOutsAndStumpings);
    const SessionSchedule& s = r.schedule;
    w.B(s.enabled).I(s.days).I(s.oversPerDay).I(s.sessionsPerDay).I(s.oversLostPerInningsChange).I(s.maxOversPerDay);
    w.I(s.intervalNearOvers).B(s.dayNight);
    const WeatherRules& x = r.weather;
    w.I(x.minOversForResult).D(x.minutesPerOver).I(x.bufferMinutes).D(x.fullInningsScore);
}

void Get(R& in, MatchRules& r, int version) {
    r.format = in.Enum<Format>(3);
    r.oversPerInnings = static_cast<int>(in.I(0, 1000));
    r.ballsPerOver = static_cast<int>(in.I(1, 10));
    r.maxOversPerBowler = static_cast<int>(in.I(0, 1000));
    r.inningsPerSide = static_cast<int>(in.I(1, 2));
    r.freeHitOnNoBall = in.B();
    r.followOnLead = static_cast<int>(in.I(0, 10000));
    r.matchOverLimit = static_cast<int>(in.I(0, 100000));
    r.widePenalty = static_cast<int>(in.I(0, 10));
    r.noBallPenalty = static_cast<int>(in.I(0, 10));
    r.squadSize = static_cast<int>(in.I(2, 15));
    r.defaultMaxOutsideCircle = static_cast<int>(in.I(-1, 11));
    r.newBallAfterOvers = static_cast<int>(in.I(0, 1000));
    const int phases = static_cast<int>(in.I(0, 20));
    r.phases.clear();
    for (int i = 0; i < phases && in.Ok(); ++i) {
        FieldingPhase p;
        p.firstOver = static_cast<int>(in.I(1, 1000));
        p.lastOver = static_cast<int>(in.I(1, 1000));
        p.maxOutsideCircle = static_cast<int>(in.I(-1, 11));
        r.phases.push_back(p);
    }
    DrsRules& d = r.drs;
    d.playerReviews = in.B();
    d.thirdUmpire = in.B();
    d.reviewsPerInnings = static_cast<int>(in.I(0, 100));
    d.umpiresCallRetainsReview = in.B();
    d.reviewTimerSeconds = static_cast<int>(in.I(0, 3600));
    d.reviewWidesAndNoBalls = in.B();
    d.reviewRunOutsAndStumpings = version >= 2 ? in.B() : false;
    SessionSchedule& s = r.schedule;
    s.enabled = in.B();
    s.days = static_cast<int>(in.I(1, 10));
    s.oversPerDay = static_cast<int>(in.I(1, 1000));
    s.sessionsPerDay = static_cast<int>(in.I(1, 10));
    s.oversLostPerInningsChange = static_cast<int>(in.I(0, 100));
    s.maxOversPerDay = static_cast<int>(in.I(1, 1000));
    s.intervalNearOvers = static_cast<int>(in.I(0, 100));
    s.dayNight = in.B();
    WeatherRules& x = r.weather;
    x.minOversForResult = static_cast<int>(in.I(0, 1000));
    x.minutesPerOver = in.D();
    x.bufferMinutes = static_cast<int>(in.I(0, 100000));
    x.fullInningsScore = in.D();
    if (in.Ok() && !(x.minutesPerOver > 0 && x.minutesPerOver < 60)) in.Fail("bad minutes per over");
}

void Put(W& w, const Delivery& d) {
    w.I(static_cast<int>(d.extra)).I(d.runs).B(d.boundary).I(static_cast<int>(d.dismissal)).B(d.strikerOut);
    w.I(d.fielder).I(static_cast<int>(d.noBallRunsAs)).I(d.shortRuns);
}

void Get(R& in, Delivery& d) {
    d.extra = in.Enum<ExtraType>(4);
    d.runs = static_cast<int>(in.I(-100, 100));
    d.boundary = in.B();
    d.dismissal = in.Enum<Dismissal>(10);
    d.strikerOut = in.B();
    d.fielder = static_cast<int>(in.I(-1, 100));
    d.noBallRunsAs = in.Enum<ExtraType>(4);
    d.shortRuns = static_cast<int>(in.I(-100, 100));
}

void Put(W& w, const DecisionCase& c) {
    w.I(static_cast<int>(c.kind)).I(static_cast<int>(c.mode)).B(c.onFieldOut);
    Put(w, c.ifOut);
    Put(w, c.ifNotOut);
    w.I(static_cast<int>(c.call)).B(c.onFieldCalled);
    Put(w, c.ifCalled);
    Put(w, c.ifNotCalled);
    const DrsEvidence& e = c.evidence;
    w.B(e.frontFootNoBall).I(static_cast<int>(e.batContact)).I(static_cast<int>(e.groundedBeforeCatch));
    const LbwEvidence l = e.lbw ? *e.lbw : LbwEvidence{};
    w.B(e.lbw.has_value()).B(l.hitBatFirst).I(static_cast<int>(l.pitched)).I(static_cast<int>(l.impact));
    w.B(l.impactUmpiresCall).B(l.shotOffered).I(static_cast<int>(l.wickets));
    w.I(static_cast<int>(e.batterGrounded)).I(static_cast<int>(e.wicketBrokenByBatter));
    w.I(static_cast<int>(e.wilfulObstruction)).I(static_cast<int>(e.callTrue));
    w.D(c.perceived[0]).D(c.perceived[1]);
}

void Get(R& in, DecisionCase& c) {
    c.kind = in.Enum<DecisionKind>(1);
    c.mode = in.Enum<Dismissal>(10);
    c.onFieldOut = in.B();
    Get(in, c.ifOut);
    Get(in, c.ifNotOut);
    c.call = in.Enum<ScoringCall>(3);
    c.onFieldCalled = in.B();
    Get(in, c.ifCalled);
    Get(in, c.ifNotCalled);
    DrsEvidence& e = c.evidence;
    e.frontFootNoBall = in.B();
    e.batContact = in.Enum<Evidence>(2);
    e.groundedBeforeCatch = in.Enum<Evidence>(2);
    const bool hasLbw = in.B();
    LbwEvidence l;
    l.hitBatFirst = in.B();
    l.pitched = in.Enum<PitchZone>(3);
    l.impact = in.Enum<ImpactZone>(2);
    l.impactUmpiresCall = in.B();
    l.shotOffered = in.B();
    l.wickets = in.Enum<WicketsZone>(2);
    if (hasLbw) e.lbw = l;
    else e.lbw.reset();
    e.batterGrounded = in.Enum<Evidence>(2);
    e.wicketBrokenByBatter = in.Enum<Evidence>(2);
    e.wilfulObstruction = in.Enum<Evidence>(2);
    e.callTrue = in.Enum<Evidence>(2);
    c.perceived[0] = in.D();
    c.perceived[1] = in.D();
}

void Put(W& w, const Command& c) {
    w.S(c.player).U(c.seq).I(static_cast<int>(c.type)).I(c.a).I(c.b);
    w.I(static_cast<long long>(c.field.fielders.size()));
    for (const auto& f : c.field.fielders) w.S(f.name).D(f.pos.x).D(f.pos.y);
}

void Get(R& in, Command& c) {
    c.player = in.S();
    c.seq = in.U();
    c.type = in.Enum<CommandType>(static_cast<int>(CommandType::ResumePlay));
    c.a = static_cast<int>(in.I(-1000000, 1000000));
    c.b = static_cast<int>(in.I(-1000000, 1000000));
    const int n = static_cast<int>(in.I(0, 30));
    c.field.fielders.clear();
    for (int i = 0; i < n && in.Ok(); ++i) {
        FieldPosition f;
        f.name = in.S();
        f.pos.x = in.D();
        f.pos.y = in.D();
        c.field.fielders.push_back(f);
    }
}

std::string ActionLine(const MatchAction& a) {
    W w("act");
    w.I(static_cast<int>(a.type)).I(a.a).I(a.b).S(a.text);
    Put(w, a.delivery);
    Put(w, a.decision);
    return w.str();
}

bool ParseAction(const std::string& line, MatchAction* a, std::string* err) {
    R in(line);
    if (in.Keyword() != "act") {
        *err = "expected an action";
        return false;
    }
    a->type = in.Enum<ActionType>(static_cast<int>(ActionType::LoseDay));
    a->a = static_cast<int>(in.I(-1000000, 1000000));
    a->b = static_cast<int>(in.I(-1000000, 1000000));
    a->text = in.S();
    Get(in, a->delivery);
    Get(in, a->decision);
    if (!in.Finished()) {
        *err = in.Ok() ? "unexpected extra values" : in.Error();
        return false;
    }
    return true;
}

std::string InputLine(const SessionInput& x) {
    W w("in");
    w.B(x.ok).I(static_cast<int>(x.type)).S(x.text).I(x.a).I(x.b).I(x.ms);
    Put(w, x.command);
    Put(w, x.delivery);
    Put(w, x.decision);
    return w.str();
}

bool ParseInput(const std::string& line, SessionInput* x, std::string* err) {
    R in(line);
    if (in.Keyword() != "in") {
        *err = "expected a session input";
        return false;
    }
    x->ok = in.B();
    x->type = in.Enum<InputType>(static_cast<int>(InputType::Release));
    x->text = in.S();
    x->a = static_cast<int>(in.I(-1000000, 1000000));
    x->b = static_cast<int>(in.I(-1000000, 1000000));
    x->ms = in.I(INT64_MIN + 1, INT64_MAX);
    Get(in, x->command);
    Get(in, x->delivery);
    Get(in, x->decision);
    if (!in.Finished()) {
        *err = in.Ok() ? "unexpected extra values" : in.Error();
        return false;
    }
    return true;
}

// Setup lines: rules, teams, then "start <battingFirst>" or "seed <n>".
std::vector<std::string> SetupLines(const MatchRules& rules, const std::array<Team, 2>& teams) {
    std::vector<std::string> out;
    W r("rules");
    Put(r, rules);
    out.push_back(r.str());
    for (int side = 0; side < 2; ++side) {
        const Team& t = teams[side];
        out.push_back(W("team").I(side).S(t.name).I(t.captain).I(t.keeper).I(static_cast<long long>(t.xi.size())).str());
        for (const Player& p : t.xi) {
            W w("player");
            w.S(p.name).I(static_cast<int>(p.bats)).I(static_cast<int>(p.bowls)).B(p.canBowl);
            w.I(p.batting).I(p.bowling).I(p.fielding).I(p.fitness).I(p.form);
            out.push_back(w.str());
        }
    }
    return out;
}

// Parses rules + teams starting at lines[*i]; advances *i.
bool ParseSetup(const std::vector<std::string>& lines, size_t* i, MatchRules* rules, std::array<Team, 2>* teams,
                std::string* err, int version) {
    auto fail = [&](const std::string& e) {
        *err = "line " + std::to_string(*i + 1) + ": " + e;
        return false;
    };
    if (*i >= lines.size()) return fail("missing rules");
    {
        R in(lines[*i]);
        if (in.Keyword() != "rules") return fail("expected rules");
        Get(in, *rules, version);
        if (!in.Finished()) return fail(in.Ok() ? "unexpected extra values" : in.Error());
        ++*i;
    }
    for (int side = 0; side < 2; ++side) {
        if (*i >= lines.size()) return fail("missing team");
        R in(lines[*i]);
        if (in.Keyword() != "team") return fail("expected team");
        if (in.I(0, 1) != side) return fail("teams out of order");
        Team& t = (*teams)[side];
        t.name = in.S();
        t.captain = static_cast<int>(in.I(0, 14));
        t.keeper = static_cast<int>(in.I(0, 14));
        const int n = static_cast<int>(in.I(1, 15));
        if (!in.Finished()) return fail(in.Ok() ? "unexpected extra values" : in.Error());
        ++*i;
        t.xi.clear();
        for (int k = 0; k < n; ++k) {
            if (*i >= lines.size()) return fail("missing player");
            R p(lines[*i]);
            if (p.Keyword() != "player") return fail("expected player");
            Player pl;
            pl.name = p.S();
            pl.bats = p.Enum<Handedness>(1);
            pl.bowls = p.Enum<BowlingKind>(1);
            pl.canBowl = p.B();
            pl.batting = static_cast<int>(p.I(0, 100));
            pl.bowling = static_cast<int>(p.I(0, 100));
            pl.fielding = static_cast<int>(p.I(0, 100));
            pl.fitness = static_cast<int>(p.I(0, 100));
            pl.form = static_cast<int>(p.I(0, 100));
            if (!p.Finished()) return fail(p.Ok() ? "unexpected extra values" : p.Error());
            t.xi.push_back(pl);
            ++*i;
        }
    }
    return true;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string Finish(std::string body) {
    return body + "checksum " + Hex(Fnv1a64(body)) + "\n";
}

// Checks header + checksum; returns the lines without the checksum line.
bool CheckFile(const std::string& text, const std::string& kind, std::vector<std::string>* lines, std::string* err,
               int* versionOut) {
    const size_t pos = text.rfind("checksum ");
    if (pos == std::string::npos || (pos > 0 && text[pos - 1] != '\n')) {
        *err = "not a complete save file (no checksum)";
        return false;
    }
    std::string stored = text.substr(pos + 9);
    while (!stored.empty() && (stored.back() == '\n' || stored.back() == '\r')) stored.pop_back();
    const std::string body = text.substr(0, pos);
    if (stored != Hex(Fnv1a64(body))) {
        *err = "checksum mismatch: the file is damaged or was edited";
        return false;
    }
    *lines = SplitLines(body);
    if (lines->empty()) {
        *err = "empty save";
        return false;
    }
    R h((*lines)[0]);
    if (h.Keyword() != kMagic) {
        *err = "not a Cricket Unleashed save";
        return false;
    }
    const std::string k = h.S();
    const long long version = h.I(0, 1000000);
    if (!h.Finished()) {
        *err = "bad header";
        return false;
    }
    if (k != kind) {
        *err = "this is a " + k + " save, not a " + kind + " save";
        return false;
    }
    if (version > kSaveFormatVersion) {
        *err = "made by a newer version of the game (format " + std::to_string(version) + ")";
        return false;
    }
    if (version < 1) {
        *err = "unknown save format version";
        return false;
    }
    *versionOut = static_cast<int>(version);
    return true;
}

bool ParseEnd(const std::string& line, size_t* steps, std::string* hash) {
    R in(line);
    if (in.Keyword() != "end") return false;
    *steps = static_cast<size_t>(in.I(0, 100000000));
    std::string h = in.S();
    *hash = h;
    return in.Finished();
}

std::string ReadAll(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    *ok = f.good();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---- journal lines: "<payload>\t#<crc32>" ------------------------------------

std::string Crc(const std::string& payload) { return Hex(Fnv1a64(payload) & 0xffffffffULL, 8); }

std::string JournalLine(const std::string& payload) { return payload + "\t#" + Crc(payload) + "\n"; }

// Valid payloads up to the first damaged line; counts the rest.
std::vector<std::string> ReadJournal(const std::string& path, size_t* discarded, bool* opened) {
    bool ok = false;
    const std::string text = ReadAll(path, &ok);
    *opened = ok;
    std::vector<std::string> good;
    *discarded = 0;
    // A final line without '\n' was cut off mid-write: never trust it.
    std::vector<std::string> lines = SplitLines(text);
    const bool lastComplete = !text.empty() && text.back() == '\n';
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        const size_t tab = l.rfind("\t#");
        const bool intact = tab != std::string::npos && (i + 1 < lines.size() || lastComplete) &&
                            l.substr(tab + 2) == Crc(l.substr(0, tab));
        if (!intact) {
            *discarded = lines.size() - i;
            break;
        }
        good.push_back(l.substr(0, tab));
    }
    return good;
}

Status StartJournal(std::ofstream& out, const std::string& path, const std::string& kind,
                    const std::vector<std::string>& setup) {
    out.close();
    out.open(path, std::ios::binary | std::ios::trunc);
    if (!out) return Status::Err("cannot open " + path);
    out << JournalLine(W(kJournalMagic).S(kind).I(kSaveFormatVersion).str());
    for (const auto& l : setup) out << JournalLine(l);
    out << JournalLine("begin");
    out.flush();
    return out ? Status::Ok() : Status::Err("write failed");
}

}  // namespace

// ---------------------------------------------------------------------------
// Match saves

std::string SaveMatch(const Match& m) {
    std::string body = W(kMagic).S("match").I(kSaveFormatVersion).str() + "\n";
    for (const auto& l : SetupLines(m.Rules(), {m.GetTeam(0), m.GetTeam(1)})) body += l + "\n";
    body += W("start").I(m.BattingFirst()).str() + "\n";
    for (const auto& a : m.Journal()) body += ActionLine(a) + "\n";
    body += W("end").U(m.Journal().size()).S(Hex(Fnv1a64(m.StateFingerprint()))).str() + "\n";
    return Finish(body);
}

namespace {

// Builds a match from setup lines (starting at *i) and replays action lines.
MatchLoad BuildMatch(const std::vector<std::string>& lines, size_t i, bool requireEnd, int version) {
    MatchLoad out;
    MatchRules rules;
    std::array<Team, 2> teams;
    std::string err;
    if (!ParseSetup(lines, &i, &rules, &teams, &err, version)) {
        out.status = Status::Err(err);
        return out;
    }
    if (i >= lines.size()) {
        out.status = Status::Err("missing start line");
        return out;
    }
    R st(lines[i]);
    const int battingFirst = static_cast<int>(st.I(0, 1));
    if (st.Keyword() != "start" || !st.Finished()) {
        out.status = Status::Err("line " + std::to_string(i + 1) + ": expected start");
        return out;
    }
    ++i;
    if (i < lines.size() && lines[i] == "begin") ++i;  // journal marker
    if (Status v = Match::ValidateSetup(rules, teams, battingFirst); !v) {
        out.status = Status::Err("invalid match setup: " + v.error);
        return out;
    }
    out.match = std::make_unique<Match>(rules, teams, battingFirst);
    for (; i < lines.size(); ++i) {
        if (lines[i].rfind("end", 0) == 0) break;
        MatchAction a;
        if (!ParseAction(lines[i], &a, &err)) {
            out.status = Status::Err("line " + std::to_string(i + 1) + ": " + err);
            return out;
        }
        if (Status s = out.match->Apply(a); !s) {
            out.status = Status::Err("line " + std::to_string(i + 1) + ": action could not be replayed: " + s.error);
            return out;
        }
        out.steps++;
    }
    if (requireEnd) {
        size_t steps = 0;
        std::string hash;
        if (i >= lines.size() || !ParseEnd(lines[i], &steps, &hash)) {
            out.status = Status::Err("missing end line");
            return out;
        }
        if (steps != out.steps) {
            out.status = Status::Err("expected " + std::to_string(steps) + " actions, found " + std::to_string(out.steps));
            return out;
        }
        if (hash != Hex(Fnv1a64(out.match->StateFingerprint()))) {
            out.status = Status::Err("replayed state does not match the saved state");
            return out;
        }
        if (i + 1 != lines.size()) {
            out.status = Status::Err("unexpected lines after end");
            return out;
        }
    }
    out.status = Status::Ok();
    return out;
}

SessionLoad BuildSession(const std::vector<std::string>& lines, size_t i, bool requireEnd, bool stopOnError,
                         size_t* stoppedAt, int version) {
    SessionLoad out;
    MatchRules rules;
    std::array<Team, 2> teams;
    std::string err;
    if (!ParseSetup(lines, &i, &rules, &teams, &err, version)) {
        out.status = Status::Err(err);
        return out;
    }
    if (i >= lines.size()) {
        out.status = Status::Err("missing seed line");
        return out;
    }
    R sd(lines[i]);
    const uint64_t seed = sd.U();
    if (sd.Keyword() != "seed" || !sd.Finished()) {
        out.status = Status::Err("line " + std::to_string(i + 1) + ": expected seed");
        return out;
    }
    ++i;
    if (i < lines.size() && lines[i] == "begin") ++i;
    if (Status v = Match::ValidateSetup(rules, teams, 0); !v) {
        out.status = Status::Err("invalid session setup: " + v.error);
        return out;
    }
    out.session = std::make_unique<MatchSession>(rules, teams, seed);
    for (; i < lines.size(); ++i) {
        if (lines[i].rfind("end", 0) == 0) break;
        SessionInput x;
        std::string why;
        bool good = ParseInput(lines[i], &x, &why);
        if (good) {
            Status s = out.session->Replay(x);
            if (s.ok != x.ok) {
                good = false;
                why = std::string("replay diverged (expected ") + (x.ok ? "accepted" : "refused") + ", got " +
                      (s.ok ? "accepted" : "refused: " + s.error) + ")";
            }
        }
        if (!good) {
            if (stopOnError) {
                if (stoppedAt) *stoppedAt = i;
                break;
            }
            out.status = Status::Err("line " + std::to_string(i + 1) + ": " + why);
            return out;
        }
        out.steps++;
    }
    if (requireEnd) {
        size_t steps = 0;
        std::string hash;
        if (i >= lines.size() || !ParseEnd(lines[i], &steps, &hash)) {
            out.status = Status::Err("missing end line");
            return out;
        }
        if (steps != out.steps) {
            out.status = Status::Err("expected " + std::to_string(steps) + " inputs, found " + std::to_string(out.steps));
            return out;
        }
        if (hash != Hex(Fnv1a64(out.session->StateFingerprint()))) {
            out.status = Status::Err("replayed state does not match the saved state");
            return out;
        }
        if (i + 1 != lines.size()) {
            out.status = Status::Err("unexpected lines after end");
            return out;
        }
    }
    out.status = Status::Ok();
    return out;
}

}  // namespace

MatchLoad LoadMatch(const std::string& text) {
    std::vector<std::string> lines;
    std::string err;
    int version = 0;
    if (!CheckFile(text, "match", &lines, &err, &version)) {
        MatchLoad out;
        out.status = Status::Err(err);
        return out;
    }
    MatchLoad out = BuildMatch(lines, 1, true, version);
    if (!out.status) out.match.reset();
    return out;
}

std::string SaveSession(const MatchSession& s) {
    std::string body = W(kMagic).S("session").I(kSaveFormatVersion).str() + "\n";
    for (const auto& l : SetupLines(s.Rules(), s.Teams())) body += l + "\n";
    body += W("seed").U(s.Seed()).str() + "\n";
    for (const auto& x : s.Inputs()) body += InputLine(x) + "\n";
    body += W("end").U(s.Inputs().size()).S(Hex(Fnv1a64(s.StateFingerprint()))).str() + "\n";
    return Finish(body);
}

SessionLoad LoadSession(const std::string& text) {
    std::vector<std::string> lines;
    std::string err;
    int version = 0;
    if (!CheckFile(text, "session", &lines, &err, &version)) {
        SessionLoad out;
        out.status = Status::Err(err);
        return out;
    }
    SessionLoad out = BuildSession(lines, 1, true, false, nullptr, version);
    if (!out.status) out.session.reset();
    return out;
}

// ---------------------------------------------------------------------------
// Files

Status SaveFileAtomic(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return Status::Err("cannot write " + tmp);
        f << content;
        f.flush();
        if (!f) return Status::Err("write failed for " + tmp);
    }
    std::error_code ec;
    if (fs::exists(path, ec)) {
        fs::rename(path, path + ".bak", ec);  // keep the previous good save
        if (ec) return Status::Err("cannot keep backup: " + ec.message());
    }
    fs::rename(tmp, path, ec);
    if (ec) return Status::Err("cannot replace " + path + ": " + ec.message());
    return Status::Ok();
}

Status SaveMatchFile(const Match& m, const std::string& path) { return SaveFileAtomic(path, SaveMatch(m)); }
Status SaveSessionFile(const MatchSession& s, const std::string& path) { return SaveFileAtomic(path, SaveSession(s)); }

MatchLoad LoadMatchFile(const std::string& path) {
    bool ok = false;
    std::string text = ReadAll(path, &ok);
    MatchLoad primary = ok ? LoadMatch(text) : MatchLoad{};
    if (!ok) primary.status = Status::Err("cannot open " + path);
    if (primary.status) return primary;
    bool okBak = false;
    std::string bak = ReadAll(path + ".bak", &okBak);
    if (!okBak) return primary;
    MatchLoad backup = LoadMatch(bak);
    if (!backup.status) return primary;
    backup.fromBackup = true;
    backup.primaryError = primary.status.error;
    return backup;
}

SessionLoad LoadSessionFile(const std::string& path) {
    bool ok = false;
    std::string text = ReadAll(path, &ok);
    SessionLoad primary = ok ? LoadSession(text) : SessionLoad{};
    if (!ok) primary.status = Status::Err("cannot open " + path);
    if (primary.status) return primary;
    bool okBak = false;
    std::string bak = ReadAll(path + ".bak", &okBak);
    if (!okBak) return primary;
    SessionLoad backup = LoadSession(bak);
    if (!backup.status) return primary;
    backup.fromBackup = true;
    backup.primaryError = primary.status.error;
    return backup;
}

// ---------------------------------------------------------------------------
// Crash logs

Status MatchJournal::Start(const std::string& path, const Match& m) {
    auto setup = SetupLines(m.Rules(), {m.GetTeam(0), m.GetTeam(1)});
    setup.push_back(W("start").I(m.BattingFirst()).str());
    written_ = 0;
    if (Status s = StartJournal(out_, path, "match", setup); !s) return s;
    return Sync(m);  // anything already played
}

Status MatchJournal::Sync(const Match& m) {
    if (!out_.is_open()) return Status::Err("journal not started");
    const auto& j = m.Journal();
    if (j.size() < written_) return Status::Err("journal belongs to a different match");
    for (; written_ < j.size(); ++written_) out_ << JournalLine(ActionLine(j[written_]));
    out_.flush();
    return out_ ? Status::Ok() : Status::Err("write failed");
}

Status SessionJournal::Start(const std::string& path, const MatchSession& s) {
    auto setup = SetupLines(s.Rules(), s.Teams());
    setup.push_back(W("seed").U(s.Seed()).str());
    written_ = 0;
    if (Status st = StartJournal(out_, path, "session", setup); !st) return st;
    return Sync(s);
}

Status SessionJournal::Sync(const MatchSession& s) {
    if (!out_.is_open()) return Status::Err("journal not started");
    const auto& in = s.Inputs();
    if (in.size() < written_) return Status::Err("journal belongs to a different session");
    for (; written_ < in.size(); ++written_) out_ << JournalLine(InputLine(in[written_]));
    out_.flush();
    return out_ ? Status::Ok() : Status::Err("write failed");
}

namespace {
// Shared front half of recovery: header check, returns payloads and the
// index of the first entry after "begin".
bool OpenJournal(const std::string& path, const std::string& kind, std::vector<std::string>* lines, size_t* first,
                 size_t* discarded, std::string* err, int* versionOut) {
    bool opened = false;
    *lines = ReadJournal(path, discarded, &opened);
    if (!opened) {
        *err = "cannot open " + path;
        return false;
    }
    if (lines->empty()) {
        *err = "journal is empty or its header is damaged";
        return false;
    }
    R h((*lines)[0]);
    const std::string k = h.Keyword() == kJournalMagic ? h.S() : std::string();
    const long long version = h.I(0, 1000000);
    if (!h.Finished() || k != kind) {
        *err = "not a " + kind + " journal";
        return false;
    }
    if (version > kSaveFormatVersion) {
        *err = "journal made by a newer version of the game";
        return false;
    }
    size_t b = 1;
    while (b < lines->size() && (*lines)[b] != "begin") ++b;
    if (b >= lines->size()) {
        *err = "journal header is incomplete";
        return false;
    }
    *first = 1;
    *versionOut = static_cast<int>(version);
    return true;
}
}  // namespace

MatchLoad RecoverMatch(const std::string& journalPath, Recovery* info) {
    std::vector<std::string> lines;
    size_t first = 0, discarded = 0;
    std::string err;
    MatchLoad out;
    int version = 0;
    if (!OpenJournal(journalPath, "match", &lines, &first, &discarded, &err, &version)) {
        out.status = Status::Err(err);
        return out;
    }
    // Replay entry by entry; stop at the first one that can't be replayed.
    size_t i = first;
    MatchRules rules;
    std::array<Team, 2> teams;
    if (!ParseSetup(lines, &i, &rules, &teams, &err, version)) {
        out.status = Status::Err(err);
        return out;
    }
    // Rebuild with the setup, then actions one by one.
    if (i + 1 >= lines.size() || lines[i + 1] != "begin") {
        out.status = Status::Err("journal header is incomplete");
        return out;
    }
    std::vector<std::string> setup(lines.begin(), lines.begin() + static_cast<long>(i) + 1);  // includes "start"
    out = BuildMatch(setup, first, false, version);
    if (!out.status) return out;
    size_t a = i + 2;  // after "start" and "begin"
    for (; a < lines.size(); ++a) {
        MatchAction act;
        if (!ParseAction(lines[a], &act, &err) || !out.match->Apply(act)) break;
        out.steps++;
    }
    discarded += lines.size() - a;
    if (info) *info = {out.steps, discarded};
    return out;
}

SessionLoad RecoverSession(const std::string& journalPath, Recovery* info) {
    std::vector<std::string> lines;
    size_t first = 0, discarded = 0;
    std::string err;
    SessionLoad out;
    int version = 0;
    if (!OpenJournal(journalPath, "session", &lines, &first, &discarded, &err, &version)) {
        out.status = Status::Err(err);
        return out;
    }
    size_t stopped = lines.size();
    out = BuildSession(lines, first, false, true, &stopped, version);
    if (!out.status) return out;
    discarded += lines.size() - stopped;
    if (info) *info = {out.steps, discarded};
    return out;
}

}  // namespace cricket
