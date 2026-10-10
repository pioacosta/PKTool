#include "core/legality/roamer.h"

#include <algorithm>
#include <cstdlib>

#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/legality/rng3.h"
#include "core/pokemon/edit4.h"
#include "core/util/lcrng.h"
#include "core/util/lcrng_solve.h"

namespace pktool::legality::roamer {
namespace {

// PKHeX EncounterStatic4: one bit per location, starting at the region's first.
constexpr int kFirstSinnoh = 16;
constexpr int kFirstJohto = 177;
constexpr int kFirstKanto = 149;
constexpr uint64_t kGrassSinnoh = 0x28033FFFFull;
constexpr uint64_t kWaterSinnoh = 0x2803E3B9Eull;
constexpr uint32_t kGrassJohto = 0x0003E7FFu;
constexpr uint32_t kWaterJohto = 0x0001E06Eu;
constexpr uint32_t kGrassKanto = 0x0AB3FFFFu;
constexpr uint32_t kWaterKanto = 0x0ABC1B28u;

bool inMask(uint64_t mask, int first, uint16_t location) {
    int bit = static_cast<int>(location) - first;
    return bit >= 0 && bit < 64 && (mask & (1ull << bit)) != 0;
}

// The IV word as the games pack it (HP, Atk, Def in the first 15 bits).
uint32_t packedIvs(const uint8_t ivs[pokemon::kStatCount]) {
    return static_cast<uint32_t>(ivs[pokemon::kHp]) | (static_cast<uint32_t>(ivs[pokemon::kAtk]) << 5) |
           (static_cast<uint32_t>(ivs[pokemon::kDef]) << 10) | (static_cast<uint32_t>(ivs[pokemon::kSpe]) << 15) |
           (static_cast<uint32_t>(ivs[pokemon::kSpA]) << 20) | (static_cast<uint32_t>(ivs[pokemon::kSpD]) << 25);
}

}  // namespace

bool metOk3(uint8_t originGame, uint16_t location) {
    if (originGame == version::kFireRed || originGame == version::kLeafGreen) return location >= 101 && location <= 125;
    return location >= 16 && location <= 49;
}

uint16_t defaultLocation3(uint8_t originGame) {
    return originGame == version::kFireRed || originGame == version::kLeafGreen ? 101 : 16;
}

bool metOk4(uint16_t species, uint16_t location, uint8_t groundTile) {
    if (groundTile != kGroundGrass && groundTile != kGroundWater) return false;
    const bool grass = groundTile == kGroundGrass;
    if (location == 193 && !grass) return false;  // Route 45 surf tile: PKHeX G4InvalidTileR45Surf
    switch (species) {
        case 243: case 244:  // Raikou, Entei
            return inMask(grass ? kGrassJohto : kWaterJohto, kFirstJohto, location);
        case 380: case 381:  // Latias, Latios (HG/SS)
            return inMask(grass ? kGrassKanto : kWaterKanto, kFirstKanto, location);
        default:
            return inMask(grass ? kGrassSinnoh : kWaterSinnoh, kFirstSinnoh, location);
    }
}

uint16_t defaultLocation4(uint16_t species) {
    switch (species) {
        case 243: case 244: return kFirstJohto;
        case 380: case 381: return kFirstKanto;
        default: return kFirstSinnoh;
    }
}

bool metOk5(uint16_t location) {
    return location < 32 && (0xBFFF8000u & (1u << location)) != 0;
}

bool ivsMatch3(uint8_t originGame, uint32_t pid, const uint8_t ivs[pokemon::kStatCount]) {
    PidIvMethod method = findPidIvMethod(pid, ivs);
    if (originGame == version::kEmerald) return method == PidIvMethod::Method1;
    const uint32_t iv32 = packedIvs(ivs);
    if (iv32 > 0xFF) return false;
    if (method == PidIvMethod::Method1) return true;
    // Method 1 roamer: the first IV call is the one that survives, 8 bits of it.
    const uint32_t low = pid & 0xFFFF;
    const uint32_t high = pid >> 16;
    uint16_t bottoms[32];
    const int found = util::lcgSolveTop16(util::kPokemonLcg, low, high, bottoms, 32);
    for (int n = 0; n < found; ++n) {
        uint32_t s2 = util::lcrngNext((low << 16) | bottoms[n]);
        if (((util::lcrngNext(s2) >> 16) & 0xFF) == iv32) return true;
    }
    return false;
}

bool generate3(const Request3& req, uint32_t salt, uint32_t* pid, uint8_t ivs[pokemon::kStatCount]) {
    // The 256 possible IV bytes, closest to the target first.
    struct Candidate {
        int distance;
        uint32_t byte;
    };
    Candidate candidates[256];
    for (uint32_t b = 0; b < 256; ++b) {
        int d = std::abs(static_cast<int>(b & 31) - req.targetIvs[pokemon::kHp]) +
                std::abs(static_cast<int>(b >> 5) - req.targetIvs[pokemon::kAtk]) + req.targetIvs[pokemon::kDef] +
                req.targetIvs[pokemon::kSpe] + req.targetIvs[pokemon::kSpA] + req.targetIvs[pokemon::kSpD];
        candidates[b] = {d, b};
    }
    std::stable_sort(candidates, candidates + 256, [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });

    constexpr uint32_t kStates = 1u << 22;  // per IV byte
    for (int c = 0; c < 8; ++c) {
        const uint32_t byte = candidates[c].byte;
        for (uint32_t i = 0; i < kStates; ++i) {
            // Free bits of the IV call's state: bits 24-31 and the bottom half.
            uint32_t free = (i + salt) & 0xFFFFFFu;
            uint32_t state = (byte << 16) | ((free >> 16) << 24) | (free & 0xFFFF);
            uint32_t s2 = rng3::lcPrev(state);
            uint32_t s1 = rng3::lcPrev(s2);
            uint32_t candidate = (s2 & 0xFFFF0000u) | (s1 >> 16);
            if (candidate % 25 != req.nature) continue;
            if ((candidate & 1) != req.abilitySlot) continue;
            if (pokemon::isShiny(candidate, req.tid, req.sid) != req.shiny) continue;
            if (pokemon::edit4::genderFromPid(req.species, candidate) != req.gender) continue;
            *pid = candidate;
            ivs[pokemon::kHp] = static_cast<uint8_t>(byte & 31);
            ivs[pokemon::kAtk] = static_cast<uint8_t>(byte >> 5);
            for (int s = pokemon::kDef; s < pokemon::kStatCount; ++s) ivs[s] = 0;
            return true;
        }
    }
    return false;
}

}  // namespace pktool::legality::roamer
