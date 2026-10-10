// Bank tests: file format (v1 -> v2), box names, release, sorting, search,
// corrupt files, and the Bank screen driven like the DS (menu, rename,
// release, sort, search).

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/bank/bank.h"
#include "core/data/personal.h"
#include "core/io/file_io.h"
#include "core/pokemon/pk4.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
using bank::Bank;
namespace pk4 = pokemon::pk4;

namespace {

std::vector<uint16_t> g_top(gfx::kScreenWidth * gfx::kScreenHeight);
std::vector<uint16_t> g_bottom(gfx::kScreenWidth * gfx::kScreenHeight);

std::string tempDir() {
    char pattern[] = "/tmp/pktool-bank-XXXXXX";
    return mkdtemp(pattern);
}

// A decrypted PK4 of `species` at `level`; shiny ones get PID 0 with TID = SID = 0.
struct Mon {
    uint8_t data[Bank::kEntitySize] = {};
    Mon(uint16_t species, uint8_t level, bool shiny = false) {
        const data::PersonalInfo* info = data::personal4(species);
        util::writeU16(data + pk4::offset::kSpecies, species);
        util::writeU32(data + pk4::offset::kPid, shiny ? 0 : 0x12345678);
        util::writeU32(data + pk4::offset::kExp,
                       data::expForLevel(static_cast<data::GrowthRate>(info->growthRate), level));
    }
};

enum : uint16_t { kBulbasaur = 1, kPidgey = 16, kPikachu = 25, kZubat = 41, kAbra = 63, kEevee = 133 };

uint16_t speciesAt(const Bank& b, int box, int slot) {
    const uint8_t* e = b.entity(box, slot);
    return e ? util::readU16(e + pk4::offset::kSpecies) : 0;
}

void put(Bank& b, int box, int slot, uint16_t species, uint8_t level, bool shiny = false) {
    Mon m(species, level, shiny);
    CHECK(b.set(box, slot, 4, m.data));
}

// A version 1 file as the first Bank releases wrote it.
std::vector<uint8_t> v1File(const Bank& b) {
    std::vector<uint8_t> v2 = b.serialize();
    const size_t namesSize = Bank::kBoxCount * Bank::kBoxNameBytes;
    std::vector<uint8_t> out(v2.begin(), v2.begin() + 8);
    out.insert(out.end(), v2.begin() + 8 + namesSize, v2.end() - 2);
    util::writeU16(out.data() + 4, 1);
    out.resize(out.size() + 2);
    util::writeU16(out.data() + out.size() - 2, util::crc16Ccitt(out.data(), out.size() - 2));
    return out;
}

// ----- UI helpers (see test_trainer_bag_ui.cpp) -----

void draw(ui::App& app) {
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);
}

void press(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
    draw(app);
}

void tap(ui::App& app, int x, int y) {
    ui::Input in;
    in.touched = true;
    in.touchX = x;
    in.touchY = y;
    app.update(in);
    app.update(ui::Input());
    draw(app);
}

void tapKey(ui::App& app, int row, int col) { tap(app, 8 + col * 24 + 10, 8 + row * 28 + 10); }
void tapOk(ui::App& app) { tap(app, 210, 134); }

struct Env {
    ui::Config config;
    std::string bankFile;
    Env() {
        std::string dir = tempDir();
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        bankFile = io::joinPath(config.dataDir, "bank/bank.pkb");
    }
    void store(Bank& b) {
        b.load(bankFile);  // sets the path
        CHECK(b.save() == io::WriteResult::Ok);
    }
};

// Opens the Bank from the home screen.
void openBankScreen(ui::App& app) {
    press(app, ui::kKeyDown);  // Bank tile
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
}

// Opens entry `row` of the Bank menu (Y, then Down `row` times, A).
void bankMenu(ui::App& app, int row) {
    press(app, ui::kKeyY);
    for (int i = 0; i < row; ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
}

// A Bank with five Pokémon scattered over two boxes; the file is written.
void seed(Env& env) {
    Bank b;
    b.load(env.bankFile);
    put(b, 0, 3, kZubat, 5);
    put(b, 0, 9, kEevee, 30);
    put(b, 0, 12, kBulbasaur, 12);
    put(b, 1, 5, kAbra, 30);
    put(b, 1, 20, kPikachu, 7, true);
    CHECK(b.save() == io::WriteResult::Ok);
}

}  // namespace

TEST(bank_v1_file_loads_and_is_written_as_v2) {
    std::string dir = tempDir();
    std::string file = io::joinPath(dir, "bank.pkb");
    Bank src;
    put(src, 0, 0, kPikachu, 10);
    put(src, 19, 29, kEevee, 50);
    CHECK(io::safeWriteFile(file, v1File(src)) == io::WriteResult::Ok);

    Bank b;
    CHECK(b.load(file) == Bank::LoadResult::Ok);
    CHECK(!b.readOnly());
    CHECK_EQ(b.count(), 2);
    CHECK_EQ(speciesAt(b, 0, 0), kPikachu);
    CHECK_EQ(speciesAt(b, 19, 29), kEevee);
    CHECK(b.boxName(0) == "Box 1");
    CHECK(b.boxName(19) == "Box 20");
    CHECK(!b.hasCustomBoxName(0));

    CHECK(b.save() == io::WriteResult::Ok);
    std::vector<uint8_t> bytes;
    CHECK(io::readFile(file, bytes));
    CHECK_EQ(util::readU16(bytes.data() + 4), 2);
    Bank again;
    CHECK(again.load(file) == Bank::LoadResult::Ok);
    CHECK_EQ(again.count(), 2);
    CHECK(std::memcmp(again.entity(19, 29), src.entity(19, 29), Bank::kEntitySize) == 0);
}

TEST(bank_box_names_round_trip) {
    std::string file = io::joinPath(tempDir(), "bank.pkb");
    Bank b;
    CHECK(b.load(file) == Bank::LoadResult::Created);
    CHECK(b.setBoxName(0, "Shinies"));
    CHECK(b.setBoxName(19, "Caf\xC3\xA9 trade"));
    CHECK(b.setBoxName(5, "ABCDEFGHIJKLMNOPQRS"));  // too long: cut to 12 characters
    CHECK(b.boxName(5) == "ABCDEFGHIJKL");
    CHECK(b.setBoxName(7, "Box 8"));  // the default name is not stored
    CHECK(!b.hasCustomBoxName(7));
    CHECK(b.setBoxName(8, "Temp"));
    CHECK(b.setBoxName(8, "   "));  // blank restores the default
    CHECK(b.boxName(8) == "Box 9");
    CHECK(!b.setBoxName(20, "No"));
    CHECK(!b.setBoxName(-1, "No"));
    CHECK(b.save() == io::WriteResult::Ok);

    Bank r;
    CHECK(r.load(file) == Bank::LoadResult::Ok);
    CHECK(r.boxName(0) == "Shinies");
    CHECK(r.boxName(19) == "Caf\xC3\xA9 trade");
    CHECK(r.boxName(5) == "ABCDEFGHIJKL");
    CHECK(r.boxName(1) == "Box 2");
    CHECK(r.hasCustomBoxName(0));
}

TEST(bank_release_clears_only_occupied_slots) {
    Bank b;
    put(b, 2, 4, kPikachu, 10);
    CHECK(!b.release(2, 5));
    CHECK(b.release(2, 4));
    CHECK_EQ(b.count(), 0);
    CHECK(b.entity(2, 4) == nullptr);
    CHECK(!b.release(2, 4));
    CHECK(!b.release(20, 0));
}

TEST(bank_sort_orders_and_compaction) {
    Bank b;
    put(b, 0, 3, kZubat, 5);
    put(b, 0, 9, kEevee, 30);
    put(b, 0, 12, kBulbasaur, 12);
    put(b, 0, 20, kAbra, 30);
    put(b, 1, 0, kPikachu, 99);  // another box: untouched by a box sort

    CHECK(b.sort(Bank::SortKey::Dex, 0));
    CHECK_EQ(speciesAt(b, 0, 0), kBulbasaur);
    CHECK_EQ(speciesAt(b, 0, 1), kZubat);
    CHECK_EQ(speciesAt(b, 0, 2), kAbra);
    CHECK_EQ(speciesAt(b, 0, 3), kEevee);
    for (int s = 4; s < 30; ++s) CHECK(b.entity(0, s) == nullptr);
    CHECK_EQ(speciesAt(b, 1, 0), kPikachu);

    CHECK(b.sort(Bank::SortKey::Name, 0));
    CHECK_EQ(speciesAt(b, 0, 0), kAbra);
    CHECK_EQ(speciesAt(b, 0, 1), kBulbasaur);
    CHECK_EQ(speciesAt(b, 0, 2), kEevee);
    CHECK_EQ(speciesAt(b, 0, 3), kZubat);

    // Level: highest first; Abra and Eevee (both 30) keep their relative order (stable).
    CHECK(b.sort(Bank::SortKey::Dex, 0));
    CHECK(b.sort(Bank::SortKey::Level, 0));
    CHECK_EQ(speciesAt(b, 0, 0), kAbra);  // Dex order was Abra, Eevee
    CHECK_EQ(speciesAt(b, 0, 1), kEevee);
    CHECK_EQ(speciesAt(b, 0, 2), kBulbasaur);
    CHECK_EQ(speciesAt(b, 0, 3), kZubat);
    CHECK_EQ(b.count(), 5);
}

TEST(bank_sort_whole_bank_is_stable_and_keeps_entities_intact) {
    Bank b;
    put(b, 0, 29, kEevee, 30);
    put(b, 3, 0, kZubat, 5, true);
    put(b, 7, 7, kEevee, 31);
    put(b, 19, 29, kBulbasaur, 2);
    Mon zubat(kZubat, 5, true);

    CHECK(b.sort(Bank::SortKey::Dex, -1));
    CHECK_EQ(speciesAt(b, 0, 0), kBulbasaur);
    CHECK_EQ(speciesAt(b, 0, 1), kZubat);
    CHECK_EQ(speciesAt(b, 0, 2), kEevee);  // Eevee Lv30 was first in Bank order
    CHECK_EQ(speciesAt(b, 0, 3), kEevee);
    const uint8_t* second = b.entity(0, 2);
    CHECK_EQ(util::readU32(second + pk4::offset::kExp) < util::readU32(b.entity(0, 3) + pk4::offset::kExp), true);
    CHECK(std::memcmp(b.entity(0, 1), zubat.data, Bank::kEntitySize) == 0);
    CHECK_EQ(b.format(0, 1), 4);
    CHECK_EQ(b.count(), 4);
    CHECK(b.entity(19, 29) == nullptr);
    // Sorting again changes nothing.
    std::vector<uint8_t> before = b.serialize();
    CHECK(b.sort(Bank::SortKey::Dex, -1));
    CHECK(before == b.serialize());
    CHECK(!b.sort(Bank::SortKey::Dex, 20));
}

TEST(bank_search_by_name_shiny_and_format) {
    Bank b;
    put(b, 0, 3, kPikachu, 10);
    put(b, 1, 0, kPidgey, 10);
    put(b, 4, 29, kPikachu, 40, true);
    put(b, 5, 1, kEevee, 20);
    Bank::SearchFilter f;
    f.text = "PI";
    std::vector<Bank::Match> r = b.search(f);
    CHECK_EQ(r.size(), 3u);
    CHECK(r[0].box == 0 && r[0].slot == 3);
    CHECK(r[1].box == 1 && r[1].slot == 0);
    CHECK(r[2].box == 4 && r[2].slot == 29);
    f.text = "kac";
    CHECK_EQ(b.search(f).size(), 2u);
    f.shinyOnly = true;
    r = b.search(f);
    CHECK(r.size() == 1 && r[0].box == 4);
    f.shinyOnly = false;
    f.text = "";
    CHECK_EQ(b.search(f).size(), 4u);
    f.format = 5;
    CHECK(b.search(f).empty());
    f.format = 4;
    CHECK_EQ(b.search(f).size(), 4u);
    f.text = "nothing";
    CHECK(b.search(f).empty());
}

TEST(bank_corrupt_file_stays_read_only_and_untouched) {
    std::string file = io::joinPath(tempDir(), "bank.pkb");
    Bank src;
    put(src, 0, 0, kPikachu, 10);
    std::vector<uint8_t> bytes = src.serialize();
    bytes[bytes.size() / 2] ^= 0xFF;  // CRC no longer matches
    CHECK(io::safeWriteFile(file, bytes) == io::WriteResult::Ok);

    Bank b;
    CHECK(b.load(file) == Bank::LoadResult::Corrupt);
    CHECK(b.readOnly());
    CHECK_EQ(b.count(), 0);
    CHECK(!b.setBoxName(0, "X"));
    CHECK(!b.sort(Bank::SortKey::Dex, -1));
    CHECK(!b.release(0, 0));
    CHECK(b.save() != io::WriteResult::Ok);
    std::vector<uint8_t> after;
    CHECK(io::readFile(file, after));
    CHECK(after == bytes);
    // Unknown versions are refused too.
    std::vector<uint8_t> future = src.serialize();
    util::writeU16(future.data() + 4, 9);
    util::writeU16(future.data() + future.size() - 2, util::crc16Ccitt(future.data(), future.size() - 2));
    CHECK(io::safeWriteFile(file, future) == io::WriteResult::Ok);
    CHECK(b.load(file) == Bank::LoadResult::Corrupt);
}

// ----- UI -----

TEST(ui_bank_rename_box_by_menu_and_by_touch) {
    Env env;
    seed(env);
    {
        ui::App app(env.config);
        openBankScreen(app);
        bankMenu(app, 0);  // Rename this box
        CHECK(std::strcmp(app.screenName(), "Keyboard") == 0);
        tapKey(app, 0, 0);  // A
        tapKey(app, 0, 1);  // B
        tapOk(app);
        CHECK(std::strcmp(app.screenName(), "Bank") == 0);
        press(app, ui::kKeyR);  // box 2
        tap(app, 128, 12);      // tap the box title
        CHECK(std::strcmp(app.screenName(), "Keyboard") == 0);
        tapKey(app, 3, 0);  // 0
        tapKey(app, 3, 7);  // 7
        tapOk(app);
    }
    Bank b;
    CHECK(b.load(env.bankFile) == Bank::LoadResult::Ok);
    CHECK(b.boxName(0) == "AB");
    CHECK(b.boxName(1) == "07");
    CHECK(b.boxName(2) == "Box 3");
    CHECK_EQ(b.count(), 5);
}

TEST(ui_bank_release_asks_first) {
    Env env;
    seed(env);
    ui::App app(env.config);
    openBankScreen(app);
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyRight);  // slot 4: Zubat
    bankMenu(app, 1);                                      // Release
    press(app, ui::kKeyB);                                 // No
    Bank b;
    CHECK(b.load(env.bankFile) == Bank::LoadResult::Ok);
    CHECK_EQ(b.count(), 5);
    bankMenu(app, 1);
    press(app, ui::kKeyA);  // Yes
    CHECK(b.load(env.bankFile) == Bank::LoadResult::Ok);
    CHECK_EQ(b.count(), 4);
    CHECK(b.entity(0, 3) == nullptr);
    CHECK_EQ(speciesAt(b, 0, 9), kEevee);
    // An empty slot has nothing to release.
    bankMenu(app, 1);
    press(app, ui::kKeyA);  // dismiss the message
    CHECK(b.load(env.bankFile) == Bank::LoadResult::Ok);
    CHECK_EQ(b.count(), 4);
}

TEST(ui_bank_sort_box_and_whole_bank) {
    Env env;
    seed(env);
    ui::App app(env.config);
    openBankScreen(app);
    bankMenu(app, 3);       // Sort this box...
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // By name
    press(app, ui::kKeyA);  // dismiss "Sorted"
    Bank b;
    CHECK(b.load(env.bankFile) == Bank::LoadResult::Ok);
    CHECK_EQ(speciesAt(b, 0, 0), kBulbasaur);
    CHECK_EQ(speciesAt(b, 0, 1), kEevee);
    CHECK_EQ(speciesAt(b, 0, 2), kZubat);
    CHECK_EQ(speciesAt(b, 1, 5), kAbra);  // box 2 untouched

    bankMenu(app, 4);       // Sort the whole Bank...
    press(app, ui::kKeyA);  // By Dex number
    press(app, ui::kKeyA);
    CHECK(b.load(env.bankFile) == Bank::LoadResult::Ok);
    CHECK_EQ(speciesAt(b, 0, 0), kBulbasaur);
    CHECK_EQ(speciesAt(b, 0, 1), kPikachu);
    CHECK_EQ(speciesAt(b, 0, 2), kZubat);
    CHECK_EQ(speciesAt(b, 0, 3), kAbra);
    CHECK_EQ(speciesAt(b, 0, 4), kEevee);
    CHECK(b.entity(1, 0) == nullptr);

    // B leaves the criterion list without sorting.
    bankMenu(app, 3);
    press(app, ui::kKeyB);
    press(app, ui::kKeyB);
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
}

TEST(ui_bank_search_jumps_to_the_result) {
    Env env;
    Bank b;
    b.load(env.bankFile);
    put(b, 0, 0, kPidgey, 5);
    put(b, 2, 7, kPikachu, 7);
    put(b, 3, 1, kEevee, 7);
    CHECK(b.save() == io::WriteResult::Ok);

    ui::App app(env.config);
    openBankScreen(app);
    bankMenu(app, 2);       // Search the Bank: the filters screen
    press(app, ui::kKeyA);  // Name: opens the keyboard
    tapKey(app, 1, 5);      // P
    tapKey(app, 0, 8);      // I
    tapOk(app);
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);  // the Search row
    press(app, ui::kKeyA);
    press(app, ui::kKeyDown);  // second result: Pikachu in box 3 slot 8
    press(app, ui::kKeyA);     // jump
    press(app, ui::kKeyX);     // pick it up where the cursor is...
    press(app, ui::kKeyR);     // ...move to box 4 and drop it on the same slot
    press(app, ui::kKeyX);
    CHECK(b.load(env.bankFile) == Bank::LoadResult::Ok);
    CHECK_EQ(speciesAt(b, 3, 7), kPikachu);
    CHECK(b.entity(2, 7) == nullptr);
    CHECK_EQ(speciesAt(b, 0, 0), kPidgey);

    // No match: a message, the Bank screen stays.
    bankMenu(app, 2);
    press(app, ui::kKeyA);  // Name (still "PI")
    press(app, ui::kKeyB);
    press(app, ui::kKeyB);
    tapKey(app, 2, 5);  // Z
    tapOk(app);
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
    press(app, ui::kKeyA);  // dismiss
    press(app, ui::kKeyB);  // back to the filters
    press(app, ui::kKeyB);  // back to the menu
    press(app, ui::kKeyB);  // back to the Bank
    press(app, ui::kKeyB);  // back home
    CHECK(std::strcmp(app.screenName(), "Home") == 0);
}

TEST(ui_bank_refuses_changes_while_a_withdrawal_is_pending) {
    Env env;
    Bank b;
    b.load(env.bankFile);
    put(b, 0, 3, kZubat, 5);
    put(b, 0, 9, kBulbasaur, 12);
    CHECK(b.save() == io::WriteResult::Ok);
    std::vector<uint8_t> original;
    CHECK(io::readFile(env.bankFile, original));

    std::string savePath = io::joinPath(env.config.scanRoot, "test.sav");
    CHECK(io::safeWriteFile(savePath, fake::blankGen4(fake::kPt)) == io::WriteResult::Ok);
    ui::App app(env.config);
    CHECK(app.openSave(savePath));
    press(app, ui::kKeyA);  // save menu: Boxes
    press(app, ui::kKeyY);  // Bank
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyRight);
    press(app, ui::kKeyA);  // withdraw the Zubat into box 1 slot 1
    press(app, ui::kKeyA);  // dismiss
    press(app, ui::kKeyY);  // back to the Bank (a withdrawal is pending)
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
    bankMenu(app, 3);       // Sort this box: refused
    press(app, ui::kKeyA);  // dismiss the message
    bankMenu(app, 0);       // Rename: refused
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
    std::vector<uint8_t> after;
    CHECK(io::readFile(env.bankFile, after));
    CHECK(after == original);

    // Held Pokémon: also refused.
    Env env2;
    seed(env2);
    ui::App app2(env2.config);
    openBankScreen(app2);
    for (int i = 0; i < 3; ++i) press(app2, ui::kKeyRight);
    press(app2, ui::kKeyX);  // pick up the Zubat
    std::vector<uint8_t> beforeSort;
    CHECK(io::readFile(env2.bankFile, beforeSort));
    bankMenu(app2, 4);
    press(app2, ui::kKeyA);  // dismiss
    std::vector<uint8_t> afterSort;
    CHECK(io::readFile(env2.bankFile, afterSort));
    CHECK(beforeSort == afterSort);
}

TEST(ui_bank_corrupt_file_cannot_be_changed) {
    Env env;
    CHECK(io::makeDirectories(io::joinPath(env.config.dataDir, "bank")));
    std::vector<uint8_t> junk(300, 0x42);
    CHECK(io::safeWriteFile(env.bankFile, junk) == io::WriteResult::Ok);
    ui::App app(env.config);
    openBankScreen(app);
    press(app, ui::kKeyA);  // dismiss the "damaged" message
    bankMenu(app, 0);       // refused
    press(app, ui::kKeyA);
    bankMenu(app, 3);
    press(app, ui::kKeyA);
    std::vector<uint8_t> after;
    CHECK(io::readFile(env.bankFile, after));
    CHECK(after == junk);
}
