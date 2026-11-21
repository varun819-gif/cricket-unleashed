// Scorecard: the full card for every innings, built from the rules engine's
// own records (batter / bowler cards and the ball-by-ball log): batting
// (runs, balls, 4s, 6s, strike rate, how out), bowling (overs, maidens,
// runs, wickets, economy, wides, no-balls), extras, fall of wickets and
// partnerships. CheckScorecard cross-checks every total against the others.
#pragma once

#include <string>
#include <vector>

#include "cricket/Match.h"

namespace cricket {

struct BattingLine {
    int player = -1;
    std::string name;
    bool batted = false, out = false;
    std::string howOut;          // "c Smith b Jones", "lbw b Jones", "run out (Smith)", "not out"
    int runs = 0, balls = 0, fours = 0, sixes = 0;
    double strikeRate = 0;       // runs per 100 balls
};

struct BowlingLine {
    int player = -1;
    std::string name;
    int balls = 0;               // legal balls
    std::string overs;           // "3.4"
    int maidens = 0, runs = 0, wickets = 0, wides = 0, noBalls = 0;
    double economy = 0;          // runs per over
};

struct FallOfWicket {
    int wicket = 0;              // 1st, 2nd, ...
    int score = 0;               // team score when it fell
    int batter = -1;
    std::string name;
    std::string over;            // "12.3"
};

struct Partnership {
    int wicket = 0;              // for the 1st, 2nd, ... wicket
    int batterA = -1, batterB = -1;
    std::string nameA, nameB;
    int runs = 0;                // everything added while they batted together (extras included)
    int balls = 0;               // legal balls
    int runsA = 0, runsB = 0;    // off their own bats
    bool unbroken = false;
};

struct InningsCard {
    int index = 0;
    int battingSide = 0;
    std::string team;
    int total = 0, wickets = 0, legalBalls = 0;
    std::string overs;
    double runRate = 0;
    Extras extras;
    int extrasTotal = 0;
    std::vector<BattingLine> batting;    // in batting order (players who batted first, then did-not-bat)
    std::vector<BowlingLine> bowling;    // in order of first ball bowled
    std::vector<FallOfWicket> fallOfWickets;
    std::vector<Partnership> partnerships;
    std::string end;                     // "all out", "20 overs", "declared", "target reached", ...
};

std::vector<InningsCard> BuildScorecard(const Match& m);

// Cross-checks the card against the innings totals. Ok, or what disagrees.
Status CheckScorecard(const Match& m);

std::string OversString(int legalBalls, int ballsPerOver = 6);

}  // namespace cricket
