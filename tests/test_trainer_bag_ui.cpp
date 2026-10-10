// UI tests of the trainer card and bag screens: they drive ui::App with
// simulated input on in-memory saves written to a temporary directory.

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "core/saves/gen4_save.h"
#include "core/saves/save_file.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
using saves::BagSlot;
using saves::Game;
using saves::TrainerData;

namespace {

std::vector<uint16_t> g_top(gfx::kScreenWidth * gfx::kScreenHeight);
std::vector<uint16_t> g_bottom(gfx::kScreenWidth * gfx::kScreenHeight);

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
    app.update(ui::Input());  // runs pending actions
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

struct Fixture {
    ui::Config config;
    std::string path;
    std::vector<uint8_t> original;
};

Fixture makeFixture(const std::vector<uint8_t>& bytes) {
    char pattern[] = "/tmp/pktool-tbui-XXXXXX";
    std::string dir = mkdtemp(pattern);
    Fixture f;
    f.config.scanRoot = dir;
    f.config.dataDir = io::joinPath(dir, "PKTOOL");
    f.path = io::joinPath(dir, "test.sav");
    f.original = bytes;
    CHECK(io::safeWriteFile(f.path, bytes) == io::WriteResult::Ok);
    return f;
}

std::unique_ptr<saves::SaveFile> loadFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    CHECK(io::readFile(path, bytes));
    saves::LoadResult result;
    std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(bytes, &result);
    CHECK(result == saves::LoadResult::Ok);
    return save;
}

// SELECT + confirm.
void writeSave(ui::App& app) {
    press(app, ui::kKeySelect);
    press(app, ui::kKeyA);
}

// Opens an entry of the save menu by tapping its row (the cursor position does not matter):
// 0 = Trainer, 1 = Bag, 2 = Backups. Rows are 22 px apart from y = 6, Boxes being the first row.
void openFromSaveMenu(ui::App& app, int entry) {
    draw(app);
    const int rows[] = {1, 2, 5};
    tap(app, 120, 6 + rows[entry] * 22 + 10);
}

// Keyboard keys by touch (see drawKeyboard): character key (row, col) and the special keys.
void tapKey(ui::App& app, int row, int col) { tap(app, 8 + col * 24 + 10, 8 + row * 28 + 10); }
void tapDelete(ui::App& app) { tap(app, 114, 134); }
void tapOk(ui::App& app) { tap(app, 210, 134); }

struct Variant {
    const char* name;
    std::vector<uint8_t> data;
};

std::vector<Variant> variants() {
    return {{"FRLG", fake::blankGen3(saves::Game::FireRedLeafGreen)},
            {"Pt", fake::blankGen4(fake::kPt)},
            {"HGSS", fake::blankGen4(fake::kHgss)},
            {"BW", fake::blankGen5(false)}};
}

}  // namespace

TEST(ui_trainer_edit_name_money_badges_and_write) {
    for (const Variant& v : variants()) {
        Fixture f = makeFixture(v.data);
        ui::App app(f.config);
        CHECK(app.openSave(f.path));
        openFromSaveMenu(app, 0);  // Trainer

        // Name: A opens the keyboard (touch the keys), OK returns to the list.
        press(app, ui::kKeyA);
        tapDelete(app);
        tapDelete(app);
        tapDelete(app);
        tapKey(app, 1, 7);  // R
        tapKey(app, 0, 4);  // E
        tapKey(app, 0, 3);  // D
        tapOk(app);

        press(app, ui::kKeyDown);   // Gender
        press(app, ui::kKeyRight);  // -> female
        press(app, ui::kKeyDown);   // Trainer ID
        press(app, ui::kKeyA);      // warning about traded Pokemon
        press(app, ui::kKeyA);      // dismiss
        press(app, ui::kKeyRight);  // +1
        press(app, ui::kKeyR);      // +1000
        press(app, ui::kKeyDown);   // Secret ID
        press(app, ui::kKeyL);      // -1000 clamps at 0
        press(app, ui::kKeyDown);   // Money
        press(app, ui::kKeyY);      // maximum
        press(app, ui::kKeyDown);   // Hours
        press(app, ui::kKeyL);      // -10 clamps at 0
        press(app, ui::kKeyRight);
        press(app, ui::kKeyDown);   // Minutes
        press(app, ui::kKeyL);      // clamps at 0
        press(app, ui::kKeyL);
        press(app, ui::kKeyDown);   // Seconds
        press(app, ui::kKeyDown);   // Badges
        press(app, ui::kKeyA);      // badge 1
        press(app, ui::kKeyRight);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyA);      // badge 3
        press(app, ui::kKeyStart);  // apply
        writeSave(app);

        std::unique_ptr<saves::SaveFile> saved = loadFile(f.path);
        CHECK(saved != nullptr);
        if (!saved) continue;
        TrainerData t = saved->trainerData();
        if (t.name != "RED") std::printf("  %s: name %s\n", v.name, t.name.c_str());
        CHECK(t.name == "RED");
        CHECK_EQ(t.gender, 1);
        CHECK_EQ(t.tid, 12346 + 1000);
        CHECK_EQ(t.sid, 54321 - 1000);
        CHECK_EQ(t.money, saved->maxMoney());
        CHECK_EQ(t.hours, 1);
        CHECK_EQ(t.minutes, 0);
        CHECK_EQ(t.badges, 0x5);
        CHECK(saved->checksumsValid());
        CHECK(f.original != saved->data());
    }
}

TEST(ui_trainer_badges_by_touch_and_hgss_kanto_badges) {
    Fixture f = makeFixture(fake::blankGen4(fake::kHgss));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 0);
    for (int i = 0; i < 8; ++i) press(app, ui::kKeyDown);  // Badges
    // 16 badges: the first and the last cell of the row. Row 8 is at y = 7 + 8 * 15.
    const int rowY = 7 + 8 * 15 + 7;
    const int cell = (140 - 8) / 16;
    tap(app, 8 + 92 + 4 + cell / 2, rowY);
    tap(app, 8 + 92 + 4 + 15 * cell + cell / 2, rowY);
    press(app, ui::kKeyStart);
    writeSave(app);
    CHECK_EQ(loadFile(f.path)->trainerData().badges, 0x8001);
}

TEST(ui_trainer_invalid_data_shows_the_error_and_changes_nothing) {
    // A save with money above the cap (as cheats leave it).
    std::vector<uint8_t> raw = fake::blankGen4(fake::kPt);
    for (int partition = 0; partition < 2; ++partition) {
        uint8_t* general = &raw[partition * saves::Gen4Save::kPartitionSize];
        util::writeU32(general + fake::kPt.trainer + 0x14, 2000000);
        fake::writeFooter(general, fake::kPt.generalSize, fake::kPt.footerSize);  // fixes the checksum
    }
    CHECK(saves::loadSaveFile(raw) != nullptr);
    Fixture f = makeFixture(raw);

    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 0);
    press(app, ui::kKeyDown);   // Gender
    press(app, ui::kKeyRight);  // an edit, so there is something to apply
    press(app, ui::kKeyStart);  // rejected: money above 999999
    press(app, ui::kKeyA);      // read the message
    press(app, ui::kKeyB);      // back: asks to discard the edit
    press(app, ui::kKeyA);      // yes
    press(app, ui::kKeySelect);
    press(app, ui::kKeyA);      // "Nothing to save"
    std::vector<uint8_t> onDisk;
    CHECK(io::readFile(f.path, onDisk));
    CHECK(onDisk == f.original);

    // The user fixes the money and then it is accepted.
    openFromSaveMenu(app, 0);
    for (int i = 0; i < 4; ++i) press(app, ui::kKeyDown);  // Money
    press(app, ui::kKeyY);
    press(app, ui::kKeyStart);
    writeSave(app);
    CHECK_EQ(loadFile(f.path)->trainerData().money, 999999u);
}

TEST(ui_trainer_discard_leaves_the_save_alone) {
    Fixture f = makeFixture(fake::blankGen5(true));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 0);
    for (int i = 0; i < 4; ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyY);      // money max
    press(app, ui::kKeyB);      // discard dialog
    press(app, ui::kKeyB);      // No: stay
    press(app, ui::kKeyB);
    press(app, ui::kKeyA);      // Yes: back to the box view
    writeSave(app);             // "Nothing to save", closed by writeSave's A
    std::vector<uint8_t> onDisk;
    CHECK(io::readFile(f.path, onDisk));
    CHECK(onDisk == f.original);
}

TEST(ui_bag_change_quantity_add_and_delete_items) {
    for (const Variant& v : variants()) {
        std::vector<uint8_t> bytes = v.data;
        std::vector<uint16_t> items;
        std::vector<uint16_t> alphabetical;
        {
            std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(bytes);
            CHECK(save != nullptr);
            if (!save) continue;
            items = save->pocketItems(0);
            CHECK(items.size() > 4);
            // The picker lists the pocket's items alphabetically (without items[0], already in the bag).
            alphabetical.assign(items.begin() + 1, items.end());
            data::sortIdsByName(alphabetical, [&](uint16_t item) { return save->itemName(item); });
            CHECK(save->setPocket(0, {{items[0], 5}, {items[1], 3}}));
            save->refreshChecksums();
            bytes = save->data();
        }
        Fixture f = makeFixture(bytes);
        ui::App app(f.config);
        CHECK(app.openSave(f.path));
        openFromSaveMenu(app, 1);  // Bag

        press(app, ui::kKeyRight);  // first item 5 -> 6
        press(app, ui::kKeyDown);
        press(app, ui::kKeyX);      // delete the second one
        press(app, ui::kKeyA);      // cursor is on "Add item": picker
        press(app, ui::kKeyDown);   // skip the first choice
        press(app, ui::kKeyA);      // add the second item in alphabetical order
        press(app, ui::kKeyA);      // +10 on the new item: 11
        press(app, ui::kKeyStart);
        writeSave(app);

        std::unique_ptr<saves::SaveFile> saved = loadFile(f.path);
        CHECK(saved != nullptr);
        if (!saved) continue;
        std::vector<BagSlot> slots = saved->readPocket(0);
        CHECK_EQ(slots.size(), 2u);
        if (slots.size() != 2) continue;
        CHECK_EQ(slots[0].item, items[0]);
        CHECK_EQ(slots[0].count, 6);
        CHECK_EQ(slots[1].item, alphabetical[1]);
        CHECK_EQ(slots[1].count, std::min(11, saved->itemMaxCount(0, alphabetical[1])));
        CHECK(saved->checksumsValid());
    }
}

TEST(ui_bag_add_to_an_empty_pocket_and_switch_pockets) {
    Fixture f = makeFixture(fake::blankGen4(fake::kPt));
    std::unique_ptr<saves::SaveFile> reference = loadFile(f.path);
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 1);
    press(app, ui::kKeyR);  // pocket 2
    press(app, ui::kKeyL);  // pocket 1
    press(app, ui::kKeyL);  // wraps to the last pocket
    press(app, ui::kKeyR);  // pocket 1 again
    press(app, ui::kKeyA);  // Add item
    press(app, ui::kKeyA);  // first choice
    press(app, ui::kKeyStart);
    writeSave(app);
    std::vector<BagSlot> slots = loadFile(f.path)->readPocket(0);
    CHECK_EQ(slots.size(), 1u);
    std::vector<uint16_t> alphabetical = reference->pocketItems(0);  // the picker is alphabetical
    data::sortIdsByName(alphabetical, [&](uint16_t item) { return reference->itemName(item); });
    CHECK_EQ(slots[0].item, alphabetical[0]);
    CHECK_EQ(slots[0].count, 1);
}

TEST(ui_bag_discard_leaves_the_save_alone) {
    Fixture f = makeFixture(fake::blankGen3(Game::Emerald));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 1);
    press(app, ui::kKeyA);  // Add item
    press(app, ui::kKeyA);  // first choice
    press(app, ui::kKeyB);  // discard dialog
    press(app, ui::kKeyA);  // yes
    writeSave(app);         // "Nothing to save"
    press(app, ui::kKeyA);
    std::vector<uint8_t> onDisk;
    CHECK(io::readFile(f.path, onDisk));
    CHECK(onDisk == f.original);
    CHECK(loadFile(f.path)->readPocket(0).empty());
}

TEST(ui_bag_out_of_limit_stacks_are_shown_and_rejected_with_a_reason) {
    // Build a save whose pocket holds more of an item than the game allows.
    std::vector<uint8_t> clean = fake::blankGen4(fake::kPt);
    int pocket = -1;
    uint16_t item = 0;
    {
        std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(clean);
        CHECK(save != nullptr);
        if (!save) return;
        for (int p = 0; p < save->pocketCount() && pocket < 0; ++p) {
            for (uint16_t candidate : save->pocketItems(p)) {
                if (save->itemMaxCount(p, candidate) == 99) {
                    pocket = p;
                    item = candidate;
                    break;
                }
            }
        }
        CHECK(pocket >= 0);
        if (pocket < 0) return;
        CHECK(save->setPocket(pocket, {{item, 5}}));
        save->refreshChecksums();
        clean = save->data();
    }
    std::vector<uint8_t> cheated = clean;
    int patched = 0;
    for (int partition = 0; partition < 2; ++partition) {
        uint8_t* general = &cheated[partition * saves::Gen4Save::kPartitionSize];
        for (size_t i = 0; i + 4 <= fake::kPt.generalSize; ++i) {
            if (util::readU16(general + i) == item && util::readU16(general + i + 2) == 5) {
                util::writeU16(general + i + 2, 999);
                ++patched;
            }
        }
        fake::writeFooter(general, fake::kPt.generalSize, fake::kPt.footerSize);
    }
    CHECK(patched >= 1);
    Fixture f = makeFixture(cheated);
    std::vector<BagSlot> shown = loadFile(f.path)->readPocket(pocket);
    CHECK_EQ(shown.size(), 1u);
    if (shown.size() != 1) return;
    CHECK_EQ(shown[0].count, 999);

    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 1);
    for (int i = 0; i < pocket; ++i) press(app, ui::kKeyR);
    // The odd stack is shown as it is; adding an item makes the pocket invalid on Apply.
    press(app, ui::kKeyDown);   // Add item
    press(app, ui::kKeyA);
    press(app, ui::kKeyA);
    press(app, ui::kKeyStart);  // rejected with a reason
    press(app, ui::kKeyA);      // read it
    press(app, ui::kKeyB);      // discard dialog
    press(app, ui::kKeyA);
    writeSave(app);             // "Nothing to save", closed by writeSave's A
    std::vector<uint8_t> onDisk;
    CHECK(io::readFile(f.path, onDisk));
    CHECK(onDisk == f.original);

    // Reducing the stack (any Left/Right press goes to the limit) makes it valid.
    openFromSaveMenu(app, 1);
    for (int i = 0; i < pocket; ++i) press(app, ui::kKeyR);
    press(app, ui::kKeyLeft);
    press(app, ui::kKeyStart);
    writeSave(app);
    std::unique_ptr<saves::SaveFile> saved = loadFile(f.path);
    std::vector<BagSlot> after = saved->readPocket(pocket);
    CHECK_EQ(after.size(), 1u);
    if (!after.empty()) CHECK_EQ(after[0].count, 99);
}

TEST(ui_backups_screen_offers_a_manual_backup) {
    Fixture f = makeFixture(fake::blankGen5(false));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 2);  // Backups
    press(app, ui::kKeyX);     // Back up now
    press(app, ui::kKeyA);     // close the message
    CHECK(!io::listBackups(f.path, io::joinPath(f.config.dataDir, "backups")).empty());
}

// ----- Save menu ----------------------------------------------------------------

namespace {

std::string where(const ui::App& app) { return app.screenName(); }

// Save menu rows: Boxes, Trainer, Bag, Bank, Save to SD, Backups, Close save.
void tapSaveMenuRow(ui::App& app, int row) {
    draw(app);
    tap(app, 120, 6 + row * 22 + 10);
}

}  // namespace

TEST(ui_opening_a_save_shows_the_save_menu) {
    for (const Variant& v : variants()) {
        Fixture f = makeFixture(v.data);
        ui::App app(f.config);
        CHECK(app.openSave(f.path));
        CHECK(where(app) == "SaveMenu");
        draw(app);
        press(app, ui::kKeyX);  // X does nothing on the menu
        CHECK(where(app) == "SaveMenu");
    }
}

TEST(ui_save_menu_opens_each_screen_and_b_comes_back) {
    for (const Variant& v : variants()) {
        Fixture f = makeFixture(v.data);
        ui::App app(f.config);
        CHECK(app.openSave(f.path));

        press(app, ui::kKeyA);  // Boxes
        CHECK(where(app) == "Box");
        press(app, ui::kKeyX);  // X is not used in the box view any more
        CHECK(where(app) == "Box");
        press(app, ui::kKeyB);
        CHECK(where(app) == "SaveMenu");

        press(app, ui::kKeyDown);  // Trainer
        press(app, ui::kKeyA);
        CHECK(where(app) == "Trainer");
        press(app, ui::kKeyB);
        CHECK(where(app) == "SaveMenu");

        press(app, ui::kKeyDown);  // Bag
        press(app, ui::kKeyA);
        CHECK(where(app) == "Bag");
        press(app, ui::kKeyB);
        CHECK(where(app) == "SaveMenu");

        press(app, ui::kKeyDown);  // Bank
        press(app, ui::kKeyA);
        CHECK(where(app) == "Bank");
        press(app, ui::kKeyB);
        CHECK(where(app) == "SaveMenu");

        press(app, ui::kKeyDown);  // Save to SD
        press(app, ui::kKeyDown);  // Backups
        press(app, ui::kKeyA);
        CHECK(where(app) == "Backups");
        press(app, ui::kKeyB);
        CHECK(where(app) == "SaveMenu");

        // Touch works on the rows as well.
        tapSaveMenuRow(app, 2);
        CHECK(where(app) == "Bag");
        press(app, ui::kKeyB);
        tapSaveMenuRow(app, 0);
        CHECK(where(app) == "Box");
        press(app, ui::kKeyY);  // the box view's own Bank shortcut still returns to the box view
        CHECK(where(app) == "Bank");
        press(app, ui::kKeyB);
        CHECK(where(app) == "Box");
    }
}

TEST(ui_save_menu_close_asks_only_with_unsaved_changes) {
    Fixture f = makeFixture(fake::blankGen4(fake::kPt));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    press(app, ui::kKeyB);  // nothing changed: straight to the browser
    CHECK(where(app) == "Browser");

    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 0);  // Trainer: change the gender and apply
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);
    press(app, ui::kKeyStart);
    CHECK(where(app) == "SaveMenu");
    press(app, ui::kKeyB);  // asks to discard
    CHECK(where(app) == "SaveMenu");
    press(app, ui::kKeyB);  // No
    CHECK(where(app) == "SaveMenu");
    tapSaveMenuRow(app, 6);  // Close save: asks as well
    press(app, ui::kKeyA);   // Yes
    CHECK(where(app) == "Browser");
    std::vector<uint8_t> onDisk;
    CHECK(io::readFile(f.path, onDisk));
    CHECK(onDisk == f.original);
}

TEST(ui_save_menu_save_to_sd_uses_the_confirmed_write) {
    Fixture f = makeFixture(fake::blankGen5(false));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));

    tapSaveMenuRow(app, 4);  // Save to SD without changes
    press(app, ui::kKeyA);   // "Nothing to save"
    CHECK(where(app) == "SaveMenu");
    std::vector<uint8_t> onDisk;
    CHECK(io::readFile(f.path, onDisk));
    CHECK(onDisk == f.original);

    openFromSaveMenu(app, 0);  // Trainer: change the gender and apply
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);
    press(app, ui::kKeyStart);
    CHECK(where(app) == "SaveMenu");

    tapSaveMenuRow(app, 4);  // confirm dialog
    press(app, ui::kKeyB);   // No: nothing is written
    CHECK(io::readFile(f.path, onDisk));
    CHECK(onDisk == f.original);
    press(app, ui::kKeySelect);  // SELECT works on the menu too
    press(app, ui::kKeyA);       // Yes: committed with a backup
    press(app, ui::kKeyA);       // close "Saved"
    CHECK(where(app) == "SaveMenu");
    CHECK(loadFile(f.path)->trainerData().gender != saves::loadSaveFile(f.original)->trainerData().gender);
    CHECK(!io::listBackups(f.path, io::joinPath(f.config.dataDir, "backups")).empty());
    press(app, ui::kKeyB);  // saved: closing does not ask
    CHECK(where(app) == "Browser");
}

TEST(ui_box_view_b_goes_back_to_the_save_menu_and_keeps_select_save) {
    Fixture f = makeFixture(fake::blankGen4(fake::kPt));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    press(app, ui::kKeyA);       // Boxes
    press(app, ui::kKeySelect);  // nothing to save
    press(app, ui::kKeyA);
    CHECK(where(app) == "Box");
    press(app, ui::kKeyB);
    CHECK(where(app) == "SaveMenu");
}

TEST(ui_trainer_and_bag_discard_dialog_returns_to_the_save_menu) {
    Fixture f = makeFixture(fake::blankGen4(fake::kPt));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 1);  // Bag
    press(app, ui::kKeyA);     // Add item
    press(app, ui::kKeyA);
    press(app, ui::kKeyB);     // discard dialog
    press(app, ui::kKeyA);     // yes
    CHECK(where(app) == "SaveMenu");
}

TEST(ui_bag_shows_but_does_not_reject_unusual_items) {
    // A key item in the Items pocket and an item twice are the player's business.
    Fixture f = makeFixture(fake::blankGen4(fake::kPt));
    {
        std::unique_ptr<saves::SaveFile> save = loadFile(f.path);
        const uint16_t key = save->pocketItems(1)[0];
        const uint16_t item = save->pocketItems(0)[0];
        CHECK(save->setPocket(0, {{key, 1}, {item, 3}, {item, 4}}));
        save->refreshChecksums();
        CHECK(io::safeWriteFile(f.path, save->data()) == io::WriteResult::Ok);
    }
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openFromSaveMenu(app, 1);
    press(app, ui::kKeyRight);  // key item 1 -> clamps at its pocket limit
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);  // 3 -> 4
    press(app, ui::kKeyStart);
    CHECK(where(app) == "SaveMenu");  // applied without an error dialog
    writeSave(app);
    std::vector<BagSlot> slots = loadFile(f.path)->readPocket(0);
    CHECK_EQ(slots.size(), 3u);
    if (slots.size() == 3) {
        CHECK_EQ(slots[1].count, 4);
        CHECK_EQ(slots[2].count, 4);
    }
}
