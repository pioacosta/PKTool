// pktool_preview — renders the DS user interface on a PC.
//
//   pktool_preview <out_dir> <scan_dir> <data_dir> [save.sav]
//
// Drives the same ui::App used on the DS through a scripted sequence of
// inputs and writes each step as a PNG with both screens stacked (2x scale).
// <data_dir> plays the role of /PKTOOL (backups and optional sprites/).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ds/app/app.h"
#include "ds/gfx/canvas.h"

using namespace pktool;

namespace {

// ----- Minimal PNG writer (stored deflate blocks, no compression) -----------

uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgb.begin() + y * width * 3, rgb.begin() + (y + 1) * width * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size();) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> header;
    put32(header, static_cast<uint32_t>(width));
    put32(header, static_cast<uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0});  // 8-bit RGB
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

// ----- Rendering -------------------------------------------------------------

constexpr int kScale = 2;
constexpr int kGap = 8;

void writeScreens(const std::vector<uint16_t>& topPixels, const std::vector<uint16_t>& bottomPixels,
                  const std::string& path);

void render(ui::App& app, const std::string& path) {
    static std::vector<uint16_t> topPixels(gfx::kScreenWidth * gfx::kScreenHeight);
    static std::vector<uint16_t> bottomPixels(gfx::kScreenWidth * gfx::kScreenHeight);
    gfx::Canvas top(topPixels.data());
    gfx::Canvas bottom(bottomPixels.data());
    // Let fades and the gliding selection bar finish: shots show the settled screen.
    for (int i = 0; i < 12; ++i) {
        app.update(ui::Input());
        app.draw(top, bottom);
    }
    app.draw(top, bottom);
    writeScreens(topPixels, bottomPixels, path);
}

// Japanese and Korean nicknames in both fonts (the CJK fallback font).
void renderCjkSample(const std::string& path) {
    static std::vector<uint16_t> topPixels(gfx::kScreenWidth * gfx::kScreenHeight);
    static std::vector<uint16_t> bottomPixels(gfx::kScreenWidth * gfx::kScreenHeight);
    gfx::Canvas top(topPixels.data());
    gfx::Canvas bottom(bottomPixels.data());
    top.clear(gfx::rgb(12, 16, 30));
    bottom.clear(gfx::rgb(12, 16, 30));
    const gfx::Color white = gfx::rgb(238, 244, 255);
    const gfx::Color muted = gfx::rgb(140, 160, 206);
    top.text(gfx::kFontLarge, 8, 6, "Japanese nickname", white);
    top.text(gfx::kFontLarge, 8, 28, "ピカチュウ ポケモン 円", white);
    top.text(gfx::kFontSmall, 8, 52, "ピカチュウ  Lv. 50  みず 鉄 窓 ABC", muted);
    top.text(gfx::kFontSmall, 8, 68, "ＦＩＲＥ ｶﾅ ★♪◎ Pokémon ♂♀", muted);
    top.text(gfx::kFontLarge, 8, 96, "Korean nickname", white);
    top.text(gfx::kFontLarge, 8, 118, "피카츄 강아지", white);
    top.text(gfx::kFontSmall, 8, 142, "피카츄  Lv. 50  한국어", muted);
    bottom.text(gfx::kFontSmall, 8, 8, "Latin text is unchanged: Pikachu 25", white);
    writeScreens(topPixels, bottomPixels, path);
}

void writeScreens(const std::vector<uint16_t>& topPixels, const std::vector<uint16_t>& bottomPixels,
                  const std::string& path) {

    const int width = gfx::kScreenWidth * kScale;
    const int height = (gfx::kScreenHeight * 2 + kGap) * kScale;
    std::vector<uint8_t> rgb(static_cast<size_t>(width * height * 3), 16);
    auto blit = [&](const std::vector<uint16_t>& src, int offsetY) {
        for (int y = 0; y < gfx::kScreenHeight * kScale; ++y) {
            for (int x = 0; x < width; ++x) {
                uint16_t p = src[(y / kScale) * gfx::kScreenWidth + x / kScale];
                uint8_t* out = &rgb[((offsetY + y) * width + x) * 3];
                out[0] = static_cast<uint8_t>((p & 31) * 255 / 31);
                out[1] = static_cast<uint8_t>(((p >> 5) & 31) * 255 / 31);
                out[2] = static_cast<uint8_t>(((p >> 10) & 31) * 255 / 31);
            }
        }
    };
    blit(topPixels, 0);
    blit(bottomPixels, (gfx::kScreenHeight + kGap) * kScale);
    if (writePng(path, width, height, rgb)) {
        std::printf("wrote %s\n", path.c_str());
    } else {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
    }
}

void press(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    ui::Input idle;
    app.update(idle);  // lets pending actions (scan, write) run
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: pktool_preview <out_dir> <scan_dir> <data_dir> [save.sav]\n");
        return 2;
    }
    std::string out = argv[1];
    ui::Config config;
    config.scanRoot = argv[2];
    config.dataDir = argv[3];
    config.assetDirs.push_back(PKTOOL_ASSET_DIR);  // fonts/cjk.bin, lang/*.bin
    ui::App app(config);

    render(app, out + "/01-home.png");
    renderCjkSample(out + "/01x-cjk-text.png");
    // Settings: shown, changed and changed back (so nothing is written).
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);
    press(app, ui::kKeyA);
    render(app, out + "/01b-settings.png");
    press(app, ui::kKeyRight);  // option lists by ID
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);
    render(app, out + "/01c-settings-edited.png");
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);  // names in Japanese
    render(app, out + "/01d-settings-japanese.png");
    for (int i = 0; i < 5; ++i) press(app, ui::kKeyRight);  // ... Korean
    render(app, out + "/01e-settings-korean.png");
    press(app, ui::kKeyRight);  // back to English
    press(app, ui::kKeyUp);
    press(app, ui::kKeyLeft);
    press(app, ui::kKeyUp);
    press(app, ui::kKeyRight);  // back to by name
    press(app, ui::kKeyB);
    press(app, ui::kKeyUp);
    press(app, ui::kKeyLeft);  // Home cursor on "Open save" again
    press(app, ui::kKeyA);
    render(app, out + "/02-browser.png");

    if (argc < 5 || !app.openSave(argv[4])) {
        return 0;
    }
    render(app, out + "/03-savemenu.png");
    if (std::getenv("PKTOOL_PREVIEW_KEYBOARD")) {
        // Nickname keyboard of the first party Pokemon, every page (kana / Hangul first).
        for (int i = 0; i < 4; ++i) press(app, ui::kKeyA);
        for (int page = 0; page < 8; ++page) {
            render(app, out + "/kbd-" + std::to_string(page) + ".png");
            press(app, ui::kKeySelect);
        }
        return 0;
    }

    // Trainer card and bag editors, opened from the save menu.
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // Trainer
    render(app, out + "/03c-trainer.png");
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);  // gender
    press(app, ui::kKeyDown);   // Trainer ID
    press(app, ui::kKeyR);
    render(app, out + "/03d-trainer-id.png");
    for (int i = 0; i < 2; ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyY);  // money
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);  // hours
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);  // badges
    press(app, ui::kKeyA);
    press(app, ui::kKeyRight);
    press(app, ui::kKeyA);
    render(app, out + "/03e-trainer-edited.png");
    press(app, ui::kKeyB);
    render(app, out + "/03f-trainer-discard.png");
    press(app, ui::kKeyA);  // discard: back to the save menu
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // Bag
    render(app, out + "/03g-bag.png");
    press(app, ui::kKeyRight);  // quantity +1
    press(app, ui::kKeyR);
    press(app, ui::kKeyR);
    render(app, out + "/03h-bag-pocket.png");
    press(app, ui::kKeyL);
    press(app, ui::kKeyL);
    press(app, ui::kKeyUp);  // "Add item" row
    press(app, ui::kKeyA);
    render(app, out + "/03i-bag-picker.png");
    press(app, ui::kKeyA);  // add it
    render(app, out + "/03j-bag-added.png");
    press(app, ui::kKeyB);
    press(app, ui::kKeyA);  // discard everything
    press(app, ui::kKeyUp);
    press(app, ui::kKeyUp);
    press(app, ui::kKeyA);  // Boxes
    render(app, out + "/03a-party.png");
    press(app, ui::kKeyR);
    render(app, out + "/04-box.png");
    press(app, ui::kKeyRight);
    press(app, ui::kKeyA);  // slot menu
    render(app, out + "/04b-menu.png");
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // Move
    press(app, ui::kKeyRight);
    render(app, out + "/04c-move.png");
    press(app, ui::kKeyB);  // cancel the move
    press(app, ui::kKeyA);  // menu again
    press(app, ui::kKeyUp);
    press(app, ui::kKeyUp);  // Release
    press(app, ui::kKeyA);
    render(app, out + "/04d-release.png");
    press(app, ui::kKeyB);  // No
    press(app, ui::kKeyA);  // menu again
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyUp);
    press(app, ui::kKeyA);  // Ribbons
    render(app, out + "/04e-ribbons.png");
    press(app, ui::kKeyA);  // first ribbon on
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
    press(app, ui::kKeyDown);
    render(app, out + "/04f-ribbons-edited.png");
    press(app, ui::kKeyB);
    press(app, ui::kKeyA);  // discard
    press(app, ui::kKeyA);  // menu again: Edit
    press(app, ui::kKeyA);
    render(app, out + "/05-edit.png");
    press(app, ui::kKeyA);  // nickname keyboard
    press(app, ui::kKeyA);
    render(app, out + "/05b-keyboard.png");
    press(app, ui::kKeySelect);
    press(app, ui::kKeySelect);  // symbols page
    press(app, ui::kKeyA);       // male sign
    press(app, ui::kKeyRight);
    press(app, ui::kKeyA);       // female sign
    render(app, out + "/05c-keyboard-symbols.png");
    press(app, ui::kKeyLeft);
    press(app, ui::kKeySelect);  // upper case again
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);
    press(app, ui::kKeyRight);
    press(app, ui::kKeyRight);  // Cancel
    press(app, ui::kKeyA);
    press(app, ui::kKeyDown);  // species field
    press(app, ui::kKeyRight);  // species +1 shows a pending change
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyRight);  // nature +1: new PID
    render(app, out + "/06-edit-changed.png");
    press(app, ui::kKeySelect);
    render(app, out + "/06b-edit-legality.png");
    press(app, ui::kKeyA);
    press(app, ui::kKeyX);  // LEGALIZE
    render(app, out + "/06c-edit-legalized.png");
    press(app, ui::kKeyA);
    render(app, out + "/06d-edit-after-legalize.png");
    for (int i = 0; i < 6; ++i) press(app, ui::kKeyUp);  // wraps to the Pokérus fields
    press(app, ui::kKeyRight);
    press(app, ui::kKeyRight);  // Pokérus strain 2
    render(app, out + "/06e-edit-pokerus.png");
    for (int i = 0; i < 8; ++i) press(app, ui::kKeyUp);  // OT and met data
    render(app, out + "/06f-edit-met.png");
    press(app, ui::kKeyStart);
    press(app, ui::kKeySelect);
    render(app, out + "/07-confirm-write.png");
    press(app, ui::kKeyB);
    press(app, ui::kKeyStart);
    render(app, out + "/07b-box-legality.png");
    press(app, ui::kKeyA);
    press(app, ui::kKeyB);  // back to the save menu, now with unsaved changes
    render(app, out + "/08-savemenu-unsaved.png");
    press(app, ui::kKeyB);
    render(app, out + "/08b-close-dialog.png");
    press(app, ui::kKeyB);  // No
    for (int i = 0; i < 5; ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // Backups
    render(app, out + "/08c-backups.png");
    press(app, ui::kKeyB);
    for (int i = 0; i < 5; ++i) press(app, ui::kKeyUp);
    press(app, ui::kKeyA);  // Boxes again
    press(app, ui::kKeyY);
    press(app, ui::kKeyB);  // Bank and back (Y is the box view's shortcut)

    // Adding Pokémon to an empty slot (box 18 is usually empty).
    press(app, ui::kKeyL);
    press(app, ui::kKeyL);
    press(app, ui::kKeyLeft);  // slot 1
    press(app, ui::kKeyA);
    render(app, out + "/09-add-menu.png");
    press(app, ui::kKeyA);
    press(app, ui::kKeyR);
    press(app, ui::kKeyR);
    press(app, ui::kKeyR);
    press(app, ui::kKeyR);  // #041 Zubat
    render(app, out + "/10-add-species.png");
    press(app, ui::kKeyA);
    render(app, out + "/11-add-species-created.png");
    press(app, ui::kKeyA);
    render(app, out + "/12-add-species-editor.png");
    press(app, ui::kKeyB);
    press(app, ui::kKeyA);  // discard
    press(app, ui::kKeyA);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
    render(app, out + "/13-add-showdown.png");
    press(app, ui::kKeyA);
    render(app, out + "/14-add-showdown-created.png");
    press(app, ui::kKeyA);
    press(app, ui::kKeyB);
    press(app, ui::kKeyA);  // discard
    press(app, ui::kKeyA);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
    for (int i = 0; i < 17; ++i) press(app, ui::kKeyR);
    render(app, out + "/15-add-event.png");
    press(app, ui::kKeyA);
    render(app, out + "/16-add-event-created.png");

    // Bank: deposit the first Pokémon of box 1.
    press(app, ui::kKeyA);
    press(app, ui::kKeyB);
    press(app, ui::kKeyA);  // discard the new Pokémon
    press(app, ui::kKeyR);
    press(app, ui::kKeyR);  // box 1
    press(app, ui::kKeyY);
    render(app, out + "/17-bank.png");
    press(app, ui::kKeyA);
    render(app, out + "/18-bank-deposited.png");
    press(app, ui::kKeyA);
    press(app, ui::kKeyY);
    render(app, out + "/19-bank-after.png");

    // Bank menu, sort criteria and search results: reopen the save, deposit a
    // few Pokémon (Bank box 1) and show the menus.
    app.openSave(argv[4]);
    press(app, ui::kKeyA);  // Boxes
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyR);
    for (int i = 0; i < 6; ++i) {
        press(app, ui::kKeyY);
        for (int k = 0; k < i; ++k) press(app, ui::kKeyRight);  // next free Bank slot
        press(app, ui::kKeyA);  // deposit (a message follows)
        press(app, ui::kKeyA);
        press(app, ui::kKeyRight);
    }
    press(app, ui::kKeyY);
    press(app, ui::kKeyY);
    render(app, out + "/20-bank-menu.png");
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
    render(app, out + "/21-bank-sort.png");
    press(app, ui::kKeyB);
    press(app, ui::kKeyB);
    press(app, ui::kKeyY);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // search keyboard
    press(app, ui::kKeyA);  // types "A"
    press(app, ui::kKeyStart);
    render(app, out + "/22-bank-search.png");

    return 0;
}
