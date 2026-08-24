"""Fold the per-function verdicts into one version statement.

Each verdict answers a local question — which releases could this one function
have come from.  The release the executable was built with is the answer to the
global one, and it is not a per-function judgement: it is the intersection of
every function's compatible set, plus an account of what happens when that
intersection is empty or when one function disagrees with the rest.

Nothing here decides anything about an individual function; it only counts.

Usage:
    python tools/crt_version/aggregate.py [--json]
"""
from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
WORK = REPO / "workspace" / "crt_version"


def load(verdicts: Path) -> list[dict]:
    out = []
    for path in sorted(verdicts.glob("*.json")):
        try:
            out.append(json.loads(path.read_text(encoding="utf-8")))
        except Exception:                                       # noqa: BLE001
            continue
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verdicts", type=Path, default=WORK / "verdicts")
    ap.add_argument("--out", type=Path, default=WORK / "aggregate.json")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    versions = sorted(json.loads((WORK / "lib_index.json")
                                 .read_text(encoding="utf-8"))["versions"])
    verdicts = load(args.verdicts)
    informative = [v for v in verdicts if v.get("compatible")]

    intersection = set(versions)
    for v in informative:
        intersection &= set(v["compatible"])

    # Which single function is responsible for excluding each release, and how
    # much weight that one function carries.
    excluders = {ver: [] for ver in versions}
    for v in informative:
        for ver in versions:
            if ver not in v["compatible"]:
                excluders[ver].append({
                    "address": v["address"], "name": v.get("name", ""),
                    "module": v.get("module", ""),
                    "comparable_bytes": v.get("comparable_bytes", 0),
                    "confidence": v.get("confidence", ""),
                })

    high = [v for v in informative if v.get("confidence") == "high"]
    high_intersection = set(versions)
    for v in high:
        high_intersection &= set(v["compatible"])

    result = {
        "candidate_versions": versions,
        "verdicts": len(verdicts),
        "informative": len(informative),
        "no_comparison": len(verdicts) - len(informative),
        "basis_breakdown": dict(Counter(v.get("basis", "?") for v in verdicts)),
        "power_breakdown": dict(Counter(v.get("power", "?") for v in verdicts)),
        "confidence_breakdown": dict(Counter(v.get("confidence", "?") for v in verdicts)),
        "intersection": sorted(intersection),
        "intersection_high_confidence_only": sorted(high_intersection),
        "compatible_counts": {ver: sum(1 for v in informative
                                       if ver in v["compatible"]) for ver in versions},
        "excluded_by": {ver: excluders[ver][:20] for ver in versions},
        "excluded_by_count": {ver: len(excluders[ver]) for ver in versions},
        "discriminating": [
            {"address": v["address"], "name": v.get("name", ""),
             "module": v.get("module", ""),
             "comparable_bytes": v.get("comparable_bytes", 0),
             "compatible": v["compatible"], "confidence": v.get("confidence", "")}
            for v in informative if v.get("power") == "discriminating"
        ],
        "open_questions": [
            {"address": v["address"], "name": v.get("name", ""),
             "open_questions": v["open_questions"]}
            for v in verdicts if v.get("open_questions")
        ],
    }
    args.out.write_text(json.dumps(result, indent=1), encoding="utf-8")

    if args.json:
        print(json.dumps(result, indent=1))
    else:
        print("verdicts=%d informative=%d" % (result["verdicts"], result["informative"]))
        print("intersection: %s" % (", ".join(result["intersection"]) or "(empty)"))
        print("intersection, high confidence only: %s"
              % (", ".join(result["intersection_high_confidence_only"]) or "(empty)"))
        print("\nper release: compatible / excluded-by")
        for ver in versions:
            print("  %-16s %4d / %4d" % (ver, result["compatible_counts"][ver],
                                         result["excluded_by_count"][ver]))
        print("\ndiscriminating functions: %d" % len(result["discriminating"]))
        for d in result["discriminating"]:
            print("  %s %-28s %-14s %5d byte  -> %s"
                  % (d["address"], d["name"][:28], d["module"][:14],
                     d["comparable_bytes"], ",".join(d["compatible"])))
    print("wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
