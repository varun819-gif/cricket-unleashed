// Text commentary generated ONLY from verified DeliveryRecords. The audio
// layer maps each (category, line id) to licensed recorded clips; text is the
// fallback and the subtitle track.
#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "cricket/Match.h"

namespace cricket {

enum class Language { English, Hindi };

struct CommentaryLine {
    std::string speaker;   // "Lead" or "Expert"
    std::string category;  // e.g. "four", "wicket.caught" - keys for audio clips
    int lineId = 0;        // which variant was picked
    std::string text;
};

class CommentaryEngine {
public:
    CommentaryEngine(Language lang, uint64_t seed);

    // Lines for one ball (lead commentator, plus expert on big moments).
    std::vector<CommentaryLine> Describe(const Match& m, const DeliveryRecord& r);
    std::vector<CommentaryLine> DescribeResult(const Match& m);
    // Appeals, DRS reviews and third-umpire referrals.
    std::vector<CommentaryLine> DescribeDecision(const Match& m, const DecisionRecord& d);

private:
    CommentaryLine Pick(const std::string& speaker, const std::string& category,
                        const std::map<std::string, std::string>& vars);
    Language lang_;
    std::mt19937_64 rng_;
    std::map<std::string, int> lastUsed_;
};

}  // namespace cricket
