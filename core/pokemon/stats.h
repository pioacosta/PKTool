#pragma once

#include <cstdint>

#include "core/pokemon/pokemon.h"

namespace pktool::pokemon {

// Nature multiplier for a non-HP stat, in tenths: 9, 10 or 11.
uint8_t natureModifier(uint8_t nature, Stat stat);

// Gen III-V battle stat formulas. `out` receives the six stats in Stat order.
// Shedinja (species 292) always has 1 HP.
void calcStats(uint16_t species, const uint8_t baseStats[kStatCount], const uint8_t ivs[kStatCount],
               const uint8_t evs[kStatCount], uint8_t level, uint8_t nature, uint16_t out[kStatCount]);

}  // namespace pktool::pokemon
