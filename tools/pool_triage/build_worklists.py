"""Turn the raw Ghidra dumps into one evidence packet per work item.

Ticket 14 processes two fixed worklists -- every undefined block in .object1,
then every function -- and ADR-0007 requires each agent to see exactly one
item.  Handing an agent a packet that already contains the bytes, the
neighbours, the call edges and the Function ID hit keeps a cheap item cheap: a
run of alignment filler can be judged without a single Ghidra call.

Inputs (from DumpTriageWorklist.java and the FID pipeline):
    <ws>/blocks.json  <ws>/functions.json  <ws>/fid/results/matches_*.json

Outputs:
    <ws>/items/blocks/<start>.json      one packet per undefined block
    <ws>/items/functions/<addr>.json    one packet per function
    <ws>/worklist.json                  the id lists and the counts

Usage:
    python tools/pool_triage/build_worklists.py [--ws <workspace/pool_triage>]
                                                [--pools-dir <dir>] [--blocks-dir <dir>]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_WS = REPO / "workspace" / "pool_triage"

# A Function ID candidate below this score is noise; FD2 used the same cut.
FID_MIN_SCORE = 10.0
FID_KEEP = 3


def load(path: Path):
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)


def dump(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as fh:
        json.dump(obj, fh, indent=1, ensure_ascii=False)


def collect_fid(ws: Path) -> dict:
    """address -> the best few library candidates, merged over every version."""
    results = ws / "fid" / "results"
    hits: dict[str, dict] = {}
    if not results.is_dir():
        return hits
    for path in sorted(results.glob("matches_*.json")):
        version = path.stem.replace("matches_", "")
        data = load(path)
        for match in data.get("matches", []):
            addr = match["address"].lower()
            entry = hits.setdefault(addr, {"candidates": {}})
            for cand in match.get("candidates", []):
                score = cand.get("score") or 0.0
                if score < FID_MIN_SCORE:
                    continue
                name = cand.get("matched_name", "")
                slot = entry["candidates"].setdefault(name, {
                    "name": name,
                    "score": score,
                    "source_obj": cand.get("source_obj", ""),
                    "versions": [],
                })
                slot["score"] = max(slot["score"], score)
                if version not in slot["versions"]:
                    slot["versions"].append(version)
    out = {}
    for addr, entry in hits.items():
        cands = sorted(entry["candidates"].values(), key=lambda c: -c["score"])[:FID_KEEP]
        if cands:
            out[addr] = cands
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ws", type=Path, default=DEFAULT_WS)
    ap.add_argument("--blocks-dir", type=Path, default=None,
                    help="verdict dir for blocks, used to report what is left")
    ap.add_argument("--pools-dir", type=Path, default=None,
                    help="verdict dir for pools, used to report what is left")
    args = ap.parse_args()
    ws = args.ws

    blocks = load(ws / "blocks.json")
    functions = load(ws / "functions.json")
    fid = collect_fid(ws)

    # Functions sorted by address, so a block can name the code around it.
    functions.sort(key=lambda f: f["addr"])

    for block in blocks:
        packet = dict(block)
        packet["kind_hint"] = classify_hint(block)
        dump(ws / "items" / "blocks" / ("%s.json" % block["start"]), packet)

    for fn in functions:
        packet = dict(fn)
        packet["fid"] = fid.get(fn["addr"], [])
        dump(ws / "items" / "functions" / ("%s.json" % fn["addr"]), packet)

    block_ids = [b["start"] for b in blocks]
    fn_ids = [f["addr"] for f in functions]
    done_blocks = existing(args.blocks_dir) - supersede_shrunk(args.blocks_dir, blocks)
    done_pools = existing(args.pools_dir)

    worklist = {
        "blocks": block_ids,
        "functions": fn_ids,
        "blocks_todo": [b for b in block_ids if b not in done_blocks],
        "functions_todo": [a for a in fn_ids if a not in done_pools],
        "fid_addresses": len(fid),
    }
    dump(ws / "worklist.json", worklist)

    print("blocks           %d (%d still to judge)" %
          (len(block_ids), len(worklist["blocks_todo"])))
    print("functions        %d (%d still to judge)" %
          (len(fn_ids), len(worklist["functions_todo"])))
    print("fid hits         %d addresses" % len(fid))
    print("packets written  %s" % (ws / "items"))
    return 0


def supersede_shrunk(dir_path: Path | None, blocks: list) -> set:
    """Retire verdicts whose block has since shrunk, and report which.

    A block that held several functions comes back after the apply with the same
    start address and a smaller end -- the tail nobody accounted for yet. Its old
    verdict describes a range that no longer exists, so it is moved aside and the
    block goes back on the worklist. The old file is kept, renamed, because the
    entries it already produced are live in Ghidra.
    """
    if dir_path is None or not dir_path.is_dir():
        return set()
    current = {b["start"]: b["end"] for b in blocks}
    retired = set()
    for path in sorted(dir_path.glob("*.json")):
        start = path.stem.lower()
        if start not in current:
            continue
        try:
            verdict = load(path)
        except ValueError:
            continue
        if (verdict.get("end") or "").lower() != current[start].lower():
            path.rename(path.with_suffix(".superseded-%s" % verdict.get("end", "unknown")))
            retired.add(start)
    return retired


def existing(dir_path: Path | None) -> set:
    if dir_path is None or not dir_path.is_dir():
        return set()
    return {p.stem.lower() for p in dir_path.glob("*.json")}


def classify_hint(block: dict) -> str:
    """A hint only -- what the bytes look like, never the judgement itself."""
    if block["referenced"]:
        return "referenced_data"
    if block["size"] < 16 and block["touches_prev_body"] and block["touches_next_entry"]:
        return "gap_between_functions"
    if block["size"] >= 128:
        return "large_unreferenced"
    return "unreferenced"


if __name__ == "__main__":
    raise SystemExit(main())
