"""Decode FDPS text blocks (FDETXTnn.TXT out of FIELD.VFS) into readable text.

The format is the one fdps_draw_text (src/text.c, 0001ff60) walks:

    block  = offset table, then token streams
    offset table: signed 16-bit BYTE offsets from the start of the block, one
                  per entry; the entry count is the first offset / 2
    stream: signed 16-bit tokens up to and including -1

    token     kind            tag in render_entry()
    >= 0      glyph           the character, or {glyph 0xNNNN} if unknown
    -1        (terminator)    -- not a token
    -2        line_break      {br}
    -3        page_break      {page}
    -4        subst_1         {subst1}   a global-text entry chosen at run time
    -5        subst_2         {subst2}   ditto, the second slot
    -6        number          {number}   a figure formatted at run time
    -0x11 n   speaker_char    {speaker char=n}   n is a character id
    -0x12 n   speaker_unit    {speaker unit=n}   n is a map unit index

A speaker code and its operand are one two-word instruction: the reader steps
by four and never tests the operand against -1, so an operand of -1 does not
end the entry.  Glyph 0 is a glyph, not a terminator.

The game itself draws any other negative token as a glyph with a negative
index; no shipped block holds one, so the decoder rejects it instead of
inventing a meaning.

Glyph indices map to characters through assets/text/glyph_table.json, the
machine-readable copy of assets/text/glyph_table.md (tools/glyph builds it).

Library use (the seam other tools import):

    from text_decode import parse_block, render_entry, load_glyph_table
    table = load_glyph_table()
    for entry in parse_block(Path("FDETXT01.TXT").read_bytes()):
        print(entry.index, render_entry(entry, table))

CLI:
    python text_decode.py show <FDETXTnn.TXT> [--table PATH] [--json]
    python text_decode.py all  <dir holding FDETXT*.TXT> <out dir> [--table PATH]

`all` decodes every block, writes one Markdown and one JSON file per block plus
a summary, and exits non-zero if any glyph index has no character in the table
or lies outside the font.
"""

import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TABLE = REPO_ROOT / "assets" / "text" / "glyph_table.json"

TERMINATOR = -1
CODES = {
    -2: "line_break",
    -3: "page_break",
    -4: "subst_1",
    -5: "subst_2",
    -6: "number",
    -0x11: "speaker_char",
    -0x12: "speaker_unit",
}
CODES_WITH_OPERAND = {-0x11, -0x12}
TAGS = {
    "line_break": "{br}",
    "page_break": "{page}",
    "subst_1": "{subst1}",
    "subst_2": "{subst2}",
    "number": "{number}",
}
BLOCK_NAME = re.compile(r"FDETXT(\d\d)\.TXT$", re.IGNORECASE)


class TextBlockError(Exception):
    """The block does not hold what the format requires."""


@dataclass(frozen=True)
class Token:
    kind: str           # "glyph" or one of CODES' values
    value: int          # the raw token word
    operand: int | None  # the speaker codes' second word, else None
    offset: int         # byte offset of the token inside the block


@dataclass(frozen=True)
class Entry:
    index: int
    offset: int         # byte offset of the stream inside the block
    tokens: tuple


def _word(data, pos, what):
    if pos + 2 > len(data):
        raise TextBlockError(f"{what}: runs past the end of the {len(data)} byte block")
    return struct.unpack_from("<h", data, pos)[0]


def parse_stream(data, start, what):
    """Tokens of the stream at byte offset `start`, terminator excluded."""
    tokens = []
    pos = start
    while True:
        value = _word(data, pos, what)
        if value == TERMINATOR:
            return tuple(tokens)
        if value >= 0:
            tokens.append(Token("glyph", value, None, pos))
            pos += 2
        elif value in CODES_WITH_OPERAND:
            operand = _word(data, pos + 2, f"{what} operand")
            tokens.append(Token(CODES[value], value, operand, pos))
            pos += 4
        elif value in CODES:
            tokens.append(Token(CODES[value], value, None, pos))
            pos += 2
        else:
            raise TextBlockError(f"{what}: unknown control code {value} at byte {pos:#x}")


def parse_block(data):
    """Every entry of one text block, in offset-table order."""
    if len(data) < 2:
        raise TextBlockError(f"block is {len(data)} bytes, too short for an offset table")
    first = struct.unpack_from("<h", data, 0)[0]
    if first <= 0 or first % 2 or first > len(data):
        raise TextBlockError(f"first offset {first} is not a valid table size")
    count = first // 2
    offsets = struct.unpack_from(f"<{count}h", data, 0)
    entries = []
    for index, offset in enumerate(offsets):
        if not first <= offset < len(data):
            raise TextBlockError(f"entry {index:#x}: offset {offset} outside the streams")
        entries.append(Entry(index, offset, parse_stream(data, offset, f"entry {index:#x}")))
    return entries


def render_token(token, glyph_table):
    if token.kind == "glyph":
        char = glyph_table.get(token.value)
        return char if char else f"{{glyph 0x{token.value:04X}}}"
    if token.kind == "speaker_char":
        return f"{{speaker char={token.operand}}}"
    if token.kind == "speaker_unit":
        return f"{{speaker unit={token.operand}}}"
    return TAGS[token.kind]


def render_entry(entry, glyph_table):
    """One entry as a single line: characters, with control codes as {tags}."""
    return "".join(render_token(t, glyph_table) for t in entry.tokens)


def glyphs_used(entries):
    return {t.value for e in entries for t in e.tokens if t.kind == "glyph"}


def _read_table(path):
    table = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(table, dict) or not isinstance(table.get("glyph_count"), int) \
            or not isinstance(table.get("glyphs"), list):
        raise TextBlockError(f"{path}: not a glyph table (needs glyph_count and glyphs)")
    return table


def load_glyph_table(path=DEFAULT_TABLE):
    """{glyph index: character} for every glyph the table has a character for."""
    return {g["index"]: g["char"] for g in _read_table(path)["glyphs"] if g.get("char")}


def glyph_count(path=DEFAULT_TABLE):
    return _read_table(path)["glyph_count"]


def entry_record(entry, glyph_table):
    return {
        "index": entry.index,
        "offset": entry.offset,
        "text": render_entry(entry, glyph_table),
        "tokens": [
            {"kind": t.kind, "value": t.value, **({"operand": t.operand} if t.operand is not None else {})}
            for t in entry.tokens
        ],
    }


def block_markdown(name, entries, glyph_table):
    lines = [f"# {name}", "", f"{len(entries)} entries.", "", "| entry | text |", "| --- | --- |"]
    for entry in entries:
        text = render_entry(entry, glyph_table).replace("|", "\\|")
        lines.append(f"| `0x{entry.index:02x}` | {text} |")
    return "\n".join(lines) + "\n"


def cmd_show(args):
    table = load_glyph_table(args.table)
    entries = parse_block(Path(args.block).read_bytes())
    if args.json:
        print(json.dumps([entry_record(e, table) for e in entries], ensure_ascii=False, indent=1))
    else:
        for entry in entries:
            print(f"0x{entry.index:02x}  {render_entry(entry, table)}")
    return 0


def cmd_all(args):
    table = load_glyph_table(args.table)
    count = glyph_count(args.table)
    src = Path(args.dir)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    blocks = sorted(p for p in src.iterdir() if BLOCK_NAME.search(p.name))
    if not blocks:
        raise TextBlockError(f"{src}: no FDETXTnn.TXT here")
    summary = {"blocks": [], "unknown_glyphs": {}, "out_of_font": {}}
    for path in blocks:
        entries = parse_block(path.read_bytes())
        used = glyphs_used(entries)
        unknown = sorted(g for g in used if g < count and g not in table)
        outside = sorted(g for g in used if g >= count)
        stem = path.name.upper().removesuffix(".TXT")
        (out / f"{stem}.md").write_text(block_markdown(path.name.upper(), entries, table), encoding="utf-8")
        (out / f"{stem}.json").write_text(
            json.dumps([entry_record(e, table) for e in entries], ensure_ascii=False, indent=1) + "\n",
            encoding="utf-8")
        summary["blocks"].append({"name": path.name.upper(), "entries": len(entries),
                                  "tokens": sum(len(e.tokens) for e in entries)})
        if unknown:
            summary["unknown_glyphs"][path.name.upper()] = unknown
        if outside:
            summary["out_of_font"][path.name.upper()] = outside
    (out / "summary.json").write_text(json.dumps(summary, indent=1) + "\n", encoding="utf-8")
    print(f"{len(blocks)} blocks, {sum(b['entries'] for b in summary['blocks'])} entries -> {out}")
    unknown = {g for v in summary["unknown_glyphs"].values() for g in v}
    outside = {g for v in summary["out_of_font"].values() for g in v}
    if unknown or outside:
        print(f"FAIL: {len(unknown)} distinct glyph indices without a character, "
              f"{len(outside)} distinct indices outside the font")
        return 1
    print("every glyph index resolves to a character")
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    show = sub.add_parser("show", help="decode one block to stdout")
    show.add_argument("block")
    show.add_argument("--table", default=DEFAULT_TABLE)
    show.add_argument("--json", action="store_true")
    every = sub.add_parser("all", help="decode every block in a directory")
    every.add_argument("dir")
    every.add_argument("out")
    every.add_argument("--table", default=DEFAULT_TABLE)
    args = parser.parse_args(argv)
    try:
        return cmd_show(args) if args.command == "show" else cmd_all(args)
    except (TextBlockError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
