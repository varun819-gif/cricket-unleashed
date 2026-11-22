// cricket_sim: runs a complete computer-v-computer match through the
// server-authoritative MatchSession. Every ball is played by the ball physics
// (BallOutcome); prints ball-by-ball commentary and the full scorecard.
//
// Usage: cricket_sim [t20] [teamA] [teamB] [--hindi] [--seed N] [--quiet]
//                    [--drs icc|no-reviews|no-tech|custom:<reviews>:<seconds>[:lose][:calls]]
//                    [--rain]   random rain delays (non-official rain rule for targets)
//                    [--pitch standard|green|dusty|bouncy|flat] [--wind X,Y] [--day-night]
//                    [--dev-formats odi|test|overs=N]  (development only; the game is T20)
//                    [--record FILE]  ball-by-ball physics + highlights (JSON lines)
//        cricket_sim --verify FILE   rebuild a saved match (e.g. the server's
//                    data/matches/<id>.save) and re-check its scorecard and result
// Team numbers 0-9 (see list printed with --teams).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>

#include "cricket/BallOutcome.h"
#include "cricket/Commentary.h"
#include "cricket/Scorecard.h"
#include "cricket/MatchSession.h"
#include "cricket/Recording.h"
#include "cricket/SaveGame.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

static void PrintScorecard(const Match& m) {
    for (const auto& c : BuildScorecard(m)) {
        std::cout << "\n=== " << c.team << " " << c.total << "/" << c.wickets << " (" << c.overs << " overs, "
                  << c.end << ", RR " << c.runRate << ") ===\n";
        std::cout << "  " << std::left << std::setw(22) << "Batter" << std::setw(30) << "" << std::right << std::setw(4)
                  << "R" << std::setw(5) << "B" << std::setw(4) << "4s" << std::setw(4) << "6s" << std::setw(8) << "SR" << "\n";
        for (const auto& b : c.batting) {
            if (!b.batted) continue;
            std::cout << "  " << std::left << std::setw(22) << b.name << std::setw(30) << b.howOut << std::right
                      << std::setw(4) << b.runs << std::setw(5) << b.balls << std::setw(4) << b.fours << std::setw(4)
                      << b.sixes << std::setw(8) << std::fixed << std::setprecision(1) << b.strikeRate << "\n";
        }
        const auto& e = c.extras;
        std::cout << "  Extras " << c.extrasTotal << " (w " << e.wides << ", nb " << e.noBalls << ", b " << e.byes
                  << ", lb " << e.legByes << ", pen " << e.penalty << ")\n";
        std::cout << "  Fall of wickets:";
        for (const auto& f : c.fallOfWickets) std::cout << " " << f.wicket << "-" << f.score << " (" << f.name << ", " << f.over << ")";
        std::cout << "\n  " << std::left << std::setw(22) << "Bowler" << std::right << std::setw(6) << "O" << std::setw(4)
                  << "M" << std::setw(5) << "R" << std::setw(4) << "W" << std::setw(8) << "Econ" << "\n";
        for (const auto& b : c.bowling)
            std::cout << "  " << std::left << std::setw(22) << b.name << std::right << std::setw(6) << b.overs
                      << std::setw(4) << b.maidens << std::setw(5) << b.runs << std::setw(4) << b.wickets
                      << std::setw(8) << std::fixed << std::setprecision(2) << b.economy << "\n";
        std::cout << "  Partnerships:";
        for (const auto& p : c.partnerships) std::cout << " " << p.runs << "(" << p.balls << ")" << (p.unbroken ? "*" : "");
        std::cout << "\n";
    }
    std::cout << "\nRESULT: " << m.Result().summary << "\n";
    if (Status s = CheckScorecard(m); !s) std::cout << "SCORECARD CHECK FAILED: " << s.error << "\n";
    if (Status s = CheckResult(m); !s) std::cout << "RESULT CHECK FAILED: " << s.error << "\n";
}

// Rebuilds a saved match from its file (every ball replayed through the rules
// engine) and checks the scorecard and the result. Exit code 0 = all correct.
static int Verify(const std::string& path) {
    SessionLoad l = LoadSessionFile(path);
    if (!l.status) {
        std::cout << "VERIFY FAILED: cannot load " << path << ": " << l.status.error << "\n";
        return 1;
    }
    if (!l.session->HasMatch()) {
        std::cout << "VERIFY FAILED: no match in the file\n";
        return 1;
    }
    const Match& m = l.session->GetMatch();
    PrintScorecard(m);
    const Status a = CheckScorecard(m), b = CheckResult(m);
    std::cout << "VERIFY " << (a && b ? "OK" : "FAILED") << ": " << m.Result().summary << "\n";
    for (const auto& c : BuildScorecard(m)) std::cout << "TOTAL " << c.team << " " << c.total << "/" << c.wickets << " " << c.overs << "\n";
    return a && b ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--verify") == 0) return Verify(argv[2]);
    MatchRules rules = MatchRules::T20();
    int ta = 0, tb = 1, teamArgs = 0;
    uint64_t seed = Toss::SecureSeed();
    bool hindi = false, quiet = false, rain = false, dayNight = false, devFormats = false;
    std::string pitchName = "standard";
    double windX = 0, windY = 0;
    auto teams = NationalTeams();
    std::string drsName = "icc";
    std::string recordPath;  // --record FILE: ball-by-ball physics + highlights (JSON lines)
    MatchRecorder recorder;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "t20") rules = MatchRules::T20();
        else if (a == "odi" || a == "test" || a.rfind("overs=", 0) == 0) {
            // The game is T20 only; other formats stay available to developers.
            if (!devFormats) {
                std::cerr << "only T20 is offered (add --dev-formats before it for development)\n";
                return 2;
            }
            rules = a == "odi" ? MatchRules::ODI() : a == "test" ? MatchRules::Test() : MatchRules::Custom(std::atoi(a.c_str() + 6));
        } else if (a == "--dev-formats") devFormats = true;
        else if (a == "--hindi") hindi = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--rain") rain = true;
        else if (a == "--day-night") dayNight = true;
        else if (a == "--pitch" && i + 1 < argc) pitchName = argv[++i];
        else if (a == "--wind" && i + 1 < argc) std::sscanf(argv[++i], "%lf,%lf", &windX, &windY);
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--drs" && i + 1 < argc) drsName = argv[++i];
        else if (a == "--record" && i + 1 < argc) recordPath = argv[++i];
        else if (a == "--teams") {
            for (size_t t = 0; t < teams.size(); ++t) std::cout << t << ": " << teams[t].name << "\n";
            return 0;
        } else if (std::isdigit(static_cast<unsigned char>(a[0]))) {
            int v = std::atoi(a.c_str()) % static_cast<int>(teams.size());
            (teamArgs++ == 0 ? ta : tb) = v;
        }
    }
    if (ta == tb) tb = (ta + 1) % static_cast<int>(teams.size());
    if (!DrsRules::FromName(drsName, &rules.drs)) {
        std::cerr << "unknown --drs ruleset: " << drsName << "\n";
        return 2;
    }

    MatchSession session(rules, {teams[ta], teams[tb]}, seed);
    session.RunAi();  // both captains are AI
    QuickSim perception(seed + 1);  // players' beliefs on appeals (not outcomes)
    BallOutcome physics(seed + 4);
    physics.conditions.pitchName = pitchName;
    physics.conditions.pitch = phys::Pitch::ByName(pitchName);
    physics.conditions.pitch.cracks = seed | 1;
    physics.conditions.wind = {windX, windY, 0};
    physics.conditions.dayNight = dayNight;
    CommentaryEngine com(hindi ? Language::Hindi : Language::English, seed + 2);
    std::cout << teams[ta].name << " vs " << teams[tb].name << "  (seed " << seed << ")\n";

    uint64_t seen = 0;
    auto printNewEvents = [&]() {
        for (const auto& e : session.EventsSince(seen)) {
            if (e.kind == "ball") {
                if (!quiet) {
                    std::cout << e.text << "\n";
                    for (const auto& l : com.Describe(session.GetMatch(), *e.ball))
                        std::cout << "      " << l.speaker << ": " << l.text << "\n";
                }
            } else {
                std::cout << "** " << e.text << "\n";
                if (e.decision && !quiet)
                    for (const auto& l : com.DescribeDecision(session.GetMatch(), *e.decision))
                        std::cout << "      " << l.speaker << ": " << l.text << "\n";
            }
        }
        seen = session.LastEventId();
    };
    printNewEvents();
    std::mt19937_64 weather(seed + 3);
    while (session.WaitingFor().what == "delivery") {
        if (rain && std::uniform_real_distribution<double>(0, 1)(weather) < 0.006)
            session.RainDelay(std::uniform_int_distribution<int>(10, 150)(weather));
        if (session.WaitingFor().what == "delivery") {
            const BallReport r = physics.PlayComputerBall(session, &perception);
            recorder.Add(r);
            if (!quiet) std::cout << "      [" << r.summary << "]\n";
        }
        printNewEvents();
    }
    printNewEvents();
    PrintScorecard(session.GetMatch());
    const auto highlights = HighlightPackage(FindHighlights(session.GetMatch(), &recorder.Traces()), 12);
    std::cout << "\nHIGHLIGHTS (" << highlights.size() << " clips):\n";
    for (const auto& h : highlights) std::cout << "  " << h.text << "\n";
    if (!recordPath.empty()) {
        if (Status st = SaveFileAtomic(recordPath, recorder.ToJsonLines(session.GetMatch(), pitchName)); !st) {
            std::cerr << "could not write " << recordPath << ": " << st.error << "\n";
            return 1;
        }
        std::cout << "Recording written to " << recordPath << "\n";
    }
    return 0;
}
