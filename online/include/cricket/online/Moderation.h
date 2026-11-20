// Player-name checks for online play (see docs/MODERATION.md).
//
// Names are normalised first (lower case, common look-alike characters such
// as 0->o, 1->i, @->a, $->s), so "B@d W0rd" is checked as "bad word".
//  * Strong words (slurs, the worst English / Hindi abuse) are blocked even
//    when hidden inside a longer name.
//  * Milder words are blocked only as a whole word, so ordinary names like
//    "Sussex" or "Dickson" still work.
//  * Names that pretend to be staff ("admin", "moderator", "official") and
//    names carrying contact details (7+ digits, web links) are blocked.
// No word list is perfect: players can still report a name, and an admin can
// reset it (docs/MODERATION.md).
#pragma once

#include <string>

namespace cricket::online {

// "" if the name is acceptable, else a short reason to show the player.
std::string NameProblem(const std::string& name);

}  // namespace cricket::online
