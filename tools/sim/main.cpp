// cricket_sim: runs a complete AI-vs-AI match through the server-authoritative
// MatchSession and prints ball-by-ball commentary and the scorecard.
//
// Usage: cricket_sim [t20|odi|test|<overs>] [teamA] [teamB] [--hindi] [--seed N] [--quiet]
// Team numbers 0-9 (see list printed with --teams).
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "cricket/Commentary.h"
#include "cricket/MatchSession.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

static void PrintScorecard(const Match& m) {
    for (const auto& inn : m.AllInnings()) {
        const Team& bat = m.GetTeam(inn.BattingSide());
        const Team& fld = m.GetTeam(1 - inn.BattingSide());
        std::cout << "\n=== " << bat.name << " innings " << inn.ScoreString() << " ===\n";
        for (int i = 0; i < 11; ++i) {
            const auto& c = inn.Batter(i);
            if (!c.batted) continue;
            std::string how = c.out ? ToString(c.how) : "not out";
            if (c.out && c.bowler >= 0) how += " b " + fld.xi[c.bowler].name;
            std::cout << "  " << std::left << std::setw(22) << bat.xi[i].name << std::setw(30) << how << std::right
                      << std::setw(4) << c.runs << " (" << c.balls << ")\n";
        }
        const auto& e = inn.GetExtras();
        std::cout << "  Extras " << e.Total() << " (w " << e.wides << ", nb " << e.noBalls << ", b " << e.byes
                  << ", lb " << e.legByes << ")\n  Bowling:\n";
        for (int i = 0; i < 11; ++i) {
            const auto& b = inn.Bowler(i);
            if (b.legalBalls == 0 && b.wides + b.noBalls == 0) continue;
            std::cout << "  " << std::left << std::setw(22) << fld.xi[i].name << std::right << std::setw(3)
                      << b.legalBalls / 6 << "." << b.legalBalls % 6 << "  " << std::setw(2) << b.maidens << "  "
                      << std::setw(3) << b.runs << "  " << b.wickets << "\n";
        }
    }
    std::cout << "\nRESULT: " << m.Result().summary << "\n";
}

int main(int argc, char** argv) {
    MatchRules rules = MatchRules::T20();
    int ta = 0, tb = 1, teamArgs = 0;
    uint64_t seed = Toss::SecureSeed();
    bool hindi = false, quiet = false;
    auto teams = FictionalTeams();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "t20") rules = MatchRules::T20();
        else if (a == "odi") rules = MatchRules::ODI();
        else if (a == "test") rules = MatchRules::Test();
        else if (a == "--hindi") hindi = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
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

    MatchSession session(rules, {teams[ta], teams[tb]}, seed);
    session.RunAi();  // both captains are AI
    QuickSim sim(seed + 1);
    CommentaryEngine com(hindi ? Language::Hindi : Language::English, seed + 2);
    std::cout << teams[ta].name << " vs " << teams[tb].name << "  (seed " << seed << ")\n";

    uint64_t seen = 0;
    while (session.WaitingFor().what == "delivery") {
        Delivery d = sim.Next(session.GetMatch());
        if (auto c = sim.MakeCase(session.GetMatch(), d)) session.ResolveAppeal(*c);
        else session.ResolveDelivery(d);
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
    }
    PrintScorecard(session.GetMatch());
    return 0;
}
