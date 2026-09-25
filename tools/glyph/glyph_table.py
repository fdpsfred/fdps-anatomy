"""Build the FDETXT.FON glyph table: exact ET3 matches plus the developer's answers.

Two sources, nothing else:
  - workspace/glyph/match.json (glyph_match.py): every glyph that equals an ET3
    STDFONT.15 glyph pixel for pixel gets that glyph's character.
  - tools/glyph/developer_answers.json: the characters the developer typed on
    the review page (review_page.py) for every glyph that is not exact.  It is
    human input and cannot be regenerated, so it is versioned beside this script.

Outputs (both versioned):
  - assets/text/glyph_table.json  machine-readable; text_decode.py reads it
  - assets/text/glyph_table.md    the same table for people

A glyph with neither source is written with char null and basis "pending"; the
build reports how many remain and exits 1 while any do, so a table with holes
cannot pass for finished.

CLI:
    python glyph_table.py import <dir>   # ArtifactData out_dir dump of the page's `answers`
    python glyph_table.py build
"""

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
DEFAULT_MATCH = REPO_ROOT / "workspace" / "glyph" / "match.json"
ANSWERS = HERE / "developer_answers.json"
OUT_JSON = REPO_ROOT / "assets" / "text" / "glyph_table.json"
OUT_MD = REPO_ROOT / "assets" / "text" / "glyph_table.md"
ANSWER_FILE = re.compile(r"^g[0-9A-Fa-f]{4}\.json$")


class TableError(Exception):
    pass


def read_answer_doc(path):
    """(index, char) out of one dumped document: ArtifactData writes the body flat."""
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(doc, dict) or not isinstance(doc.get("index"), int) or not isinstance(doc.get("char"), str):
        raise TableError(f"{path}: not an answer document {{index, char}}: {doc!r}")
    return doc["index"], doc["char"]


def check_answer(key, char):
    """One glyph is one character; anything else is a typo or a note, not an answer."""
    if len(char) != 1:
        raise TableError(f"answer {key} is {char!r}: expected exactly one character")


def cmd_import(dump_dir, match_path):
    """Read an ArtifactData out_dir dump of the page's `answers` collection."""
    folder = Path(dump_dir) / "answers"
    if not folder.is_dir():
        folder = Path(dump_dir)
    files = sorted(p for p in folder.iterdir() if ANSWER_FILE.match(p.name))
    if not files:
        raise TableError(f"{folder}: no answer documents (g<4 hex digits>.json) found")
    answers = {}
    for path in files:
        index, char = read_answer_doc(path)
        key = f"0x{index:04X}"
        if path.stem.upper() != f"G{index:04X}":
            raise TableError(f"{path}: file name does not match its index {key}")
        check_answer(key, char)
        answers[key] = char
    ANSWERS.write_text(json.dumps(dict(sorted(answers.items())), ensure_ascii=False, indent=1) + "\n",
                       encoding="utf-8")
    match = json.loads(Path(match_path).read_text(encoding="utf-8"))
    pending = {f"0x{g['index']:04X}" for g in match["glyphs"] if not g["exact"]}
    missing = sorted(pending - answers.keys())
    print(f"{len(answers)} answers -> {ANSWERS}")
    if missing:
        print(f"still unanswered ({len(missing)}): {', '.join(missing)}")
        return 1
    return 0


def build_rows(match, answers):
    rows = []
    known = {g["index"] for g in match["glyphs"]}
    for key in answers:
        if int(key, 16) not in known:
            raise TableError(f"answer {key} names a glyph outside the font")
    for g in match["glyphs"]:
        key = f"0x{g['index']:04X}"
        if g["exact"]:
            if key in answers:
                raise TableError(f"answer {key} given for a glyph that already matches ET3 exactly")
            big5 = g["candidates"][0]["big5"]
            rows.append({"index": g["index"], "char": g["exact"], "basis": "et3_exact", "big5": big5})
        elif key in answers:
            check_answer(key, answers[key])
            rows.append({"index": g["index"], "char": answers[key], "basis": "developer"})
        else:
            rows.append({"index": g["index"], "char": None, "basis": "pending"})
    return rows


def markdown(rows, summary):
    lines = [
        "# 字模對照表：`FDETXT.FON` 索引 ↔ 字",
        "",
        "`FDETXT.FON` 每個字模索引代表的字。格式與 token 見 [`resource_info/text.md`](../../resource_info/text.md)；"
        "機器可讀的同一份表是 [`glyph_table.json`](glyph_table.json)，由 "
        "[`tools/glyph/`](../../tools/glyph/_index.md) 產生，文字解碼器 "
        "[`tools/text_decode/`](../../tools/text_decode/_index.md) 讀它。",
        "",
        "## 依據",
        "",
        "| 依據 | 意義 | 字數 |",
        "| --- | --- | ---: |",
        f"| 倚天 | 16×16 字模與倚天 `STDFONT.15` 的某個 15 列字模逐像素完全相同（另一列全空），Big5 欄是該字的碼 | {summary['et3_exact']} |",
        f"| 開發者 | 倚天字型裡沒有完全相同的字模（全形標點、英數字、空白與少數字形不同的字），由開發者逐字判讀 | {summary['developer']} |",
    ]
    if summary["pending"]:
        lines.append(f"| 待填 | 尚未判讀 | {summary['pending']} |")
    lines += [
        "",
        f"共 {summary['glyph_count']} 個字模。",
        "",
        "## 對照表",
        "",
        "| 索引 | 字 | 依據 | Big5 |",
        "| --- | --- | --- | --- |",
    ]
    names = {"et3_exact": "倚天", "developer": "開發者", "pending": "待填"}
    for r in rows:
        char = r["char"] if r["char"] is not None else ""
        shown = "（全形空白）" if char == "　" else char.replace("|", "\\|")
        lines.append(f"| `0x{r['index']:04X}` | {shown} | {names[r['basis']]} | {r.get('big5', '')} |")
    return "\n".join(lines) + "\n"


def cmd_build(match_path):
    match = json.loads(Path(match_path).read_text(encoding="utf-8"))
    answers = json.loads(ANSWERS.read_text(encoding="utf-8")) if ANSWERS.is_file() else {}
    rows = build_rows(match, answers)
    summary = {"glyph_count": len(rows)}
    for basis in ("et3_exact", "developer", "pending"):
        summary[basis] = sum(1 for r in rows if r["basis"] == basis)
    same_char = defaultdict(list)
    for r in rows:
        if r["char"]:
            same_char[r["char"]].append(r["index"])
    shared = {c: ix for c, ix in same_char.items() if len(ix) > 1}
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps({"font": "FDETXT.FON", "glyph_count": len(rows), "summary": summary,
                                    "glyphs": rows}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    OUT_MD.write_text(markdown(rows, summary), encoding="utf-8")
    print(json.dumps(summary))
    if shared:
        print("glyphs sharing one character: " + ", ".join(
            f"{c!r}: " + "/".join(f"0x{i:04X}" for i in ix) for c, ix in shared.items()))
    print(f"-> {OUT_JSON}\n-> {OUT_MD}")
    if summary["pending"]:
        print(f"INCOMPLETE: {summary['pending']} glyphs still pending")
        return 1
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    imp = sub.add_parser("import")
    imp.add_argument("dir")
    imp.add_argument("--match", default=DEFAULT_MATCH)
    build = sub.add_parser("build")
    build.add_argument("--match", default=DEFAULT_MATCH)
    args = parser.parse_args(argv)
    try:
        return cmd_import(args.dir, args.match) if args.command == "import" else cmd_build(args.match)
    except (TableError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
