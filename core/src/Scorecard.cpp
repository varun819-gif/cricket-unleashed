#include "cricket/Scorecard.h"

#include <algorithm>
#include <map>

namespace cricket {

namespace {

double Round1(double v) { return static_cast<double>(static_cast<long long>(v * 10 + (v >= 0 ? 0.5 : -0.5))) / 10.0; }
double Round2(double v) { return static_cast<double>(static_cast<long long>(v * 100 + (v >= 0 ? 0.5 : -0.5))) / 100.0; }

std::string Surname(const std::string& full) {
    const size_t sp = full.rfind(' ');
    return sp == std::string::npos ? full : full.substr(sp + 1);
}

std::string HowOut(Dismissal how, const std::string& bowler, const std::string& fielder, bool fielderIsBowler) {
    switch (how) {
        case Dismissal::Bowled: return "b " + bowler;
        case Dismissal::Lbw: return "lbw b " + bowler;
        case Dismissal::Caught:
            if (fielderIsBowler) return "c & b " + bowler;
            return "c " + (fielder.empty() ? std::string("sub") : fielder) + " b " + bowler;
        case Dismissal::Stumped: return "st " + (fielder.empty() ? std::string("keeper") : fielder) + " b " + bowler;
        case Dismissal::HitWicket: return "hit wicket b " + bowler;
        case Dismissal::RunOut: return fielder.empty() ? "run out" : "run out (" + fielder + ")";
        case Dismissal::ObstructingField: return "obstructing the field";
        case Dismissal::HitBallTwice: return "hit the ball twice";
        case Dismissal::TimedOut: return "timed out";
        case Dismissal::RetiredOut: return "retired out";
        case Dismissal::None: break;
    }
    return "not out";
}

std::string OverOf(const DeliveryRecord& r) {
    return std::to_string(r.over) + "." + std::to_string(r.ballInOver);
}

const char* EndText(InningsEnd e) {
    switch (e) {
        case InningsEnd::AllOut: return "all out";
        case InningsEnd::OversComplete: return "overs complete";
        case InningsEnd::TargetReached: return "target reached";
        case InningsEnd::Declared: return "declared";
        case InningsEnd::Forced: return "ended";
        case InningsEnd::None: break;
    }
    return "in progress";
}

}  // namespace

std::string OversString(int legalBalls, int ballsPerOver) {
    return std::to_string(legalBalls / ballsPerOver) + "." + std::to_string(legalBalls % ballsPerOver);
}

std::vector<InningsCard> BuildScorecard(const Match& m) {
    std::vector<InningsCard> out;
    for (const Innings& inn : m.AllInnings()) {
        const Team& bat = m.GetTeam(inn.BattingSide());
        const Team& fld = m.GetTeam(1 - inn.BattingSide());
        const int bpo = inn.Rules().ballsPerOver;
        InningsCard c;
        c.index = inn.Index();
        c.battingSide = inn.BattingSide();
        c.team = bat.name;
        c.total = inn.Total();
        c.wickets = inn.Wickets();
        c.legalBalls = inn.LegalBalls();
        c.overs = OversString(c.legalBalls, bpo);
        c.runRate = c.legalBalls ? Round2(c.total * static_cast<double>(bpo) / c.legalBalls) : 0;
        c.extras = inn.GetExtras();
        c.extrasTotal = c.extras.Total();
        c.end = EndText(inn.EndReason());
        // Who dismissed whom (fielder) from the ball log.
        std::map<int, const DeliveryRecord*> outBall;
        for (const auto& r : inn.Balls())
            if (r.dismissedBatter >= 0) outBall[r.dismissedBatter] = &r;
        // Batting order: the order batters first appeared.
        std::vector<int> order;
        auto seen = [&](int p) { return std::find(order.begin(), order.end(), p) != order.end(); };
        for (const auto& r : inn.Balls()) {
            if (r.striker >= 0 && !seen(r.striker)) order.push_back(r.striker);
            if (r.nonStriker >= 0 && !seen(r.nonStriker)) order.push_back(r.nonStriker);
        }
        for (int i = 0; i < static_cast<int>(bat.xi.size()); ++i)
            if (inn.Batter(i).batted && !seen(i)) order.push_back(i);
        for (int i = 0; i < static_cast<int>(bat.xi.size()); ++i)
            if (!seen(i)) order.push_back(i);
        for (int p : order) {
            const BatterCard& b = inn.Batter(p);
            BattingLine l;
            l.player = p;
            l.name = bat.xi[static_cast<size_t>(p)].name;
            l.batted = b.batted;
            l.out = b.out;
            l.runs = b.runs;
            l.balls = b.balls;
            l.fours = b.fours;
            l.sixes = b.sixes;
            l.strikeRate = b.balls ? Round1(100.0 * b.runs / b.balls) : 0;
            if (!b.batted) l.howOut = "did not bat";
            else if (!b.out) l.howOut = "not out";
            else {
                std::string bowler = b.bowler >= 0 ? Surname(fld.xi[static_cast<size_t>(b.bowler)].name) : "";
                std::string fielder;
                bool cAndB = false;
                auto it = outBall.find(p);
                if (it != outBall.end() && it->second->delivery.fielder >= 0) {
                    fielder = Surname(fld.xi[static_cast<size_t>(it->second->delivery.fielder)].name);
                    cAndB = it->second->delivery.fielder == b.bowler;
                }
                l.howOut = HowOut(b.how, bowler, fielder, cAndB);
            }
            c.batting.push_back(l);
        }
        // Bowling, in the order they first bowled.
        std::vector<int> bowlers;
        for (const auto& r : inn.Balls())
            if (r.bowler >= 0 && std::find(bowlers.begin(), bowlers.end(), r.bowler) == bowlers.end())
                bowlers.push_back(r.bowler);
        for (int p : bowlers) {
            const BowlerCard& b = inn.Bowler(p);
            BowlingLine l;
            l.player = p;
            l.name = fld.xi[static_cast<size_t>(p)].name;
            l.balls = b.legalBalls;
            l.overs = OversString(b.legalBalls, bpo);
            l.maidens = b.maidens;
            l.runs = b.runs;
            l.wickets = b.wickets;
            l.wides = b.wides;
            l.noBalls = b.noBalls;
            l.economy = b.legalBalls ? Round2(b.runs * static_cast<double>(bpo) / b.legalBalls) : 0;
            c.bowling.push_back(l);
        }
        // Fall of wickets and partnerships from the ball log.
        Partnership cur;
        bool open = false;
        for (const auto& r : inn.Balls()) {
            if (!open) {
                cur = Partnership{};
                cur.wicket = static_cast<int>(c.partnerships.size()) + 1;
                cur.batterA = r.striker;
                cur.batterB = r.nonStriker;
                open = true;
            }
            // A new pair without a wicket (e.g. retired hurt): start a new stand.
            const bool samePair = (r.striker == cur.batterA && r.nonStriker == cur.batterB) ||
                                  (r.striker == cur.batterB && r.nonStriker == cur.batterA);
            if (!samePair) {
                cur.unbroken = false;
                c.partnerships.push_back(cur);
                cur = Partnership{};
                cur.wicket = static_cast<int>(c.partnerships.size()) + 1;
                cur.batterA = r.striker;
                cur.batterB = r.nonStriker;
            }
            cur.runs += r.runsToTeam;
            if (r.legal) cur.balls++;
            if (r.striker == cur.batterA) cur.runsA += r.runsToBatter;
            else cur.runsB += r.runsToBatter;
            if (r.dismissedBatter >= 0) {
                FallOfWicket f;
                f.wicket = r.wicketsAfter;
                f.score = r.totalAfter;
                f.batter = r.dismissedBatter;
                f.name = bat.xi[static_cast<size_t>(r.dismissedBatter)].name;
                f.over = OverOf(r);
                c.fallOfWickets.push_back(f);
                c.partnerships.push_back(cur);
                open = false;
            }
        }
        if (open) {
            cur.unbroken = true;
            c.partnerships.push_back(cur);
        }
        for (auto& p : c.partnerships) {
            if (p.batterA >= 0) p.nameA = bat.xi[static_cast<size_t>(p.batterA)].name;
            if (p.batterB >= 0) p.nameB = bat.xi[static_cast<size_t>(p.batterB)].name;
        }
        out.push_back(c);
    }
    return out;
}

Status CheckScorecard(const Match& m) {
    const auto cards = BuildScorecard(m);
    for (size_t k = 0; k < cards.size(); ++k) {
        const InningsCard& c = cards[k];
        const Innings& inn = m.AllInnings()[k];
        auto fail = [&](const std::string& what) {
            return Status::Err(c.team + " innings " + std::to_string(c.index + 1) + ": " + what);
        };
        int batRuns = 0, fours = 0, sixes = 0, outs = 0;
        for (const auto& b : c.batting) {
            batRuns += b.runs;
            fours += b.fours;
            sixes += b.sixes;
            outs += b.out ? 1 : 0;
        }
        if (batRuns + c.extrasTotal != c.total) return fail("batters' runs + extras != total");
        int bowlRuns = 0, bowlBalls = 0, bowlWkts = 0;
        for (const auto& b : c.bowling) {
            bowlRuns += b.runs;
            bowlBalls += b.balls;
            bowlWkts += b.wickets;
        }
        if (bowlRuns + c.extras.byes + c.extras.legByes + c.extras.penalty != c.total)
            return fail("bowlers' runs + byes + leg byes + penalties != total");
        if (bowlBalls != c.legalBalls) return fail("bowlers' balls != legal balls");
        if (outs != c.wickets) return fail("batters out != wickets");
        if (static_cast<int>(c.fallOfWickets.size()) != c.wickets) return fail("fall of wickets count != wickets");
        int prevScore = 0;
        for (size_t i = 0; i < c.fallOfWickets.size(); ++i) {
            if (c.fallOfWickets[i].wicket != static_cast<int>(i) + 1) return fail("fall of wickets out of order");
            if (c.fallOfWickets[i].score < prevScore) return fail("fall of wickets score went down");
            prevScore = c.fallOfWickets[i].score;
        }
        // Boundaries, credited wickets and maidens re-derived from the ball log.
        int logFours = 0, logSixes = 0, credited = 0, wides = 0, noBalls = 0;
        std::map<std::pair<int, int>, std::pair<int, int>> overs;  // (bowler, over) -> (legal balls, runs charged)
        for (const auto& r : inn.Balls()) {
            const Delivery& d = r.delivery;
            const bool offBat = d.extra == ExtraType::None || (d.extra == ExtraType::NoBall && d.noBallRunsAs == ExtraType::None);
            if (d.boundary && offBat) (d.runs == 6 ? logSixes : logFours)++;
            if (r.dismissedBatter >= 0 && CreditedToBowler(d.dismissal)) ++credited;
            if (d.extra == ExtraType::Wide) ++wides;
            if (d.extra == ExtraType::NoBall) ++noBalls;
        }
        if (logFours != fours || logSixes != sixes) return fail("4s / 6s don't match the ball log");
        if (credited != bowlWkts) return fail("bowlers' wickets != wickets credited to bowlers");
        int cardWides = 0, cardNoBalls = 0;
        for (const auto& b : c.bowling) cardWides += b.wides, cardNoBalls += b.noBalls;
        if (cardWides != wides || cardNoBalls != noBalls) return fail("wides / no-balls don't match the ball log");
        int pRuns = 0, pBalls = 0;
        for (const auto& p : c.partnerships) pRuns += p.runs, pBalls += p.balls;
        if (pRuns + c.extras.penalty != c.total && pRuns != c.total) return fail("partnerships don't add up to the total");
        if (pBalls != c.legalBalls) return fail("partnership balls != legal balls");
        for (const auto& b : c.batting)
            if (b.balls > 0 && b.strikeRate != Round1(100.0 * b.runs / b.balls)) return fail("strike rate wrong");
    }
    return Status::Ok();
}

}  // namespace cricket
