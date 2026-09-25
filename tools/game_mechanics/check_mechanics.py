"""check_mechanics.py -- the gate for the game-mechanics pages (ticket 25.5).

Ticket 25.5 writes program_info/ pages that answer "how does this game work":
formulas, rules and flows, one subsystem per page, every rule tied to the
function that implements it.  A drafting agent writes each page under
workspace/game_mechanics/drafts/; land.py copies a clean draft into
program_info/.  This script is the gate both sides run.

Errors (exit 1):
  no-verification-target  the page does not open with a 驗證對象 line/section
  citation-mismatch       `name`（`0xaddr`） names a symbol the Ghidra snapshot
                          does not have at that address
  unknown-symbol          a backticked fdps_ symbol exists neither in the
                          snapshot nor anywhere in src/
  uncited-section         a ## section ties no rule to any function
  broken-link             a relative link resolves to nothing
  forbidden-reference     the page cites workspace/ or legacy/
  narrative               the page narrates the analysis (dates, "後來發現",
                          ticket numbers) instead of stating the conclusion
Warnings:
  pending-link            a link to cut_content/_index.md before ticket 25.10
                          creates the folder (any deeper cut_content/ path is
                          a guess and an error)

A draft is checked as if it already lived in program_info/, except that a link
to a sibling page that is still a draft passes.  --links runs only the link
check, on shared pages other workflow stages edit (pitfalls.md, _index.md).

What it cannot see: whether a formula is right, and whether EVERY rule (not
just every section) names its function.  That is the drafting agent's reading
of the assembly, and ticket 25.17's verification.

Usage: python tools/game_mechanics/check_mechanics.py --draft DOC [DOC ...]
       python tools/game_mechanics/check_mechanics.py --landed DOC [DOC ...]
       python tools/game_mechanics/check_mechanics.py --links FILE [FILE ...]
       python tools/game_mechanics/check_mechanics.py --selftest
       (add --json for machine-readable output)
Exit : 0 when no page has an error.
"""
import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DRAFTS = ROOT / "workspace" / "game_mechanics" / "drafts"
LANDED = ROOT / "program_info"
SRC = ROOT / "src"

# The Ghidra snapshot's name table has one owner (tools/_index.md).
sys.path.insert(0, str(ROOT / "tools" / "data_emit"))


def check_text(text, names, idents, doc_dir, repo_root, alt_dir=None):
    """Findings for one page's text.

    doc_dir is the folder the page lives (or will live) in; relative links are
    resolved from there.  alt_dir, when given, is where sibling pages that are
    still drafts can be found -- a link that misses doc_dir but hits alt_dir
    passes, because both pages land together.
    """
    findings = []
    lines = text.splitlines()
    head = lines[:15]
    if not any(l.lstrip("#* ").startswith("驗證對象") for l in head):
        findings.append(("error", "no-verification-target", 1,
                         "the page must open with a 驗證對象 line or section"))
    for no, line in enumerate(lines, 1):
        for name, addr in citations(line):
            known = names.get(name)
            if not known:
                findings.append(("error", "citation-mismatch", no,
                                 "`%s` is not a symbol in the Ghidra snapshot" % name))
            elif addr not in known:
                findings.append(("error", "citation-mismatch", no,
                                 "`%s` is at %s in the Ghidra snapshot, not 0x%x"
                                 % (name, ", ".join("0x%x" % a for a in sorted(known)), addr)))
        for m in SYMBOL.finditer(line):
            sym = m.group(1)
            if sym not in names and sym not in idents:
                findings.append(("error", "unknown-symbol", no,
                                 "`%s` is neither in the Ghidra snapshot nor in src/" % sym))
    for no, line in enumerate(lines, 1):
        if FORBIDDEN.search(line):
            findings.append(("error", "forbidden-reference", no,
                             "the knowledge base may not cite workspace/ or legacy/"))
        m = NARRATIVE.search(line)
        if m:
            findings.append(("error", "narrative", no,
                             "'%s' describes how the analysis went; that belongs in devlog/"
                             % m.group(0)))
    findings.extend(check_sections(lines))
    if doc_dir is not None:
        findings.extend(check_links(lines, Path(doc_dir), Path(repo_root), alt_dir))
    return findings


LINK = re.compile(r"\]\(([^)\s]+)\)")


def check_links(lines, doc_dir, repo_root, alt_dir=None):
    """Relative links must resolve from where the page will live.

    cut_content/ is owned by a later ticket (25.10).  Until the folder exists,
    the only link into it that may be written is its _index.md -- reported as
    pending, not broken.  Any deeper path would be a guess at 25.10's layout
    that nobody would come back to fix, so it is an error.
    """
    out = []
    pending_root = (repo_root / "cut_content").resolve()
    for no, line in enumerate(lines, 1):
        for m in LINK.finditer(line):
            target = m.group(1)
            if re.match(r"[a-z]+:", target) or target.startswith("#"):
                continue
            rel = target.split("#", 1)[0]
            path = (doc_dir / rel).resolve()
            if path.exists():
                continue
            if alt_dir is not None and path.parent == Path(doc_dir).resolve() \
                    and (Path(alt_dir) / path.name).exists():
                continue
            if path == pending_root / "_index.md" and not pending_root.exists():
                out.append(("warning", "pending-link", no,
                            "%s does not exist yet (cut_content/ is ticket 25.10's)" % target))
            else:
                out.append(("error", "broken-link", no, "%s does not exist" % target))
    return out


def check_file_links(path, repo_root):
    """Link findings for any knowledge-base file, resolved from its own folder."""
    lines = path.read_text(encoding="utf-8").splitlines()
    return check_links(lines, path.parent, repo_root)


# Sections that describe the page rather than the game carry no rule to cite.
EXEMPT_SECTIONS = ("驗證對象", "相關文件", "另見")


def check_sections(lines):
    """Every ## section must tie at least one rule to a function by citation."""
    out = []
    title, start, cited = None, 0, False

    def close():
        if title is not None and not cited and not any(e in title for e in EXEMPT_SECTIONS):
            out.append(("error", "uncited-section", start,
                        "section '%s' cites no function as `name`（`0xaddr`）" % title))

    for no, line in enumerate(lines, 1):
        if line.startswith("## "):
            close()
            title, start, cited = line[3:].strip(), no, False
        elif any(True for _ in citations(line)):
            cited = True
    close()
    return out


FORBIDDEN = re.compile(r"(?<![A-Za-z0-9_])(?:workspace|legacy)/")

# Phrases that narrate the analysis instead of stating a conclusion (CLAUDE.md:
# no "solved at some time / used to be / phase" in the knowledge base).  Kept
# narrow on purpose: words like 原版 or 之後 describe the game, not the work.
NARRATIVE = re.compile(
    r"一開始以為|原本以為|本來以為|後來發現|後來才|起初|(?<![一-鿿])票\s*\d+(?:\.\d+)?"
    r"|\b20\d\d-\d\d-\d\d\b|devlog", re.IGNORECASE)

# A backticked game symbol, possibly followed by a call, member access or index.
SYMBOL = re.compile(r"`(fdps_[A-Za-z0-9_]+)(?:[^`]*)`")


# `name`（`0x1ecc7`） or `name` (`0001ecc7`) or `name @ 0x1ecc7` -- the ways a
# page ties a symbol to its address.
CITATION = re.compile(
    r"`([A-Za-z_][A-Za-z0-9_]*)`\s*[（(]\s*`(?:0x)?([0-9a-fA-F]{4,8})`\s*[）)]"
    r"|`([A-Za-z_][A-Za-z0-9_]*)\s*@\s*(?:0x)?([0-9a-fA-F]{4,8})`")


def citations(line):
    for m in CITATION.finditer(line):
        name = m.group(1) or m.group(3)
        addr = m.group(2) or m.group(4)
        yield name, int(addr, 16)


def selftest():
    failures = []

    def expect(label, cond):
        if not cond:
            failures.append(label)

    good_head = "# Battle\n\n驗證對象：`src/combat.c`。\n"
    codes = lambda text: {f[1] for f in check_text(text, {}, set(), None, None)}  # noqa: E731

    expect("missing verification target is an error",
           "no-verification-target" in codes("# Battle\n\nSome text.\n"))
    expect("a 驗證對象 line satisfies it",
           "no-verification-target" not in codes(good_head))

    names = {"fdps_calc_damage": {0x1ecc7}, "main": {0x29220}}

    def codes_n(text):
        return {f[1] for f in check_text(text, names, set(names), None, None)}

    expect("a citation whose address matches passes",
           "citation-mismatch" not in codes_n(good_head + "\n傷害由 `fdps_calc_damage`（`0x1ecc7`）算出。\n"))
    expect("ascii parentheses and 8-digit addresses are the same citation",
           "citation-mismatch" not in codes_n(good_head + "\n見 `main` (`00029220`)。\n"))
    expect("a citation naming the wrong address is an error",
           "citation-mismatch" in codes_n(good_head + "\n傷害由 `fdps_calc_damage`（`0x1ecc8`）算出。\n"))
    expect("a citation of a name the snapshot does not know is an error",
           "citation-mismatch" in codes_n(good_head + "\n由 `fdps_made_up`（`0x1ecc7`）算出。\n"))

    def codes_i(text, idents):
        return {f[1] for f in check_text(text, names, idents, None, None)}

    expect("an fdps_ symbol that exists nowhere is an error",
           "unknown-symbol" in codes_i(good_head + "\n`fdps_ghost` 負責。\n", set()))
    expect("an fdps_ symbol the source defines passes",
           "unknown-symbol" not in codes_i(good_head + "\n`fdps_ghost` 負責。\n", {"fdps_ghost"}))
    expect("a symbol known to the snapshot passes",
           "unknown-symbol" not in codes_i(good_head + "\n`fdps_calc_damage` 負責。\n", set()))
    uncited = good_head + "\n## 命中\n\n命中率是 90%。\n\n## 爆擊\n\n`main`（`0x29220`）決定。\n"
    found = [f for f in check_text(uncited, names, set(), None, None) if f[1] == "uncited-section"]
    expect("a section that ties no rule to a function is an error, and only that one",
           len(found) == 1 and "命中" in found[0][3])
    expect("a citation in a subsection counts for its section",
           "uncited-section" not in codes_n(good_head + "\n## 命中\n\n### 細節\n\n`main`（`0x29220`）。\n"))
    expect("the 驗證對象 and 相關文件 sections need no citation",
           "uncited-section" not in codes_n("# B\n\n## 驗證對象\n\n`src/x.c`\n\n## 相關文件\n\n見別處。\n"))
    with tempfile.TemporaryDirectory() as tmp:
        repo = Path(tmp)
        (repo / "program_info").mkdir()
        (repo / "rebuild_info").mkdir()
        (repo / "rebuild_info" / "pitfalls.md").write_text("x", encoding="utf-8")

        def link_findings(body):
            return check_text(good_head + body, names, set(), repo / "program_info", repo)

        def link_codes(body):
            return {f[1] for f in link_findings(body)}

        expect("a link to an existing file passes",
               "broken-link" not in link_codes("見 [踩雷點](../rebuild_info/pitfalls.md#不能修的原版-bug)。\n"))
        expect("a link to a missing file is an error",
               "broken-link" in link_codes("見 [格式](../resource_info/nothing.md)。\n"))
        expect("a link to cut_content/'s index before the folder exists is only a pending warning",
               [f[0] for f in link_findings("見 [抽獎](../cut_content/_index.md)。\n")
                if f[1] in ("broken-link", "pending-link")] == ["warning"])
        expect("a guessed page inside a cut_content/ that does not exist yet is an error",
               "broken-link" in link_codes("見 [抽獎](../cut_content/lottery.md)。\n"))
        expect("web links are not checked",
               "broken-link" not in link_codes("見 [站](https://example.com/x)。\n"))

        drafts = repo / "drafts"
        drafts.mkdir()
        (drafts / "spell.md").write_text("x", encoding="utf-8")
        sibling = good_head + "見 [法術](spell.md)。\n"
        expect("a draft may link a sibling page that so far exists only as a draft",
               "broken-link" not in {f[1] for f in check_text(
                   sibling, names, set(), repo / "program_info", repo, alt_dir=drafts)})
        expect("once landed, the same link must resolve for real",
               "broken-link" in {f[1] for f in check_text(
                   sibling, names, set(), repo / "program_info", repo)})

        (repo / "program_info" / "_index.md").write_text(
            "| [`a.md`](a.md) | x |\n| [`spell.md`](spell.md) | y |\n", encoding="utf-8")
        (repo / "program_info" / "spell.md").write_text("x", encoding="utf-8")
        found = check_file_links(repo / "program_info" / "_index.md", repo)
        expect("a shared page's links are checked from its own folder",
               [(f[1], f[2]) for f in found] == [("broken-link", 1)])

    expect("a bold 驗證對象 line, as the folder's pages write it, satisfies it",
           "no-verification-target" not in codes("# B\n\n**驗證對象**：`FDPS.LE` 位址 `0x3bade`。\n"))
    expect("game text about tickets is not a ticket number",
           "narrative" not in codes(good_head + "\n酒館給彩票 3 張。\n"))

    expect("citing workspace/ from the knowledge base is an error",
           "forbidden-reference" in codes(good_head + "\n資料在 `workspace/game_mechanics/x.json`。\n"))
    expect("citing legacy/ from the knowledge base is an error",
           "forbidden-reference" in codes(good_head + "\n見 legacy/old.md。\n"))
    for phrase in ("一開始以為是命中率", "後來發現是爆擊", "票 25.5 解出", "2026-09-25 確認",
                   "詳見 devlog"):
        expect("process narrative '%s' is an error" % phrase,
               "narrative" in codes(good_head + "\n" + phrase + "。\n"))
    expect("describing the game's own sequence is not narrative",
           "narrative" not in codes(good_head + "\n玩家回合結束後輪到敵方回合，原版的判定照舊。\n"))
    expect("the battle's own turn phases are not narrative",
           "narrative" not in codes(good_head + "\n`fdps_run_enemy_phase` 跑 enemy phase。\n"))

    expect("a member access on a known symbol is judged by the symbol",
           "unknown-symbol" not in codes_i(good_head + "\n`fdps_calc_damage->hp` 與 `fdps_calc_damage()`。\n", set()))

    for f in failures:
        print("FAIL", f)
    print("selftest: %d failure(s)" % len(failures))
    return 1 if failures else 0


def load_names():
    """{symbol: {address, ...}} from the Ghidra snapshot."""
    from check_data import load_original_names
    names = {}
    for addr, name in load_original_names().items():
        names.setdefault(name, set()).add(addr)
    return names


def load_idents():
    """Every fdps_ identifier that appears anywhere in src/."""
    idents = set()
    for path in SRC.iterdir():
        if path.suffix.lower() in (".c", ".h", ".asm"):
            idents.update(re.findall(r"\bfdps_[A-Za-z0-9_]+",
                                     path.read_text(encoding="utf-8", errors="replace")))
    return idents


def check_page(path, names, idents, draft=False):
    """Findings for one page, judged as if it lived in program_info/.

    A draft may link sibling pages that are still drafts themselves.
    """
    if not path.exists():
        return [("error", "missing", 0, "%s does not exist" % path)]
    return check_text(path.read_text(encoding="utf-8"), names, idents, LANDED, ROOT,
                      alt_dir=DRAFTS if draft else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--draft", nargs="+", metavar="DOC")
    ap.add_argument("--landed", nargs="+", metavar="DOC")
    ap.add_argument("--links", nargs="+", metavar="FILE",
                    help="link check only, for shared pages other stages edit")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.draft or a.landed or a.links):
        ap.error("give --draft, --landed or --links")
    report = {}
    if a.draft or a.landed:
        names, idents = load_names(), load_idents()
        for doc in a.draft or []:
            report[doc] = check_page(DRAFTS / (doc + ".md"), names, idents, draft=True)
        for doc in a.landed or []:
            report[doc] = check_page(LANDED / (doc + ".md"), names, idents)
    for f in a.links or []:
        path = Path(f) if Path(f).is_absolute() else ROOT / f
        report[f] = (check_file_links(path, ROOT) if path.exists()
                     else [("error", "missing", 0, "%s does not exist" % path)])
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
