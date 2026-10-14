#pragma once

#include <cstdint>

namespace pktool::gfx {

// One glyph of a 1-bit bitmap font (see tools/gen_font.py).
struct Glyph {
    uint16_t offset;  // into Font::bits, 2 bytes per row
    uint8_t advance;  // horizontal advance in pixels
    int8_t xOffset;
    int8_t yOffset;   // from the top of the line
    uint8_t width;
    uint8_t height;
};

struct Font {
    const Glyph* glyphs;  // one per code point in [first, last]
    const uint8_t* bits;
    uint16_t first;
    uint16_t last;
    uint8_t baseline;    // row of the baseline, from the top of the line
    uint8_t lineHeight;
};

// Hand-drawn male/female signs, yen sign and small comma (see symbols.cpp),
// used instead of the font's glyph; nullptr for any other code point. `*bits` receives the glyph's bitmap.
const Glyph* symbolGlyph(const Font& font, uint32_t cp, const uint8_t** bits);

extern const Font kFontSmall;  // Jersey 15, 15px: body text
extern const Font kFontLarge;  // Jersey 10, 20px: titles

}  // namespace pktool::gfx
