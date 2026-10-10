// The browser scans the SD card and probes every .sav file. Reading all of each one (512 KiB at
// ~1 MB/s on the DS) was the slowest part of opening the browser, so probeSave() reads only the
// footers, trainer card, party and what the version needs. These tests count the bytes read and
// check the answer is the same as with the whole file.

#include <utime.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "core/saves/gen3_save.h"
#include "core/saves/gen4_save.h"
#include "core/saves/gen5_save.h"
#include "core/saves/save_probe.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "tests/fake_saves.h"
#include "tests/fixtures.h"
#include "tests/test.h"

using namespace pktool;
using saves::Game;

namespace {

std::string makeDir() {
    char pattern[] = "/tmp/pktool-scan-XXXXXX";
    return mkdtemp(pattern);
}

struct Sample {
    const char* name;
    std::vector<uint8_t> data;
    size_t maxBytes;  // what a probe may read at most
};

// A save with a Roserade (or the equivalent) in the party, so the summary has a party to compare.
std::vector<uint8_t> withParty4(std::vector<uint8_t> data) {
    saves::Gen4Save save;
    CHECK(save.load(std::move(data)) == saves::LoadResult::Ok);
    CHECK(save.appendParty(test::kRoseradePk4));
    save.refreshChecksums();
    return save.data();
}

std::vector<Sample> samples() {
    return {
        {"gen3 FRLG", fake::blankGen3(Game::FireRedLeafGreen), 60 * 1024},
        {"gen3 Emerald", fake::blankGen3(Game::Emerald), 60 * 1024},
        {"gen3 RS", fake::blankGen3(Game::RubySapphire), 60 * 1024},
        {"gen4 DP", withParty4(fake::blankGen4(fake::kDp)), 130 * 1024},
        {"gen4 Pt", withParty4(fake::blankGen4(fake::kPt)), 130 * 1024},
        {"gen4 HGSS", withParty4(fake::blankGen4(fake::kHgss)), 144 * 1024},
        {"gen5 BW", fake::blankGen5(false), 4 * 1024},
        {"gen5 B2W2", fake::blankGen5(true), 4 * 1024},
    };
}

bool sameSummary(const saves::SaveSummary& a, const saves::SaveSummary& b) {
    return a.path == b.path && a.game == b.game && a.version == b.version && a.trainer == b.trainer &&
           a.trainerId == b.trainerId && a.party == b.party;
}

}  // namespace

TEST(scan_reads_a_fraction_of_each_save_and_agrees_with_the_whole_file) {
    std::string dir = makeDir();
    for (const Sample& s : samples()) {
        std::string path = io::joinPath(dir, std::string(s.name) + ".sav");
        CHECK(io::safeWriteFile(path, s.data) == io::WriteResult::Ok);

        saves::SaveSummary full;
        CHECK(saves::probeSaveFull(path, full));

        io::resetReadStats();
        saves::SaveSummary quick;
        CHECK(saves::probeSave(path, quick));
        const io::ReadStats stats = io::readStats();
        std::printf("  %-12s read %7llu of %7zu bytes in %3u reads\n", s.name,
                    static_cast<unsigned long long>(stats.bytes), s.data.size(), stats.reads);

        CHECK(sameSummary(full, quick));
        CHECK(quick.trainer == "ASH");
        CHECK(stats.bytes <= s.maxBytes);
        CHECK(stats.bytes < s.data.size() / 2);
        CHECK_EQ(stats.opens, 1u);
    }
}

TEST(scan_ignores_other_files_without_reading_them) {
    std::string dir = makeDir();
    std::vector<uint8_t> noise(saves::Gen4Save::kFileSize);
    for (size_t i = 0; i < noise.size(); ++i) noise[i] = static_cast<uint8_t>(i * 7 + 3);
    std::vector<uint8_t> noise3(saves::Gen3Save::kFileSize, 0x5A);
    CHECK(io::safeWriteFile(io::joinPath(dir, "mario.sav"), noise) == io::WriteResult::Ok);
    CHECK(io::safeWriteFile(io::joinPath(dir, "zelda.sav"), noise3) == io::WriteResult::Ok);
    CHECK(io::safeWriteFile(io::joinPath(dir, "small.sav"), std::vector<uint8_t>(8192, 1)) == io::WriteResult::Ok);
    io::resetReadStats();
    saves::SaveSummary summary;
    CHECK(!saves::probeSave(io::joinPath(dir, "mario.sav"), summary));
    CHECK(!saves::probeSave(io::joinPath(dir, "zelda.sav"), summary));
    CHECK(!saves::probeSave(io::joinPath(dir, "small.sav"), summary));
    CHECK(io::readStats().bytes < 3 * 1024);
}

TEST(scan_finds_the_same_saves_as_before) {
    std::string dir = makeDir();
    CHECK(io::makeDirectories(io::joinPath(dir, "sub")));
    int expected = 0;
    for (const Sample& s : samples()) {
        std::string where = expected % 2 ? "sub" : "";
        CHECK(io::safeWriteFile(io::joinPath(io::joinPath(dir, where), std::string(s.name) + ".sav"), s.data) ==
              io::WriteResult::Ok);
        ++expected;
    }
    io::resetReadStats();
    std::vector<saves::SaveSummary> found = saves::findPokemonSaves(dir, 2);
    CHECK_EQ(static_cast<int>(found.size()), expected);
    // 8 saves, 2.6 MB on the card: well under a fifth of it was read.
    CHECK(io::readStats().bytes < 600 * 1024);
    for (const saves::SaveSummary& f : found) {
        saves::SaveSummary full;
        CHECK(saves::probeSaveFull(f.path, full));
        CHECK(sameSummary(full, f));
    }
}

// If the newest copy of a Gen IV block is corrupt the game (and load()) use the older copy,
// which the quick scan did not read: it must notice and read the whole file.
TEST(scan_falls_back_to_the_whole_file_when_the_newest_copy_is_corrupt) {
    std::string dir = makeDir();
    for (const fake::Gen4Layout* l : {&fake::kDp, &fake::kPt, &fake::kHgss}) {
        std::vector<uint8_t> data = withParty4(fake::blankGen4(*l));
        // Partition 1 becomes the newest, with a general block that fails its checksum.
        uint8_t* general = &data[saves::Gen4Save::kPartitionSize];
        util::writeU32(general + l->generalSize - 0x14, 11);
        util::writeU32(general + l->generalSize - 0x10, 11);
        general[0x200] ^= 0x55;
        std::string path = io::joinPath(dir, "corrupt.sav");
        CHECK(io::safeWriteFile(path, data) == io::WriteResult::Ok);

        saves::SaveSummary full;
        CHECK(saves::probeSaveFull(path, full));
        io::resetReadStats();
        saves::SaveSummary quick;
        CHECK(saves::probeSave(path, quick));
        CHECK(sameSummary(full, quick));
        CHECK(quick.trainer == "ASH");
        CHECK(io::readStats().bytes >= data.size());  // the retry read it all
    }
}

TEST(scan_gen3_reads_the_current_slot_only) {
    // Slot 1 has the higher counter in the fake save: corrupting slot 0 must not matter, and
    // corrupting slot 1 falls back to... nothing (a Gen III save needs the complete current
    // slot), exactly as with the whole file.
    std::string dir = makeDir();
    std::vector<uint8_t> data = fake::blankGen3(Game::FireRedLeafGreen);
    std::string path = io::joinPath(dir, "g3.sav");
    for (int spoil = 0; spoil < 3; ++spoil) {
        std::vector<uint8_t> copy = data;
        if (spoil == 1) copy[0x100] ^= 0xFF;            // slot 0 sector 0: not the current slot
        if (spoil == 2) copy[14 * 0x1000 + 0x100] ^= 1;  // current slot: checksum fails
        CHECK(io::safeWriteFile(path, copy) == io::WriteResult::Ok);
        saves::SaveSummary full;
        saves::SaveSummary quick;
        bool fullOk = saves::probeSaveFull(path, full);
        bool quickOk = saves::probeSave(path, quick);
        CHECK_EQ(fullOk, quickOk);
        CHECK_EQ(fullOk, spoil != 2);
        if (fullOk) CHECK(sameSummary(full, quick));
    }
}

TEST(rescan_reads_only_the_files_that_changed) {
    std::string dir = makeDir();
    std::string a = io::joinPath(dir, "a.sav");
    std::string b = io::joinPath(dir, "b.sav");
    CHECK(io::safeWriteFile(a, withParty4(fake::blankGen4(fake::kPt))) == io::WriteResult::Ok);
    CHECK(io::safeWriteFile(b, fake::blankGen5(false)) == io::WriteResult::Ok);

    std::vector<saves::SaveSummary> first = saves::findPokemonSaves(dir, 1);
    CHECK_EQ(first.size(), 2u);

    io::resetReadStats();
    std::vector<saves::SaveSummary> again = saves::findPokemonSaves(dir, 1, &first);
    CHECK_EQ(again.size(), 2u);
    CHECK_EQ(io::readStats().bytes, 0u);
    CHECK(again[0].trainer == first[0].trainer && again[0].party == first[0].party);

    // A changed file (new modification time) is read again, the other one is not.
    std::vector<uint8_t> changed = fake::blankGen5(false);
    changed[0x19400 + 4] = 0xFF;  // the trainer name changes
    CHECK(io::safeWriteFile(b, changed) == io::WriteResult::Ok);
    struct utimbuf times = {first[1].modified + 100, first[1].modified + 100};
    CHECK_EQ(utime(b.c_str(), &times), 0);
    io::resetReadStats();
    again = saves::findPokemonSaves(dir, 1, &first);
    CHECK_EQ(again.size(), 2u);
    CHECK(io::readStats().bytes > 0 && io::readStats().bytes < 8 * 1024);
}
