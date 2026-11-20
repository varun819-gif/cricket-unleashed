#include "cricket/TeamFactory.h"

#include <random>

namespace cricket {

namespace {

struct Region {
    const char* team;
    std::vector<const char*> first;
    std::vector<const char*> last;
};

const std::vector<Region>& Regions() {
    static const std::vector<Region> r = {
        {"Ganga Royals", {"Aarav", "Vihaan", "Kabir", "Rohan", "Ishaan", "Arjun", "Dev", "Yash", "Karan", "Neel", "Om"},
         {"Mehra", "Raghavan", "Kulkarni", "Sethi", "Bhatt", "Iyer", "Chauhan", "Nair", "Joshi", "Pillai", "Saxena"}},
        {"Indus Falcons", {"Zain", "Hamza", "Bilal", "Faraz", "Usman", "Saad", "Imad", "Rizwan", "Talha", "Asad", "Junaid"},
         {"Qureshi", "Malik", "Durrani", "Siddiqi", "Baig", "Ansari", "Chaudhry", "Mirza", "Rana", "Shah", "Abbasi"}},
        {"Southern Cross", {"Lachlan", "Mitch", "Cooper", "Hayden", "Riley", "Brodie", "Jai", "Toby", "Angus", "Flynn", "Bailey"},
         {"Harding", "Coulter", "Mackay", "Fenwick", "Doyle", "Prescott", "Brennan", "Wakefield", "Rourke", "Sutton", "Langley"}},
        {"Albion Lions", {"Oliver", "Harry", "George", "Freddie", "Alfie", "Archie", "Theo", "Rupert", "Ollie", "Ned", "Hugo"},
         {"Ashworth", "Pembroke", "Whitfield", "Carrow", "Bellamy", "Thornton", "Hale", "Radley", "Fairbanks", "Lowe", "Kerridge"}},
        {"Cape Springers", {"Pieter", "Sipho", "Ruan", "Thabo", "Dewald", "Kyle", "Lungi", "Jandre", "Wiaan", "Andile", "Morne"},
         {"van Wyk", "Nkosi", "Botha", "Dlamini", "Steyn", "Mokoena", "Pretorius", "Ndlovu", "Coetzee", "Mahlangu", "Venter"}},
        {"Aotea Kauri", {"Tane", "Hamish", "Lockie", "Corey", "Rangi", "Finn", "Wiremu", "Blair", "Tom", "Nikau", "Jesse"},
         {"Wiremu", "McCrae", "Tamihana", "Ferguson", "Horomona", "Galloway", "Paewai", "Strachan", "Ngata", "Ritchie", "Kereama"}},
        {"Lanka Leopards", {"Kasun", "Dinesh", "Pathum", "Chamika", "Nuwan", "Dasun", "Maheesh", "Lahiru", "Isuru", "Akila", "Tharindu"},
         {"Perera", "Fernando", "Jayasekara", "Bandara", "Wickrama", "Silva", "Gunawardena", "Rathnayake", "Dias", "Herath", "Kumara"}},
        {"Delta Tigers", {"Tamim", "Rakib", "Shoriful", "Mehedi", "Nayeem", "Towhid", "Arif", "Sabbir", "Rony", "Jaker", "Hasan"},
         {"Rahman", "Hossain", "Islam", "Chowdhury", "Sarkar", "Ahmed", "Mridha", "Uddin", "Talukder", "Khan", "Molla"}},
        {"Caribbean Hurricanes", {"Shai", "Akeal", "Romario", "Brandon", "Kemar", "Jason", "Alzarri", "Nkrumah", "Roston", "Keacy", "Obed"},
         {"Charles", "Thomas", "Joseph", "Greaves", "Blackwood", "Forde", "Mayers", "Hinds", "Seales", "Bascombe", "Pierre"}},
        {"Hindu Kush Eagles", {"Rahmat", "Ibrahim", "Azmat", "Gulbadin", "Fazal", "Naveen", "Noor", "Karim", "Hashmat", "Ikram", "Qais"},
         {"Zadran", "Shinwari", "Safi", "Naib", "Farooqi", "Omarzai", "Stanikzai", "Janat", "Alikhil", "Ahmadi", "Mangal"}},
    };
    return r;
}

}  // namespace

std::vector<Team> FictionalTeams() {
    std::vector<Team> teams;
    uint64_t seed = 20260101;
    for (const auto& reg : Regions()) {
        std::mt19937_64 rng(seed++);
        auto roll = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
        Team t;
        t.name = reg.team;
        for (int i = 0; i < 11; ++i) {
            Player p;
            p.name = std::string(reg.first[i]) + " " + reg.last[(i * 7 + 3) % 11];
            p.bats = roll(0, 3) == 0 ? Handedness::Left : Handedness::Right;
            p.form = roll(35, 85);
            p.fitness = roll(70, 98);
            p.fielding = roll(55, 92);
            if (i < 5) {  // specialist batters (index 4 keeps wicket)
                p.batting = roll(68, 92);
                p.bowling = roll(10, 35);
                p.canBowl = (i == 3);  // part-timer
                p.bowls = BowlingKind::Spin;
            } else if (i < 7) {  // all-rounders
                p.batting = roll(55, 75);
                p.bowling = roll(60, 80);
                p.canBowl = true;
                p.bowls = i == 5 ? BowlingKind::Pace : BowlingKind::Spin;
            } else {  // bowlers
                p.batting = roll(15, 40);
                p.bowling = roll(70, 92);
                p.canBowl = true;
                p.bowls = i == 10 ? BowlingKind::Spin : BowlingKind::Pace;
            }
            t.xi.push_back(p);
        }
        t.captain = 0;
        t.keeper = 4;
        teams.push_back(std::move(t));
    }
    return teams;
}

const std::vector<NationalTeamInfo>& NationalTeamList() {
    static const std::vector<NationalTeamInfo> list = {
        {"India", "IN", "Ganga Royals"},          {"Pakistan", "PK", "Indus Falcons"},
        {"Australia", "AU", "Southern Cross"},    {"England", "ENG", "Albion Lions"},
        {"South Africa", "ZA", "Cape Springers"}, {"New Zealand", "NZ", "Aotea Kauri"},
        {"Sri Lanka", "LK", "Lanka Leopards"},    {"Bangladesh", "BD", "Delta Tigers"},
        {"West Indies", "WI", "Caribbean Hurricanes"}, {"Afghanistan", "AF", "Hindu Kush Eagles"},
    };
    return list;
}

std::vector<Team> NationalTeams() {
    std::vector<Team> teams = FictionalTeams();
    const auto& list = NationalTeamList();
    for (auto& t : teams)
        for (const auto& n : list)
            if (t.name == n.fictional) t.name = n.name;
    return teams;
}

}  // namespace cricket
