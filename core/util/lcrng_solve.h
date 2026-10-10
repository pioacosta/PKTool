#pragma once

#include <cstdint>

// Fast inverse searches on the 32-bit linear congruential generators of the Pokemon games.
//
// The PID/IV code often knows the upper 16 bits of a state and the upper bits of an output a
// few calls later, and wants every lower half that fits ("which RNG states produced this PID?").
// Trying all 65536 lower halves costs ~65536 multiplications every time; the answer is a handful
// of lattice points, found here with a continued-fraction reduction in a few dozen operations.
namespace pktool::util {

// state' = state * mul + add (mod 2^32).
struct Lcg {
    uint32_t mul;
    uint32_t add;
};

constexpr Lcg kPokemonLcg = {0x41C64E6Du, 0x6073u};      // Gen III-V games
constexpr Lcg kGameCubeLcg = {0x000343FDu, 0x00269EC3u};  // Colosseum / XD

// The generator that does `steps` calls at once (steps >= 0).
Lcg lcgJump(Lcg lcg, int steps);

// Every x in [0, 65536), ascending, such that the state (upper16 << 16 | x), advanced by `lcg`,
// has its upper 16 bits equal to `wanted16`. At most `max` values are stored; returns how many
// there are (or `max` if there are more). Use lcgJump() for outputs several calls ahead.
int lcgSolveTop16(Lcg lcg, uint32_t upper16, uint32_t wanted16, uint16_t* out, int max);

// Same, but for a 15-bit IV word: bits 16-30 of the advanced state equal `word15` (bit 31 free).
int lcgSolveWord15(Lcg lcg, uint32_t upper16, uint32_t word15, uint16_t* out, int max);

}  // namespace pktool::util
