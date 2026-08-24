"""Work out which game-logic functions ticket 15 still has to name.

Reads the read-only dump produced by DumpNamingState.java and the verdict files
written so far, and splits the pool_fdps functions into three lists:

  full         a default FUN_ name: needs a name, a convention, parameter names
               and a behaviour plate comment -- the whole judgement.
  params_only  already named by ticket 12, but some parameter is still param_N.
               The name, convention and plate stand; only the parameters are open.
  settled      named, every parameter named: nothing left for this ticket.

Done is decided by the verdict file, never by the agent's report (ADR-0007 5.1),
and a verdict only counts while it still describes the body it was written for.
A body that changed since retires its verdict to <addr>.json.superseded-<sha>
and puts the function back on the list; a function that no longer exists retires
to .orphan. Retiring rather than deleting keeps the reasoning around when a
later pass wants to know what the earlier one saw.

Usage:
  python tools/logic_naming/build_worklist.py [--dump DIR] [--verdicts DIR] [--out FILE]
"""

import argparse
import json
import os
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_DUMP = os.path.join(REPO, "workspace", "logic_naming", "dump")
DEFAULT_VERDICTS = os.path.join(REPO, "workspace", "logic_naming", "verdicts")
DEFAULT_OUT = os.path.join(REPO, "workspace", "logic_naming", "worklist.json")


def load_dump(dump_dir):
    path = os.path.join(dump_dir, "functions.json")
    if not os.path.isfile(path):
        sys.exit("no dump at %s -- run DumpNamingState.java first" % path)
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def retire(verdict_path, suffix):
    """Move a verdict aside under a suffix that says why it stopped counting."""
    target = verdict_path + suffix
    n = 1
    while os.path.exists(target):
        target = verdict_path + suffix + ".%d" % n
        n += 1
    shutil.move(verdict_path, target)
    return os.path.basename(target)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--verdicts", default=DEFAULT_VERDICTS)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--no-retire", action="store_true",
                    help="report stale verdicts without moving them aside")
    args = ap.parse_args()

    dump = load_dump(args.dump)
    os.makedirs(args.verdicts, exist_ok=True)

    by_addr = {f["addr"]: f for f in dump["functions"]}

    # A verdict whose function is gone describes nothing. Ticket 14.2 moved
    # boundaries, so this is a real case, not a theoretical one.
    orphaned = []
    for name in sorted(os.listdir(args.verdicts)):
        if not name.endswith(".json"):
            continue
        addr = name[:-5]
        if addr in by_addr:
            continue
        path = os.path.join(args.verdicts, name)
        orphaned.append(addr if args.no_retire else retire(path, ".orphan"))

    full, params_only, settled, superseded = [], [], [], []
    for addr in sorted(by_addr):
        meta = by_addr[addr]
        vpath = os.path.join(args.verdicts, addr + ".json")
        if os.path.isfile(vpath):
            try:
                with open(vpath, encoding="utf-8") as fh:
                    verdict = json.load(fh)
            except Exception as exc:  # a half-written file is not a done item
                superseded.append(addr)
                if not args.no_retire:
                    retire(vpath, ".unparseable")
                print("unparseable verdict %s: %s" % (addr, exc), file=sys.stderr)
                verdict = None
            if verdict is not None:
                covers = verdict.get("covers") or {}
                if covers.get("body_sha") == meta["body_sha"]:
                    settled.append(addr)
                    continue
                superseded.append(addr)
                if not args.no_retire:
                    retire(vpath, ".superseded-%s" % (covers.get("body_sha") or "nohash"))

        if meta["default_name"]:
            full.append(addr)
        elif meta["default_params"]:
            params_only.append(addr)
        else:
            # Named by ticket 12 with every parameter already carrying a semantic
            # name: this ticket has nothing to add, and saying so beats spending
            # an agent to confirm it.
            settled.append(addr)

    # Leaves first. A function that calls nothing states its own purpose in its
    # own bytes; a function that calls twenty others is mostly a description of
    # what those twenty do, and reads far better once they have names. Sorting by
    # callee count turns the fixed worklist into roughly a bottom-up walk without
    # needing to discover an order the way ticket 12's BFS did.
    full.sort(key=lambda a: (by_addr[a]["callee_count"], by_addr[a]["size"], a))

    out = {
        "dump": os.path.abspath(args.dump),
        "verdicts": os.path.abspath(args.verdicts),
        "fdps_count": len(by_addr),
        "counts": {
            "full": len(full),
            "params_only": len(params_only),
            "settled": len(settled),
            "retired_superseded": len(superseded),
            "retired_orphan": len(orphaned),
        },
        "full": full,
        "params_only": params_only,
        "settled": settled,
        "retired_superseded": superseded,
        "retired_orphan": orphaned,
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2)

    print("pool_fdps functions   %d" % len(by_addr))
    print("  full judgement      %d" % len(full))
    print("  parameters only     %d" % len(params_only))
    print("  settled             %d" % len(settled))
    print("  retired superseded  %d" % len(superseded))
    print("  retired orphan      %d" % len(orphaned))
    print("worklist -> %s" % os.path.abspath(args.out))


if __name__ == "__main__":
    main()
