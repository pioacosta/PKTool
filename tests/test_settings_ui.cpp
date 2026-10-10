// Tests of the settings file and screen, and of the male/female signs
// (fonts, text encoders, on-screen keyboard).

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/data/names.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "core/io/settings.h"
#include "core/saves/save_file.h"
#include "core/text/gen4_text.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;

namespace {

std::vector<uint16_t> g_top(gfx::kScreenWidth * gfx::kScreenHeight);
std::vector<uint16_t> g_bottom(gfx::kScreenWidth * gfx::kScreenHeight);

void press(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);
}

std::string makeDir() {
    char pattern[] = "/tmp/pktool-settings-XXXXXX";
    return mkdtemp(pattern);
}

ui::Config makeConfig(const std::string& dir) {
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    return config;
}

std::string readText(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!io::readFile(path, bytes)) return std::string();
    return std::string(bytes.begin(), bytes.end());
}

// Home -> Settings tile (bottom right).
void openSettings(ui::App& app) {
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);
    press(app, ui::kKeyA);
}

// Draws a string with each font and returns the number of lit pixels.
int litPixels(const gfx::Font& font, const std::string& text) {
    std::vector<uint16_t> pixels(gfx::kScreenWidth * gfx::kScreenHeight, 0);
    gfx::Canvas canvas(pixels.data());
    canvas.text(font, 0, 0, text, gfx::rgb(255, 255, 255));
    int count = 0;
    for (uint16_t p : pixels) count += p != 0 ? 1 : 0;
    return count;
}

}  // namespace

TEST(settings_text_round_trip_and_forgiving_parser) {
    io::Settings s;
    CHECK(!s.sortById);
    CHECK_EQ(s.newLevel, 50);
    s.sortById = true;
    s.newLevel = 7;
    io::Settings back;
    io::parseSettings(io::settingsToText(s), back);
    CHECK(back == s);

    io::Settings messy;
    io::parseSettings("junk\r\n# comment\r\nSORT_ORDER = ID \r\nnew_level=0\r\nnew_level=101\r\nnew_level=abc\r\nnew_level=\r\nextra=1", messy);
    CHECK(messy.sortById);
    CHECK_EQ(messy.newLevel, 50);  // every bad value was ignored
    io::parseSettings("new_level=100\nsort_order=alphabetical", messy);
    CHECK(!messy.sortById);
    CHECK_EQ(messy.newLevel, 100);

    io::Settings none;
    CHECK(!io::loadSettings("/tmp/pktool-no-such-settings.ini", none));
    CHECK(none == io::Settings());
}

TEST(ui_settings_screen_persists_and_reloads) {
    std::string dir = makeDir();
    ui::Config config = makeConfig(dir);
    std::string file = io::joinPath(config.dataDir, "settings.ini");
    CHECK(!io::fileExists(file));
    {
        ui::App app(config);
        CHECK(std::string(app.screenName()) == "Home");
        openSettings(app);
        CHECK(std::string(app.screenName()) == "Settings");

        CHECK(data::currentOrder() == data::Order::Alphabetical);
        press(app, ui::kKeyRight);  // Option lists: by ID
        CHECK(data::currentOrder() == data::Order::ById);
        CHECK_EQ(data::sortedIds(data::NameKind::Species, 4)[0], 1);
        CHECK_EQ(data::sortedIds(data::NameKind::Species, 4)[24], 25);
        press(app, ui::kKeyDown);   // New Pokemon level
        press(app, ui::kKeyRight);  // 51
        press(app, ui::kKeyL);      // 41
        press(app, ui::kKeyL);
        press(app, ui::kKeyL);
        press(app, ui::kKeyL);
        press(app, ui::kKeyL);      // 1 (clamped)
        press(app, ui::kKeyL);
        press(app, ui::kKeyR);      // 11
        CHECK(!io::fileExists(file));  // nothing is written until B
        press(app, ui::kKeyB);
        CHECK(std::string(app.screenName()) == "Home");
        CHECK(io::fileExists(file));
        CHECK(readText(file) == "# PKTool settings\nsort_order=id\nnew_level=11\nanimations=on\n");
    }
    {
        // A new session reads the file back and applies it.
        data::setOrder(data::Order::Alphabetical);
        ui::App app(config);
        CHECK(data::currentOrder() == data::Order::ById);
        openSettings(app);
        press(app, ui::kKeyA);  // toggles back to by name
        CHECK(data::currentOrder() == data::Order::Alphabetical);
        press(app, ui::kKeyB);
        CHECK(readText(file) == "# PKTool settings\nsort_order=alphabetical\nnew_level=11\nanimations=on\n");
    }
    data::setOrder(data::Order::Alphabetical);
}

TEST(ui_settings_touch_and_about) {
    std::string dir = makeDir();
    ui::App app(makeConfig(dir));
    // Tap the Settings tile (bottom right of the Home screen); touch zones exist after a draw.
    press(app, 0);
    ui::Input in;
    in.touched = true;
    in.touchX = 190;
    in.touchY = 130;
    app.update(in);
    app.update(ui::Input());
    CHECK(std::string(app.screenName()) == "Settings");
    // Tap the right arrow of the level row: row 1 starts at y = 8 + 19.
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);
    in.touchX = 230;
    in.touchY = 8 + 19 + 8;
    app.update(in);
    app.update(ui::Input());
    app.draw(top, bottom);
    press(app, ui::kKeyB);
    CHECK_EQ(readText(io::joinPath(dir, "PKTOOL/settings.ini")), "# PKTool settings\nsort_order=alphabetical\nnew_level=51\nanimations=on\n");

    press(app, ui::kKeyA);  // the Home cursor is still on the Settings tile
    CHECK(std::string(app.screenName()) == "Settings");
    press(app, ui::kKeyUp);  // About (wraps)
    press(app, ui::kKeyA);   // opens the box
    press(app, ui::kKeyA);   // closes it
    CHECK(std::string(app.screenName()) == "Settings");
    press(app, ui::kKeyB);
    CHECK(std::string(app.screenName()) == "Home");
}

TEST(ui_settings_unchanged_writes_nothing_and_new_level_applies) {
    std::string dir = makeDir();
    ui::Config config = makeConfig(dir);
    std::string file = io::joinPath(config.dataDir, "settings.ini");
    {
        ui::App app(config);
        openSettings(app);
        press(app, ui::kKeyB);
        CHECK(!io::fileExists(file));
    }
    // New Pokemon level 20 (Treecko-like low species are not raised).
    CHECK(io::makeDirectories(config.dataDir));
    std::string text = "new_level=20\n";
    CHECK(io::safeWriteFile(file, std::vector<uint8_t>(text.begin(), text.end())) == io::WriteResult::Ok);
    std::string savePath = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(savePath, fake::blankGen4(fake::kPt)) == io::WriteResult::Ok);
    ui::App app(config);
    CHECK(app.openSave(savePath));
    press(app, ui::kKeyA);  // Boxes
    press(app, ui::kKeyR);
    press(app, ui::kKeyA);  // empty slot: add menu
    press(app, ui::kKeyA);  // New Pokemon: species list
    press(app, ui::kKeyA);  // first species
    CHECK_EQ(app.editedPokemon().level, 20);
}

TEST(ui_species_picker_by_id_follows_the_setting) {
    std::string dir = makeDir();
    ui::Config config = makeConfig(dir);
    std::string text = "sort_order=id\n";
    CHECK(io::makeDirectories(config.dataDir));
    CHECK(io::safeWriteFile(io::joinPath(config.dataDir, "settings.ini"), std::vector<uint8_t>(text.begin(), text.end())) ==
          io::WriteResult::Ok);
    std::string savePath = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(savePath, fake::blankGen4(fake::kPt)) == io::WriteResult::Ok);
    {
        ui::App app(config);
        CHECK(app.openSave(savePath));
        press(app, ui::kKeyA);
        press(app, ui::kKeyR);
        press(app, ui::kKeyA);
        press(app, ui::kKeyA);
        const std::vector<std::string>& rows = app.pickerRows();
        CHECK(rows[0].substr(0, 3) == "001");
        CHECK(rows[1].substr(0, 3) == "002");
        press(app, ui::kKeyR);  // no letters: a page of 10
        CHECK_EQ(app.pickerCursor(), 10);
    }
    data::setOrder(data::Order::Alphabetical);
}

TEST(ui_species_picker_y_toggles_the_order_and_keeps_the_species) {
    std::string dir = makeDir();
    ui::Config config = makeConfig(dir);
    std::string savePath = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(savePath, fake::blankGen4(fake::kPt)) == io::WriteResult::Ok);
    {
        ui::App app(config);
        CHECK(app.openSave(savePath));
        press(app, ui::kKeyA);
        press(app, ui::kKeyR);
        press(app, ui::kKeyA);
        press(app, ui::kKeyA);  // species list, alphabetical
        CHECK(app.pickerRows()[0].substr(0, 3) != "001");
        press(app, ui::kKeyDown);
        const std::string picked = app.pickerRows()[static_cast<size_t>(app.pickerCursor())];
        press(app, ui::kKeyY);  // Dex order
        CHECK(app.pickerRows()[0].substr(0, 3) == "001");
        CHECK(app.pickerRows()[static_cast<size_t>(app.pickerCursor())] == picked);
        press(app, ui::kKeyY);  // back to A-Z
        CHECK(app.pickerRows()[0].substr(0, 3) != "001");
        CHECK(app.pickerRows()[static_cast<size_t>(app.pickerCursor())] == picked);
        press(app, ui::kKeyY);  // Dex order again: saved
    }
    std::vector<uint8_t> bytes;
    CHECK(io::readFile(io::joinPath(config.dataDir, "settings.ini"), bytes));
    CHECK(std::string(bytes.begin(), bytes.end()).find("sort_order=id") != std::string::npos);
    data::setOrder(data::Order::Alphabetical);
}

TEST(sorted_by_id_views) {
    data::setOrder(data::Order::ById);
    for (uint8_t format : {3, 4, 5}) {
        for (data::NameKind kind : {data::NameKind::Species, data::NameKind::Move, data::NameKind::Item,
                                    data::NameKind::Nature, data::NameKind::Ball}) {
            const std::vector<uint16_t>& ids = data::sortedIds(kind, format);
            CHECK(!ids.empty());
            for (size_t i = 1; i < ids.size(); ++i) CHECK(ids[i - 1] < ids[i]);
            CHECK_EQ(data::stepSorted(kind, format, ids.back(), 1), ids.front());
            CHECK_EQ(data::stepSorted(kind, format, ids.front(), -1), ids.back());
        }
        const std::vector<uint16_t>& species = data::sortedIds(data::NameKind::Species, format);
        CHECK_EQ(data::jumpSorted(data::NameKind::Species, format, 1, 1), species[10]);
        CHECK_EQ(data::jumpSorted(data::NameKind::Species, format, species[10], -1), 1);
    }
    data::setOrder(data::Order::Alphabetical);
    CHECK(std::string(data::speciesName(data::sortedIds(data::NameKind::Species, 4)[0])) == "Abomasnow");
}

// ----- Male and female signs ------------------------------------------------

TEST(fonts_have_male_and_female_glyphs) {
    for (const gfx::Font* font : {&gfx::kFontSmall, &gfx::kFontLarge}) {
        CHECK(gfx::Canvas::hasGlyph(*font, 0x2642));
        CHECK(gfx::Canvas::hasGlyph(*font, 0x2640));
        CHECK(!gfx::Canvas::hasGlyph(*font, 0x4E00));
        int male = litPixels(*font, "\xE2\x99\x82");
        int female = litPixels(*font, "\xE2\x99\x80");
        CHECK(male >= 12);
        CHECK(female >= 12);
        CHECK(male != litPixels(*font, "M"));  // no longer drawn as letters
        CHECK(female != litPixels(*font, "F"));
        CHECK(male != litPixels(*font, "?"));
        CHECK(gfx::Canvas::textWidth(*font, "\xE2\x99\x82") > 4);
        CHECK(gfx::Canvas::textWidth(*font, "Nidoran\xE2\x99\x80") > gfx::Canvas::textWidth(*font, "Nidoran"));
        // Both signs differ from each other.
        CHECK(litPixels(*font, "\xE2\x99\x82") != litPixels(*font, "\xE2\x99\x80") ||
              gfx::Canvas::textWidth(*font, "\xE2\x99\x82") != gfx::Canvas::textWidth(*font, "\xE2\x99\x80"));
    }
}

TEST(gender_signs_survive_every_text_encoding) {
    const std::string name = "Nidoran\xE2\x99\x82";
    for (uint8_t language : {2}) {
        uint8_t buffer[12] = {};
        text::encodeGen3(name, buffer, 11, 10, language);
        CHECK(text::decodeGen3(buffer, 11, language) == name);
        text::encodeGen3("\xE2\x99\x80", buffer, 11, 10, language);
        CHECK(text::decodeGen3(buffer, 11, language) == "\xE2\x99\x80");
    }
    uint8_t wide[24] = {};
    text::encodeGen4(name, wide, 12, 10);
    CHECK(text::decodeGen4(wide, 12) == name);
    text::encodeGen5(name, wide, 12, 10);
    CHECK(text::decodeGen5(wide, 12) == name);
    // The species names of the data tables carry the signs too.
    CHECK(std::string(data::speciesName(29)) == "Nidoran\xE2\x99\x80");
    CHECK(std::string(data::speciesName(32)) == "Nidoran\xE2\x99\x82");
}

TEST(keyboard_symbols_page_types_gender_signs) {
    struct Variant {
        const char* name;
        std::vector<uint8_t> data;
    };
    std::vector<Variant> variants = {{"FRLG", fake::blankGen3(saves::Game::FireRedLeafGreen)},
                                     {"Pt", fake::blankGen4(fake::kPt)},
                                     {"BW", fake::blankGen5(false)}};
    for (const Variant& v : variants) {
        std::string dir = makeDir();
        std::string path = io::joinPath(dir, "test.sav");
        CHECK(io::safeWriteFile(path, v.data) == io::WriteResult::Ok);
        ui::App app(makeConfig(dir));
        CHECK(app.openSave(path));
        press(app, ui::kKeyDown);
        press(app, ui::kKeyA);  // Trainer
        press(app, ui::kKeyA);  // name: keyboard
        CHECK(std::string(app.screenName()) == "Keyboard");
        press(app, ui::kKeySelect);  // lower case
        press(app, ui::kKeySelect);  // symbols
        press(app, ui::kKeyA);       // first key: male sign
        press(app, ui::kKeyRight);
        press(app, ui::kKeyA);       // female sign
        press(app, ui::kKeyStart);   // done
        for (int i = 0; i < 4; ++i) press(app, ui::kKeyDown);  // Money (the blank Gen III fixture holds an invalid amount)
        press(app, ui::kKeyX);                                  // zero
        press(app, ui::kKeyStart);   // apply
        press(app, ui::kKeySelect);  // write the save
        press(app, ui::kKeyA);
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        saves::LoadResult result;
        std::unique_ptr<saves::SaveFile> saved = saves::loadSaveFile(bytes, &result);
        CHECK(result == saves::LoadResult::Ok);
        std::string name = saved->trainerData().name;
        CHECK(name.size() >= 6);
        if (name.size() >= 6) {
            CHECK(name.substr(name.size() - 6) == "\xE2\x99\x82\xE2\x99\x80");
        }
    }
}
