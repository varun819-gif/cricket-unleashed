// cricket_sim: runs a complete computer-v-computer match through the
// server-authoritative MatchSession. Every ball is played by the ball physics
// (BallOutcome); prints ball-by-ball commentary and the full scorecard.
//
// Usage: cricket_sim [t20|odi|test|overs=N] [teamA] [teamB] [--hindi] [--seed N] [--quiet]
//                    [--drs icc|no-reviews|no-tech|custom:<reviews>:<seconds>[:lose][:calls]]
//                    [--rain]   random rain delays (non-official rain rule for targets)
//                    [--pitch standard|green|dusty|bouncy|flat] [--wind X,Y] [--day-night]
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
}

int main(int argc, char** argv) {
    MatchRules rules = MatchRules::T20();
    int ta = 0, tb = 1, teamArgs = 0;
    uint64_t seed = Toss::SecureSeed();
    bool hindi = false, quiet = false, rain = false, dayNight = false;
    std::string pitchName = "standard";
    double windX = 0, windY = 0;
    auto teams = NationalTeams();
    std::string drsName = "icc";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "t20") rules = MatchRules::T20();
        else if (a == "odi") rules = MatchRules::ODI();
        else if (a == "test") rules = MatchRules::Test();
        else if (a == "--hindi") hindi = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--rain") rain = true;
        else if (a == "--day-night") dayNight = true;
        else if (a == "--pitch" && i + 1 < argc) pitchName = argv[++i];
        else if (a == "--wind" && i + 1 < argc) std::sscanf(argv[++i], "%lf,%lf", &windX, &windY);
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--drs" && i + 1 < argc) drsName = argv[++i];
        else if (a == "--teams") {
            for (size_t t = 0; t < teams.size(); ++t) std::cout << t << ": " << teams[t].name << "\n";
            return 0;
        } else if (a.rfind("overs=", 0) == 0) rules = MatchRules::Custom(std::atoi(a.c_str() + 6));
        else if (std::isdigit(static_cast<unsigned char>(a[0]))) {
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
            if (!quiet) std::cout << "      [" << r.summary << "]\n";
        }
        printNewEvents();
    }
    printNewEvents();
    PrintScorecard(session.GetMatch());
    return 0;
}
