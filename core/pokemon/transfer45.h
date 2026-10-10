#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/pokemon/pk4.h"

// Poké Transfer: moving a Gen IV Pokémon into a Gen V game (one way).
namespace pktool::pokemon::transfer45 {

// Converts a decrypted stored PK4 into a decrypted stored PK5 the way Poké
// Transfer does (mirrors PKHeX's PK4.ConvertToPK5): the nature gets its own
// field, names are re-encoded, the met location becomes Poké Transfer and the
// met level the current level, HM moves are removed, PP refilled, mail and
// Arceus plates are dropped, the HG/SS and Pt extra fields are cleared.
// `notes` receives what changed that the user may care about.
void convert(const uint8_t pk4Data[pk4::kStoredSize], uint8_t pk5Out[pk4::kStoredSize],
             std::vector<std::string>* notes = nullptr);

}  // namespace pktool::pokemon::transfer45
