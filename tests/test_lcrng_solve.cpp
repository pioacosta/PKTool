// The lattice solver of core/util/lcrng_solve must find exactly the lower halves a brute-force
// scan of all 65536 finds.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/legality/pid_iv.h"
#include "core/util/lcrng.h"
#include "core/util/lcrng_solve.h"
#include "tests/test.h"

using namespace pktool;

namespace {

uint32_t g_state = 0x12345678u;
uint32_t nextRandom() {
    g_state = g_state * 1664525u + 1013904223u;
    return g_state >> 8;
}

uint32_t advance(util::Lcg lcg, uint32_t state) { return state * lcg.mul + lcg.add; }

std::vector<uint16_t> bruteTop16(util::Lcg lcg, uint32_t upper, uint32_t wanted) {
    std::vector<uint16_t> out;
    for (uint32_t x = 0; x < 0x10000; ++x) {
        if ((advance(lcg, (upper << 16) | x) >> 16) == wanted) out.push_back(static_cast<uint16_t>(x));
    }
    return out;
}

std::vector<uint16_t> bruteWord15(util::Lcg lcg, uint32_t upper, uint32_t word) {
    std::vector<uint16_t> out;
    for (uint32_t x = 0; x < 0x10000; ++x) {
        if (((advance(lcg, (upper << 16) | x) >> 16) & 0x7FFF) == word) out.push_back(static_cast<uint16_t>(x));
    }
    return out;
}

bool same(const uint16_t* got, int count, const std::vector<uint16_t>& want) {
    if (count != static_cast<int>(want.size())) return false;
    for (int i = 0; i < count; ++i) {
        if (got[i] != want[i]) return false;
    }
    return true;
}

}  // namespace

TEST(lcrng_jump_matches_repeated_calls) {
    for (util::Lcg base : {util::kPokemonLcg, util::kGameCubeLcg}) {
        uint32_t s = 0xCAFEF00Du;
        uint32_t viaCalls = s;
        for (int steps = 0; steps < 6; ++steps) {
            CHECK_EQ(advance(util::lcgJump(base, steps), s), viaCalls);
            viaCalls = advance(base, viaCalls);
        }
    }
    CHECK_EQ(util::lcgJump(util::kPokemonLcg, 1).mul, util::kLcrngMul);
    CHECK_EQ(util::lcgJump(util::kPokemonLcg, 1).add, 0x6073u);
}

TEST(lcrng_solve_finds_exactly_what_a_scan_finds) {
    int totalFound = 0;
    int most = 0;
    for (util::Lcg base : {util::kPokemonLcg, util::kGameCubeLcg}) {
        for (int steps = 1; steps <= 3; ++steps) {
            const util::Lcg lcg = util::lcgJump(base, steps);
            for (int q = 0; q < 70; ++q) {
                // Wanted values taken from real states half of the time (so answers exist).
                uint32_t upper = q == 0 ? 0 : q == 1 ? 0xFFFF : nextRandom() & 0xFFFF;
                uint32_t state = (upper << 16) | (nextRandom() & 0xFFFF);
                uint32_t wanted = q % 2 == 0 ? advance(lcg, state) >> 16 : nextRandom() & 0xFFFF;
                std::vector<uint16_t> want = bruteTop16(lcg, upper, wanted);
                uint16_t got[64];
                int count = util::lcgSolveTop16(lcg, upper, wanted, got, 64);
                CHECK(same(got, count, want));
                totalFound += count;
                if (count > most) most = count;

                uint32_t word = q % 2 == 0 ? (advance(lcg, state) >> 16) & 0x7FFF : nextRandom() & 0x7FFF;
                std::vector<uint16_t> want15 = bruteWord15(lcg, upper, word);
                count = util::lcgSolveWord15(lcg, upper, word, got, 64);
                CHECK(same(got, count, want15));
                totalFound += count;
                if (count > most) most = count;
            }
        }
    }
    std::printf("  %d solutions in total, at most %d for one query\n", totalFound, most);
    CHECK(totalFound > 400);
}

TEST(lcrng_solve_respects_the_output_limit) {
    uint16_t got[2];
    // Word queries have ~2 solutions on average: find one with more than one and cut it.
    for (uint32_t word = 0; word < 0x7FFF; ++word) {
        std::vector<uint16_t> all = bruteWord15(util::kPokemonLcg, 0x1234, word);
        if (all.size() < 2) continue;
        CHECK_EQ(util::lcgSolveWord15(util::kPokemonLcg, 0x1234, word, got, 1), 1);
        CHECK_EQ(got[0], all[0]);
        CHECK_EQ(util::lcgSolveWord15(util::kPokemonLcg, 0x1234, word, got, 2), 2);
        CHECK_EQ(got[1], all[1]);
        break;
    }
}

namespace {

// The scan findPidIvMethod() used before the solver: every lower half, one call at a time.
legality::PidIvMethod bruteMethod(uint32_t pid, const uint8_t ivs[pokemon::kStatCount]) {
    const uint32_t low = pid & 0xFFFF;
    const uint32_t high = pid >> 16;
    const uint16_t word1 = static_cast<uint16_t>(ivs[0] | (ivs[1] << 5) | (ivs[2] << 10));
    const uint16_t word2 = static_cast<uint16_t>(ivs[3] | (ivs[4] << 5) | (ivs[5] << 10));
    for (uint32_t bottom = 0; bottom < 0x10000; ++bottom) {
        uint32_t s1 = (low << 16) | bottom;
        uint32_t s2 = util::lcrngNext(s1);
        if ((s2 >> 16) != high) continue;
        uint32_t s3 = util::lcrngNext(s2);
        uint32_t s4 = util::lcrngNext(s3);
        uint32_t s5 = util::lcrngNext(s4);
        uint16_t r3 = static_cast<uint16_t>((s3 >> 16) & 0x7FFF);
        uint16_t r4 = static_cast<uint16_t>((s4 >> 16) & 0x7FFF);
        uint16_t r5 = static_cast<uint16_t>((s5 >> 16) & 0x7FFF);
        if (r3 == word1 && r4 == word2) return legality::PidIvMethod::Method1;
        if (r4 == word1 && r5 == word2) return legality::PidIvMethod::Method2;
        if (r3 == word1 && r5 == word2) return legality::PidIvMethod::Method4;
    }
    if (legality::isCuteCharmPid(pid) && legality::ivsFromConsecutiveCalls(ivs)) return legality::PidIvMethod::CuteCharm;
    return legality::PidIvMethod::None;
}

}  // namespace

TEST(pid_iv_method_detection_agrees_with_the_scan) {
    int hits[6] = {};
    for (int n = 0; n < 160; ++n) {
        uint32_t seed = nextRandom() << 8 ^ nextRandom();
        // A frame made by the generator (Method 1, 2 or 4), or random values.
        uint32_t s1 = util::lcrngNext(seed);
        uint32_t s2 = util::lcrngNext(s1);
        uint32_t s3 = util::lcrngNext(s2);
        uint32_t s4 = util::lcrngNext(s3);
        uint32_t s5 = util::lcrngNext(s4);
        uint32_t pid = (s2 & 0xFFFF0000u) | (s1 >> 16);
        uint32_t w1 = (s3 >> 16) & 0x7FFF;
        uint32_t w2 = (s4 >> 16) & 0x7FFF;
        switch (n % 4) {
            case 1: w1 = (s4 >> 16) & 0x7FFF; w2 = (s5 >> 16) & 0x7FFF; break;  // Method 2
            case 2: w2 = (s5 >> 16) & 0x7FFF; break;                              // Method 4
            case 3: pid ^= nextRandom() & 0xFF; break;                            // probably none
            default: break;
        }
        if (n % 40 == 39) pid = nextRandom() & 0xFF;  // Cute Charm range
        uint8_t ivs[6] = {static_cast<uint8_t>(w1 & 31), static_cast<uint8_t>((w1 >> 5) & 31), static_cast<uint8_t>((w1 >> 10) & 31),
                          static_cast<uint8_t>(w2 & 31), static_cast<uint8_t>((w2 >> 5) & 31), static_cast<uint8_t>((w2 >> 10) & 31)};
        legality::PidIvMethod want = bruteMethod(pid, ivs);
        CHECK(legality::findPidIvMethod(pid, ivs) == want);
        ++hits[static_cast<int>(want)];
    }
    std::printf("  none %d, method 1 %d, method 2 %d, method 4 %d, cute charm %d\n", hits[0], hits[1], hits[2], hits[3], hits[5]);
    CHECK(hits[1] > 20 && hits[2] > 20 && hits[3] > 20);
}
