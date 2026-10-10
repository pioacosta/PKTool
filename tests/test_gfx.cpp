// The software renderer's fast paths (packed blending, glyphs written without per-pixel clipping,
// clipped image copies) must give the same pixels as the plain per-pixel versions.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "ds/gfx/canvas.h"
#include "ds/gfx/font.h"
#include "ds/gfx/sprites.h"
#include "tests/test.h"

using namespace pktool;
using gfx::Canvas;

namespace {

uint32_t g_seed = 99;
uint32_t rnd() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

gfx::Color referenceBlend(gfx::Color base, gfx::Color over, int alpha) {
    int inv = 32 - alpha;
    int r = ((base & 31) * inv + (over & 31) * alpha) >> 5;
    int g = (((base >> 5) & 31) * inv + ((over >> 5) & 31) * alpha) >> 5;
    int b = (((base >> 10) & 31) * inv + ((over >> 10) & 31) * alpha) >> 5;
    return static_cast<gfx::Color>(0x8000 | r | (g << 5) | (b << 10));
}

// The original glyph loop: one clipped pixel() call per set bit.
int referenceText(Canvas& c, const gfx::Font& font, int x, int y, const std::string& ascii, gfx::Color color) {
    int start = x;
    for (char ch : ascii) {
        const gfx::Glyph& g = font.glyphs[static_cast<uint8_t>(ch) - font.first];
        const uint8_t* rows = font.bits + g.offset;
        for (int row = 0; row < g.height; ++row) {
            uint16_t bits = static_cast<uint16_t>(rows[row * 2] | (rows[row * 2 + 1] << 8));
            for (int col = 0; col < g.width; ++col) {
                if (bits & (1u << col)) c.pixel(x + g.xOffset + col, y + g.yOffset + row, color);
            }
        }
        x += g.advance;
    }
    return x - start;
}

}  // namespace

TEST(gfx_blend_matches_the_channel_by_channel_formula) {
    for (int alpha = 0; alpha <= 32; ++alpha) {
        for (int n = 0; n < 4000; ++n) {
            gfx::Color a = static_cast<gfx::Color>(rnd() & 0xFFFF);
            gfx::Color b = static_cast<gfx::Color>(rnd() & 0xFFFF);
            CHECK_EQ(gfx::blend(a, b, alpha), referenceBlend(a, b, alpha));
        }
    }
    // Extremes of every channel.
    for (gfx::Color a : {0x0000, 0x001F, 0x03E0, 0x7C00, 0x7FFF, 0xFFFF}) {
        for (gfx::Color b : {0x0000, 0x001F, 0x03E0, 0x7C00, 0x7FFF, 0xFFFF}) {
            for (int alpha : {0, 1, 16, 31, 32}) CHECK_EQ(gfx::blend(a, b, alpha), referenceBlend(a, b, alpha));
        }
    }
}

TEST(gfx_text_fast_path_matches_per_pixel_drawing) {
    std::vector<uint16_t> fast(gfx::kScreenWidth * gfx::kScreenHeight);
    std::vector<uint16_t> slow(gfx::kScreenWidth * gfx::kScreenHeight);
    for (const gfx::Font* font : {&gfx::kFontSmall, &gfx::kFontLarge}) {
        for (int n = 0; n < 60; ++n) {
            Canvas a(fast.data());
            Canvas b(slow.data());
            a.clear(0x8000);
            b.clear(0x8000);
            // Clip rectangles that cut some glyphs and spare others; text partly off screen.
            int cx = static_cast<int>(rnd() % 120);
            int cy = static_cast<int>(rnd() % 90);
            int cw = 20 + static_cast<int>(rnd() % 130);
            int ch = 8 + static_cast<int>(rnd() % 60);
            a.setClip(cx, cy, cw, ch);
            b.setClip(cx, cy, cw, ch);
            int x = static_cast<int>(rnd() % 200) - 20;
            int y = static_cast<int>(rnd() % 150) - 5;
            std::string text = "Pokemon Garchomp Lv.100 #445";
            CHECK_EQ(a.text(*font, x, y, text, 0x7FFF), referenceText(b, *font, x, y, text, 0x7FFF));
            CHECK(fast == slow);
        }
    }
}

TEST(gfx_images_are_clipped_like_single_pixels) {
    std::vector<uint16_t> fast(gfx::kScreenWidth * gfx::kScreenHeight);
    std::vector<uint16_t> slow(gfx::kScreenWidth * gfx::kScreenHeight);
    std::vector<uint16_t> pixels(40 * 30);
    for (uint16_t& p : pixels) p = (rnd() & 3) == 0 ? 0 : static_cast<uint16_t>(0x8000 | (rnd() & 0x7FFF));
    gfx::Image image;
    image.width = 40;
    image.height = 30;
    image.pixels = pixels.data();
    for (int n = 0; n < 80; ++n) {
        Canvas a(fast.data());
        Canvas b(slow.data());
        a.clear(0x8000);
        b.clear(0x8000);
        int cx = static_cast<int>(rnd() % 100);
        int cy = static_cast<int>(rnd() % 80);
        int cw = 30 + static_cast<int>(rnd() % 120);
        int chh = 20 + static_cast<int>(rnd() % 90);
        a.setClip(cx, cy, cw, chh);
        b.setClip(cx, cy, cw, chh);
        int x = static_cast<int>(rnd() % 260) - 30;
        int y = static_cast<int>(rnd() % 200) - 20;
        a.image(x, y, image);
        for (int iy = 0; iy < image.height; ++iy) {
            for (int ix = 0; ix < image.width; ++ix) {
                uint16_t p = pixels[iy * image.width + ix];
                if (p & 0x8000) b.pixel(x + ix, y + iy, p);
            }
        }
        CHECK(fast == slow);
    }
}

TEST(gfx_text_width_handles_broken_utf8_like_before) {
    // A lone lead byte, a truncated sequence and a stray continuation byte each draw as '?'.
    int question = Canvas::textWidth(gfx::kFontSmall, "?");
    CHECK_EQ(Canvas::textWidth(gfx::kFontSmall, "\xC3"), question);
    CHECK_EQ(Canvas::textWidth(gfx::kFontSmall, "\xE3\x81"), question);
    CHECK_EQ(Canvas::textWidth(gfx::kFontSmall, "\x80"), question);
    CHECK_EQ(Canvas::textWidth(gfx::kFontSmall, "a\xFF" "b"),
             Canvas::textWidth(gfx::kFontSmall, "a") + question + Canvas::textWidth(gfx::kFontSmall, "b"));
    CHECK_EQ(Canvas::textWidth(gfx::kFontSmall, ""), 0);
    // Aligned text that fits is not shortened; text that does not fit gets an ellipsis.
    std::string longText = "A rather long label that cannot fit";
    CHECK(Canvas::textWidth(gfx::kFontSmall, Canvas::ellipsize(gfx::kFontSmall, longText, 60)) <= 60);
    CHECK(Canvas::ellipsize(gfx::kFontSmall, "Short", 200) == "Short");
}

TEST(sprite_store_evicts_the_least_recently_used_sprites_only) {
    char pattern[] = "/tmp/pktool-lru-XXXXXX";
    std::string dir = mkdtemp(pattern);
    CHECK(io::makeDirectories(io::joinPath(dir, "icons")));
    for (int species = 1; species <= 200; ++species) {
        std::vector<uint8_t> sprite = {'P', 'K', 'S', '1', 1, 0, 1, 0, static_cast<uint8_t>(species), 0x80};
        CHECK(io::safeWriteFile(io::joinPath(dir, "icons/" + std::to_string(species) + ".bin"), sprite) == io::WriteResult::Ok);
    }
    gfx::SpriteStore store({dir});
    CHECK(store.available());
    // A box worth of icons is used again and again while other sprites come and go.
    io::resetReadStats();
    for (int round = 0; round < 12; ++round) {
        for (int species = 1; species <= 30; ++species) {
            gfx::Image icon = store.get("icons", static_cast<uint16_t>(species));
            CHECK(icon.pixels != nullptr && icon.pixels[0] == (0x8000 | species));
        }
        for (int extra = 0; extra < 6; ++extra) {
            CHECK(store.get("icons", static_cast<uint16_t>(100 + round * 6 + extra)).pixels != nullptr);
        }
    }
    // Every sprite was read once: the 30 icons in use never left the cache (96 fit; 72 others came).
    CHECK_EQ(io::readStats().opens, 30u + 12u * 6u);
    store.clear();
    CHECK(store.get("icons", 1).pixels != nullptr);  // still works after clear()
}

TEST(sprite_store_reads_the_packed_sprites) {
    // The committed NitroFS sprites are packs (tools/pack_sprites.py).
    gfx::SpriteStore store({std::string(PKTOOL_SOURCE_DIR) + "/assets/nitrofs/sprites"});
    CHECK(store.available());
    for (const char* kind : {"icons", "front", "front-shiny"}) {
        gfx::Image image = store.get(kind, 25);
        CHECK(image.pixels != nullptr);
        CHECK(image.width > 0 && image.height > 0);
        CHECK(store.get(kind, 649).pixels != nullptr);
        CHECK(store.get(kind, 0).pixels != nullptr);  // the egg
        CHECK(store.get(kind, 700).pixels == nullptr);
    }
}
