// Alphabetical option lists: the sorted views (core/data/sorted_names) and the
// editor and picker behaviour built on them (driven through ui::App).

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "core/data/gen3.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
using data::NameKind;

namespace {

std::vector<uint16_t> g_top(gfx::kScreenWidth * gfx::kScreenHeight);
std::vector<uint16_t> g_bottom(gfx::kScreenWidth * gfx::kScreenHeight);

void press(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());  // runs pending actions
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);
}

std::string key(NameKind kind, uint8_t format, uint16_t id) {
    return data::foldName(data::nameOf(kind, format, id));
}

bool isSorted(NameKind kind, uint8_t format) {
    const std::vector<uint16_t>& ids = data::sortedIds(kind, format);
    size_t first = data::allowsNone(kind) ? 1 : 0;
    if (data::allowsNone(kind) && (ids.empty() || ids[0] != 0)) return false;
    for (size_t i = first + 1; i < ids.size(); ++i) {
        if (key(kind, format, ids[i - 1]) > key(kind, format, ids[i])) return false;
    }
    return true;
}

// The set of IDs a list must contain, built independently of the sorting code.
std::set<uint16_t> expectedIds(NameKind kind, uint8_t format) {
    std::set<uint16_t> ids;
    switch (kind) {
        case NameKind::Species:
            for (int s = 1; s <= (format >= 5 ? 649 : format == 3 ? 386 : 493); ++s) ids.insert(static_cast<uint16_t>(s));
            break;
        case NameKind::Move:
            for (int m = 0; m <= (format >= 5 ? 559 : format == 3 ? 354 : 467); ++m) ids.insert(static_cast<uint16_t>(m));
            break;
        case NameKind::Item:
            ids.insert(0);
            for (int i = 1; i <= (format >= 5 ? 638 : 536); ++i) {
                if (std::string(data::itemName(static_cast<uint16_t>(i))) == "???") continue;
                if (format == 3 && data::itemToGen3(static_cast<uint16_t>(i)) == 0) continue;
                ids.insert(static_cast<uint16_t>(i));
            }
            break;
        case NameKind::Nature:
            for (int n = 0; n < 25; ++n) ids.insert(static_cast<uint16_t>(n));
            break;
        case NameKind::Ball:
            for (int b = 1; b <= (format >= 5 ? 25 : format == 3 ? 12 : 24); ++b) ids.insert(static_cast<uint16_t>(b));
            break;
    }
    return ids;
}

struct Variant {
    const char* name;
    uint8_t format;
    std::vector<uint8_t> data;
};

std::vector<Variant> variants() {
    return {{"FRLG", 3, fake::blankGen3(saves::Game::FireRedLeafGreen)},
            {"Pt", 4, fake::blankGen4(fake::kPt)},
            {"BW", 5, fake::blankGen5(false)}};
}

// Opens the save, goes to an empty box slot and creates a Pokemon through
// Add > New Pokemon (the species at `row` of the picker). Leaves the editor open.
void openEditor(ui::App& app, const std::string& path, int row) {
    CHECK(app.openSave(path));
    press(app, ui::kKeyA);  // save menu: Boxes
    press(app, ui::kKeyR);  // party -> box 1
    press(app, ui::kKeyA);  // empty slot: Add menu
    press(app, ui::kKeyA);  // New Pokemon: species picker
    for (int i = 0; i < row; ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // create
    press(app, ui::kKeyA);  // close the report
}

void goToField(ui::App& app, int from, int to) {
    for (int i = from; i < to; ++i) press(app, ui::kKeyDown);
}

constexpr int kNature = 3, kItem = 7, kBall = 8, kMove1 = 9;

}  // namespace

TEST(sorted_views_are_sorted_and_hold_exactly_the_valid_ids) {
    const NameKind kinds[] = {NameKind::Species, NameKind::Move, NameKind::Item, NameKind::Nature, NameKind::Ball};
    for (uint8_t format : {3, 4, 5}) {
        for (NameKind kind : kinds) {
            CHECK(isSorted(kind, format));
            const std::vector<uint16_t>& ids = data::sortedIds(kind, format);
            std::set<uint16_t> unique(ids.begin(), ids.end());
            CHECK_EQ(unique.size(), ids.size());
            CHECK(unique == expectedIds(kind, format));
        }
        // "(None)" comes first where 0 is allowed.
        CHECK_EQ(data::sortedIds(NameKind::Move, format)[0], 0);
        CHECK_EQ(data::sortedIds(NameKind::Item, format)[0], 0);
    }
}

TEST(sorted_names_ignore_case_accents_and_punctuation) {
    CHECK(data::foldName("Pok\xC3\xA9mon Box Link") == "pokemon box link");
    CHECK(data::foldName("  \"Zap\" Cannon!") == "zap cannon");
    CHECK_EQ(data::initialOf("\xC3\x89" "clair"), 'e');
    CHECK_EQ(data::initialOf("(None)"), 'n');
    // Gen III: only real species and items of the game.
    for (uint16_t item : data::sortedIds(NameKind::Item, 3)) {
        CHECK(item == 0 || data::itemToGen3(item) != 0);
    }
    CHECK(std::string(data::speciesName(data::sortedIds(NameKind::Species, 3)[0])) == "Abra");
    CHECK(std::string(data::speciesName(data::sortedIds(NameKind::Species, 4)[0])) == "Abomasnow");
}

TEST(sorted_step_and_letter_jump) {
    for (uint8_t format : {3, 4, 5}) {
        const std::vector<uint16_t>& ids = data::sortedIds(NameKind::Move, format);
        // Stepping wraps around both ways.
        CHECK_EQ(data::stepSorted(NameKind::Move, format, ids.back(), 1), ids.front());
        CHECK_EQ(data::stepSorted(NameKind::Move, format, ids.front(), -1), ids.back());
        CHECK_EQ(data::stepSorted(NameKind::Move, format, ids[1], 1), ids[2]);
        // None is its own group: R goes to the first real move (an A move).
        uint16_t first = data::jumpSorted(NameKind::Move, format, 0, 1);
        CHECK_EQ(first, ids[1]);
        CHECK_EQ(data::initialOf(data::moveName(first)), 'a');
        // R then lands on the first move of the next initial letter.
        uint16_t next = data::jumpSorted(NameKind::Move, format, first, 1);
        CHECK(data::initialOf(data::moveName(next)) > 'a');
        CHECK(data::initialOf(data::moveName(data::stepSorted(NameKind::Move, format, next, -1))) == 'a');
        // L goes back to the first of the previous letter, and wraps to the last letter.
        CHECK_EQ(data::jumpSorted(NameKind::Move, format, next, -1), first);
        uint16_t last = data::jumpSorted(NameKind::Move, format, 0, -1);
        CHECK(data::initialOf(data::moveName(last)) >= 'w');
        CHECK_EQ(data::jumpSorted(NameKind::Move, format, ids.back(), 1), 0);
        // An ID outside the list enters at an end.
        CHECK_EQ(data::stepSorted(NameKind::Ball, format, 0, 1), data::sortedIds(NameKind::Ball, format).front());
    }
}

TEST(ui_species_picker_is_alphabetical) {
    for (const Variant& v : variants()) {
        char pattern[] = "/tmp/pktool-sort-XXXXXX";
        std::string dir = mkdtemp(pattern);
        ui::Config config;
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        std::string path = io::joinPath(dir, "test.sav");
        CHECK(io::safeWriteFile(path, v.data) == io::WriteResult::Ok);
        ui::App app(config);
        CHECK(app.openSave(path));
        press(app, ui::kKeyA);  // save menu: Boxes
        press(app, ui::kKeyR);
        press(app, ui::kKeyA);
        press(app, ui::kKeyA);  // species picker

        const std::vector<std::string>& rows = app.pickerRows();
        CHECK_EQ(rows.size(), data::sortedIds(NameKind::Species, v.format).size());
        std::string previous;
        for (const std::string& row : rows) {  // "025  Pikachu"
            std::string name = data::foldName(row.c_str() + 5);
            CHECK(previous <= name);
            previous = name;
        }
        CHECK(rows[0].substr(5) == data::speciesName(data::sortedIds(NameKind::Species, v.format)[0]));
        // R jumps to the first species of the next initial letter.
        press(app, ui::kKeyR);
        CHECK(app.pickerCursor() > 0);
        CHECK(data::initialOf(rows[static_cast<size_t>(app.pickerCursor())].c_str() + 5) > 'a');
        CHECK_EQ(data::initialOf(rows[static_cast<size_t>(app.pickerCursor() - 1)].c_str() + 5), 'a');
        press(app, ui::kKeyL);
        CHECK_EQ(app.pickerCursor(), 0);

        // Creating the selected row makes that species.
        press(app, ui::kKeyDown);
        std::string second = rows[1].substr(5);
        press(app, ui::kKeyA);
        CHECK(std::string(data::speciesName(app.editedPokemon().species)) == second);
    }
}

TEST(ui_editor_name_fields_step_alphabetically) {
    for (const Variant& v : variants()) {
        char pattern[] = "/tmp/pktool-sort-XXXXXX";
        std::string dir = mkdtemp(pattern);
        ui::Config config;
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        std::string path = io::joinPath(dir, "test.sav");
        CHECK(io::safeWriteFile(path, v.data) == io::WriteResult::Ok);
        ui::App app(config);
        openEditor(app, path, 0);  // Abra
        const uint8_t format = v.format;
        press(app, ui::kKeyDown);  // Species field
        CHECK_EQ(app.editedPokemon().species, data::sortedIds(NameKind::Species, format)[0]);

        // Species: Right = alphabetical successor, Left goes back, L/R jump by letter.
        press(app, ui::kKeyRight);
        CHECK_EQ(app.editedPokemon().species, data::sortedIds(NameKind::Species, format)[1]);
        press(app, ui::kKeyLeft);
        CHECK_EQ(app.editedPokemon().species, data::sortedIds(NameKind::Species, format)[0]);
        press(app, ui::kKeyL);  // previous letter wraps to the last one
        CHECK(data::initialOf(data::speciesName(app.editedPokemon().species)) >= 'w');
        press(app, ui::kKeyR);  // and the next letter wraps to 'a'
        CHECK_EQ(app.editedPokemon().species, data::sortedIds(NameKind::Species, format)[0]);

        // Nature.
        goToField(app, 1, kNature);
        for (int step = 0; step < 3; ++step) {
            uint8_t before = app.editedPokemon().nature;
            press(app, ui::kKeyRight);
            uint8_t after = app.editedPokemon().nature;
            CHECK_EQ(after, data::stepSorted(NameKind::Nature, format, before, 1));
        }

        // Held item: None -> first item alphabetically; L/R by letter.
        goToField(app, kNature, kItem);
        CHECK_EQ(app.editedPokemon().heldItem, 0);
        press(app, ui::kKeyRight);
        const std::vector<uint16_t>& items = data::sortedIds(NameKind::Item, format);
        CHECK_EQ(app.editedPokemon().heldItem, items[1]);
        press(app, ui::kKeyRight);
        CHECK_EQ(app.editedPokemon().heldItem, items[2]);
        press(app, ui::kKeyR);
        uint16_t jumped = app.editedPokemon().heldItem;
        CHECK(data::initialOf(data::itemName(jumped)) > data::initialOf(data::itemName(items[1])));
        CHECK(data::initialOf(data::itemName(data::stepSorted(NameKind::Item, format, jumped, -1))) <
              data::initialOf(data::itemName(jumped)));
        press(app, ui::kKeyL);
        CHECK_EQ(app.editedPokemon().heldItem, items[1]);
        press(app, ui::kKeyL);  // None is a group of its own
        CHECK_EQ(app.editedPokemon().heldItem, 0);
        press(app, ui::kKeyLeft);  // wraps to the last item
        CHECK_EQ(app.editedPokemon().heldItem, items.back());
        if (format == 3) CHECK(data::itemToGen3(app.editedPokemon().heldItem) != 0);

        // Ball: only balls of the generation, alphabetical.
        goToField(app, kItem, kBall);
        const std::vector<uint16_t>& balls = data::sortedIds(NameKind::Ball, format);
        for (size_t i = 0; i < balls.size() + 1; ++i) {
            uint8_t before = app.editedPokemon().met.ball;
            press(app, ui::kKeyRight);
            CHECK_EQ(app.editedPokemon().met.ball, data::stepSorted(NameKind::Ball, format, before, 1));
            CHECK(std::find(balls.begin(), balls.end(), app.editedPokemon().met.ball) != balls.end());
        }

        // Moves: each slot steps to the alphabetical successor, skipping moves
        // the Pokemon already knows.
        goToField(app, kBall, kMove1);
        for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);  // Move 4
        const pokemon::Pokemon& p = app.editedPokemon();
        const std::vector<uint16_t>& moves = data::sortedIds(NameKind::Move, format);
        uint16_t before = p.moves[3];
        press(app, ui::kKeyRight);
        uint16_t after = p.moves[3];
        size_t at = static_cast<size_t>(data::sortedIndex(NameKind::Move, format, before));
        size_t want = (at + 1) % moves.size();
        while (std::find(p.moves, p.moves + 3, moves[want]) != p.moves + 3 && moves[want] != 0) want = (want + 1) % moves.size();
        CHECK_EQ(after, moves[want]);
        press(app, ui::kKeyR);  // next initial letter
        CHECK(p.moves[3] != 0);
        CHECK(data::initialOf(data::moveName(p.moves[3])) != data::initialOf(data::moveName(after)) || after == 0);

        // Numeric fields keep L/R = +-10: Level (field 2).
        for (int i = 0; i < kMove1 + 3 - 2; ++i) press(app, ui::kKeyUp);
        uint8_t level = app.editedPokemon().level;
        press(app, ui::kKeyR);
        CHECK_EQ(app.editedPokemon().level, level + 10);
        press(app, ui::kKeyL);
        CHECK_EQ(app.editedPokemon().level, level);
    }
}
