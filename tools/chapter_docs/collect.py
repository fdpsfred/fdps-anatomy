"""collect.py -- the compact index of the chapter drafts' metadata (ticket 25.8).

Later workflow stages read this, never the drafts themselves: per chapter
whether the meta says complete, its confidence and open-question count, and
the three lists the shared stages act on (pitfall candidates, cut-content
candidates, cross-chapter notes), each item tagged with its chapter.

Usage: python tools/chapter_docs/collect.py
Output: JSON on stdout.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import chapter_facts as facts  # noqa: E402


def collect(drafts=facts.DRAFTS):
    out = {"drafts": [], "pitfall_candidates": [], "cut_content_candidates": [],
           "cross_chapter": [], "unreadable": []}
    for n in facts.CHAPTERS:
        path = drafts / f"ch{n:02d}.meta.json"
        if not path.exists():
            continue
        try:
            meta = json.loads(path.read_text(encoding="utf-8"))
        except ValueError:
            out["unreadable"].append(path.name)
            continue
        out["drafts"].append({"chapter": n, "complete": meta.get("complete") is True,
                              "confidence": meta.get("confidence", "?"),
                              "open_questions": len(meta.get("open_questions", []))})
        for c in meta.get("pitfall_candidates", []):
            item = dict(c)
            item["chapter"] = [n]
            out["pitfall_candidates"].append(item)
        for c in meta.get("cut_content_candidates", []):
            out["cut_content_candidates"].append(dict(c, chapter=n))
        for c in meta.get("cross_chapter", []):
            out["cross_chapter"].append(dict(c, chapter=n))
    return out


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    print(json.dumps(collect(), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
