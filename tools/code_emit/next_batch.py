"""next_batch.py -- read emit progress off disk and hand the workflow its worklist.

The emit workflow keeps no state of its own.  Everything that says how far the
rebuild has got lives in tools/code_emit/data/emit_state.json, so a run that was
interrupted -- by a token limit, by a machine, by a person -- resumes by asking
this script what is left, with no conversation context required.

Two jobs:

    --stats   how many functions are pending / emitted / reviewed / committed /
              failed / skipped
    (default) print the JSON to hand to the workflow as `args`

An entry is only treated as done when it is `committed` AND the function it
describes still has the body size the emit was made against.  Ghidra bodies do
change under later analysis, and an entry whose function has since grown or
shrunk describes code that no longer exists; without this check it would be
skipped forever.  The comparison is against ghidra_snapshot/functions.txt, which
is re-exported into the same commit as every landing, so it tracks the database
it is standing in for.

Usage: python tools/code_emit/next_batch.py [--stats] [--limit N] [--label TAG]
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE = Path(__file__).resolve().parent / "data" / "emit_state.json"
SNAPSHOT = ROOT / "ghidra_snapshot" / "functions.txt"

DONE = "committed"
TERMINAL = ("committed", "skip")

SNAP_RX = re.compile(r"^([0-9a-f]{8})\s*\|\s*(0x[0-9a-f]+)\s*\|", re.M)


def load_state():
    return json.loads(STATE.read_text(encoding="utf-8"))


def save_state(state):
    STATE.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n",
                     encoding="utf-8")


def snapshot_sizes():
    """address -> body size, from the versioned Ghidra text snapshot."""
    if not SNAPSHOT.is_file():
        return {}
    text = SNAPSHOT.read_text(encoding="utf-8", errors="replace")
    return {m.group(1): m.group(2) for m in SNAP_RX.finditer(text)}


def stale(entry, sizes):
    """True when the recorded emit was made against a body that has changed.

    Silence is the dangerous answer here, so both unknowns count as stale: an
    address that vanished from the snapshot, and an entry that never recorded
    what it was emitted against. "Nobody wrote down which code this was made
    from" is not evidence that the code is still the same; treating it as
    evidence is how an entry becomes permanently done.
    """
    if entry.get("status") != DONE:
        return False
    against = entry.get("emitted_against")
    if against is None:
        return True
    return sizes.get(entry.get("_addr"), None) != against


def worklist(state, limit):
    sizes = snapshot_sizes()
    out = []
    for addr, entry in sorted(state.get("functions", {}).items()):
        entry = dict(entry, _addr=addr)
        if entry.get("status") in TERMINAL and not stale(entry, sizes):
            continue
        out.append({"addr": addr,
                    "name": entry.get("name"),
                    "target": entry.get("target"),
                    "body_size": sizes.get(addr) or entry.get("body_size")})
        if limit and len(out) >= limit:
            break
    return out


def stats(state):
    sizes = snapshot_sizes()
    counts = {}
    retired = []
    for addr, entry in sorted(state.get("functions", {}).items()):
        status = entry.get("status", "pending")
        if stale(dict(entry, _addr=addr), sizes):
            status = "stale (re-emit)"
            retired.append("%s %s" % (addr, entry.get("name")))
        counts[status] = counts.get(status, 0) + 1
    total = sum(counts.values())
    print("emit state: %d function(s)" % total)
    for status in sorted(counts):
        print("  %-16s %d" % (status, counts[status]))
    for line in retired:
        print("  stale, back on the worklist: %s" % line)
    return counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--limit", type=int, default=0,
                    help="how many functions to hand the workflow; 0 for all")
    ap.add_argument("--label", default="emit",
                    help="batch label, carried into the workflow's report")
    args = ap.parse_args()

    state = load_state()
    if args.stats:
        stats(state)
        return 0

    fns = worklist(state, args.limit)
    print(json.dumps({"batchLabel": args.label, "functions": fns},
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
