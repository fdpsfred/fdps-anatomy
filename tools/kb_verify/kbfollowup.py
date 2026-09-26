"""The open questions the verification passes left behind, settled like the
outside fixes.

The closing reports of the four passes list, besides the fixes, a handful of
things a verifier noticed but could not settle inside its own item (a claim in
another page it did not verify, a comment outside its group, a count that
needs re-measuring).  Leaving them would leave the knowledge base knowingly
unfinished, so each is written down in followups_25_17.json (the reports'
wording, turned into a request) and settled by kboutside.py's machinery --
grouping, gate, second reading, landing -- under workspace/kb_followup/.

    python tools/kb_verify/kbfollowup.py <kboutside command>   freeze / show / pending / check / rescan / report / apply / ghidra
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kboutside  # noqa: E402

FOLLOWUPS = HERE / "followups_25_17.json"


def followups():
    rows = json.loads(FOLLOWUPS.read_text(encoding="utf-8"))
    return [(r["ref"], r["text"], "", r.get("home_doc")) for r in rows]


if __name__ == "__main__":
    kboutside.use_workspace("kb_followup", "kb-followup", followups)
    raise SystemExit(kboutside.main())
