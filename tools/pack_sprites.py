#!/usr/bin/env python3
"""Pack the sprite files of tools/fetch_sprites.py into one file per kind.

    python3 tools/pack_sprites.py assets/nitrofs/sprites [--remove-loose]

Reads <dir>/<kind>/<species>.bin and writes <dir>/<kind>.pak. An existing pack
is kept: the loose files are added to it (replacing the same species), so a
single sprite can be updated with e.g. `fetch_sprites.py --egg-only` + this.
Pack layout:
    "PKSP", u16 count, u16 0, u32 offsets[count + 2] (from the start of the
    file; species s is bytes offsets[s]..offsets[s + 1], empty if equal), then
    the PKS1 blobs, all little endian.
The DS keeps a pack open and seeks to a sprite instead of looking it up among
~650 files in a directory, which is slow on flashcarts.
"""
import argparse
import shutil
import struct
from pathlib import Path

KINDS = ("icons", "front", "front-shiny")


def unpack(pak: Path) -> dict:
    data = pak.read_bytes()
    if data[:4] != b"PKSP":
        raise SystemExit(f"{pak}: not a sprite pack")
    count = struct.unpack_from("<H", data, 4)[0]
    offsets = struct.unpack_from(f"<{count + 2}I", data, 8)
    return {s: data[offsets[s]:offsets[s + 1]] for s in range(count + 1) if offsets[s + 1] > offsets[s]}


def pack(folder: Path, out: Path) -> int:
    species = unpack(out) if out.exists() else {}
    species.update({int(p.stem): p.read_bytes() for p in folder.glob("*.bin") if p.stem.isdigit()})
    count = max(species) if species else 0
    header = 8 + 4 * (count + 2)
    offsets, blobs, at = [], bytearray(), header
    for s in range(count + 1):
        offsets.append(at)
        blob = species.get(s, b"")
        blobs += blob
        at += len(blob)
    offsets.append(at)
    out.write_bytes(b"PKSP" + struct.pack("<HH", count, 0) + struct.pack(f"<{count + 2}I", *offsets) + blobs)
    return len(species)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dir", type=Path)
    parser.add_argument("--remove-loose", action="store_true", help="delete <kind>/ folders after packing")
    args = parser.parse_args()
    for kind in KINDS:
        folder = args.dir / kind
        if not folder.is_dir():
            continue
        n = pack(folder, args.dir / f"{kind}.pak")
        print(f"{kind}.pak: {n} sprites")
        if args.remove_loose:
            shutil.rmtree(folder)


if __name__ == "__main__":
    main()
