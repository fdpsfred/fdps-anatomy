"""Assemble one evidence pack per function where the AIL Function ID result and
the ticket-14 pool verdict disagree.

A pack holds everything the re-reading agent needs that is expensive to gather
and cheap to write down: what ticket 14 decided and why, what the library match
says, how trustworthy that match is (score, how many other functions the same
library module also matched, whether the hashes are equal outright), and what
the neighbouring functions were judged to be. The assembly itself is not copied
in - the agent reads that from Ghidra, one function, itself.

`--extra` widens the worklist beyond outright disagreement. A ticket-14 verdict
that was left at medium confidence and now has a byte-identical library match is
not a contradiction, but leaving it recorded as medium would make the knowledge
base state something the evidence no longer supports; those addresses go through
the same one-agent-per-function re-read.

Usage:
    python tools/pool_triage/fid/build_contradiction_worklist.py [--extra <addr> ...]
"""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DEFAULT_WORK = REPO / "workspace" / "pool_triage" / "fid_ail"
POOL_VERDICTS = REPO / "workspace" / "pool_triage" / "verdicts" / "pools"
SNAPSHOT = REPO / "ghidra_snapshot" / "functions.txt"


def load_snapshot(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or line.startswith(" ") or not line.strip():
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 8:
            continue
        addr, size, _cc, _src, _purge, _flags, tags, proto = parts[:8]
        tagset = [t for t in tags.split(",") if t and t != "-"]
        pools = [t for t in tagset if t.startswith("pool_")]
        rows.append({
            "addr": addr,
            "size": int(size, 16),
            "pool": pools[0][5:] if len(pools) == 1 else "+".join(pools),
            "proto": proto,
        })
    rows.sort(key=lambda r: int(r["addr"], 16))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", type=Path, default=DEFAULT_WORK)
    ap.add_argument("--extra", nargs="*", default=[],
                    help="addresses to pack even though their pool and the match agree")
    args = ap.parse_args()

    report = json.loads((args.work / "report" / "ail_fid_report.json").read_text(encoding="utf-8"))
    lib = {f["name"]: f for f in json.loads(
        (args.work / "hashes_ail_code.json").read_text(encoding="utf-8"))["functions"]}
    matches = json.loads((args.work / "results" / "matches_ail_fd2.json").read_text(
        encoding="utf-8"))["matches"]

    # How many distinct FDPS functions each library module matched. A module that
    # answers to a dozen different functions is a shape collision, not evidence.
    collisions = Counter()
    for m in matches:
        for c in m["candidates"]:
            collisions[c["matched_name"]] += 1

    rows = load_snapshot(SNAPSHOT)
    index = {r["addr"]: i for i, r in enumerate(rows)}

    out = args.work / "contradictions"
    out.mkdir(parents=True, exist_ok=True)

    # --extra is sticky. The workflow's Plan phase re-runs this builder with no
    # arguments on every resumed run, so an extra address that is not carried
    # over would lose its packet and drop out of the worklist without ever
    # appearing in the unfinished list.
    previous = out / "worklist.json"
    carried = []
    if previous.is_file():
        old = json.loads(previous.read_text(encoding="utf-8"))
        carried = old.get("extra", []) if isinstance(old, dict) else []
    extra = list(dict.fromkeys(list(carried) + list(args.extra)))
    if carried and not args.extra:
        print("carried over %d extra address(es) from the previous worklist" % len(carried))

    items = list(report["contradictions"])
    by_row = {r["address"]: r for r in report["rows"]}
    for addr in extra:
        if any(c["address"] == addr for c in items):
            continue
        row = by_row.get(addr)
        if row is not None and row["matched_name"]:
            items.append({
                "address": addr,
                "pool": "pool_ail",
                "name": row["name"],
                "body_size": row["body_size"],
                "matched_name": row["matched_name"],
                "score": row["score"],
                "candidates": row["candidates"],
                "full_hash_equal": row["full_hash_equal"],
            })
            continue
        # An address outside the ail pool, or one Function ID could not hash at
        # all. The five-byte thunks are both: too short to hash, and filed under
        # a different pool, yet the library demonstrably publishes them. Their
        # evidence lives in the naming decision instead of the match table.
        snap = next((r for r in rows if r["addr"] == addr), None)
        if snap is None:
            print("SKIP %s: not in the snapshot" % addr)
            continue
        items.append({
            "address": addr,
            "pool": "pool_" + snap["pool"],
            "name": re.search(r"\b(\w+)\(", snap["proto"]).group(1) if re.search(
                r"\b(\w+)\(", snap["proto"]) else "?",
            "body_size": snap["size"],
            "matched_name": None,
            "score": None,
            "candidates": 0,
            "full_hash_equal": False,
        })

    worklist = []
    packs = {}
    for c in items:
        addr = c["address"]
        i = index.get(addr)
        neighbours = []
        if i is not None:
            for j in (i - 2, i - 1, i + 1, i + 2):
                if 0 <= j < len(rows):
                    n = rows[j]
                    neighbours.append({"addr": n["addr"], "pool": n["pool"],
                                       "size": n["size"], "proto": n["proto"]})
        # The ticket-14 decision, not whatever is currently in the verdict
        # directory. The workflow's apply stage overwrites <addr>.json with the
        # 14.1 verdict and keeps the original as <addr>.json.t14, so on any
        # rebuild after an apply round the plain file is this ticket's own
        # earlier conclusion - handing it back to the re-reading agent as "the
        # opposing evidence" would turn the packet into an echo.
        prior = {}
        backup = POOL_VERDICTS / ("%s.json.t14" % addr)
        vf = backup if backup.is_file() else POOL_VERDICTS / ("%s.json" % addr)
        if vf.is_file():
            prior = json.loads(vf.read_text(encoding="utf-8"))
        libfn = lib.get(c["matched_name"], {}) if c["matched_name"] else {}

        # A naming decision, where one exists, carries the reading of the body
        # that produced the name and any doubt it raised about the pool. That is
        # the whole evidence base for a function Function ID cannot hash.
        naming = {}
        nf = args.work / "renames" / "decisions" / ("%s.json" % addr)
        if nf.is_file():
            n = json.loads(nf.read_text(encoding="utf-8"))
            naming = {"name": n.get("name", ""), "kind": n.get("kind", ""),
                      "evidence": n.get("evidence", ""), "pool_doubt": n.get("pool_doubt", "")}

        pack = {
            "addr": addr,
            "current_name": c["name"],
            "body_size": c["body_size"],
            "ticket14_pool": c["pool"].replace("pool_", ""),
            "ticket14_verdict": {
                "role": prior.get("role", ""),
                "evidence": prior.get("evidence", ""),
                "confidence": prior.get("confidence", ""),
                "open_question": prior.get("open_question", ""),
            },
            "fid": {
                "matched_name": c["matched_name"],
                "score": c["score"],
                "candidate_count": c["candidates"],
                "full_hash_equal": c["full_hash_equal"],
                "library_body_size": libfn.get("body_size"),
                "library_code_units": libfn.get("code_units"),
                "module_matched_n_functions": collisions[c["matched_name"]],
            },
            "naming_decision": naming,
            "neighbours": neighbours,
        }
        packs[addr] = pack
        worklist.append(addr)

    # Everything is built before anything is removed. Deleting first and
    # crashing halfway would take worklist.json with it, and worklist.json is
    # where the sticky --extra list lives - the state would be gone for good
    # rather than merely stale.
    for stale in out.glob("*.json"):
        stale.unlink()
    for addr, pack in packs.items():
        (out / ("%s.json" % addr)).write_text(json.dumps(pack, indent=2), encoding="utf-8")
    (out / "worklist.json").write_text(
        json.dumps({"addresses": worklist, "extra": extra}, indent=2), encoding="utf-8")
    print("wrote %d evidence packs to %s" % (len(worklist), out))
    print(json.dumps(worklist))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
