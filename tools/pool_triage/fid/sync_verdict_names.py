"""Carry a rename through to the pool verdict files.

The verdict file is the project's record of what a function is; Ghidra is where
that record is applied. A rename that lands in Ghidra but not in the verdict
leaves the two disagreeing, and ApplyPoolVerdicts reads the verdict - so a later
re-apply would be working from the stale name.

Usage:
    python tools/pool_triage/fid/sync_verdict_names.py [--map <mechanical.json>]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DEFAULT_MAP = REPO / "workspace" / "pool_triage" / "fid_ail" / "renames" / "mechanical.json"
POOL_VERDICTS = REPO / "workspace" / "pool_triage" / "verdicts" / "pools"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", type=Path, default=DEFAULT_MAP)
    ap.add_argument("--verdicts", type=Path, default=POOL_VERDICTS)
    args = ap.parse_args()

    renames = json.loads(args.map.read_text(encoding="utf-8"))
    updated = missing = unchanged = 0
    for addr, r in sorted(renames.items()):
        path = args.verdicts / ("%s.json" % addr)
        if not path.is_file():
            print("MISSING verdict for %s" % addr)
            missing += 1
            continue
        v = json.loads(path.read_text(encoding="utf-8"))
        if v.get("name") == r["to"]:
            unchanged += 1
            continue
        v["name"] = r["to"]
        path.write_text(json.dumps(v, indent=2, ensure_ascii=False), encoding="utf-8")
        updated += 1
    print("verdicts updated: %d  already correct: %d  missing: %d"
          % (updated, unchanged, missing))
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
