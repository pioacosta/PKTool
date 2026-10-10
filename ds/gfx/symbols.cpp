// Hand-drawn glyphs for the male and female signs (U+2642, U+2640), which the
// generated Jersey fonts (tools/gen_font.py) do not contain. They follow the
// font's grid: 1px strokes, cap height 8 (small) / 11 (large). Original work,
// released under the same licence as the project.
//
// Each row is 2 bytes (little endian), bit 0 = leftmost pixel, like Font::bits.

#include "ds/gfx/font.h"

namespace pktool::gfx {
namespace {

// Small font (cap height 8, rows 3..9 of the line).
//   male           female
//   ...####        .###.
//   .....##        #...#
//   ....#.#        #...#
//   .###...        .###.
//   #...#..        ..#..
//   #...#..        #####
//   .###...        ..#..
const uint8_t kSmallMale[] = {0x78, 0, 0x60, 0, 0x50, 0, 0x0E, 0, 0x11, 0, 0x11, 0, 0x0E, 0};
const uint8_t kSmallFemale[] = {0x0E, 0, 0x11, 0, 0x11, 0, 0x0E, 0, 0x04, 0, 0x1F, 0, 0x04, 0};

// Large font (cap height 11, rows 2..12 of the line).
//   male          female
//   ....#####     .#####.
//   ......###     ##...##
//   .......##     #.....#
//   ......#..     #.....#
//   .#####..      #.....#
//   ##...##       ##...##
//   #.....#       .#####.
//   #.....#       ...#...
//   #.....#       .#####.
//   ##...##       ...#...
//   .#####.       ...#...
const uint8_t kLargeMale[] = {0xF0, 0x01, 0xC0, 0x01, 0x80, 0x01, 0x40, 0x00, 0x3E, 0x00, 0x63, 0x00,
                              0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0x63, 0x00, 0x3E, 0x00};
const uint8_t kLargeFemale[] = {0x3E, 0, 0x63, 0, 0x41, 0, 0x41, 0, 0x41, 0, 0x63, 0,
                                0x3E, 0, 0x08, 0, 0x3E, 0, 0x08, 0, 0x08, 0};

//                                  offset advance x  y  w  h
const Glyph kSmallMaleGlyph = {0, 9, 1, 3, 7, 7};
const Glyph kSmallFemaleGlyph = {0, 7, 1, 3, 5, 7};
const Glyph kLargeMaleGlyph = {0, 11, 1, 2, 9, 11};
const Glyph kLargeFemaleGlyph = {0, 9, 1, 2, 7, 11};

}  // namespace

const Glyph* symbolGlyph(const Font& font, uint32_t cp, const uint8_t** bits) {
    bool male = cp == 0x2642;
    if (!male && cp != 0x2640) {
        return nullptr;
    }
    if (font.lineHeight >= 18) {
        *bits = male ? kLargeMale : kLargeFemale;
        return male ? &kLargeMaleGlyph : &kLargeFemaleGlyph;
    }
    *bits = male ? kSmallMale : kSmallFemale;
    return male ? &kSmallMaleGlyph : &kSmallFemaleGlyph;
}

}  // namespace pktool::gfx
