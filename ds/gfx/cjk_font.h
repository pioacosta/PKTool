#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ds/gfx/font.h"

// Japanese/Korean fallback font, 12x12 pixels, read on demand from
// fonts/cjk.bin (assets/nitrofs, built by tools/gen_cjk_font.py from Ark Pixel
// Font and Galmuri11, both SIL OFL 1.1). Canvas::text uses it for the code
// points the Latin fonts do not have: kana, kanji, Hangul, symbols.
//
// Only the code point index (about 17 KB) stays in RAM; the 18-byte bitmaps
// are read from the file when a glyph is first drawn and kept in a small cache.
namespace pktool::gfx {

// Opens "<dir>/fonts/cjk.bin" for the first directory that has it. Returns
// false (and draws "?") when no directory does.
bool openCjkFont(const std::vector<std::string>& directories);
void closeCjkFont();
bool cjkFontAvailable();

// True when the font has a glyph for `cp`.
bool cjkHas(uint32_t cp);
// Glyph of `cp` for the line of `font` (its baseline places it), or nullptr.
// `*bits` receives 12 rows of 2 bytes, like Font::bits. Both pointers stay
// valid until the next call.
const Glyph* cjkGlyph(const Font& font, uint32_t cp, const uint8_t** bits);

}  // namespace pktool::gfx
