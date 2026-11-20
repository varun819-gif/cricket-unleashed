#include "cricket/Commentary.h"

#include <cstdio>

namespace cricket {

namespace {

using Pool = std::map<std::string, std::vector<std::string>>;

const Pool& English() {
    static const Pool p = {
        {"dot", {"Good ball from {bowl}, {bat} defends.", "Beaten! {bat} plays and misses.",
                 "Dot ball. {bowl} keeping it tight.", "Straight to the fielder, no run."}},
        {"single", {"Pushed into the gap, they take a single.", "{bat} works it away for one.",
                    "Quick single, good running.", "Dropped at his feet and they scamper through."}},
        {"runs", {"{runs} runs, well run!", "Into the gap, they come back for {runs}.",
                  "Good placement from {bat}, {runs} more."}},
        {"four", {"FOUR! Cracking shot from {bat}!", "That races away to the boundary!",
                  "Timed to perfection, four runs.", "Through the gap and nobody stops that. FOUR!"}},
        {"six", {"SIX! That is huge from {bat}!", "Into the stands! What a hit!",
                 "Maximum! {bowl} can only watch it sail.", "Gone all the way! SIX!"}},
        {"wide", {"Wide called, too far from the batter.", "That's a wide, {bowl} strays down the leg side.",
                  "Extra run, the umpire signals wide."}},
        {"noball", {"No-ball! Overstepped.", "No-ball called by the umpire.", "Front foot no-ball from {bowl}."}},
        {"freehit", {"Free hit coming up!", "And that means a free hit next ball."}},
        {"bye", {"{runs} byes, the keeper couldn't hold it.", "Past the keeper, {runs} byes."}},
        {"legbye", {"Off the pads, {runs} leg-byes.", "{runs} leg-byes, it came off the thigh pad."}},
        {"wicket.bowled", {"BOWLED HIM! Stumps shattered!", "Through the gate! {bat} is bowled by {bowl}!",
                           "Clean bowled! What a delivery!"}},
        {"wicket.caught", {"OUT! Caught! {bat} has to go.", "In the air... and taken! {bowl} strikes!",
                           "Edged and caught! Big wicket."}},
        {"wicket.lbw", {"Huge appeal... and the finger goes up! LBW!", "Trapped in front! {bat} is LBW.",
                        "Plumb! That's out leg before wicket."}},
        {"wicket.stumped", {"Stumped! Lightning quick work by the keeper!", "Out of his crease and stumped!"}},
        {"wicket.runout", {"RUN OUT! Direct hit!", "Mix-up in the middle and it's a run out!",
                           "Brilliant fielding, that's a run out."}},
        {"wicket.other", {"That's out! {bat} has to walk back."}},
        {"fifty", {"Fifty for {bat}! Raises the bat to the crowd.", "Half-century! Fine innings from {bat}."}},
        {"hundred", {"HUNDRED! What a moment for {bat}!", "Century! The crowd is on its feet for {bat}!"}},
        {"maiden", {"A maiden over from {bowl}. Superb control.", "Maiden! Pressure building."}},
        {"over", {"End of the over. Score {score}.", "That's the over. {score}."}},
        {"innings", {"That's the end of the innings. {score}.", "Innings complete at {score}."}},
        {"expert.wicket", {"Big moment. That changes the pressure completely.",
                           "The bowler set that up beautifully over the last few balls.",
                           "Poor shot selection at a crucial time."}},
        {"expert.boundary", {"Look at the head position, perfectly still.", "Picked the length early, that's class.",
                             "That's the release shot the batting side needed."}},
        {"expert.chase", {"They need {need} from {balls} balls now.", "Required rate is {rrr}. Pressure is on."}},
        {"result", {"{summary}! What a contest!", "And that's the match. {summary}."}},
        {"appeal.out", {"Big appeal... and the finger goes up!", "Given out on the field!"}},
        {"appeal.notout", {"Huge appeal, but the umpire says not out.", "Shout turned down by the umpire."}},
        {"review.taken", {"{team} have gone upstairs! They want a review.", "That's a review from {team}."}},
        {"review.overturned", {"The decision is OVERTURNED!", "Overturned! The TV umpire changes it."}},
        {"review.upheld", {"The on-field decision stands. {team} lose a review.", "Review unsuccessful. Decision stays."}},
        {"review.umpirescall", {"Umpire's call! The decision stays, and {team} keep their review.",
                                "So close... umpire's call. Review retained."}},
        {"thirdumpire.out", {"The big screen says OUT!", "Third umpire confirms it. OUT!"}},
        {"thirdumpire.notout", {"NOT OUT says the third umpire!", "The big screen shows not out. Lucky escape!"}},
        {"noball.check", {"Hang on... it's a no-ball! Saved by the front foot.", "No-ball! The bowler overstepped, the wicket doesn't count."}},
        {"expert.review", {"The replays were clear: {reason}.", "Technology decides it: {reason}."}},
        {"call.signalled", {"The umpire signals {call}.", "{call} called by the umpire."}},
        {"call.notsignalled", {"The umpire doesn't call {call}.", "No {call} call from the umpire."}},
        {"call.changed", {"Changed on review! The {call} call is reversed.", "The TV umpire reverses the {call} call."}},
        {"call.stands", {"The {call} call stands.", "Call stays as it was."}},
        {"boundary.check", {"The TV umpire checks the boundary...", "Upstairs to check the rope..."}},
        {"boundary.six", {"Confirmed: that's a SIX!", "It cleared the rope. Six!"}},
        {"boundary.four", {"Just short! Only a four.", "The fielder's foot touched the rope... it's four, not six."}},
        {"penalty", {"Five penalty runs to {team}! {reason}.", "Penalty runs awarded to {team}: {reason}."}},
        {"newball", {"{team} take the new ball.", "The new ball is out for {team}."}},
        {"shortrun", {"One short! The umpire takes a run away.", "Short run called. That run won't count."}},
        {"interval.lunch", {"That's lunch on day {day}. {team} are {score}.", "The players head off for lunch. {team} {score}."}},
        {"interval.tea", {"Tea on day {day}. {team} {score}.", "Time for tea. {team} have reached {score}."}},
        {"interval.dinner", {"The dinner break, day {day}. {team} {score}.", "Off they go for dinner. {team} {score}."}},
        {"interval.stumps", {"Stumps on day {day}! {team} {score}.", "That's the end of day {day}. {team} {score}."}},
        {"interval.innings", {"That's the end of the innings. {team} {score}.", "Innings break. {team} finish on {score}."}},
    };
    return p;
}

const Pool& Hindi() {
    static const Pool p = {
        {"dot", {"{bowl} की बढ़िया गेंद, {bat} ने रोका।", "बीट! {bat} चूक गए।", "डॉट बॉल। कसी हुई गेंदबाज़ी।"}},
        {"single", {"गैप में धकेला, एक रन।", "{bat} ने एक रन चुराया।", "तेज़ सिंगल, अच्छी दौड़।"}},
        {"runs", {"{runs} रन, बढ़िया दौड़!", "गैप में खेला, {runs} रन।"}},
        {"four", {"चौका! {bat} का शानदार शॉट!", "गेंद तेज़ी से बाउंड्री के पार!", "क्या टाइमिंग है, चार रन!"}},
        {"six", {"छक्का! {bat} ने गेंद स्टैंड्स में भेज दी!", "बहुत बड़ा छक्का!", "गेंद आसमान में, और बाहर! छक्का!"}},
        {"wide", {"वाइड, गेंद बल्लेबाज़ से बहुत दूर।", "अंपायर ने वाइड दिया।"}},
        {"noball", {"नो-बॉल! पैर लाइन से बाहर।", "अंपायर ने नो-बॉल दिया।"}},
        {"freehit", {"अगली गेंद फ्री हिट!", "फ्री हिट आ रही है!"}},
        {"bye", {"कीपर से छूटी, {runs} बाई।", "{runs} बाई के रन।"}},
        {"legbye", {"पैड से लगकर {runs} लेग-बाई।", "{runs} लेग-बाई के रन।"}},
        {"wicket.bowled", {"बोल्ड! गिल्लियाँ बिखर गईं!", "{bowl} ने {bat} को बोल्ड कर दिया!"}},
        {"wicket.caught", {"आउट! कैच पकड़ लिया!", "हवा में गेंद... और कैच! {bowl} को विकेट!"}},
        {"wicket.lbw", {"ज़ोरदार अपील... और आउट! एलबीडब्ल्यू!", "{bat} पगबाधा आउट!"}},
        {"wicket.stumped", {"स्टंप्ड! विकेटकीपर की फुर्ती!", "क्रीज़ से बाहर और स्टंप!"}},
        {"wicket.runout", {"रन आउट! सीधा थ्रो!", "दौड़ में गड़बड़, रन आउट!"}},
        {"wicket.other", {"आउट! {bat} को पवेलियन लौटना होगा।"}},
        {"fifty", {"{bat} का अर्धशतक! बल्ला उठाकर अभिवादन।", "पचास रन पूरे, {bat} की बढ़िया पारी।"}},
        {"hundred", {"शतक! {bat} के लिए यादगार पल!", "सौ रन! पूरा स्टेडियम खड़ा है!"}},
        {"maiden", {"{bowl} का मेडन ओवर। कमाल का नियंत्रण।", "मेडन! दबाव बढ़ रहा है।"}},
        {"over", {"ओवर ख़त्म। स्कोर {score}।", "ओवर पूरा, स्कोर {score}।"}},
        {"innings", {"पारी समाप्त। स्कोर {score}।"}},
        {"expert.wicket", {"बड़ा विकेट, अब दबाव पूरी तरह बदल गया है।", "गेंदबाज़ ने पिछली गेंदों से ये प्लान बनाया था।"}},
        {"expert.boundary", {"सिर एकदम स्थिर, लेंथ जल्दी पढ़ ली।", "यही शॉट टीम को चाहिए था।"}},
        {"expert.chase", {"अब {balls} गेंदों में {need} रन चाहिए।", "आवश्यक रन रेट {rrr}, दबाव बढ़ रहा है।"}},
        {"result", {"{summary}! क्या मुकाबला था!"}},
        {"appeal.out", {"ज़ोरदार अपील... और उंगली उठ गई!", "मैदानी अंपायर ने आउट दिया!"}},
        {"appeal.notout", {"बड़ी अपील, लेकिन अंपायर ने नॉट आउट दिया।", "अपील ठुकरा दी गई।"}},
        {"review.taken", {"{team} ने रिव्यू लिया है!", "{team} तीसरे अंपायर के पास गए।"}},
        {"review.overturned", {"फ़ैसला पलट गया!", "तीसरे अंपायर ने फ़ैसला बदल दिया!"}},
        {"review.upheld", {"मैदानी फ़ैसला बरकरार। {team} का एक रिव्यू गया।", "रिव्यू नाकाम रहा।"}},
        {"review.umpirescall", {"अंपायर्स कॉल! फ़ैसला वही, और {team} का रिव्यू बचा रहा।"}},
        {"thirdumpire.out", {"बड़ी स्क्रीन पर आउट!", "तीसरे अंपायर ने आउट दिया!"}},
        {"thirdumpire.notout", {"तीसरे अंपायर का फ़ैसला: नॉट आउट!", "बाल-बाल बचे, नॉट आउट!"}},
        {"noball.check", {"रुकिए... ये नो-बॉल है! विकेट नहीं मिलेगा।"}},
        {"expert.review", {"रीप्ले से साफ़ है: {reason}।", "तकनीक का फ़ैसला: {reason}।"}},
        {"call.signalled", {"अंपायर ने {call} का इशारा किया।"}},
        {"call.notsignalled", {"अंपायर ने {call} नहीं दिया।"}},
        {"call.changed", {"रिव्यू में {call} का फ़ैसला पलट गया!"}},
        {"call.stands", {"{call} का फ़ैसला बरकरार।"}},
        {"boundary.check", {"तीसरे अंपायर बाउंड्री जाँच रहे हैं..."}},
        {"boundary.six", {"पक्का हुआ: छक्का!"}},
        {"boundary.four", {"रस्सी छू गई... छक्का नहीं, चौका!"}},
        {"penalty", {"{team} को पाँच पेनल्टी रन! {reason}।"}},
        {"newball", {"{team} ने नई गेंद ली।"}},
        {"shortrun", {"एक रन छोटा! अंपायर ने एक रन काट दिया।"}},
        {"interval.lunch", {"दिन {day} का लंच। {team} {score}।"}},
        {"interval.tea", {"दिन {day} की चाय। {team} {score}।"}},
        {"interval.dinner", {"दिन {day} का डिनर ब्रेक। {team} {score}।"}},
        {"interval.stumps", {"दिन {day} का खेल ख़त्म! {team} {score}।"}},
        {"interval.innings", {"पारी समाप्त। {team} {score}।"}},
    };
    return p;
}

// Hindi explanations for every DRS reason code.
const char* ReasonHindi(ReasonCode r) {
    switch (r) {
        case ReasonCode::OnField: return "मैदानी अंपायर का फ़ैसला";
        case ReasonCode::FrontFootNoBall: return "फ्रंट-फ़ुट नो-बॉल";
        case ReasonCode::BallTracking: return "बॉल ट्रैकिंग";
        case ReasonCode::UmpiresCall: return "अंपायर्स कॉल";
        case ReasonCode::NoTrackingData: return "बॉल ट्रैकिंग डेटा नहीं, मैदानी फ़ैसला बरकरार";
        case ReasonCode::BallGrounded: return "कैच से पहले गेंद ज़मीन से लगी";
        case ReasonCode::CatchNotConclusive: return "साफ़ कैच साबित नहीं हुआ";
        case ReasonCode::EdgeDetected: return "बल्ले का किनारा लगा";
        case ReasonCode::NoEdge: return "बल्ला नहीं लगा";
        case ReasonCode::NoEdgeButLbw: return "बल्ला नहीं लगा, लेकिन पगबाधा आउट";
        case ReasonCode::NoEdgeLbwUmpiresCall: return "बल्ला नहीं लगा; पगबाधा अंपायर्स कॉल, नॉट आउट";
        case ReasonCode::NoEdgeLbwNotOut: return "बल्ला नहीं लगा; पगबाधा भी नहीं";
        case ReasonCode::EdgeInconclusive: return "सबूत पक्का नहीं, मैदानी फ़ैसला बरकरार";
        case ReasonCode::ShortOfCrease: return "बल्लेबाज़ क्रीज़ से बाहर";
        case ReasonCode::MadeGround: return "बल्लेबाज़ समय पर क्रीज़ में";
        case ReasonCode::BenefitOfDoubt: return "संदेह का लाभ बल्लेबाज़ को";
        case ReasonCode::NoBatInvolved: return "बल्ला शामिल नहीं";
        case ReasonCode::CleanCatch: return "साफ़ कैच";
        case ReasonCode::WicketBrokenByBatter: return "बल्लेबाज़ ने ख़ुद विकेट गिराया";
        case ReasonCode::WicketNotBrokenByBatter: return "विकेट बल्लेबाज़ से नहीं गिरा";
        case ReasonCode::WilfulObstruction: return "जान-बूझकर बाधा";
        case ReasonCode::NotWilful: return "बाधा जान-बूझकर नहीं थी";
        case ReasonCode::CallConfirmed: return "रीप्ले ने फ़ैसले की पुष्टि की";
        case ReasonCode::CallReversed: return "रीप्ले में फ़ैसला ग़लत निकला";
        case ReasonCode::CallInconclusive: return "पक्का नहीं, मैदानी फ़ैसला बरकरार";
        case ReasonCode::NotReferable: return "यह फ़ैसला रेफ़र नहीं हो सकता";
    }
    return "";
}

const char* CallName(ScoringCall c, Language lang) {
    if (lang == Language::Hindi) {
        switch (c) {
            case ScoringCall::Wide: return "वाइड";
            case ScoringCall::HeightNoBall: return "नो-बॉल";
            case ScoringCall::Boundary: return "बाउंड्री";
            default: return "";
        }
    }
    switch (c) {
        case ScoringCall::Wide: return "wide";
        case ScoringCall::HeightNoBall: return "no-ball";
        case ScoringCall::Boundary: return "boundary";
        default: return "";
    }
}

std::string Fill(std::string s, const std::map<std::string, std::string>& vars) {
    for (const auto& [k, v] : vars) {
        const std::string key = "{" + k + "}";
        for (size_t pos; (pos = s.find(key)) != std::string::npos;) s.replace(pos, key.size(), v);
    }
    return s;
}

std::string Fixed1(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

}  // namespace

CommentaryEngine::CommentaryEngine(Language lang, uint64_t seed) : lang_(lang), rng_(seed) {}

CommentaryLine CommentaryEngine::Pick(const std::string& speaker, const std::string& category,
                                      const std::map<std::string, std::string>& vars) {
    const Pool& pool = lang_ == Language::Hindi ? Hindi() : English();
    auto it = pool.find(category);
    if (it == pool.end() || it->second.empty()) return {speaker, category, 0, ""};
    const auto& opts = it->second;
    int n = static_cast<int>(opts.size());
    int idx = static_cast<int>(rng_() % n);
    // Never repeat the same line twice in a row for a category.
    auto last = lastUsed_.find(category);
    if (n > 1 && last != lastUsed_.end() && last->second == idx) idx = (idx + 1) % n;
    lastUsed_[category] = idx;
    return {speaker, category, idx, Fill(opts[idx], vars)};
}

std::vector<CommentaryLine> CommentaryEngine::Describe(const Match& m, const DeliveryRecord& r) {
    std::vector<CommentaryLine> out;
    const Team& bat = m.GetTeam(m.AllInnings()[r.inningsIndex].BattingSide());
    const Team& fld = m.GetTeam(1 - m.AllInnings()[r.inningsIndex].BattingSide());
    const Innings& inn = m.AllInnings()[r.inningsIndex];
    std::map<std::string, std::string> v = {
        {"bat", bat.xi[r.striker].name},
        {"bowl", fld.xi[r.bowler].name},
        {"runs", std::to_string(r.delivery.extra == ExtraType::None ? r.runsToBatter : r.runsToTeam)},
        {"score", std::to_string(r.totalAfter) + "/" + std::to_string(r.wicketsAfter)},
    };
    const Delivery& d = r.delivery;

    if (d.dismissal != Dismissal::None) {
        v["bat"] = bat.xi[r.dismissedBatter].name;
        std::string cat = "wicket.other";
        switch (d.dismissal) {
            case Dismissal::Bowled: cat = "wicket.bowled"; break;
            case Dismissal::Caught: cat = "wicket.caught"; break;
            case Dismissal::Lbw: cat = "wicket.lbw"; break;
            case Dismissal::Stumped: cat = "wicket.stumped"; break;
            case Dismissal::RunOut: cat = "wicket.runout"; break;
            default: break;
        }
        out.push_back(Pick("Lead", cat, v));
        out.push_back(Pick("Expert", "expert.wicket", v));
    } else if (d.extra == ExtraType::Wide) {
        out.push_back(Pick("Lead", "wide", v));
    } else if (d.extra == ExtraType::Bye || d.extra == ExtraType::LegBye) {
        out.push_back(Pick("Lead", d.extra == ExtraType::Bye ? "bye" : "legbye", v));
    } else if (d.extra == ExtraType::NoBall && d.noBallRunsAs != ExtraType::None) {
        out.push_back(Pick("Lead", "noball", v));
        v["runs"] = std::to_string(d.runs);
        if (d.runs > 0) out.push_back(Pick("Lead", d.noBallRunsAs == ExtraType::Bye ? "bye" : "legbye", v));
    } else {
        if (d.extra == ExtraType::NoBall) out.push_back(Pick("Lead", "noball", v));
        if (d.boundary && d.runs == 6) out.push_back(Pick("Lead", "six", v));
        else if (d.boundary) out.push_back(Pick("Lead", "four", v));
        else if (d.runs == 0 && d.extra == ExtraType::None) out.push_back(Pick("Lead", "dot", v));
        else if (d.runs == 1) out.push_back(Pick("Lead", "single", v));
        else if (d.runs > 1) out.push_back(Pick("Lead", "runs", v));
        if (d.boundary && (rng_() % 3 == 0)) out.push_back(Pick("Expert", "expert.boundary", v));
    }
    if (d.shortRuns > 0) out.push_back(Pick("Lead", "shortrun", v));
    if (d.extra == ExtraType::NoBall && m.Rules().freeHitOnNoBall && !r.inningsCompleted)
        out.push_back(Pick("Lead", "freehit", v));
    if (r.batterHundred) out.push_back(Pick("Lead", "hundred", v));
    else if (r.batterFifty) out.push_back(Pick("Lead", "fifty", v));
    if (r.inningsCompleted) {
        out.push_back(Pick("Lead", "innings", v));
    } else if (r.overCompleted) {
        out.push_back(Pick("Lead", r.maiden ? "maiden" : "over", v));
        // Chase pressure analysis from the expert at the end of an over.
        if (inn.Target() && m.Rules().oversPerInnings > 0) {
            int need = *inn.Target() - r.totalAfter;
            int balls = m.Rules().oversPerInnings * m.Rules().ballsPerOver - inn.LegalBalls();
            if (need > 0 && balls > 0 && balls <= 36) {
                v["need"] = std::to_string(need);
                v["balls"] = std::to_string(balls);
                v["rrr"] = Fixed1(need * 6.0 / balls);
                out.push_back(Pick("Expert", "expert.chase", v));
            }
        }
    }
    return out;
}

std::vector<CommentaryLine> CommentaryEngine::DescribeResult(const Match& m) {
    if (m.Phase() != MatchPhase::Complete) return {};
    return {Pick("Lead", "result", {{"summary", m.Result().summary}})};
}

std::string CommentaryEngine::Reason(ReasonCode r) const {
    return lang_ == Language::Hindi ? ReasonHindi(r) : ReasonText(r);
}

std::vector<CommentaryLine> CommentaryEngine::DescribeDecision(const Match& m, const DecisionRecord& d) {
    std::vector<CommentaryLine> out;
    const Innings& inn = m.AllInnings().at(d.inningsIndex);
    int entitled = d.onFieldOut ? inn.BattingSide() : 1 - inn.BattingSide();
    if (d.call != ScoringCall::None) entitled = d.onFieldCalled ? 1 - inn.BattingSide() : inn.BattingSide();
    std::map<std::string, std::string> v = {
        {"team", m.GetTeam(d.reviewed ? d.reviewingSide : entitled).name},
        {"reason", Reason(d.result.code)},
        {"call", CallName(d.call, lang_)},
    };
    if (d.call == ScoringCall::Boundary) {
        out.push_back(Pick("Lead", "boundary.check", v));
        out.push_back(Pick("Lead", d.result.finalCalled ? "boundary.six" : "boundary.four", v));
        return out;
    }
    if (d.call != ScoringCall::None) {
        out.push_back(Pick("Lead", d.onFieldCalled ? "call.signalled" : "call.notsignalled", v));
        if (d.reviewed) {
            out.push_back(Pick("Lead", "review.taken", v));
            out.push_back(Pick("Lead", d.result.finalCalled != d.onFieldCalled ? "call.changed" : "call.stands", v));
            out.push_back(Pick("Expert", "expert.review", v));
        }
        return out;
    }
    if (d.kind == DecisionKind::PlayerAppeal) {
        out.push_back(Pick("Lead", d.onFieldOut ? "appeal.out" : "appeal.notout", v));
        if (d.reviewed) {
            out.push_back(Pick("Lead", "review.taken", v));
            const char* cat = "review.upheld";
            if (d.result.outcome == ReviewOutcome::Overturned) cat = "review.overturned";
            else if (d.result.outcome == ReviewOutcome::UmpiresCall) cat = "review.umpirescall";
            if (d.result.noBall) out.push_back(Pick("Lead", "noball.check", v));
            else out.push_back(Pick("Lead", cat, v));
            out.push_back(Pick("Expert", "expert.review", v));
        } else if (d.result.noBall && d.onFieldOut) {
            out.push_back(Pick("Lead", "noball.check", v));
        }
    } else {
        if (d.result.noBall && !d.result.finalOut) out.push_back(Pick("Lead", "noball.check", v));
        else out.push_back(Pick("Lead", d.result.finalOut ? "thirdumpire.out" : "thirdumpire.notout", v));
        out.push_back(Pick("Expert", "expert.review", v));
    }
    return out;
}

std::vector<CommentaryLine> CommentaryEngine::DescribePenalty(const Match& m, const PenaltyRecord& p) {
    return {Pick("Lead", "penalty", {{"team", m.GetTeam(p.side).name}, {"reason", p.reason}})};
}

std::vector<CommentaryLine> CommentaryEngine::DescribeInterval(const Match& m, const IntervalRecord& r) {
    const char* cat = "interval.innings";
    switch (r.kind) {
        case IntervalKind::Lunch: cat = "interval.lunch"; break;
        case IntervalKind::Tea: cat = "interval.tea"; break;
        case IntervalKind::Dinner: cat = "interval.dinner"; break;
        case IntervalKind::Stumps: cat = "interval.stumps"; break;
        case IntervalKind::InningsBreak: break;
    }
    return {Pick("Lead", cat,
                 {{"day", std::to_string(r.day)},
                  {"team", m.GetTeam(r.battingSide).name},
                  {"score", std::to_string(r.total) + "/" + std::to_string(r.wickets)}})};
}

std::vector<CommentaryLine> CommentaryEngine::DescribeNewBall(const Match& m) {
    return {Pick("Lead", "newball", {{"team", m.GetTeam(m.FieldingSide()).name}})};
}

}  // namespace cricket
