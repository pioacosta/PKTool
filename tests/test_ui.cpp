#include <cstdlib>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "ds/gfx/sprites.h"
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
    app.update(ui::Input());  // run pending actions
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);
}

}  // namespace

TEST(canvas_fill_and_clip) {
    gfx::Canvas c(g_top.data());
    c.clear(gfx::rgb(0, 0, 0));
    c.fillRect(-10, -10, 20, 20, gfx::rgb(255, 255, 255));
    CHECK_EQ(g_top[0], gfx::rgb(255, 255, 255));
    CHECK_EQ(g_top[10], gfx::rgb(0, 0, 0));
    c.setClip(100, 100, 10, 10);
    c.fillRect(0, 0, 256, 192, gfx::rgb(255, 0, 0));
    CHECK_EQ(g_top[100 * 256 + 100], gfx::rgb(255, 0, 0));
    CHECK_EQ(g_top[99 * 256 + 100], gfx::rgb(0, 0, 0));
    c.resetClip();
    c.fillRect(250, 190, 100, 100, gfx::rgb(0, 255, 0));  // clipped at the screen edge
    CHECK_EQ(g_top[191 * 256 + 255], gfx::rgb(0, 255, 0));
}

TEST(canvas_text_measurement) {
    int w = gfx::Canvas::textWidth(gfx::kFontSmall, "Garchomp");
    CHECK(w > 30 && w < 60);
    CHECK_EQ(gfx::Canvas::textWidth(gfx::kFontSmall, ""), 0);
    std::string fitted = gfx::Canvas::ellipsize(gfx::kFontSmall, "A very long Pok\xC3\xA9mon nickname", 40);
    CHECK(gfx::Canvas::textWidth(gfx::kFontSmall, fitted) <= 40);
    CHECK(fitted.size() > 3 && fitted.compare(fitted.size() - 3, 3, "...") == 0);
    // Unknown characters fall back to '?' instead of crashing.
    CHECK(gfx::Canvas::textWidth(gfx::kFontSmall, "\xE3\x81\x82") > 0);
}

TEST(canvas_blend) {
    gfx::Color black = gfx::rgb(0, 0, 0);
    gfx::Color white = gfx::rgb(255, 255, 255);
    CHECK_EQ(gfx::blend(black, white, 32), white);
    CHECK_EQ(gfx::blend(black, white, 0), black);
}

TEST(sprite_file_parsing) {
    std::vector<uint8_t> bytes = {'P', 'K', 'S', '1', 2, 0, 1, 0, 0x1F, 0x80, 0x00, 0x00};
    int w = 0, h = 0;
    std::vector<uint16_t> pixels;
    CHECK(gfx::SpriteStore::parse(bytes, w, h, pixels));
    CHECK_EQ(w, 2);
    CHECK_EQ(h, 1);
    CHECK_EQ(pixels[0], 0x801F);
    bytes.pop_back();
    CHECK(!gfx::SpriteStore::parse(bytes, w, h, pixels));
    CHECK(!gfx::SpriteStore::parse({'X', 'K', 'S', '1', 1, 0, 1, 0, 0, 0}, w, h, pixels));
}

TEST(ui_navigates_without_saves_or_sprites) {
    char pattern[] = "/tmp/pktool-ui-XXXXXX";
    std::string dir = mkdtemp(pattern);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    ui::App app(config);
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);

    press(app, ui::kKeyA);      // open save -> browser (empty)
    press(app, ui::kKeyA);      // nothing to open
    press(app, ui::kKeyDown);
    press(app, ui::kKeyB);      // back home
    press(app, ui::kKeyDown);   // settings tile
    press(app, ui::kKeyRight);
    press(app, ui::kKeyA);      // settings screen
    press(app, ui::kKeyUp);     // About row
    press(app, ui::kKeyA);      // about dialog
    press(app, ui::kKeyA);      // close it
    press(app, ui::kKeyB);      // back home
    CHECK(!app.openSave(io::joinPath(dir, "missing.sav")));
    press(app, ui::kKeyA);      // dismiss the error

    ui::Input start;
    start.pressed = ui::kKeyStart;
    CHECK(!app.update(start));  // START on the home screen exits
}

TEST(ui_touch_on_hint_bar_acts_as_button) {
    char pattern[] = "/tmp/pktool-ui-XXXXXX";
    std::string dir = mkdtemp(pattern);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    ui::App app(config);
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);

    // Tapping the first tile opens the (empty) save browser.
    ui::Input tap;
    tap.touched = true;
    tap.touchX = 40;
    tap.touchY = 40;
    app.update(tap);
    app.update(ui::Input());
    app.draw(top, bottom);
    // Tapping the "B Back" hint returns home; tapping START there exits.
    ui::Input back;
    back.touched = true;
    back.touchX = 100;
    back.touchY = 184;
    app.update(back);
    app.draw(top, bottom);
    ui::Input exitTap;
    exitTap.touched = true;
    exitTap.touchX = 200;
    exitTap.touchY = 184;
    CHECK(!app.update(exitTap));
}

TEST(sprite_store_falls_back_to_next_directory) {
    char pattern[] = "/tmp/pktool-sprites-XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::string sd = io::joinPath(dir, "sd");
    CHECK(io::makeDirectories(io::joinPath(sd, "icons")));
    const std::vector<uint8_t> sprite = {'P', 'K', 'S', '1', 1, 0, 1, 0, 0x1F, 0x80};
    CHECK(io::safeWriteFile(io::joinPath(sd, "icons/25.bin"), sprite) == io::WriteResult::Ok);

    gfx::SpriteStore store({io::joinPath(dir, "missing-nitro"), sd});
    CHECK(store.available());
    gfx::Image icon = store.get("icons", 25);
    CHECK(icon.pixels != nullptr && icon.width == 1 && icon.pixels[0] == 0x801F);
    CHECK(store.get("icons", 26).pixels == nullptr);

    gfx::SpriteStore none({io::joinPath(dir, "nothing")});
    CHECK(!none.available());
}
