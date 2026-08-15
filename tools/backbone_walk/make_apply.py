"""Merge a batch of agent verdicts with the orchestrator's decisions.

The agents propose; this script records what was actually decided and hands
``ApplyBackboneWalk.java`` a flat list to transcribe into Ghidra. It makes no
judgements of its own -- every override below was reached by reading that one
function's verdict, and the reason is recorded next to it.

Usage: python make_apply.py <batch.json> <decisions.json> <out.json>
"""

import io
import json
import sys


def load(path):
    with io.open(path, encoding="utf-8") as fh:
        return json.load(fh)


def main():
    batch, decisions, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    verdicts = {v["addr"]: v for v in load(batch)}
    decided = load(decisions)

    missing = [a for a in decided if a not in verdicts]
    if missing:
        raise SystemExit("decisions reference addresses with no verdict: %s" % missing)
    undecided = [a for a in verdicts if a not in decided]
    if undecided:
        raise SystemExit("verdicts with no decision: %s" % undecided)

    items = []
    for addr in sorted(decided):
        d = decided[addr]
        v = verdicts[addr]
        plate = v["plate_comment"]
        if d.get("plate_prefix"):
            plate = d["plate_prefix"].rstrip() + "\n\n" + plate
        items.append({
            "addr": addr,
            "name": d["name"],
            "prototype": d["prototype"],
            "no_return": bool(d.get("no_return")),
            "pool": v["pool"],
            "plate_comment": plate,
        })

    with io.open(out_path, "w", encoding="utf-8") as fh:
        fh.write(json.dumps(items, indent=2, ensure_ascii=False))
    print("%d item(s) -> %s" % (len(items), out_path))
    for it in items:
        print("  %s  %-20s %s%s" % (
            it["addr"], it["name"], it["prototype"],
            "  [no-return]" if it["no_return"] else ""))


if __name__ == "__main__":
    main()
