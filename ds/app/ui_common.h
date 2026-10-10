#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "core/pokemon/pokemon.h"
#include "ds/gfx/canvas.h"

// Theme colours, layout constants, touch-zone identifiers and small helpers
// shared by the UI source files (app.cpp, trainer_bag_ui.cpp).
namespace pktool::ui::detail {

using gfx::Align;
using gfx::Canvas;
using gfx::Color;
using gfx::kFontLarge;
using gfx::kFontSmall;
using gfx::rgb;

// ----- Theme -----------------------------------------------------------------

inline const Color kBackground = rgb(24, 38, 82);
inline const Color kGridLine = rgb(34, 54, 110);
inline const Color kTitleBar = rgb(12, 16, 30);
inline const Color kStatusBar = rgb(22, 34, 72);
inline const Color kPanel = rgb(18, 30, 64);
inline const Color kPanelDark = rgb(14, 26, 48);
inline const Color kBoxArea = rgb(20, 44, 62);
inline const Color kBoxGrid = rgb(26, 54, 74);
inline const Color kBorder = rgb(44, 88, 170);
inline const Color kBorderLight = rgb(150, 176, 226);
inline const Color kAccent = rgb(40, 180, 240);
inline const Color kCursor = rgb(240, 120, 96);
inline const Color kText = rgb(238, 244, 255);
inline const Color kMuted = rgb(140, 160, 206);
inline const Color kDim = rgb(70, 92, 140);
inline const Color kGreen = rgb(70, 204, 140);
inline const Color kYellow = rgb(244, 200, 70);
inline const Color kRed = rgb(236, 92, 92);
inline const Color kMale = rgb(96, 164, 255);
inline const Color kFemale = rgb(255, 126, 156);
inline const Color kSelectedRow = rgb(28, 58, 110);

constexpr int kTitleHeight = 18;
constexpr int kStatusTop = 18;
constexpr int kStatusHeight = 15;
constexpr int kBodyTop = kStatusTop + kStatusHeight;
constexpr int kHintTop = 176;

// Touch zone identifiers. Values below kZoneItem are key masks.
constexpr int kZoneItem = 0x10000;       // + index (tile, row, cell)
constexpr int kZonePrevBox = 0x20000;
constexpr int kZoneNextBox = 0x20001;
constexpr int kZoneDecrease = 0x30000;   // + editor row
constexpr int kZoneIncrease = 0x40000;   // + editor row
constexpr int kZoneBoxTitle = 0x50000;   // box name: tap to rename
constexpr int kZoneBadge = 0x60000;      // + badge index (trainer screen)

inline int wrap(int value, int count) {
    if (count <= 0) {
        return 0;
    }
    return (value % count + count) % count;
}

inline int clampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

inline std::string twoDigits(int value) {
    char buffer[12];  // any int fits
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return buffer;
}

// 1234567 -> "1,234,567".
inline std::string groupDigits(uint32_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    int count = 0;
    for (size_t i = digits.size(); i-- > 0;) {
        out.insert(out.begin(), digits[i]);
        if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return out;
}

inline Color genderColor(pokemon::Gender gender) {
    return gender == pokemon::Gender::Female ? kFemale : kMale;
}

// Small gender glyphs (the fonts have no male/female signs). 7x7 bitmaps, bit 0 = left.
inline void drawGenderGlyph(Canvas& c, int x, int y, pokemon::Gender gender) {
    static const uint8_t kMaleRows[7] = {0x78, 0x60, 0x50, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t kFemaleRows[7] = {0x0E, 0x11, 0x11, 0x0E, 0x04, 0x1F, 0x04};
    if (gender == pokemon::Gender::Genderless) {
        return;
    }
    const uint8_t* rows = gender == pokemon::Gender::Male ? kMaleRows : kFemaleRows;
    for (int r = 0; r < 7; ++r) {
        for (int b = 0; b < 7; ++b) {
            if (rows[r] & (1 << b)) {
                c.pixel(x + b, y + r, genderColor(gender));
            }
        }
    }
}

// 7x7 star marking shiny Pokémon.
inline void drawStar(Canvas& c, int x, int y, Color color) {
    static const uint8_t kRows[7] = {0x08, 0x08, 0x7F, 0x3E, 0x1C, 0x36, 0x41};
    for (int r = 0; r < 7; ++r) {
        for (int b = 0; b < 7; ++b) {
            if (kRows[r] & (1 << b)) {
                c.pixel(x + b, y + r, color);
            }
        }
    }
}

}  // namespace pktool::ui::detail
