"""Collect the public symbol names of the Watcom libraries FDPS.LE was linked against.

The names are what CRT functions get called, verbatim, so the linker resolves
them at rebuild time (rebuild_info/naming.md). `wlib -l` prints them; nothing
here parses OMF by hand.

The listing has two sections and both are read, because they do not carry the
same set: the dictionary at the top maps every public symbol to its module,
while the per-module blocks below repeat them in a bare column layout. Reading
only one of the two silently drops names.

`CSTRTX3S.OBJ` is a loose object rather than a library, so it is packed into a
throwaway library first - it holds `_cstart_` and the DOS/4G startup symbols,
which appear nowhere else.

Usage:
    python tools/pool_triage/fid/extract_watcom_symbols.py [--out <json>]
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DEFAULT_OUT = REPO / "workspace" / "pool_triage" / "fid" / "watcom_symbols.json"

SERIES = Path(r"C:\Users\fdpsf\Documents\WATCOM_10_series")
VERSION = "WATCOM_10.0a"
WLIB = SERIES / VERSION / "BINNT" / "wlib.exe"

# Exactly the libraries wlink pulls from, per rebuild_info/build_flags.md.
LIBRARIES = ["LIB386/DOS/CLIB3S.LIB", "LIB386/MATH387S.LIB", "LIB386/DOS/emu387.lib"]
LOOSE_OBJECTS = ["LIB386/DOS/CSTRTX3S.OBJ"]

SYMBOL = r"[A-Za-z_$@?][\w$@?]*"


def run(cmd, cwd):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("command failed: %s" % " ".join(str(c) for c in cmd))
    return r.stdout


def symbols_from_listing(text: str) -> set[str]:
    out: set[str] = set()
    for line in text.splitlines():
        if "Offset=" in line:
            continue
        if line.startswith(" "):
            # per-module block: bare names, several to a line
            out.update(tok for tok in line.split() if re.fullmatch(SYMBOL, tok))
        else:
            # dictionary: symbol....module, up to two pairs per line
            for m in re.finditer(r"(%s)\.{2,}(%s)" % (SYMBOL, SYMBOL), line):
                out.add(m.group(1))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    if not WLIB.is_file():
        raise SystemExit("wlib not found: %s" % WLIB)

    # wlib reads '-' as its "delete module" command and writes beside the
    # library, so everything happens on copies under a path free of both
    # hazards.
    work = args.out.parent / "symbols_work"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    root = SERIES / VERSION
    syms: set[str] = set()
    for i, rel in enumerate(LIBRARIES):
        src = root / rel
        if not src.is_file():
            raise SystemExit("library not found: %s" % src)
        lib = "l%d.lib" % i
        shutil.copyfile(src, work / lib)
        run([str(WLIB), "-q", "-l=%s.lst" % lib, lib], cwd=work)
        found = symbols_from_listing((work / ("%s.lst" % lib)).read_text(encoding="latin-1"))
        print("  %-24s %4d symbols" % (Path(rel).name, len(found)))
        syms |= found

    for i, rel in enumerate(LOOSE_OBJECTS):
        src = root / rel
        if not src.is_file():
            raise SystemExit("object not found: %s" % src)
        obj = "o%d.obj" % i
        lib = "o%d.lib" % i
        shutil.copyfile(src, work / obj)
        run([str(WLIB), "-q", "-n", lib, "+" + obj], cwd=work)
        run([str(WLIB), "-q", "-l=%s.lst" % lib, lib], cwd=work)
        found = symbols_from_listing((work / ("%s.lst" % lib)).read_text(encoding="latin-1"))
        print("  %-24s %4d symbols" % (Path(rel).name, len(found)))
        syms |= found

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(sorted(syms), indent=0), encoding="utf-8")
    print("%d unique public symbols -> %s" % (len(syms), args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
