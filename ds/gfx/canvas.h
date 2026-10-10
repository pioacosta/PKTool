#pragma once

#include <cstdint>
#include <string>

#include "ds/gfx/font.h"

// Software renderer for one DS screen (256x192, 15-bit colour).
//
// Pixels use the DS bitmap-background format: bit 15 = opaque, then 5 bits
// each of blue, green and red. The code is portable so screens can also be
// rendered on a PC (tools/preview) for testing and screenshots.
namespace pktool::gfx {

constexpr int kScreenWidth = 256;
constexpr int kScreenHeight = 192;

using Color = uint16_t;

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<Color>(0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

// Mixes `over` onto `base`; alpha 0-32 (32 = only `over`).
Color blend(Color base, Color over, int alpha);

// An image in the same pixel format; pixels without bit 15 are transparent.
struct Image {
    int width = 0;
    int height = 0;
    const uint16_t* pixels = nullptr;
};

enum class Align : uint8_t { Left, Center, Right };

class Canvas {
public:
    explicit Canvas(uint16_t* pixels) : pixels_(pixels) {}

    uint16_t* pixels() { return pixels_; }
    const uint16_t* pixels() const { return pixels_; }

    void clear(Color color);
    void pixel(int x, int y, Color color);
    void fillRect(int x, int y, int w, int h, Color color);
    void blendRect(int x, int y, int w, int h, Color color, int alpha);
    void hline(int x, int y, int w, Color color);
    void vline(int x, int y, int h, Color color);
    void rect(int x, int y, int w, int h, Color color, int thickness = 1);
    // Rounded rectangle with 2px corners, optional fill (fill == 0: none).
    void roundRect(int x, int y, int w, int h, Color border, Color fill, int thickness = 1);
    void fillCircle(int cx, int cy, int radius, Color color);
    void circle(int cx, int cy, int radius, Color color);
    // Background grid like the reference design: cells of `cell` pixels.
    void grid(int x, int y, int w, int h, Color background, Color line, int cell);
    // Solid triangle pointing left (dir < 0) or right (dir > 0), `size` tall.
    void arrow(int x, int y, int size, int dir, Color color);
    // Corner brackets around a rectangle (selection cursor).
    void brackets(int x, int y, int w, int h, Color color, int length = 4);

    // Draws UTF-8 text; returns the width drawn. Characters outside Latin-1
    // are drawn as '?', except the male and female signs (symbols.cpp) and what
    // the Japanese/Korean fallback font has (cjk_font.h).
    int text(const Font& font, int x, int y, const std::string& utf8, Color color);
    int text(const Font& font, int x, int y, int width, Align align, const std::string& utf8, Color color);
    // True when the font draws `cp` itself (not the "?" fallback).
    static bool hasGlyph(const Font& font, uint32_t cp);
    static int textWidth(const Font& font, const std::string& utf8);
    // Shortens `utf8` with "..." so it fits in `width` pixels.
    static std::string ellipsize(const Font& font, const std::string& utf8, int width);

    void image(int x, int y, const Image& img);
    void imageScaled(int x, int y, const Image& img, int scale);

    // Restricts drawing to a rectangle (for scrolling lists).
    void setClip(int x, int y, int w, int h);
    void resetClip();

private:
    bool clipped(int x, int y) const {
        return x < clipX0_ || y < clipY0_ || x >= clipX1_ || y >= clipY1_;
    }
    static const Glyph& glyphFor(const Font& font, uint32_t cp, const uint8_t** bits);

    uint16_t* pixels_;
    int clipX0_ = 0;
    int clipY0_ = 0;
    int clipX1_ = kScreenWidth;
    int clipY1_ = kScreenHeight;
};

}  // namespace pktool::gfx
