"""Roll the ticket 14.2 verdicts up into the numbers the knowledge base states.

Every count on program_info/code_pools.md has to come from the verdicts rather
than from an edit by hand: a re-review moves functions between pools, and a
hand-edited total is wrong the moment one of them moves.

Reads:
    <ws>/verdicts/*.json      one verdict per function, four axes each
    <ws>/functions.json       body sizes and end addresses
Writes:
    <ws>/summary.json

Usage:
    python tools/pool_rereview/summarize_verdicts.py [--ws <workspace/pool_rereview>]
"""
from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_WS = REPO / "workspace" / "pool_rereview"

AXES = ("pool", "name", "boundary", "signature", "plate")


def load(path: Path, default=None):
    if not path.is_file():
        return default
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)


def runs_of(addresses: list) -> list:
    """Consecutive addresses collapsed into ranges, for the AIL functional areas."""
    if not addresses:
        return []
    out, start, prev = [], addresses[0], addresses[0]
    for a in addresses[1:]:
        if a != prev + 1:
            out.append((start, prev))
            start = a
        prev = a
    out.append((start, prev))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ws", type=Path, default=DEFAULT_WS)
    args = ap.parse_args()
    ws = args.ws

    facts = {f["addr"]: f for f in (load(ws / "functions.json") or [])}
    verdicts = {}
    for path in sorted((ws / "verdicts").glob("*.json")):
        data = load(path)
        if data:
            verdicts[path.stem.lower()] = data

    pools = Counter()
    pool_bytes = Counter()
    confidence = Counter()
    axis_confidence = {axis: Counter() for axis in AXES}
    changed = {axis: [] for axis in AXES}
    unresolved = []
    deferred = []
    boundary_fixes = []
    assumed_signatures = []
    named = defaultdict(list)
    by_pool_addrs = defaultdict(list)

    for addr, v in verdicts.items():
        pool = ((v.get("pool") or {}).get("verdict") or "unknown")
        pools[pool] += 1
        pool_bytes[pool] += (facts.get(addr) or {}).get("size", 0)
        confidence[(v.get("pool") or {}).get("confidence") or "unknown"] += 1
        by_pool_addrs[pool].append(addr)

        for axis in AXES:
            block = v.get(axis) or {}
            axis_confidence[axis][block.get("confidence") or "unknown"] += 1
            if block.get("agrees_with_current") is False:
                changed[axis].append(addr)

        fix = (v.get("boundary") or {}).get("fix") or "none"
        if fix != "none":
            boundary_fixes.append("%s:%s" % (addr, fix))
        if (v.get("signature") or {}).get("assumed"):
            assumed_signatures.append(addr)

        name = (v.get("name") or {}).get("verdict") or ""
        if name and not name.startswith(("FUN_", "SUB_", "thunk_FUN_")):
            named[name].append(addr)

        # Two different things, kept apart. An axis below high confidence is this
        # ticket's own unfinished business. A question a confident verdict left
        # behind -- what an argument means, what a type code selects -- belongs
        # to the naming and emit tickets and is counted, not chased.
        shaky = any(((v.get(axis) or {}).get("confidence") not in ("high", None))
                    for axis in AXES)
        if shaky or pool == "unknown":
            unresolved.append({
                "addr": addr,
                "pool": pool,
                "open_question": (v.get("open_question") or "")[:200],
            })
        elif v.get("open_question"):
            deferred.append({"addr": addr, "question": v["open_question"][:200]})

    # The boundary axis has no agrees_with_current field; a fix is the change.
    changed["boundary"] = [b.split(":")[0] for b in boundary_fixes]

    ail_ranges = []
    for lo, hi in runs_of(sorted(int(a, 16) for a in by_pool_addrs.get("ail", []))):
        ail_ranges.append("%08x-%08x" % (lo, hi))

    summary = {
        "verdicts": len(verdicts),
        "functions_dumped": len(facts),
        "pools": dict(pools),
        "pool_bytes": dict(pool_bytes),
        "pool_confidence": dict(confidence),
        "axis_confidence": {a: dict(c) for a, c in axis_confidence.items()},
        "changed_counts": {a: len(v) for a, v in changed.items()},
        "changed": changed,
        "boundary_fixes": boundary_fixes,
        "assumed_signatures": len(assumed_signatures),
        "name_collisions": {n: a for n, a in named.items() if len(a) > 1},
        "unresolved": unresolved,
        "deferred_questions": deferred,
    }
    if ail_ranges:
        summary["ail_address_runs"] = ail_ranges

    out = ws / "summary.json"
    with out.open("w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=1, ensure_ascii=False)

    print("verdicts        %d of %d functions" % (len(verdicts), len(facts)))
    print("pools           %s" % dict(pools))
    print("pool bytes      %s" % dict(pool_bytes))
    print("pool confidence %s" % dict(confidence))
    print("changed         %s" % {a: len(v) for a, v in changed.items()})
    print("boundary fixes  %d" % len(boundary_fixes))
    print("assumed sigs    %d" % len(assumed_signatures))
    print("name clashes    %d" % len(summary["name_collisions"]))
    print("unresolved      %d" % len(unresolved))
    print("deferred qs     %d (high confidence, question for a later ticket)" % len(deferred))
    print("written to      %s" % out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
