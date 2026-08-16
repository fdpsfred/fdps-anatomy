"""Work out which functions can take a vendor library symbol as their name.

The naming rule for both vendor pools is the same and comes from the previous
project: a function that IS a library function is called exactly what the
library calls it, with no project prefix, because the point of the name is that
the Watcom linker resolves it (rebuild_info/naming.md). This script decides
which functions have evidence strong enough to carry such a name, and which have
to be read by an agent first.

Two evidence sources, one per pool:

  crt   the Watcom PUBDEF set, taken from `wlib -l` listings of the libraries
        the linker actually used, cross-checked against the ticket-14 Function
        ID query so a name that the query contradicts is never applied blind.
  ail   the ticket-14.1 Function ID query against ailv3.lib. A match counts only
        when the body hashes identically AND the library function it names is
        not the shape-collision family - AIL's debug-log wrappers all hash the
        same, so a match there identifies the layer, not the function.

Output (workspace/pool_triage/fid_ail/renames/):
  mechanical.json      address -> library symbol, transcription only
  needs_judgement.json address -> why, one agent each

Usage:
    python tools/pool_triage/fid/build_symbol_renames.py [--symbols <json>]
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
SNAPSHOT = REPO / "ghidra_snapshot" / "functions.txt"
CRT_MATCHES = REPO / "workspace" / "pool_triage" / "fid" / "results" / "matches_10.0a.json"

# Below this the Function ID score says "some function of this shape", not
# "this function". Ghidra's own default cutoff is 14.6; the previous project
# used 30 as the line above which a name could be taken without reading the
# body, and that is the number reused here.
AUTO_SCORE = 30.0

# The identifier just before the argument list. `\w` is not enough: the names
# this project is required to use include `@` and `$` - `IF@COS` is a Watcom
# intrinsic helper, `L$1_stk_save_ss` a library-internal static - and a `\w`
# pattern silently returns `COS` and `1_stk_save_ss` for them. Every downstream
# comparison then works on a name no function actually has.
NAME_IN_PROTOTYPE = r"([A-Za-z_$@?][\w$@?]*)\("


def load_snapshot(path: Path):
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or line.startswith(" ") or not line.strip():
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 8:
            continue
        pools = [t[5:] for t in parts[6].split(",") if t.startswith("pool_")]
        m = re.search(NAME_IN_PROTOTYPE, parts[7])
        out[parts[0]] = {
            "name": m.group(1) if m else "?",
            "pool": pools[0] if len(pools) == 1 else "?",
            "size": int(parts[1], 16),
            # A Ghidra thunk shows the name of what it jumps to. That is an
            # alias by design, not a second function laying claim to the symbol.
            "is_thunk": parts[5].startswith("thunk->"),
        }
    return out


def best_candidate(match):
    return max(match["candidates"], key=lambda c: c["score"]) if match["candidates"] else None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", type=Path, default=DEFAULT_WORK)
    ap.add_argument("--symbols", type=Path, required=True,
                    help="JSON list of Watcom PUBDEF names (see _index.md)")
    args = ap.parse_args()

    syms = set(json.loads(args.symbols.read_text(encoding="utf-8")))
    fns = load_snapshot(SNAPSHOT)
    taken = {v["name"]: a for a, v in fns.items()}

    proposals = {}
    judgement = {}

    def propose(addr, symbol, why):
        # A match table can name an address the snapshot does not have - a
        # function created or deleted since the export. Refusing loudly beats a
        # KeyError, and beats renaming something on stale information.
        if addr not in fns:
            judgement[addr] = ("the library comparison proposes %s but this address is "
                               "not a function in the current snapshot" % symbol)
            return
        proposals[addr] = {"from": fns[addr]["name"], "to": symbol, "why": why}

    # ---------------------------------------------------------------- crt
    crt_hits = {}
    if CRT_MATCHES.is_file():
        for m in json.loads(CRT_MATCHES.read_text(encoding="utf-8"))["matches"]:
            b = best_candidate(m)
            if b:
                crt_hits[m["address"]] = b

    for addr, info in sorted(fns.items()):
        if info["pool"] != "crt" or not info["name"].startswith("crt_"):
            continue
        if info["name"].startswith("crt_equivalent_"):
            continue          # a rewrite, not a library symbol; the prefix stays
        symbol = info["name"][4:]
        hit = crt_hits.get(addr)
        if hit and hit["score"] >= AUTO_SCORE and hit["matched_name"] != symbol:
            judgement[addr] = ("named crt_%s but the library comparison says %s "
                               "(score %.1f)" % (symbol, hit["matched_name"], hit["score"]))
            continue
        if symbol not in syms:
            judgement[addr] = ("named crt_%s but %s is not a PUBDEF in any linked "
                               "library" % (symbol, symbol))
            continue
        propose(addr, symbol, "crt: name already carried the library symbol behind a prefix")

    # A strong hit on a function nobody named yet is a name waiting to be applied.
    for addr, hit in sorted(crt_hits.items()):
        info = fns.get(addr)
        if not info or info["pool"] != "crt" or not info["name"].startswith("FUN_"):
            continue
        if hit["score"] < AUTO_SCORE or hit["matched_name"] not in syms:
            continue
        propose(addr, hit["matched_name"],
                "crt: unnamed, library match at score %.1f" % hit["score"])

    # ---------------------------------------------------------------- ail
    report = json.loads((args.work / "report" / "ail_fid_report.json").read_text(encoding="utf-8"))
    claims = Counter(r["matched_name"] for r in report["rows"]
                     if r["matched_name"] and r["full_hash_equal"])
    for r in report["rows"]:
        addr = r["address"]
        if not r["matched_name"] or not r["full_hash_equal"]:
            continue
        if r["candidates"] != 1 or claims[r["matched_name"]] != 1:
            continue          # shape collision: identifies the layer, not the function
        propose(addr, r["matched_name"],
                "ail: byte-identical single-candidate match at score %.1f" % r["score"])

    # ------------------------------------------------ names that fit no rule
    #
    # A vendor-pool function that is neither unnamed nor holding a name the
    # convention allows is not covered by either loop above and would otherwise
    # pass silently.
    for addr, info in sorted(fns.items()):
        if info["pool"] not in ("crt", "ail") or addr in proposals or addr in judgement:
            continue
        n = info["name"]
        if n.startswith(("FUN_", "thunk_FUN_", "crt_equivalent_", "L$")):
            continue
        if info["pool"] == "crt" and n in syms:
            continue
        if info["pool"] == "crt":
            judgement[addr] = ("named %s, which is neither a PUBDEF of a linked library "
                               "nor one of the L$ / crt_equivalent_ forms" % n)
            continue
        # AIL: the prefix alone is not conformance. `_impl` and `_worker` are
        # this project's own invention; the library calls an entry point's
        # implementation `AIL_internal_<name>_inner`, and a name the linker
        # cannot resolve is the whole problem this rule exists to prevent.
        if n.endswith(("_impl", "_worker")):
            judgement[addr] = ("named %s, but the inner worker of a public entry point is "
                               "AIL_internal_<name>_inner in the library; _impl and _worker "
                               "are not conventions ailv3.lib knows" % n)
            continue
        if not n.startswith("AIL_"):
            judgement[addr] = ("named %s, which matches no naming rule for the ail pool" % n)

    # ------------------------------------------------ resolve name conflicts
    #
    # Two functions cannot hold one symbol. Whenever a target is claimed more
    # than once - by two proposals, or by a proposal and a function that
    # already carries it - every claimant goes to an agent, because a conflict
    # means one of the identifications behind it is wrong and picking a winner
    # here would be exactly the batch judgement ADR-0002 forbids.
    # An address claims the name it would end up with, and also any name a
    # strong library match gives it even when the address is already going to an
    # agent for some other reason. Without that second claim the conflict is
    # invisible: a weak match lets one function walk off with `sprintf` while
    # the 265-point match that really owns the name sits in the judgement list.
    # Every function in the program claims a name, not just the vendor pools: a
    # library symbol landing on a name some fdps or binary_artifact function
    # already holds is just as broken, and Ghidra will not say a word about it.
    claims: dict[str, set[str]] = {}
    for addr, info in fns.items():
        if info["is_thunk"] and addr not in proposals:
            continue
        wanted = {proposals[addr]["to"] if addr in proposals else info["name"]}
        hit = crt_hits.get(addr)
        if hit and hit["score"] >= AUTO_SCORE and hit["matched_name"] in syms:
            wanted.add(hit["matched_name"])
        for w in wanted:
            claims.setdefault(w, set()).add(addr)

    # Contested names are collected in full before anything is written out.
    # Deciding inside the loop let a later claimant push an address into the
    # judgement list after the same address had already been written to
    # mechanical.json, so it appeared in both and was renamed anyway.
    contested = set()
    for name, who in claims.items():
        if len(who) > 1:
            for c in sorted(who):
                judgement.setdefault(c, "the name %s is claimed by %s; at most one of "
                                     "them can be right" % (name, " and ".join(sorted(who))))
            contested |= who

    mechanical = {}
    for addr, p in sorted(proposals.items()):
        if addr in contested or addr in judgement or p["from"] == p["to"]:
            continue
        mechanical[addr] = p

    out = args.work / "renames"
    out.mkdir(parents=True, exist_ok=True)

    # An address an agent has already decided drops out of the list. Some of the
    # conflicts here are inherent to the evidence rather than to a mistake - the
    # 26-byte `_itoa` and `_ltoa` forwarders tie at the same Function ID score
    # by construction - so without this the same pair is reported as unresolved
    # on every run, forever, long after it was settled by reading the bodies.
    decided = {p.stem for p in (out / "decisions").glob("*.json")}
    if decided:
        settled = sorted(set(judgement) & decided)
        for addr in settled:
            del judgement[addr]
        if settled:
            print("already decided by an agent, dropped from the list: %d (%s)"
                  % (len(settled), " ".join(settled)))
    (out / "mechanical.json").write_text(json.dumps(mechanical, indent=2), encoding="utf-8")
    (out / "needs_judgement.json").write_text(json.dumps(judgement, indent=2), encoding="utf-8")

    by_pool = Counter(fns[a]["pool"] for a in mechanical)
    print("mechanical renames: %d  %s" % (len(mechanical), dict(by_pool)))
    print("needs judgement:    %d" % len(judgement))
    for a in sorted(judgement):
        print("  %s %-34s %s" % (a, fns.get(a, {}).get("name", "?"), judgement[a]))
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
