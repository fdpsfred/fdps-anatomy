"""Explode the DOS 32-bit stack-convention runtime of every installed Watcom.

The version verdict in rebuild_info/build_flags.md rests on a handful of byte
matches; this widens the base to every library module of every candidate
release, so that each confirmed CRT function in FDPS.LE can be asked which
releases it is compatible with.

Extraction is `wlib -q -x`, never a hand-written .lib reader (see
rebuild_info/pitfalls.md).  Records are then repaired for Watcom Easy OMF-386
with the ticket-14 patcher, because the same quirky-record problem that stops
Ghidra's loader also stops any straight record walk.

Usage:
    python tools/crt_version/extract_versions.py [--out <dir>] [--only 10.0a ...]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_OUT = REPO / "workspace" / "crt_version"

SERIES_ROOTS = [
    Path(r"C:\Users\fdpsf\Documents\WATCOM_9.5_series"),
    Path(r"C:\Users\fdpsf\Documents\WATCOM_10_series"),
]
# Both 10.0a installs are kept.  They agree on the compiler, MATH387S, EMU387,
# GRAPH and the startup object, so they are the same release — but their
# CLIB3S.LIB differ, and one of the two is short of a module.  Which copy is
# intact is evidence, not an assumption to bake in here, so the sweep reports
# them side by side.
SKIP_INSTALLS: set[str] = set()

# (label, path relative to the install root); the startup object is a bare .obj
LIBRARIES = [
    ("CLIB3S", "LIB386/DOS/CLIB3S.LIB"),
    ("MATH387S", "LIB386/MATH387S.LIB"),
    ("EMU387", "LIB386/DOS/EMU387.LIB"),
    ("GRAPH", "LIB386/DOS/GRAPH.LIB"),
]
STARTUP_GLOBS = ["LIB386/DOS/CSTRT*.OBJ", "LIB386/DOS/CSTART*.OBJ"]

WLIB = Path(r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a\BINNT\wlib.exe")
PATCHER = REPO / "tools" / "pool_triage" / "fid" / "omf_patch_segdef.py"


def installs():
    for series in SERIES_ROOTS:
        if not series.is_dir():
            continue
        for root in sorted(series.iterdir()):
            if not root.is_dir() or root.name in SKIP_INSTALLS:
                continue
            if not (root / "LIB386").is_dir():
                continue
            yield root.name.replace("WATCOM_", ""), root


def run(cmd, **kw):
    res = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        raise SystemExit("command failed: %s" % " ".join(str(c) for c in cmd))
    return res.stdout


def explode(lib_path: Path, dest: Path) -> int:
    dest.mkdir(parents=True, exist_ok=True)
    for stale in dest.glob("*.obj"):
        stale.unlink()
    if lib_path.suffix.lower() == ".obj":
        (dest / lib_path.name.lower()).write_bytes(lib_path.read_bytes())
        return 1
    local = dest / lib_path.name
    local.write_bytes(lib_path.read_bytes())
    run([str(WLIB), "-q", "-x", local.name], cwd=dest)
    local.unlink()
    return len(list(dest.glob("*.obj")))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--only", nargs="*", default=None)
    args = ap.parse_args()

    if not WLIB.is_file():
        raise SystemExit("wlib not found: %s" % WLIB)

    extracted = args.out / "libs"
    index = {"versions": {}}
    for version, root in installs():
        if args.only and version not in args.only:
            continue
        entry = {"root": str(root), "libs": {}}
        targets = [(label, root / rel) for label, rel in LIBRARIES]
        seen_startup = set()
        for pattern in STARTUP_GLOBS:
            for hit in sorted(root.glob(pattern)):
                if hit.name.lower() in seen_startup:
                    continue
                seen_startup.add(hit.name.lower())
                # one directory per startup object: they share no library, and a
                # shared directory would let the later one wipe the earlier
                targets.append(("STARTUP_" + hit.stem.upper(), hit))
        for label, path in targets:
            if not path.is_file():
                print("  SKIP %-6s %s (missing)" % (version, path.name))
                continue
            dest = extracted / version / label
            n = explode(path, dest)
            run([sys.executable, str(PATCHER), "--in-dir", str(dest),
                 "--in-place", "--quiet"])
            mods = {}
            for obj in sorted(dest.glob("*.obj")):
                mods[obj.stem] = {
                    "file": str(obj),
                    "sha": hashlib.sha256(obj.read_bytes()).hexdigest(),
                    "size": obj.stat().st_size,
                }
            entry["libs"].setdefault(label, {}).update(mods)
            print("  %-6s %-9s %4d modules" % (version, label, n))
        index["versions"][version] = entry

    args.out.mkdir(parents=True, exist_ok=True)
    out = args.out / "lib_index.json"
    out.write_text(json.dumps(index, indent=1), encoding="utf-8")
    print("versions=%d  wrote %s" % (len(index["versions"]), out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
