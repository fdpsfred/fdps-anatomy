"""check_chapter.py -- page assembly and the gate for chapters/chNN.md (ticket 25.8).

A chapter page is a fixed skeleton: a title, ten ## sections in a fixed
order, and seven generated blocks (chapter_facts.py) sitting in their
sections as marker comments.  A drafting agent writes the prose sections and
leaves each marker as the bare opening comment; the judgements the generated
blocks need (waves, text readers, never-shown entries) go into the draft's
.meta.json.  fill() puts the generated content between the markers, so a
page is always "the agent's prose + blocks regenerated from data", and a
landed page can be re-checked against fresh data at any time.

    <!-- chapter_docs:KEY -->
    ...generated...
    <!-- /chapter_docs:KEY -->

Errors (exit 1):
  structure            title line, ## sections, or a marker is wrong/missing
  empty-section        a prose section has no prose
  missing-handler      處理流程 does not cite the chapter's init/post/end handler
  uncited-section      a section that states program behaviour cites no function
  citation-mismatch    `name`（`0xaddr`） disagrees with the Ghidra snapshot
  unknown-symbol       a backticked fdps_ symbol exists nowhere
  broken-link          a relative link resolves to nothing
  forbidden-reference  the page cites workspace/ or legacy/
  narrative            the page narrates the analysis
  judgement            the judgement record does not fit the chapter's data
  undecided            a generated block still says 未判定
  stale                (landed only) a generated block differs from a fresh
                       regeneration -- data, source or judgement moved
Warnings:
  pending-link         a link to cut_content/_index.md before the folder exists

Usage: python tools/chapter_docs/check_chapter.py --draft N [N ...] [--json]
       python tools/chapter_docs/check_chapter.py --landed N [N ...] [--json]
       python tools/chapter_docs/check_chapter.py --template N
Exit : 0 when no page has an error.
"""
import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "game_mechanics"))

import chapter_facts as facts  # noqa: E402
import check_mechanics as mech  # noqa: E402

ROOT = facts.ROOT
LANDED = ROOT / "chapters"
DRAFTS = facts.DRAFTS

# (section heading, generated block in it or None, needs prose, needs a citation)
SECTIONS = (
    ("概要", None, True, False),
    ("加入與離隊", None, True, False),
    ("勝敗條件與特殊機制", None, True, True),
    ("敵人配置", "deployments", True, False),
    ("寶物", "treasure", False, False),
    ("村莊", "village", False, False),
    ("處理流程", None, True, True),
    ("回合事件與格子事件", "events", False, False),
    ("過場腳本", "scripts", False, False),
    ("對話", "dialogue", False, False),
)
HEADER_BLOCK = "header"
BLOCK_KEYS = [HEADER_BLOCK] + [s[1] for s in SECTIONS if s[1]]
UNDECIDED = ("（未判定）", "（讀取端未判定）")

OPEN = "<!-- chapter_docs:{} -->"
CLOSE = "<!-- /chapter_docs:{} -->"


def title_line(n):
    return f"# 第 {n} 章　{facts.chapter_title(n)}"


def template(n):
    """The skeleton a drafting agent starts from."""
    lines = [title_line(n), "", OPEN.format(HEADER_BLOCK)]
    for heading, block, _, _ in SECTIONS:
        lines += ["", f"## {heading}", ""]
        if block:
            lines.append(OPEN.format(block))
    return "\n".join(lines) + "\n"


_REGION = re.compile(r"<!-- chapter_docs:(\w+) -->(?:\n.*?\n<!-- /chapter_docs:\1 -->)?", re.S)


def fill(text, n, judgement):
    """Every marker (bare or already filled) replaced by a fresh block."""
    def put(m):
        key = m.group(1)
        if key not in facts.BLOCKS:
            return m.group(0)
        body = facts.render_block(n, key, judgement)
        return f"{OPEN.format(key)}\n{body}\n{CLOSE.format(key)}"
    return _REGION.sub(put, text)


def regions(text):
    """{key: body} of the filled regions in a page."""
    return {m.group(1): m.group(0) for m in _REGION.finditer(text)}


def check_structure(lines, n):
    out = []
    if not lines or lines[0] != title_line(n):
        out.append(("error", "structure", 1, "the first line must be '%s'" % title_line(n)))
    headings = [(no, l[3:].strip()) for no, l in enumerate(lines, 1) if l.startswith("## ")]
    want = [s[0] for s in SECTIONS]
    if [h for _, h in headings] != want:
        out.append(("error", "structure", headings[0][0] if headings else 1,
                    "## sections must be exactly, in order: " + "、".join(want)
                    + "; found: " + "、".join(h for _, h in headings)))
        return out
    text = "\n".join(lines)
    first_section = headings[0][0]
    for key in BLOCK_KEYS:
        found = [no for no, l in enumerate(lines, 1) if l == OPEN.format(key)]
        if len(found) != 1:
            out.append(("error", "structure", found[1] if len(found) > 1 else 1,
                        "marker %s must appear exactly once" % OPEN.format(key)))
            continue
        no = found[0]
        if key == HEADER_BLOCK:
            if no > first_section:
                out.append(("error", "structure", no, "the header block belongs above ## 概要"))
            continue
        heading = next(s[0] for s in SECTIONS if s[1] == key)
        owner = [h for hno, h in headings if hno < no]
        if not owner or owner[-1] != heading:
            out.append(("error", "structure", no, "%s belongs in ## %s" % (key, heading)))
    other = set(re.findall(r"<!-- /?chapter_docs:(\w+) -->", text)) - set(BLOCK_KEYS)
    for key in sorted(other):
        out.append(("error", "structure", 1, "unknown marker chapter_docs:%s" % key))
    return out


def section_bodies(lines):
    """[(heading, first line no, [lines])] with generated regions removed."""
    out, cur, start, body, inside = [], None, 0, [], None
    for no, line in enumerate(lines, 1):
        m = re.match(r"<!-- (/?)chapter_docs:(\w+) -->$", line)
        if m:
            inside = None if m.group(1) else m.group(2)
            continue
        if line.startswith("## "):
            if cur is not None:
                out.append((cur, start, body))
            cur, start, body, inside = line[3:].strip(), no, [], None
            continue
        if cur is not None and inside is None:
            body.append(line)
    if cur is not None:
        out.append((cur, start, body))
    return out


def check_prose(lines, n):
    out = []
    handlers = facts.handler_tables()
    needs = {s[0]: s for s in SECTIONS}
    for heading, start, body in section_bodies(lines):
        spec = needs.get(heading)
        if spec is None:
            continue
        prose = [l for l in body if l.strip() and not l.startswith("#")]
        if spec[2] and not prose:
            out.append(("error", "empty-section", start, "## %s has no prose" % heading))
        if spec[3] and not any(True for l in body for _ in mech.citations(l)):
            out.append(("error", "uncited-section", start,
                        "## %s cites no function as `name`（`0xaddr`）" % heading))
        if heading == "處理流程":
            cited = {name for l in body for name, _ in mech.citations(l)}
            for kind in ("init", "post", "end"):
                name = handlers[kind][n - 1]
                if name not in cited:
                    out.append(("error", "missing-handler", start,
                                "## 處理流程 must cite %s" % name))
    return out


def generated_lines(lines):
    """Line numbers inside generated regions (game text lives there)."""
    inside, out = False, set()
    for no, line in enumerate(lines, 1):
        if re.match(r"<!-- chapter_docs:\w+ -->$", line):
            inside = True
        elif re.match(r"<!-- /chapter_docs:\w+ -->$", line):
            inside = False
        elif inside:
            out.add(no)
    return out


def check_symbols(lines, names, idents):
    out = []
    generated = generated_lines(lines)
    for no, line in enumerate(lines, 1):
        for name, addr in mech.citations(line):
            known = names.get(name)
            if not known:
                out.append(("error", "citation-mismatch", no,
                            "`%s` is not a symbol in the Ghidra snapshot" % name))
            elif addr not in known:
                out.append(("error", "citation-mismatch", no,
                            "`%s` is at %s in the Ghidra snapshot, not 0x%x"
                            % (name, ", ".join("0x%x" % a for a in sorted(known)), addr)))
        for m in mech.SYMBOL.finditer(line):
            if m.group(1) not in names and m.group(1) not in idents:
                out.append(("error", "unknown-symbol", no,
                            "`%s` is neither in the Ghidra snapshot nor in src/" % m.group(1)))
        if mech.FORBIDDEN.search(line):
            out.append(("error", "forbidden-reference", no,
                        "the knowledge base may not cite workspace/ or legacy/"))
        m = None if no in generated else mech.NARRATIVE.search(line)
        if m:
            out.append(("error", "narrative", no,
                        "'%s' describes how the analysis went; that belongs in devlog/" % m.group(0)))
    return out


def check_page_text(text, n, judgement, names, idents, draft):
    """Findings for one filled page."""
    lines = text.splitlines()
    out = check_structure(lines, n)
    out += check_prose(lines, n)
    out += check_symbols(lines, names, idents)
    out += mech.check_links(lines, LANDED, ROOT, DRAFTS if draft else None)
    if judgement is None:
        out.append(("error", "judgement", 1, "no judgement record"))
    else:
        for p in facts.validate_judgement(n, judgement):
            out.append(("error", "judgement", 1, p))
    for no, line in enumerate(lines, 1):
        if any(u in line for u in UNDECIDED):
            out.append(("error", "undecided", no, "a generated block still says it is undecided"))
    return out


def check_draft(n, names, idents):
    path = DRAFTS / f"ch{n:02d}.md"
    if not path.exists():
        return [("error", "missing", 0, "%s does not exist" % path)]
    judgement = facts.load_judgement(n, draft=True)
    text = fill(path.read_text(encoding="utf-8"), n, judgement)
    return check_page_text(text, n, judgement, names, idents, draft=True)


def check_landed(n, names, idents):
    path = LANDED / f"ch{n:02d}.md"
    if not path.exists():
        return [("error", "missing", 0, "%s does not exist" % path)]
    judgement = facts.load_judgement(n)
    text = path.read_text(encoding="utf-8")
    out = check_page_text(text, n, judgement, names, idents, draft=False)
    fresh = regions(fill(text, n, judgement))
    for key, body in regions(text).items():
        if fresh.get(key) != body:
            out.append(("error", "stale", 1,
                        "generated block %s differs from a fresh regeneration" % key))
    return out


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--draft", nargs="+", type=int, metavar="N")
    ap.add_argument("--landed", nargs="+", type=int, metavar="N")
    ap.add_argument("--template", type=int, metavar="N")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if a.template:
        print(template(a.template), end="")
        return 0
    if not (a.draft or a.landed):
        ap.error("give --draft, --landed or --template")
    names, idents = mech.load_names(), mech.load_idents()
    report = {}
    for n in a.draft or []:
        report[f"ch{n:02d}"] = check_draft(n, names, idents)
    for n in a.landed or []:
        report[f"ch{n:02d}"] = check_landed(n, names, idents)
    report = {k: [{"level": f[0], "code": f[1], "line": f[2], "message": f[3]} for f in fs]
              for k, fs in report.items()}
    errors = sum(1 for fs in report.values() for f in fs if f["level"] == "error")
    if a.json:
        print(json.dumps({"errors": errors, "pages": report}, ensure_ascii=False, indent=2))
    else:
        for doc, fs in report.items():
            n = sum(1 for f in fs if f["level"] == "error")
            print("%s: %s" % (doc, "clean" if n == 0 else "%d error(s)" % n))
            for f in fs:
                print("  %s %s line %d: %s" % (f["level"], f["code"], f["line"], f["message"]))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
