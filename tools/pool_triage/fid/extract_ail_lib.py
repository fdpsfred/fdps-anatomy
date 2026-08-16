"""Extract the FD2 project's synthesised AIL static library for Function ID use.

`fd2-anatomy/libs/ailv3/ailv3.lib` was assembled from FD2.LE's own bytes, not
shipped by Miles, so a hit against it proves "this FDPS byte range equals that
FD2 byte range" and nothing about any official Miles build.  That is exactly
the question ticket 14.1 asks.

The library was packed in consolidated mode: two modules, `ail_code` (every AIL
function in one segment, one PUBDEF per entry point) and `ail_data` (shared
runtime state).  Only `ail_code` carries instructions, so only it is useful to
Function ID; `ail_data` is extracted anyway so that its absence of code is a
checked fact rather than an assumption.

Unlike the Watcom `.LIB` files handled by extract_libs.py, these modules are
emitted by FD2's own OMF writer in plain 32-bit record types, so the Easy
OMF-386 quirk patch finds nothing to do.  It still runs: a silent no-op is the
evidence, guessing is not.

Usage:
    python tools/pool_triage/fid/extract_ail_lib.py [--out <workspace dir>]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DEFAULT_OUT = REPO / "workspace" / "pool_triage" / "fid_ail"

AIL_LIB = Path(r"C:\Users\fdpsf\Documents\fd2-anatomy\libs\ailv3\ailv3.lib")
WLIB = Path(r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a\BINNT\wlib.exe")


def run(cmd, **kw):
    result = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit("command failed: %s" % " ".join(str(c) for c in cmd))
    return result.stdout


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    if not AIL_LIB.is_file():
        raise SystemExit("ailv3.lib not found: %s" % AIL_LIB)
    if not WLIB.is_file():
        raise SystemExit("wlib not found: %s" % WLIB)

    extracted = args.out / "extracted"
    extracted.mkdir(parents=True, exist_ok=True)
    for stale in extracted.glob("*.obj"):
        stale.unlink()

    # wlib treats '-' as the "delete module" command prefix and writes its
    # output into the current directory, so it gets a local copy under a name
    # free of both hazards rather than the repo path.
    local = extracted / "ailv3.lib"
    shutil.copyfile(AIL_LIB, local)
    run([str(WLIB), "-q", "-x", local.name], cwd=extracted)
    run([str(WLIB), "-q", "-l=ailv3.lst", local.name], cwd=extracted)
    local.unlink()

    objs = sorted(extracted.glob("*.obj"))
    print("extracted %d modules" % len(objs))
    for obj in objs:
        print("  %-16s %8d bytes" % (obj.name, obj.stat().st_size))

    out = run([sys.executable, str(HERE / "omf_patch_segdef.py"),
               "--in-dir", str(extracted), "--in-place"])
    print(out.strip())

    manifest = {
        "source_lib": str(AIL_LIB).replace("\\", "/"),
        "source_sha256": hashlib.sha256(AIL_LIB.read_bytes()).hexdigest(),
        "modules": [
            {
                "key": obj.stem,
                "sha": hashlib.sha256(obj.read_bytes()).hexdigest(),
                "src": str(obj.resolve()).replace("\\", "/"),
            }
            for obj in objs
        ],
    }
    path = args.out / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print("wrote %s" % path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
