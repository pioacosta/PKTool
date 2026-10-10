#pragma once

#include <cstdint>

#include "core/legality/rng4.h"
#include "core/pokemon/pokemon.h"

// PID/IV correlation of Gen III/IV RNG-generated Pokémon.
//
// Wild and static encounters draw the PID and IVs from consecutive calls of
// the LCRNG: PID low half, PID high half, then two 15-bit IV words. Gen IV
// Methods J and K (D/P/Pt and HG/SS) pick the nature first but end with the
// same sequence as Method 1, so one check covers them. Gen III Methods 2 and
// 4 skip one call before the first or second IV word.
namespace pktool::legality {

enum class PidIvMethod : uint8_t {
    None,
    Method1,  // also Gen IV Methods J/K
    Method2,
    Method4,
    Method3,  // Gen III only: PID high half one call late (A_CDE)
    CuteCharm,  // Gen IV: PID = small buffer + nature, IVs from the RNG
};

const char* methodName(PidIvMethod method);

// Finds the method whose RNG sequence produces both `pid` and `ivs`.
// Also returns the seed that generated the PID's low half.
PidIvMethod findPidIvMethod(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint32_t* seedOut = nullptr);

// True if the two IV words are consecutive outputs of the LCRNG, as when the
// PID does not come from the same RNG sequence (e.g. Cute Charm).
bool ivsFromConsecutiveCalls(const uint8_t ivs[pokemon::kStatCount]);

// Gen IV Cute Charm PIDs are below 0x100: a gender buffer plus the nature.
inline bool isCuteCharmPid(uint32_t pid) {
    return pid < 0x100;
}

// Strict Gen IV Cute Charm rule (PKHeX CuteCharm4): the PID the game builds
// for a wild Pokemon of `species` with `gender` when the lead has Cute Charm.
// A female gets PID = nature (0-24), a male gets the lowest male PID of its
// gender ratio rounded up to a multiple of 25 plus the nature. Species that
// evolved into a different gender ratio (Shedinja, Wormadam, Mothim,
// Vespiquen, Gallade, Froslass, Marill/Azumarill from a male Azurill) are
// judged by the species they were met as.
bool isCuteCharmPid(uint16_t species, pokemon::Gender gender, uint32_t pid);
// The Cute Charm PID for a given gender ratio and nature (male target).
uint32_t cuteCharmPidMale(uint8_t genderRatio, uint32_t nature);

// Poke Radar chain shiny (D/P/Pt grass): the shiny PID is drawn from 15
// consecutive RNG calls (3 bits of the low half, 3 bits of the high half,
// then the remaining 13 low bits one call each) followed by the IV words.
bool isChainShinyFrame(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t tid, uint16_t sid);

// Decodes a 15-bit IV word pair (as produced by the RNG) into stat order.
void ivsFromWords(uint16_t word1, uint16_t word2, uint8_t ivs[pokemon::kStatCount]);

// Gen III wild/static frames: Methods 1, 2, 3 and 4 (Unown's PID halves are
// swapped). Never returns CuteCharm.
PidIvMethod findPidIvMethod3(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t species);

// A generated Gen III frame and the method it follows.
struct PidIvResult3 {
    PidIvResult frame;
    PidIvMethod method = PidIvMethod::None;
};

// Like generatePidIv for Gen III wild encounters: Method 1 first, then Methods
// 2 and 4 (when `allowMethods24`) if they come closer to the target IVs. The
// request's `method`, `allowSync` and `allowCuteCharm` are ignored.
PidIvResult3 generatePidIv3(const PidIvRequest& request, int effort, uint32_t salt = 0, bool allowMethods24 = true);

}  // namespace pktool::legality
