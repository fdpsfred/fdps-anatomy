"""Turn the AIL Function ID query into the numbers ticket 14.1 asks for.

Inputs (all produced by the steps documented in tools/pool_triage/fid/_index.md):
  results/matches_ail_fd2.json   FidQuery.java against FDPS.LE
  hashes_fdps.json               FidHashDump.java against FDPS.LE
  hashes_ail_code.json           FidHashDump.java against the imported ail_code.obj
  ghidra_snapshot/functions.txt  the versioned pool tags

What it computes:
  * per-pool hit counts, so a hit landing outside `pool_ail` is visible rather
    than averaged away
  * for every `pool_ail` function without a hit, the reason: too short for FID
    to hash at all, or hashable but absent from the library
  * full-hash equality between the FDPS function and its best candidate, which
    is the strong form of the claim (same instruction bytes modulo relocated
    operands) as opposed to the scorer's similarity
  * which library modules were never hit, i.e. what FD2 has that FDPS does not

Usage:
    python tools/pool_triage/fid/analyze_ail_matches.py [--work <workspace dir>]
"""
from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DEFAULT_WORK = REPO / "workspace" / "pool_triage" / "fid_ail"
SNAPSHOT = REPO / "ghidra_snapshot" / "functions.txt"


def load_pools(path: Path) -> dict[str, dict]:
    """address -> {'size', 'tags', 'name'} from the versioned Ghidra snapshot."""
    out: dict[str, dict] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or line.startswith(" ") or not line.strip():
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 8:
            continue
        addr, size, _cc, _src, _purge, _flags, tags, proto = parts[:8]
        tagset = set(t for t in tags.split(",") if t and t != "-")
        pools = sorted(t for t in tagset if t.startswith("pool_"))
        out[addr] = {
            "size": int(size, 16),
            "tags": tagset,
            "pool": pools[0] if len(pools) == 1 else ("+".join(pools) or "none"),
            "proto": proto,
        }
    return out


def load_hashes(path: Path) -> dict[str, dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    return {f["address"]: f for f in data["functions"]}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", type=Path, default=DEFAULT_WORK)
    args = ap.parse_args()

    pools = load_pools(SNAPSHOT)
    fdps_hash = load_hashes(args.work / "hashes_fdps.json")
    lib_hash = load_hashes(args.work / "hashes_ail_code.json")
    lib_by_name = {f["name"]: f for f in lib_hash.values() if f["hashable"]}

    matches = json.loads((args.work / "results" / "matches_ail_fd2.json").read_text(
        encoding="utf-8"))["matches"]
    by_addr = {m["address"]: m for m in matches}

    report: dict = {}

    # --- 1. where the hits land -------------------------------------------------
    hit_pools = Counter(pools.get(a, {}).get("pool", "not-in-snapshot") for a in by_addr)
    report["hits_by_pool"] = dict(hit_pools)

    ail_addrs = sorted(a for a, v in pools.items() if v["pool"] == "pool_ail")
    report["pool_ail_total"] = len(ail_addrs)

    # --- 2. per-function verdict for the ail pool -------------------------------
    rows = []
    for addr in ail_addrs:
        info = pools[addr]
        h = fdps_hash.get(addr, {})
        m = by_addr.get(addr)
        best = None
        if m and m["candidates"]:
            best = max(m["candidates"], key=lambda c: c["score"])
        lib = lib_by_name.get(best["matched_name"]) if best else None
        rows.append({
            "address": addr,
            "name": h.get("name", ""),
            "body_size": info["size"],
            "hashable": h.get("hashable", False),
            "code_units": h.get("code_units"),
            "matched_name": best["matched_name"] if best else None,
            "score": best["score"] if best else None,
            "candidates": len(m["candidates"]) if m else 0,
            "full_hash_equal": bool(
                lib and h.get("hashable") and lib["full_hash"] == h.get("full_hash")),
            "specific_hash_equal": bool(
                lib and h.get("hashable") and lib["specific_hash"] == h.get("specific_hash")),
        })
    report["rows"] = rows

    hit = [r for r in rows if r["matched_name"]]
    miss = [r for r in rows if not r["matched_name"]]
    report["summary"] = {
        "hit": len(hit),
        "miss": len(miss),
        "miss_unhashable": sum(1 for r in miss if not r["hashable"]),
        "miss_hashable": sum(1 for r in miss if r["hashable"]),
        "full_hash_equal": sum(1 for r in hit if r["full_hash_equal"]),
        "specific_hash_equal": sum(1 for r in hit if r["specific_hash_equal"]),
        "single_candidate": sum(1 for r in hit if r["candidates"] == 1),
    }

    # --- 3. hits outside the ail pool ------------------------------------------
    contradictions = []
    for addr, m in sorted(by_addr.items()):
        pool = pools.get(addr, {}).get("pool", "not-in-snapshot")
        if pool == "pool_ail":
            continue
        if not m["candidates"]:
            continue
        best = max(m["candidates"], key=lambda c: c["score"])
        h = fdps_hash.get(addr, {})
        lib = lib_by_name.get(best["matched_name"])
        contradictions.append({
            "address": addr,
            "pool": pool,
            "name": m["current_name"],
            "body_size": m["body_size"],
            "matched_name": best["matched_name"],
            "score": best["score"],
            "candidates": len(m["candidates"]),
            "full_hash_equal": bool(
                lib and h.get("hashable") and lib["full_hash"] == h.get("full_hash")),
        })
    report["contradictions"] = contradictions

    # --- 4. library modules never hit ------------------------------------------
    matched_lib_names = set()
    for m in matches:
        for c in m["candidates"]:
            matched_lib_names.add(c["matched_name"])
    unmatched_lib = sorted(n for n in lib_by_name if n not in matched_lib_names)
    report["library_hashable"] = len(lib_by_name)
    report["library_never_hit"] = unmatched_lib

    # --- 5. reconciling the two function counts ---------------------------------
    #
    # `L_<function>_alt_<offset>` is FD2's name for a mid-function alternate
    # entry, not a function; a few of them survive as Ghidra functions in the
    # library image because nothing else covers their code (see
    # FidDemoteAltEntries.java). They are excluded here, which is what makes the
    # library side count 428 - the same number the FD2 knowledge base states.
    lib_real = {f["name"]: f for f in lib_hash.values()
                if not f["name"].startswith("L_") and int(f["address"], 16) < 0x10000}
    paired_lib = set()
    paired_fdps = []
    for r in rows:
        if r["matched_name"] and r["full_hash_equal"] and r["matched_name"] in lib_real:
            paired_fdps.append(r["address"])
            # Credit every candidate, not only the best one. AIL's public layer
            # is a family of debug-log wrappers with one shape - print the API
            # name, call the worker - and Function ID masks both the string
            # pointer and the call target, so a dozen distinct wrappers share a
            # hash. Crediting only the top-scoring name would report the other
            # eleven as absent from FDPS.LE when they are merely tied.
            for c in by_addr[r["address"]]["candidates"]:
                if c["matched_name"] in lib_real:
                    paired_lib.add(c["matched_name"])

    # How many pairings name one library function and nothing else: the match is
    # then an identification rather than a statement about a shape.
    best_users = Counter(r["matched_name"] for r in rows
                         if r["address"] in set(paired_fdps))
    unambiguous = sum(1 for r in rows
                      if r["address"] in set(paired_fdps)
                      and r["candidates"] == 1
                      and best_users[r["matched_name"]] == 1)
    # An AIL function with no library counterpart only threatens a rebuild that
    # links ailv3.lib if something calls it; dead code the linker dragged in
    # does not have to be reproduced at all.
    callers = {}
    cc_path = args.work / "caller_counts.json"
    if cc_path.is_file():
        callers = {f["address"]: f["external_refs"]
                   for f in json.loads(cc_path.read_text(encoding="utf-8"))["functions"]}
    else:
        print("WARNING: %s missing, so the caller counts read as zero. "
              "Run DumpCallerCounts.java against FDPS.LE first." % cc_path)
    paired_set = set(paired_fdps)
    unpaired = [r for r in rows if r["address"] not in paired_set]
    report["reconciliation"] = {
        "library_functions": len(lib_real),
        "library_alt_entry_functions": sum(
            1 for f in lib_hash.values()
            if f["name"].startswith("L_") and int(f["address"], 16) < 0x10000),
        "pool_ail_functions": len(ail_addrs),
        "paired": len(paired_fdps),
        "paired_unambiguous": unambiguous,
        "paired_library_side": len(paired_lib),
        "fdps_only": [{"address": r["address"], "name": r["name"], "body_size": r["body_size"],
                       "hashable": r["hashable"], "callers": callers.get(r["address"])}
                      for r in unpaired],
        "fdps_only_called": sum(1 for r in unpaired if callers.get(r["address"], 0) > 0),
        "library_only": sorted(n for n in lib_real if n not in paired_lib),
    }

    out = args.work / "report"
    out.mkdir(parents=True, exist_ok=True)
    (out / "ail_fid_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    # --- console summary --------------------------------------------------------
    s = report["summary"]
    print("pool_ail functions:      %d" % report["pool_ail_total"])
    print("  with a FID hit:        %d" % s["hit"])
    print("    full-hash identical: %d" % s["full_hash_equal"])
    print("    incl. callee names:  %d" % s["specific_hash_equal"])
    print("    single candidate:    %d" % s["single_candidate"])
    print("  no hit:                %d" % s["miss"])
    print("    unhashable (<4 CU):  %d" % s["miss_unhashable"])
    print("    hashable, no match:  %d" % s["miss_hashable"])
    print()
    print("hits by pool: %s" % dict(sorted(hit_pools.items())))
    print("library modules hashable: %d, never hit: %d"
          % (report["library_hashable"], len(unmatched_lib)))
    print()
    rec = report["reconciliation"]
    print("counts: library %d functions (+%d alt-entry artefacts), pool_ail %d"
          % (rec["library_functions"], rec["library_alt_entry_functions"],
             rec["pool_ail_functions"]))
    print("  paired byte-for-byte: %d FDPS <- %d library (%d name one library "
          "function and nothing else)"
          % (rec["paired"], rec["paired_library_side"], rec["paired_unambiguous"]))
    print("  FDPS-side unpaired:    %d, of which %d have callers"
          % (len(rec["fdps_only"]), rec["fdps_only_called"]))
    print("  library-side unpaired: %d" % len(rec["library_only"]))
    print()
    if contradictions:
        print("hits outside pool_ail (%d):" % len(contradictions))
        for c in contradictions:
            print("  %s %-9s %-34s -> %-34s score=%7.2f cands=%2d full_eq=%s"
                  % (c["address"], c["pool"], c["name"][:34], c["matched_name"][:34],
                     c["score"], c["candidates"], c["full_hash_equal"]))
    print()
    print("wrote %s" % (out / "ail_fid_report.json"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
