"""Split the ticket 14.2 dump into one facts packet and one current-state packet
per function, and work out what still has to be re-read.

The split is the point.  Ticket 14.2 is a second reading of judgements ticket 14
already made, and a second reading that starts from the existing tag is not a
reading at all -- it is a search for reasons to agree.  So the evidence an agent
needs to form its own conclusion (bytes, call edges, references, library hits)
goes into items/facts/, and what Ghidra currently claims (name, pool tag,
signature, plate comment) goes into items/current/.  The prompt orders the two
reads; keeping them in separate files is what makes the order enforceable.

Inputs:
    <ws>/functions.json <ws>/current.json           DumpRereviewState.java
    workspace/pool_triage/fid/results/matches_*.json          Watcom Function ID
    workspace/pool_triage/fid_ail/results/matches_ail_fd2.json   AIL Function ID
    workspace/pool_triage/fid_ail/hashes_fdps.json            per-function hashes
    workspace/pool_triage/fid_ail/hashes_ail_code.json        library-side hashes
    workspace/pool_triage/fid/watcom_symbols.json             public symbol list

Outputs:
    <ws>/items/facts/<addr>.json
    <ws>/items/current/<addr>.json
    <ws>/worklist.json

Usage:
    python tools/pool_rereview/build_packets.py [--ws <workspace/pool_rereview>]
                                                [--verdicts <dir>]
"""
from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_WS = REPO / "workspace" / "pool_rereview"
TRIAGE_WS = REPO / "workspace" / "pool_triage"

FID_MIN_SCORE = 10.0
FID_KEEP = 4


def load(path: Path, default=None):
    if not path.is_file():
        return default
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)


def dump(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as fh:
        json.dump(obj, fh, indent=1, ensure_ascii=False)


def watcom_hits() -> dict:
    """address -> best candidates against the Watcom runtime libraries."""
    results = TRIAGE_WS / "fid" / "results"
    hits: dict[str, dict] = {}
    if not results.is_dir():
        return {}
    for path in sorted(results.glob("matches_*.json")):
        version = path.stem.replace("matches_", "")
        for match in load(path, {}).get("matches", []):
            addr = match["address"].lower()
            entry = hits.setdefault(addr, {"cands": {}, "candidate_count": 0})
            entry["candidate_count"] = max(entry["candidate_count"],
                                           len(match.get("candidates", [])))
            for cand in match.get("candidates", []):
                score = cand.get("score") or 0.0
                if score < FID_MIN_SCORE:
                    continue
                name = cand.get("matched_name", "")
                slot = entry["cands"].setdefault(name, {
                    "name": name, "score": score,
                    "source_obj": cand.get("source_obj", ""), "versions": [],
                })
                slot["score"] = max(slot["score"], score)
                if version not in slot["versions"]:
                    slot["versions"].append(version)
    out = {}
    for addr, entry in hits.items():
        cands = sorted(entry["cands"].values(), key=lambda c: -c["score"])[:FID_KEEP]
        if cands:
            out[addr] = {"candidates": cands, "candidate_count": entry["candidate_count"]}
    return out


def ail_hits() -> dict:
    path = TRIAGE_WS / "fid_ail" / "results" / "matches_ail_fd2.json"
    out = {}
    for match in load(path, {}).get("matches", []):
        cands = [{
            "name": c.get("matched_name", ""),
            "score": c.get("score") or 0.0,
            "source_obj": c.get("source_obj", ""),
        } for c in match.get("candidates", [])]
        out[match["address"].lower()] = {
            "candidates": cands[:FID_KEEP],
            "candidate_count": len(cands),
        }
    return out


def hash_index() -> tuple:
    """Per-function hashes, plus how many functions share each hash.

    A hash shared by a dozen functions is the signature of a shape collision --
    the family of AIL debug wrappers that differ only in a relocated string
    pointer all hash the same, and a name taken from such a hit is a name taken
    from a coin toss.  Ticket 14.2 exists partly to catch exactly that.
    """
    fdps = load(TRIAGE_WS / "fid_ail" / "hashes_fdps.json", {}).get("functions", [])
    lib = load(TRIAGE_WS / "fid_ail" / "hashes_ail_code.json", {}).get("functions", [])
    fdps_counts = Counter(f.get("full_hash") for f in fdps if f.get("full_hash"))
    lib_counts = Counter(f.get("full_hash") for f in lib if f.get("full_hash"))
    by_addr = {}
    for f in fdps:
        h = f.get("full_hash")
        by_addr[f["address"].lower()] = {
            "hashable": f.get("hashable", False),
            "code_units": f.get("code_units"),
            "full_hash": h,
            "same_hash_in_image": fdps_counts.get(h, 0) if h else 0,
            "same_hash_in_ail_lib": lib_counts.get(h, 0) if h else 0,
        }
    return by_addr


def watcom_symbol_set() -> set:
    data = load(TRIAGE_WS / "fid" / "watcom_symbols.json", {})
    names = set()
    if isinstance(data, dict):
        for value in data.values():
            if isinstance(value, list):
                for item in value:
                    if isinstance(item, str):
                        names.add(item)
                    elif isinstance(item, dict) and "name" in item:
                        names.add(item["name"])
            elif isinstance(value, dict):
                names.update(value.keys())
    elif isinstance(data, list):
        for item in data:
            if isinstance(item, str):
                names.add(item)
            elif isinstance(item, dict) and "name" in item:
                names.add(item["name"])
    return names


def ail_symbol_set() -> set:
    """Every symbol the FD2-synthesised AIL library publishes."""
    names = set()
    for f in load(TRIAGE_WS / "fid_ail" / "hashes_ail_code.json", {}).get("functions", []):
        if f.get("name"):
            names.add(f["name"])
    return names


def retire_by_boundary(ws: Path, verdict_dir: Path, facts: list) -> list:
    """Retire every verdict inside the range of a boundary fix that has landed.

    Moving a function's body changes what its neighbours are, so their verdicts
    describe code that is no longer theirs -- even when their own bytes did not
    move and the body hash still matches.  ApplyRereviewVerdicts appends one line
    per boundary change; each line is consumed exactly once, tracked in
    boundary_applied.json, so a rebuild does not keep retiring the same
    neighbours forever.
    """
    log = ws / "boundary_changes.jsonl"
    state = ws / "boundary_applied.json"
    if not log.is_file() or not verdict_dir.is_dir():
        return []
    lines = [ln for ln in log.read_text(encoding="utf-8").splitlines() if ln.strip()]
    already = (load(state, {}) or {}).get("lines", 0)
    fresh = lines[already:]
    dump(state, {"lines": len(lines)})
    if not fresh:
        return []

    ranges = []
    for line in fresh:
        try:
            entry = json.loads(line)
            ranges.append((int(entry["min"], 16), int(entry["max"], 16)))
        except (ValueError, KeyError):
            continue
    retired = []
    for fn in facts:
        lo, hi = int(fn["addr"], 16), int(fn["end"], 16)
        if not any(not (hi < rlo or lo > rhi) for rlo, rhi in ranges):
            continue
        path = verdict_dir / ("%s.json" % fn["addr"])
        if path.is_file():
            path.replace(path.with_suffix(".superseded-boundary"))
            retired.append(fn["addr"])
    return retired


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ws", type=Path, default=DEFAULT_WS)
    ap.add_argument("--verdicts", type=Path, default=None)
    args = ap.parse_args()
    ws = args.ws
    verdict_dir = args.verdicts or (ws / "verdicts")

    facts = load(ws / "functions.json")
    current = {c["addr"]: c for c in load(ws / "current.json")}
    wat = watcom_hits()
    ail = ail_hits()
    hashes = hash_index()
    wat_syms = watcom_symbol_set()
    ail_syms = ail_symbol_set()

    for fn in facts:
        addr = fn["addr"]
        packet = dict(fn)
        packet["fid"] = {
            "watcom": wat.get(addr, {}).get("candidates", []),
            "watcom_candidate_count": wat.get(addr, {}).get("candidate_count", 0),
            "ail": ail.get(addr, {}).get("candidates", []),
            "ail_candidate_count": ail.get(addr, {}).get("candidate_count", 0),
        }
        packet["fid"].update(hashes.get(addr, {}))
        packet["asm"] = str(ws / "asm" / ("%s.txt" % addr))
        dump(ws / "items" / "facts" / ("%s.json" % addr), packet)

        cur = dict(current.get(addr, {}))
        name = cur.get("name", "")
        cur["name_is_watcom_public_symbol"] = name in wat_syms
        cur["name_is_ail_library_symbol"] = name in ail_syms
        cur["pool_tags"] = [t for t in cur.get("tags", []) if t.startswith("pool_")]
        dump(ws / "items" / "current" / ("%s.json" % addr), cur)

    addrs = [f["addr"] for f in facts]
    live_sha = {f["addr"]: f["body_sha"] for f in facts}

    retired_boundary = retire_by_boundary(ws, verdict_dir, facts)

    todo, retired, orphaned, done = [], [], [], []
    if verdict_dir.is_dir():
        for path in sorted(verdict_dir.glob("*.json")):
            key = path.stem.lower()
            try:
                verdict = load(path)
            except ValueError:
                path.replace(path.with_suffix(".unparseable"))
                retired.append(key)
                continue
            covers = (verdict or {}).get("covers") or {}
            if key not in live_sha:
                # The function this verdict describes no longer exists: a boundary
                # fix merged or deleted it.  Keeping the file would make the
                # address look done forever.
                path.replace(path.with_suffix(".orphan"))
                orphaned.append(key)
                continue
            if covers.get("body_sha") != live_sha[key]:
                path.replace(path.with_suffix(".superseded-%s" % (covers.get("body_sha") or "none")))
                retired.append(key)
                continue
            done.append(key)

    done_set = set(done)
    todo = [a for a in addrs if a not in done_set]

    worklist = {
        "functions": addrs,
        "functions_total": len(addrs),
        "todo": todo,
        "done": len(done),
        "retired_stale": retired,
        "retired_orphan": orphaned,
        "retired_boundary": retired_boundary,
    }
    dump(ws / "worklist.json", worklist)

    print("functions        %d" % len(addrs))
    print("verdicts done    %d" % len(done))
    print("still to judge   %d" % len(todo))
    print("retired (stale)  %d" % len(retired))
    print("retired (orphan) %d" % len(orphaned))
    print("retired (neighbour of a boundary fix) %d" % len(retired_boundary))
    print("watcom fid hits  %d addresses" % len(wat))
    print("ail fid hits     %d addresses" % len(ail))
    print("packets written  %s" % (ws / "items"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
