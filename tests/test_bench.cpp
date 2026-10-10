// Timing benchmarks. They only run with PKTOOL_BENCH=1 and print CPU times; nothing
// is asserted except that the flows still work, so they are safe to leave in the suite.
//
//   PKTOOL_BENCH=1 [PKTOOL_FIXTURES=dir] ./build/pktool_tests bench
//
// The DS has a 67 MHz ARM9 (no FPU, 4 MiB RAM): multiply the host times by ~30-50 for an
// estimate of the console.

#include <time.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/data/events3.h"
#include "core/data/names.h"
#include "core/io/file_io.h"
#include "core/legality/legality.h"
#include "core/legality/legalize.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/cxd3.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/pk3.h"
#include "core/data/locks3.h"
#include "core/legality/rng3.h"
#include "core/saves/save_file.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/test.h"

size_t pktoolHeapAllocations() __attribute__((weak));  // tests/test_memory.cpp

using namespace pktool;

namespace {

bool benchEnabled() {
    const char* v = std::getenv("PKTOOL_BENCH");
    return v != nullptr && *v != '\0' && *v != '0';
}

double cpuMs() {
    timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

struct Samples {
    std::vector<double> ms;
    std::vector<std::string> what;
    void add(double v, const std::string& label) {
        ms.push_back(v);
        what.push_back(label);
    }
    void print(const char* title) const {
        if (ms.empty()) return;
        std::vector<size_t> order(ms.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ms[a] > ms[b]; });
        double total = 0;
        for (double v : ms) total += v;
        std::printf("  %-34s %6zu calls  total %8.1f ms  mean %7.3f ms  p95 %7.3f ms  max %8.3f ms (%s)\n", title,
                    ms.size(), total, total / ms.size(), ms[order[order.size() / 20]], ms[order[0]],
                    what[order[0]].c_str());
        for (size_t i = 1; i < 3 && i < order.size(); ++i) {
            std::printf("  %-34s %70s %8.3f ms (%s)\n", "", "next", ms[order[i]], what[order[i]].c_str());
        }
    }
};

// A hash of results, printed with the timings: an optimisation must not change it.
struct Hash {
    uint64_t h = 1469598103934665603ull;
    void add(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFF;
            h *= 1099511628211ull;
        }
    }
    void add(const std::string& s) {
        for (unsigned char c : s) add(c);
        add(0x100);
    }
};

void hashPokemon(Hash& h, const pokemon::Pokemon& p) {
    h.add(p.species);
    h.add(p.pid);
    h.add(p.level);
    h.add(p.nature);
    h.add(p.ability);
    h.add(p.heldItem);
    h.add(p.originGame);
    h.add(p.met.metLocation);
    h.add(p.met.eggLocation);
    h.add(p.met.metLevel);
    h.add(p.met.ball);
    h.add(p.trainer.tid);
    h.add(p.trainer.sid);
    h.add(p.isEgg);
    h.add(p.shiny);
    for (int i = 0; i < 4; ++i) h.add(p.moves[i]);
    for (int i = 0; i < 6; ++i) h.add(p.ivs[i]);
    for (int i = 0; i < 6; ++i) h.add(p.evs[i]);
    h.add(p.nickname);
}

void hashReport(Hash& h, const legality::Report& r) {
    for (const legality::Finding& f : r.findings) {
        h.add(static_cast<uint64_t>(f.severity));
        h.add(f.text);
    }
    h.add(0xFEED);
}

pokemon::create4::Trainer trainerFor(uint8_t version) {
    pokemon::create4::Trainer t;
    t.name = "ASH";
    t.tid = 12345;
    t.sid = 54321;
    t.version = version;
    t.language = 2;
    t.today = {26, 9, 29};
    return t;
}

// Requests of increasing difficulty for one species.
pokemon::Pokemon request(const pokemon::create4::Trainer& trainer, uint16_t species, int variant) {
    pokemon::Pokemon p = pokemon::create4::newPokemon(trainer, species, variant == 1 ? 100 : 50, species * 977u + 13);
    if (variant >= 1) {
        for (auto& iv : p.ivs) iv = 31;
    }
    if (variant == 2) {
        // Shiny with perfect IVs: the hardest PID/IV frame to find.
        p.pid = (static_cast<uint32_t>(trainer.tid ^ trainer.sid) << 16) | 0x0000;
        p.shiny = true;
    }
    return p;
}

void legalizeRun(const char* title, uint8_t version, uint16_t maxSpecies, int effort) {
    pokemon::create4::Trainer trainer = trainerFor(version);
    Samples samples;
    Hash hash;
    int illegal = 0;
    for (uint16_t species = 1; species <= maxSpecies; ++species) {
        for (int variant = 0; variant < 3; ++variant) {
            pokemon::Pokemon p = request(trainer, species, variant);
            double t0 = cpuMs();
            legality::LegalizeResult r = legality::legalize(p, effort);
            double dt = cpuMs() - t0;
            if (!r.report.legal()) ++illegal;
            hashPokemon(hash, r.pokemon);
            hashReport(hash, r.report);
            for (const std::string& c : r.changes) hash.add(c);
            for (const std::string& c : r.unresolved) hash.add(c);
            samples.add(dt, std::string(data::speciesName(species)) + " v" + std::to_string(variant));
        }
    }
    samples.print(title);
    std::printf("  results hash %016llx, %d requests could not be fully legalized\n",
                static_cast<unsigned long long>(hash.h), illegal);
}

void checkRun(const char* title, uint8_t version, uint16_t maxSpecies) {
    pokemon::create4::Trainer trainer = trainerFor(version);
    std::vector<pokemon::Pokemon> mons;
    for (uint16_t species = 1; species <= maxSpecies; ++species) mons.push_back(request(trainer, species, species % 3));
    Samples samples;
    Hash hash;
    for (int round = 0; round < 3; ++round) {
        for (const pokemon::Pokemon& p : mons) {
            double t0 = cpuMs();
            legality::Report r = legality::check(p);
            double dt = cpuMs() - t0;
            if (round == 0) hashReport(hash, r);
            samples.add(dt, data::speciesName(p.species));
        }
    }
    samples.print(title);
    std::printf("  reports hash %016llx\n", static_cast<unsigned long long>(hash.h));
}

}  // namespace

TEST(bench_legalize) {
    if (!benchEnabled()) return;
    std::printf("LEGALIZE (effort 4 as in the UI; plain, level 100 + 31 IVs, shiny + 31 IVs)\n");
    legalizeRun("Gen III FireRed, 386 species", 4, 386, 4);
    legalizeRun("Gen IV Platinum, 493 species", 12, 493, 4);
    legalizeRun("Gen IV HeartGold, 493 species", 7, 493, 4);
    legalizeRun("Gen V Black 2, 649 species", 23, 649, 4);

    std::printf("Colosseum/XD generation with wanted traits\n");
    {
        pokemon::create4::Trainer trainer = trainerFor(4);
        Samples samples;
        Hash hash;
        for (int index : pokemon::cxd3::menuEntries()) {
            for (int nature : {-1, 3, 17}) {
                pokemon::gift3::Criteria want;
                want.nature = static_cast<int8_t>(nature);
                want.shiny = nature == 17 ? 1 : -1;
                uint8_t raw[80];
                double t0 = cpuMs();
                pokemon::Pokemon p = pokemon::cxd3::create(index, trainer, 7u + index, want, raw);
                double dt = cpuMs() - t0;
                hashPokemon(hash, p);
                samples.add(dt, std::string(data::speciesName(p.species)) + " n" + std::to_string(nature));
            }
        }
        samples.print("cxd3::create (all entries)");
        std::printf("  results hash %016llx\n", static_cast<unsigned long long>(hash.h));
    }
}

namespace {

// `p` with the plain XD frame of `origin`: a frame no team lock may allow (see test_cxd_locks.cpp).
pokemon::Pokemon withXdFrame(pokemon::Pokemon p, uint32_t origin) {
    namespace rng3 = legality::rng3;
    uint32_t s = rng3::xdNext(origin);
    uint32_t iv1 = (s >> 16) & 0x7FFF;
    s = rng3::xdNext(s);
    uint32_t iv2 = (s >> 16) & 0x7FFF;
    s = rng3::xdNext(rng3::xdNext(s));
    uint32_t hi = s >> 16;
    s = rng3::xdNext(s);
    p.pid = (hi << 16) | (s >> 16);
    uint32_t iv32 = (iv2 << 15) | iv1;
    for (int k = 0; k < 6; ++k) p.ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
    uint8_t raw[80];
    pokemon::create4::encode(p, raw);
    return pokemon::pk3::toPokemon(raw, pokemon::pk3::kStoredSize);
}

}  // namespace

// The checker's Shadow Pokemon team-lock search (bounded to 400k steps) on genuine and random frames.
TEST(bench_shadow_locks) {
    if (!benchEnabled()) return;
    pokemon::create4::Trainer trainer = trainerFor(4);
    trainer.tid = 0x4D2F;
    trainer.sid = 0x8123;
    Samples genuine;
    Samples random;
    Hash hash;
    for (int index : pokemon::cxd3::menuEntries()) {
        const data::Gc3& e = data::gc3(index);
        if (e.kind != data::kGcXdShadow && e.kind != data::kGcColoShadow) continue;
        uint8_t raw[80];
        pokemon::Pokemon p = pokemon::cxd3::create(index, trainer, 7, pokemon::gift3::Criteria(), raw);
        if (p.species == 0) continue;
        double t0 = cpuMs();
        legality::Report good = legality::checkGen3(p);
        genuine.add(cpuMs() - t0, data::speciesName(p.species));
        hashReport(hash, good);
        for (uint32_t o = 1; o <= 40; ++o) {
            pokemon::Pokemon q = withXdFrame(p, o * 2654435761u);
            t0 = cpuMs();
            legality::Report r = legality::checkGen3(q);
            random.add(cpuMs() - t0, std::string(data::speciesName(p.species)) + " #" + std::to_string(o));
            hashReport(hash, r);
        }
    }
    genuine.print("check of generated shadows");
    random.print("check of random shadow frames");
    std::printf("  reports hash %016llx\n", static_cast<unsigned long long>(hash.h));
}

TEST(bench_checker) {
    if (!benchEnabled()) return;
    std::printf("Legality checker (one Pokemon)\n");
    checkRun("Gen III", 4, 386);
    checkRun("Gen IV", 12, 493);
    checkRun("Gen V", 23, 649);
}

namespace {

struct ScreenStats {
    int draws = 0;
    double drawMs = 0;
    double drawMax = 0;
    size_t allocs = 0;
    uint32_t opens = 0;  // files opened while drawing (sprites)
    int updates = 0;
    double updateMs = 0;
    double updateMax = 0;
};

uint16_t g_top[gfx::kScreenWidth * gfx::kScreenHeight];
uint16_t g_bottom[gfx::kScreenWidth * gfx::kScreenHeight];

}  // namespace

// Random walk through the UI of a real (or fake) save, timing every draw and update per screen.
TEST(bench_render_and_update) {
    if (!benchEnabled()) return;
    const char* fixtures = std::getenv("PKTOOL_FIXTURES");
    std::vector<std::string> saves;
    if (fixtures != nullptr && *fixtures != '\0') saves = io::findSaveFiles(fixtures, 2);
    if (saves.empty()) {
        std::printf("render/update bench skipped: set PKTOOL_FIXTURES\n");
        return;
    }
    char pattern[] = "/tmp/pktool-bench-XXXXXX";
    std::string dir = mkdtemp(pattern);
    const char* assetsEnv = std::getenv("PKTOOL_ASSETS");  // (a copy on a fast disk gives steadier timings)
    const std::string assets = assetsEnv && *assetsEnv ? std::string(assetsEnv) : std::string(PKTOOL_SOURCE_DIR) + "/assets/nitrofs";
    const uint32_t keys[] = {ui::kKeyA,    ui::kKeyA,    ui::kKeyA,    ui::kKeyB,    ui::kKeyDown, ui::kKeyDown,
                             ui::kKeyDown, ui::kKeyUp,   ui::kKeyRight, ui::kKeyLeft, ui::kKeyR,    ui::kKeyL,
                             ui::kKeyX,    ui::kKeyY,    ui::kKeyStart, ui::kKeyA,    ui::kKeyDown, ui::kKeyRight};
    for (const std::string& source : saves) {
        std::vector<uint8_t> bytes;
        if (!io::readFile(source, bytes)) continue;
        std::string path = io::joinPath(dir, io::fileName(source));
        if (io::safeWriteFile(path, bytes) != io::WriteResult::Ok) continue;
        ui::Config config;
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        config.spriteDirs = {assets + "/sprites"};
        config.assetDirs = {assets};
        ui::App app(config);
        if (!app.openSave(path)) continue;
        std::map<std::string, ScreenStats> stats;
        uint32_t rng = 12345;
        const int steps = 2500;
        for (int step = 0; step < steps; ++step) {
            rng = rng * 1664525u + 1013904223u;
            ui::Input in;
            in.pressed = in.repeated = keys[(rng >> 16) % (sizeof keys / sizeof keys[0])];
            // Keep the walk away from closing the save and from writing it again and again.
            if (std::strcmp(app.screenName(), "SaveMenu") == 0 && (rng & 0x300) == 0) in.pressed = in.repeated = ui::kKeyDown;
            ScreenStats& s = stats[app.screenName()];
            double t0 = cpuMs();
            app.update(in);
            app.update(ui::Input());
            double t1 = cpuMs();
            s.updates += 1;
            s.updateMs += t1 - t0;
            s.updateMax = std::max(s.updateMax, t1 - t0);
            ScreenStats& after = stats[app.screenName()];
            if (!app.needsRedraw() && (rng & 7) != 0) continue;
            gfx::Canvas top(g_top);
            gfx::Canvas bottom(g_bottom);
            size_t a0 = pktoolHeapAllocations ? pktoolHeapAllocations() : 0;
            const uint32_t opens0 = io::readStats().opens;
            double d0 = cpuMs();
            app.draw(top, bottom);
            double d1 = cpuMs();
            after.draws += 1;
            after.opens += io::readStats().opens - opens0;
            after.drawMs += d1 - d0;
            after.drawMax = std::max(after.drawMax, d1 - d0);
            after.allocs += (pktoolHeapAllocations ? pktoolHeapAllocations() : 0) - a0;
        }
        std::printf("UI random walk, %s (%d steps)\n", io::fileName(source).c_str(), steps);
        for (const auto& kv : stats) {
            const ScreenStats& s = kv.second;
            if (s.draws == 0 && s.updates == 0) continue;
            std::printf("  %-10s draws %4d mean %6.3f ms max %6.3f ms %6.1f allocs %5.2f opens/draw   updates %4d mean %7.3f ms max %8.3f ms\n",
                        kv.first.c_str(), s.draws, s.draws ? s.drawMs / s.draws : 0.0, s.drawMax,
                        s.draws ? static_cast<double>(s.allocs) / s.draws : 0.0,
                        s.draws ? static_cast<double>(s.opens) / s.draws : 0.0, s.updates,
                        s.updates ? s.updateMs / s.updates : 0.0, s.updateMax);
        }
    }
}
