// Stage D: saving and resuming matches and server sessions, validation,
// backups and crash recovery.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "Test.h"
#include "cricket/AutoCaptain.h"
#include "cricket/SaveGame.h"
#include "cricket/TeamFactory.h"

using namespace cricket;
namespace fs = std::filesystem;

namespace {

std::array<Team, 2> TwoTeams() {
    auto all = FictionalTeams();
    return {all[0], all[1]};
}

std::string Dir() {
    static std::string d = [] {
        auto p = fs::temp_directory_path() / ("cu_save_tests_" + std::to_string(::getpid()));
        fs::create_directories(p);
        return p.string();
    }();
    return d;
}

std::string Hex16(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

// What a cheater would do: edit the body and recompute the checksum.
std::string Reseal(const std::string& text) {
    const size_t pos = text.rfind("checksum ");
    const std::string body = text.substr(0, pos);
    return body + "checksum " + Hex16(Fnv1a64(body)) + "\n";
}

std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void Spit(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

// Drives a match (AI captains + QuickSim) for up to `balls` deliveries.
struct Driver {
    AutoCaptain c0, c1;
    QuickSim sim;
    explicit Driver(uint64_t seed) : c0(seed), c1(seed + 1), sim(seed + 2) {}
    void Step(Match& m) {
        if (m.Phase() == MatchPhase::AwaitingFollowOnDecision) {
            CHECK_OK(m.DecideFollowOn(true));
            return;
        }
        AutoCaptain& bat = m.BattingSide() == 0 ? c0 : c1;
        AutoCaptain& fld = m.BattingSide() == 0 ? c1 : c0;
        if (m.HasPendingDecision()) {
            const int side = m.ReviewEntitledSide();
            CHECK_OK(fld.ShouldReview(m, side) ? m.ReviewDecision(side) : m.AcceptDecision());
            return;
        }
        Innings& inn = m.Current();
        switch (inn.State()) {
            case InningsState::AwaitingOpeners: CHECK_OK(m.SetOpeners(0, 1)); break;
            case InningsState::AwaitingBatter: CHECK_OK(m.SetNextBatter(bat.ChooseNextBatter(m))); break;
            case InningsState::AwaitingBowler: {
                if (inn.NewBallDue()) CHECK_OK(m.TakeNewBall());
                int b = fld.ChooseBowler(m);
                CHECK_OK(m.SetBowler(b));
                break;
            }
            case InningsState::InPlay: {
                Delivery d = sim.Next(m);
                auto c = sim.MakeCase(m, d);
                if (!c || !m.Appeal(*c)) CHECK_OK(m.Bowl(d));
                break;
            }
            case InningsState::Complete: break;
        }
    }
    void Run(Match& m, int steps) {
        for (int i = 0; i < steps && m.Phase() != MatchPhase::Complete; ++i) Step(m);
    }
};

void ExpectSameAfterReload(const Match& m) {
    std::string text = SaveMatch(m);
    MatchLoad l = LoadMatch(text);
    CHECK_OK(l.status);
    if (!l.match) return;
    CHECK_EQ(l.match->StateFingerprint(), m.StateFingerprint());
    CHECK_EQ(l.match->Journal().size(), m.Journal().size());
    CHECK_EQ(l.steps, m.Journal().size());
    CHECK_EQ(SaveMatch(*l.match), text);  // saving the loaded match gives the same file
}

}  // namespace

TEST(save_fnv_reference_value) {
    CHECK_EQ(Hex16(Fnv1a64("")), std::string("cbf29ce484222325"));
    CHECK_EQ(Hex16(Fnv1a64("a")), std::string("af63dc4c8601ec8c"));
}

TEST(save_round_trip_every_format_mid_match_and_finished) {
    for (int f = 0; f < 3; ++f) {
        MatchRules rules = f == 0 ? MatchRules::T20() : f == 1 ? MatchRules::ODI() : MatchRules::Test();
        Match m(rules, TwoTeams(), f % 2);
        Driver d(100 + f);
        ExpectSameAfterReload(m);  // before a ball
        d.Run(m, 157);             // mid-innings, often mid-over
        ExpectSameAfterReload(m);
        d.Run(m, 100000);          // finished
        CHECK(m.Phase() == MatchPhase::Complete);
        ExpectSameAfterReload(m);
    }
}

TEST(save_resumed_match_continues_identically) {
    Match a(MatchRules::T20(), TwoTeams(), 0);
    Driver da(7);
    da.Run(a, 200);
    MatchLoad l = LoadMatch(SaveMatch(a));
    CHECK_OK(l.status);
    if (!l.match) return;
    Driver db = da;  // same simulator / AI random state from here on
    da.Run(a, 100000);
    db.Run(*l.match, 100000);
    CHECK_EQ(l.match->StateFingerprint(), a.StateFingerprint());
    CHECK_EQ(l.match->Result().summary, a.Result().summary);
}

TEST(save_with_pending_review_rain_penalty_lunch_follow_on) {
    {
        Match m(MatchRules::T20(), TwoTeams(), 0);
        m.SetOpeners(0, 1);
        m.SetBowler(10);
        DecisionCase c;
        c.mode = Dismissal::Caught;
        c.onFieldOut = true;
        c.ifOut.dismissal = Dismissal::Caught;
        c.evidence.batContact = Evidence::No;
        c.perceived = {0.123456789012345, 0.9};
        CHECK_OK(m.Appeal(c));
        CHECK_ERR(m.AwardPenaltyRuns(1, 5, "pitch damage"));  // pending -> refused, not journaled
        MatchLoad l = LoadMatch(SaveMatch(m));
        CHECK_OK(l.status);
        if (!l.match) return;
        CHECK(l.match->HasPendingDecision());
        CHECK_EQ(l.match->PendingDecision()->perceived[0], 0.123456789012345);  // exact doubles
        CHECK_OK(l.match->ReviewDecision(0));
        CHECK_OK(m.ReviewDecision(0));
        CHECK_EQ(l.match->StateFingerprint(), m.StateFingerprint());
    }
    {
        Match m(MatchRules::ODI(), TwoTeams(), 0);
        Driver d(11);
        d.Run(m, 120);
        CHECK_OK(m.RainDelay(95));
        CHECK_OK(m.AwardPenaltyRuns(m.BattingSide(), 5, "ball hit helmet"));
        d.Run(m, 400);
        ExpectSameAfterReload(m);
    }
    {
        Match m(MatchRules::Test(), TwoTeams(), 0);
        Driver d(12);
        while (m.Intervals().empty() && m.Phase() != MatchPhase::Complete) d.Step(m);
        CHECK_OK(m.RainDelay(45));
        ExpectSameAfterReload(m);  // at lunch, after rain
        CHECK_OK(m.LoseRestOfDay());
        ExpectSameAfterReload(m);
    }
    {
        Match m(MatchRules::Test(), TwoTeams(), 0);
        Delivery four;
        four.runs = 4;
        Delivery out;
        out.dismissal = Dismissal::Bowled;
        m.SetOpeners(0, 1);
        for (int i = 0; m.AllInnings().size() < 3 && i < 2000; ++i) {
            Innings& inn = m.Current();
            if (m.Phase() == MatchPhase::AwaitingFollowOnDecision) break;
            if (inn.State() == InningsState::AwaitingOpeners) m.SetOpeners(0, 1);
            else if (inn.State() == InningsState::AwaitingBatter) {
                for (int b = 0; b < 11; ++b)
                    if (!inn.Batter(b).batted) {
                        m.SetNextBatter(b);
                        break;
                    }
            } else if (inn.State() == InningsState::AwaitingBowler) m.SetBowler(inn.LastOverBowler() == 10 ? 9 : 10);
            else m.Bowl(m.AllInnings().size() == 1 && inn.LegalBalls() < 60 ? four : out);
        }
        CHECK(m.Phase() == MatchPhase::AwaitingFollowOnDecision);
        ExpectSameAfterReload(m);
    }
}

TEST(save_unicode_names_and_custom_rules_survive) {
    auto teams = TwoTeams();
    teams[0].name = "भारत Royals 100%";
    teams[0].xi[0].name = "Rohan \"Ro\" O'Brien - कप्तान";
    teams[1].name = "-";
    MatchRules r = MatchRules::Custom(7);
    r.drs = DrsRules::Custom(3, 20, false);
    r.drs.reviewWidesAndNoBalls = true;
    r.weather.minutesPerOver = 4.123456789;
    Match m(r, teams, 1);
    Driver d(5);
    d.Run(m, 30);
    MatchLoad l = LoadMatch(SaveMatch(m));
    CHECK_OK(l.status);
    if (!l.match) return;
    CHECK_EQ(l.match->GetTeam(0).name, teams[0].name);
    CHECK_EQ(l.match->GetTeam(0).xi[0].name, teams[0].xi[0].name);
    CHECK_EQ(l.match->GetTeam(1).name, std::string("-"));
    CHECK_EQ(l.match->Rules().weather.minutesPerOver, 4.123456789);
    CHECK_EQ(l.match->Rules().drs.reviewsPerInnings, 3);
    CHECK(l.match->Rules().drs.reviewWidesAndNoBalls);
    CHECK_EQ(l.match->StateFingerprint(), m.StateFingerprint());
}

TEST(save_rejects_damaged_edited_and_foreign_files) {
    Match m(MatchRules::T20(), TwoTeams(), 0);
    Driver d(3);
    d.Run(m, 60);
    const std::string good = SaveMatch(m);
    CHECK_OK(LoadMatch(good).status);

    std::string flipped = good;
    flipped[flipped.size() / 2] ^= 1;
    CHECK(LoadMatch(flipped).status.error.find("checksum") != std::string::npos);

    CHECK(LoadMatch(good.substr(0, good.size() / 2)).status.error.find("complete") != std::string::npos);
    CHECK_ERR(LoadMatch("").status);
    CHECK_ERR(LoadMatch("hello\nchecksum 0\n").status);

    // A cheater changes a delivery from 0 to 6 runs and reseals the checksum:
    // the final-state fingerprint no longer matches.
    std::string edited = good;
    const size_t act = edited.find("\nact 3 ");  // first Bowl action
    CHECK(act != std::string::npos);
    const size_t runs = act + std::string("\nact 3 0 0 - 0 ").size();
    edited[runs] = edited[runs] == '6' ? '1' : '6';
    edited = Reseal(edited);
    MatchLoad e = LoadMatch(edited);
    CHECK_ERR(e.status);
    CHECK(!e.match);

    // An impossible action (9 runs) fails during replay.
    std::string impossible = good;
    impossible[runs] = '9';
    impossible = Reseal(impossible);
    CHECK(LoadMatch(impossible).status.error.find("replayed") != std::string::npos);

    // Newer format, wrong kind.
    std::string newer = good;
    newer.replace(newer.find(" match 1\n"), 9, " match 2\n");
    CHECK(LoadMatch(Reseal(newer)).status.error.find("newer") != std::string::npos);
    std::string session = good;
    session.replace(session.find(" match 1\n"), 9, " session 1\n");
    CHECK(LoadMatch(Reseal(session)).status.error.find("session") != std::string::npos);

    // Impossible rules in the header are refused before anything is built.
    std::string badRules = good;
    const size_t rl = badRules.find("\nrules 0 20 6 ");
    CHECK(rl != std::string::npos);
    badRules.replace(rl, 14, "\nrules 0 20 0 ");  // 0 balls per over
    CHECK_ERR(LoadMatch(Reseal(badRules)).status);
}

TEST(save_files_atomic_with_backup_fallback) {
    const std::string path = Dir() + "/match.cus";
    fs::remove(path);
    fs::remove(path + ".bak");
    Match m(MatchRules::T20(), TwoTeams(), 0);
    Driver d(9);
    d.Run(m, 40);
    CHECK_OK(SaveMatchFile(m, path));
    const std::string first = m.StateFingerprint();
    CHECK(!fs::exists(path + ".bak"));
    d.Run(m, 40);
    CHECK_OK(SaveMatchFile(m, path));
    CHECK(fs::exists(path + ".bak"));
    CHECK(!fs::exists(path + ".tmp"));

    MatchLoad l = LoadMatchFile(path);
    CHECK_OK(l.status);
    CHECK(!l.fromBackup);
    if (l.match) CHECK_EQ(l.match->StateFingerprint(), m.StateFingerprint());

    // The main file gets damaged: the backup (previous save) is used.
    std::string text = Slurp(path);
    Spit(path, text.substr(0, text.size() - 40));
    MatchLoad b = LoadMatchFile(path);
    CHECK_OK(b.status);
    CHECK(b.fromBackup);
    CHECK(!b.primaryError.empty());
    if (b.match) CHECK_EQ(b.match->StateFingerprint(), first);

    // Both damaged: a clear error, no match.
    Spit(path + ".bak", "garbage");
    MatchLoad n = LoadMatchFile(path);
    CHECK_ERR(n.status);
    CHECK(!n.match);
    CHECK_ERR(LoadMatchFile(Dir() + "/does-not-exist.cus").status);
}

TEST(save_crash_journal_recovers_to_last_good_action) {
    const std::string path = Dir() + "/match.journal";
    Match m(MatchRules::ODI(), TwoTeams(), 1);
    MatchJournal j;
    CHECK_OK(j.Start(path, m));
    Driver d(21);
    std::vector<std::string> prints;
    for (int i = 0; i < 300 && m.Phase() != MatchPhase::Complete; ++i) {
        d.Step(m);
        CHECK_OK(j.Sync(m));
        prints.resize(m.Journal().size() + 1);
        prints[m.Journal().size()] = m.StateFingerprint();
    }
    CHECK_EQ(j.Written(), m.Journal().size());

    Recovery info;
    MatchLoad full = RecoverMatch(path, &info);
    CHECK_OK(full.status);
    CHECK_EQ(info.recovered, m.Journal().size());
    CHECK_EQ(info.discarded, size_t{0});
    if (full.match) CHECK_EQ(full.match->StateFingerprint(), m.StateFingerprint());

    // Power cut in the middle of writing the last line.
    std::string text = Slurp(path);
    Spit(path, text.substr(0, text.size() - 7));
    MatchLoad cut = RecoverMatch(path, &info);
    CHECK_OK(cut.status);
    CHECK_EQ(info.recovered, m.Journal().size() - 1);
    CHECK_EQ(info.discarded, size_t{1});
    if (cut.match) CHECK_EQ(cut.match->StateFingerprint(), prints[m.Journal().size() - 1]);

    // Cut exactly at the final line break: the last line can't be trusted
    // to be complete, so it is dropped.
    Spit(path, text.substr(0, text.size() - 1));
    MatchLoad nl = RecoverMatch(path, &info);
    CHECK_OK(nl.status);
    CHECK_EQ(info.discarded, size_t{1});
    CHECK_EQ(info.recovered, m.Journal().size() - 1);

    // Garbage written over the tail: everything before it is kept.
    const size_t keep = text.size() * 2 / 3;
    std::string scrambled = text.substr(0, keep) + "#!@ not a journal line\nmore junk\n";
    Spit(path, scrambled);
    MatchLoad g = RecoverMatch(path, &info);
    CHECK_OK(g.status);
    CHECK(info.recovered > 0 && info.discarded >= 2);
    if (g.match) CHECK_EQ(g.match->StateFingerprint(), prints[info.recovered]);

    // Damaged header: refuse.
    Spit(path, "CU-JOURNAL match 1\t#00000000\n");
    CHECK_ERR(RecoverMatch(path).status);
    CHECK_ERR(RecoverMatch(Dir() + "/missing.journal").status);
}

TEST(save_rules_validation) {
    MatchRules r = MatchRules::T20();
    r.ballsPerOver = 0;
    CHECK_ERR(Match::ValidateRules(r));
    bool threw = false;
    try {
        Match m(r, TwoTeams(), 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
    CHECK_OK(Match::ValidateRules(MatchRules::Test()));
    CHECK_OK(Match::ValidateRules(MatchRules::Custom(3)));
}

// ---------------------------------------------------------------- server sessions

namespace {
Command Cmd(const std::string& p, uint64_t seq, CommandType t, int a = 0, int b = 0) {
    Command c;
    c.player = p;
    c.seq = seq;
    c.type = t;
    c.a = a;
    c.b = b;
    return c;
}

// India vs Pakistan, two humans, Test match: toss, review with timer,
// lunch pause, rain, a custom field, a refused (duplicate) command.
struct Online {
    MatchSession s{MatchRules::Test(), TwoTeams(), 2024};
    uint64_t seq[2] = {0, 0};
    std::string name[2] = {"india-captain", "pakistan-captain"};
    int64_t now = 0;
    Status Send(int side, CommandType t, int a = 0, int b = 0) {
        now += 1500;
        return s.SubmitAt(Cmd(name[side], ++seq[side], t, a, b), now);
    }
    void Begin() {
        s.Join(name[0], 0);
        s.Join(name[1], 1);
        Send(1, CommandType::TossCall, 1);
        Send(s.GetToss().Winner(), CommandType::TossDecision, 0);
    }
    void Play(QuickSim& sim, int steps) {
        for (int i = 0; i < steps; ++i) {
            Pending p = s.WaitingFor();
            const Match& m = s.GetMatch();
            if (p.what == "openers") Send(p.side, CommandType::SetOpeners, 0, 1);
            else if (p.what == "batter") {
                for (int b = 0; b < 11; ++b)
                    if (!m.Current().Batter(b).batted) {
                        Send(p.side, CommandType::SetNextBatter, b);
                        break;
                    }
            } else if (p.what == "bowler") Send(p.side, CommandType::SetBowler, m.Current().CanBowlNextOver(10) ? 10 : 9);
            else if (p.what == "review") Send(p.side, CommandType::Review, i % 2);
            else if (p.what == "interval") {
                Send(0, CommandType::ResumePlay);
                Send(1, CommandType::ResumePlay);
            } else if (p.what == "follow-on") Send(p.side, CommandType::FollowOn, 1);
            else if (p.what == "delivery") {
                now += 30000;
                s.Tick(now);
                Delivery d = sim.Next(m);
                auto c = sim.MakeCase(m, d);
                if (!c || !s.ResolveAppeal(*c)) s.ResolveDelivery(d);
            } else return;
        }
    }
};

std::vector<std::string> EventTexts(const MatchSession& s) {
    std::vector<std::string> v;
    for (const auto& e : s.EventsSince(0)) v.push_back(e.kind + ":" + e.text);
    return v;
}
}  // namespace

TEST(save_session_round_trip_and_identical_continuation) {
    Online o;
    o.Begin();
    QuickSim sim(77);
    o.Play(sim, 400);
    CHECK_ERR(o.s.Submit(Cmd(o.name[0], 1, CommandType::ResumePlay)));  // stale seq: refused but journaled
    // Refused, but it still uses up sequence number +50: the journal must
    // keep it or the resumed server would accept a replayed command.
    const int fielding = 1 - o.s.GetMatch().BattingSide();
    o.seq[fielding] += 49;
    CHECK_ERR(o.Send(fielding, CommandType::Declare));
    o.s.RainDelay(30);
    o.Play(sim, 37);

    const std::string text = SaveSession(o.s);
    SessionLoad l = LoadSession(text);
    CHECK_OK(l.status);
    if (!l.session) return;
    CHECK_EQ(l.session->StateFingerprint(), o.s.StateFingerprint());
    CHECK(EventTexts(*l.session) == EventTexts(o.s));
    CHECK_EQ(SaveSession(*l.session), text);
    CHECK_EQ(l.steps, o.s.Inputs().size());

    // Both servers carry on with the same inputs: they stay identical,
    // including the AI captains' random decisions.
    Online copy;  // a fresh server with the same seed, rebuilt from the inputs
    for (const auto& in : o.s.Inputs()) CHECK_EQ(copy.s.Replay(in).ok, in.ok);
    copy.seq[0] = o.seq[0];
    copy.seq[1] = o.seq[1];
    copy.now = o.now;
    QuickSim sim2 = sim;
    o.Play(sim, 600);
    copy.Play(sim2, 600);
    CHECK_EQ(copy.s.StateFingerprint(), o.s.StateFingerprint());
}

TEST(save_session_ai_vs_ai_continuation) {
    MatchSession a(MatchRules::T20(), TwoTeams(), 555);
    a.RunAi();
    QuickSim simA(556);
    for (int i = 0; i < 120 && a.WaitingFor().what == "delivery"; ++i) {
        Delivery d = simA.Next(a.GetMatch());
        auto c = simA.MakeCase(a.GetMatch(), d);
        if (!c || !a.ResolveAppeal(*c)) a.ResolveDelivery(d);
    }
    SessionLoad l = LoadSession(SaveSession(a));
    CHECK_OK(l.status);
    if (!l.session) return;
    QuickSim simB = simA;
    for (auto* pair : {&a, l.session.get()}) {
        QuickSim& sim = pair == &a ? simA : simB;
        while (pair->WaitingFor().what == "delivery") {
            Delivery d = sim.Next(pair->GetMatch());
            auto c = sim.MakeCase(pair->GetMatch(), d);
            if (!c || !pair->ResolveAppeal(*c)) pair->ResolveDelivery(d);
        }
    }
    CHECK_EQ(l.session->StateFingerprint(), a.StateFingerprint());
    CHECK_EQ(l.session->GetMatch().Result().summary, a.GetMatch().Result().summary);
}

TEST(save_session_keeps_refused_inputs) {
    // A refused command still uses up its sequence number. A save made right
    // after it must remember that, or the resumed server would accept an
    // old command again (replay attack).
    MatchSession s(MatchRules::T20(), TwoTeams(), 4);
    CHECK_OK(s.Join("a", 0));
    CHECK_OK(s.Join("b", 1));
    CHECK_ERR(s.Submit(Cmd("a", 500, CommandType::TossCall, 0)));  // not a's call: refused, seq 500 used
    SessionLoad l = LoadSession(SaveSession(s));
    CHECK_OK(l.status);
    if (!l.session) return;
    CHECK_EQ(l.session->StateFingerprint(), s.StateFingerprint());
    Status replayed = l.session->Submit(Cmd("a", 300, CommandType::TossCall, 0));
    CHECK(replayed.error.find("duplicate") != std::string::npos);
}

TEST(save_session_tampering_detected) {
    Online o;
    o.Begin();
    QuickSim sim(8);
    o.Play(sim, 80);
    const std::string good = SaveSession(o.s);
    // Flip the recorded result of the first input ("accepted" -> "refused").
    std::string t = good;
    const size_t in = t.find("\nin 1 ");
    CHECK(in != std::string::npos);
    t[in + 4] = '0';
    SessionLoad l = LoadSession(Reseal(t));
    CHECK_ERR(l.status);
    CHECK(l.status.error.find("diverged") != std::string::npos);
    CHECK(!l.session);
    CHECK_ERR(LoadSession(SaveMatch(Match(MatchRules::T20(), TwoTeams(), 0))).status);  // a match file
}

TEST(save_session_crash_journal_and_file_backup) {
    const std::string path = Dir() + "/session.journal";
    Online o;
    SessionJournal j;
    CHECK_OK(j.Start(path, o.s));
    o.Begin();
    QuickSim sim(31);
    for (int k = 0; k < 20; ++k) {
        o.Play(sim, 10);
        CHECK_OK(j.Sync(o.s));
    }
    Recovery info;
    SessionLoad r = RecoverSession(path, &info);
    CHECK_OK(r.status);
    CHECK_EQ(info.recovered, o.s.Inputs().size());
    if (r.session) CHECK_EQ(r.session->StateFingerprint(), o.s.StateFingerprint());

    std::string text = Slurp(path);
    Spit(path, text.substr(0, text.size() - 3));
    r = RecoverSession(path, &info);
    CHECK_OK(r.status);
    CHECK_EQ(info.discarded, size_t{1});
    CHECK_EQ(info.recovered, o.s.Inputs().size() - 1);

    const std::string file = Dir() + "/session.cus";
    fs::remove(file);
    fs::remove(file + ".bak");
    CHECK_OK(SaveSessionFile(o.s, file));
    SessionLoad f = LoadSessionFile(file);
    CHECK_OK(f.status);
    if (f.session) CHECK_EQ(f.session->StateFingerprint(), o.s.StateFingerprint());
}
