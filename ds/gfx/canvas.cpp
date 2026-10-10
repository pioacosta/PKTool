#include "ds/gfx/canvas.h"

#include "ds/gfx/cjk_font.h"

#include <algorithm>
#include <vector>

namespace pktool::gfx {
namespace {

// Reads the UTF-8 code point at `i` and moves `i` past it; invalid bytes become '?' (a sequence
// cut short by the end of the string consumes the rest of it). Decoding on the fly keeps text
// drawing from allocating: every label of every frame goes through here.
uint32_t nextCodePoint(const std::string& s, size_t& i) {
    uint8_t lead = static_cast<uint8_t>(s[i]);
    if (lead < 0x80) {
        ++i;
        return lead;
    }
    uint32_t cp;
    size_t length;
    if ((lead & 0xE0) == 0xC0) {
        cp = lead & 0x1F;
        length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        cp = lead & 0x0F;
        length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        cp = lead & 0x07;
        length = 4;
    } else {
        ++i;
        return '?';
    }
    if (i + length > s.size()) {
        i = s.size();
        return '?';
    }
    for (size_t k = 1; k < length; ++k) {
        cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
    }
    i += length;
    return cp;
}

uint32_t substitute(uint32_t cp) {
    switch (cp) {
        case 0x2018:
        case 0x2019:
            return '\'';
        case 0x201C:
        case 0x201D:
            return '"';
        case 0x3000:
            return ' ';
        case 0x2022:
            return 0xB7;  // bullet -> middle dot
        default:
            return cp;
    }
}

}  // namespace

Color blend(Color base, Color over, int alpha) {
    // The three 5-bit channels are spread over a 32-bit word (red at bit 0, blue at 10, a copy
    // of green at 21) so that one multiplication blends all of them: each channel product is at
    // most 31 * 32 = 992, which fits its 10-bit lane. Same result as blending channel by channel.
    constexpr uint32_t kLanes = 0x03E07C1Fu;
    const uint32_t b = (base | (static_cast<uint32_t>(base) << 16)) & kLanes;
    const uint32_t o = (over | (static_cast<uint32_t>(over) << 16)) & kLanes;
    const uint32_t m = ((b * static_cast<uint32_t>(32 - alpha) + o * static_cast<uint32_t>(alpha)) >> 5) & kLanes;
    return static_cast<Color>(0x8000 | ((m | (m >> 16)) & 0x7FFF));
}

void Canvas::clear(Color color) {
    std::fill(pixels_, pixels_ + kScreenWidth * kScreenHeight, color);
}

void Canvas::pixel(int x, int y, Color color) {
    if (!clipped(x, y)) {
        pixels_[y * kScreenWidth + x] = color;
    }
}

void Canvas::fillRect(int x, int y, int w, int h, Color color) {
    int x0 = std::max(x, clipX0_);
    int y0 = std::max(y, clipY0_);
    int x1 = std::min(x + w, clipX1_);
    int y1 = std::min(y + h, clipY1_);
    for (int yy = y0; yy < y1; ++yy) {
        std::fill(pixels_ + yy * kScreenWidth + x0, pixels_ + yy * kScreenWidth + std::max(x0, x1), color);
    }
}

void Canvas::blendRect(int x, int y, int w, int h, Color color, int alpha) {
    int x0 = std::max(x, clipX0_);
    int y0 = std::max(y, clipY0_);
    int x1 = std::min(x + w, clipX1_);
    int y1 = std::min(y + h, clipY1_);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            uint16_t& p = pixels_[yy * kScreenWidth + xx];
            p = blend(p, color, alpha);
        }
    }
}

void Canvas::hline(int x, int y, int w, Color color) {
    fillRect(x, y, w, 1, color);
}

void Canvas::vline(int x, int y, int h, Color color) {
    fillRect(x, y, 1, h, color);
}

void Canvas::rect(int x, int y, int w, int h, Color color, int thickness) {
    fillRect(x, y, w, thickness, color);
    fillRect(x, y + h - thickness, w, thickness, color);
    fillRect(x, y, thickness, h, color);
    fillRect(x + w - thickness, y, thickness, h, color);
}

void Canvas::roundRect(int x, int y, int w, int h, Color border, Color fill, int thickness) {
    if (fill != 0) {
        fillRect(x + 1, y + 1, w - 2, h - 2, fill);
    }
    for (int t = 0; t < thickness; ++t) {
        int xi = x + t;
        int yi = y + t;
        int wi = w - 2 * t;
        int hi = h - 2 * t;
        hline(xi + 2, yi, wi - 4, border);
        hline(xi + 2, yi + hi - 1, wi - 4, border);
        vline(xi, yi + 2, hi - 4, border);
        vline(xi + wi - 1, yi + 2, hi - 4, border);
        pixel(xi + 1, yi + 1, border);
        pixel(xi + wi - 2, yi + 1, border);
        pixel(xi + 1, yi + hi - 2, border);
        pixel(xi + wi - 2, yi + hi - 2, border);
    }
}

void Canvas::fillCircle(int cx, int cy, int radius, Color color) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius + radius / 2) {
                pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

void Canvas::circle(int cx, int cy, int radius, Color color) {
    int inner = (radius - 1) * (radius - 1) + (radius - 1) / 2;
    int outer = radius * radius + radius / 2;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int d = dx * dx + dy * dy;
            if (d <= outer && d > inner) {
                pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

void Canvas::grid(int x, int y, int w, int h, Color background, Color line, int cell) {
    fillRect(x, y, w, h, background);
    for (int yy = y + cell - 1; yy < y + h; yy += cell) {
        hline(x, yy, w, line);
    }
    for (int xx = x + cell - 1; xx < x + w; xx += cell) {
        vline(xx, y, h, line);
    }
}

void Canvas::arrow(int x, int y, int size, int dir, Color color) {
    int half = size / 2;
    for (int i = 0; i <= half; ++i) {
        int column = dir < 0 ? x + i : x + half - i;
        vline(column, y + half - i, 2 * i + 1, color);
    }
}

void Canvas::brackets(int x, int y, int w, int h, Color color, int length) {
    int r = x + w - 1;
    int b = y + h - 1;
    hline(x, y, length, color);
    vline(x, y, length, color);
    hline(r - length + 1, y, length, color);
    vline(r, y, length, color);
    hline(x, b, length, color);
    vline(x, b - length + 1, length, color);
    hline(r - length + 1, b, length, color);
    vline(r, b - length + 1, length, color);
}

const Glyph& Canvas::glyphFor(const Font& font, uint32_t cp, const uint8_t** bits) {
    if (const Glyph* symbol = symbolGlyph(font, cp, bits)) {
        return *symbol;
    }
    cp = substitute(cp);
    if (cp < font.first || cp > font.last || font.glyphs[cp - font.first].advance == 0) {
        if (const Glyph* cjk = cjkGlyph(font, cp, bits)) {
            return *cjk;
        }
        cp = '?';
    }
    const Glyph& g = font.glyphs[cp - font.first];
    *bits = font.bits + g.offset;
    return g;
}

bool Canvas::hasGlyph(const Font& font, uint32_t cp) {
    const uint8_t* bits = nullptr;
    if (symbolGlyph(font, cp, &bits) != nullptr) {
        return true;
    }
    cp = substitute(cp);
    return (cp >= font.first && cp <= font.last && font.glyphs[cp - font.first].advance != 0) || cjkHas(cp);
}

int Canvas::text(const Font& font, int x, int y, const std::string& utf8, Color color) {
    int start = x;
    for (size_t i = 0; i < utf8.size();) {
        const uint8_t* rows = nullptr;
        const Glyph& g = glyphFor(font, nextCodePoint(utf8, i), &rows);
        const int gx = x + g.xOffset;
        const int gy = y + g.yOffset;
        // A glyph entirely inside the clip rectangle is written without per-pixel tests.
        const bool inside = gx >= clipX0_ && gy >= clipY0_ && gx + g.width <= clipX1_ && gy + g.height <= clipY1_;
        for (int row = 0; row < g.height; ++row) {
            uint16_t bits = static_cast<uint16_t>(rows[row * 2] | (rows[row * 2 + 1] << 8));
            if (g.width < 16) bits &= static_cast<uint16_t>((1u << g.width) - 1);
            if (bits == 0) continue;
            if (inside) {
                uint16_t* dst = pixels_ + (gy + row) * kScreenWidth + gx;
                for (int col = 0; bits != 0; ++col, bits >>= 1) {
                    if (bits & 1u) dst[col] = color;
                }
            } else {
                for (int col = 0; col < g.width; ++col) {
                    if (bits & (1u << col)) pixel(gx + col, gy + row, color);
                }
            }
        }
        x += g.advance;
    }
    return x - start;
}

int Canvas::text(const Font& font, int x, int y, int width, Align align, const std::string& utf8, Color color) {
    int w = textWidth(font, utf8);
    if (w <= width) {
        if (align == Align::Center) {
            x += (width - w) / 2;
        } else if (align == Align::Right) {
            x += width - w;
        }
        return text(font, x, y, utf8, color);
    }
    std::string fitted = ellipsize(font, utf8, width);
    w = textWidth(font, fitted);
    if (align == Align::Center) {
        x += (width - w) / 2;
    } else if (align == Align::Right) {
        x += width - w;
    }
    return text(font, x, y, fitted, color);
}

int Canvas::textWidth(const Font& font, const std::string& utf8) {
    int width = 0;
    for (size_t i = 0; i < utf8.size();) {
        const uint8_t* bits = nullptr;
        width += glyphFor(font, nextCodePoint(utf8, i), &bits).advance;
    }
    return width;
}

std::string Canvas::ellipsize(const Font& font, const std::string& utf8, int width) {
    if (textWidth(font, utf8) <= width) {
        return utf8;
    }
    std::string out = utf8;
    while (!out.empty()) {
        // Drop the last UTF-8 character.
        size_t cut = out.size() - 1;
        while (cut > 0 && (static_cast<uint8_t>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.erase(cut);
        if (textWidth(font, out + "...") <= width) {
            return out + "...";
        }
    }
    return std::string();
}

void Canvas::image(int x, int y, const Image& img) {
    imageScaled(x, y, img, 1);
}

void Canvas::imageScaled(int x, int y, const Image& img, int scale) {
    if (img.pixels == nullptr) {
        return;
    }
    if (scale == 1) {
        // Only the part of the image inside the clip rectangle is visited.
        const int ix0 = std::max(0, clipX0_ - x);
        const int ix1 = std::min(img.width, clipX1_ - x);
        const int iy0 = std::max(0, clipY0_ - y);
        const int iy1 = std::min(img.height, clipY1_ - y);
        for (int iy = iy0; iy < iy1; ++iy) {
            const uint16_t* src = img.pixels + iy * img.width;
            uint16_t* row = pixels_ + (y + iy) * kScreenWidth;
            for (int ix = ix0; ix < ix1; ++ix) {
                if (src[ix] & 0x8000) row[x + ix] = src[ix];
            }
        }
        return;
    }
    for (int iy = 0; iy < img.height; ++iy) {
        for (int ix = 0; ix < img.width; ++ix) {
            uint16_t p = img.pixels[iy * img.width + ix];
            if (!(p & 0x8000)) {
                continue;
            }
            fillRect(x + ix * scale, y + iy * scale, scale, scale, p);
        }
    }
}

void Canvas::setClip(int x, int y, int w, int h) {
    clipX0_ = std::max(0, x);
    clipY0_ = std::max(0, y);
    clipX1_ = std::min(kScreenWidth, x + w);
    clipY1_ = std::min(kScreenHeight, y + h);
}

void Canvas::resetClip() {
    setClip(0, 0, kScreenWidth, kScreenHeight);
}

}  // namespace pktool::gfx
