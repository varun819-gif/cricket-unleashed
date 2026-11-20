#include "cricket/AutoCaptain.h"

#include <algorithm>

namespace cricket {

const char* ToString(Difficulty d) {
    switch (d) {
        case Difficulty::Beginner: return "Beginner";
        case Difficulty::Easy: return "Easy";
        case Difficulty::Medium: return "Medium";
        case Difficulty::Hard: return "Hard";
        case Difficulty::WorldClass: return "World Class";
        case Difficulty::Adaptive: return "Adaptive";
    }
    return "?";
}

namespace {
int OversLeft(const Match& m) {
    const auto& r = m.Rules();
    if (r.oversPerInnings == 0) return 1000;
    return r.oversPerInnings - m.Current().CompletedOvers();
}
}  // namespace

TossChoice AutoCaptain::ChooseAtToss(const MatchRules& r) {
    // Tests: usually bat first. Limited overs: slight preference to chase.
    double batBias = r.format == Format::Test ? 0.75 : 0.4;
    return std::uniform_real_distribution<double>(0, 1)(rng_) < batBias ? TossChoice::Bat : TossChoice::Bowl;
}

std::pair<int, int> AutoCaptain::ChooseOpeners(const Match&) const { return {0, 1}; }

int AutoCaptain::ChooseNextBatter(const Match& m) const {
    const auto& inn = m.Current();
    for (int i = 0; i < m.Rules().squadSize; ++i)
        if (!inn.Batter(i).batted) return i;
    return -1;
}

int AutoCaptain::ChooseBowler(const Match& m) {
    const auto& inn = m.Current();
    const Team& t = m.GetTeam(m.FieldingSide());
    const int left = OversLeft(m);
    const bool limited = m.Rules().oversPerInnings > 0;
    std::uniform_real_distribution<double> jitter(0.0, 8.0);

    int best = -1;
    double bestScore = -1e9;
    for (int pass = 0; pass < 2 && best < 0; ++pass) {
        for (int i = 0; i < static_cast<int>(t.xi.size()); ++i) {
            const Player& p = t.xi[i];
            if (pass == 0 && !p.canBowl) continue;
            if (i == t.keeper) continue;
            if (!inn.CanBowlNextOver(i)) continue;
            const int overs = inn.Bowler(i).legalBalls / m.Rules().ballsPerOver;
            double s = p.bowling + p.form * 0.25 - overs * (limited ? 2.0 : 0.8) + jitter(rng_);
            if (limited && left <= 4 && p.bowls == BowlingKind::Pace) s += 8;  // death overs
            if (limited && m.Rules().IsPowerplay(inn.CurrentOverNumber()) && p.bowls == BowlingKind::Pace) s += 5;
            if (!limited && inn.CompletedOvers() > 40 && p.bowls == BowlingKind::Spin) s += 6;  // old ball
            // Protect the quota: in limited overs keep the best bowlers for later.
            if (limited && left > 8 && overs >= m.Rules().maxOversPerBowler - 1) s -= 10;
            if (s > bestScore) {
                bestScore = s;
                best = i;
            }
        }
    }
    return best;
}

FieldPreset AutoCaptain::ChooseFieldPreset(const Match& m, BowlingKind kind) const {
    const auto& inn = m.Current();
    const auto& r = m.Rules();
    const int over = inn.CurrentOverNumber();
    if (r.oversPerInnings == 0) {  // Test
        if (kind == BowlingKind::Spin) return FieldPreset::Spin;
        return inn.CompletedOvers() < 25 || inn.Wickets() >= 7 ? FieldPreset::Attacking : FieldPreset::Pace;
    }
    if (r.IsPowerplay(over)) return kind == BowlingKind::Spin ? FieldPreset::Spin : FieldPreset::Powerplay;
    if (OversLeft(m) <= 4) return FieldPreset::DeathOvers;
    if (kind == BowlingKind::Spin) return FieldPreset::Spin;
    // Chasing side scoring quickly -> protect the boundary.
    double rrr = inn.RequiredRunRate();
    if (rrr > 0 && rrr < 6.0 && inn.Wickets() >= 6) return FieldPreset::Attacking;
    return FieldPreset::Defensive;
}

FieldSetup AutoCaptain::ChooseLegalField(const Match& m, BowlingKind kind) const {
    FieldContext ctx{&m.Rules(), m.Current().CurrentOverNumber()};
    for (FieldPreset p : {ChooseFieldPreset(m, kind), FieldPreset::Balanced, FieldPreset::Powerplay}) {
        FieldSetup f = FieldSetup::FromPreset(p);
        if (ValidateField(f, ctx).status) return f;
    }
    return FieldSetup::FromPreset(FieldPreset::Powerplay);  // legal in every phase
}

bool AutoCaptain::ShouldEnforceFollowOn(const Match&) {
    // Usually yes; tired bowlers sometimes say no.
    return std::uniform_real_distribution<double>(0, 1)(rng_) < 0.8;
}

bool AutoCaptain::ShouldDeclare(const Match& m) const {
    const auto& inn = m.Current();
    if (m.Rules().oversPerInnings != 0) return false;
    if (inn.Index() == 2) {  // third innings: declare with a big lead
        int lead = m.RunsForSide(inn.BattingSide()) - m.RunsForSide(1 - inn.BattingSide());
        return lead >= 350;
    }
    return inn.Total() >= 600;
}

Delivery QuickSim::Next(const Match& m) {
    const auto& inn = m.Current();
    const Team& bat = m.GetTeam(m.BattingSide());
    const Team& fld = m.GetTeam(m.FieldingSide());
    const Player& b = bat.xi[inn.Striker()];
    const Player& w = fld.xi[inn.CurrentBowler()];
    const bool freeHit = inn.NextBallIsFreeHit();
    const auto& r = m.Rules();

    // Aggression: 0 (Test, defending) .. 1 (death overs / big chase)
    double aggression = 0.35;
    if (r.oversPerInnings > 0) {
        int left = r.oversPerInnings - inn.CompletedOvers();
        if (r.IsPowerplay(inn.CurrentOverNumber())) aggression = 0.55;
        if (left <= 4) aggression = 0.85;
        double rrr = inn.RequiredRunRate();
        if (rrr > 9) aggression = std::min(1.0, aggression + 0.2);
    } else {
        aggression = 0.15;
    }
    if (freeHit) aggression = 1.0;

    Delivery d;
    double x = U();
    if (x < 0.025) {
        d.extra = ExtraType::Wide;
        if (U() < 0.1) d.runs = 4, d.boundary = true;
        return d;
    }
    if (x < 0.035) {
        d.extra = ExtraType::NoBall;
        // fall through to a normal shot off the no-ball
    }

    // Skill difference in [-1, 1]: positive favours the bowler.
    double edge = (w.bowling - b.batting) / 100.0;
    double pWicket = std::clamp(0.03 + 0.04 * edge + 0.03 * aggression, 0.008, 0.12);
    if (r.oversPerInnings == 0) pWicket *= 0.6;  // Test batters value their wicket
    if (d.extra == ExtraType::None && !freeHit && U() < pWicket) {
        double t = U();
        bool spin = w.bowls == BowlingKind::Spin;
        if (t < 0.22) d.dismissal = Dismissal::Bowled;
        else if (t < 0.70) d.dismissal = Dismissal::Caught;
        else if (t < 0.86) d.dismissal = Dismissal::Lbw;
        else if (t < 0.90 && spin) d.dismissal = Dismissal::Stumped;
        else if (t < 0.91) d.dismissal = Dismissal::HitWicket;
        else {
            d.dismissal = Dismissal::RunOut;
            d.strikerOut = U() < 0.6;
            d.runs = U() < 0.5 ? 0 : 1;
        }
        if (d.dismissal == Dismissal::Caught) d.fielder = static_cast<int>(U() * 11);
        return d;
    }

    if (d.extra == ExtraType::None && U() < 0.03) {
        d.extra = U() < 0.6 ? ExtraType::LegBye : ExtraType::Bye;
        d.runs = U() < 0.85 ? 1 : 4;
        d.boundary = d.runs == 4;
        return d;
    }

    double skill = (b.batting - 50) / 200.0;  // -0.25 .. +0.25
    double p6 = 0.01 + 0.07 * aggression + 0.03 * skill;
    double p4 = 0.06 + 0.09 * aggression + 0.04 * skill;
    double p3 = 0.01;
    double p2 = 0.07;
    double p1 = 0.32 + 0.05 * aggression;
    double y = U();
    if ((y -= p6) < 0) d.runs = 6, d.boundary = true;
    else if ((y -= p4) < 0) d.runs = 4, d.boundary = true;
    else if ((y -= p3) < 0) d.runs = 3;
    else if ((y -= p2) < 0) d.runs = 2;
    else if ((y -= p1) < 0) d.runs = 1;
    else d.runs = 0;
    return d;
}

}  // namespace cricket
