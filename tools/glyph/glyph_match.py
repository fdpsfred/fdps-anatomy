"""Match every glyph of FDETXT.FON against the ETen (倚天) STDFONT.15 font.

Adapted from fd2-anatomy/tools/glyph/et3_pixel_match.py (the ET3 index layout
and the idea) and fd2-anatomy/resource_info/chinese_glyph_encoding.md (why the
layout is not the linear Big5 formula).  What differs from the FD2 script:

- The glyph sheet is FDETXT.FON out of FIELD.VFS: 1792 glyphs, 16x16 1bpp, 32
  bytes each, rows MSB first, no header (src/main.c sets the 16x16 cell and the
  32-byte stride; src/text.c walks the bits).
- "Exact" means the whole 16x16 cell equals an ET3 15-row glyph placed at the
  top or one row down, with the remaining row blank.  The FD2 script dropped a
  row before comparing and so could call a glyph exact while the dropped row
  still had pixels in it.
- Candidates for a glyph that is not exact also come from FD2's own reviewed
  glyph table: the nearest FD2 FDOTHER.DAT[4] glyph and the character FD2 gave
  it.  That is a hint for the human filling in the review page, never an
  answer by itself.

ET3 STDFONT.15 layout (fd2-anatomy verified it by bitmap):
    idx 0..5400       Big5 0xA440..0xC67E  (lead 0xA4..0xC5 x157, 0xC6 x63)
    idx 5401..13052   Big5 0xC940..0xF9D5  (lead 0xC9..0xF8 x157, 0xF9 x116)
    idx 13053..13093  cp950 0xF9D6..0xF9FE (ETEN extension, not in plain Big5)
There is no 0xA1xx..0xA3xx symbol block, so full-width punctuation can never
match exactly.

CLI:
    python glyph_match.py [--font FDETXT.FON] [--fd2 fd2-anatomy root] [--out match.json]
"""

import argparse
import json
import re
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ET3_DIR = Path(__file__).resolve().parent / "ET3_fonts"
DEFAULT_FONT = REPO_ROOT / "workspace" / "vfs_dump" / "FIELD" / "FDETXT.FON"
DEFAULT_FD2 = REPO_ROOT.parent / "fd2-anatomy"
DEFAULT_OUT = REPO_ROOT / "workspace" / "glyph" / "match.json"

GLYPH_BYTES = 32
ROWS = 16
ET3_ROWS = 15
ET3_STD_BYTES = 30
ET3_ASC_BYTES = 15
FD2_SHEET_ENTRY = 4  # FDOTHER.DAT[4]
# FD2 .DAT: the 6-byte "LLLLLL" magic, then a table of u32 member offsets.
FD2_DAT_OFFSET_TABLE = 6
FD2_TABLE_ROW = re.compile(r"^\| `0x([0-9A-Fa-f]{4})` \| (.*?) \|$")
ET3_CANDIDATES = 5
ASC_CANDIDATES = 2


def rows_to_int(rows):
    """Pack 16-bit rows into one integer so a Hamming distance is one XOR."""
    value = 0
    for row in rows:
        value = (value << 16) | row
    return value


def sheet_rows(data, count, row_count, row_bytes):
    out = []
    size = row_count * row_bytes
    for i in range(count):
        chunk = data[i * size:(i + 1) * size]
        if row_bytes == 2:
            out.append(tuple((chunk[r * 2] << 8) | chunk[r * 2 + 1] for r in range(row_count)))
        else:  # ASCFONT: 8 pixels a row, left-aligned in a 16-pixel row
            out.append(tuple(chunk[r] << 8 for r in range(row_count)))
    return out


def et3_big5_layout(count):
    codes = []
    tails = list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF))
    for lead in range(0xA4, 0xC6):
        codes += [(lead << 8) | t for t in tails]
    codes += [0xC600 | t for t in range(0x40, 0x7F)]
    for lead in range(0xC9, 0xF9):
        codes += [(lead << 8) | t for t in tails]
    codes += [0xF900 | t for t in range(0x40, 0x7F)]
    codes += [0xF900 | t for t in range(0xA1, 0xFF)]
    if len(codes) != count:
        raise ValueError(f"ET3 layout has {len(codes)} codes, STDFONT.15 has {count} glyphs")
    return codes


@dataclass
class Et3:
    std: list          # 15-row STDFONT glyphs
    asc: list          # 15-row ASCFONT glyphs, left-aligned in 16 columns
    codes: list        # Big5/cp950 code of each STDFONT index
    placed: list       # (top, one row down) packed 16x16 cells per STDFONT glyph
    asc_placed: list   # the same for ASCFONT
    exact: dict        # packed 16x16 cell -> [STDFONT indices]

    def big5(self, index):
        return self.codes[index]

    def char(self, index):
        return self.codes[index].to_bytes(2, "big").decode("cp950")

    def index_of_big5(self, code):
        return self.codes.index(code)


def load_et3(directory=ET3_DIR):
    std_raw = (Path(directory) / "STDFONT.15").read_bytes()
    asc_raw = (Path(directory) / "ASCFONT.15").read_bytes()
    if len(std_raw) % ET3_STD_BYTES or len(asc_raw) % ET3_ASC_BYTES:
        raise ValueError("ET3 font sizes are not whole glyph counts")
    std = sheet_rows(std_raw, len(std_raw) // ET3_STD_BYTES, ET3_ROWS, 2)
    asc = sheet_rows(asc_raw, len(asc_raw) // ET3_ASC_BYTES, ET3_ROWS, 1)
    placed = _placements(std)
    exact = {}
    for index, pair in enumerate(placed):
        for packed in pair:
            exact.setdefault(packed, []).append(index)
    return Et3(std, asc, et3_big5_layout(len(std)), placed, _placements(asc), exact)


def _placements(glyphs):
    """Each 15-row glyph placed at the top and one row down of a 16-row cell."""
    return [(rows_to_int(g + (0,)), rows_to_int((0,) + g)) for g in glyphs]


def _nearest(packed, placed, limit):
    scored = sorted((min((packed ^ a).bit_count(), (packed ^ b).bit_count()), i)
                    for i, (a, b) in enumerate(placed))
    return scored[:limit]


def match_glyph(rows, et3):
    """Classify one 16x16 glyph against ET3.

    Returns {"blank", "exact" (character or None), "et3_index", "candidates"};
    candidates are only computed when the glyph is not exact."""
    rows = tuple(rows)
    packed = rows_to_int(rows)
    if packed == 0:
        return {"blank": True, "exact": None, "et3_index": None, "candidates": []}
    hits = et3.exact.get(packed, [])
    if len(set(hits)) == 1:
        index = hits[0]
        return {"blank": False, "exact": et3.char(index), "et3_index": index,
                "candidates": [{"source": "et3", "char": et3.char(index),
                                "big5": f"{et3.big5(index):04X}", "distance": 0}]}
    candidates = [{"source": "et3", "char": et3.char(i), "big5": f"{et3.big5(i):04X}", "distance": d}
                  for d, i in _nearest(packed, et3.placed, ET3_CANDIDATES)]
    candidates += [{"source": "et3_ascii", "char": chr(i) if 0x21 <= i < 0x7F else None,
                    "code": i, "distance": d}
                   for d, i in _nearest(packed, et3.asc_placed, ASC_CANDIDATES)]
    candidates.sort(key=lambda c: c["distance"])
    return {"blank": False, "exact": None, "et3_index": None, "candidates": candidates,
            "ambiguous_exact": sorted(set(hits)) or None}


def load_font(path):
    data = Path(path).read_bytes()
    if len(data) % GLYPH_BYTES:
        raise ValueError(f"{path}: {len(data)} bytes is not a whole number of 32-byte glyphs")
    return data, sheet_rows(data, len(data) // GLYPH_BYTES, ROWS, 2)


def load_fd2_reference(fd2_root):
    """FD2's glyph sheet and its reviewed character table, or None if absent."""
    fd2_root = Path(fd2_root)
    dat = fd2_root / "fd2_game_files" / "FDOTHER.DAT"
    table_md = fd2_root / "assets" / "text" / "glyph_table.md"
    if not dat.is_file() or not table_md.is_file():
        return None
    raw = dat.read_bytes()
    start, end = struct.unpack_from("<II", raw, FD2_DAT_OFFSET_TABLE + FD2_SHEET_ENTRY * 4)
    sheet = raw[start:end]
    chars = {}
    for line in table_md.read_text(encoding="utf-8").splitlines():
        m = FD2_TABLE_ROW.match(line)
        if m:
            chars[int(m.group(1), 16)] = m.group(2)
    glyphs = sheet_rows(sheet, len(sheet) // GLYPH_BYTES, ROWS, 2)
    by_bytes = {}
    for i in range(len(glyphs)):
        by_bytes.setdefault(glyph_bytes(sheet, i), i)
    return {"packed": [rows_to_int(g) for g in glyphs], "chars": chars, "by_bytes": by_bytes}


def glyph_bytes(sheet, index):
    return sheet[index * GLYPH_BYTES:(index + 1) * GLYPH_BYTES]


def duplicate_bitmaps(data, count):
    """Groups of glyph indices whose 32 bytes are identical (blank ones included)."""
    groups = {}
    for i in range(count):
        groups.setdefault(glyph_bytes(data, i), []).append(i)
    return [g for g in groups.values() if len(g) > 1]


def fd2_hint(packed, fd2):
    distance, index = min(((packed ^ p).bit_count(), i) for i, p in enumerate(fd2["packed"]))
    return {"index": index, "char": fd2["chars"].get(index), "distance": distance}


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--font", default=DEFAULT_FONT)
    parser.add_argument("--fd2", default=DEFAULT_FD2, help="fd2-anatomy checkout, for hints and the cross-check")
    parser.add_argument("--out", default=DEFAULT_OUT)
    args = parser.parse_args(argv)
    try:
        return run(args)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


def run(args):
    data, glyphs = load_font(args.font)
    et3 = load_et3()
    fd2 = load_fd2_reference(args.fd2)
    if fd2 is None:
        print(f"warning: no FD2 reference under {args.fd2}; hints and cross-check skipped")

    records = []
    histogram = Counter()
    fd2_same_bytes = fd2_agree = 0
    fd2_disagree = []
    for index, rows in enumerate(glyphs):
        result = match_glyph(rows, et3)
        record = {"index": index, "bitmap": glyph_bytes(data, index).hex(), **result}
        if fd2 is not None:
            packed = rows_to_int(rows)
            same = fd2["by_bytes"].get(glyph_bytes(data, index))
            if same is not None:
                fd2_same_bytes += 1
                if result["exact"] is not None:
                    if fd2["chars"].get(same) == result["exact"]:
                        fd2_agree += 1
                    else:
                        fd2_disagree.append({"index": index, "et3": result["exact"], "fd2_index": same,
                                             "fd2": fd2["chars"].get(same)})
            if result["exact"] is None:
                record["fd2"] = fd2_hint(packed, fd2)
        best = 0 if result["exact"] else ("blank" if result["blank"] else result["candidates"][0]["distance"])
        record["best_distance"] = best
        histogram[best] += 1
        records.append(record)

    summary = {
        "glyph_count": len(glyphs),
        "exact": sum(1 for r in records if r["exact"]),
        "blank": sum(1 for r in records if r["blank"]),
        "not_exact": sum(1 for r in records if not r["exact"] and not r["blank"]),
        "best_distance_histogram": {str(k): histogram[k]
                                    for k in sorted(histogram, key=lambda k: (isinstance(k, str), k))},
        "duplicate_bitmaps": duplicate_bitmaps(data, len(glyphs)),
        "fd2_same_bytes": fd2_same_bytes,
        "fd2_exact_agree": fd2_agree,
        "fd2_exact_disagree": fd2_disagree,
    }
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"summary": summary, "glyphs": records}, ensure_ascii=False, indent=1) + "\n",
                   encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=1))
    print(f"-> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
