#!/usr/bin/env python3
"""Build PKTool's optional sprite pack on a PC.

This script downloads sprites from the PokeAPI sprites repository
(https://github.com/PokeAPI/sprites) and converts them to PKTool's raw format.
The pack committed in assets/nitrofs/sprites (packed into PKTool.nds) was
built with:

    python3 tools/fetch_sprites.py --out assets/nitrofs/sprites

Creates:
    icons/<species>.bin        box icons (Gen VII style, 40x30)
    front/<species>.bin        summary sprites (Platinum; Black/White for 494-649)
    front-shiny/<species>.bin  shiny summary sprites

Species 0 is the egg (icons: Gen VII, front: the Gen IV egg of HeartGold/SoulSilver
- PokeAPI has none for Platinum - at its original size, also used for front-shiny). Without --max-species it is always fetched;
`--egg-only` fetches just it.

File format: "PKS1", u16 width, u16 height, then width*height u16 pixels in
the DS bitmap format (bit 15 = opaque, 5 bits each of blue, green, red),
little endian. Requires Pillow.
"""

import argparse
import io
import struct
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image

BASE = "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon"
SOURCES = {
    "icons": BASE + "/versions/generation-vii/icons/{n}.png",
    "front": BASE + "/versions/generation-iv/platinum/{n}.png",
    "front-shiny": BASE + "/versions/generation-iv/platinum/shiny/{n}.png",
}
# Unova species (494-649) have no Platinum sprites: Black/White ones instead.
SOURCES_GEN5 = {
    "icons": SOURCES["icons"],
    "front": BASE + "/versions/generation-v/black-white/{n}.png",
    "front-shiny": BASE + "/versions/generation-v/black-white/shiny/{n}.png",
}


EGG = {
    "icons": (BASE + "/versions/generation-vii/icons/egg.png", 1),
    "front": (BASE + "/versions/generation-iv/heartgold-soulsilver/egg.png", 1),
    "front-shiny": (BASE + "/versions/generation-iv/heartgold-soulsilver/egg.png", 1),
}


def to_pks(png: bytes, scale: int = 1) -> bytes:
    image = Image.open(io.BytesIO(png)).convert("RGBA")
    # Crop fully transparent borders so sprites can be centred precisely.
    bbox = image.getbbox()
    if bbox:
        image = image.crop(bbox)
    if scale > 1:
        image = image.resize((image.width * scale, image.height * scale), Image.NEAREST)
    width, height = image.size
    out = bytearray(b"PKS1" + struct.pack("<HH", width, height))
    rgba = image.tobytes()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i + 4]
        value = 0
        if a >= 128:
            value = 0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
        out += struct.pack("<H", value)
    return bytes(out)


def fetch_egg(kind: str, out: Path) -> str:
    url, scale = EGG[kind]
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read()
    except Exception as error:  # noqa: BLE001 - report and continue
        return f"failed ({error})"
    (out / kind / "0.bin").write_bytes(to_pks(data, scale))
    return "ok"


def fetch(kind: str, species: int, out: Path) -> str:
    target = out / kind / f"{species}.bin"
    if target.exists():
        return "cached"
    url = (SOURCES if species <= 493 else SOURCES_GEN5)[kind].format(n=species)
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read()
    except Exception as error:  # noqa: BLE001 - report and continue
        return f"failed ({error})"
    target.write_bytes(to_pks(data))
    return "ok"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", required=True, type=Path, help="destination, e.g. <SD>/PKTOOL/sprites")
    parser.add_argument("--max-species", type=int, default=649, help="highest species number (649 = Gen V)")
    parser.add_argument("--kinds", default="icons,front,front-shiny", help="comma-separated sprite kinds")
    parser.add_argument("--egg-only", action="store_true", help="only (re)fetch the egg sprites (species 0)")
    args = parser.parse_args()

    kinds = [k for k in args.kinds.split(",") if k]
    for kind in kinds:
        if kind not in SOURCES:
            sys.exit(f"unknown kind {kind!r}")
        (args.out / kind).mkdir(parents=True, exist_ok=True)

    for kind in kinds:
        print(f"{kind}/0 (egg): {fetch_egg(kind, args.out)}")
    if args.egg_only:
        return
    jobs = [(kind, n) for kind in kinds for n in range(1, args.max_species + 1)]
    failures = 0
    with ThreadPoolExecutor(max_workers=16) as pool:
        for (kind, n), result in zip(jobs, pool.map(lambda job: fetch(job[0], job[1], args.out), jobs)):
            if result.startswith("failed"):
                failures += 1
                print(f"{kind}/{n}: {result}")
    print(f"done: {len(jobs) - failures} sprites in {args.out} ({failures} failed)")
    print("pack them for the DS: python3 tools/pack_sprites.py", args.out, "--remove-loose")


if __name__ == "__main__":
    main()
