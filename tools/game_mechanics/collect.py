"""collect.py -- compact index over the ticket 25.5 draft metadata.

Each drafting agent writes workspace/game_mechanics/drafts/<doc>.meta.json next
to its page.  Later stages of the workflow need an overview -- which drafts are
still open, which original bugs and rebuild pitfalls were noticed, which cut
content the bugs block -- without reading every page.  This prints that
overview; it judges nothing.

Usage: python tools/game_mechanics/collect.py [--selftest]
Output: JSON on stdout, already in the shape the workflow's schema asks for
  {"drafts": [{"doc", "complete", "confidence", "open_questions" (a count)}],
   "pitfall_candidates": [{"id", "doc", "what", "intuitive_wrong_way", "canonical_owner"}],
   "original_bugs": [{"id", "doc", "title", "blocks_content"}],
   "cut_content_candidates": [{"doc", "what", "why"}],
   "unreadable": [file, ...]}
Pitfall candidates with the same id are merged: the first keeps its text and
"doc" lists every page that reported it.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_mechanics import DRAFTS  # noqa: E402


def collect(drafts):
    out = {"drafts": [], "pitfall_candidates": [], "original_bugs": [],
           "cut_content_candidates": [], "unreadable": []}
    seen_pitfalls = {}
    for path in sorted(drafts.glob("*.meta.json")):
        try:
            meta = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            out["unreadable"].append(path.name)
            continue
        doc = meta.get("doc") or path.name[:-len(".meta.json")]
        out["drafts"].append({
            "doc": doc,
            "complete": bool(meta.get("complete")),
            "confidence": meta.get("confidence", "low"),
            "open_questions": len(meta.get("open_questions") or []),
        })
        for p in meta.get("pitfall_candidates") or []:
            pid = p.get("id") or "%s-%d" % (doc, len(seen_pitfalls))
            if pid in seen_pitfalls:
                seen_pitfalls[pid]["doc"].append(doc)
                continue
            entry = {"id": pid, "doc": [doc], "what": p.get("what", ""),
                     "intuitive_wrong_way": p.get("intuitive_wrong_way", ""),
                     "canonical_owner": p.get("canonical_owner", "")}
            seen_pitfalls[pid] = entry
            out["pitfall_candidates"].append(entry)
        for b in meta.get("original_bugs") or []:
            out["original_bugs"].append({"id": b.get("id", ""), "doc": doc,
                                         "title": b.get("title", ""),
                                         "blocks_content": bool(b.get("blocks_content"))})
        for c in meta.get("cut_content_candidates") or []:
            out["cut_content_candidates"].append({"doc": doc, "what": c.get("what", ""),
                                                  "why": c.get("why", "")})
    return out


def selftest():
    failures = []

    def expect(label, cond):
        if not cond:
            failures.append(label)

    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        (d / "battle.meta.json").write_text(json.dumps({
            "doc": "battle", "complete": True, "confidence": "high", "open_questions": [],
            "pitfall_candidates": [{"id": "no-srand", "what": "rand is never seeded"}],
            "original_bugs": [{"id": "lv30-xp", "title": "經驗值歸零", "blocks_content": False}],
        }), encoding="utf-8")
        (d / "village.meta.json").write_text(json.dumps({
            "doc": "village", "complete": True, "confidence": "medium",
            "open_questions": ["price rounding"],
            "pitfall_candidates": [{"id": "no-srand", "what": "same thing again"},
                                   {"id": "pw-table", "what": "24-row table read out of bounds"}],
            "cut_content_candidates": [{"what": "lottery prize", "why": "stack slot"}],
        }), encoding="utf-8")
        (d / "save.meta.json").write_text("{not json", encoding="utf-8")
        r = collect(d)

    expect("every readable draft is listed, in page order",
           [x["doc"] for x in r["drafts"]] == ["battle", "village"])
    expect("a draft's open questions are counted",
           r["drafts"][1]["open_questions"] == 1 and r["drafts"][1]["complete"] is True)
    ids = [p["id"] for p in r["pitfall_candidates"]]
    expect("pitfall candidates are merged by id", ids == ["no-srand", "pw-table"])
    expect("a merged candidate keeps the first text and lists every page",
           r["pitfall_candidates"][0]["what"] == "rand is never seeded"
           and r["pitfall_candidates"][0]["doc"] == ["battle", "village"])
    expect("original bugs are listed with their page",
           r["original_bugs"] == [{"id": "lv30-xp", "doc": "battle", "title": "經驗值歸零",
                                   "blocks_content": False}])
    expect("cut content candidates are listed", len(r["cut_content_candidates"]) == 1)
    expect("an unreadable meta file is reported, not skipped", r["unreadable"] == ["save.meta.json"])

    for f in failures:
        print("FAIL", f)
    print("selftest: %d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    print(json.dumps(collect(DRAFTS), ensure_ascii=False, indent=2))
