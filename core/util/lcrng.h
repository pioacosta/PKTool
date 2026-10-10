#pragma once

#include <cstdint>

namespace pktool::util {

// The linear congruential RNG used by Gen III-V games (PID/IV generation and
// Gen IV/V entity encryption).
inline uint32_t lcrngNext(uint32_t seed) {
    return seed * 0x41C64E6Du + 0x6073u;
}

// The generator is affine, so stepping the seed by one steps every output by a constant:
// next(s + 1) = next(s) + kLcrngMul, and the same with kLcrngMulPrev for the inverse generator.
// Scans over consecutive seeds use it to avoid a multiplication per candidate.
constexpr uint32_t kLcrngMul = 0x41C64E6Du;
constexpr uint32_t kLcrngMulPrev = 0xEEB9EB65u;  // multiplier of the inverse generator

}  // namespace pktool::util
