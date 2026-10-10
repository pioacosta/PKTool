#pragma once

// Fuzz targets shared by three front-ends:
//   - the always-on smoke test in the normal test-suite (tests/test_fuzz_smoke.cpp),
//   - a self-contained mutation fuzzer (tests/fuzz/fuzz_driver.cpp), which needs
//     nothing but the C++ compiler,
//   - a libFuzzer entry point (tests/fuzz/fuzz_libfuzzer.cpp, clang, -DPKTOOL_FUZZ=ON).
//
// A target takes arbitrary bytes and must never crash, hang or trip a
// sanitizer. When it can also state an invariant (a round trip, a report that
// agrees with the checker) it calls fuzz::fail(), which prints and aborts.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/pokemon/create4.h"
#include "core/pokemon/pokemon.h"

namespace pktool::fuzz {

using TargetFn = void (*)(const uint8_t* data, size_t size);

struct Target {
    const char* name;
    TargetFn fn;
};

// All targets: save_load, save_probe, entity, text, showdown, bank, settings, legality.
const std::vector<Target>& targets();
const Target* findTarget(const std::string& name);

// Synthetic seed inputs for a target (blank saves with generated Pokemon,
// Showdown sets, a valid Bank file...). Never derived from real saves.
std::vector<std::vector<uint8_t>> seeds(const std::string& target);

// Prints "FUZZ INVARIANT FAILED" with the message and aborts.
[[noreturn]] void fail(const char* target, const char* message);

// Small deterministic generator (xorshift64*) so runs are reproducible.
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { next(); }
    uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1Dull;
    }
    uint32_t u32() { return static_cast<uint32_t>(next() >> 32); }
    // Uniform-ish value in [0, n).
    uint32_t below(uint32_t n) { return n ? u32() % n : 0; }
    bool chance(uint32_t percent) { return below(100) < percent; }
};

// ----- Random but structured Pokemon (used by the mass test and the legality target)

// A trainer of a random game of `generation` (3, 4 or 5).
pokemon::create4::Trainer randomTrainer(Rng& rng, int generation);

// A Pokemon (or egg) of `generation` made through the creator and then given
// random requests: nature, shininess, gender, ability, IVs, EVs, moves, item,
// ball and sometimes outright impossible values, the way the editor or a
// Showdown set could.
// With `clean`, only plausible requests are made (level at or above the lowest
// legal one, nature, shininess, gender, ability), no random moves, item, ball
// or broken values.
pokemon::Pokemon randomPokemon(Rng& rng, int generation, bool clean = false);

// Encodes a model into a fresh decrypted stored entity of p.format.
// Returns the entity size (80 for PK3, else 136).
size_t encodeStored(const pokemon::Pokemon& p, uint8_t out[136]);

// One random mutation round on `base` (1-4 edits: bit flips, byte and integer
// overwrites, block copies, inserts, deletes, a splice from `other`). The
// result is at most `maxLen` bytes.
std::vector<uint8_t> mutate(Rng& rng, const std::vector<uint8_t>& base, const std::vector<uint8_t>* other,
                            size_t maxLen);

// Fixes the checksums that make a mutated buffer load as the save it was
// made from (Gen III sectors, Gen IV block CRCs, Gen V checksum table).
void repairSave(std::vector<uint8_t>& bytes);

}  // namespace pktool::fuzz
