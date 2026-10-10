#pragma once

#include <cstdint>

#include "core/pokemon/pokemon.h"

// Generation of RNG-consistent PID/IV combinations (Gen III/IV).
//
// Method 1: [PID low, PID high, IV word 1, IV word 2] from consecutive LCRNG
//   calls (Gen III wild/static, Gen IV static and gifts).
// Method J (D/P/Pt) and K (HG/SS) wild encounters: a nature call first, then
//   PID pairs are drawn until PID % 25 equals that nature, then the IV words.
//   J picks the nature as rand / 0xA3E, K as rand % 25. Frames produced for
//   J/K also satisfy the Method 1 correlation.
// With a Synchronize lead the nature call is replaced by a coin flip (J: bit 15
//   of the call clear, K: bit 0 clear) and the PID loop targets the lead's
//   nature, so frames the plain nature call would not explain become valid.
// With a Cute Charm lead the PID is a gender buffer plus the nature and no PID
//   calls are made: the IVs are any two consecutive calls.
namespace pktool::legality {

enum class RngMethod : uint8_t { Method1, MethodJ, MethodK };

// How a generated frame is reached beyond a plain wild encounter.
enum class PidSource : uint8_t {
    Normal,
    Synchronize,  // the lead has Synchronize: the nature call is replaced by a coin flip (J: bit 15, K: bit 0 of the call)
    CuteCharm,    // the lead has Cute Charm: PID = gender buffer + nature, IVs are any two consecutive calls
};

struct PidIvRequest {
    uint16_t species = 0;
    uint16_t tid = 0;
    uint16_t sid = 0;
    uint8_t nature = 0;
    bool shiny = false;
    pokemon::Gender gender = pokemon::Gender::Male;
    uint8_t abilitySlot = 0;
    uint8_t targetIvs[pokemon::kStatCount] = {31, 31, 31, 31, 31, 31};
    RngMethod method = RngMethod::Method1;
    // Wild encounters of Gen IV only: let the search use a lead with
    // Synchronize (Methods J/K) or Cute Charm. Off by default.
    bool allowSync = false;
    bool allowCuteCharm = false;
};

struct PidIvResult {
    bool found = false;
    uint32_t pid = 0;
    uint8_t ivs[pokemon::kStatCount] = {};
    int distance = 0;  // sum of |IV - target|
    PidSource source = PidSource::Normal;
};

// Searches for the frame closest to the target IVs that satisfies every hard
// constraint (nature, shininess, gender, ability slot, method). `effort`
// scales the search (roughly linear in time; 1 is ~0.1 s on a DS).
// `salt` varies the search between calls.
PidIvResult generatePidIv(const PidIvRequest& request, int effort, uint32_t salt = 0);

// IVs closest to `target` that are two consecutive RNG outputs, as in
// Pokewalker catches and Cute Charm frames (`pid` is left 0).
PidIvResult generateConsecutiveIvs(const uint8_t target[pokemon::kStatCount], int effort);

// True if a PID/IV frame is reachable with Method J (or K): a nature call
// matching PID % 25 precedes the PID, possibly after rejected PID pairs.
// `pidLowState` is the RNG state whose top half is the PID's low half.
bool isNatureFrameValid(uint32_t pidLowState, RngMethod method, bool allowSync = false);

// Picks the generation method for a Pokémon's origin game.
RngMethod methodForOrigin(uint8_t originGame);

inline uint32_t lcrngPrev(uint32_t seed) {
    return seed * 0xEEB9EB65u + 0x0A3561A1u;
}

}  // namespace pktool::legality
