#!/usr/bin/env python3
"""Collect the distribution periods of Gen IV and Gen V event Pokémon.

PKHeX's Mystery Gift database has the Wonder Cards but not when they were
handed out. Bulbapedia's event distribution lists have the periods; this
script downloads them (raw wikitext), extracts every {{G4event}} and
{{G5event}} entry and writes tools/event_dates.csv, one row per period:

    gen,language,species,ot,tid,level,start,end,event

tools/gen_data.py matches the rows to the Wonder Cards (by species, language,
trainer and level) and compiles the periods next to the cards. The CSV is
committed, so regenerating the data tables does not need network access.

Bulbapedia (https://bulbapedia.bulbagarden.net) text is CC BY-NC-SA 2.5; only
the distribution dates and the matching keys are kept.

Usage:
    python3 tools/fetch_event_dates.py [--cache DIR] [--out tools/event_dates.csv]
"""

import argparse
import csv
import re
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
from datetime import date, timedelta
from pathlib import Path

API = "https://bulbapedia.bulbagarden.net/w/index.php?title={}&action=raw"
USER_AGENT = "PKTool event-date collector (https://github.com/pioacosta/PKTool)"

# Page language -> PKHeX language ID (as in the Wonder Cards).
LANGUAGES = {"Japanese": 1, "English": 2, "French": 3, "Italian": 4, "German": 5, "Spanish": 7, "Korean": 8}
GENERATIONS = {4: "IV", 5: "V"}
KINDS = ["local", "Wi-Fi"]

MONTHS = {m: i + 1 for i, m in enumerate(
    ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
     "November", "December"])}


def page_titles() -> list[tuple[int, int, str]]:
    titles = []
    for gen, roman in GENERATIONS.items():
        for kind in KINDS:
            for name, lang in LANGUAGES.items():
                titles.append((gen, lang, f"List of {kind} {name} event Pokémon distributions in Generation {roman}"))
    return titles


def fetch(title: str, cache: Path | None) -> str | None:
    path = cache / (re.sub(r"[^A-Za-z0-9-]+", "_", title) + ".txt") if cache else None
    if path and path.exists():
        return path.read_text(encoding="utf-8")
    url = API.format(urllib.parse.quote(title.replace(" ", "_")))
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            text = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise
    time.sleep(1)  # be gentle with the wiki
    if not text.strip():
        return None
    if path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return text


def templates(text: str, names: tuple[str, ...]):
    """Yields (name, body) for each top-level {{name ...}} template, braces balanced."""
    pattern = re.compile(r"\{\{(" + "|".join(names) + r")\s*\|")
    pos = 0
    while (match := pattern.search(text, pos)) is not None:
        depth, i = 0, match.start()
        while i < len(text):
            if text.startswith("{{", i):
                depth += 1
                i += 2
            elif text.startswith("}}", i):
                depth -= 1
                i += 2
                if depth == 0:
                    break
            else:
                i += 1
        yield match.group(1), match.start(), text[match.end():i - 2]
        pos = i


def split_params(body: str) -> dict[str, str]:
    """Splits a template body on top-level '|' into name=value parameters."""
    parts, depth, start = [], 0, 0
    i = 0
    while i < len(body):
        if body.startswith(("{{", "[["), i):
            depth += 1
            i += 2
        elif body.startswith(("}}", "]]"), i):
            depth -= 1
            i += 2
        else:
            if body[i] == "|" and depth == 0:
                parts.append(body[start:i])
                start = i + 1
            i += 1
    parts.append(body[start:])
    params = {}
    for part in parts:
        if "=" in part:
            key, value = part.split("=", 1)
            params[key.strip()] = value.strip()
    return params


def tooltip(text: str, visible: bool) -> str:
    """Replaces {{tt|shown|hidden}} by the shown (visible=True) or the hidden part."""
    def repl(match: re.Match) -> str:
        args = match.group(1).split("|")
        return args[0] if visible or len(args) < 2 else args[1]
    while (new := re.sub(r"\{\{tt2?\|([^{}]*)\}\}", repl, text)) != text:
        text = new
    return text


def plain(text: str) -> str:
    text = re.sub(r"<br\s*/?>", ", ", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"\[\[(?:[^|\]]*\|)?([^\]]*)\]\]", r"\1", text)
    text = re.sub(r"\{\{color\|[^|]*\|([^{}]*)\}\}", r"\1", text)
    return text


TOKEN = re.compile(r"(" + "|".join(MONTHS) + r")|(\d{4})|(\d{1,2})(?:st|nd|rd|th)?\b|\b(to|through)\b|(–|-)")


def parse_periods(text: str, default_year: int | None) -> list[tuple[date, date]]:
    """Distribution periods written like 'March 3 to April 3, 2008; May 4, 2009'.

    Days without a month take the previous month; dates without a year take
    the next year written (or `default_year`). Returns [] when no day is given.
    """
    atoms = []  # [month, day, year, joined-to-previous-by-'to']
    joined = False
    month = None
    for m in TOKEN.finditer(text):
        name, year, day, to, dash = m.groups()
        if name:
            month = MONTHS[name]
        elif year:
            for atom in reversed(atoms):
                if atom[2] is not None:
                    break
                atom[2] = int(year)
        elif day and month:
            atoms.append([month, int(day), None, joined])
            joined = False
        elif to or dash:
            joined = bool(atoms)
    for atom in atoms:
        if atom[2] is None:
            atom[2] = default_year
    periods = []
    for i, (month, day, year, to_prev) in enumerate(atoms):
        if year is None:
            return []
        try:
            current = date(year, month, day)
        except ValueError:
            return []
        if to_prev and periods:
            start = periods[-1][0]
            if current < start and start.year == current.year:
                start = start.replace(year=start.year - 1)  # "December 6 to January 12, 2009"
            periods[-1] = (start, current)
        else:
            periods.append((current, current))
    return periods if all(a <= b for a, b in periods) else []


def merge(periods: list[tuple[date, date]]) -> list[tuple[date, date]]:
    out = []
    for start, end in sorted(set(periods)):
        if out and start <= out[-1][1] + timedelta(days=1):
            out[-1] = (out[-1][0], max(out[-1][1], end))
        else:
            out.append((start, end))
    return out


def normalize(text: str) -> str:
    return unicodedata.normalize("NFKC", plain(tooltip(text, True))).strip()


def heading_before(text: str, pos: int) -> str:
    headings = re.findall(r"^===([^=].*?)===\s*$", text[:pos], re.MULTILINE)
    return headings[-1].strip() if headings else ""


def extract(gen: int, lang: int, text: str, species_ids: dict[str, int]) -> tuple[list[list], list[str]]:
    rows, skipped = [], []
    for _, pos, body in templates(text, (f"G{gen}event",)):
        p = split_params(body)
        event = heading_before(text, pos)
        species = species_ids.get(normalize(p.get("pokemon", "")).lower())
        if species is None:
            skipped.append(f"{event}: unknown species {p.get('pokemon')!r}")
            continue
        datedis = plain(p.get("datedis", ""))
        years = re.findall(r"\b(20\d\d)\b", tooltip(datedis, True))
        default_year = int(years[-1]) if years else None
        periods = parse_periods(tooltip(datedis, True), default_year)
        if not periods:  # "2011" with the periods per country
            periods = parse_periods(tooltip(plain(p.get("country", "")), False), default_year)
        if not periods:
            skipped.append(f"{event}: no dates in {p.get('datedis')!r}")
            continue
        ot = normalize(p.get("ot", ""))
        tid = normalize(p.get("id", ""))
        if not re.fullmatch(r"\d{5}", tid):
            ot, tid = "", ""  # the receiving trainer's (eggs, "Various")
        level = normalize(p.get("level", ""))
        level = level if level.isdigit() else ""
        for start, end in merge(periods):
            rows.append([gen, lang, species, ot, tid, level, start.isoformat(), end.isoformat(), event])
    return rows, skipped


def species_table() -> dict[str, int]:
    """English species names -> national dex numbers (PokeAPI-free: from the generated names)."""
    names_cpp = Path(__file__).resolve().parent.parent / "core" / "data" / "generated" / "names_en.cpp"
    text = names_cpp.read_text(encoding="ascii")
    block = re.search(r"kSpeciesNames\w*\[\]\s*=\s*\{(.*?)\};", text, re.DOTALL)
    if block is None:
        raise SystemExit(f"{names_cpp}: species table not found")
    names = [bytes(s, "ascii").decode("unicode_escape").encode("latin-1").decode("utf-8")
             for s in re.findall(r'"((?:[^"\\]|\\.)*)"', block.group(1))]
    table = {unicodedata.normalize("NFKC", n).lower(): i for i, n in enumerate(names) if n}
    table.update({"nidoran♀": 29, "nidoran♂": 32, "farfetch'd": 83, "mr. mime": 122, "mime jr.": 439})
    return table


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cache", type=Path, help="directory to keep the downloaded pages in")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent / "event_dates.csv")
    parser.add_argument("--verbose", action="store_true", help="list the entries without usable dates")
    args = parser.parse_args()

    species_ids = species_table()
    rows, skipped = [], []
    for gen, lang, title in page_titles():
        text = fetch(title, args.cache)
        if text is None:
            continue
        page_rows, page_skipped = extract(gen, lang, text, species_ids)
        rows += page_rows
        skipped += [f"{title}: {s}" for s in page_skipped]
        print(f"{title}: {len(page_rows)} periods", file=sys.stderr)
    rows.sort(key=lambda r: (r[0], r[1], r[2], r[4], r[6]))
    with args.out.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["gen", "language", "species", "ot", "tid", "level", "start", "end", "event"])
        writer.writerows(rows)
    print(f"{args.out}: {len(rows)} periods, {len(skipped)} entries without usable dates", file=sys.stderr)
    if args.verbose:
        print("\n".join(skipped), file=sys.stderr)


if __name__ == "__main__":
    main()
