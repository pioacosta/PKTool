// Bank search filters in the UI: Bank menu > Search the Bank opens a screen
// with the name, "shiny only" and game filters (empty name allowed).

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/bank/bank.h"
#include "core/data/personal.h"
#include "core/io/file_io.h"
#include "core/pokemon/pk4.h"
#include "core/util/bytes.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/test.h"

using namespace pktool;
using bank::Bank;
namespace pk4 = pokemon::pk4;

namespace {

std::vector<uint16_t> g_top(gfx::kScreenWidth * gfx::kScreenHeight);
std::vector<uint16_t> g_bottom(gfx::kScreenWidth * gfx::kScreenHeight);

enum : uint16_t { kPidgey = 16, kPikachu = 25, kEevee = 133 };

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

void put(Bank& b, int box, int slot, uint16_t species, uint8_t format, bool shiny) {
    uint8_t data[Bank::kEntitySize] = {};
    const data::PersonalInfo* info = data::personal4(species);
    util::writeU16(data + pk4::offset::kSpecies, species);
    util::writeU32(data + pk4::offset::kPid, shiny ? 0 : 0x12345678);  // PID 0 with TID = SID = 0 is shiny
    util::writeU32(data + pk4::offset::kExp, data::expForLevel(static_cast<data::GrowthRate>(info->growthRate), 10));
    CHECK(b.set(box, slot, format, data));
}

struct Env {
    ui::Config config;
    std::string bankFile;
    Env() {
        char pattern[] = "/tmp/pktool-bankf-XXXXXX";
        std::string dir = mkdtemp(pattern);
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        bankFile = io::joinPath(config.dataDir, "bank/bank.pkb");
    }
};

// Bank: shiny Pikachu (Gen IV, box 1 slot 3), plain Pikachu (Gen V, box 2 slot 1),
// shiny Eevee (Gen V, box 3 slot 8), plain Pidgey (Gen IV, box 4 slot 2).
void seed(Env& env) {
    Bank b;
    b.load(env.bankFile);
    put(b, 0, 2, kPikachu, 4, true);
    put(b, 1, 0, kPikachu, 5, false);
    put(b, 2, 7, kEevee, 5, true);
    put(b, 3, 1, kPidgey, 4, false);
    CHECK(b.save() == io::WriteResult::Ok);
}

void openSearch(ui::App& app) {
    press(app, ui::kKeyDown);  // Bank tile
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
    press(app, ui::kKeyY);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);  // Search the Bank
    press(app, ui::kKeyA);
}

// On the filters screen: cursor to `row` (0 name, 1 shiny, 2 game, 3 Search).
void toRow(ui::App& app, int& at, int row) {
    while (at < row) {
        press(app, ui::kKeyDown);
        ++at;
    }
}

// The slot the Bank cursor is on, found by picking its Pokémon up (X) and
// dropping it on the same slot of the next box: returns its species there.
uint16_t moveCursorPokemonToNextBox(ui::App& app, Env& env, int fromBox, int slot) {
    press(app, ui::kKeyX);
    press(app, ui::kKeyR);
    press(app, ui::kKeyX);
    Bank b;
    b.load(env.bankFile);
    const uint8_t* moved = b.entity(fromBox + 1, slot);
    return moved ? util::readU16(moved + pk4::offset::kSpecies) : 0;
}

}  // namespace

TEST(bank_filter_shiny_only_with_an_empty_name_finds_shinies) {
    Env env;
    seed(env);
    ui::App app(env.config);
    openSearch(app);
    int at = 0;
    toRow(app, at, 1);
    press(app, ui::kKeyRight);  // Shiny: Shiny only
    toRow(app, at, 3);
    press(app, ui::kKeyA);      // Search: two shiny Pokémon, in Bank order
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
    press(app, ui::kKeyDown);   // second result: the shiny Eevee (box 3 slot 8)
    press(app, ui::kKeyA);      // go to it
    CHECK_EQ(moveCursorPokemonToNextBox(app, env, 2, 7), kEevee);
}

TEST(bank_filter_game_filter_and_combination) {
    Env env;
    seed(env);
    ui::App app(env.config);
    openSearch(app);
    int at = 0;
    toRow(app, at, 2);
    press(app, ui::kKeyRight);  // Game: Gen III
    press(app, ui::kKeyRight);  // Gen IV
    toRow(app, at, 3);
    press(app, ui::kKeyA);      // Gen IV Pokémon: the shiny Pikachu (box 1) then Pidgey (box 4)
    press(app, ui::kKeyDown);   // Pidgey
    press(app, ui::kKeyA);
    CHECK_EQ(moveCursorPokemonToNextBox(app, env, 3, 1), kPidgey);
}

TEST(bank_filter_game_five_and_no_match) {
    Env env;
    seed(env);
    ui::App app(env.config);
    openSearch(app);
    int at = 0;
    toRow(app, at, 2);
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyRight);  // Gen V
    toRow(app, at, 3);
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
    press(app, ui::kKeyA);  // first result: the plain Pikachu (box 2 slot 1)
    CHECK_EQ(moveCursorPokemonToNextBox(app, env, 1, 0), kPikachu);

    // The filters are kept: Gen V -> Gen III with "shiny only" matches nothing.
    press(app, ui::kKeyY);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // the filters screen again, still on Gen V
    at = 0;
    toRow(app, at, 1);
    press(app, ui::kKeyRight);  // Shiny only
    press(app, ui::kKeyDown);
    press(app, ui::kKeyLeft);   // Gen V -> Gen IV
    press(app, ui::kKeyLeft);   // Gen III
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);      // Search: no match, a message
    press(app, ui::kKeyA);      // dismiss: still on the filters
    press(app, ui::kKeyB);      // filters -> menu
    press(app, ui::kKeyB);      // menu -> Bank
    press(app, ui::kKeyB);      // Bank -> Home
    CHECK(std::strcmp(app.screenName(), "Home") == 0);
}

TEST(bank_filter_search_without_name_or_filter_asks_for_one) {
    Env env;
    seed(env);
    ui::App app(env.config);
    openSearch(app);
    int at = 0;
    toRow(app, at, 3);
    press(app, ui::kKeyA);  // nothing set: a message, no results
    press(app, ui::kKeyA);  // dismiss
    press(app, ui::kKeyB);  // filters -> Bank menu
    press(app, ui::kKeyB);  // menu -> Bank
    press(app, ui::kKeyB);  // Bank -> Home
    CHECK(std::strcmp(app.screenName(), "Home") == 0);
}

TEST(bank_filter_b_from_results_returns_to_the_filters) {
    Env env;
    seed(env);
    ui::App app(env.config);
    openSearch(app);
    int at = 0;
    toRow(app, at, 1);
    press(app, ui::kKeyA);  // A also toggles Shiny
    toRow(app, at, 3);
    press(app, ui::kKeyA);  // results
    press(app, ui::kKeyB);  // back to the filters (Search row selected)
    press(app, ui::kKeyA);  // search again: same results
    press(app, ui::kKeyA);  // go to the first result: the shiny Pikachu (box 1 slot 3)
    CHECK_EQ(moveCursorPokemonToNextBox(app, env, 0, 2), kPikachu);
}
