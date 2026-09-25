"""land.py -- put clean chapter drafts into chapters/ (ticket 25.8).

The transcription stage of the ticket 25.8 workflow.  It makes no judgement.
For each chapter it takes the draft page and the draft's .meta.json, refuses
the pair unless the meta says complete and the gate passes on the filled page,
and otherwise writes two files:

    chapters/chNN.md                          the draft with every generated
                                              block filled in
    tools/chapter_docs/judgements/chNN.json   the meta's judgement subset
                                              (waves, text_readers,
                                              never_shown) -- what the
                                              generated blocks read, kept so
                                              the landed page can be
                                              regenerated and re-checked later

Nothing in the draft is edited on the way; a refused chapter leaves both
targets as they were.

Usage: python tools/chapter_docs/land.py N [N ...] [--json]
       python tools/chapter_docs/land.py --selftest
Exit : 0 when every named chapter landed (or was already landed unchanged).
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import check_chapter as gate  # noqa: E402
import chapter_facts as facts  # noqa: E402


def land(n, drafts, landed, judgements, fill, check):
    """Land one chapter.  Returns (status, detail).

    status: new, updated, unchanged, refused, missing.
    fill(text, judgement) -> filled page; check(filled text, judgement) -> findings.
    """
    src = drafts / f"ch{n:02d}.md"
    meta_path = drafts / f"ch{n:02d}.meta.json"
    if not src.exists() or not meta_path.exists():
        return "missing", "no draft or no meta at %s" % src.parent
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except ValueError as e:
        return "refused", "meta does not parse: %s" % e
    if meta.get("complete") is not True:
        return "refused", "the meta file does not say complete"
    judgement = facts.judgement_subset(meta)
    text = fill(src.read_text(encoding="utf-8"), judgement)
    errors = [f for f in check(text, judgement) if f[0] == "error"]
    if errors:
        return "refused", "; ".join("%s line %d: %s" % (f[1], f[2], f[3]) for f in errors[:5])
    page = text.encode("utf-8")
    record = (json.dumps(judgement, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    dst, jdst = landed / f"ch{n:02d}.md", judgements / f"ch{n:02d}.json"
    if dst.exists() and jdst.exists() and dst.read_bytes() == page and jdst.read_bytes() == record:
        return "unchanged", ""
    status = "updated" if dst.exists() else "new"
    judgements.mkdir(parents=True, exist_ok=True)
    jdst.write_bytes(record)
    dst.write_bytes(page)
    return status, ""


def selftest():
    failures = []

    def expect(label, cond):
        if not cond:
            failures.append(label)

    def fake_fill(text, judgement):
        return text.replace("<!-- x -->", "FILLED")

    def fake_check(text, judgement):
        return [("error", "narrative", 1, "x")] if "BAD" in text else []

    with tempfile.TemporaryDirectory() as tmp:
        d, l, j = Path(tmp) / "d", Path(tmp) / "l", Path(tmp) / "j"
        d.mkdir()
        l.mkdir()
        (d / "ch01.md").write_text("# a\n<!-- x -->\n", encoding="utf-8")
        (d / "ch01.meta.json").write_text(json.dumps({"complete": True, "waves": [],
                                                      "noise": 1}), encoding="utf-8")
        (d / "ch02.md").write_text("# BAD\n", encoding="utf-8")
        (d / "ch02.meta.json").write_text(json.dumps({"complete": True}), encoding="utf-8")
        (d / "ch03.md").write_text("# c\n", encoding="utf-8")
        (d / "ch03.meta.json").write_text(json.dumps({"complete": False}), encoding="utf-8")

        def run(n):
            return land(n, d, l, j, fake_fill, fake_check)[0]

        expect("a clean draft lands as new", run(1) == "new")
        expect("the landed page is the filled draft",
               (l / "ch01.md").read_text(encoding="utf-8") == "# a\nFILLED\n")
        expect("only the judgement subset is recorded",
               set(json.loads((j / "ch01.json").read_text(encoding="utf-8")))
               == set(facts.JUDGEMENT_KEYS))
        expect("landing it again changes nothing", run(1) == "unchanged")
        expect("a draft failing the gate is refused and leaves nothing",
               run(2) == "refused" and not (l / "ch02.md").exists()
               and not (j / "ch02.json").exists())
        expect("a meta that is not complete is refused", run(3) == "refused")
        expect("an absent draft is reported", run(4) == "missing")

    for f in failures:
        print("FAIL", f)
    print("selftest: %d failure(s)" % len(failures))
    return 1 if failures else 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("chapters", nargs="*", type=int)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.chapters:
        ap.error("name at least one chapter")
    names, idents = gate.mech.load_names(), gate.mech.load_idents()
    results = {}
    for n in a.chapters:
        status, detail = land(
            n, gate.DRAFTS, gate.LANDED, facts.JUDGEMENTS,
            lambda text, j, n=n: gate.fill(text, n, j),
            lambda text, j, n=n: gate.check_page_text(text, n, j, names, idents, draft=True))
        results[f"ch{n:02d}"] = {"status": status, "detail": detail}
    if a.json:
        print(json.dumps(results, ensure_ascii=False, indent=2))
    else:
        for doc, r in results.items():
            print("%s: %s %s" % (doc, r["status"], r["detail"]))
    return 1 if any(r["status"] in ("refused", "missing") for r in results.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
