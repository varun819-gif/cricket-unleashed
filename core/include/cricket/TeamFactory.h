// Launch teams. Real country names and national flags are allowed (project
// decision, 2026-11-20). Player names stay FICTIONAL, and no cricket board
// logos, kits or real player likenesses are used without a licence
// (see docs/ASSET_REGISTER.md).
#pragma once

#include <vector>

#include "cricket/Types.h"

namespace cricket {

// Ten fictional international sides with deterministic, varied player ratings.
std::vector<Team> FictionalTeams();

struct NationalTeamInfo {
    const char* name;       // "India"
    const char* flag;       // flag file code: IN, PK, AU, ENG, ZA, NZ, LK, BD, WI, AF
    const char* fictional;  // the fictional side with the same squad
};
const std::vector<NationalTeamInfo>& NationalTeamList();
// The same ten squads (fictional players) under real country names.
std::vector<Team> NationalTeams();

}  // namespace cricket
