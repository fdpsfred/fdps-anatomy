"""apply_pitfalls.py -- land decided rebuild_info/pitfalls.md edits (ticket 25.8).

The workflow's judges write one verdict file per candidate into workspace/chapter_docs/pitfalls/;
this script applies them verbatim with the same insertion rules the
game-mechanics work uses (tools/game_mechanics/apply_kb.py owns them, imported
here), then runs the link check on the edited page.  Idempotent.

Verdict file: {"id", "outcome", "reason",
               "section": "<## heading>", "row": "| ... |"       (added)
               "old_line": "| ... |", "new_line": "| ... |"}     (linked)

Usage: python tools/chapter_docs/apply_pitfalls.py
Output: JSON {"errors", "link_errors", "results"}
Exit : 0 when nothing was refused and every link resolves.
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "game_mechanics"))

import chapter_facts as facts  # noqa: E402
from apply_kb import apply_pitfalls  # noqa: E402
import check_mechanics as mech  # noqa: E402

VERDICTS = facts.WS / "pitfalls"
PITFALLS = facts.ROOT / "rebuild_info" / "pitfalls.md"


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    verdicts = [json.loads(p.read_text(encoding="utf-8"))
                for p in sorted(VERDICTS.glob("*.json"))] if VERDICTS.exists() else []
    old = PITFALLS.read_bytes().decode("utf-8")
    new, results = apply_pitfalls(old, verdicts)
    if new != old:
        PITFALLS.write_bytes(new.encode("utf-8"))
    links = [f for f in mech.check_file_links(PITFALLS, facts.ROOT) if f[0] == "error"]
    errors = sum(1 for r in results if r[1] == "error")
    print(json.dumps({"errors": errors, "link_errors": len(links),
                      "link_findings": [f[3] for f in links],
                      "results": [{"id": r[0], "status": r[1], "detail": r[2]} for r in results]},
                     ensure_ascii=False, indent=2))
    return 1 if errors or links else 0


if __name__ == "__main__":
    sys.exit(main())
