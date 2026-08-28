"""next_batch.py -- read emit progress off disk and hand the workflow its worklist.

The emit workflow keeps no state of its own.  Everything that says how far the
rebuild has got lives in tools/code_emit/data/emit_state.json, so a run that was
interrupted -- by a token limit, by a machine, by a person -- resumes by asking
this script what is left, with no conversation context required.

The worklist itself is not stored twice.  Which functions there are and which
file each belongs in comes from tools/code_emit/data/routing.json, ticket 21.5's
routing table; emit_state.json says only how far each one has got.  A function
absent from the state file has simply not been started, and a `target` in the
state file that disagrees with routing is reported rather than believed --
routing owns the file, and a stale copy of that answer in a second file is how
two emits end up writing the same function into different .c files.

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
ROUTING = Path(__file__).resolve().parent / "data" / "routing.json"
SNAPSHOT = ROOT / "ghidra_snapshot" / "functions.txt"
ORDER = ROOT / "workspace" / "code_emit" / "emit_order.json"
GRAPH = ROOT / "workspace" / "call_graph" / "graph.json"

DONE = "committed"
TERMINAL = ("committed", "skip")

SNAP_RX = re.compile(r"^([0-9a-f]{8})\s*\|\s*(0x[0-9a-f]+)\s*\|", re.M)


def load_state():
    return json.loads(STATE.read_text(encoding="utf-8"))


def save_state(state):
    STATE.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n",
                     encoding="utf-8")


def load_routing():
    if not ROUTING.is_file():
        sys.exit("missing %s -- ticket 21.5's routing table is the worklist"
                 % ROUTING)
    return json.loads(ROUTING.read_text(encoding="utf-8"))["functions"]


def merged(state, routing):
    """address -> entry, routing supplying the roster, state the progress."""
    progress = state.get("functions", {})
    out = {}
    conflicts = []
    for addr, route in sorted(routing.items()):
        entry = dict(progress.get(addr, {}))
        if entry.get("target") and entry["target"] != route["target"]:
            conflicts.append("%s %s: state says %s, routing says %s"
                             % (addr, route["name"], entry["target"],
                                route["target"]))
        entry.setdefault("status", "pending")
        entry["name"] = route["name"]
        entry["target"] = route["target"]
        out[addr] = entry
    for addr, entry in progress.items():
        if addr not in out:
            conflicts.append("%s %s: in emit_state but not in routing"
                             % (addr, entry.get("name")))
            out[addr] = dict(entry)
    return out, conflicts


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


def emit_order():
    """The callee-before-caller order, or None when it has not been built.

    Address order is what `sorted` would give and it is the wrong answer: a
    function emitted before its callees is tested against generated stubs
    rather than against the real thing.  tools/code_emit/emit_order.py works
    the order out from the call graph; if its output is missing the worklist
    still comes out, in address order, and says so.
    """
    if not ORDER.is_file():
        return None
    return json.loads(ORDER.read_text(encoding="utf-8"))["order"]


def callees():
    """caller -> [callee address], direct and table-dispatched alike."""
    if not GRAPH.is_file():
        return {}
    graph = json.loads(GRAPH.read_text(encoding="utf-8"))
    out = {}
    for key in ("direct_edges", "indirect_edges"):
        for row in graph.get(key, []):
            src, dst = row[0], row[1]
            if src != dst:
                out.setdefault(src, set()).add(dst)
    return {k: sorted(v) for k, v in out.items()}


def worklist(functions, limit):
    sizes = snapshot_sizes()
    order = emit_order()
    calls = callees()
    done = {addr for addr, entry in functions.items()
            if entry.get("status") == DONE
            and not stale(dict(entry, _addr=addr), sizes)}

    addrs = [a for a in (order or sorted(functions)) if a in functions]
    # Anything the order does not know about -- a function added to routing
    # after the order was last built -- goes on the end rather than vanishing.
    addrs += [a for a in sorted(functions) if a not in set(addrs)]

    out = []
    for addr in addrs:
        entry = dict(functions[addr], _addr=addr)
        if entry.get("status") in TERMINAL and not stale(entry, sizes):
            continue
        stubbed = [functions[c]["name"] for c in calls.get(addr, [])
                   if c in functions and c not in done]
        out.append({"addr": addr,
                    "name": entry.get("name"),
                    "target": entry.get("target"),
                    "body_size": sizes.get(addr) or entry.get("body_size"),
                    "stubbed_callees": stubbed})
        if limit and len(out) >= limit:
            break
    return out


def stats(functions):
    sizes = snapshot_sizes()
    counts = {}
    retired = []
    for addr, entry in sorted(functions.items()):
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

    functions, conflicts = merged(load_state(), load_routing())
    for line in conflicts:
        print("CONFLICT %s" % line, file=sys.stderr)

    if args.stats:
        stats(functions)
        return 1 if conflicts else 0

    if conflicts:
        print("refusing to hand out a worklist while routing and state "
              "disagree", file=sys.stderr)
        return 1

    fns = worklist(functions, args.limit)
    print(json.dumps({"batchLabel": args.label, "functions": fns},
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
