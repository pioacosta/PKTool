#include "core/legality/pid_iv.h"

#include <cstdlib>

#include "core/data/personal.h"
#include "core/legality/rng4.h"
#include "core/pokemon/edit4.h"
#include "core/util/lcrng.h"
#include "core/util/lcrng_solve.h"

namespace pktool::legality {

const char* methodName(PidIvMethod method) {
    switch (method) {
        case PidIvMethod::Method1:
            return "Method 1/J/K";
        case PidIvMethod::Method2:
            return "Method 2";
        case PidIvMethod::Method4:
            return "Method 4";
        case PidIvMethod::Method3:
            return "Method 3";
        case PidIvMethod::CuteCharm:
            return "Cute Charm";
        case PidIvMethod::None:
            break;
    }
    return "none";
}

void ivsFromWords(uint16_t word1, uint16_t word2, uint8_t ivs[pokemon::kStatCount]) {
    // word1: HP, Atk, Def; word2: Spe, SpA, SpD (5 bits each).
    ivs[pokemon::kHp] = word1 & 31;
    ivs[pokemon::kAtk] = (word1 >> 5) & 31;
    ivs[pokemon::kDef] = (word1 >> 10) & 31;
    ivs[pokemon::kSpe] = word2 & 31;
    ivs[pokemon::kSpA] = (word2 >> 5) & 31;
    ivs[pokemon::kSpD] = (word2 >> 10) & 31;
}

bool ivsFromConsecutiveCalls(const uint8_t ivs[pokemon::kStatCount]) {
    const uint32_t word1 = ivs[pokemon::kHp] | (ivs[pokemon::kAtk] << 5) | (ivs[pokemon::kDef] << 10);
    const uint32_t word2 = ivs[pokemon::kSpe] | (ivs[pokemon::kSpA] << 5) | (ivs[pokemon::kSpD] << 10);
    // The IV word is bits 16-30 of the state; bit 31 and the low half are free.
    uint16_t bottoms[32];
    for (uint32_t top = 0; top < 2; ++top) {
        if (util::lcgSolveWord15(util::kPokemonLcg, (top << 15) | word1, word2, bottoms, 32) > 0) return true;
    }
    return false;
}

uint32_t cuteCharmPidMale(uint8_t genderRatio, uint32_t nature) {
    return 25u * (genderRatio / 25u + 1u) + nature;
}

namespace {

bool isAzurillMalePid(uint32_t pid) {
    return pid >= 0xC8 && pid <= 0xE0;  // 200 + nature: the male buffer of Azurill (ratio 191)
}

}  // namespace

bool isCuteCharmPid(uint16_t species, pokemon::Gender gender, uint32_t pid) {
    if (pid > 0xFF) return false;
    // Species met before they evolved into a different gender ratio.
    switch (species) {
        case 292: species = 290; gender = pokemon::edit4::genderFromPid(290, pid); break;  // Shedinja: Nincada
        case 413: species = 412; gender = pokemon::Gender::Female; break;                    // Wormadam: Burmy
        case 414: species = 412; gender = pokemon::Gender::Male; break;                      // Mothim: Burmy
        case 416: species = 415; gender = pokemon::Gender::Female; break;                    // Vespiquen: Combee
        case 475: species = 281; gender = pokemon::Gender::Male; break;                      // Gallade: Kirlia
        case 478: species = 361; gender = pokemon::Gender::Female; break;                    // Froslass: Snorunt
        case 183:
        case 184:
            if (isAzurillMalePid(pid)) {  // Marill line from a male Azurill
                species = 298;
                gender = pokemon::Gender::Male;
            }
            break;
        default: break;
    }
    const data::PersonalInfo* info = data::personal4(species);
    if (info == nullptr || info->genderRatio >= data::kGenderRatioFemaleOnly) return false;  // no gender to bend
    if (gender == pokemon::Gender::Male) return pid == cuteCharmPidMale(info->genderRatio, pid % 25);
    if (gender == pokemon::Gender::Female) return pid < 25;
    return false;
}

bool isChainShinyFrame(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t tid, uint16_t sid) {
    if (!pokemon::isShiny(pid, tid, sid)) return false;
    const uint32_t word1 = ivs[pokemon::kHp] | (ivs[pokemon::kAtk] << 5) | (ivs[pokemon::kDef] << 10);
    const uint32_t word2 = ivs[pokemon::kSpe] | (ivs[pokemon::kSpA] << 5) | (ivs[pokemon::kSpD] << 10);
    for (uint32_t top = 0; top < 2; ++top) {
        uint16_t bottoms[32];
        const int found = util::lcgSolveWord15(util::kPokemonLcg, (top << 15) | word1, word2, bottoms, 32);
        for (int n = 0; n < found; ++n) {
            const uint32_t iv1State = (((top << 15) | word1) << 16) | bottoms[n];
            // iv1State follows 15 PID calls; call 15 gives bit 15 of the low half.
            uint32_t s = lcrngPrev(iv1State);
            int bit = 15;
            for (; bit >= 3; --bit) {
                if (((s >> 16) & 1) != ((pid >> bit) & 1)) break;
                s = lcrngPrev(s);
            }
            if (bit != 2) continue;
            // s is now the state after call 2 (high half bits 0-2), one back is call 1.
            if (((s >> 16) & 7) != ((pid >> 16) & 7)) continue;
            if (((lcrngPrev(s) >> 16) & 7) != (pid & 7)) continue;
            const uint32_t high = (((pid & 0xFFFF) ^ tid ^ sid) & 0xFFF8u) | ((s >> 16) & 7);
            if (high == (pid >> 16)) return true;
        }
    }
    return false;
}

PidIvMethod findPidIvMethod(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint32_t* seedOut) {
    const uint32_t low = pid & 0xFFFF;
    const uint32_t high = pid >> 16;
    const uint16_t word1 = static_cast<uint16_t>(ivs[pokemon::kHp] | (ivs[pokemon::kAtk] << 5) | (ivs[pokemon::kDef] << 10));
    const uint16_t word2 = static_cast<uint16_t>(ivs[pokemon::kSpe] | (ivs[pokemon::kSpA] << 5) | (ivs[pokemon::kSpD] << 10));

    // The RNG state right after the PID-low call has `low` in its top half; the bottom halves
    // for which the next call gives `high` are found directly, not by trying all 65536.
    uint16_t bottoms[32];
    const int candidates = util::lcgSolveTop16(util::kPokemonLcg, low, high, bottoms, 32);
    for (int n = 0; n < candidates; ++n) {
        uint32_t s1 = (low << 16) | bottoms[n];
        uint32_t s2 = util::lcrngNext(s1);
        uint32_t s3 = util::lcrngNext(s2);
        uint32_t s4 = util::lcrngNext(s3);
        uint32_t s5 = util::lcrngNext(s4);
        uint16_t r3 = static_cast<uint16_t>((s3 >> 16) & 0x7FFF);
        uint16_t r4 = static_cast<uint16_t>((s4 >> 16) & 0x7FFF);
        uint16_t r5 = static_cast<uint16_t>((s5 >> 16) & 0x7FFF);
        PidIvMethod found = PidIvMethod::None;
        if (r3 == word1 && r4 == word2) {
            found = PidIvMethod::Method1;
        } else if (r4 == word1 && r5 == word2) {
            found = PidIvMethod::Method2;
        } else if (r3 == word1 && r5 == word2) {
            found = PidIvMethod::Method4;
        }
        if (found != PidIvMethod::None) {
            if (seedOut != nullptr) {
                *seedOut = s1;
            }
            return found;
        }
    }
    if (isCuteCharmPid(pid) && ivsFromConsecutiveCalls(ivs)) {
        return PidIvMethod::CuteCharm;
    }
    return PidIvMethod::None;
}

namespace {

using util::kLcrngMul;
using util::kLcrngMulPrev;
using util::lcrngNext;

// `first16` is the half drawn first, `second16` the next one; then the IV words.
PidIvMethod findOrdered(uint32_t first16, uint32_t second16, uint16_t word1, uint16_t word2) {
    auto iv = [](uint32_t s) { return static_cast<uint16_t>((s >> 16) & 0x7FFF); };
    // Candidates: second16 follows one call (Methods 1, 2, 4) or two (Method 3), in order.
    uint16_t one[32], two[32];
    const int oneCount = util::lcgSolveTop16(util::kPokemonLcg, first16, second16, one, 32);
    const int twoCount = util::lcgSolveTop16(util::lcgJump(util::kPokemonLcg, 2), first16, second16, two, 32);
    int a = 0, b = 0;
    while (a < oneCount || b < twoCount) {
        uint32_t bottom;
        if (b >= twoCount || (a < oneCount && one[a] <= two[b])) {
            bottom = one[a];
            if (b < twoCount && two[b] == bottom) ++b;
            ++a;
        } else {
            bottom = two[b++];
        }
        const uint32_t s1 = (first16 << 16) | bottom;
        const uint32_t s2 = lcrngNext(s1);
        const uint32_t s3 = lcrngNext(s2);
        const uint32_t s4 = lcrngNext(s3);
        const uint32_t s5 = lcrngNext(s4);
        if ((s2 >> 16) == second16) {
            if (iv(s3) == word1 && iv(s4) == word2) return PidIvMethod::Method1;
            if (iv(s4) == word1 && iv(s5) == word2) return PidIvMethod::Method2;
            if (iv(s3) == word1 && iv(s5) == word2) return PidIvMethod::Method4;
        }
        if ((s3 >> 16) == second16 && iv(s4) == word1 && iv(s5) == word2) return PidIvMethod::Method3;
    }
    return PidIvMethod::None;
}

bool meets(const PidIvRequest& req, uint32_t pid) {
    return pid % 25 == req.nature && (pid & 1) == (req.abilitySlot & 1u) &&
           pokemon::isShiny(pid, req.tid, req.sid) == req.shiny &&
           pokemon::edit4::genderFromPid(req.species, pid) == req.gender;
}

// Records the frame if it is closer than the best one (so Method 1, offered
// first, wins ties).
void offer(PidIvResult3& best, PidIvMethod method, uint32_t pid, uint32_t word1, uint32_t word2, const uint8_t* target) {
    uint8_t ivs[pokemon::kStatCount];
    ivsFromWords(static_cast<uint16_t>(word1), static_cast<uint16_t>(word2), ivs);
    int d = 0;
    for (int s = 0; s < pokemon::kStatCount; ++s) d += std::abs(ivs[s] - target[s]);
    if (best.frame.found && d >= best.frame.distance) return;
    best.method = method;
    best.frame.found = true;
    best.frame.pid = pid;
    best.frame.distance = d;
    for (int s = 0; s < pokemon::kStatCount; ++s) best.frame.ivs[s] = ivs[s];
}

// Method 2 (a call between the PID and the IVs) or Method 4 (a call between
// the two IV words), non-shiny: IV word 1 values by growing distance from the
// target, the PID found by walking the RNG back.
void searchIvs24(const PidIvRequest& req, PidIvMethod method, int effort, PidIvResult3& best) {
    const uint8_t* t = req.targetIvs;
    int budget = 2 * effort;
    for (int dist = 0; dist <= 93 && budget > 0; ++dist) {
        if (best.frame.found && dist >= best.frame.distance) return;
        for (int hp = 0; hp < 32 && budget > 0; ++hp) {
            for (int atk = 0; atk < 32 && budget > 0; ++atk) {
                int rem = dist - std::abs(hp - t[pokemon::kHp]) - std::abs(atk - t[pokemon::kAtk]);
                if (rem < 0) continue;
                for (int sign = 0; sign < (rem == 0 ? 1 : 2) && budget > 0; ++sign) {
                    int def = t[pokemon::kDef] + (sign == 0 ? rem : -rem);
                    if (def < 0 || def > 31) continue;
                    --budget;
                    const uint32_t word1 = static_cast<uint32_t>(hp | (atk << 5) | (def << 10));
                    // Consecutive iv1 values step iv2, the PID halves... by constants: add, don't multiply.
                    constexpr uint32_t kPrev2 = kLcrngMulPrev * kLcrngMulPrev;
                    constexpr uint32_t kPrev3 = kPrev2 * kLcrngMulPrev;
                    constexpr uint32_t kNext2 = kLcrngMul * kLcrngMul;
                    const bool method2 = method == PidIvMethod::Method2;
                    const uint32_t highStep = method2 ? kPrev2 : kLcrngMulPrev;
                    const uint32_t lowStep = method2 ? kPrev3 : kPrev2;
                    const uint32_t wordStep = method == PidIvMethod::Method4 ? kNext2 : kLcrngMul;
                    for (uint32_t top = 0; top < 2; ++top) {
                        const uint32_t iv1 = (top << 31) | (word1 << 16);
                        uint32_t iv2 = lcrngNext(iv1);
                        if (method == PidIvMethod::Method4) iv2 = lcrngNext(iv2);
                        uint32_t high = lcrngPrev(iv1);
                        if (method2) high = lcrngPrev(high);
                        uint32_t low = lcrngPrev(high);
                        for (uint32_t bottom = 0; bottom < 0x10000;
                             ++bottom, iv2 += wordStep, high += highStep, low += lowStep) {
                            if (((low >> 16) & 1u) != (req.abilitySlot & 1u)) continue;  // cheapest test first
                            const uint32_t pid = (high & 0xFFFF0000u) | (low >> 16);
                            if (!meets(req, pid)) continue;
                            offer(best, method, pid, word1, (iv2 >> 16) & 0x7FFF, t);
                        }
                    }
                }
            }
        }
    }
}

// Shiny: pick shiny PIDs with the wanted traits, then the frames producing them.
void searchPid24(const PidIvRequest& req, int effort, uint32_t salt, PidIvResult3& best) {
    auto iv = [](uint32_t s) { return (s >> 16) & 0x7FFFu; };
    uint32_t rng = salt ^ 0x9E3779B9u;
    int pids = 4 * effort;
    for (int attempts = 0; pids > 0 && attempts < 200000; ++attempts) {
        rng = lcrngNext(rng);
        const uint32_t low = rng >> 16;
        rng = lcrngNext(rng);
        const uint32_t high = (low ^ req.tid ^ req.sid ^ ((rng >> 16) & 7)) & 0xFFFF;
        const uint32_t pid = (high << 16) | low;
        if (!meets(req, pid)) continue;
        --pids;
        uint16_t bottoms[32];
        const int found = util::lcgSolveTop16(util::kPokemonLcg, low, high, bottoms, 32);
        for (int n = 0; n < found; ++n) {
            const uint32_t s1 = (low << 16) | bottoms[n];
            const uint32_t s2 = lcrngNext(s1);
            const uint32_t s3 = lcrngNext(s2);
            const uint32_t s4 = lcrngNext(s3);
            const uint32_t s5 = lcrngNext(s4);
            offer(best, PidIvMethod::Method2, pid, iv(s4), iv(s5), req.targetIvs);
            offer(best, PidIvMethod::Method4, pid, iv(s3), iv(s5), req.targetIvs);
        }
    }
}

}  // namespace

PidIvMethod findPidIvMethod3(uint32_t pid, const uint8_t ivs[pokemon::kStatCount], uint16_t species) {
    const uint16_t word1 = static_cast<uint16_t>(ivs[pokemon::kHp] | (ivs[pokemon::kAtk] << 5) | (ivs[pokemon::kDef] << 10));
    const uint16_t word2 = static_cast<uint16_t>(ivs[pokemon::kSpe] | (ivs[pokemon::kSpA] << 5) | (ivs[pokemon::kSpD] << 10));
    constexpr uint16_t kUnown = 201;
    PidIvMethod m = findOrdered(pid & 0xFFFF, pid >> 16, word1, word2);
    if (m == PidIvMethod::None && species == kUnown) m = findOrdered(pid >> 16, pid & 0xFFFF, word1, word2);
    return m;
}

PidIvResult3 generatePidIv3(const PidIvRequest& request, int effort, uint32_t salt, bool allowMethods24) {
    PidIvResult3 best;
    PidIvRequest req = request;
    req.method = RngMethod::Method1;
    req.allowSync = false;
    req.allowCuteCharm = false;
    best.frame = generatePidIv(req, effort, salt);
    if (best.frame.found) best.method = PidIvMethod::Method1;
    if (!allowMethods24 || effort < 1 || req.nature >= 25 || !pokemon::edit4::canHaveGender(req.species, req.gender)) {
        return best;
    }
    if (best.frame.found && best.frame.distance == 0) return best;  // exact with Method 1
    if (req.shiny) {
        searchPid24(req, effort, salt, best);
    } else {
        searchIvs24(req, PidIvMethod::Method2, effort, best);
        searchIvs24(req, PidIvMethod::Method4, effort, best);
    }
    return best;
}

}  // namespace pktool::legality
