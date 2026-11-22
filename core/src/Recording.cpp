#include "cricket/Recording.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cricket {

namespace {

std::string Esc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += ch;
    }
    return o;
}

std::string Num(double v, int decimals = 3) {
    if (!std::isfinite(v)) return "null";
    char b[40];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    // Trim trailing zeros ("1.500" -> "1.5", "2.000" -> "2").
    std::string s = b;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

TracePoint Pt(const phys::Sample& s) {
    return {static_cast<float>(s.t), static_cast<float>(s.p.x), static_cast<float>(s.p.y), static_cast<float>(s.p.z)};
}

std::vector<TracePoint> Decimate(const std::vector<phys::Sample>& in, double stepS) {
    std::vector<TracePoint> out;
    double next = -1;
    for (const auto& s : in) {
        if (s.t + 1e-9 >= next) {
            out.push_back(Pt(s));
            next = s.t + stepS;
        }
    }
    if (!in.empty() && (out.empty() || out.back().t != static_cast<float>(in.back().t))) out.push_back(Pt(in.back()));
    return out;
}

std::string PointsJson(const std::vector<TracePoint>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) o += ",";
        o += "[" + Num(v[i].t, 3) + "," + Num(v[i].x, 3) + "," + Num(v[i].y, 3) + "," + Num(v[i].z, 3) + "]";
    }
    return o + "]";
}

std::string PlayerName(const Match& m, int side, int idx) {
    const Team& t = m.GetTeam(side);
    return idx >= 0 && idx < static_cast<int>(t.xi.size()) ? t.xi[static_cast<size_t>(idx)].name : std::string("?");
}

std::string OverBall(const DeliveryRecord& d) {
    // Commentary style: over.ball of this delivery (an extra keeps the count).
    const int ball = d.legal ? d.ballInOver : d.ballInOver + 1;
    return std::to_string(d.over) + "." + std::to_string(std::max(1, ball));
}

}  // namespace

const char* ToString(HighlightKind k) {
    switch (k) {
        case HighlightKind::Wicket: return "wicket";
        case HighlightKind::Six: return "six";
        case HighlightKind::Four: return "four";
        case HighlightKind::Review: return "review";
        case HighlightKind::Fifty: return "fifty";
        case HighlightKind::Hundred: return "hundred";
        case HighlightKind::Finish: return "finish";
        case HighlightKind::Maiden: return "maiden";
        case HighlightKind::CloseRunOut: return "close-run-out";
    }
    return "?";
}

BallTrace TraceOf(const BallReport& r) {
    BallTrace t;
    t.speedKph = r.spec.speedKph;
    t.releaseHeight = r.spec.releaseHeight;
    t.pitched = r.path.pitched;
    if (r.path.pitched)
        t.bounce = {static_cast<float>(r.path.pitchTime), static_cast<float>(r.path.pitchPoint.x),
                    static_cast<float>(r.path.pitchPoint.y), static_cast<float>(r.path.pitchPoint.z)};
    // The flight up to the batter (popping crease + a little), every 10 ms.
    std::vector<phys::Sample> flight;
    for (const auto& s : r.path.samples) {
        flight.push_back(s);
        if (s.p.y < -0.5) break;
    }
    t.flight = Decimate(flight, 0.010);
    t.shot = r.shot.provided ? r.shot.shot : "leave";
    t.contact = phys::ToString(r.bat.contact);
    t.exitKph = r.bat.exitSpeedKph;
    t.afterShot = Decimate(r.field.path, 0.050);
    t.fielder = r.field.fielder;
    t.caught = r.field.caught;
    t.summary = r.summary;
    t.runsCalled = r.shot.runsCalled;
    t.runOutChance = r.runOutChance;
    t.stumpingChance = r.stumpingChance;
    return t;
}

std::vector<const DeliveryRecord*> AllDeliveries(const Match& m) {
    std::vector<const DeliveryRecord*> out;
    for (const auto& inn : m.AllInnings())
        for (const auto& d : inn.Balls()) out.push_back(&d);
    return out;
}

std::vector<Highlight> FindHighlights(const Match& m, const std::vector<BallTrace>* traces) {
    std::vector<Highlight> out;
    const auto balls = AllDeliveries(m);
    const auto& inns = m.AllInnings();
    const bool limited = m.Rules().oversPerInnings > 0;
    auto add = [&](HighlightKind k, size_t i, int imp, const std::string& text) {
        const DeliveryRecord& d = *balls[i];
        Highlight h;
        h.kind = k;
        h.ballIndex = static_cast<int>(i);
        h.inningsIndex = d.inningsIndex;
        h.over = d.over;
        h.ballInOver = d.ballInOver;
        h.importance = std::min(100, imp);
        h.text = text;
        out.push_back(h);
    };
    for (size_t i = 0; i < balls.size(); ++i) {
        const DeliveryRecord& d = *balls[i];
        const int bat = inns.at(static_cast<size_t>(d.inningsIndex)).BattingSide();
        const int bowlSide = 1 - bat;
        const std::string score = " (" + std::to_string(d.totalAfter) + "/" + std::to_string(d.wicketsAfter) + ")";
        const std::string where = OverBall(d) + " ";
        // Late in a chase, everything matters more.
        const bool chase = d.inningsIndex == 1 && limited;
        const bool lateChase = chase && d.over >= m.Rules().oversPerInnings - 2;
        const int bonus = lateChase ? 10 : 0;
        if (d.delivery.dismissal != Dismissal::None) {
            const int who = d.dismissedBatter >= 0 ? d.dismissedBatter : d.striker;
            std::string how = ToString(d.delivery.dismissal);
            if (d.delivery.dismissal != Dismissal::RunOut) how += ", bowled by " + PlayerName(m, bowlSide, d.bowler);
            add(HighlightKind::Wicket, i, 80 + bonus, where + "WICKET! " + PlayerName(m, bat, who) + " " + how + score);
        } else if (d.delivery.boundary && d.delivery.runs == 6 && d.delivery.extra == ExtraType::None) {
            add(HighlightKind::Six, i, 60 + bonus, where + "SIX! " + PlayerName(m, bat, d.striker) + score);
        } else if (d.delivery.boundary && d.delivery.runs == 4 && d.delivery.extra == ExtraType::None) {
            add(HighlightKind::Four, i, 40 + bonus, where + "FOUR! " + PlayerName(m, bat, d.striker) + score);
        }
        if (d.batterHundred) add(HighlightKind::Hundred, i, 90, where + "HUNDRED for " + PlayerName(m, bat, d.striker));
        else if (d.batterFifty) add(HighlightKind::Fifty, i, 70, where + "FIFTY for " + PlayerName(m, bat, d.striker));
        if (d.maiden) add(HighlightKind::Maiden, i, 20, where + "Maiden over by " + PlayerName(m, bowlSide, d.bowler));
        if (traces && i < traces->size() && (*traces)[i].runOutChance && d.delivery.dismissal == Dismissal::None)
            add(HighlightKind::CloseRunOut, i, 50 + bonus, where + "Close run out chance - not out" + score);
        if (i + 1 == balls.size() && m.Phase() == MatchPhase::Complete)
            add(HighlightKind::Finish, i, 85 + bonus, where + "The finish: " + m.Result().summary);
    }
    // Reviews (DRS): the delivery they belong to.
    for (const auto& dec : m.Decisions()) {
        if (!dec.reviewed) continue;
        for (size_t i = 0; i < balls.size(); ++i) {
            const DeliveryRecord& d = *balls[i];
            const int before = d.legal ? d.ballInOver - 1 : d.ballInOver;
            if (d.inningsIndex == dec.inningsIndex && d.over == dec.over && before == dec.ballInOver) {
                const bool overturned = dec.result.finalOut != dec.onFieldOut;
                add(HighlightKind::Review, i, overturned ? 75 : 60,
                    OverBall(d) + " REVIEW: " + (dec.result.finalOut ? "OUT" : "NOT OUT") +
                        (overturned ? " (decision overturned)" : " (decision stands)"));
                break;
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Highlight& a, const Highlight& b) { return a.ballIndex < b.ballIndex; });
    return out;
}

std::vector<Highlight> HighlightPackage(const std::vector<Highlight>& all, size_t maxClips) {
    // Greedy: take the most important moment left, but each clip of a kind
    // already taken makes the next one of that kind a little less attractive
    // (a highlights video wants wickets AND sixes AND fours). The finish and
    // hundreds are never marked down. One clip per ball.
    std::vector<bool> taken(all.size(), false), ballUsed;
    std::vector<int> perKind(16, 0);
    std::vector<size_t> pick;
    while (pick.size() < maxClips) {
        int best = -1, bestScore = -1000;
        for (size_t k = 0; k < all.size(); ++k) {
            if (taken[k]) continue;
            const auto b = static_cast<size_t>(all[k].ballIndex);
            if (b < ballUsed.size() && ballUsed[b]) continue;
            const auto kind = static_cast<size_t>(all[k].kind);
            const bool special = all[k].kind == HighlightKind::Finish || all[k].kind == HighlightKind::Hundred;
            const int score = all[k].importance - (special ? 0 : 8 * perKind[kind]);
            if (score > bestScore) best = static_cast<int>(k), bestScore = score;  // earlier wins ties
        }
        if (best < 0) break;
        const auto k = static_cast<size_t>(best);
        taken[k] = true;
        const auto b = static_cast<size_t>(all[k].ballIndex);
        if (ballUsed.size() <= b) ballUsed.resize(b + 1, false);
        ballUsed[b] = true;
        ++perKind[static_cast<size_t>(all[k].kind)];
        pick.push_back(k);
    }
    std::sort(pick.begin(), pick.end(), [&](size_t a, size_t b) { return all[a].ballIndex < all[b].ballIndex; });
    std::vector<Highlight> out;
    for (size_t k : pick) out.push_back(all[k]);
    return out;
}

std::string MatchRecorder::ToJsonLines(const Match& m, const std::string& conditions) const {
    const auto balls = AllDeliveries(m);
    const bool joined = balls.size() == traces_.size();
    std::string o;
    o += "{\"type\":\"match\",\"version\":1,\"teams\":[\"" + Esc(m.GetTeam(0).name) + "\",\"" + Esc(m.GetTeam(1).name) +
         "\"],\"oversPerInnings\":" + std::to_string(m.Rules().oversPerInnings) + ",\"result\":\"" +
         Esc(m.Result().summary) + "\",\"conditions\":\"" + Esc(conditions) + "\",\"balls\":" +
         std::to_string(balls.size()) + ",\"physicsTraces\":" + (joined ? "true" : "false") + "}\n";
    const auto& inns = m.AllInnings();
    for (size_t i = 0; i < balls.size(); ++i) {
        const DeliveryRecord& d = *balls[i];
        const int bat = inns.at(static_cast<size_t>(d.inningsIndex)).BattingSide();
        o += "{\"type\":\"ball\",\"i\":" + std::to_string(i) + ",\"innings\":" + std::to_string(d.inningsIndex) +
             ",\"over\":\"" + OverBall(d) + "\",\"bowler\":\"" + Esc(PlayerName(m, 1 - bat, d.bowler)) +
             "\",\"striker\":\"" + Esc(PlayerName(m, bat, d.striker)) + "\",\"runsToTeam\":" +
             std::to_string(d.runsToTeam) + ",\"runsToBatter\":" + std::to_string(d.runsToBatter) +
             ",\"boundary\":" + (d.delivery.boundary ? "true" : "false") + ",\"legal\":" + (d.legal ? "true" : "false") +
             ",\"extra\":" + std::to_string(static_cast<int>(d.delivery.extra)) + ",\"wicket\":\"" +
             (d.delivery.dismissal == Dismissal::None ? "" : ToString(d.delivery.dismissal)) + "\",\"score\":\"" +
             std::to_string(d.totalAfter) + "/" + std::to_string(d.wicketsAfter) + "\"";
        if (joined) {
            const BallTrace& t = traces_[i];
            o += ",\"physics\":{\"speedKph\":" + Num(t.speedKph, 1) + ",\"variation\":\"" + Esc(t.variation) +
                 "\",\"pitched\":" + (t.pitched ? "true" : "false") + ",\"bounce\":[" + Num(t.bounce.t) + "," +
                 Num(t.bounce.x) + "," + Num(t.bounce.y) + "," + Num(t.bounce.z) + "],\"shot\":\"" + Esc(t.shot) +
                 "\",\"contact\":\"" + Esc(t.contact) + "\",\"exitKph\":" + Num(t.exitKph, 1) +
                 ",\"fielder\":" + std::to_string(t.fielder) + ",\"caught\":" + (t.caught ? "true" : "false") +
                 ",\"runsCalled\":" + std::to_string(t.runsCalled) + ",\"summary\":\"" + Esc(t.summary) +
                 "\",\"flight\":" + PointsJson(t.flight) + ",\"afterShot\":" + PointsJson(t.afterShot) + "}";
        }
        o += "}\n";
    }
    const auto hl = FindHighlights(m, joined ? &traces_ : nullptr);
    const auto pack = HighlightPackage(hl, 12);
    auto list = [&](const std::vector<Highlight>& v) {
        std::string s = "[";
        for (size_t k = 0; k < v.size(); ++k) {
            if (k) s += ",";
            s += "{\"kind\":\"" + std::string(ToString(v[k].kind)) + "\",\"ball\":" + std::to_string(v[k].ballIndex) +
                 ",\"importance\":" + std::to_string(v[k].importance) + ",\"text\":\"" + Esc(v[k].text) + "\"}";
        }
        return s + "]";
    };
    o += "{\"type\":\"highlights\",\"all\":" + list(hl) + ",\"package\":" + list(pack) + "}\n";
    return o;
}

}  // namespace cricket
