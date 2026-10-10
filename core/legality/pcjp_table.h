#pragma once

#include <cstdint>

// The Pokemon Center Japan 5th Anniversary egg machine (April 25 to May 18,
// 2003) drew its egg from a weighted table of eight entries (Pichu, Bagon,
// Absol and Ralts, each with an own move or Wish; 1 in 5 Pichu are shiny).
// Ported from PKHeX's PCJPFifthAnniversary and WeightedTable3.
namespace pktool::legality::rng3 {

struct PcjpResult {
    uint32_t index;  // 0 Pichu, 1 Bagon, 2 Absol, 3 Ralts
    bool wish;       // the Wish moveset instead of the other one
    bool shiny;
};

// The table roll for a 16-bit seed (two LCRNG calls).
PcjpResult pcjpResult(uint16_t seed16);

// Table index of a species (0-3), as PKHeX computes it.
uint32_t pcjpIndex(uint16_t species);

// True if the roll of `seed16` gives this species, moveset and shininess.
bool pcjpTableMatch(uint16_t species, bool wish, bool shiny, uint16_t seed16);

}  // namespace pktool::legality::rng3
