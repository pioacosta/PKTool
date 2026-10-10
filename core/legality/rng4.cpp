#include "core/legality/rng4.h"

#include <cstdlib>

#include "core/data/personal.h"
#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/pokemon/edit4.h"
#include "core/util/lcrng.h"
#include "core/util/lcrng_solve.h"

namespace pktool::legality {
namespace {

using util::kLcrngMul;
using util::kLcrngMulPrev;
using util::lcrngNext;

// Upper bound on rejected PID pairs considered before the nature call.
constexpr int kMaxRerolls = 64;

uint8_t natureOf(RngMethod method, uint32_t rand16) {
    return static_cast<uint8_t>(method == RngMethod::MethodJ ? rand16 / 0xA3E : rand16 % 25);
}

// Synchronize coin flip: J passes on bit 15 clear, K on bit 0 clear.
bool syncPasses(RngMethod method, uint32_t rand16) {
    return method == RngMethod::MethodJ ? (rand16 >> 15) == 0 : (rand16 & 1) == 0;
}

uint32_t pidFromStates(uint32_t lowState, uint32_t highState) {
    return (highState & 0xFFFF0000u) | (lowState >> 16);
}

bool meetsConstraints(const PidIvRequest& req, uint32_t pid) {
    return pid % 25 == req.nature && (pid & 1) == (req.abilitySlot & 1u) &&
           pokemon::isShiny(pid, req.tid, req.sid) == req.shiny &&
           pokemon::edit4::genderFromPid(req.species, pid) == req.gender;
}

int ivDistance(const uint8_t a[pokemon::kStatCount], const uint8_t b[pokemon::kStatCount]) {
    int d = 0;
    for (int s = 0; s < pokemon::kStatCount; ++s) d += std::abs(a[s] - b[s]);
    return d;
}

int wordDistance(uint32_t word, const uint8_t* target) {
    return std::abs(static_cast<int>(word & 31) - target[0]) + std::abs(static_cast<int>((word >> 5) & 31) - target[1]) +
           std::abs(static_cast<int>((word >> 10) & 31) - target[2]);
}

// A frame is better with a smaller IV distance; at equal distance a frame
// that needs no lead is better than one that does.
void record(PidIvResult& best, uint32_t pid, uint32_t word1, uint32_t word2, const uint8_t* target,
            PidSource source = PidSource::Normal) {
    uint8_t ivs[pokemon::kStatCount];
    ivsFromWords(static_cast<uint16_t>(word1), static_cast<uint16_t>(word2), ivs);
    int d = ivDistance(ivs, target);
    if (!best.found || d < best.distance ||
        (d == best.distance && best.source != PidSource::Normal && source == PidSource::Normal)) {
        best.found = true;
        best.pid = pid;
        best.distance = d;
        best.source = source;
        for (int s = 0; s < pokemon::kStatCount; ++s) best.ivs[s] = ivs[s];
    }
}

// Which lead (if any) the nature call of a frame needs.
bool frameSource(const PidIvRequest& req, uint32_t lowState, PidSource* source) {
    if (req.method == RngMethod::Method1 || isNatureFrameValid(lowState, req.method)) {
        *source = PidSource::Normal;
        return true;
    }
    if (req.allowSync && isNatureFrameValid(lowState, req.method, true)) {
        *source = PidSource::Synchronize;
        return true;
    }
    return false;
}

// Visits IV word 1 values (HP/Atk/Def) by growing distance from the target,
// `budget` of them at most. `done(dist)` ends the walk early.
template <class Done, class Visit>
void forEachWord1(const uint8_t* t, int budget, Done done, Visit visit) {
    for (int dist = 0; dist <= 93 && budget > 0; ++dist) {
        if (done(dist)) return;
        for (int hp = 0; hp < 32 && budget > 0; ++hp) {
            for (int atk = 0; atk < 32 && budget > 0; ++atk) {
                int rem = dist - std::abs(hp - t[pokemon::kHp]) - std::abs(atk - t[pokemon::kAtk]);
                if (rem < 0) continue;
                for (int sign = 0; sign < (rem == 0 ? 1 : 2) && budget > 0; ++sign) {
                    int def = t[pokemon::kDef] + (sign == 0 ? rem : -rem);
                    if (def < 0 || def > 31) continue;
                    --budget;
                    visit(dist, static_cast<uint32_t>(hp | (atk << 5) | (def << 10)));
                }
            }
        }
    }
}

// Non-shiny: start from IV words close to the target and walk back to the PID.
void searchByIvs(const PidIvRequest& req, int effort, PidIvResult& best) {
    const uint8_t* t = req.targetIvs;
    forEachWord1(
        t, 2 * effort,  // number of IV-word-1 values to try
        [&](int dist) { return best.found && dist >= best.distance; },  // cannot improve any more
        [&](int dist, uint32_t word1) {
            // The two states before iv1 give the PID and, one step after it, the second IV
            // word. Consecutive iv1 values step all of them by constants, so the loop only
            // adds; the cheap PID tests (ability bit, nature) come first, then the rest. (The
            // order of the independent filters does not change which frame is recorded.)
            constexpr uint32_t kMulPrev2 = kLcrngMulPrev * kLcrngMulPrev;
            const uint32_t abilityBit = req.abilitySlot & 1u;
            for (uint32_t top = 0; top < 2; ++top) {
                uint32_t iv1 = (top << 31) | (word1 << 16);
                uint32_t iv2 = lcrngNext(iv1);
                uint32_t high = lcrngPrev(iv1);
                uint32_t low = lcrngPrev(high);
                for (uint32_t bottom = 0; bottom < 0x10000;
                     ++bottom, iv2 += kLcrngMul, high += kLcrngMulPrev, low += kMulPrev2) {
                    if (((low >> 16) & 1u) != abilityBit) continue;
                    const uint32_t pid = pidFromStates(low, high);
                    if (pid % 25 != req.nature) continue;
                    const uint32_t word2 = (iv2 >> 16) & 0x7FFF;
                    if (best.found && dist + wordDistance(word2, t + pokemon::kSpe) > best.distance) continue;
                    if (!meetsConstraints(req, pid)) continue;
                    PidSource source;
                    if (!frameSource(req, low, &source)) continue;
                    record(best, pid, word1, word2, t, source);
                }
            }
        });
}

// Shiny: choose shiny PIDs with the right traits, then find the RNG states
// that produce them (IVs are whatever follows).
void searchByPid(const PidIvRequest& req, int effort, uint32_t salt, PidIvResult& best) {
    uint32_t rng = salt ^ 0x9E3779B9u;
    int pids = 4 * effort;
    for (int attempts = 0; pids > 0 && attempts < 200000; ++attempts) {
        rng = lcrngNext(rng);
        uint32_t low = rng >> 16;
        rng = lcrngNext(rng);
        uint32_t shinyXor = (rng >> 16) & 7;
        uint32_t high = (low ^ req.tid ^ req.sid ^ shinyXor) & 0xFFFF;
        uint32_t pid = (high << 16) | low;
        if (!meetsConstraints(req, pid)) continue;
        --pids;
        uint16_t bottoms[32];
        const int found = util::lcgSolveTop16(util::kPokemonLcg, low, high, bottoms, 32);
        for (int n = 0; n < found; ++n) {
            uint32_t lowState = (low << 16) | bottoms[n];
            uint32_t highState = lcrngNext(lowState);
            PidSource source;
            if (!frameSource(req, lowState, &source)) continue;
            uint32_t iv1 = lcrngNext(highState);
            uint32_t iv2 = lcrngNext(iv1);
            record(best, pid, (iv1 >> 16) & 0x7FFF, (iv2 >> 16) & 0x7FFF, req.targetIvs, source);
        }
    }
}

// Cute Charm: the PID is a gender buffer plus the nature (no PID calls), so
// the IVs are free: any two consecutive RNG outputs.
bool cuteCharmPid(const PidIvRequest& req, uint32_t* pid) {
    const data::PersonalInfo* info = data::personal4(req.species);
    // The lead's Cute Charm only bends the gender of species with both genders.
    if (info == nullptr || info->genderRatio == data::kGenderRatioMaleOnly ||
        info->genderRatio >= data::kGenderRatioFemaleOnly) {
        return false;
    }
    if (req.gender == pokemon::Gender::Female) {
        *pid = req.nature;
    } else if (req.gender == pokemon::Gender::Male) {
        *pid = cuteCharmPidMale(info->genderRatio, req.nature);
    } else {
        return false;
    }
    return *pid <= 0xFF && meetsConstraints(req, *pid);
}

// IVs closest to `t` among all pairs of consecutive RNG outputs.
void searchConsecutiveIvs(const uint8_t* t, int budget, uint32_t pid, PidSource source, PidIvResult& best) {
    forEachWord1(
        t, budget, [&](int dist) { return best.found && dist >= best.distance; },
        [&](int dist, uint32_t word1) {
            for (uint32_t top = 0; top < 2; ++top) {
                uint32_t iv2 = lcrngNext((top << 31) | (word1 << 16));
                for (uint32_t bottom = 0; bottom < 0x10000; ++bottom, iv2 += kLcrngMul) {
                    uint32_t word2 = (iv2 >> 16) & 0x7FFF;
                    if (best.found && dist + wordDistance(word2, t + pokemon::kSpe) >= best.distance) continue;
                    record(best, pid, word1, word2, t, source);
                }
            }
        });
}

void searchCuteCharm(const PidIvRequest& req, int effort, PidIvResult& best) {
    uint32_t pid;
    if (!cuteCharmPid(req, &pid)) return;
    searchConsecutiveIvs(req.targetIvs, effort, pid, PidSource::CuteCharm, best);
}

}  // namespace

bool isNatureFrameValid(uint32_t pidLowState, RngMethod method, bool allowSync) {
    if (method == RngMethod::Method1) return true;
    uint32_t pid = pidFromStates(pidLowState, lcrngNext(pidLowState));
    uint32_t nature = pid % 25;
    uint32_t state = lcrngPrev(pidLowState);  // candidate nature call
    for (int reroll = 0; reroll <= kMaxRerolls; ++reroll) {
        if (natureOf(method, state >> 16) == nature) return true;
        if (allowSync && syncPasses(method, state >> 16)) return true;
        // Otherwise `state` must be the high half of a rejected PID pair.
        uint32_t lowState = lcrngPrev(state);
        if (pidFromStates(lowState, state) % 25 == nature) return false;  // the loop would have stopped
        state = lcrngPrev(lowState);
    }
    return false;
}

RngMethod methodForOrigin(uint8_t originGame) {
    switch (originGame) {
        case version::kDiamond:
        case version::kPearl:
        case version::kPlatinum:
            return RngMethod::MethodJ;
        case version::kHeartGold:
        case version::kSoulSilver:
            return RngMethod::MethodK;
        default:
            return RngMethod::Method1;
    }
}

PidIvResult generateConsecutiveIvs(const uint8_t target[pokemon::kStatCount], int effort) {
    PidIvResult best;
    if (effort >= 1) searchConsecutiveIvs(target, 2 * effort, 0, PidSource::Normal, best);
    return best;
}

PidIvResult generatePidIv(const PidIvRequest& req, int effort, uint32_t salt) {
    PidIvResult best;
    if (req.nature >= 25 || !pokemon::edit4::canHaveGender(req.species, req.gender) || effort < 1) {
        return best;
    }
    if (req.shiny) {
        searchByPid(req, effort, salt, best);
    } else {
        searchByIvs(req, effort, best);
    }
    if (req.allowCuteCharm && (!best.found || best.distance > 0)) {
        searchCuteCharm(req, effort, best);
    }
    return best;
}

}  // namespace pktool::legality
