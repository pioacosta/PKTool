// Heap use of the flows that matter on the DS, where the heap is ~2.5 MB and
// running out of it halfway through a write corrupted a save. A counting
// operator new tracks the peak above the starting point of each flow; the
// limits below leave room for a 512 KB save, one working copy and the UI,
// not for several copies of the save.
//
// The counters need glibc's malloc_usable_size and are off under ASan (which
// has its own operator new): the tests then only run the flows.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
#include <malloc.h>
#define PKTOOL_COUNT_HEAP 1
#endif

#include "core/bank/bank.h"
#include "core/data/language.h"
#include "core/io/file_io.h"
#include "core/legality/legality.h"
#include "core/legality/legalize.h"
#include "core/pokemon/create4.h"
#include "core/saves/save_commit.h"
#include "core/saves/save_probe.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "ds/gfx/cjk_font.h"
#include "tests/fixtures.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

#ifdef PKTOOL_COUNT_HEAP
namespace {
size_t g_live = 0;
size_t g_peak = 0;
size_t g_allocs = 0;

void* counted(size_t size) {
    void* p = std::malloc(size ? size : 1);
    if (p == nullptr) std::abort();
    g_live += malloc_usable_size(p);
    ++g_allocs;
    if (g_live > g_peak) g_peak = g_live;
    return p;
}

void release(void* p) {
    if (p == nullptr) return;
    g_live -= malloc_usable_size(p);
    std::free(p);
}
}  // namespace

void* operator new(size_t size) { return counted(size); }
void* operator new[](size_t size) { return counted(size); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, size_t) noexcept { release(p); }
void operator delete[](void* p, size_t) noexcept { release(p); }
#endif

using namespace pktool;

// Number of heap allocations so far (0 when the counters are off): tests/test_bench.cpp uses it
// to find draw code that allocates every frame.
size_t pktoolHeapAllocations() {
#ifdef PKTOOL_COUNT_HEAP
    return g_allocs;
#else
    return 0;
#endif
}

namespace {

constexpr size_t kSave = 512 * 1024;

// Starts measuring: returns the live bytes to subtract from the peak.
size_t startMeasuring() {
#ifdef PKTOOL_COUNT_HEAP
    g_peak = g_live;
    return g_live;
#else
    return 0;
#endif
}

size_t peakSince(size_t start) {
#ifdef PKTOOL_COUNT_HEAP
    return g_peak - start;
#else
    (void)start;
    return 0;
#endif
}

void report(const char* what, size_t peak) {
    std::printf("  %-28s peak %6zu KB\n", what, peak / 1024);
}

uint16_t g_top[gfx::kScreenWidth * gfx::kScreenHeight];
uint16_t g_bottom[gfx::kScreenWidth * gfx::kScreenHeight];

void press(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());  // runs pending actions
    gfx::Canvas top(g_top);
    gfx::Canvas bottom(g_bottom);
    app.draw(top, bottom);
}

struct Variant {
    const char* name;
    std::vector<uint8_t> data;
};

std::vector<Variant> variants() {
    return {{"Gen III (FRLG)", fake::blankGen3(saves::Game::FireRedLeafGreen)},
            {"Gen IV (Pt)", fake::blankGen4(fake::kPt)},
            {"Gen V (B2W2)", fake::blankGen5(true)}};
}

std::string makeDir() {
    char pattern[] = "/tmp/pktool-mem-XXXXXX";
    return mkdtemp(pattern);
}

}  // namespace

TEST(memory_commit_keeps_few_copies_of_the_save) {
    for (const Variant& v : variants()) {
        std::string dir = makeDir();
        std::string path = io::joinPath(dir, "test.sav");
        CHECK(io::safeWriteFile(path, v.data) == io::WriteResult::Ok);
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(std::move(bytes));
        CHECK(save != nullptr);
        if (!save) continue;

        size_t start = startMeasuring();
        saves::CommitResult result = saves::commitSave(*save, path, io::joinPath(dir, "PKTOOL/backups"));
        size_t peak = peakSince(start);
        CHECK(result.status == saves::CommitResult::Status::Ok);
        report(v.name, peak);
        // The save itself is already loaded; validation may keep one more copy.
        if (v.data.size() >= kSave) CHECK(peak <= kSave + 64 * 1024);
    }
}

TEST(memory_opening_a_save_while_another_is_open) {
    for (const Variant& v : variants()) {
        std::string dir = makeDir();
        ui::Config config;
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        std::string path = io::joinPath(dir, "test.sav");
        CHECK(io::safeWriteFile(path, v.data) == io::WriteResult::Ok);
        ui::App app(config);
        CHECK(app.openSave(path));
        press(app, ui::kKeyA);  // Boxes

        size_t start = startMeasuring();
        CHECK(app.openSave(path));  // replaces the open save
        size_t peak = peakSince(start);
        report(v.name, peak);
        if (v.data.size() >= kSave) CHECK(peak <= kSave + 64 * 1024);
    }
}

TEST(memory_scanning_for_saves) {
    for (const Variant& v : variants()) {
        std::string dir = makeDir();
        CHECK(io::safeWriteFile(io::joinPath(dir, "a.sav"), v.data) == io::WriteResult::Ok);
        CHECK(io::safeWriteFile(io::joinPath(dir, "b.sav"), v.data) == io::WriteResult::Ok);
        size_t start = startMeasuring();
        std::vector<saves::SaveSummary> found = saves::findPokemonSaves(dir, 1);
        size_t peak = peakSince(start);
        CHECK_EQ(found.size(), 2u);
        report(v.name, peak);
        if (v.data.size() >= kSave) CHECK(peak <= kSave + 64 * 1024);
    }
}

TEST(memory_save_menu_flow_through_the_ui) {
    for (const Variant& v : variants()) {
        std::string dir = makeDir();
        ui::Config config;
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        std::string path = io::joinPath(dir, "test.sav");
        CHECK(io::safeWriteFile(path, v.data) == io::WriteResult::Ok);
        ui::App app(config);
        CHECK(app.openSave(path));

        size_t start = startMeasuring();
        press(app, ui::kKeyA);       // Boxes
        press(app, ui::kKeyR);       // box 1
        press(app, ui::kKeyA);       // empty slot: Add menu
        press(app, ui::kKeyA);       // New Pokemon: species picker
        press(app, ui::kKeyA);       // create the first species
        press(app, ui::kKeyA);       // close the report
        press(app, ui::kKeyStart);   // apply the editor
        press(app, ui::kKeySelect);  // write
        press(app, ui::kKeyA);       // confirm
        size_t peak = peakSince(start);
        report(v.name, peak);
        // Growth above the open save: pickers, one Pokemon, the commit.
        if (v.data.size() >= kSave) CHECK(peak <= kSave + 256 * 1024);
    }
}

TEST(memory_event_picker_and_bank) {
    for (const Variant& v : variants()) {
        std::string dir = makeDir();
        ui::Config config;
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        std::string path = io::joinPath(dir, "test.sav");
        CHECK(io::safeWriteFile(path, v.data) == io::WriteResult::Ok);
        ui::App app(config);
        CHECK(app.openSave(path));
        press(app, ui::kKeyA);  // Boxes
        press(app, ui::kKeyR);  // box 1

        size_t start = startMeasuring();
        press(app, ui::kKeyA);     // empty slot: Add menu
        press(app, ui::kKeyDown);
        press(app, ui::kKeyDown);  // Event
        press(app, ui::kKeyA);     // the Wonder Card list (hundreds of rows)
        size_t picker = peakSince(start);
        press(app, ui::kKeyB);
        press(app, ui::kKeyB);
        press(app, ui::kKeyY);     // Bank
        size_t bank = peakSince(start);
        report(v.name, picker);
        report("  + Bank", bank);
        CHECK(picker <= 160 * 1024);
        CHECK(bank <= 300 * 1024);
    }
}

// ---------------------------------------------------------------------------------------------
// Audit of the other big consumers of the DS heap. Each test prints the peak above the
// starting point and what stays allocated afterwards.

namespace {

// Bytes still allocated above the starting point.
size_t retainedSince(size_t start) {
#ifdef PKTOOL_COUNT_HEAP
    return g_live > start ? g_live - start : 0;
#else
    (void)start;
    return 0;
#endif
}

void reportRetained(const char* what, size_t peak, size_t kept) {
    std::printf("  %-28s peak %6zu KB, stays %6zu KB\n", what, peak / 1024, kept / 1024);
}

}  // namespace

TEST(memory_bank_load_and_save_with_600_pokemon) {
    std::string dir = makeDir();
    std::string path = io::joinPath(dir, "bank.pkb");
    {
        bank::Bank full;
        CHECK(full.load(path) == bank::Bank::LoadResult::Created);
        for (int box = 0; box < bank::Bank::kBoxCount; ++box) {
            for (int slot = 0; slot < bank::Bank::kSlotsPerBox; ++slot) {
                CHECK(full.set(box, slot, 4, test::kRoseradePk4));
            }
        }
        size_t start = startMeasuring();
        CHECK(full.save() == io::WriteResult::Ok);
        reportRetained("Bank save (600 Pokemon)", peakSince(start), retainedSince(start));
        // The serialised Bank (one copy) is all it may add to the Bank itself.
        CHECK(peakSince(start) <= 100 * 1024);
    }
    size_t start = startMeasuring();
    bank::Bank loaded;
    CHECK(retainedSince(start) <= 1024);  // nothing until it is loaded
    CHECK(loaded.load(path) == bank::Bank::LoadResult::Ok);
    size_t peak = peakSince(start);
    reportRetained("Bank load (600 Pokemon)", peak, retainedSince(start));
    CHECK_EQ(loaded.count(), 600);
    // The Bank itself is ~82 KB; loading it may not hold the 84 KB file as well.
    CHECK(retainedSince(start) <= 90 * 1024);
    CHECK(peak <= 180 * 1024);  // the file and the Bank are both held while parsing
}

TEST(memory_the_app_and_the_bank_cost_nothing_until_used) {
    std::string dir = makeDir();
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    size_t start = startMeasuring();
    {
        ui::App app(config);
        reportRetained("idle App", peakSince(start), retainedSince(start));
        // (The Bank's 82 KB of slots, the sprite cache and the lists are allocated on demand.)
        CHECK(retainedSince(start) <= 24 * 1024);
    }
    bank::Bank unused;
    CHECK_EQ(unused.count(), 0);
    CHECK(unused.entity(3, 3) == nullptr);
    CHECK(unused.search(bank::Bank::SearchFilter()).empty());
    CHECK(retainedSince(start) <= 2 * 1024);
    // Writing to it allocates the slots, and what was written reads back.
    CHECK(unused.set(1, 2, 4, test::kRoseradePk4));
    CHECK_EQ(unused.count(), 1);
    CHECK_EQ(unused.format(1, 2), 4);
    CHECK(unused.serialize().size() > 80 * 1024);
}

TEST(memory_language_packs_and_the_cjk_font) {
    const std::string assets = std::string(PKTOOL_SOURCE_DIR) + "/assets/nitrofs";
    data::setLanguageDirs({assets});
    for (uint8_t lang : {data::kLangJapanese, data::kLangKorean, data::kLangGerman}) {
        size_t start = startMeasuring();
        CHECK(data::setNameLanguage(lang));
        reportRetained(data::languageName(lang), peakSince(start), retainedSince(start));
        CHECK(peakSince(start) <= 80 * 1024);  // the biggest pack is 52 KB; one copy plus the offsets
        CHECK(retainedSince(start) <= 64 * 1024);
    }
    CHECK(data::setNameLanguage(data::kLangEnglish));
    // Default nicknames in three other languages: species names only.
    size_t start = startMeasuring();
    for (uint8_t lang : {data::kLangJapanese, data::kLangFrench, data::kLangItalian}) {
        CHECK(data::speciesNameIn(lang, 25) != nullptr);
    }
    reportRetained("3 nickname languages", peakSince(start), retainedSince(start));
    CHECK(retainedSince(start) <= 40 * 1024);
    data::setLanguageDirs({});

    std::vector<uint16_t> pixels(gfx::kScreenWidth * gfx::kScreenHeight);  // (the screen is not the font's)
    gfx::Canvas canvas(pixels.data());
    start = startMeasuring();
    CHECK(gfx::openCjkFont({assets}));
    reportRetained("CJK font index", peakSince(start), retainedSince(start));
    CHECK(retainedSince(start) <= 24 * 1024);  // the 123 KB bitmaps stay in the file
    for (int row = 0; row < 8; ++row) {
        canvas.text(gfx::kFontSmall, 0, row * 20,
                    "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88\xE3\x81\x8A\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94",
                    gfx::rgb(255, 255, 255));
    }
    reportRetained("CJK font + glyph cache", peakSince(start), retainedSince(start));
    CHECK(retainedSince(start) <= 40 * 1024);
    gfx::closeCjkFont();
}

TEST(memory_legalize_and_check) {
    pokemon::create4::Trainer trainer;
    trainer.name = "ASH";
    trainer.tid = 12345;
    trainer.sid = 54321;
    trainer.today = {26, 9, 29};
    struct Gen {
        const char* name;
        uint8_t version;
        uint16_t last;
    };
    for (Gen g : {Gen{"Gen III", 4, 386}, Gen{"Gen IV", 12, 493}, Gen{"Gen V", 23, 649}}) {
        trainer.version = g.version;
        size_t worstLegalize = 0;
        size_t worstCheck = 0;
        for (uint16_t species = 1; species <= g.last; species += 7) {
            pokemon::Pokemon p = pokemon::create4::newPokemon(trainer, species, 70, species * 31u);
            for (auto& iv : p.ivs) iv = 31;
            size_t start = startMeasuring();
            legality::Report report = legality::check(p);
            worstCheck = std::max(worstCheck, peakSince(start));
            start = startMeasuring();
            legality::LegalizeResult result = legality::legalize(p, 4);
            worstLegalize = std::max(worstLegalize, peakSince(start));
            (void)report;
            (void)result;
        }
        std::printf("  %-28s check peak %4zu KB, LEGALIZE peak %4zu KB\n", g.name, worstCheck / 1024, worstLegalize / 1024);
        CHECK(worstCheck <= 64 * 1024);
        CHECK(worstLegalize <= 128 * 1024);
    }
}
