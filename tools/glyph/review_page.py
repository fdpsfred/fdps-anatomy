"""Build the review page for every FDETXT.FON glyph that has no exact ET3 match.

The page shows, per glyph: the bitmap enlarged, the nearest ET3 STDFONT and
ASCFONT glyphs with their differing-pixel counts, the nearest FD2 glyph and the
character FD2's reviewed table gives it, and a few lines of game text the
glyph appears in (other glyphs still waiting for an answer are drawn as their
bitmaps, the one under review is highlighted).  The developer types the right
character into the box under each glyph.

Answers go to the artifact's database (collection `answers`, one document per
glyph named g<index as 4 hex digits>, body {index, char}); the agent reads them
back with the ArtifactData tool and saves them as tools/glyph/developer_answers.json,
which glyph_table.py merges into the table.  Nothing is copied by hand.

CLI:
    python review_page.py [--match workspace/glyph/match.json] [--blocks dir] [--out page.html]
"""

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools" / "text_decode"))
import text_decode  # noqa: E402
import glyph_match  # noqa: E402

DEFAULT_MATCH = REPO_ROOT / "workspace" / "glyph" / "match.json"
DEFAULT_BLOCKS = REPO_ROOT / "workspace" / "vfs_dump" / "FIELD"
DEFAULT_OUT = REPO_ROOT / "workspace" / "glyph" / "review" / "glyph-review.html"
TEMPLATE = HERE / "review_page_template.html"
CONTEXTS_PER_GLYPH = 3
CONTEXT_RADIUS = 8


def context_tokens(tokens, centre, table):
    """The tokens around position `centre`, as small JSON-able cells."""
    out = []
    for pos in range(max(0, centre - CONTEXT_RADIUS), min(len(tokens), centre + CONTEXT_RADIUS + 1)):
        t = tokens[pos]
        if t.kind == "glyph":
            if t.value in table:
                out.append({"c": table[t.value]})
            else:
                out.append({"g": t.value, **({"self": True} if pos == centre else {})})
        elif t.kind in ("speaker_char", "speaker_unit"):
            out.append({"k": "speaker"})
        else:
            out.append({"k": t.kind})
    return out


def collect_contexts(blocks_dir, pending, table):
    usage = {i: 0 for i in pending}
    contexts = {i: [] for i in pending}
    seen_entries = {i: set() for i in pending}
    blocks = sorted(p for p in Path(blocks_dir).iterdir() if text_decode.BLOCK_NAME.search(p.name))
    if not blocks:
        raise text_decode.TextBlockError(f"{blocks_dir}: no FDETXTnn.TXT here")
    for path in blocks:
        name = path.name.upper().removesuffix(".TXT")
        for entry in text_decode.parse_block(path.read_bytes()):
            for pos, t in enumerate(entry.tokens):
                if t.kind != "glyph" or t.value not in usage:
                    continue
                usage[t.value] += 1
                key = (name, entry.index)
                if len(contexts[t.value]) < CONTEXTS_PER_GLYPH and key not in seen_entries[t.value]:
                    seen_entries[t.value].add(key)
                    contexts[t.value].append({"block": name, "entry": entry.index,
                                              "cells": context_tokens(entry.tokens, pos, table)})
    return usage, contexts


def bitmap_hex(rows15):
    """A 15-row ET3 glyph as the page's 16-row hex bitmap (top-aligned)."""
    return "".join(f"{r:04x}" for r in list(rows15) + [0])


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--match", default=DEFAULT_MATCH)
    parser.add_argument("--blocks", default=DEFAULT_BLOCKS)
    parser.add_argument("--fd2", default=glyph_match.DEFAULT_FD2)
    parser.add_argument("--out", default=DEFAULT_OUT)
    args = parser.parse_args(argv)
    try:
        return run(args)
    except (OSError, ValueError, KeyError, text_decode.TextBlockError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


def run(args):
    match =json.loads(Path(args.match).read_text(encoding="utf-8"))
    glyphs = match["glyphs"]
    table = {g["index"]: g["exact"] for g in glyphs if g["exact"]}
    pending = [g for g in glyphs if not g["exact"]]
    usage, contexts = collect_contexts(args.blocks, {g["index"] for g in pending}, table)
    et3 = glyph_match.load_et3()
    fd2 = glyph_match.load_fd2_reference(args.fd2)

    items = []
    for g in pending:
        candidates = []
        if g.get("fd2") and fd2 is not None:
            hint = g["fd2"]
            candidates.append({"source": "fd2", "char": hint["char"], "distance": hint["distance"],
                               "label": f"FD2 0x{hint['index']:04X}",
                               "bitmap": fd2_bitmap_hex(fd2, hint["index"])})
        for c in g["candidates"]:
            if c["source"] == "et3":
                candidates.append({"source": "et3", "char": c["char"], "distance": c["distance"],
                                   "label": f"Big5 {c['big5']}",
                                   "bitmap": bitmap_hex(et3.std[et3.index_of_big5(int(c["big5"], 16))])})
            else:
                candidates.append({"source": "ascii", "char": c["char"], "distance": c["distance"],
                                   "label": f"ASCII {c['code']:#04x}",
                                   "bitmap": bitmap_hex(et3.asc[c["code"]])})
        items.append({"index": g["index"], "bitmap": g["bitmap"], "blank": g["blank"],
                      "uses": usage[g["index"]], "candidates": candidates,
                      "contexts": contexts[g["index"]]})

    bitmaps = {g["index"]: g["bitmap"] for g in pending}
    data = {"items": items, "bitmaps": bitmaps, "summary": {
        "glyph_count": match["summary"]["glyph_count"], "exact": match["summary"]["exact"],
        "pending": len(pending)}}
    html = TEMPLATE.read_text(encoding="utf-8").replace(
        "/*__DATA__*/null", json.dumps(data, ensure_ascii=False).replace("</", "<\\/"))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html, encoding="utf-8")
    print(f"{len(items)} glyphs to review -> {out}")
    return 0


def fd2_bitmap_hex(fd2, index):
    packed = fd2["packed"][index]
    return f"{packed:064x}"


if __name__ == "__main__":
    sys.exit(main())
