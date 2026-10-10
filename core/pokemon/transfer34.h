#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pokemon.h"

// Pal Park: moving a Gen III Pokémon into a Gen IV game (one way).
namespace pktool::pokemon::transfer34 {

// Converts a decrypted stored PK3 into a decrypted stored PK4 the way Pal
// Park does (mirrors PKHeX's PK3.ConvertToPK4): the met location becomes
// Pal Park, the met level the current level and the met date `today`;
// friendship resets to 70; names are re-encoded (un-nicknamed Pokémon get the
// Gen IV species name); HM moves are removed and PP refilled; mail is
// dropped; ribbons and contest stats move to their Gen IV fields; Unown gets
// its form from the PID. Eggs are refused by the games, so convert() must
// not be called with one. `notes` receives what changed.
void convert(const uint8_t pk3Data[pk3::kStoredSize], uint8_t pk4Out[pk4::kStoredSize], const Date& today,
             std::vector<std::string>* notes = nullptr);

}  // namespace pktool::pokemon::transfer34
