"""Which CRT functions still need a verdict, so a run can pick up where it left off.

A verdict counts as done only when it exists, passes the gate, and was written
against the packet that is on disk now.  The last part matters because the sweep
gets re-run when the tooling improves: a verdict formed from an older packet
describes a comparison that no longer holds, and silently keeping it would let
a stale answer survive a correction.

Usage:
    python tools/crt_version/pending.py            # addresses still to do
    python tools/crt_version/pending.py --all      # every address, with state
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from check_verdicts import candidate_versions, check_one

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
WORK = REPO / "workspace" / "crt_version"


def state_of(addr: str, packets: Path, verdicts: Path, versions: set[str]) -> dict:
    packet = json.loads((packets / ("%s.json" % addr)).read_text(encoding="utf-8"))
    stamp = {"dump_len": packet["dump_len"],
             "sweep_compatible": packet["compatible_versions"]}
    vpath = verdicts / ("%s.json" % addr)
    if not vpath.is_file():
        return {"address": addr, "state": "missing", **stamp}
    problems = check_one(vpath, versions)
    if problems:
        return {"address": addr, "state": "failing", "problems": problems, **stamp}
    verdict = json.loads(vpath.read_text(encoding="utf-8"))
    if verdict.get("dump_len") != stamp["dump_len"] or \
       verdict.get("sweep_compatible") != stamp["sweep_compatible"]:
        return {"address": addr, "state": "stale", **stamp}
    return {"address": addr, "state": "done",
            "power": verdict.get("power"), "confidence": verdict.get("confidence"),
            "open": bool(verdict.get("open_questions")), **stamp}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--packets", type=Path, default=WORK / "packets")
    ap.add_argument("--verdicts", type=Path, default=WORK / "verdicts")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()

    args.verdicts.mkdir(parents=True, exist_ok=True)
    versions = set(candidate_versions())
    addrs = sorted(p.stem for p in args.packets.glob("*.json"))
    states = [state_of(a, args.packets, args.verdicts, versions) for a in addrs]
    if args.all:
        print(json.dumps(states, indent=1))
    else:
        print(json.dumps([s["address"] for s in states if s["state"] != "done"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
