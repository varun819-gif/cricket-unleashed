#include "cricket/online/Moderation.h"

#include <cctype>
#include <vector>

namespace cricket::online {

namespace {

// Blocked anywhere in the name (after normalising and removing spaces).
const std::vector<std::string>& Strong() {
    static const std::vector<std::string> w = {
        "fuck", "cunt", "nigger", "nigga", "faggot", "motherfuck", "madarchod", "maderchod", "behenchod",
        "bhenchod", "benchod", "chutiya", "chutia", "chootiya", "bhosdi", "bhosad", "rapist", "whore",
        "jhaatu", "kamina", "haramzada", "haraamzada", "randibaaz",
    };
    return w;
}
// Blocked as a whole word only.
const std::vector<std::string>& Weak() {
    static const std::vector<std::string> w = {
        "dick", "sex", "sexy", "rape", "slut", "bc", "mc", "bsdk", "gandu", "randi", "lund", "lauda", "loda",
        "porn", "nazi", "shit", "bitch", "ass", "asshole", "hitler", "isis", "harami", "bastard", "pussy",
        "boobs", "kutti", "saala", "saali", "terrorist", "jihadi", "paki", "katua",
    };
    return w;
}
// Pretending to be staff or the game itself.
const std::vector<std::string>& Reserved() {
    static const std::vector<std::string> w = {"admin", "moderator", "official", "support", "cricketunleashed",
                                               "staff", "system", "server"};
    return w;
}

char Fold(char c) {
    switch (c) {
        case '0': return 'o';
        case '1': case '!': case '|': return 'i';
        case '3': return 'e';
        case '4': case '@': return 'a';
        case '5': case '$': return 's';
        case '7': return 't';
        case '8': return 'b';
        default: return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

}  // namespace

std::string NameProblem(const std::string& name) {
    std::string lower;
    int digits = 0;
    for (char c : name) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c >= '0' && c <= '9') ++digits;
    }
    if (digits >= 7) return "names can't contain phone numbers";
    if (lower.find("http") != std::string::npos || lower.find("www.") != std::string::npos ||
        lower.find(".com") != std::string::npos)
        return "names can't contain web links";
    // Normalised words, and everything joined together.
    std::vector<std::string> words;
    std::string word, joined;
    for (char c : name) {
        const char f = Fold(c);
        if (std::isalpha(static_cast<unsigned char>(f))) {
            word += f;
            joined += f;
        } else if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty()) words.push_back(word);
    // "f u c k" written with spaces also joins up.
    for (const auto& s : Strong())
        if (joined.find(s) != std::string::npos) return "please choose a different name";
    for (const auto& r : Reserved())
        if (joined.find(r) != std::string::npos) return "names can't look like game staff";
    for (const auto& w : words)
        for (const auto& b : Weak())
            if (w == b) return "please choose a different name";
    return "";
}

}  // namespace cricket::online
