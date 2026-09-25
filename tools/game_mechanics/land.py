"""land.py -- copy clean game-mechanics drafts into program_info/ (ticket 25.5).

The transcription stage of the ticket 25.5 workflow.  It makes no judgement:
a draft either passes check_mechanics.py and is copied byte for byte, or it
fails and is refused with the gate's findings.  Nothing is edited on the way.

Usage: python tools/game_mechanics/land.py DOC [DOC ...] [--json]
       python tools/game_mechanics/land.py --selftest
Exit : 0 when every named draft landed (or was already landed unchanged).
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_mechanics as gate  # noqa: E402


def land(doc, drafts, landed, check):
    """Land one draft.  Returns (status, detail).

    status is one of: new, updated, unchanged, refused, missing.
    check(path) -> findings, as check_mechanics.check_page.
    """
    src = drafts / (doc + ".md")
    if not src.exists():
        return "missing", "no draft at %s" % src
    errors = [f for f in check(src) if f[0] == "error"]
    if errors:
        return "refused", "; ".join("%s line %d: %s" % (f[1], f[2], f[3]) for f in errors[:5])
    data = src.read_bytes()
    dst = landed / (doc + ".md")
    if dst.exists():
        if dst.read_bytes() == data:
            return "unchanged", ""
        status = "updated"
    else:
        status = "new"
    dst.write_bytes(data)
    return status, ""


def selftest():
    failures = []

    def expect(label, cond):
        if not cond:
            failures.append(label)

    def fake_check(path):
        text = path.read_text(encoding="utf-8")
        return [("error", "narrative", 1, "x")] if "BAD" in text else []

    with tempfile.TemporaryDirectory() as tmp:
        drafts, landed = Path(tmp) / "d", Path(tmp) / "l"
        drafts.mkdir()
        landed.mkdir()
        (drafts / "battle.md").write_text("# 戰鬥\n", encoding="utf-8")
        (drafts / "save.md").write_text("# BAD\n", encoding="utf-8")

        expect("a clean draft lands as new", land("battle", drafts, landed, fake_check)[0] == "new")
        expect("the landed page is the draft byte for byte",
               (landed / "battle.md").read_bytes() == (drafts / "battle.md").read_bytes())
        expect("landing it again changes nothing",
               land("battle", drafts, landed, fake_check)[0] == "unchanged")
        (drafts / "battle.md").write_text("# 戰鬥機制\n", encoding="utf-8")
        expect("a revised draft replaces the page",
               land("battle", drafts, landed, fake_check)[0] == "updated")
        expect("a draft that fails the gate is refused",
               land("save", drafts, landed, fake_check)[0] == "refused")
        expect("a refused draft leaves no page behind", not (landed / "save.md").exists())
        expect("an absent draft is reported, not skipped",
               land("spell", drafts, landed, fake_check)[0] == "missing")

    for f in failures:
        print("FAIL", f)
    print("selftest: %d failure(s)" % len(failures))
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("docs", nargs="*")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.docs:
        ap.error("name at least one DOC")
    names, idents = gate.load_names(), gate.load_idents()
    results = {}
    for doc in a.docs:
        status, detail = land(doc, gate.DRAFTS, gate.LANDED,
                              lambda p: gate.check_page(p, names, idents, draft=True))
        results[doc] = {"status": status, "detail": detail}
    if a.json:
        print(json.dumps(results, ensure_ascii=False, indent=2))
    else:
        for doc, r in results.items():
            print("%s: %s %s" % (doc, r["status"], r["detail"]))
    bad = [d for d, r in results.items() if r["status"] in ("refused", "missing")]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
