// cricket_calibration: plays many complete computer-v-computer matches with
// the ball physics and checks that the results look like real cricket:
// formats (T20, ODI, Test), pitch types, pace v spin, strong v weak teams.
// Prints a table and exits non-zero if a value leaves its sanity band.
// The bands are wide on purpose: this is a simplified, calibrated model, and
// the "real" figures are approximate international averages.
//
// Usage: cricket_calibration [--quick] [--all-formats]
// The game is T20 only; --all-formats also checks the ODI / Test engine.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cricket/BallOutcome.h"
#include "cricket/Scorecard.h"
#include "cricket/TeamFactory.h"

using namespace cricket;

namespace {

struct Stats {
    double innings = 0, runs = 0, wickets = 0, balls = 0, dots = 0, fours = 0, sixes = 0, extras = 0;
    double paceBalls = 0, paceRuns = 0, paceWkts = 0, spinBalls = 0, spinRuns = 0, spinWkts = 0;
    int matches = 0, winsA = 0, scoreProblems = 0;
    int runOuts = 0, stumpings = 0, lbws = 0, bowled = 0, caught = 0;
    double PerInn(double v) const { return innings ? v / innings : 0; }
};

Team Scaled(Team t, double factor) {
    for (auto& p : t.xi) {
        p.batting = static_cast<int>(p.batting * factor);
        p.bowling = static_cast<int>(p.bowling * factor);
        p.fielding = static_cast<int>(p.fielding * factor);
    }
    return t;
}

void Play(Stats& st, MatchRules rules, Team a, Team b, uint64_t seed, const std::string& pitch) {
    MatchSession s(std::move(rules), {a, b}, seed);
    s.RunAi();
    BallOutcome o(seed * 13 + 5);
    o.conditions.pitchName = pitch;
    o.conditions.pitch = phys::Pitch::ByName(pitch);
    o.conditions.pitch.cracks = seed | 1;
    QuickSim perception(seed + 3);
    for (int guard = 0; guard < 30000 && s.WaitingFor().what == "delivery"; ++guard) {
        o.PlayComputerBall(s, &perception);
        s.RunAi();
        if (s.WaitingFor().what == "interval") s.ResumeAfterInterval();
        s.RunAi();
    }
    const Match& m = s.GetMatch();
    st.matches++;
    if (m.Result().kind == MatchResult::Kind::Win && m.Result().winner == 0) st.winsA++;
    if (!CheckScorecard(m).ok || !CheckResult(m).ok) st.scoreProblems++;
    for (const auto& inn : m.AllInnings()) {
        st.innings++;
        st.runs += inn.Total();
        st.wickets += inn.Wickets();
        st.balls += inn.LegalBalls();
        st.extras += inn.GetExtras().Total();
        const Team& fld = m.GetTeam(1 - inn.BattingSide());
        for (const auto& r : inn.Balls()) {
            const Delivery& d = r.delivery;
            if (r.legal && r.runsToTeam == 0 && d.dismissal == Dismissal::None) st.dots++;
            const bool offBat = d.extra == ExtraType::None || (d.extra == ExtraType::NoBall && d.noBallRunsAs == ExtraType::None);
            if (d.boundary && offBat) (d.runs == 6 ? st.sixes : st.fours)++;
            switch (d.dismissal) {
                case Dismissal::RunOut: st.runOuts++; break;
                case Dismissal::Stumped: st.stumpings++; break;
                case Dismissal::Lbw: st.lbws++; break;
                case Dismissal::Bowled: st.bowled++; break;
                case Dismissal::Caught: st.caught++; break;
                default: break;
            }
            if (r.bowler < 0) continue;
            const bool spin = fld.xi[static_cast<size_t>(r.bowler)].bowls == BowlingKind::Spin;
            (spin ? st.spinBalls : st.paceBalls) += r.legal ? 1 : 0;
            (spin ? st.spinRuns : st.paceRuns) += r.runsToTeam;
            if (r.dismissedBatter >= 0 && CreditedToBowler(d.dismissal)) (spin ? st.spinWkts : st.paceWkts)++;
        }
    }
}

int failures = 0;
void Band(const char* what, double v, double lo, double hi) {
    const bool ok = v >= lo && v <= hi;
    if (!ok) ++failures;
    std::printf("  %-34s %8.2f   [%g .. %g] %s\n", what, v, lo, hi, ok ? "" : "  <-- OUT OF BAND");
}

void Report(const char* title, const Stats& s) {
    std::printf("%s: %d matches\n", title, s.matches);
    std::printf("  per innings: %.1f runs, %.1f wkts, %.1f overs, RR %.2f, dots %.0f%%, 4s %.1f, 6s %.1f, extras %.1f\n",
                s.PerInn(s.runs), s.PerInn(s.wickets), s.PerInn(s.balls) / 6, s.runs / s.balls * 6,
                100 * s.dots / s.balls, s.PerInn(s.fours), s.PerInn(s.sixes), s.PerInn(s.extras));
    const double w = s.caught + s.bowled + s.lbws + s.runOuts + s.stumpings;
    std::printf("  dismissals: caught %.0f%%, bowled %.0f%%, lbw %.0f%%, run out %.0f%%, stumped %.0f%%\n",
                100 * s.caught / w, 100 * s.bowled / w, 100 * s.lbws / w, 100 * s.runOuts / w, 100 * s.stumpings / w);
    std::printf("  pace: econ %.2f, strike rate %.1f | spin: econ %.2f, strike rate %.1f\n",
                s.paceRuns / s.paceBalls * 6, s.paceWkts ? s.paceBalls / s.paceWkts : 0, s.spinRuns / s.spinBalls * 6,
                s.spinWkts ? s.spinBalls / s.spinWkts : 0);
    if (s.scoreProblems) std::printf("  SCORECARD PROBLEMS: %d\n", s.scoreProblems);
}

}  // namespace

int main(int argc, char** argv) {
    bool quick = false, allFormats = false;
    for (int i = 1; i < argc; ++i) {
        quick |= std::strcmp(argv[i], "--quick") == 0;
        allFormats |= std::strcmp(argv[i], "--all-formats") == 0;
    }
    const int nT20 = quick ? 20 : 60, nOdi = quick ? 6 : 16, nTest = quick ? 2 : 6, nPitch = quick ? 10 : 30;
    auto all = NationalTeams();
    Stats t20, odi, test;
    for (int i = 0; i < nT20; ++i) Play(t20, MatchRules::T20(), all[i % 10], all[(i + 3) % 10], 1000 + i, "standard");
    for (int i = 0; allFormats && i < nOdi; ++i) Play(odi, MatchRules::ODI(), all[i % 10], all[(i + 3) % 10], 2000 + i, "standard");
    for (int i = 0; allFormats && i < nTest; ++i) Play(test, MatchRules::Test(), all[i % 10], all[(i + 3) % 10], 3000 + i, "standard");
    Report("T20", t20);
    Band("T20 runs per innings", t20.PerInn(t20.runs), 135, 200);
    Band("T20 wickets per innings", t20.PerInn(t20.wickets), 4.0, 8.5);
    Band("T20 dot-ball %", 100 * t20.dots / t20.balls, 28, 50);
    Band("T20 fours per innings", t20.PerInn(t20.fours), 9, 22);
    Band("T20 sixes per innings", t20.PerInn(t20.sixes), 2.5, 11);
    if (allFormats) {
        Report("ODI", odi);
        Band("ODI runs per innings", odi.PerInn(odi.runs), 200, 330);
        Band("ODI wickets per innings", odi.PerInn(odi.wickets), 5, 9.5);
        Report("Test", test);
        Band("Test run rate", test.runs / test.balls * 6, 2.5, 4.2);
        Band("Test runs per wicket", test.runs / test.wickets, 18, 45);
    }
    // Pitches (T20).
    std::printf("Pitches (T20, %d matches each):\n", nPitch);
    double flatRuns = 0, greenRuns = 0, dustySpinShare = 0, greenSpinShare = 0;
    for (const char* p : {"flat", "standard", "green", "dusty", "bouncy"}) {
        Stats s;
        for (int i = 0; i < nPitch; ++i) Play(s, MatchRules::T20(), all[i % 10], all[(i + 3) % 10], 4000 + i, p);
        const double spinShare = s.spinWkts / (s.spinWkts + s.paceWkts);
        std::printf("  %-8s runs/inn %.1f, wkts/inn %.1f, spin share of wickets %.0f%%, spin econ %.2f, pace econ %.2f\n", p,
                    s.PerInn(s.runs), s.PerInn(s.wickets), 100 * spinShare, s.spinRuns / s.spinBalls * 6,
                    s.paceRuns / s.paceBalls * 6);
        if (!std::strcmp(p, "flat")) flatRuns = s.PerInn(s.runs);
        if (!std::strcmp(p, "green")) greenRuns = s.PerInn(s.runs), greenSpinShare = spinShare;
        if (!std::strcmp(p, "dusty")) dustySpinShare = spinShare;
    }
    Band("flat minus green runs per innings", flatRuns - greenRuns, 0, 80);
    Band("spin share: dusty minus green (pts)", 100 * (dustySpinShare - greenSpinShare), 0, 60);
    // Skill: a strong side against a weakened copy of a side.
    Stats skill;
    for (int i = 0; i < nPitch; ++i)
        Play(skill, MatchRules::T20(), Scaled(all[i % 10], 1.0), Scaled(all[(i + 3) % 10], 0.7), 5000 + i, "standard");
    std::printf("Strong v weak (T20): strong side won %d of %d\n", skill.winsA, skill.matches);
    Band("strong side win %", 100.0 * skill.winsA / skill.matches, 60, 100);
    Band("scorecard problems (all matches)", t20.scoreProblems + odi.scoreProblems + test.scoreProblems + skill.scoreProblems, 0, 0);
    std::printf(failures ? "\n%d value(s) out of band\n" : "\nall values inside their bands\n", failures);
    return failures ? 1 : 0;
}
