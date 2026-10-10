#!/usr/bin/env python3
"""Build assets/nitrofs/fonts/cjk.bin, the Japanese/Korean fallback font.

Source fonts (both SIL Open Font License 1.1, no Reserved Font Name):
  * Ark Pixel Font 12px monospaced, ja variant, by TakWolf: kana, kanji, symbols
    (https://github.com/TakWolf/ark-pixel-font, "ark-pixel-font-12px-monospaced-bdf").
  * Galmuri11 by Lee Minseo: Hangul syllables, which Ark Pixel does not have
    (https://github.com/quiple/galmuri, "Galmuri-v*.zip").
Download each zip into its own EMPTY directory, unzip it there and pass the two
.bdf files (treat them as data only).

Only the glyphs PKTool can meet are converted:
  * every non-Latin-1 character of the Gen III (international + Japanese),
    Gen IV (international + Korean) character tables and of the Japanese,
    Korean and European name lists that gen_data.py packs,
  * hiragana, katakana, CJK/full-width punctuation and forms, half-width
    katakana, Hangul compatibility jamo,
  * the 2350 KS X 1001 Hangul syllables (Gen V Korean nicknames are UTF-16),
  * the 2965 JIS X 0208 level 1 kanji (Gen V Japanese nicknames/OT names).

File format "PKC1" (little endian), read by ds/gfx/cjk_font.cpp:
    "PKC1", u16 count, u8 cellW, u8 cellH, u8 ascent, u8 reserved,
    u16 codepoints[count] (ascending), u8 advance[count],
    count * 18 bytes: the 12x12 cell, row by row, 12 bits per row, LSB first.

Usage:
    python3 -I tools/gen_cjk_font.py --pkhex /path/to/PKHeX.Core \
        --ja ark-pixel-12px-monospaced-ja.bdf --ko Galmuri11.bdf
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_data  # noqa: E402  (shares the PKHeX table parsers)

CELL = 12
ASCENT = 10
KO_ASCENT = 11  # Galmuri11 syllables are 11 rows tall and sit on the baseline
LANGS = ("ja", "fr", "it", "de", "es", "ko")


def parse_bdf(path: Path, ascent: int = ASCENT) -> dict[int, tuple[int, list[int]]]:
    """code point -> (advance, 12 rows of 12 bits, bit 0 = leftmost pixel)."""
    glyphs = {}
    enc = adv = bbx = rows = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("ENCODING "):
            enc = int(line.split()[1])
        elif line.startswith("DWIDTH "):
            adv = int(line.split()[1])
        elif line.startswith("BBX "):
            bbx = tuple(int(v) for v in line.split()[1:5])
        elif line == "BITMAP":
            rows = []
        elif line == "ENDCHAR":
            if enc is not None and 0 <= enc < 0x10000 and rows is not None and bbx and adv:
                w, h, xo, yo = bbx
                cell = [0] * CELL
                top = ascent - (yo + h)
                for i, hexrow in enumerate(rows):
                    r = top + i
                    if not 0 <= r < CELL:
                        continue
                    nbits = 4 * len(hexrow)
                    value = int(hexrow, 16)
                    for b in range(w):
                        if (value >> (nbits - 1 - b)) & 1 and 0 <= xo + b < CELL:
                            cell[r] |= 1 << (xo + b)
                glyphs[enc] = (min(adv, CELL), cell)
            enc = adv = bbx = rows = None
        elif rows is not None:
            rows.append(line.strip())
    return glyphs


def needed_chars(core: Path) -> set[int]:
    chars: set[int] = set()
    s4 = (core / "PKM" / "Strings" / "StringConverter4Util.cs").read_text(encoding="utf-8")
    for table in ("TableINT", "TableKOR"):
        chars.update(gen_data.parse_char_table(s4, table))
    s3 = (core / "PKM" / "Strings" / "StringConverter3.cs").read_text(encoding="utf-8")
    s3 = s3.replace("FGM", "HGM").replace("FGF", "HGF")
    for table in ("G3_EN", "G3_JP"):
        chars.update(gen_data.parse_char_table(s3, table))
    text = core / "Resources" / "text"
    for lang in LANGS:
        for name in ("Species", "Moves", "Abilities", "Natures"):
            chars.update(map(ord, (text / "other" / lang / f"text_{name}_{lang}.txt").read_text(encoding="utf-8-sig")))
        chars.update(map(ord, (text / "items" / f"text_Items_{lang}.txt").read_text(encoding="utf-8-sig")))
        g3 = text / "items" / "gen3" / f"text_ItemsG3_{lang}.txt"
        if g3.exists():
            chars.update(map(ord, gen_data.read_text_auto(g3)))
    for lo, hi in [(0x3000, 0x303F), (0x3041, 0x3096), (0x30A1, 0x30FA), (0x30FC, 0x30FF), (0x3131, 0x3163),
                   (0xFF01, 0xFF5E), (0xFF61, 0xFF9F)]:
        chars.update(range(lo, hi + 1))
    for row in range(0xB0, 0xC9):  # KS X 1001 Hangul syllables
        for cell in range(0xA1, 0xFF):
            try:
                chars.add(ord(bytes([row, cell]).decode("euc-kr")))
            except UnicodeDecodeError:
                pass
    for lead in range(0x88, 0x99):  # JIS X 0208 level 1 kanji (0x889F-0x9872)
        for trail in range(0x40, 0xFD):
            if (lead, trail) < (0x88, 0x9F) or (lead, trail) > (0x98, 0x72):
                continue
            try:
                chars.add(ord(bytes([lead, trail]).decode("shift_jis")))
            except UnicodeDecodeError:
                pass
    # Latin-1 comes from the Latin fonts; the gender signs are hand drawn.
    return {c for c in chars if 0xFF < c < 0xFFFF and c not in (0x2642, 0x2640, 0x246D, 0x246E)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pkhex", required=True, type=Path)
    parser.add_argument("--ja", required=True, type=Path)
    parser.add_argument("--ko", required=True, type=Path)
    parser.add_argument("--out", type=Path,
                        default=Path(__file__).resolve().parent.parent / "assets" / "nitrofs" / "fonts" / "cjk.bin")
    args = parser.parse_args()

    ja, ko = parse_bdf(args.ja), parse_bdf(args.ko, KO_ASCENT)
    chosen, missing = {}, []
    for cp in sorted(needed_chars(args.pkhex)):
        hangul = 0x1100 <= cp <= 0x11FF or 0x3130 <= cp <= 0x318F or 0xAC00 <= cp <= 0xD7AF
        first, second = (ko, ja) if hangul else (ja, ko)
        glyph = first.get(cp) or second.get(cp)
        if glyph is None:
            missing.append(cp)
        else:
            chosen[cp] = glyph
    cps = sorted(chosen)
    out = bytearray(b"PKC1")
    out += struct.pack("<HBBBB", len(cps), CELL, CELL, ASCENT, 0)
    out += struct.pack(f"<{len(cps)}H", *cps)
    out += bytes(chosen[c][0] for c in cps)
    for c in cps:
        bits = 0
        for r, row in enumerate(chosen[c][1]):
            bits |= row << (r * CELL)
        out += bits.to_bytes(CELL * CELL // 8, "little")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f"{args.out}: {len(cps)} glyphs, {len(out)} bytes")
    if missing:
        print("not in the font (left out): " + " ".join(f"U+{c:04X}" for c in missing[:60]) +
              (f" ... ({len(missing)})" if len(missing) > 60 else ""))


if __name__ == "__main__":
    main()
