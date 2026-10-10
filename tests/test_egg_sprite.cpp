// The egg is species 0 of the sprite packs: box icon and summary sprite.
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "ds/gfx/sprites.h"
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

// Creates a Day-Care egg in an empty box slot of a Platinum save and returns the top screen.
std::vector<uint16_t> eggScreen(const std::vector<std::string>& spriteDirs) {
    char pattern[] = "/tmp/pktool-egg-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    config.spriteDirs = spriteDirs;
    const std::string savePath = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(savePath, fake::blankGen4(fake::kPt)) == io::WriteResult::Ok);
    ui::App app(config);
    CHECK(app.openSave(savePath));
    press(app, ui::kKeyA);  // Boxes
    press(app, ui::kKeyR);
    press(app, ui::kKeyA);  // empty slot: add menu
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // Egg: species list
    press(app, ui::kKeyA);  // first species: the egg is created
    CHECK(app.editedPokemon().isEgg);
    press(app, ui::kKeyA);  // close the result dialog
    for (int i = 0; i < 10; ++i) press(app, 0);  // let the screen fade-in settle
    return g_top;
}

}  // namespace

TEST(egg_sprites_are_in_the_packs) {
    gfx::SpriteStore store({std::string(PKTOOL_SOURCE_DIR) + "/assets/nitrofs/sprites"});
    for (const char* kind : {"icons", "front", "front-shiny"}) {
        gfx::Image egg = store.get(kind, 0);
        CHECK(egg.pixels != nullptr);
        CHECK(egg.width > 8 && egg.height > 8);
    }
}

TEST(egg_summary_draws_the_sprite_instead_of_the_placeholder) {
    const std::string sprites = std::string(PKTOOL_SOURCE_DIR) + "/assets/nitrofs/sprites";
    std::vector<uint16_t> with = eggScreen({sprites});
    std::vector<uint16_t> without = eggScreen({});
    int differing = 0;
    for (size_t i = 0; i < with.size(); ++i) differing += with[i] != without[i] ? 1 : 0;
    CHECK(differing > 200);  // the 28x30 egg replaces a drawn circle
}
