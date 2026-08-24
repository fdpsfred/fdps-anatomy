"""Fold each verdict's rebuild pitfall into its plate comment.

A pitfall found while naming a function -- "the side byte is a truth value, not
a side code, so the obvious `if (rec[6] == side_select)` is wrong" -- is only
useful to the person who later writes that function in C. The verdict files live
under workspace/ and are not versioned; the plate comment is, through the Ghidra
snapshot, and it is what that person will be looking at. So the pitfall belongs
in the plate.

Later runs of the workflow write the note into the plate directly. This script
is for the verdicts written before that, and it is idempotent: a plate that
already ends in a Rebuild note is left alone.

After running it, re-apply the verdicts so the new plates reach Ghidra:
  run_ghidra_script tools/logic_naming/ApplyNamingVerdicts.java  <verdict dir> all

Usage:
  python tools/logic_naming/merge_pitfalls.py [--verdicts DIR] [--dry-run]
"""

import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_VERDICTS = os.path.join(REPO, "workspace", "logic_naming", "verdicts")
MARKER = "Rebuild note:"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verdicts", default=DEFAULT_VERDICTS)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(args.verdicts):
        sys.exit("no verdict directory at %s" % args.verdicts)

    merged = already = nothing = 0
    for name in sorted(os.listdir(args.verdicts)):
        if not name.endswith(".json"):
            continue
        path = os.path.join(args.verdicts, name)
        with open(path, encoding="utf-8") as fh:
            verdict = json.load(fh)

        pitfall = (verdict.get("pitfall") or "").strip()
        if not pitfall:
            nothing += 1
            continue
        plate = verdict.get("plate") or {}
        text = (plate.get("text") or "").rstrip()
        if MARKER in text:
            already += 1
            continue

        # The note goes last, after the Pool line: it is the thing a reader wants
        # when they have finished understanding the function and are about to
        # write it.
        plate["text"] = "%s\n\n%s\n%s" % (text, MARKER, pitfall)
        verdict["plate"] = plate
        merged += 1
        if not args.dry_run:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(verdict, fh, indent=2, ensure_ascii=False)

    print("plates given a rebuild note = %d%s" % (merged, " (dry run)" if args.dry_run else ""))
    print("already had one             = %d" % already)
    print("no pitfall recorded         = %d" % nothing)


if __name__ == "__main__":
    main()
