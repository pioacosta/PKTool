// Mass test: random Pokemon of Gen III, IV and V go through the creator, the
// editor operations, LEGALIZE, the checker and the entity codecs.
//
//   PKTOOL_MASS=100000 ./pktool_tests mass          (Release build)
//   PKTOOL_MASS_SEED=S  PKTOOL_MASS=1 ./pktool_tests mass   (replays one failure)
//
// Without PKTOOL_MASS a small run (150) keeps CI fast. Item i uses the
// generator seed (PKTOOL_MASS_SEED + i), printed with every failure.
//
// Asserted for every Pokemon:
//   1. the codec round trip is byte-identical (encode -> decode -> apply);
//   2. LEGALIZE's report is the checker's verdict on the returned Pokemon;
//   3. nothing unresolved means a legal result (an explicit list otherwise);
//   4. the result keeps its verdict through the codec and a box slot of a save;
//   5. legalizing a legal result again keeps it legal.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "core/data/names.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/pokemon/entity.h"
#include "core/saves/save_file.h"
#include "tests/fake_saves.h"
#include "tests/fuzz/fuzz_targets.h"
#include "tests/test.h"

using namespace pktool;

namespace {

struct MassStats {
    long total = 0;
    long legalResult = 0;
    long unresolvedResult = 0;
    long failures = 0;
    long changedNature = 0;
    long changedShiny = 0;
    long changedMoves = 0;
    std::map<std::string, long> unresolvedReasons;
    long perGeneration[6] = {};
    long cleanTotal = 0;
    long unstorableOt = 0;
    long cleanLegal = 0;
    std::map<std::string, long> cleanReasons;
};

void mismatch(MassStats& stats, uint64_t seed, int generation, const char* what, const pokemon::Pokemon& p) {
    ++stats.failures;
    std::printf("  MASS FAILURE (gen %d, replay: PKTOOL_MASS_SEED=%llu PKTOOL_MASS=1): %s\n    species %u level %u nature %u shiny %d egg %d ball %u\n",
                generation, static_cast<unsigned long long>(seed), what, p.species, p.level, p.nature, p.shiny,
                p.isEgg, p.met.ball);
    CHECK(false);
}

// Groups "Blaziken only evolves at level 36" style messages under one key.
std::string reasonKey(const std::string& text) {
    size_t at = text.find(" only evolves at level");
    if (at != std::string::npos) return "<species> only evolves at level N";
    at = text.find(" is only found from Lv.");
    if (at != std::string::npos) return "<species> is only found from Lv. N";
    return text.substr(0, 70);
}

size_t entitySize(const pokemon::Pokemon& p) { return p.format == 3 ? 80 : 136; }

bool codecRoundTrip(const pokemon::Pokemon& p) {
    uint8_t raw[136];
    fuzz::encodeStored(p, raw);
    pokemon::Pokemon q = pokemon::entity::decode(p.format, raw, entitySize(p));
    uint8_t raw2[136];
    std::memcpy(raw2, raw, sizeof raw);
    pokemon::entity::apply(q, raw2);
    return std::memcmp(raw, raw2, entitySize(p)) == 0;
}

pokemon::Pokemon throughCodec(const pokemon::Pokemon& p) {
    uint8_t raw[136];
    fuzz::encodeStored(p, raw);
    return pokemon::entity::decode(p.format, raw, entitySize(p));
}

void runOne(MassStats& stats, uint64_t seed, std::unique_ptr<saves::SaveFile> (&saves)[3], int effort) {
    fuzz::Rng rng(seed);
    int generation = 3 + static_cast<int>(rng.below(3));
    const bool clean = rng.chance(30);
    pokemon::Pokemon p = fuzz::randomPokemon(rng, generation, clean);
    ++stats.total;
    stats.cleanTotal += clean ? 1 : 0;
    ++stats.perGeneration[generation];

    if (!codecRoundTrip(p)) return mismatch(stats, seed, generation, "codec round trip of the requested Pokemon", p);

    legality::LegalizeResult r = legality::legalize(p, effort);
    const pokemon::Pokemon& out = r.pokemon;
    if (out.format != p.format) return mismatch(stats, seed, generation, "LEGALIZE changed the format", p);
    if (!codecRoundTrip(out)) return mismatch(stats, seed, generation, "codec round trip of the LEGALIZE result", out);
    if (r.report.overall() != legality::check(out).overall())
        return mismatch(stats, seed, generation, "LEGALIZE's report differs from the checker", p);

    if (r.report.legal()) {
        ++stats.legalResult;
        stats.cleanLegal += clean ? 1 : 0;
    } else {
        ++stats.unresolvedResult;
        if (r.unresolved.empty()) return mismatch(stats, seed, generation, "not legal and nothing reported as unresolved", p);
        ++stats.unresolvedReasons[reasonKey(r.unresolved[0])];
        if (clean) ++stats.cleanReasons[reasonKey(r.unresolved[0])];
    }

    // The verdict survives the entity codec and a slot in a save.
    pokemon::Pokemon back = throughCodec(out);
    // (An unresolved origin game that a format cannot store, e.g. 20 in PK3, is
    // truncated by the codec: only legal results must keep their verdict.)
    // A Gen III egg is Japanese: a Latin OT name must survive in the entity (the
    // Japanese table's full-width letters have the same byte values).
    const bool unstorableOt = back.trainer.name.empty() && !out.trainer.name.empty();
    stats.unstorableOt += unstorableOt ? 1 : 0;
    if (r.report.legal() && !unstorableOt && legality::check(back).overall() != r.report.overall()) {
        auto dump = [](const char* label, const legality::Report& rep) {
            for (const auto& f : rep.findings) {
                if (f.severity != legality::Severity::Valid) std::printf("    %s [%d] %s\n", label, static_cast<int>(f.severity), f.text.c_str());
            }
        };
        std::printf("    result: %s lang %u OT \"%s\" origin %u egg %d | requested lang %u OT \"%s\"\n", data::speciesName(out.species),
                    out.language, out.trainer.name.c_str(), out.originGame, out.isEgg, p.language, p.trainer.name.c_str());
        dump("before", r.report);
        dump("after ", legality::check(back));
    }
    if (r.report.legal() && !unstorableOt && legality::check(back).overall() != r.report.overall())
        return mismatch(stats, seed, generation, "verdict changes after encode/decode", out);
    saves::SaveFile& save = *saves[generation - 3];
    uint8_t raw[136];
    fuzz::encodeStored(out, raw);
    int box = static_cast<int>(seed % static_cast<uint64_t>(save.boxCount()));
    int slot = static_cast<int>((seed / 7) % 30);
    uint8_t read[136] = {};
    if (!save.writeBoxSlot(box, slot, raw) || save.readBoxSlot(box, slot, read) != saves::SlotState::Valid ||
        std::memcmp(raw, read, entitySize(out)) != 0)
        return mismatch(stats, seed, generation, "box slot write/read is not a round trip", out);

    if (!p.isEgg && r.report.legal()) {
        if (out.nature != p.nature) ++stats.changedNature;
        if (out.shiny != p.shiny) ++stats.changedShiny;
        bool sameMoves = true;
        for (int m = 0; m < 4; ++m) sameMoves = sameMoves && out.moves[m] == p.moves[m];
        if (!sameMoves && ++stats.changedMoves <= 3) {
            std::printf("  [info] moves changed (seed %llu, gen %d, %s):", static_cast<unsigned long long>(seed), generation, data::speciesName(p.species));
            for (const auto& c : r.changes) std::printf(" {%s}", c.c_str());
            std::printf("\n");
        }
    }

    if (r.report.legal()) {
        legality::LegalizeResult again = legality::legalize(out, effort);
        if (!again.report.legal()) {
            for (const auto& c : r.changes) std::printf("    first change: %s\n", c.c_str());
            for (const auto& c : again.changes) std::printf("    second change: %s\n", c.c_str());
            for (const auto& u : again.unresolved) std::printf("    second unresolved: %s\n", u.c_str());
            for (const auto& f : again.report.findings) {
                if (f.severity == legality::Severity::Invalid) std::printf("    second invalid: %s\n", f.text.c_str());
            }
            std::printf("    species %u lvl %u origin %u loc %u metLvl %u ball %u egg %d lang %u\n", out.species, out.level, out.originGame, out.met.metLocation, out.met.metLevel, out.met.ball, out.isEgg, out.language);
        }
        if (!again.report.legal()) return mismatch(stats, seed, generation, "legalizing a legal result makes it illegal", out);
    }
}

}  // namespace

TEST(mass_generate_legalize_round_trip) {
    const char* env = std::getenv("PKTOOL_MASS");
    long n = env && *env ? std::atol(env) : 150;
    const char* seedEnv = std::getenv("PKTOOL_MASS_SEED");
    uint64_t base = seedEnv && *seedEnv ? std::strtoull(seedEnv, nullptr, 10) : 1;
    const bool big = n > 1000;

    std::unique_ptr<saves::SaveFile> saves[3] = {saves::loadSaveFile(fake::blankGen3(saves::Game::Emerald)),
                                                 saves::loadSaveFile(fake::blankGen4(fake::kPt)),
                                                 saves::loadSaveFile(fake::blankGen5(true))};
    CHECK(saves[0] && saves[1] && saves[2]);
    if (!saves[0] || !saves[1] || !saves[2]) return;

    MassStats stats;
    auto start = std::chrono::steady_clock::now();
    for (long i = 0; i < n; ++i) {
        runOne(stats, base + static_cast<uint64_t>(i), saves, 4);
        if (big && (i + 1) % 10000 == 0) {
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::printf("  ... %ld/%ld (%.1fs, %ld failures)\n", i + 1, n, s, stats.failures);
            std::fflush(stdout);
        }
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  mass: %ld Pokemon (Gen III %ld, IV %ld, V %ld) in %.2fs (%.2f ms each): %ld legal, %ld with unresolved items, %ld failures\n",
                stats.total, stats.perGeneration[3], stats.perGeneration[4], stats.perGeneration[5], seconds,
                1000.0 * seconds / static_cast<double>(stats.total ? stats.total : 1), stats.legalResult,
                stats.unresolvedResult, stats.failures);
    std::printf("  legal results that changed a request: nature %ld, shininess %ld, moves %ld\n", stats.changedNature,
                stats.changedShiny, stats.changedMoves);
    std::printf("  OT names lost in the entity: %ld\n", stats.unstorableOt);
    CHECK_EQ(stats.unstorableOt, 0L);
    std::printf("  plausible requests (creator defaults, no random moves): %ld, legal %ld, with unresolved items %ld\n",
                stats.cleanTotal, stats.cleanLegal, stats.cleanTotal - stats.cleanLegal);
    if (big) {
        for (const auto& kv : stats.cleanReasons) {
            if (kv.second >= 3) std::printf("    plausible, unresolved: %6ld  %s\n", kv.second, kv.first.c_str());
        }
        for (const auto& kv : stats.unresolvedReasons) {
            if (kv.second >= std::max<long>(5, stats.total / 500)) std::printf("    %6ld  %s\n", kv.second, kv.first.c_str());
        }
    }
    CHECK_EQ(stats.failures, 0);
}
