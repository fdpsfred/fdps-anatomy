"""The open questions the verification passes left behind, settled like the
outside fixes.

The closing reports of the passes list, besides the fixes, a handful of things
a verifier noticed but could not settle inside its own item (a claim in
another page it did not verify, a comment outside its group, a count that
needs re-measuring).  Leaving them would leave the knowledge base knowingly
unfinished, so each is written down as a request in a batch file and settled
by kboutside.py's machinery -- grouping, gate, second reading, landing.

    python tools/kb_verify/kbfollowup.py [--batch N] <kboutside command>

Batch 1 is followups_25_17.json (from the first four passes, workspace
kb_followup/); batch 2 is followups_25_17_b2.json (what batch 1 itself left,
workspace kb_followup2/).  Each batch keeps its own items and verdicts, so a
later batch never re-groups -- and so never invalidates -- an earlier one.
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kboutside  # noqa: E402

BATCHES = {
    1: ("followups_25_17.json", "kb_followup", "kb-followup"),
    2: ("followups_25_17_b2.json", "kb_followup2", "kb-followup2"),
}


def loader(path):
    def followups():
        rows = json.loads(path.read_text(encoding="utf-8"))
        return [(r["ref"], r["text"], "", r.get("home_doc")) for r in rows]
    return followups


if __name__ == "__main__":
    batch = 1
    if len(sys.argv) > 2 and sys.argv[1] == "--batch":
        batch = int(sys.argv[2])
        del sys.argv[1:3]
    name, workspace, stem = BATCHES[batch]
    kboutside.use_workspace(workspace, stem, loader(HERE / name))
    raise SystemExit(kboutside.main())
