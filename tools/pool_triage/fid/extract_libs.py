"""Extract the Watcom libraries FDPS.LE was linked against into loose .obj files.

Which libraries those are is settled in rebuild_info/build_flags.md: CLIB3S,
MATH387S and EMU387 of the 10.0 family, plus the DOS/4G startup object
CSTRTX3S.OBJ.  GRAPH.LIB is extracted as well so that a false CRT match against
it would show up rather than pass silently.

Three versions are extracted (10.0, 10.0a, 10.0b) because the ticket-11
evidence narrows FDPS.LE to the 10.0 family without separating 10.0a from
10.0b; identical modules are deduplicated by SHA-256 in the manifest, so the
extra versions cost little.

Steps, per version:
  1. wlib -q -x         explode each library into one .obj per module
  2. omf_patch_segdef   repair Watcom Easy OMF-386 records Ghidra cannot read
  3. build_manifest     one deduplicated worklist for the Ghidra import

Usage:
    python tools/pool_triage/fid/extract_libs.py [--out <workspace dir>]
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DEFAULT_OUT = REPO / "workspace" / "pool_triage" / "fid"

SERIES = Path(r"C:\Users\fdpsf\Documents\WATCOM_10_series")
VERSIONS = ["WATCOM_10.0", "WATCOM_10.0a", "WATCOM_10.0b"]

# (label, path relative to a version root)
LIBRARIES = [
    ("CLIB3S", "LIB386/DOS/CLIB3S.LIB"),
    ("MATH387S", "LIB386/MATH387S.LIB"),
    ("EMU387", "LIB386/DOS/emu387.lib"),
    ("GRAPH", "LIB386/DOS/graph.lib"),
    ("CSTRTX3S", "LIB386/DOS/CSTRTX3S.OBJ"),
]

WLIB = SERIES / "WATCOM_10.0a" / "BINNT" / "wlib.exe"


def run(cmd, **kw):
    result = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit("command failed: %s" % " ".join(str(c) for c in cmd))
    return result.stdout


def extract_one(lib_path: Path, dest: Path) -> int:
    """Explode one library (or copy one bare .obj) into ``dest``."""
    dest.mkdir(parents=True, exist_ok=True)
    for stale in dest.glob("*.obj"):
        stale.unlink()
    if lib_path.suffix.lower() == ".obj":
        (dest / lib_path.name.lower()).write_bytes(lib_path.read_bytes())
        return 1
    # wlib writes the extracted modules into the current directory, so run it
    # from the destination with a local copy of the library.
    local = dest / lib_path.name
    local.write_bytes(lib_path.read_bytes())
    run([str(WLIB), "-q", "-x", local.name], cwd=dest)
    local.unlink()
    return len(list(dest.glob("*.obj")))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    if not WLIB.is_file():
        raise SystemExit("wlib not found: %s" % WLIB)

    extracted = args.out / "extracted"
    total = 0
    for version in VERSIONS:
        root = SERIES / version
        if not root.is_dir():
            print("SKIP missing version %s" % version)
            continue
        label = version.replace("WATCOM_", "")
        for lib_label, rel in LIBRARIES:
            lib_path = root / rel
            if not lib_path.is_file():
                print("SKIP missing %s %s" % (label, rel))
                continue
            dest = extracted / label / lib_label
            n = extract_one(lib_path, dest)
            total += n
            print("  %-6s %-9s %4d modules" % (label, lib_label, n))

    print("extracted %d modules" % total)

    patcher = HERE / "omf_patch_segdef.py"
    for lib_dir in sorted(extracted.glob("*/*")):
        if lib_dir.is_dir():
            out = run([sys.executable, str(patcher), "--in-dir", str(lib_dir),
                       "--in-place", "--quiet"])
            tail = [line for line in out.splitlines() if line.startswith("Total")]
            print("  patch %-24s %s" % (lib_dir.relative_to(extracted), tail[0] if tail else ""))

    manifest = args.out / "manifest.json"
    print(run([sys.executable, str(HERE / "build_manifest.py"),
               "--extracted", str(extracted), "--out", str(manifest)]).strip())

    # Ghidra names an imported program after its file, and FidPopulate looks the
    # program up by "<manifest key>.obj", so the unique modules have to be
    # materialised under those names before the import.
    dedup = args.out / "dedup"
    print(run([sys.executable, str(HERE / "build_dedup_dir.py"),
               "--manifest", str(manifest), "--out", str(dedup)]).strip())
    data = json.loads(manifest.read_text(encoding="utf-8"))
    for module in data["modules"]:
        module["src"] = str((dedup / ("%s.obj" % module["key"])).resolve()).replace("\\", "/")
    manifest.write_text(json.dumps(data, indent=2), encoding="utf-8")
    print("manifest src rewritten to the dedup directory")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
