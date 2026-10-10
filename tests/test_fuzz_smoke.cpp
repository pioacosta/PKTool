// Deterministic smoke run of the fuzz targets inside the normal test-suite:
// every target gets its synthetic seeds, mutations of them and purely random
// bytes. PKTOOL_FUZZ_SMOKE=N sets the number of inputs per target (default 40;
// the heavy save targets use a quarter). Crashes and failed invariants abort.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "tests/fuzz/fuzz_targets.h"
#include "tests/test.h"

using namespace pktool;

namespace {

int smokeCount() {
    const char* env = std::getenv("PKTOOL_FUZZ_SMOKE");
    return env && *env ? std::atoi(env) : 40;
}

void smoke(const char* name, size_t maxLen) {
    const fuzz::Target* t = fuzz::findTarget(name);
    CHECK(t != nullptr);
    if (!t) return;
    std::vector<std::vector<uint8_t>> seeds = fuzz::seeds(name);
    CHECK(!seeds.empty());
    if (seeds.empty()) return;
    for (const auto& s : seeds) t->fn(s.data(), s.size());
    int count = smokeCount();
    if (maxLen > 100000) count = count / 4 + 1;  // heavy inputs
    fuzz::Rng rng(0xC0FFEE ^ std::strlen(name));
    for (int i = 0; i < count; ++i) {
        const auto& base = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
        std::vector<uint8_t> input;
        if (i % 5 == 4) {
            input.resize(rng.below(static_cast<uint32_t>(std::min<size_t>(maxLen, 300))));
            for (auto& b : input) b = static_cast<uint8_t>(rng.u32());
        } else {
            input = fuzz::mutate(rng, base, &seeds[rng.below(static_cast<uint32_t>(seeds.size()))], maxLen);
        }
        t->fn(input.data(), input.size());
    }
}

}  // namespace

TEST(fuzz_smoke_save_load) { smoke("save_load", 0x80200); }
TEST(fuzz_smoke_save_probe) { smoke("save_probe", 0x80200); }
TEST(fuzz_smoke_entity) { smoke("entity", 400); }
TEST(fuzz_smoke_text) { smoke("text", 120); }
TEST(fuzz_smoke_showdown) { smoke("showdown", 2000); }
TEST(fuzz_smoke_bank) { smoke("bank", 90000); }
TEST(fuzz_smoke_settings) { smoke("settings", 200); }
TEST(fuzz_smoke_legality) { smoke("legality", 200); }
TEST(fuzz_smoke_checks) { smoke("checks", 200); }
TEST(fuzz_smoke_card_file) { smoke("card_file", 1000); }
TEST(fuzz_smoke_language_pack) { smoke("language_pack", 200000); }
TEST(fuzz_smoke_cjk_font) { smoke("cjk_font", 200000); }
