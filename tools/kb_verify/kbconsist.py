"""The non-judging half of ticket 25.17's cross-document consistency pass.

Verifying one document at a time cannot see two things: a fact that is stated
in several documents and was corrected in only one of them, and the same fact
written twice (in two documents, or twice in one long page) where the
knowledge base wants exactly one owner.  This script finds the candidates for
both deterministically and hands each candidate group to one agent
(consist_ticket25_17.js); the agent judges, this script lands.

    python tools/kb_verify/kbconsist.py freeze [--refresh]   build the item list (items.json)
    python tools/kb_verify/kbconsist.py items [--json]
    python tools/kb_verify/kbconsist.py show <ID>            ONE item (the agent's only view of it)
    python tools/kb_verify/kbconsist.py pending [--all]
    python tools/kb_verify/kbconsist.py check [--ids ...] [--json]
    python tools/kb_verify/kbconsist.py rescan
    python tools/kb_verify/kbconsist.py report --date YYYY-MM-DD [--stopped REASON]
    python tools/kb_verify/kbconsist.py apply [--exclude PATH ...] [--dry-run]
    python tools/kb_verify/kbconsist.py dupes [--json]       the duplicate groups alone

Five kinds of item:

  P<n>  propagation: an edit the per-document pass landed corrected a fact
        about some anchors (an address, an fdps_ symbol, a file name); every
        other knowledge-base line naming the same anchors is a place the same
        wrong fact may still stand.
  D<n>  duplicate: passages (paragraphs, list items, table rows) in different
        places whose wording overlaps heavily -- by character 4-grams, so the
        grouping is mechanical and the agent decides whether it is one fact
        with two owners.
  X<n>  conflict: a per-document verifier noticed that another document says
        something different about the same thing (its cross_doc list).
  PF<n> pitfall: a "a rebuild would get this wrong" candidate a verifier
        recorded; the agent decides whether it clears the bar and is missing
        from rebuild_info/pitfalls.md.
  R<x>  reclassification: a cut_content/ placement the ticket asks to look at
        again (fixed list RECLASS below, one entry per item).

X items are merged per pair of pages and anchor; a duplicate chain longer than
DUP_MAX_GROUP passages is handed out as its pairs.  Every item is one agent's
only work.
"""

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kbverify  # noqa: E402  (the scope, masks, verdicts and lint of the per-document pass)

REPO = kbverify.REPO
WORK = REPO / "workspace" / "kb_consist"
ITEMS = WORK / "items.json"
VERDICTS = WORK / "verdicts"
REPORT_STEM = "kb-consist"


def use_workspace(name, report_stem):
    """Point the item list, the verdicts and the report at another pass that
    shares this pass's verdict shape, gate, rescan and landing (kbrefused.py)."""
    global WORK, ITEMS, VERDICTS, REPORT_STEM
    WORK = REPO / "workspace" / name
    ITEMS = WORK / "items.json"
    VERDICTS = WORK / "verdicts"
    REPORT_STEM = report_stem
RUNS = kbverify.RUNS

# Pages written wholly by a generator: an edit there has to go into the generator.
GENERATED_PAGES = kbverify.GENERATED_PAGES

# The placements ticket 25.17 asks to look at again, one entry per item.
_RULES = ("CONTEXT.md \"刪減與未用\" has two rules that decide this: a member of the game's own "
          "module (not a third-party library) that is finished but never called is residual "
          "(殘留內容); code FDPS rewrote and never enabled is residual, and only code inherited "
          "UNCHANGED from FD2 is a predecessor leftover (前作遺留), which needs an FD2-side "
          "comparison (C:\\Users\\fdpsf\\Documents\\fd2-anatomy, its src/ and knowledge base).")
_MOVED = ("It used to sit in the exclusion list (排除清單) and is now listed under 殘留內容 in "
          "cut_content/code.md. Decide whether that move holds.")
RECLASS = {
    "R1a": "cut_content/code.md C16c (the 19 unused members of the MSCDEX access layer). "
           + _MOVED + " " + _RULES,
    "R1b": "cut_content/code.md C16d (the 3 unused members of the VFS reader). " + _MOVED + " " + _RULES,
    "R1c": "cut_content/code.md C16e (the 6 RLE drawing modes nothing reaches). " + _MOVED + " " + _RULES,
    "R2": ("cut_content/code.md C10 (the reader of the predecessor's archive files, "
           "fdps_load_indexed_archive_entry) is classed 前作遺留. Decide whether it is inherited "
           "unchanged from FD2 (前作遺留) or rewritten by FDPS and never enabled (殘留內容). " + _RULES),
    "R3a": ("cut_content/story.md places FDETXT30 entry 0x13 under S4 (殘留內容: dialogue written "
            "in full that nothing shows, with no shown version elsewhere). Decide whether its "
            "content is shown elsewhere in a reworded form (then it is a superseded draft, S13b, "
            "排除) or is a different turn of the story that is shown nowhere (S4). The owner "
            "table is OWNERS in tools/cut_content/story.py; the independent re-verification rated "
            "this boundary medium confidence. Entry 0x14 is a separate item."),
    "R3b": ("cut_content/story.md places FDETXT30 entry 0x14 under S13b (排除: a superseded draft "
            "whose reworded version is shown from another block). Decide whether its content is "
            "shown elsewhere in a reworded form (S13b) or is a different turn of the story shown "
            "nowhere (S4, 殘留內容). The owner table is OWNERS in tools/cut_content/story.py; the "
            "independent re-verification rated this boundary medium confidence. Entry 0x13 is a "
            "separate item."),
    "R4": ("The exclusion list of cut_content/_index.md has S19: the defeat condition 0x03 "
           "「全員死亡」 in the headers of the scene blocks FDETXT62-FDETXT65. It currently says the "
           "data cannot tell whether this is an earlier wording than the shipped one or a copy "
           "edited wrongly. Decide: a superseded draft (like S13b), a copy edited wrongly (a "
           "data-entry error), another class, or truly undecidable -- from the entry, the four "
           "blocks' headers and FDETXT27's, where each block was copied from (which chapter "
           "block its other entries match), and FD2's corresponding text. If it cannot be "
           "decided, keep it as it is."),
}

# ---------------------------------------------------------------- anchors

ANCHOR_PATTERNS = (
    re.compile(r"\b(?:fdps|data_fdps)_[A-Za-z0-9_]+"),
    re.compile(r"(?<![0-9A-Za-z_])0x[0-9a-fA-F]{4,6}(?![0-9A-Za-z])"),
    re.compile(r"(?<![0-9A-Za-z_])000[0-9a-fA-F]{5}(?![0-9A-Za-z])"),
    re.compile(r"\b[A-Z][A-Z0-9_\-]{2,}\.(?:DAT|VFS|CEL|SAF|TXT|COD|DTL|MPL|FON|PAL|SAV|LE|EXE)\b"),
)


def anchors(text):
    out = set()
    for pat in ANCHOR_PATTERNS:
        for m in pat.finditer(text):
            a = m.group(0)
            if a.startswith("000") and not a.startswith("0x"):
                a = "0x" + a.lstrip("0")
            if a.startswith("0x"):
                a = "0x" + a[2:].lower().lstrip("0")
                if len(a) < 6:          # 0x10 .. 0xfff: too common to anchor anything
                    continue
            out.add(a)
    return out


def kb_lines():
    """{doc: [(line_no, text)]} for every knowledge-base page, generated
    regions left out."""
    out = {}
    for rel in kbverify.all_kb_docs():
        if rel in ("README.md",):
            continue
        lines = kbverify.read_lines(rel)
        hidden = kbverify.masked_set(kbverify.masks(lines)[0])
        out[rel] = [(i + 1, l) for i, l in enumerate(lines) if i not in hidden]
    return out


# ------------------------------------------------------------- duplicates

DUP_MIN_CJK = 30
DUP_CONTAINMENT = 0.45
CJK = re.compile(r"[一-鿿]")
DUP_COMMON_SHINGLE = 40
DUP_MAX_GROUP = 6


def _norm(text):
    """The prose of a passage: links, code spans and punctuation removed, so
    two passages that only cite the same functions do not look alike."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"`[^`]*`", "", text)
    text = re.sub(r"[*|>#\-\s，。、；：「」（）()【】／]", "", text)
    return text


def _chapter_page(doc):
    return re.fullmatch(r"chapters/ch\d\d\.md", doc) is not None


def _index_row_for(index_passage, page_passage):
    """True when index_passage is a folder _index.md line that links the page
    page_passage sits on."""
    doc, text = index_passage[0], index_passage[3]
    if not doc.endswith("_index.md"):
        return False
    folder = doc.rsplit("/", 1)[0] if "/" in doc else ""
    page = page_passage[0]
    rel = page[len(folder) + 1:] if folder and page.startswith(folder + "/") else None
    return rel is not None and "](%s" % rel in text


def passages():
    """[(doc, first_line, last_line, text)] -- paragraphs, list items and table
    rows outside generated regions, headings and code fences."""
    out = []
    for rel in kbverify.all_kb_docs():
        if rel in ("README.md", "CONTEXT.md", "open_issues.md") or rel in GENERATED_PAGES:
            continue
        lines = kbverify.read_lines(rel)
        hidden = kbverify.masked_set(kbverify.masks(lines)[0])
        fence, para = False, []

        def flush():
            if para:
                out.append((rel, para[0][0], para[-1][0], " ".join(t for _, t in para)))
                para.clear()
        for i, line in enumerate(lines):
            if line.startswith("```"):
                fence = not fence
                flush()
                continue
            if fence or i in hidden or not line.strip() or line.startswith("#"):
                flush()
                continue
            if line.startswith("|"):
                flush()
                if not re.match(r"^\|\s*:?-{3}", line):
                    out.append((rel, i + 1, i + 1, line))
                continue
            if re.match(r"^\s*(?:[-*]|\d+\.)\s", line):
                flush()
            para.append((i + 1, line))
        flush()
    # Prose only: a passage needs enough CJK text to state something.
    return [p for p in out if len(CJK.findall(_norm(p[3]))) >= DUP_MIN_CJK]


def shingles(text, k=4):
    t = _norm(text)
    return {t[i:i + k] for i in range(len(t) - k + 1)}


def duplicate_groups():
    ps = passages()
    sh = [shingles(p[3]) for p in ps]
    posting = {}
    for idx, s in enumerate(sh):
        for g in s:
            posting.setdefault(g, []).append(idx)
    pairs = []
    for idx, s in enumerate(sh):
        counts = {}
        for g in s:
            post = posting[g]
            if len(post) > DUP_COMMON_SHINGLE:
                continue
            for j in post:
                if j > idx:
                    counts[j] = counts.get(j, 0) + 1
        for j, c in counts.items():
            a, b = ps[idx], ps[j]
            if a[0] == b[0] and abs(a[1] - b[1]) <= 1:
                continue
            # Each chapter page restates its own chapter's flow in the same
            # words by design; the cross-chapter owner is chapters/_index.md.
            if a[0] != b[0] and _chapter_page(a[0]) and _chapter_page(b[0]):
                continue
            # A folder index's row describes the page it links to by design.
            if _index_row_for(a, b) or _index_row_for(b, a):
                continue
            common = len(s & sh[j])
            if common / max(1, min(len(s), len(sh[j]))) >= DUP_CONTAINMENT:
                pairs.append((idx, j))
    parent = list(range(len(ps)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b in pairs:
        parent[find(a)] = find(b)
    groups = {}
    for a, b in pairs:
        groups.setdefault(find(a), set()).update((a, b))
    out = []
    for root, members in groups.items():
        # One agent reads a group whole.  A chain that grew past DUP_MAX_GROUP
        # passages is handed out as its pairs instead, so no agent gets a heap.
        parts = [members] if len(members) <= DUP_MAX_GROUP else \
            [{a, b} for a, b in pairs if find(a) == root]
        for part in parts:
            out.append(sorted((ps[m][0], ps[m][1], ps[m][2], ps[m][3]) for m in part))
    out.sort(key=lambda g: (g[0][0], g[0][1], g[-1][0], g[-1][1]))
    return out


# ------------------------------------------------------------------ items

FACT_KINDS = ("wrong_fact", "imprecise", "stale_count", "owner_violation")


def _span_lines(doc, text):
    """0-based line numbers the text occupies in doc now, or an empty set."""
    page = (REPO / doc).read_text(encoding="utf-8")
    if text not in page:
        return set()
    first = page.count("\n", 0, page.index(text))
    return set(range(first, first + text.count("\n") + 1))


def propagation_items(applied, lines):
    """One item per landed fact correction whose anchors occur on some other
    line: in other pages, and in its own page outside the corrected text."""
    out = []
    for a in applied:
        if a["kind"] not in FACT_KINDS:
            continue
        keys = anchors(a["old"])
        if not keys:
            continue
        own = _span_lines(a["doc"], a["new"])
        hits = []
        for doc, rows in lines.items():
            for no, text in rows:
                if doc == a["doc"] and (no - 1) in own:
                    continue
                shared = keys & anchors(text)
                if shared:
                    hits.append({"doc": doc, "line": no, "anchors": sorted(shared), "text": text})
        if hits:
            out.append({"kind": "propagation", "source": a, "anchors": sorted(keys), "hits": hits})
    return out


def conflict_items():
    """The verifiers' cross_doc notes, one item per pair of pages and anchor:
    two verifiers noticing the same disagreement make one item, not two."""
    merged = {}
    for u in kbverify.units():
        path = kbverify.verdict_path(u["id"])
        v, _ = kbverify._load(path) if path.is_file() else (None, None)
        for c in (v or {}).get("cross_doc", []):
            key = (tuple(sorted((u["doc"], c["other_doc"]))), c["anchor"].strip())
            entry = merged.setdefault(key, {"kind": "conflict", "docs": list(key[0]),
                                            "anchor": key[1], "notes": []})
            entry["notes"].append({"unit": u["id"], "doc": u["doc"], "other_doc": c["other_doc"],
                                   "note": c["note"]})
    return [merged[k] for k in sorted(merged)]


def pitfall_items():
    """Every pitfall candidate a verifier recorded, one item each."""
    out = []
    for u in kbverify.units():
        path = kbverify.verdict_path(u["id"])
        v, _ = kbverify._load(path) if path.is_file() else (None, None)
        for text in (v or {}).get("pitfall_candidates", []):
            out.append({"kind": "pitfall", "unit": u["id"], "doc": u["doc"], "candidate": text})
    return out


def build_items():
    applied = json.loads(kbverify.APPLIED.read_text(encoding="utf-8")) \
        if kbverify.APPLIED.is_file() else []
    items = []
    for prefix, found in (("P", propagation_items(applied, kb_lines())),
                          ("D", [{"kind": "duplicate",
                                  "passages": [{"doc": d, "first": f, "last": l, "text": t}
                                               for d, f, l, t in g]}
                                 for g in duplicate_groups()]),
                          ("X", conflict_items()),
                          ("PF", pitfall_items())):
        items += [dict(it, id="%s%d" % (prefix, k)) for k, it in enumerate(found, 1)]
    items += [{"id": rid, "kind": "reclass", "question": text} for rid, text in RECLASS.items()]
    for it in items:
        it["sha1"] = kbverify.sha1(json.dumps({k: v for k, v in it.items() if k != "sha1"},
                                              ensure_ascii=False, sort_keys=True))
    return items


def load_items():
    if not ITEMS.is_file():
        return []
    return json.loads(ITEMS.read_text(encoding="utf-8"))


def by_id():
    return {i["id"]: i for i in load_items()}


def render(item):
    out = []
    if item["kind"] == "propagation":
        a = item["source"]
        out.append("Kind: propagation of a correction")
        out.append("The per-document pass changed one statement in %s. Reason: %s" % (a["doc"], a["problem"]))
        out.append("Before: %s" % a["old"])
        out.append("After:  %s" % a["new"])
        out.append("")
        out.append("Anchors in the old statement: %s" % ", ".join(item["anchors"]))
        out.append("Knowledge-base lines naming the same anchors (%d; a starting point -- grep "
                   "for more yourself):" % len(item["hits"]))
        for h in item["hits"][:120]:
            out.append("  %s:%d [%s] %s" % (h["doc"], h["line"], ",".join(h["anchors"]), h["text"][:400]))
        if len(item["hits"]) > 120:
            out.append("  ... %d more lines not listed" % (len(item["hits"]) - 120))
        out.append("")
        out.append("Question: does any of these places carry the same wrong fact, or contradict "
                   "the corrected one?  Fix each that does.")
    elif item["kind"] == "duplicate":
        out.append("Kind: duplicate")
        out.append("These passages overlap heavily in wording (grouped mechanically by character "
                   "4-grams; that does not make them duplicates):")
        for p in item["passages"]:
            out.append("")
            out.append("  %s lines %d-%d:" % (p["doc"], p["first"], p["last"]))
            out.append("  %s" % p["text"][:1500])
        out.append("")
        out.append("Question: is this one fact written more than once?  Each fact has exactly one "
                   "owner (README.md).  If so, keep it in the owner (the folders' _index.md and "
                   "cut_content/_index.md \"與其他正典的分工\" say which) and cut the others down to a "
                   "sentence with a link.  Passages that contradict each other are made to agree "
                   "with the evidence; passages that are each right and say different things stay.")
    elif item["kind"] == "conflict":
        out.append("Kind: conflict noticed by the per-document verifiers")
        out.append("Pages: %s" % ", ".join(item["docs"]))
        out.append("Anchor: %s" % item["anchor"])
        for n in item["notes"]:
            out.append("  reported by the verifier of %s (unit %s), about %s: %s"
                       % (n["doc"], n["unit"], n["other_doc"], n["note"]))
        out.append("")
        out.append("Question: which statement is true (check any other page naming the same "
                   "anchor too)?  Fix the wrong side and make sure the fact has one owner.")
    elif item["kind"] == "pitfall":
        out.append("Kind: pitfall candidate")
        out.append("The verifier of %s (unit %s) proposed:" % (item["doc"], item["unit"]))
        out.append("  %s" % item["candidate"])
        out.append("")
        out.append("Question: is this something a rebuild written by intuition or compiler "
                   "convention would get different from the original (rebuild_info/_index.md "
                   "gives the bar), and is it missing from rebuild_info/pitfalls.md?  If both, "
                   "add one row to the right section of pitfalls.md in its table's shape (edit: "
                   "old = an existing row, new = that row + a newline + the new row), linking the "
                   "page that owns the fact.  If it is already there, or below the bar, change "
                   "nothing and say why.")
    elif item["kind"] == "refused":
        out.append("Kind: an edit the consistency pass could not land")
        out.append("Consistency item %s (%s) settled on: %s" % (item["source_id"], item["source_kind"],
                                                             item["source_conclusion"]))
        out.append("One of its edits was refused at landing because its old text was no longer "
                   "in the page: another edit landed first on the same passage.")
        out.append("  page: %s" % item["doc"])
        out.append("  why the edit was made: %s" % item["why"])
        out.append("  old: %s" % item["old"])
        out.append("  new: %s" % item["new"])
        out.append("")
        out.append("See what happened to that passage: git diff %s -- %s (the page before both "
                   "passes landed, against the working tree), and the other items' verdicts in "
                   "workspace\\kb_consist\\verdicts\\ and workspace\\kb_verify\\verdicts\\."
                   % (item["base"], item["doc"]))
        out.append("")
        out.append("Question: does the page as it stands now state what this edit meant to "
                   "state -- the fact, not the wording?  If yes (the edit that landed covers it, "
                   "possibly better), verdict consistent with no edits.  If the landed wording "
                   "is wrong or lost part of this fix, write the edit that brings the current "
                   "text to the correct, complete statement (old copied from the page as it is "
                   "now).  Check the fact itself against first-hand sources if the two edits "
                   "disagree on it.")
    elif item["kind"] == "reclass":
        out.append("Kind: reclassification of a cut_content/ entry")
        out.append(item["question"])
        out.append("")
        out.append("The classes: CONTEXT.md \"刪減與未用\" and cut_content/_index.md \"分類\".  When the "
                   "class changes, the edits move the entry to the matching section of its topic "
                   "page (or to the exclusion list) and change its 分類 line; the summary table "
                   "is regenerated by tools/cut_content/cut_content.py index, so leave it alone; "
                   "an OWNERS change in tools/cut_content/story.py goes into outside.")
    return "\n".join(out)


# ------------------------------------------------------------------ gate

VERDICT_VALUES = ("consistent", "fixed", "cannot_settle")
REQUIRED = ("id", "item_sha1", "verdict", "conclusion", "evidence", "edits", "outside",
            "confidence", "open_question", "developer_question")


def check_edits(edits, where="edits"):
    problems = []
    if not isinstance(edits, list):
        return ["%s must be a list" % where]
    docs = set(kbverify.all_kb_docs())
    texts = {}
    spans = {}
    for k, e in enumerate(edits):
        w = "%s[%d]" % (where, k)
        if not isinstance(e, dict) or not all(isinstance(e.get(x), str) for x in ("doc", "old", "new", "why")):
            problems.append("%s needs doc, old, new, why strings" % w)
            continue
        if e["doc"] not in docs:
            problems.append("%s: %s is not a knowledge-base page" % (w, e["doc"]))
            continue
        if e["doc"] in GENERATED_PAGES:
            problems.append("%s: %s is generated; name the generator fix in outside" % (w, e["doc"]))
            continue
        if not e["old"].strip() or e["old"] == e["new"]:
            problems.append("%s: empty or no-op edit" % w)
            continue
        text = texts.setdefault(e["doc"], (REPO / e["doc"]).read_text(encoding="utf-8"))
        c = text.count(e["old"])
        if c != 1:
            problems.append("%s: old occurs %d times in %s" % (w, c, e["doc"]))
            continue
        lines = text.split("\n")
        hidden = kbverify.masked_set(kbverify.masks(lines)[0])
        s = text.index(e["old"])
        first = text.count("\n", 0, s)
        if any(i in hidden for i in range(first, first + e["old"].count("\n") + 1)):
            problems.append("%s: old lies inside a generated region" % w)
            continue
        for a, b, j in spans.get(e["doc"], []):
            if s < b and a < s + len(e["old"]):
                problems.append("%s overlaps edits[%d]" % (w, j))
        spans.setdefault(e["doc"], []).append((s, s + len(e["old"]), k))
    return problems


def check_verdict(v, item):
    if not isinstance(v, dict):
        return ["not a JSON object"]
    problems = ["missing field %s" % k for k in REQUIRED if k not in v]
    if problems:
        return problems
    if v["id"] != item["id"]:
        problems.append("id mismatch")
    if v["item_sha1"] != item["sha1"]:
        problems.append("stale: judged a different item")
    if v["verdict"] not in VERDICT_VALUES:
        problems.append("verdict %r not one of %s" % (v["verdict"], VERDICT_VALUES))
    if v["confidence"] not in kbverify.CONFIDENCE:
        problems.append("confidence %r" % v["confidence"])
    if not isinstance(v["conclusion"], str) or len(v["conclusion"].strip()) < 15:
        problems.append("conclusion must state the settled fact")
    # A duplicate or a refused edit is about the pages' own wording; the rest
    # are claims about the program or the data and need first-hand evidence.
    problems += kbverify.check_evidence(v["evidence"], item["kind"] not in ("duplicate", "refused"))
    if v.get("_landed"):
        return problems             # the edits are in the pages now
    problems += check_edits(v["edits"])
    if v["verdict"] == "fixed" and not v["edits"] and not str(v["outside"]).strip():
        problems.append("fixed without edits or an outside fix")
    if v["verdict"] == "consistent" and v["edits"]:
        problems.append("consistent with edits: say fixed")
    rr = v.get("_reread")
    if rr is not None:
        if not isinstance(rr, dict) or rr.get("decision") not in ("confirm", "amend", "reject"):
            problems.append("_reread.decision must be confirm, amend or reject")
        elif not isinstance(rr.get("why"), str) or len(rr["why"].strip()) < 10:
            problems.append("_reread.why must say what was checked")
        elif rr["decision"] == "amend":
            problems += check_edits(rr.get("edits"), "_reread.edits")
    return problems


def needs_reread(v):
    return bool(v["edits"]) or v["confidence"] != "high" or v["verdict"] == "cannot_settle" \
        or bool(str(v["outside"]).strip())


def verdict_path(iid):
    return VERDICTS / ("%s.json" % iid)


def states(items):
    rows = []
    for it in items:
        p = verdict_path(it["id"])
        if not p.is_file():
            rows.append({"id": it["id"], "state": "missing"})
            continue
        v, err = kbverify._load(p)
        problems = [err] if err else check_verdict(v, it)
        rows.append({"id": it["id"], "state": "failing" if problems else "done",
                     **({"problems": problems} if problems else {})})
    return rows


def rescan_todo():
    out = []
    for row in states(load_items()):
        if row["state"] != "done":
            continue
        v, _ = kbverify._load(verdict_path(row["id"]))
        if needs_reread(v) and "_reread" not in v:
            why = []
            if v["edits"]:
                why.append("%d edit(s)" % len(v["edits"]))
            if v["confidence"] != "high":
                why.append("confidence %s" % v["confidence"])
            if v["verdict"] == "cannot_settle":
                why.append("cannot settle")
            if str(v["outside"]).strip():
                why.append("fix outside the knowledge base")
            out.append({"id": row["id"], "why": "; ".join(why)})
    return out


def final_edits(v):
    rr = v.get("_reread")
    if rr is None:
        return [] if needs_reread(v) else v["edits"]
    if rr["decision"] == "reject":
        return []
    return v["edits"] if rr["decision"] == "confirm" else (rr.get("edits") or [])


def apply(exclude=(), dry_run=False):
    applied, refused = [], []
    texts = {}
    landed = []
    for it in load_items():
        # Everything that is not landed here says why (ADR-0007 5.3).
        if not verdict_path(it["id"]).is_file():
            refused.append({"id": it["id"], "doc": "-", "why": "no verdict"})
            continue
        v, err = kbverify._load(verdict_path(it["id"]))
        if v is None:
            refused.append({"id": it["id"], "doc": "-", "why": err})
            continue
        if v.get("_landed"):
            continue                                    # landed by an earlier apply
        problems = check_verdict(v, it)
        if problems:
            refused.append({"id": it["id"], "doc": "-",
                            "why": "verdict fails the gate: " + "; ".join(problems[:3])})
            continue
        if needs_reread(v) and "_reread" not in v:
            refused.append({"id": it["id"], "doc": "-", "why": "no second reading yet"})
            continue
        edits = final_edits(v)
        if any(e["doc"] in exclude for e in edits):
            refused += [{"id": it["id"], "doc": e["doc"], "why": kbverify.EXCLUDED} for e in edits]
            continue
        landed.append(it["id"])
        for e in edits:
            text = texts.setdefault(e["doc"], (REPO / e["doc"]).read_text(encoding="utf-8"))
            if text.count(e["old"]) != 1:
                refused.append({"id": it["id"], "doc": e["doc"],
                                "why": "old occurs %d times now" % text.count(e["old"])})
                continue
            texts[e["doc"]] = text.replace(e["old"], e["new"], 1)
            applied.append({"id": it["id"], "doc": e["doc"]})
    if not dry_run:
        for doc, text in texts.items():
            if (REPO / doc).read_text(encoding="utf-8") != text:
                if doc == "chapters/_index.md":
                    problems = kbverify.mirror_index_sources(
                        (REPO / doc).read_text(encoding="utf-8"), text)
                    if problems:
                        refused += [{"id": "-", "doc": doc, "why": p} for p in problems]
                        continue
                (REPO / doc).write_bytes(text.encode("utf-8"))
        bad_docs = {r["doc"] for r in refused if r["id"] == "-"}
        applied = [a for a in applied if a["doc"] not in bad_docs]
        for iid in landed:
            v, _ = kbverify._load(verdict_path(iid))
            if any(e["doc"] in bad_docs for e in final_edits(v)):
                continue
            v["_landed"] = True
            mine = [r for r in refused if r["id"] == iid]
            if mine:
                v["_refused"] = mine
            verdict_path(iid).write_text(json.dumps(v, ensure_ascii=False, indent=1) + "\n",
                                         encoding="utf-8")
    return applied, refused


def summarize(stopped):
    items = load_items()
    rows = states(items)
    out = {"items": len(items), "by_kind": {}, "complete": 0,
           "unfinished": [r for r in rows if r["state"] != "done"], "by_verdict": {},
           "edits_landing": 0, "rejected": [], "outside_fixes": [], "developer_questions": [],
           "still_open": [], "not_reread": [], "refused_at_landing": [], "stopped": stopped}
    for it in items:
        out["by_kind"][it["kind"]] = out["by_kind"].get(it["kind"], 0) + 1
    for r in rows:
        if r["state"] != "done":
            continue
        out["complete"] += 1
        v, _ = kbverify._load(verdict_path(r["id"]))
        out["by_verdict"][v["verdict"]] = out["by_verdict"].get(v["verdict"], 0) + 1
        out["edits_landing"] += len(final_edits(v))
        if (v.get("_reread") or {}).get("decision") == "reject":
            out["rejected"].append(r["id"])
        if str(v["outside"]).strip():
            out["outside_fixes"].append({"id": r["id"], "outside": v["outside"]})
        if str(v["developer_question"]).strip():
            out["developer_questions"].append({"id": r["id"], "question": v["developer_question"]})
        if v["confidence"] != "high" or str(v["open_question"]).strip():
            out["still_open"].append({"id": r["id"], "confidence": v["confidence"],
                                      "open_question": v["open_question"]})
        if needs_reread(v) and "_reread" not in v:
            out["not_reread"].append(r["id"])       # its edits cannot land yet
        if v.get("_refused"):
            out["refused_at_landing"] += v["_refused"]
    return out


# ------------------------------------------------------------------- CLI

def main(builder=None):
    """The CLI; builder replaces build_items for a pass that shares this one's
    machinery with its own item list (kbrefused.py)."""
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("freeze").add_argument("--refresh", action="store_true")
    sub.add_parser("items").add_argument("--json", action="store_true")
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
    p.add_argument("--dry-run", action="store_true")
    sub.add_parser("dupes").add_argument("--json", action="store_true")
    a = ap.parse_args()

    if a.cmd == "freeze":
        if ITEMS.is_file() and not a.refresh:
            print(json.dumps({"frozen": True, "items": len(load_items())}))
            return 0
        items = (builder or build_items)()
        WORK.mkdir(parents=True, exist_ok=True)
        ITEMS.write_text(json.dumps(items, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        counts = {}
        for it in items:
            counts[it["kind"]] = counts.get(it["kind"], 0) + 1
        print(json.dumps({"frozen": True, "items": len(items), "by_kind": counts}, ensure_ascii=False))
        return 0
    if a.cmd == "items":
        items = load_items()
        if a.json:
            print(json.dumps([{"id": i["id"], "kind": i["kind"]} for i in items], ensure_ascii=False))
        else:
            for i in items:
                print(i["id"], i["kind"])
        return 0
    if a.cmd == "show":
        it = by_id().get(a.id)
        if it is None:
            print("no item %s" % a.id, file=sys.stderr)
            return 1
        print("item: %s\nitem_sha1: %s\nverdict_file: %s\n" % (it["id"], it["sha1"], verdict_path(it["id"])))
        print(render(it))
        return 0
    if a.cmd == "pending":
        rows = states(load_items())
        if a.all:
            print(json.dumps([{"id": r["id"], "state": r["state"]} for r in rows], ensure_ascii=False, indent=1))
        else:
            print(json.dumps([r["id"] for r in rows if r["state"] != "done"]))
        return 0
    if a.cmd == "check":
        table = by_id()
        wanted = a.ids or list(table)
        unknown = [i for i in wanted if i not in table]
        rows = states([table[i] for i in wanted if i in table])
        result = {"checked": len(wanted), "ok": sum(r["state"] == "done" for r in rows),
                  "missing": [r["id"] for r in rows if r["state"] == "missing"] + unknown,
                  "failures": {r["id"]: r["problems"] for r in rows if r["state"] == "failing"}}
        result["gate_passed"] = not result["missing"] and not result["failures"]
        if a.json:
            print(json.dumps(result, ensure_ascii=False, indent=1))
        else:
            print("checked=%d ok=%d missing=%d failing=%d" % (
                result["checked"], result["ok"], len(result["missing"]), len(result["failures"])))
            for i, pr in result["failures"].items():
                print("  FAIL %s: %s" % (i, "; ".join(pr)))
            print("GATE %s" % ("PASSED" if result["gate_passed"] else "FAILED"))
        return 0 if result["gate_passed"] else 1
    if a.cmd == "rescan":
        print(json.dumps(rescan_todo(), ensure_ascii=False, indent=1))
        return 0
    if a.cmd == "report":
        summary = summarize(a.stopped)
        RUNS.mkdir(parents=True, exist_ok=True)
        stem = "%s-%s" % (a.date, REPORT_STEM)
        written = [RUNS / ("%s-summary.json" % stem)]
        written[0].write_text(json.dumps(summary, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        if not a.stopped:
            archive = []
            for it in load_items():
                p = verdict_path(it["id"])
                if p.is_file():
                    v, _ = kbverify._load(p)
                    if v is not None:
                        archive.append(dict(v, item=render(it)))
            written.append(RUNS / ("%s-verdicts.json" % stem))
            written[1].write_text(json.dumps(archive, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        print(json.dumps({"written": [str(p) for p in written], "complete": summary["complete"],
                          "items": summary["items"],
                          "unfinished": [r["id"] for r in summary["unfinished"]]}, ensure_ascii=False))
        return 0
    if a.cmd == "apply":
        applied, refused = apply(set(a.exclude), a.dry_run)
        print(json.dumps({"applied": len(applied), "refused": refused,
                          "docs": sorted({x["doc"] for x in applied})}, ensure_ascii=False, indent=1))
        return 1 if any(r["why"] != kbverify.EXCLUDED for r in refused) else 0
    if a.cmd == "dupes":
        groups = duplicate_groups()
        if a.json:
            print(json.dumps(groups, ensure_ascii=False, indent=1))
        else:
            for g in groups:
                print("-- group of %d" % len(g))
                for d, f, l, t in g:
                    print("   %s:%d-%d %s" % (d, f, l, t[:110]))
            print("%d groups" % len(groups))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
