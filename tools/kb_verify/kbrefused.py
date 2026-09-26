"""The edits the consistency pass could not land, triaged and re-judged.

kbconsist.py apply refuses an edit whose old text is no longer exactly once in
its page -- in practice because another item's edit landed first on the same
passage.  Nothing refused may be dropped silently (ADR-0007 5.3), so every
refused edit is settled here:

  - deterministically, when the edit's new text is already in the page word
    for word (the edit that landed says the same thing, or this item's own
    edit landed and the refusal was of a sibling): `triage` records it as
    covered, with the evidence, in workspace/kb_refused/triage.json;
  - otherwise by one agent per refused edit (refused_ticket25_17.js), through
    the consistency pass's own machinery: the same verdict shape, gate, second
    reading and landing, with its own item list and verdicts.

    python tools/kb_verify/kbrefused.py triage              covered list + the item list (items.json)
    python tools/kb_verify/kbrefused.py <kbconsist command>  show / pending / check / rescan / report / apply
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kbverify  # noqa: E402
import kbconsist  # noqa: E402

REPO = kbverify.REPO
CONSIST_VERDICTS = kbconsist.VERDICTS
CONSIST_ITEMS = kbconsist.ITEMS
TRIAGE = REPO / "workspace" / "kb_refused" / "triage.json"
# The commit before either pass landed anything: what a page looked like then.
BASE = "14a5e44"


def refused_edits():
    """[(item, verdict, edit)] for every final edit of an item that has
    refusals and whose old text is not exactly once in its page now.  An
    item's refusal record names the page, not the edit, so every such edit of
    that item is examined; the ones that did land show up as covered."""
    items = {i["id"]: i for i in json.loads(CONSIST_ITEMS.read_text(encoding="utf-8"))}
    out = []
    for iid, item in items.items():
        path = CONSIST_VERDICTS / ("%s.json" % iid)
        if not path.is_file():
            continue
        v = json.loads(path.read_text(encoding="utf-8"))
        if not v.get("_refused"):
            continue
        for e in kbconsist.final_edits(v):
            text = (REPO / e["doc"]).read_text(encoding="utf-8")
            if text.count(e["old"]) != 1:
                out.append((item, v, e))
    return out


def triage():
    covered, items = [], []
    for item, v, e in refused_edits():
        text = (REPO / e["doc"]).read_text(encoding="utf-8")
        if e["new"] in text:
            covered.append({"source_id": item["id"], "doc": e["doc"], "old": e["old"], "new": e["new"],
                            "settled": "covered: the new text is in the page word for word"})
            continue
        items.append({"kind": "refused", "source_id": item["id"], "source_kind": item["kind"],
                      "source_conclusion": v["conclusion"], "doc": e["doc"], "old": e["old"],
                      "new": e["new"], "why": e.get("why", ""), "base": BASE})
    TRIAGE.parent.mkdir(parents=True, exist_ok=True)
    TRIAGE.write_text(json.dumps(covered, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    items = [dict(it, id="F%d" % k) for k, it in enumerate(items, 1)]
    for it in items:
        it["sha1"] = kbverify.sha1(json.dumps({k: v for k, v in it.items() if k != "sha1"},
                                              ensure_ascii=False, sort_keys=True))
    return items, covered


def main():
    kbconsist.use_workspace("kb_refused", "kb-refused")
    if len(sys.argv) > 1 and sys.argv[1] == "triage":
        sys.stdout.reconfigure(encoding="utf-8")
        if kbconsist.ITEMS.is_file():
            print(json.dumps({"frozen": True, "items": len(kbconsist.load_items()),
                              "covered": len(json.loads(TRIAGE.read_text(encoding="utf-8")))}))
            return 0
        items, covered = triage()
        kbconsist.ITEMS.write_text(json.dumps(items, ensure_ascii=False, indent=1) + "\n",
                                   encoding="utf-8")
        print(json.dumps({"frozen": True, "items": len(items), "covered": len(covered)}))
        return 0
    return kbconsist.main()


if __name__ == "__main__":
    raise SystemExit(main())
