#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "tests/test.h"

using namespace pktool;

namespace {

std::string makeTempDir() {
    char pattern[] = "/tmp/pktool-test-XXXXXX";
    const char* dir = mkdtemp(pattern);
    return dir ? dir : "";
}

}  // namespace

TEST(file_io_safe_write_creates_and_replaces) {
    std::string dir = makeTempDir();
    CHECK(!dir.empty());
    std::string path = io::joinPath(dir, "game.sav");

    std::vector<uint8_t> first(1024, 0x11);
    CHECK(io::safeWriteFile(path, first) == io::WriteResult::Ok);
    std::vector<uint8_t> read;
    CHECK(io::readFile(path, read));
    CHECK(read == first);
    CHECK(!io::fileExists(path + ".tmp"));

    std::vector<uint8_t> second(1024, 0x22);
    CHECK(io::safeWriteFile(path, second) == io::WriteResult::Ok);
    CHECK(io::readFile(path, read));
    CHECK(read == second);
    CHECK(io::readFile(path + ".bak", read));
    CHECK(read == first);
}

TEST(file_io_failed_write_leaves_original_untouched) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "game.sav");
    std::vector<uint8_t> original(64, 0x33);
    CHECK(io::safeWriteFile(path, original) == io::WriteResult::Ok);
    // A directory with the temporary file's name makes the first step fail.
    CHECK(io::makeDirectories(path + ".tmp"));
    CHECK(io::safeWriteFile(path, std::vector<uint8_t>(64, 0x44)) == io::WriteResult::TempWriteFailed);
    std::vector<uint8_t> read;
    CHECK(io::readFile(path, read));
    CHECK(read == original);
}

TEST(file_io_backup_list_and_restore) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "Pokemon Platinum.sav");
    std::string backups = io::joinPath(dir, "PKTOOL/backups");

    std::vector<uint8_t> v1(128, 1);
    CHECK(io::safeWriteFile(path, v1) == io::WriteResult::Ok);
    std::string backup1 = io::createBackup(path, backups);
    CHECK(!backup1.empty());

    std::vector<uint8_t> v2(128, 2);
    CHECK(io::safeWriteFile(path, v2) == io::WriteResult::Ok);
    std::string backup2 = io::createBackup(path, backups);
    CHECK(!backup2.empty());
    CHECK(backup1 != backup2);

    std::vector<std::string> list = io::listBackups(path, backups);
    CHECK_EQ(list.size(), 2u);
    CHECK(list.size() == 2 && list[0] == backup2);

    CHECK(io::restoreBackup(backup1, path, backups) == io::WriteResult::Ok);
    std::vector<uint8_t> read;
    CHECK(io::readFile(path, read));
    CHECK(read == v1);
    // Restoring backed up the state it replaced.
    CHECK_EQ(io::listBackups(path, backups).size(), 3u);
}

TEST(file_io_backup_of_a_full_save_is_exact) {
    // Backups and read-back checks are streamed in chunks; a 512 KB save with
    // a size that is not a multiple of the chunk must still copy exactly.
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "big.sav");
    std::string backups = io::joinPath(dir, "PKTOOL/backups");
    std::vector<uint8_t> data(512 * 1024 + 77);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 131 + (i >> 9));
    CHECK(io::safeWriteFile(path, data) == io::WriteResult::Ok);

    std::string backup = io::createBackup(path, backups);
    CHECK(!backup.empty());
    std::vector<uint8_t> copy;
    CHECK(io::readFile(backup, copy));
    CHECK(copy == data);
    CHECK(!io::fileExists(backup + ".tmp"));
    CHECK(!io::fileExists(path + ".tmp"));

    // No backup of a missing file.
    CHECK(io::createBackup(io::joinPath(dir, "missing.sav"), backups).empty());
}

TEST(file_io_finds_save_files_recursively) {
    std::string dir = makeTempDir();
    CHECK(io::makeDirectories(io::joinPath(dir, "roms/pokemon")));
    CHECK(io::makeDirectories(io::joinPath(dir, "_system_")));
    const std::vector<uint8_t> blank(4, 0);
    for (const char* name : {"a.sav", "roms/b.SAV", "roms/pokemon/c.sav", "roms/pokemon/c.nds", "_system_/d.sav"}) {
        CHECK(io::safeWriteFile(io::joinPath(dir, name), blank) == io::WriteResult::Ok);
    }
    std::vector<std::string> found = io::findSaveFiles(dir, 3);
    CHECK_EQ(found.size(), 3u);
    CHECK_EQ(io::findSaveFiles(dir, 0).size(), 1u);
}

TEST(file_io_write_with_backup_moves_the_current_file_into_the_backups) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "game.sav");
    std::string backups = io::joinPath(dir, "PKTOOL/backups");
    std::vector<uint8_t> before(1000, 0x11);
    std::vector<uint8_t> after(1000, 0x22);
    CHECK(io::safeWriteFile(path, before) == io::WriteResult::Ok);
    std::remove((path + ".bak").c_str());
    std::string backup;
    CHECK(io::safeWriteWithBackup(path, after, backups, backup) == io::WriteResult::Ok);
    std::vector<uint8_t> read;
    CHECK(io::readFile(path, read));
    CHECK(read == after);
    CHECK(!backup.empty());
    CHECK(io::readFile(backup, read));
    CHECK(read == before);  // the old file itself, moved
    CHECK(!io::fileExists(path + ".tmp"));
    CHECK_EQ(io::listBackups(path, backups).size(), 1u);
    // A second save in the same second gets its own backup.
    std::string second;
    CHECK(io::safeWriteWithBackup(path, before, backups, second) == io::WriteResult::Ok);
    CHECK(second != backup);
    CHECK_EQ(io::listBackups(path, backups).size(), 2u);
    // No current file: just written, no backup.
    std::string fresh = io::joinPath(dir, "new.sav");
    std::string none;
    CHECK(io::safeWriteWithBackup(fresh, after, backups, none) == io::WriteResult::Ok);
    CHECK(none.empty());
}
