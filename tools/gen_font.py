#!/usr/bin/env python3
"""Convert a pixel TTF/WOFF font into a compact 1-bit bitmap font for the DS UI.

Usage:
    python3 tools/gen_font.py --font jersey-15.woff --size 15 --name kFontSmall \
        --out ds/gfx/generated/font_small.cpp

The fonts used by PKTool come from Google Fonts / Fontsource and are licensed
under the SIL Open Font License 1.1 (see third_party/fonts/OFL.txt):
  - Jersey 15  (UI text, 15px)  https://fontsource.org/fonts/jersey-15
  - Jersey 10  (titles, 20px)   https://fontsource.org/fonts/jersey-10
Pixel fonts must be rendered at a multiple of their design grid (e.g. 15px
for Jersey 15), otherwise strokes come out uneven on the DS screen.
Get them with `npm pack @fontsource/jersey-15 @fontsource/jersey-10`; the
latin-400-normal .woff files are inside the tarballs.

The small UI font is generated with --overrides tools/font_overrides.py: the
letters, digits and a few capitals are redrawn by hand (the original design
has blobby counters at this size) and accented Latin-1 letters are composed
from them. The hand-drawn glyphs are original work released under the same
licence as the project; everything else is Jersey 15. The large font is
generated without overrides (its committed table predates the advance
rounding tweak in render_glyph, so regenerating it changes some advances).

Requires Pillow and fontTools.
"""

import argparse
import io
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

FIRST = 32
LAST = 255  # printable ASCII and Latin-1 (accented letters used in names)


def load_font(path: Path, size: int) -> ImageFont.FreeTypeFont:
    font = TTFont(str(path))
    font.flavor = None  # WOFF/WOFF2 -> plain TTF
    buffer = io.BytesIO()
    font.save(buffer)
    buffer.seek(0)
    return ImageFont.truetype(buffer, size)


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, ascent: int):
    """Returns (advance, x_offset, y_offset, width, height, rows) for one glyph.

    y_offset is relative to the top of the line (baseline = ascent)."""
    # Recent FreeType/Pillow versions report advances one pixel wider than the
    # ones this table was first generated with; subtract it (but never let a
    # glyph overlap the next one).
    advance = int(round(font.getlength(ch)))
    canvas = Image.new("1", (64, 64), 0)
    draw = ImageDraw.Draw(canvas)
    draw.fontmode = "1"
    origin_x, origin_y = 16, 16
    draw.text((origin_x, origin_y), ch, font=font, fill=1)
    bbox = canvas.getbbox()
    if bbox is None:
        return advance, 0, 0, 0, 0, []
    left, top, right, bottom = bbox
    rows = []
    for y in range(top, bottom):
        bits = 0
        for x in range(left, right):
            if canvas.getpixel((x, y)):
                bits |= 1 << (x - left)
        rows.append(bits)
    if left > origin_x:
        advance -= 1
    advance = max(advance, left - origin_x + right - left)
    return advance, left - origin_x, top - origin_y, right - left, bottom - top, rows


def rows_to_pixels(width, rows, x_off, y_off):
    return {(x_off + x, y_off + y) for y, bits in enumerate(rows) for x in range(width) if bits >> x & 1}


def pixels_to_glyph(pixels, advance=None):
    """Packs a pixel set into (advance, x_off, y_off, width, height, rows)."""
    if not pixels:
        return (advance or 0, 0, 0, 0, 0, [])
    left = min(x for x, _ in pixels)
    right = max(x for x, _ in pixels)
    top = min(y for _, y in pixels)
    bottom = max(y for _, y in pixels)
    rows = []
    for y in range(top, bottom + 1):
        rows.append(sum(1 << (x - left) for x in range(left, right + 1) if (x, y) in pixels))
    width = right - left + 1
    return (advance if advance is not None else left + width, left, top, width, bottom - top + 1, rows)


def art_to_glyph(top, lines):
    """Hand-drawn glyph: x_off 1 and a 1px gap, like the rest of the font."""
    width = len(lines[0])
    rows = [sum(1 << x for x, c in enumerate(line) if c == "#") for line in lines]
    return (width + 1, 1, top, width, len(lines), rows)


def load_overrides(path: Path):
    namespace = {}
    exec(compile(path.read_text(encoding="utf-8"), str(path), "exec"), namespace)
    return namespace


def apply_overrides(glyphs, spec):
    """Applies the hand-drawn glyphs, then composes accented Latin-1 letters
    from the (possibly overridden) base letter and an accent mark."""
    import unicodedata

    glyphs = dict(glyphs)
    for ch, (top, lines) in spec["GLYPHS"].items():
        glyphs[ord(ch)] = art_to_glyph(top, lines)
    marks = spec["MARKS"]  # combining code point -> (lines, top row offset, "upper"/"lower"/"below")
    for cp in range(160, LAST + 1):
        decomposition = unicodedata.decomposition(chr(cp)).split()
        if len(decomposition) != 2 or decomposition[0].startswith("<"):
            continue
        base_cp, mark_cp = int(decomposition[0], 16), int(decomposition[1], 16)
        if mark_cp not in marks or base_cp not in glyphs:
            continue
        base = glyphs[base_cp]
        lower = chr(base_cp).islower()
        if chr(base_cp) == "i":
            base = glyphs[spec["DOTLESS_I"][0]] if False else dotless_i(base)
        mark_lines, kind = marks[mark_cp]
        mark_width = len(mark_lines[0])
        base_pixels = rows_to_pixels(base[3], base[5], base[1], base[2])
        total = max(base[3], mark_width)
        base_shift = (total - base[3]) // 2
        mark_x = base[1] + (total - mark_width) // 2
        pixels = {(x + base_shift, y) for x, y in base_pixels}
        if kind == "below":
            mark_top = spec["BASELINE"]
        else:
            mark_top = spec["LOWER_MARK_TOP"] if lower else spec["UPPER_MARK_TOP"]
        for y, line in enumerate(mark_lines):
            for x, c in enumerate(line):
                if c == "#":
                    pixels.add((mark_x + x, mark_top + y))
        glyph = pixels_to_glyph(pixels)
        glyphs[cp] = (total + 1, glyph[1], glyph[2], glyph[3], glyph[4], glyph[5])
    return glyphs


def dotless_i(base):
    """Drops the dot of an 'i' (its first row) so an accent can replace it."""
    advance, x_off, y_off, width, height, rows = base
    return (advance, x_off, y_off + 2, width, height - 2, rows[2:])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--size", required=True, type=int)
    parser.add_argument("--name", required=True, help="C++ variable name, e.g. kFontSmall")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--line-height", type=int, help="override the line height")
    parser.add_argument("--overrides", type=Path, help="python file with hand-drawn glyphs (see tools/font_overrides.py)")
    parser.add_argument("--license", default="SIL Open Font License 1.1", help="font licence note")
    args = parser.parse_args()

    font = load_font(args.font, args.size)
    ascent, descent = font.getmetrics()

    # Render every glyph, then normalise vertical offsets so capital letters
    # start at row 2 of the line; taller glyphs (accented capitals) get small
    # negative offsets.
    raw = {}
    for cp in range(FIRST, LAST + 1):
        if 127 <= cp < 160:
            raw[cp] = (0, 0, 0, 0, 0, [])
            continue
        glyph = render_glyph(font, chr(cp), ascent)
        if glyph[3] > 16:
            raise SystemExit(f"glyph {chr(cp)!r} is wider than 16 pixels")
        raw[cp] = glyph
    _, _, cap_top, _, _, _ = render_glyph(font, "H", ascent)
    shift = cap_top - 2
    glyphs = {}
    for cp, (advance, x_off, y_off, width, height, rows) in raw.items():
        glyphs[cp] = (advance, x_off, y_off - shift if height else 0, width, height, rows)
    baseline = ascent - shift

    if args.overrides:
        glyphs = apply_overrides(glyphs, load_overrides(args.overrides))

    glyph_lines = []
    bitmap = []
    bottom_max = 0
    for cp in range(FIRST, LAST + 1):
        advance, x_off, y_off, width, height, rows = glyphs[cp]
        offset = len(bitmap) if not 127 <= cp < 160 else 0
        for row in rows:
            bitmap.append(row & 0xFF)
            bitmap.append(row >> 8)
        if height:
            bottom_max = max(bottom_max, y_off + height)
        comment = f"  // {cp:#04x}" if not 127 <= cp < 160 else ""
        glyph_lines.append(f"    {{{offset}, {advance}, {x_off}, {y_off}, {width}, {height}}},{comment}")
    fixed = glyph_lines
    line_height = args.line_height or (bottom_max + 1)

    bits = []
    for i in range(0, len(bitmap), 16):
        bits.append("    " + ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 16]) + ",")

    content = f"""// AUTO-GENERATED by tools/gen_font.py - do not edit by hand.
// Font: {args.font.name} at {args.size}px. {args.license}; see third_party/fonts/.

#include "ds/gfx/font.h"

namespace pktool::gfx {{
namespace {{

const Glyph kGlyphs[] = {{
{chr(10).join(fixed)}
}};

// Glyph rows, 16 bits per row (little endian), bit 0 = leftmost pixel.
const uint8_t kBits[] = {{
{chr(10).join(bits)}
}};

}}  // namespace

const Font {args.name} = {{kGlyphs, kBits, {FIRST}, {LAST}, {baseline}, {line_height}}};

}}  // namespace pktool::gfx
"""
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(content, encoding="ascii")
    print(f"{args.out}: baseline {baseline}, line height {line_height}, {len(bitmap)} bytes of bitmap")


if __name__ == "__main__":
    main()
