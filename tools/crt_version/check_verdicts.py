"""Gate for the ticket-16 verdicts: every one must show its working.

This ticket writes nothing into Ghidra, so the usual orphan-code/error-bookmark
gate has nothing to check.  What can go wrong here instead is a verdict that
states a compatible-version set without saying what was compared to get it —
which reads exactly like a sound one and cannot be re-checked later.  So the
gate is on the evidence: the version sets have to cover every candidate release
exactly once, and the evidence text has to name something concrete from the
packet (a module, an offset, a byte count, an instruction).

Usage:
    python tools/crt_version/check_verdicts.py [--addresses a b c] [--json]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
WORK = REPO / "workspace" / "crt_version"

BASIS_VALUES = {
    "byte_match",        # matched a module byte for byte with fixups ignored
    "ambiguous_match",   # matched, but in so many modules that none can be named
    "no_match",          # long enough to compare, matched nothing anywhere
    "too_short",         # not enough unmasked bytes to identify anything
    "not_comparable",    # split body, data-only, or otherwise not a lone run
}
POWER_VALUES = {"discriminating", "family_only", "none"}
CONFIDENCE_VALUES = {"high", "medium", "low"}
MIN_EVIDENCE = 80


def candidate_versions() -> list[str]:
    index = json.loads((WORK / "lib_index.json").read_text(encoding="utf-8"))
    return sorted(index["versions"])


def check_one(path: Path, versions: set[str]) -> list[str]:
    problems = []
    try:
        v = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:                                   # noqa: BLE001
        return ["unreadable: %s" % exc]

    for field in ("address", "name", "compatible", "excluded", "basis",
                  "power", "evidence", "confidence"):
        if field not in v:
            problems.append("missing field %s" % field)
    if problems:
        return problems

    if v["address"] != path.stem:
        problems.append("address %s does not match file name" % v["address"])
    compat, excl = set(v["compatible"]), set(v["excluded"])
    if compat & excl:
        problems.append("versions in both lists: %s" % sorted(compat & excl))
    missing = versions - (compat | excl)
    if missing:
        problems.append("versions judged neither way: %s" % sorted(missing))
    unknown = (compat | excl) - versions
    if unknown:
        problems.append("unknown version labels: %s" % sorted(unknown))

    if v["basis"] not in BASIS_VALUES:
        problems.append("basis %r not one of %s" % (v["basis"], sorted(BASIS_VALUES)))
    if v["power"] not in POWER_VALUES:
        problems.append("power %r not one of %s" % (v["power"], sorted(POWER_VALUES)))
    if v["confidence"] not in CONFIDENCE_VALUES:
        problems.append("confidence %r not valid" % v["confidence"])

    evidence = (v.get("evidence") or "").strip()
    if len(evidence) < MIN_EVIDENCE:
        problems.append("evidence is %d chars, needs at least %d"
                        % (len(evidence), MIN_EVIDENCE))
    elif not re.search(r"\d", evidence):
        problems.append("evidence cites no number (offset, byte count or address)")

    if v["basis"] == "byte_match":
        if not (v.get("module") or "").strip():
            problems.append("byte_match without a module name; use ambiguous_match "
                            "when the hits are too generic to name one")
        if not compat:
            problems.append("byte_match with an empty compatible set")
    # ambiguous_match is about what the *bytes* settle, not about what is known.
    # A generic ten byte thunk names no module on its own and still sits at a
    # known offset inside one, fixed by the neighbours transplanted with it; the
    # module field is allowed to carry that, and the evidence says where it came
    # from.  An earlier version of this gate forbade the pair and pushed agents
    # into deleting a correct identification to satisfy the schema.
    if v["basis"] == "ambiguous_match" and not compat:
        problems.append("ambiguous_match with an empty compatible set")
    if v["basis"] in ("no_match", "too_short", "not_comparable") and compat:
        problems.append("basis %s but compatible is not empty" % v["basis"])

    if v["power"] == "discriminating" and len(compat) >= len(versions):
        problems.append("marked discriminating but excludes nothing")
    if v["power"] == "none" and compat:
        problems.append("marked power=none but names compatible releases")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verdicts", type=Path, default=WORK / "verdicts")
    ap.add_argument("--addresses", nargs="*", default=None)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    versions = set(candidate_versions())
    wanted = args.addresses
    if wanted:
        files = [args.verdicts / ("%s.json" % a) for a in wanted]
    else:
        files = sorted(args.verdicts.glob("*.json"))

    failures, missing, ok = {}, [], 0
    for path in files:
        if not path.is_file():
            missing.append(path.stem)
            continue
        problems = check_one(path, versions)
        if problems:
            failures[path.stem] = problems
        else:
            ok += 1

    result = {"checked": len(files), "ok": ok, "missing": missing,
              "failures": failures, "gate_passed": not failures and not missing}
    if args.json:
        print(json.dumps(result, indent=1))
    else:
        print("checked=%d ok=%d missing=%d failing=%d"
              % (len(files), ok, len(missing), len(failures)))
        for stem in missing:
            print("  MISSING  %s" % stem)
        for stem, problems in failures.items():
            print("  FAIL     %s" % stem)
            for p in problems:
                print("             %s" % p)
        print("GATE %s" % ("PASSED" if result["gate_passed"] else "FAILED"))
    return 0 if result["gate_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
