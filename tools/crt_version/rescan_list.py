"""Which verdicts still need a second reading.

The rescan exists because a judging agent sees one packet and cannot tell an
exceptional result from an ordinary one.  Deriving its worklist from the verdict
files rather than from one run's memory is what makes it survive an interruption:
a run killed halfway through the rescan leaves the remaining verdicts unmarked,
and the next run picks up exactly those.

A verdict needs a second reading when it carries weight or carries doubt:

  it splits the 10.0 family   -- these are the verdicts that decide the ticket,
                                 every other one only separates 9.5 from 10.x
  confidence is not high      -- the first reading said so itself
  open_questions is non-empty -- likewise

and has not had one yet, which is recorded by `_supersedes` (the reading changed
the verdict) or `_reread` (it confirmed it).

Usage:
    python tools/crt_version/rescan_list.py           # addresses needing a reread
    python tools/crt_version/rescan_list.py --all     # every address, with reason
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
WORK = REPO / "workspace" / "crt_version"

FAMILY_10_0 = {"10.0", "10.0a", "10.0a_infobase", "10.0b"}


def reasons(v: dict) -> list[str]:
    out = []
    compat = set(v.get("compatible") or [])
    inside = compat & FAMILY_10_0
    if inside and inside != FAMILY_10_0:
        out.append("splits the 10.0 family: %s" % ", ".join(sorted(inside)))
    if v.get("confidence") != "high":
        out.append("confidence is %s" % v.get("confidence"))
    if v.get("open_questions"):
        out.append("open question recorded")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verdicts", type=Path, default=WORK / "verdicts")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()

    rows = []
    for path in sorted(args.verdicts.glob("*.json")):
        try:
            v = json.loads(path.read_text(encoding="utf-8"))
        except Exception:                                       # noqa: BLE001
            rows.append({"address": path.stem, "state": "unreadable"})
            continue
        why = reasons(v)
        already = bool(v.get("_supersedes") or v.get("_reread"))
        if not why:
            state = "not_needed"
        elif already:
            state = "reread"
        else:
            state = "todo"
        rows.append({"address": path.stem, "state": state, "why": why,
                     "name": v.get("name", ""),
                     "compatible": v.get("compatible", [])})

    if args.all:
        print(json.dumps(rows, indent=1))
    else:
        print(json.dumps([{"address": r["address"], "why": "; ".join(r["why"])}
                          for r in rows if r["state"] == "todo"], indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
