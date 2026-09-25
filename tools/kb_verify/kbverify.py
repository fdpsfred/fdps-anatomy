"""The non-judging half of ticket 25.17's knowledge-base verification.

Every knowledge-base document that no generator writes is verified claim by
claim by one agent (verify_ticket25_17.js).  A document is the unit because a
claim's truth often depends on its context in the page; a document too long for
one careful reading is cut at its ## sections into several units, and a unit is
still one agent's only item.  This script is everything around that which
needs no judgement:

    python tools/kb_verify/kbverify.py units [--json]         the worklist and its sizes
    python tools/kb_verify/kbverify.py show <UNIT>            ONE unit (the agent's only view of it)
    python tools/kb_verify/kbverify.py pending [--all]
    python tools/kb_verify/kbverify.py check [--ids ...] [--json]
    python tools/kb_verify/kbverify.py rescan
    python tools/kb_verify/kbverify.py report --date YYYY-MM-DD [--stopped REASON]
    python tools/kb_verify/kbverify.py apply [--exclude PATH ...] [--only PATH ...] [--dry-run]
    python tools/kb_verify/kbverify.py lint [--json] [PATH ...]
    python tools/kb_verify/kbverify.py indexes [--json]

What is left out of the worklist on purpose, because a generator writes it and
a gate compares it with the data cell by cell (SCOPE_NOTES): the three
assets/text pages, the tables tools/data_tables writes, the generated blocks of
the chapter pages and of chapters/_index.md, the whole of cut_content/, and the
Ghidra snapshot files.  The tables the knowledge base keeps by hand but
tools/data_skill compares with the records column by column stay in the unit,
with a note naming the columns that gate already covers.

A verdict names every problem it found as a finding with an exact-string edit
(`old` -> `new`) confined to its own unit.  Nothing here decides whether a
finding is right; `apply` only lands the edits a second reader confirmed
(rescan), and refuses an edit whose `old` text is not exactly once in the
unit's own lines.  Applied edits are logged for the cross-document pass
(kbconsist.py).
"""

import argparse
import hashlib
import json
import re
import sys
from functools import lru_cache
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
REPO = TOOLS.parent
for _sub in ("game_mechanics", "data_tables", "data_emit"):
    sys.path.insert(0, str(TOOLS / _sub))

WORK = REPO / "workspace" / "kb_verify"
VERDICTS = WORK / "verdicts"
APPLIED = WORK / "applied.json"
RUNS = REPO / "devlog" / "runs"

# A unit is at most this many characters of text an agent has to verify
# (generated blocks and generated tables do not count).
UNIT_LIMIT = 15000

# ---------------------------------------------------------------- the scope

VERIFIED_GLOBS = (
    "CONTEXT.md",
    "program_info/*.md",
    "resource_info/*.md",
    "rebuild_info/*.md",
    "assets/_index.md",
    "assets/items.md", "assets/spells.md", "assets/characters.md", "assets/classes.md",
    "assets/enemies.md", "assets/races.md", "assets/names.md", "assets/shops.md",
    "assets/tables/*.md",
    "assets/text/_index.md",
    "chapters/_index.md",
    "chapters/ch*.md",
    "libs/_index.md",
    "ghidra_snapshot/_index.md",
)

# Knowledge-base files no agent re-reads, with the gate that stands for them.
GENERATED_PAGES = ("assets/text/glyph_table.md", "assets/text/global_text.md",
                   "assets/text/scene_text.md")
SCOPE_NOTES = {
    "assets/text/glyph_table.md": "tools/glyph/glyph_table.py build (a rebuild is byte-identical)",
    "assets/text/global_text.md": "tools/global_text/global_text.py verify",
    "assets/text/scene_text.md": "tools/global_text/global_text.py verify",
    "cut_content/": ("tools/cut_content/cut_content.py check and verify-media, "
                     "tools/cut_content/story.py check --final (every entry already verified one "
                     "by one, by the new-trace workflows and by a batch re-verification)"),
    "ghidra_snapshot/ (except _index.md)": "output of tools/ghidra_snapshot/ExportGhidraSnapshot.java",
}

GENERATED_BLOCK = re.compile(r"^<!-- (chapter_docs|story):(\S+) -->$")

# Hand-kept tables tools/data_skill/build.py compares with the records.
GATED_TABLES = {
    ("編號", "名稱", "類型", "AP", "HIT", "DP", "EV", "距離", "附加屬性", "價格"):
        ("data_skill compares each row with ITEM.DAT: 類型, AP, HIT, DP, EV, 距離, the rate in "
         "附加屬性, 價格; data_tables compares 名稱 with FDETXT00"),
    ("編號", "名稱", "K1", "數量", "距離", "範圍", "對象", "選取模式"):
        "data_skill compares each row with ITEM.DAT: K1, 數量, 距離, 範圍, 對象, 選取模式",
    ("編號", "名稱", "威力", "命中率", "距離", "範圍", "MP", "對象"):
        ("data_skill compares every number column with MAGICDAT.DAT; data_tables compares 名稱 "
         "with FDETXT00"),
    ("代碼", "職業", "地形消耗", "暴擊率", "魔法抗性"):
        ("data_skill compares the three number columns with PROMAP.DAT; data_tables compares "
         "職業 with FDETXT00"),
    ("索引", "人物／職業", "種族", "職業", "等級", "HP 基礎", "MP 基礎", "移動力", "法術", "物品",
     "AP 基礎", "DP 基礎", "DX 基礎"):
        ("data_skill compares every number column with FRIAPRDA.DAT and requires the set of rows "
         "to be exactly the indices the program reads; of 人物／職業 only the name before the "
         "slash is compared with FDETXT00 (by data_tables)"),
    ("索引", "人物／職業", "AP", "DP", "DX", "HP", "MP", "習得索引"):
        ("data_skill compares every number column with FRILEVUP.DAT; of 人物／職業 only the name "
         "before the slash is compared with FDETXT00 (by data_tables)"),
}


def verified_docs():
    out = []
    for pattern in VERIFIED_GLOBS:
        out += sorted(p for p in REPO.glob(pattern) if p.is_file())
    seen, docs = set(), []
    for p in out:
        rel = p.relative_to(REPO).as_posix()
        if rel not in seen:
            seen.add(rel)
            docs.append(rel)
    return docs


def split_row(line):
    """A Markdown table row's cells, the way data_tables (their owner) reads them."""
    import data_tables
    return data_tables.split_row(line)


@lru_cache(maxsize=None)
def generated_table_headers():
    import data_tables
    return {tuple(header): key for key, (_doc, header, _b) in data_tables.TABLES.items()}


def masks(lines):
    """[(first, last, note)] 0-based inclusive line ranges nobody verifies here,
    and {line index: note} for the hand tables a gate covers column by column."""
    out, gated = [], {}
    i = 0
    headers = generated_table_headers()
    while i < len(lines):
        m = GENERATED_BLOCK.match(lines[i])
        if m:
            close = "<!-- /%s:%s -->" % (m.group(1), m.group(2))
            j = i
            while j < len(lines) and lines[j] != close:
                j += 1
            out.append((i, min(j, len(lines) - 1),
                        "generated block %s:%s -- written by its generator and compared with a "
                        "fresh regeneration by its gate; not yours to verify"
                        % (m.group(1), m.group(2))))
            i = j + 1
            continue
        if lines[i].startswith("|"):
            header = tuple(split_row(lines[i]))
            j = i
            while j < len(lines) and lines[j].startswith("|"):
                j += 1
            if header in headers:
                out.append((i, j - 1, "table `%s` generated by tools/data_tables -- "
                                      "`data_tables.py check` compares every cell with the data; "
                                      "not yours to verify" % headers[header]))
            elif header in GATED_TABLES:
                gated[i] = GATED_TABLES[header]
            i = j
            continue
        i += 1
    return out, gated


def masked_set(mask_ranges):
    s = set()
    for a, b, _ in mask_ranges:
        s.update(range(a, b + 1))
    return s


def read_lines(rel):
    return (REPO / rel).read_text(encoding="utf-8").split("\n")


def _sections(lines, level):
    """Start indices of the headings of exactly `level` #'s, outside code fences."""
    starts, fence = [], False
    prefix = "#" * level + " "
    for i, line in enumerate(lines):
        if line.startswith("```"):
            fence = not fence
        elif not fence and line.startswith(prefix):
            starts.append(i)
    return starts


def _weight(lines, first, last, hidden):
    return sum(len(lines[i]) + 1 for i in range(first, last + 1) if i not in hidden)


def _chunks(lines, first, last, hidden, limit):
    """Cut [first, last] into ranges of at most `limit` weight: at ### headings
    when there are any, else at blank lines and between table rows."""
    if _weight(lines, first, last, hidden) <= limit:
        return [(first, last)]
    subs = [i for i in _sections(lines, 3) if first < i <= last]
    if not subs:
        return _chunks_plain(lines, first, last, hidden, limit)
    final = []
    for a, b in _pack(lines, [first] + subs + [last + 1], hidden, limit):
        if _weight(lines, a, b, hidden) > limit:
            final += _chunks_plain(lines, a, b, hidden, limit)
        else:
            final.append((a, b))
    return final


def _chunks_plain(lines, first, last, hidden, limit):
    """Cut at blank lines and between table rows (never inside a generated
    region), packing the pieces up to `limit`."""
    cuts = [i for i in range(first + 1, last + 1) if i not in hidden and (
        lines[i - 1].strip() == "" or
        (lines[i].startswith("|") and lines[i - 1].startswith("|")
         and not lines[i].startswith("| ---")))]
    return _pack(lines, [first] + cuts + [last + 1], hidden, limit)


def _pack(lines, bounds, hidden, limit):
    """Greedily join consecutive pieces [bounds[k], bounds[k+1]) up to `limit`."""
    out, cur = [], None
    for k in range(len(bounds) - 1):
        a, b = bounds[k], bounds[k + 1] - 1
        if cur is None:
            cur = [a, b]
        elif _weight(lines, cur[0], b, hidden) <= limit:
            cur[1] = b
        else:
            out.append(tuple(cur))
            cur = [a, b]
    if cur:
        out.append(tuple(cur))
    return out


def unit_id(rel, k, n):
    stem = rel[:-3] if rel.endswith(".md") else rel
    stem = stem.replace("/", "__")
    return stem if n == 1 else "%s__%d" % (stem, k)


FROZEN = "units.json"


@lru_cache(maxsize=None)
def units():
    """[{id, doc, first, last, weight}] -- 0-based inclusive line ranges.

    Once frozen (`units --freeze`, done by the workflow's plan step) the cut
    of every document is kept: a unit is found again by the text of its first
    line, so landing one unit's edits never moves another unit's boundaries.
    A document whose cut can no longer be found is cut afresh."""
    fresh = compute_units()
    path = WORK / FROZEN
    if not path.is_file():
        return fresh
    frozen = json.loads(path.read_text(encoding="utf-8"))
    out = []
    for rel in verified_docs():
        mine = [f for f in frozen if f["doc"] == rel]
        lines = read_lines(rel)
        starts = []
        for f in mine:
            hits = [i for i, l in enumerate(lines) if l == f["start_text"]]
            starts.append(hits[f["start_occurrence"]] if f["start_occurrence"] < len(hits) else None)
        if not mine or None in starts or starts != sorted(starts):
            out += [u for u in fresh if u["doc"] == rel]
            continue
        hidden = masked_set(masks(lines)[0])
        for k, f in enumerate(mine):
            first = starts[k]
            last = (starts[k + 1] - 1) if k + 1 < len(starts) else len(lines) - 1
            out.append({"id": f["id"], "doc": rel, "first": first, "last": last,
                        "part": f["part"], "parts": f["parts"],
                        "weight": _weight(lines, first, last, hidden)})
    return out


def freeze_units():
    rows = []
    for u in compute_units():
        lines = read_lines(u["doc"])
        text = lines[u["first"]]
        occurrence = sum(1 for l in lines[:u["first"]] if l == text)
        rows.append({"id": u["id"], "doc": u["doc"], "part": u["part"], "parts": u["parts"],
                     "start_text": text, "start_occurrence": occurrence})
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / FROZEN).write_text(json.dumps(rows, ensure_ascii=False, indent=1) + "\n",
                               encoding="utf-8")
    units.cache_clear()
    return rows


def compute_units():
    out = []
    for rel in verified_docs():
        lines = read_lines(rel)
        mask_ranges, _ = masks(lines)
        hidden = masked_set(mask_ranges)
        heads = _sections(lines, 2)
        bounds = [0] + [h for h in heads if h > 0] + [len(lines)]
        sections = [(bounds[k], bounds[k + 1] - 1) for k in range(len(bounds) - 1)]
        pieces = []
        for a, b in sections:
            pieces += _chunks(lines, a, b, hidden, UNIT_LIMIT)
        packed = _pack(lines, [a for a, _ in pieces] + [pieces[-1][1] + 1], hidden, UNIT_LIMIT)
        packed = [(a, b) for a, b in packed if _weight(lines, a, b, hidden) > 0]
        for k, (a, b) in enumerate(packed, 1):
            out.append({"id": unit_id(rel, k, len(packed)), "doc": rel, "first": a, "last": b,
                        "part": k, "parts": len(packed),
                        "weight": _weight(lines, a, b, hidden)})
    return out


def by_id():
    return {u["id"]: u for u in units()}


def unit_text(u):
    """What the agent sees: its lines, numbered, generated regions folded."""
    lines = read_lines(u["doc"])
    mask_ranges, gated = masks(lines)
    folded = {a: (b, note) for a, b, note in mask_ranges}
    out, i = [], u["first"]
    while i <= u["last"]:
        if i in folded:
            b, note = folded[i]
            out.append("%5d-%d| [%s]" % (i + 1, b + 1, note))
            i = b + 1
            continue
        if i in gated:
            out.append("     | [the next table is gated: %s. Verify only the columns the gate does "
                       "not cover, and the text around the table]" % gated[i])
        out.append("%5d| %s" % (i + 1, lines[i]))
        i += 1
    return "\n".join(out)


def sha1(text):
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def unit_sha1(u):
    lines = read_lines(u["doc"])
    return sha1("\n".join(lines[u["first"]:u["last"] + 1]))


def headline(u):
    return ("document %s, unit %d of %d, lines %d-%d (lines outside them belong to other agents)"
            % (u["doc"], u["part"], u["parts"], u["first"] + 1, u["last"] + 1))


# ---------------------------------------------------------------- verdicts

FINDING_KINDS = ("wrong_fact", "imprecise", "stale_count", "broken_reference", "narrative",
                 "forbidden_reference", "duplicate", "owner_violation", "unverifiable", "other")
CONFIDENCE = ("high", "medium", "low")
SOURCES = ("src", "ghidra", "data", "tool", "kb", "guide", "fd2")
FIRST_HAND = ("src", "ghidra", "data", "tool")
NEEDS_FIRST_HAND = ("wrong_fact", "imprecise", "stale_count", "unverifiable")
GHIDRA_ADDRESS = re.compile(r"(?<![0-9A-Za-z])(?:0x)?(?=[0-9a-fA-F]{0,7}[0-9])[0-9a-fA-F]{5,8}(?![0-9A-Za-z])")
REQUIRED = ("unit", "unit_sha1", "claims_checked", "findings", "cross_doc", "pitfall_candidates",
            "summary")
FINDING_FIELDS = ("n", "kind", "quote", "problem", "evidence", "confidence", "edit", "outside")


def check_evidence(evidence, need_first_hand):
    problems, first = [], 0
    if not isinstance(evidence, list) or not evidence:
        return ["no evidence"]
    for i, e in enumerate(evidence):
        if not isinstance(e, dict):
            problems.append("evidence[%d] is not an object" % i)
            continue
        src, loc, obs = e.get("source"), str(e.get("location") or ""), str(e.get("observation") or "")
        if src not in SOURCES:
            problems.append("evidence[%d].source %r not one of %s" % (i, src, SOURCES))
            continue
        if not obs.strip() or not loc.strip():
            problems.append("evidence[%d] needs a location and an observation" % i)
            continue
        if src == "src" and not re.search(r"\.(c|h|asm):\d+", loc):
            problems.append("evidence[%d] src location needs file:line, got %r" % (i, loc))
            continue
        if src == "ghidra" and not GHIDRA_ADDRESS.search(loc):
            problems.append("evidence[%d] ghidra location needs an address, got %r" % (i, loc))
            continue
        if src == "data" and "@" not in loc:
            problems.append("evidence[%d] data location needs FILE@offset, got %r" % (i, loc))
            continue
        if src == "tool" and "tools/" not in loc.replace("\\", "/"):
            problems.append("evidence[%d] tool location needs the command run, got %r" % (i, loc))
            continue
        if src in FIRST_HAND:
            first += 1
    if need_first_hand and first == 0:
        problems.append("no first-hand evidence (src, ghidra, data or a tool's output)")
    return problems


def check_edit(edit, u, lines, hidden, where):
    """Problems with one exact-string edit against the unit's own lines."""
    if not isinstance(edit, dict) or not isinstance(edit.get("old"), str) \
            or not isinstance(edit.get("new"), str):
        return ["%s: edit must be {old, new} strings" % where]
    old, new = edit["old"], edit["new"]
    if not old.strip():
        return ["%s: edit.old is empty" % where]
    if old == new:
        return ["%s: edit changes nothing" % where]
    text = "\n".join(lines)
    count = text.count(old)
    if count != 1:
        return ["%s: edit.old occurs %d times in %s (must be exactly once)" % (where, count, u["doc"])]
    start = text.index(old)
    first = text.count("\n", 0, start)
    last = first + old.count("\n")
    if first < u["first"] or last > u["last"]:
        return ["%s: edit.old lies outside this unit's lines %d-%d"
                % (where, u["first"] + 1, u["last"] + 1)]
    if any(i in hidden for i in range(first, last + 1)):
        return ["%s: edit.old lies inside a generated region" % where]
    return []


def check_verdict(v, u):
    if not isinstance(v, dict):
        return ["the verdict file is not a JSON object"]
    problems = ["missing field %s" % k for k in REQUIRED if k not in v]
    if problems:
        return problems
    if v["unit"] != u["id"]:
        problems.append("unit %r does not match %s" % (v["unit"], u["id"]))
    if unit_sha1(u) not in (v["unit_sha1"], v.get("_landed_sha1")):
        problems.append("stale: the unit's text changed since it was judged")
    if v.get("_landed_sha1") and unit_sha1(u) == v["_landed_sha1"]:
        # Landed: the edits are in the page now, so they are no longer
        # checked against it.
        return problems
    if not isinstance(v["claims_checked"], int) or v["claims_checked"] < 1:
        problems.append("claims_checked must be a positive int")
    if not isinstance(v["summary"], str) or len(v["summary"].strip()) < 10:
        problems.append("summary must be a sentence")
    lines = read_lines(u["doc"])
    hidden = masked_set(masks(lines)[0])
    if not isinstance(v["findings"], list):
        return problems + ["findings must be a list"]
    seen = set()
    spans = []
    for k, f in enumerate(v["findings"]):
        where = "finding[%d]" % k
        if not isinstance(f, dict):
            problems.append("%s is not an object" % where)
            continue
        absent = [x for x in FINDING_FIELDS if x not in f]
        if absent:
            problems += ["%s: missing %s" % (where, x) for x in absent]
            continue
        if f["n"] in seen:
            problems.append("%s: n %r repeats" % (where, f["n"]))
        seen.add(f["n"])
        if f["kind"] not in FINDING_KINDS:
            problems.append("%s: kind %r not one of %s" % (where, f["kind"], FINDING_KINDS))
        if f["confidence"] not in CONFIDENCE:
            problems.append("%s: confidence %r" % (where, f["confidence"]))
        if not isinstance(f["problem"], str) or len(f["problem"].strip()) < 10:
            problems.append("%s: problem must say what is wrong" % where)
        if not isinstance(f["quote"], str) or not f["quote"].strip():
            problems.append("%s: quote the claim" % where)
        if not isinstance(f.get("developer_question", ""), str):
            problems.append("%s: developer_question must be a string" % where)
        problems += ["%s: %s" % (where, p) for p in
                     check_evidence(f["evidence"], f["kind"] in NEEDS_FIRST_HAND)]
        if f["edit"] is None:
            # unverifiable waits for evidence; other with no fix records a lint
            # finding that stands, with the reason in problem.
            if f["kind"] not in ("unverifiable", "other") and not str(f["outside"]).strip():
                problems.append("%s: no edit and no outside fix -- say how it is fixed" % where)
        else:
            errs = check_edit(f["edit"], u, lines, hidden, where)
            problems += errs
            if not errs:
                text = "\n".join(lines)
                s = text.index(f["edit"]["old"])
                spans.append((s, s + len(f["edit"]["old"]), f["n"]))
    spans.sort()
    for (a1, b1, n1), (a2, b2, n2) in zip(spans, spans[1:]):
        if a2 < b1:
            problems.append("findings %s and %s edit overlapping text" % (n1, n2))
    if not isinstance(v["cross_doc"], list):
        problems.append("cross_doc must be a list")
    else:
        for k, c in enumerate(v["cross_doc"]):
            if not isinstance(c, dict) or not all(isinstance(c.get(x), str) and c.get(x).strip()
                                                  for x in ("other_doc", "anchor", "note")):
                problems.append("cross_doc[%d] needs other_doc, anchor and note" % k)
            elif not (REPO / c["other_doc"]).exists():
                problems.append("cross_doc[%d].other_doc %r does not exist" % (k, c["other_doc"]))
    if not isinstance(v["pitfall_candidates"], list):
        problems.append("pitfall_candidates must be a list")
    reread = v.get("_reread")
    if reread is not None:
        problems += check_reread(reread, v, u, lines, hidden)
    return problems


DECISIONS = ("confirm", "amend", "reject")


def check_reread(reread, v, u, lines, hidden):
    problems = []
    if not isinstance(reread, dict) or not isinstance(reread.get("decisions"), list):
        return ["_reread must be {decisions: [...]}"]
    want = {f["n"] for f in v["findings"] if isinstance(f, dict) and needs_reread(f)}
    got = {}
    for k, d in enumerate(reread["decisions"]):
        where = "_reread.decisions[%d]" % k
        if not isinstance(d, dict) or d.get("decision") not in DECISIONS:
            problems.append("%s: decision must be one of %s" % (where, DECISIONS))
            continue
        if not isinstance(d.get("why"), str) or len(d["why"].strip()) < 10:
            problems.append("%s: why must say what was checked" % where)
        got[d.get("n")] = d
        if d["decision"] == "amend":
            if d.get("edit") is None:
                if not str(d.get("outside") or "").strip():
                    problems.append("%s: amend needs an edit or an outside fix" % where)
            else:
                problems += check_edit(d["edit"], u, lines, hidden, where)
    missing = sorted(want - set(got), key=str)
    if missing:
        problems.append("_reread has no decision for findings %s" % missing)
    return problems


def needs_reread(f):
    return f.get("edit") is not None or f.get("confidence") != "high" \
        or f.get("kind") == "unverifiable" or bool(str(f.get("outside") or "").strip())


def _load(path):
    try:
        return json.loads(path.read_text(encoding="utf-8")), None
    except Exception as exc:                                        # noqa: BLE001
        return None, "unreadable: %s" % exc


def verdict_path(uid):
    return VERDICTS / ("%s.json" % uid)


def states(wanted):
    rows = []
    for u in wanted:
        path = verdict_path(u["id"])
        if not path.is_file():
            rows.append({"id": u["id"], "state": "missing"})
            continue
        v, err = _load(path)
        problems = [err] if err else check_verdict(v, u)
        rows.append({"id": u["id"], "state": "failing" if problems else "done",
                     **({"problems": problems} if problems else {})})
    return rows


def rescan_todo():
    todo = []
    for row in states(units()):
        if row["state"] != "done":
            continue
        v, _ = _load(verdict_path(row["id"]))
        flagged = [f["n"] for f in v["findings"] if needs_reread(f)]
        if flagged and "_reread" not in v:
            todo.append({"id": row["id"], "findings": flagged})
    return todo


def final_edits(v):
    """[(finding, edit)] the landing may apply: confirmed or amended by the
    second reader.  A finding nobody had to re-read has no edit by definition."""
    decisions = {d.get("n"): d for d in (v.get("_reread") or {}).get("decisions", [])}
    out = []
    for f in v["findings"]:
        d = decisions.get(f["n"])
        if d is None or d["decision"] == "reject":
            continue
        edit = f["edit"] if d["decision"] == "confirm" else d.get("edit")
        if edit is not None:
            out.append((f, edit))
    return out


def outside_fixes(v):
    decisions = {d.get("n"): d for d in (v.get("_reread") or {}).get("decisions", [])}
    out = []
    for f in v["findings"]:
        d = decisions.get(f["n"])
        if d is not None and d["decision"] == "reject":
            continue
        outside = (d.get("outside") if d is not None and d["decision"] == "amend" else None) \
            or f.get("outside")
        if str(outside or "").strip():
            out.append({"n": f["n"], "kind": f["kind"], "outside": outside,
                        "problem": f["problem"]})
    return out


def summarize(stopped):
    rows = states(units())
    done = [r["id"] for r in rows if r["state"] == "done"]
    out = {"units": len(rows), "complete": len(done),
           "unfinished": [r for r in rows if r["state"] != "done"],
           "claims_checked": 0, "findings": 0, "by_kind": {}, "edits_confirmed": 0,
           "rejected": 0, "unverifiable": [], "outside_fixes": [], "cross_doc": [],
           "pitfall_candidates": [], "not_reread": [], "per_unit": [], "stopped": stopped,
           "scope_left_to_gates": SCOPE_NOTES}
    for uid in done:
        v, _ = _load(verdict_path(uid))
        out["claims_checked"] += v["claims_checked"]
        out["findings"] += len(v["findings"])
        for f in v["findings"]:
            out["by_kind"][f["kind"]] = out["by_kind"].get(f["kind"], 0) + 1
            if f["kind"] == "unverifiable" or f["confidence"] == "low":
                out["unverifiable"].append({"unit": uid, "n": f["n"], "problem": f["problem"]})
            if str(f.get("developer_question") or "").strip():
                out.setdefault("developer_questions", []).append(
                    {"unit": uid, "n": f["n"], "question": f["developer_question"]})
        edits = final_edits(v) if "_reread" in v else []
        out["edits_confirmed"] += len(edits)
        out["rejected"] += sum(1 for d in (v.get("_reread") or {}).get("decisions", [])
                               if d["decision"] == "reject")
        out["outside_fixes"] += [dict(o, unit=uid) for o in outside_fixes(v)]
        out["cross_doc"] += [dict(c, unit=uid) for c in v["cross_doc"]]
        out["pitfall_candidates"] += [{"unit": uid, "text": p} for p in v["pitfall_candidates"]]
        if any(needs_reread(f) for f in v["findings"]) and "_reread" not in v:
            out["not_reread"].append(uid)
        if v.get("_refused"):
            out.setdefault("refused_at_landing", []).extend(v["_refused"])
        out["per_unit"].append({"unit": uid, "claims": v["claims_checked"],
                                "findings": len(v["findings"]), "summary": v["summary"]})
    return out


# ------------------------------------------------------------------ landing

EXCLUDED = "excluded this run"


def apply_edits(exclude=(), only=(), dry_run=False):
    """Land every confirmed edit.  Returns (applied, refused)."""
    applied, refused = [], []
    per_doc = {}
    landed_units = {}
    for u in units():
        if only and u["doc"] not in only:
            continue
        # Everything that is not landed here says why (ADR-0007 5.3).
        if not verdict_path(u["id"]).is_file():
            refused.append({"doc": u["doc"], "unit": u["id"], "n": "-", "why": "no verdict"})
            continue
        v, err = _load(verdict_path(u["id"]))
        if v is None:
            refused.append({"doc": u["doc"], "unit": u["id"], "n": "-", "why": err})
            continue
        if v.get("_landed_sha1"):
            continue                                    # landed by an earlier apply
        problems = check_verdict(v, u)
        if problems:
            refused.append({"doc": u["doc"], "unit": u["id"], "n": "-",
                            "why": "verdict fails the gate: " + "; ".join(problems[:3])})
            continue
        if "_reread" not in v and any(needs_reread(f) for f in v.get("findings", [])):
            refused.append({"doc": u["doc"], "unit": u["id"], "n": "-",
                            "why": "flagged findings have no second reading yet"})
            continue
        landed_units.setdefault(u["doc"], []).append(u["id"])
        for f, edit in (final_edits(v) if "_reread" in v else []):
            per_doc.setdefault(u["doc"], []).append((u, f, edit))
    for doc, items in sorted(per_doc.items()):
        if doc in exclude:
            refused += [{"doc": doc, "unit": u["id"], "n": f["n"], "why": EXCLUDED}
                        for u, f, _ in items]
            continue
        path = REPO / doc
        text = path.read_text(encoding="utf-8")
        new_text = text
        for u, f, edit in items:
            if new_text.count(edit["old"]) != 1:
                refused.append({"doc": doc, "unit": u["id"], "n": f["n"],
                                "why": "old text occurs %d times now" % new_text.count(edit["old"])})
                continue
            new_text = new_text.replace(edit["old"], edit["new"], 1)
            applied.append({"doc": doc, "unit": u["id"], "n": f["n"], "kind": f["kind"],
                            "problem": f["problem"], "old": edit["old"], "new": edit["new"]})
        if new_text != text and not dry_run:
            if doc == "chapters/_index.md":
                problems = mirror_index_sources(text, new_text)
                if problems:
                    refused += [{"doc": doc, "unit": "-", "n": "-", "why": p} for p in problems]
                    applied = [a for a in applied if a["doc"] != doc]
                    continue
            path.write_bytes(new_text.encode("utf-8"))
    if not dry_run:
        # Record on each landed verdict the unit's text as it is now, so the
        # gate keeps counting it done (its old text is gone) and a second
        # `apply` does not land it twice.  Refused findings are kept on it.
        units.cache_clear()
        now = by_id()
        refused_by_unit = {}
        for r in refused:
            refused_by_unit.setdefault(r["unit"], []).append(r)
        for doc, ids in landed_units.items():
            if doc in exclude or any(r["doc"] == doc and r["unit"] == "-" for r in refused):
                continue
            for uid in ids:
                v, _ = _load(verdict_path(uid))
                if uid not in now:
                    refused.append({"doc": doc, "unit": uid, "n": "-",
                                    "why": "the unit cannot be found after landing"})
                    continue
                v["_landed_sha1"] = unit_sha1(now[uid])
                if refused_by_unit.get(uid):
                    v["_refused"] = refused_by_unit[uid]
                verdict_path(uid).write_text(json.dumps(v, ensure_ascii=False, indent=1) + "\n",
                                             encoding="utf-8")
        WORK.mkdir(parents=True, exist_ok=True)
        log = json.loads(APPLIED.read_text(encoding="utf-8")) if APPLIED.is_file() else []
        known = {(a["unit"], a["n"]) for a in log}
        log += [a for a in applied if (a["unit"], a["n"]) not in known]
        APPLIED.write_text(json.dumps(log, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return applied, refused


def mirror_index_sources(old_page, new_page):
    """chapters/_index.md is assembled by tools/chapter_docs/index.py from a
    fixed introduction in index.py and a chains draft: an edit to the landed
    page is written into whichever of the two holds that text, so the next
    `index.py verify` still matches.

    These are the sources the page is built from, not this tool's output:
    index.py takes the chains section from the draft when the draft exists
    and from the landed page when it does not, so with no draft the page
    edit alone is the whole landing."""
    import difflib
    index_py = TOOLS / "chapter_docs" / "index.py"
    draft = REPO / "workspace" / "chapter_docs" / "drafts" / "_index_chains.md"
    heading = "## 跨章機制鏈"
    problems = []
    # index.py joins INTRO and the first table with a blank line; the string
    # literal in index.py holds INTRO without that trailing newline.
    old_intro = old_page.split("\n## ", 1)[0].rstrip("\n")
    new_intro = new_page.split("\n## ", 1)[0].rstrip("\n")
    if old_intro != new_intro:
        src = index_py.read_text(encoding="utf-8")
        if src.count(old_intro) != 1:
            problems.append("index.py INTRO does not hold the page's introduction verbatim")
        else:
            index_py.write_bytes(src.replace(old_intro, new_intro, 1).encode("utf-8"))
    old_chain = old_page[old_page.index(heading):] if heading in old_page else ""
    new_chain = new_page[new_page.index(heading):] if heading in new_page else ""
    if old_chain != new_chain and draft.is_file():
        draft.write_bytes(new_chain.rstrip("\n").encode("utf-8") + b"\n")
    rest_old = old_page.replace(old_intro, "", 1).replace(old_chain, "")
    rest_new = new_page.replace(new_intro, "", 1).replace(new_chain, "")
    if rest_old != rest_new:
        diff = list(difflib.unified_diff(rest_old.splitlines(), rest_new.splitlines(), lineterm=""))
        problems.append("an edit touches the generated part of chapters/_index.md: %s"
                        % " / ".join(diff[2:6]))
    return problems


# --------------------------------------------------------------------- lint

def all_kb_docs():
    """Every knowledge-base Markdown file, generated ones included."""
    out = []
    for pattern in ("README.md", "CONTEXT.md", "open_issues.md", "program_info/*.md",
                    "resource_info/*.md", "rebuild_info/*.md", "assets/*.md", "assets/*/*.md",
                    "chapters/*.md", "cut_content/*.md", "libs/*.md", "ghidra_snapshot/_index.md"):
        out += sorted(p.relative_to(REPO).as_posix() for p in REPO.glob(pattern) if p.is_file())
    return list(dict.fromkeys(out))


def lint(paths=None):
    """KB-wide rules on every page: no workspace/ or legacy/ citation, no
    process narrative outside generated regions and quoted game text, every
    relative link resolves, every `name`（`0xaddr`） citation matches the
    snapshot, every backticked fdps_ symbol exists.  The rules and their
    wording are check_mechanics's (the owner of the knowledge-base rules)."""
    import check_mechanics as mech
    names, idents = mech.load_names(), mech.load_idents()
    report = {}
    for rel in paths or all_kb_docs():
        lines = read_lines(rel)
        hidden = masked_set(masks(lines)[0])
        findings = []
        fence = False
        for no, line in enumerate(lines, 1):
            if line.startswith("```"):
                fence = not fence
            if (no - 1) in hidden:
                continue
            if mech.FORBIDDEN.search(line):
                findings.append(("forbidden-reference", no, line.strip()[:120]))
            m = mech.NARRATIVE.search(line)
            if m and not fence:
                findings.append(("narrative", no, m.group(0)))
            for name, addr in mech.citations(line):
                known = names.get(name)
                if known is not None and addr not in known:
                    findings.append(("citation-mismatch", no, "`%s` is at %s, not 0x%x" % (
                        name, ", ".join("0x%x" % a for a in sorted(known)), addr)))
            for m2 in mech.SYMBOL.finditer(line):
                if m2.group(1) not in names and m2.group(1) not in idents:
                    findings.append(("unknown-symbol", no, m2.group(1)))
        for level, code, no, msg in mech.check_links(lines, (REPO / rel).parent, REPO):
            if (no - 1) not in hidden:
                findings.append((code, no, msg))
        if findings:
            report[rel] = [{"code": c, "line": n, "message": msg} for c, n, msg in findings]
    return report


def index_consistency():
    """Every file in a knowledge-base folder is listed in that folder's
    _index.md, and every tools/ subfolder has an _index.md and a row in
    tools/_index.md."""
    problems = []
    for folder in ("program_info", "resource_info", "rebuild_info", "assets", "assets/tables",
                   "assets/text", "chapters", "cut_content", "libs"):
        index = REPO / folder / "_index.md"
        if not index.is_file():
            problems.append("%s has no _index.md" % folder)
            continue
        text = index.read_text(encoding="utf-8")
        for p in sorted((REPO / folder).iterdir()):
            if p.name == "_index.md" or p.name.startswith(".") or p.name == "__pycache__":
                continue
            if p.is_file() and p.suffix == ".md" and "(%s)" % p.name not in text \
                    and "`%s`" % p.name not in text:
                problems.append("%s/_index.md does not list %s" % (folder, p.name))
            if p.is_dir() and "%s/" % p.name not in text:
                problems.append("%s/_index.md does not mention the %s/ subfolder" % (folder, p.name))
    tools_index = (TOOLS / "_index.md").read_text(encoding="utf-8")
    for p in sorted(TOOLS.iterdir()):
        if not p.is_dir() or p.name.startswith((".", "_")):
            continue
        if not any(c.name != "__pycache__" for c in p.iterdir()):
            continue                # a leftover bytecode cache, not a tool
        if not (p / "_index.md").is_file():
            problems.append("tools/%s has no _index.md" % p.name)
        if "(%s/_index.md)" % p.name not in tools_index:
            problems.append("tools/_index.md has no row for %s/" % p.name)
        own = (p / "_index.md").read_text(encoding="utf-8") if (p / "_index.md").is_file() else ""
        for f in sorted(p.iterdir()):
            if f.is_file() and f.suffix in (".py", ".js", ".java", ".html") and f.name not in own:
                problems.append("tools/%s/_index.md does not mention %s" % (p.name, f.name))
    readme = (REPO / "README.md").read_text(encoding="utf-8")
    for folder in ("program_info", "resource_info", "assets", "chapters", "cut_content",
                   "rebuild_info", "libs", "ghidra_snapshot", "tools", "devlog"):
        if "`%s/`" % folder not in readme:
            problems.append("README.md does not describe %s/" % folder)
    return problems


# ---------------------------------------------------------------------- CLI

def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("units")
    p.add_argument("--json", action="store_true")
    p.add_argument("--freeze", action="store_true",
                   help="keep the current cut of every document (done once, by the plan step)")
    sub.add_parser("show").add_argument("id")
    sub.add_parser("pending").add_argument("--all", action="store_true")
    p = sub.add_parser("check")
    p.add_argument("--ids", nargs="*", default=None)
    p.add_argument("--json", action="store_true")
    sub.add_parser("rescan")
    p = sub.add_parser("report")
    p.add_argument("--date", required=True)
    p.add_argument("--stopped", default="")
    p = sub.add_parser("apply")
    p.add_argument("--exclude", nargs="*", default=[])
    p.add_argument("--only", nargs="*", default=[])
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("lint")
    p.add_argument("paths", nargs="*")
    p.add_argument("--json", action="store_true")
    sub.add_parser("indexes").add_argument("--json", action="store_true")
    args = ap.parse_args()
    table = by_id()

    if args.cmd == "units":
        if args.freeze and not (WORK / FROZEN).is_file():
            freeze_units()
        rows = units()
        if args.json:
            print(json.dumps(rows, ensure_ascii=False, indent=1))
        else:
            for u in rows:
                print("%-45s %6d chars  lines %d-%d" % (u["id"], u["weight"], u["first"] + 1,
                                                         u["last"] + 1))
            print("%d units over %d documents" % (len(rows), len({u["doc"] for u in rows})))
        return 0
    if args.cmd == "show":
        u = table.get(args.id)
        if u is None:
            print("no unit %s" % args.id, file=sys.stderr)
            return 1
        print(headline(u))
        print("unit_sha1: %s" % unit_sha1(u))
        print("verdict_file: %s" % verdict_path(u["id"]))
        print()
        print(unit_text(u))
        found = [f for f in lint([u["doc"]]).get(u["doc"], [])
                 if u["first"] + 1 <= f["line"] <= u["last"] + 1]
        print()
        print("deterministic lint (kbverify.py lint) findings in this unit: %d" % len(found))
        for f in found:
            print("  line %d %s: %s" % (f["line"], f["code"], f["message"]))
        return 0
    if args.cmd == "pending":
        rows = states(units())
        if args.all:
            print(json.dumps([{"id": r["id"], "state": r["state"]} for r in rows],
                             ensure_ascii=False, indent=1))
        else:
            print(json.dumps([r["id"] for r in rows if r["state"] != "done"], ensure_ascii=False))
        return 0
    if args.cmd == "check":
        wanted = args.ids or list(table)
        unknown = [i for i in wanted if i not in table]
        rows = states([table[i] for i in wanted if i in table])
        result = {"checked": len(wanted), "ok": sum(r["state"] == "done" for r in rows),
                  "missing": [r["id"] for r in rows if r["state"] == "missing"] + unknown,
                  "failures": {r["id"]: r["problems"] for r in rows if r["state"] == "failing"}}
        result["gate_passed"] = not result["missing"] and not result["failures"]
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=1))
        else:
            print("checked=%d ok=%d missing=%d failing=%d" % (
                result["checked"], result["ok"], len(result["missing"]), len(result["failures"])))
            for i in result["missing"]:
                print("  MISSING  %s" % i)
            for i, problems in result["failures"].items():
                print("  FAIL     %s: %s" % (i, "; ".join(problems)))
            print("GATE %s" % ("PASSED" if result["gate_passed"] else "FAILED"))
        return 0 if result["gate_passed"] else 1
    if args.cmd == "rescan":
        print(json.dumps(rescan_todo(), ensure_ascii=False, indent=1))
        return 0
    if args.cmd == "report":
        summary = summarize(args.stopped)
        RUNS.mkdir(parents=True, exist_ok=True)
        stem = "%s-kb-verify" % args.date
        written = [RUNS / ("%s-summary.json" % stem)]
        written[0].write_text(json.dumps(summary, ensure_ascii=False, indent=1) + "\n",
                              encoding="utf-8")
        if not args.stopped:
            archive = []
            for u in units():
                v, _ = _load(verdict_path(u["id"])) if verdict_path(u["id"]).is_file() else (None, None)
                if v is not None:
                    archive.append(v)
            written.append(RUNS / ("%s-verdicts.json" % stem))
            written[1].write_text(json.dumps(archive, ensure_ascii=False, indent=1) + "\n",
                                  encoding="utf-8")
        print(json.dumps({"written": [str(p) for p in written], "complete": summary["complete"],
                          "units": summary["units"], "findings": summary["findings"],
                          "edits_confirmed": summary["edits_confirmed"],
                          "unfinished": [r["id"] for r in summary["unfinished"]],
                          "not_reread": summary["not_reread"]}, ensure_ascii=False))
        return 0
    if args.cmd == "apply":
        applied, refused = apply_edits(set(args.exclude), set(args.only), args.dry_run)
        print(json.dumps({"applied": len(applied), "refused": refused,
                          "docs": sorted({a["doc"] for a in applied})}, ensure_ascii=False, indent=1))
        # A page held back on purpose (--exclude) is not a failure; anything else is.
        return 1 if any(r["why"] != EXCLUDED for r in refused) else 0
    if args.cmd == "lint":
        report = lint(args.paths or None)
        if args.json:
            print(json.dumps(report, ensure_ascii=False, indent=1))
        else:
            for doc, fs in report.items():
                for f in fs:
                    print("%s:%d %s %s" % (doc, f["line"], f["code"], f["message"]))
            print("lint: %d finding(s) in %d file(s)" % (sum(len(v) for v in report.values()),
                                                        len(report)))
        return 1 if report else 0
    if args.cmd == "indexes":
        problems = index_consistency()
        if args.json:
            print(json.dumps(problems, ensure_ascii=False, indent=1))
        else:
            for p in problems:
                print(p)
            print("indexes: %d problem(s)" % len(problems))
        return 1 if problems else 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
