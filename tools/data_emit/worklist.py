"""worklist.py -- what ticket 23 still has to do, straight from the linker.

The worklist is not a file anybody maintains.  The first of the unit-test
image's two links reports every symbol the emitted code references and nothing
defines (tools/code_emit/build_emit.py writes that list to
workspace/code_emit/undefined.json), and every global still on that list is
one ticket 23 has not landed.  Landing a definition removes it from the next
build's list, so the list shrinks by itself and "done" is the list being
empty -- no counter to drift from the code.

For each undefined global this adds what a judge agent needs to start work,
all of it read from files rather than decided:

  routing     original address, recorded type and size, segment, target file,
              the files that read it
  header      the extern line(s) src/*.h currently has for it -- the definition
              has to agree with them or the compile fails
  verdict     none / valid / invalid (with the reasons) / landed
  handoff     ids of ticket 22's concerns that were handed to ticket 23 and
              name this symbol (tools/code_emit/data/emit_issues.json)

Usage:
  python tools/data_emit/worklist.py [--build] [--limit N]
     --build   rebuild first (build_emit.py build) so the list is current;
               without it the last build's list is used
Prints JSON on stdout.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import land  # noqa: E402

UNDEF = ROOT / "workspace" / "code_emit" / "undefined.json"
ISSUES = ROOT / "tools" / "code_emit" / "data" / "emit_issues.json"
SRC = ROOT / "src"


def build():
    proc = subprocess.run([sys.executable, str(ROOT / "tools" / "code_emit" / "build_emit.py"),
                           "build"], cwd=str(ROOT), capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    return proc.returncode, (proc.stdout + proc.stderr)[-3000:]


def header_lines():
    """{symbol: [(header, line)]} for every extern in src/*.h."""
    out = {}
    for h in sorted(SRC.glob("*.h")):
        for line in h.read_text(encoding="utf-8").splitlines():
            if not line.lstrip().startswith("extern"):
                continue
            for m in re.finditer(r"\b(data_fdps_\w+)\b", line):
                out.setdefault(m.group(1), []).append((h.name, line.strip()))
    return out


def handoffs():
    """{symbol: [issue id]} for ticket 22 concerns handed to ticket 23."""
    issues = json.loads(ISSUES.read_text(encoding="utf-8"))
    out = {}
    for addr, lst in issues.items():
        for n, e in enumerate(lst):
            if e.get("status") != "handoff" or str(e.get("handoff_to")) != "23":
                continue
            text = (e.get("what") or "") + " " + (e.get("answer") or "")
            for sym in set(re.findall(r"\bdata_fdps_\w+", text)):
                out.setdefault(sym, []).append("%s#%d" % (addr, n))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--limit", type=int, default=0,
                    help="how many symbols still needing a verdict to list; 0 = all")
    a = ap.parse_args()

    report = {}
    if a.build:
        rc, tail = build()
        report["build_rc"] = rc
        if rc != 0:
            report["build_tail"] = tail
    if not UNDEF.is_file():
        report["error"] = "no %s -- run with --build" % UNDEF.relative_to(ROOT)
        print(json.dumps(report, indent=2, ensure_ascii=False))
        return 1
    undef = json.loads(UNDEF.read_text(encoding="utf-8"))
    routing = land.routing_by_symbol(land.load_routing())
    manifest = {e["symbol"] for e in land.load_manifest()["symbols"]}
    hdr = header_lines()
    hand = handoffs()

    rows = []
    not_global = []
    for s in undef.get("symbols", []):
        sym = s["symbol"]
        row = routing.get(sym)
        if row is None:
            not_global.append({"symbol": sym, "kind": s.get("kind")})
            continue
        v = land.read_verdict(sym)
        if sym in manifest:
            state, why = "landed_but_undefined", []
        elif v is None:
            state, why = "none", []
        elif v.get("_unreadable"):
            state, why = "invalid", ["not valid JSON"]
        elif v.get("reroute"):
            state, why = "reroute", [v["reroute"].get("to", "?")]
        else:
            why = land.validate(v, routing)
            state = "invalid" if why else "valid"
        rows.append({"symbol": sym, "addr": row["addr"], "type": row["type"],
                     "size": row.get("size"), "segment": row.get("segment"),
                     "target": row["target"], "readers": row.get("reader_files", []),
                     "header": [{"file": f, "line": l} for f, l in hdr.get(sym, [])],
                     "verdict": state, "verdict_problems": why,
                     "handoff": hand.get(sym, [])})
    rows.sort(key=lambda r: r["addr"])
    todo = [r for r in rows if r["verdict"] in ("none", "invalid")]
    report.update({
        "undefined_total": undef.get("count"),
        "globals_undefined": len(rows),
        "not_globals": not_global,
        "needs_verdict": len(todo),
        "ready_to_land": sum(1 for r in rows if r["verdict"] == "valid"),
        "reroute": [r["symbol"] for r in rows if r["verdict"] == "reroute"],
        "landed_but_undefined": [r["symbol"] for r in rows
                                 if r["verdict"] == "landed_but_undefined"],
        "judge": todo[:a.limit] if a.limit else todo,
    })
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
