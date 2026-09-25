"""apply_kb.py -- land decided edits into shared knowledge-base pages (ticket 25.5).

Two pages outside program_info/'s new files are touched by the ticket 25.5
workflow: rebuild_info/pitfalls.md (new rows, and links from existing rows to
program_info/known_bugs.md) and program_info/_index.md (one row per new page).
Agents decide those edits and write them to files; this script applies them
verbatim and judges nothing.  Both are idempotent, so a rerun of the workflow
never adds a row twice.

  pitfalls  reads workspace/game_mechanics/pitfalls/*.json, one verdict each:
              {"id", "outcome", "reason",
               "section": "<## heading text>", "row": "| ... |"      (outcome added)
               "old_line": "| ... |", "new_line": "| ... |"}         (outcome linked)
            other outcomes are recorded and change nothing.
  index     reads workspace/game_mechanics/index_rows.json: {"<doc>": "| ... |"}
            and adds each row to the first table of program_info/_index.md
            unless a row already links <doc>.md.

Usage: python tools/game_mechanics/apply_kb.py pitfalls|index
       python tools/game_mechanics/apply_kb.py --selftest
Output: JSON {"results": [{"id", "status", "detail"}], "errors": n}
Exit : 0 when nothing was refused.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_mechanics import ROOT  # noqa: E402

WS = ROOT / "workspace" / "game_mechanics"
PITFALLS = ROOT / "rebuild_info" / "pitfalls.md"
INDEX = ROOT / "program_info" / "_index.md"


def _last_table_row(lines, start, end):
    """Index of the last table row in lines[start:end], or None."""
    last = None
    for i in range(start, end):
        if lines[i].startswith("|"):
            last = i
    return last


def _newline(text):
    return "\r\n" if "\r\n" in text else "\n"


def apply_pitfalls(text, verdicts):
    nl = _newline(text)
    lines = text.split(nl)
    results = []
    for v in verdicts:
        vid, outcome = v.get("id", "?"), v.get("outcome")
        if outcome == "added":
            row = v.get("row", "")
            if not row.startswith("|"):
                results.append((vid, "error", "row is not a table row"))
                continue
            if row in lines:
                results.append((vid, "already_present", ""))
                continue
            heading = "## " + v.get("section", "")
            if heading not in lines:
                results.append((vid, "error", "no section '%s'" % heading))
                continue
            start = lines.index(heading) + 1
            end = next((i for i in range(start, len(lines)) if lines[i].startswith("## ")),
                       len(lines))
            last = _last_table_row(lines, start, end)
            if last is None:
                results.append((vid, "error", "section '%s' has no table" % heading))
                continue
            lines.insert(last + 1, row)
            results.append((vid, "inserted", ""))
        elif outcome == "linked":
            old, new = v.get("old_line", ""), v.get("new_line", "")
            if old in lines:
                lines[lines.index(old)] = new
                results.append((vid, "relinked", ""))
            elif new in lines:
                results.append((vid, "link_already_present", ""))
            else:
                results.append((vid, "error", "the row changed since it was read"))
        else:
            results.append((vid, "no_change", outcome or "no outcome"))
    return nl.join(lines), results


def apply_index(text, rows):
    nl = _newline(text)
    lines = text.split(nl)
    results = []
    for doc, row in rows.items():
        if "](%s.md)" % doc in text:
            results.append((doc, "already_present", ""))
            continue
        if not row.startswith("|"):
            results.append((doc, "error", "row is not a table row"))
            continue
        first = next((i for i, l in enumerate(lines) if l.startswith("|")), None)
        if first is None:
            results.append((doc, "error", "no table"))
            continue
        end = next((i for i in range(first, len(lines)) if not lines[i].startswith("|")),
                   len(lines))
        lines.insert(end, row)
        text = nl.join(lines)
        results.append((doc, "inserted", ""))
    return nl.join(lines), results


def selftest():
    failures = []

    def expect(label, cond):
        if not cond:
            failures.append(label)

    page = ("# 重建踩雷點\n\n## 不能修的原版 bug\n\n| 事項 | 照直覺 | 正典 |\n| --- | --- | --- |\n"
            "| A | a | x |\n\n段落。\n\n## 不能加的檢查\n\n| 事項 | 照直覺 | 正典 |\n| --- | --- | --- |\n")
    out, r = apply_pitfalls(page, [{"id": "b", "outcome": "added", "section": "不能修的原版 bug",
                                    "row": "| B | b | y |"}])
    expect("an added row lands after the last row of its section's table",
           "| A | a | x |\n| B | b | y |\n\n段落。" in out and r == [("b", "inserted", "")])
    out2, r2 = apply_pitfalls(out, [{"id": "b", "outcome": "added", "section": "不能修的原版 bug",
                                     "row": "| B | b | y |"}])
    expect("adding the same row again changes nothing", out2 == out and r2[0][1] == "already_present")
    out3, r3 = apply_pitfalls(page, [{"id": "a", "outcome": "linked", "old_line": "| A | a | x |",
                                      "new_line": "| A | a | x；[bug](../program_info/known_bugs.md) |"}])
    expect("a link replaces exactly the row it was decided on",
           "| A | a | x；[bug](../program_info/known_bugs.md) |" in out3 and "| A | a | x |" not in out3)
    _, r4 = apply_pitfalls(out3, [{"id": "a", "outcome": "linked", "old_line": "| A | a | x |",
                                   "new_line": "| A | a | x；[bug](../program_info/known_bugs.md) |"}])
    expect("re-applying a link is recognised", r4[0][1] == "link_already_present")
    _, r5 = apply_pitfalls(page, [{"id": "c", "outcome": "linked", "old_line": "| gone |",
                                   "new_line": "| new |"}])
    expect("a row edited by someone else in the meantime is refused, not guessed", r5[0][1] == "error")
    _, r6 = apply_pitfalls(page, [{"id": "d", "outcome": "added", "section": "沒有這節", "row": "| D |"}])
    expect("an unknown section is refused", r6[0][1] == "error")
    out7, r7 = apply_pitfalls(page, [{"id": "e", "outcome": "rejected_below_threshold"}])
    expect("a rejection changes nothing", out7 == page and r7[0][1] == "no_change")
    crlf = page.replace("\n", "\r\n")
    out8, r8 = apply_pitfalls(crlf, [{"id": "b", "outcome": "added", "section": "不能修的原版 bug",
                                      "row": "| B | b | y |"}])
    expect("a CRLF page is edited in place and keeps its line endings",
           r8[0][1] == "inserted" and "| A | a | x |\r\n| B | b | y |\r\n" in out8
           and "\n" not in out8.replace("\r\n", ""))

    idx = "# program_info\n\n| 文件 | 內容 |\n| --- | --- |\n| [`a.md`](a.md) | A |\n\n尾段。\n"
    o, r = apply_index(idx, {"battle": "| [`battle.md`](battle.md) | 戰鬥 |",
                             "a": "| [`a.md`](a.md) | again |"})
    expect("a new page's row is appended to the table",
           "| [`a.md`](a.md) | A |\n| [`battle.md`](battle.md) | 戰鬥 |\n\n尾段。" in o)
    expect("a page that already has a row is left alone",
           dict((x[0], x[1]) for x in r) == {"battle": "inserted", "a": "already_present"})
    o2, _ = apply_index(o, {"battle": "| [`battle.md`](battle.md) | 戰鬥 |"})
    expect("re-applying the index changes nothing", o2 == o)

    for f in failures:
        print("FAIL", f)
    print("selftest: %d failure(s)" % len(failures))
    return 1 if failures else 0


def _read(path):
    return path.read_bytes().decode("utf-8")


def main():
    if "--selftest" in sys.argv:
        return selftest()
    if len(sys.argv) != 2 or sys.argv[1] not in ("pitfalls", "index"):
        print(__doc__)
        return 2
    if sys.argv[1] == "pitfalls":
        verdicts = []
        for p in sorted((WS / "pitfalls").glob("*.json")):
            verdicts.append(json.loads(p.read_text(encoding="utf-8")))
        target = PITFALLS
        new, results = apply_pitfalls(_read(target), verdicts)
    else:
        rows = json.loads((WS / "index_rows.json").read_text(encoding="utf-8"))
        target = INDEX
        new, results = apply_index(_read(target), rows)
    if new != _read(target):
        target.write_bytes(new.encode("utf-8"))
    errors = sum(1 for r in results if r[1] == "error")
    print(json.dumps({"target": str(target.relative_to(ROOT)), "errors": errors,
                      "results": [{"id": r[0], "status": r[1], "detail": r[2]} for r in results]},
                     ensure_ascii=False, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
