#include "core/legality/rng3.h"

#include "core/data/events3.h"
#include "core/util/lcrng_solve.h"

namespace pktool::legality::rng3 {
namespace {

using pokemon::kAtk;
using pokemon::kDef;
using pokemon::kHp;
using pokemon::kSpA;
using pokemon::kSpD;
using pokemon::kSpe;

uint32_t out16(uint32_t state) { return state >> 16; }
uint32_t out15(uint32_t state) { return (state >> 16) & 0x7FFF; }

uint32_t lcPrev5(uint32_t s) {
    for (int i = 0; i < 5; ++i) s = lcPrev(s);
    return s;
}

uint32_t xdNextN(uint32_t s, int n) {
    for (int i = 0; i < n; ++i) s = xdNext(s);
    return s;
}

bool isShiny3(uint32_t idXor, uint32_t pid) { return (idXor ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8; }

// Origin states of two successive 16-bit XD outputs (`first`, `second`): the
// state before the call that produced `first`.
int xdOrigins(uint32_t first, uint32_t second, uint32_t* out, int max) {
    uint16_t bottoms[32];
    const int found = util::lcgSolveTop16(util::kGameCubeLcg, first, second, bottoms, 32);
    int count = 0;
    for (int n = 0; n < found && count < max; ++n) out[count++] = xdPrev((first << 16) | bottoms[n]);
    return count;
}

bool xdIvsMatch(uint32_t state, uint32_t iv1, uint32_t iv2) {
    uint32_t a = xdNext(state);
    uint32_t b = xdNext(a);
    return out15(a) == iv1 && out15(b) == iv2;
}

// Colosseum starters are male and never shiny: the game draws PID pairs until one fits.
uint32_t coloStarterPid(uint32_t* state, uint32_t id32) {
    for (int attempt = 0; attempt < 4096; ++attempt) {
        uint32_t a = xdNext(*state);
        uint32_t b = xdNext(a);
        *state = b;
        uint32_t pid = (out16(a) << 16) | out16(b);
        bool male = (pid & 0xFF) >= 31;  // Eevee's 12.5% female ratio
        if (male && !isShiny3((id32 >> 16) ^ (id32 & 0xFFFF), pid)) return pid;
    }
    return 0;
}

// ---- Channel Jirachi: the menu pattern and the accept animation consume random calls.

bool channelValidAccept(uint32_t* seed, uint32_t pivot) {
    if (pivot == 0) {
        *seed = xdPrev(*seed);
        *seed = xdPrev(*seed);
        if (out16(*seed) <= 0x547A) return false;
        *seed = xdPrev(*seed);
        if (out16(*seed) <= 0x4000) return false;
    } else if (pivot == 1) {
        *seed = xdPrev(*seed);
        if (out16(*seed) > 0x4000) return false;
    } else {
        *seed = xdPrev(*seed);
        if (out16(*seed) > 0x547A) return false;
        *seed = xdPrev(*seed);
        if (out16(*seed) <= 0x4000) return false;
    }
    for (int i = 0; i < 5; ++i) *seed = xdPrev(*seed);
    return true;
}

bool channelValidMenu(uint32_t* seed) {
    uint32_t end = *seed >> 30;
    if (end == 0) return false;
    uint32_t seen = 1u << end;
    for (int guard = 0; guard < 4096; ++guard) {
        *seed = xdPrev(*seed);
        uint32_t pattern = *seed >> 30;
        if (pattern == end) return false;
        seen |= 1u << pattern;
        if (seen >= 0xE) return true;
    }
    return false;
}

bool channelPossible(uint32_t seed) {
    for (uint32_t pivot = 0; pivot < 3; ++pivot) {
        uint32_t origin = seed;
        if (channelValidAccept(&origin, pivot) && channelValidMenu(&origin)) return true;
    }
    return false;
}

}  // namespace

uint32_t ivWord1(const uint8_t ivs[pokemon::kStatCount]) {
    return static_cast<uint32_t>(ivs[kHp] | (ivs[kAtk] << 5) | (ivs[kDef] << 10));
}

uint32_t ivWord2(const uint8_t ivs[pokemon::kStatCount]) {
    return static_cast<uint32_t>(ivs[kSpe] | (ivs[kSpA] << 5) | (ivs[kSpD] << 10));
}

uint32_t bacdRegular(uint32_t a16, uint32_t b16) { return (a16 << 16) | b16; }

uint32_t bacdAntiShiny(uint32_t pid, uint32_t idXor) {
    return isShiny3(idXor, pid) ? ((pid + 8) & 0xFFFFFFF8u) : pid;
}

uint32_t bacdAntiXor(uint32_t a16, uint32_t b16, uint32_t idXor) { return ((a16 ^ (idXor ^ b16)) << 16) | b16; }

uint32_t bacdForceShiny(uint32_t x16, uint32_t b16, uint32_t idXor) {
    return (x16 << 16) | (((idXor ^ x16) & 0xFFF8) | (b16 & 7));
}

int findBacd(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint32_t idXor, BacdMatch* out, int max) {
    const uint32_t iv1 = ivWord1(ivs);
    const uint32_t iv2 = ivWord2(ivs);
    int count = 0;
    // x: the state after the B call (the one the first IV word is drawn from).
    for (uint32_t top = 0; top < 2 && count < max; ++top) {
        uint16_t bottoms[32];
        const int found = util::lcgSolveWord15(util::kPokemonLcg, (top << 15) | iv1, iv2, bottoms, 32);
        for (int n = 0; n < found && count < max; ++n) {
            uint32_t ivState = (top << 31) | (iv1 << 16) | bottoms[n];
            uint32_t x = lcPrev(ivState);
            uint32_t b16 = out16(x);
            uint32_t stateA = lcPrev(x);
            uint32_t a16 = out16(stateA);
            uint32_t expect = bacdRegular(a16, b16);
            BacdType type = BacdType::Regular;
            uint32_t origin = lcPrev(stateA);
            if (expect != pid) {
                if (!isShiny3(idXor, pid)) {
                    if (((expect + 8) & 0xFFFFFFF8u) == pid) {
                        type = BacdType::Anti;
                    } else if ((a16 & ~7u) != 0 && bacdAntiXor(a16, b16, idXor) == pid) {
                        type = BacdType::AntiXor;
                    } else {
                        continue;
                    }
                } else {
                    uint32_t x16 = out16(lcPrev(stateA));
                    if (x16 == (pid >> 16) && bacdForceShiny(x16, b16, idXor) == pid) {
                        type = BacdType::ForceShiny;
                        origin = lcPrev(lcPrev(stateA));
                    } else {
                        continue;
                    }
                }
            }
            out[count++] = {type, origin};
        }
    }
    return count;
}

int bacdOtGender(uint8_t rule, uint32_t origin) {
    auto after = [&](int calls) {
        uint32_t s = origin;
        for (int i = 0; i < calls; ++i) s = lcNext(s);
        return s;
    };
    switch (rule) {
        case data::kEvOtOnly0: return 0;
        case data::kEvOtOnly1: return 1;
        case data::kEvOtRandD3Male:
        case data::kEvOtRandD3Female:
            return static_cast<int>((out16(after(6)) / 3) & 1);
        case data::kEvOtRandD3: return static_cast<int>((out16(after(5)) / 3) & 1);
        case data::kEvOtRandS3: return static_cast<int>((out16(after(5)) >> 3) & 1);
        case data::kEvOtRandS7: return static_cast<int>(((out16(after(5)) >> 7) & 1) ^ 1);
        case data::kEvOtRandSG15: return static_cast<int>((out16(after(6)) >> 15) & 1);
        default: return -1;
    }
}

bool mystryMewOrigin(uint32_t origin) {
    const uint16_t* seeds = data::generated::kMystryMewSeeds;
    const int count = data::generated::kMystryMewSeedCount;
    auto indexOf = [&](uint32_t seed) {
        for (int i = 0; i < count; ++i) {
            if (seeds[i] == seed) return i;
        }
        return -1;
    };
    int index = -1;
    int sub = -1;
    if (origin <= 0xFFFF) {
        index = indexOf(origin);
        sub = 0;
    } else {
        uint32_t seed = origin;
        for (int i = 1; i < 5; ++i) {
            seed = lcPrev5(seed);
            if (seed <= 0xFFFF) {
                index = indexOf(seed);
                sub = i;
                break;
            }
        }
    }
    if (index < 0) return false;
    uint32_t seed = sub == 0 ? origin : seeds[index];
    return !(seed == 0x6065 && sub != 2);  // the unreleased siblings of 0x6065
}

ChannelFrame channelFrame(uint32_t seed) {
    ChannelFrame f = {};
    seed = xdNext(seed);
    f.sid = static_cast<uint16_t>(out16(seed));
    seed = xdNext(seed);
    uint32_t pid1 = out16(seed);
    seed = xdNext(seed);
    uint32_t pid2 = out16(seed);
    f.pid = (pid1 << 16) | pid2;
    if (static_cast<uint32_t>(pid2 > 7 ? 0 : 1) != (pid1 ^ f.sid ^ 40122u)) f.pid ^= 0x80000000u;
    seed = xdNext(seed);
    f.heldItem3 = static_cast<uint16_t>((out16(seed) >> 15) + 169);
    seed = xdNext(seed);
    f.version = static_cast<uint8_t>(1 + (out16(seed) >> 15));
    seed = xdNext(seed);
    f.otGender = static_cast<uint8_t>(out16(seed) >> 15);
    uint32_t iv32 = 0;
    for (int i = 0; i < 6; ++i) {
        seed = xdNext(seed);
        iv32 |= (seed >> 27) << (5 * i);
    }
    for (int s = 0; s < pokemon::kStatCount; ++s) f.ivs[s] = static_cast<uint8_t>((iv32 >> (5 * s)) & 31);
    return f;
}

uint32_t channelSkipToPidIv(uint32_t seed) {
    uint32_t seen = 0;
    for (int guard = 0; guard < 4096; ++guard) {
        seed = xdNext(seed);
        seen |= 1u << (seed >> 30);
        if (seen >= 0xE) break;
    }
    seed = xdNextN(seed, 4);
    seed = xdNext(seed);
    if (out16(seed) <= 0x4000) return xdNext(seed);
    seed = xdNext(seed);
    if (out16(seed) <= 0x547A) return xdNext(seed);
    return xdNextN(seed, 2);
}

bool matchChannel(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t sid, uint8_t version, uint8_t otGender) {
    uint32_t top = pid & 0xFFFF0000u;
    uint32_t bot = pid << 16;
    uint32_t undo = (top >> 16) ^ 0x8000u;
    if (static_cast<uint32_t>(undo > 7 ? 0 : 1) != ((bot >> 16) ^ sid ^ 40122u)) top = undo << 16;
    uint32_t iv32 = 0;
    for (int s = 0; s < pokemon::kStatCount; ++s) iv32 |= static_cast<uint32_t>(ivs[s]) << (5 * s);

    uint32_t origins[4];
    int count = xdOrigins(top >> 16, bot >> 16, origins, 4);
    for (int i = 0; i < count; ++i) {
        uint32_t seed = origins[i];
        uint32_t c = xdNextN(seed, 3);
        uint32_t d = xdNext(c);  // version
        if ((d >> 31) + 1 != version) continue;
        uint32_t e = xdNext(d);  // OT gender
        if ((e >> 31) != otGender) continue;
        uint32_t ivSeed = e;
        uint32_t ivs32 = 0;
        for (int k = 0; k < 6; ++k) {
            ivSeed = xdNext(ivSeed);
            ivs32 |= (ivSeed >> 27) << (5 * k);
        }
        if (ivs32 != iv32) continue;
        if ((seed >> 16) != sid) continue;
        if (channelPossible(xdPrev(seed))) return true;
    }
    return false;
}

int findCxdAll(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t tid, uint16_t sid, CxdMatch* out, int max) {
    const uint32_t iv1 = ivWord1(ivs);
    const uint32_t iv2 = ivWord2(ivs);
    const uint32_t hi = pid >> 16;
    const uint32_t lo = pid & 0xFFFF;
    const uint32_t tsv = static_cast<uint32_t>(tid ^ sid) >> 3;
    uint32_t origins[8];
    int count = xdOrigins(hi, lo, origins, 8);
    int found = 0;
    for (int i = 0; i < count && found < max; ++i) {
        uint32_t seed = origins[i];  // the state of the call before the PID's high half
        uint32_t b = xdPrev(seed);
        uint32_t a = xdPrev(b);
        if (out15(a) == iv1 && out15(b) == iv2) {
            out[found++] = {CxdType::Plain, xdPrev(a)};
            continue;
        }

        if (((hi ^ lo) >> 3) == tsv) continue;  // already shiny: never re-rolled
        uint32_t p2 = seed;
        uint32_t p1 = b;
        uint32_t psv = (p2 ^ p1) >> 19;
        if (psv != tsv) continue;  // the previous PID must have been shiny
        for (int step = 0; step < 64; ++step) {
            b = xdPrev(a);
            a = xdPrev(b);
            if (out15(a) == iv1 && out15(b) == iv2) {
                out[found++] = {CxdType::Anti, xdPrev(a)};
                break;
            }
            p2 = xdPrev(p1);
            p1 = xdPrev(p2);
            psv = (p2 ^ p1) >> 19;
            if (psv != tsv) break;
        }
    }
    return found;
}

CxdType findCxd(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t tid, uint16_t sid) {
    CxdMatch m;
    return findCxdAll(pid, ivs, tid, sid, &m, 1) > 0 ? m.type : CxdType::None;
}

int pidOrigins(uint32_t pid, uint32_t* out, int max) { return xdOrigins(pid >> 16, pid & 0xFFFF, out, max); }

// ---- Poke Spots (PKHeX MethodPokeSpot)

namespace {

// The animation rolls before the frame (IsValidAnimation).
bool validAnimation(uint32_t seed) {
    seed = xdPrev(seed);
    uint32_t animation = out16(seed) % 10;
    if (animation < 5 && animation != 3) return true;
    seed = xdPrev(seed);
    animation = out16(seed) % 10;
    if (animation >= 5 && animation != 8) return true;
    seed = xdPrev(xdPrev(xdPrev(seed)));
    return out16(seed) % 10 == 8;
}

bool validActivation(uint8_t slot, uint32_t seed) {
    uint32_t esv = out16(seed) % 100;
    if ((esv < 50 ? 0 : esv < 85 ? 1 : 2) != slot) return false;
    seed = xdPrev(seed);
    if (out16(seed) % 3 == 0) return true;  // neither Munchlax nor Bonsly can spawn
    if (out16(seed) % 100 < 10) return false;
    seed = xdPrev(seed);
    return out16(seed) % 3 == 0;  // only Munchlax
}

}  // namespace

bool pokeSpotActivation(uint8_t slot, uint32_t seed) { return validActivation(slot, seed); }

bool pokeSpotIvFrame(uint32_t preIv, uint8_t ivs[pokemon::kStatCount], uint8_t* abilityBit) {
    uint32_t origin = preIv;
    for (int k = 0; k < 6; ++k) origin = xdPrev(origin);
    if (!validAnimation(origin)) return false;
    uint32_t s = xdNext(preIv);
    uint32_t iv1 = out15(s);
    s = xdNext(s);
    uint32_t iv2 = out15(s);
    uint32_t iv32 = (iv2 << 15) | iv1;
    for (int k = 0; k < pokemon::kStatCount; ++k) ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
    *abilityBit = static_cast<uint8_t>(out16(xdNext(s)) & 1);
    return true;
}

bool matchPokeSpot(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint8_t slot) {
    uint32_t origins[8];
    int count = pidOrigins(pid, origins, 8);
    bool pidOk = false;
    for (int i = 0; i < count && !pidOk; ++i) pidOk = validActivation(slot, origins[i]);
    if (!pidOk) return false;
    const uint32_t iv1 = ivWord1(ivs);
    const uint32_t iv2 = ivWord2(ivs);
    for (uint32_t top = 0; top < 2; ++top) {
        uint16_t bottoms[32];
        const int found = util::lcgSolveWord15(util::kGameCubeLcg, (top << 15) | iv1, iv2, bottoms, 32);
        for (int n = 0; n < found; ++n) {
            uint32_t state = (top << 31) | (iv1 << 16) | bottoms[n];
            uint32_t origin = xdPrev(state);
            for (int k = 0; k < 6; ++k) origin = xdPrev(origin);
            if (validAnimation(origin)) return true;
        }
    }
    return false;
}

bool matchColoStarter(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t tid, uint16_t sid, uint16_t species) {
    const uint32_t iv1 = ivWord1(ivs);
    const uint32_t iv2 = ivWord2(ivs);
    const uint32_t id32 = (static_cast<uint32_t>(sid) << 16) | tid;
    const bool umbreon = species == 197;
    uint32_t origins[4];
    int count = xdOrigins(tid, sid, origins, 4);
    for (int i = 0; i < count; ++i) {
        uint32_t seed = origins[i];
        if (umbreon && !xdIvsMatch(xdNextN(seed, 4), iv1, iv2)) continue;
        uint32_t state = xdNextN(seed, 7);
        uint32_t expect = coloStarterPid(&state, id32);
        if (umbreon) {
            if (expect == pid) return true;
            continue;
        }
        uint32_t afterFake = xdNextN(state, 2);
        if (!xdIvsMatch(afterFake, iv1, iv2)) continue;
        uint32_t pidState = xdNextN(afterFake, 3);
        if (coloStarterPid(&pidState, id32) == pid) return true;
    }
    return false;
}

bool matchXdStarter(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t tid, uint16_t sid) {
    const uint32_t iv1 = ivWord1(ivs);
    const uint32_t iv2 = ivWord2(ivs);
    uint32_t origins[4];
    int count = xdOrigins(tid, sid, origins, 4);
    for (int i = 0; i < count; ++i) {
        uint32_t seed = origins[i];
        if (!xdIvsMatch(xdNextN(seed, 4), iv1, iv2)) continue;
        uint32_t state = xdNextN(seed, 7);
        uint32_t a = xdNext(state);
        uint32_t b = xdNext(a);
        if (((out16(a) << 16) | out16(b)) == pid) return true;
    }
    return false;
}

namespace {

void ivsFromState(uint32_t state, uint8_t ivs[pokemon::kStatCount]) {
    uint32_t a = xdNext(state);
    uint32_t b = xdNext(a);
    uint32_t iv32 = (out15(b) << 15) | out15(a);
    for (int k = 0; k < pokemon::kStatCount; ++k) ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
}

}  // namespace

bool coloStarterFrame(uint16_t tid, uint16_t sid, uint16_t species, uint32_t* pid, uint8_t ivs[pokemon::kStatCount],
                      uint8_t* abilityBit) {
    const uint32_t id32 = (static_cast<uint32_t>(sid) << 16) | tid;
    uint32_t origins[4];
    int count = xdOrigins(tid, sid, origins, 4);
    for (int i = 0; i < count; ++i) {
        uint32_t seed = origins[i];
        uint32_t state = xdNextN(seed, 7);
        if (species == 197) {  // Umbreon
            ivsFromState(xdNextN(seed, 4), ivs);
            *abilityBit = static_cast<uint8_t>(out16(state) & 1);
            *pid = coloStarterPid(&state, id32);
        } else {  // Espeon follows Umbreon's PID search
            coloStarterPid(&state, id32);
            uint32_t afterFake = xdNextN(state, 2);
            ivsFromState(afterFake, ivs);
            uint32_t pidState = xdNextN(afterFake, 3);
            *abilityBit = static_cast<uint8_t>(out16(pidState) & 1);
            *pid = coloStarterPid(&pidState, id32);
        }
        if (*pid != 0) return true;
    }
    return false;
}

bool xdStarterFrame(uint16_t tid, uint16_t sid, uint32_t* pid, uint8_t ivs[pokemon::kStatCount], uint8_t* abilityBit) {
    uint32_t origins[4];
    int count = xdOrigins(tid, sid, origins, 4);
    if (count == 0) return false;
    uint32_t seed = origins[0];
    ivsFromState(xdNextN(seed, 4), ivs);
    uint32_t state = xdNextN(seed, 7);
    *abilityBit = static_cast<uint8_t>(out16(state) & 1);
    uint32_t a = xdNext(state);
    uint32_t b = xdNext(a);
    *pid = (out16(a) << 16) | out16(b);
    return true;
}

}  // namespace pktool::legality::rng3
