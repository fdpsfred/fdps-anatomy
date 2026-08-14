"""Compile probe.c under a matrix of wcc386 flag sets and disassemble each OBJ.

The point is differential evidence: run the real Watcom 10.0a compiler, then read
which flag combination reproduces the code shapes observed in FDPS.LE.  The NT
hosted compiler is used because it is byte-identical in codegen to the DOS
hosted one and needs no emulator.

Outputs land in workspace/build_flags/probe/<tag>.{obj,dis}.

Usage:
    python probe_matrix.py            # run the whole matrix
    python probe_matrix.py <tag>...   # only the named variants
"""

import os
import shutil
import subprocess
import sys

WATCOM = r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "probe")

# Sweep starting point, not the derived answer: the memory model here is the
# one the sweep began with, and the variants below override it where relevant.
# The settled flag set lives in verify_flags.py.
BASE = ["-bt=dos4g", "-ms", "-zq"]

# tag -> flags replacing/extending BASE
VARIANTS = {
    "3s":        ["-3s", "-fpi87"],
    "3r":        ["-3r", "-fpi87"],
    "4s":        ["-4s", "-fpi87"],
    "5s":        ["-5s", "-fpi87"],
    "3s_flat":   ["-3s", "-fpi87", "-mf"],
    "3s_zc":     ["-3s", "-fpi87", "-zc"],
    "3s_fpi":    ["-3s", "-fpi"],
    "3s_fpc":    ["-3s", "-fpc"],
    "3s_fp3":    ["-3s", "-fpi87", "-fp3"],
    "3s_fp5":    ["-3s", "-fpi87", "-fp5"],
    "3s_nostk":  ["-3s", "-fpi87", "-s"],
    "3s_ox":     ["-3s", "-fpi87", "-ox"],
    "3s_zp1":    ["-3s", "-fpi87", "-zp1"],
    "3s_zp4":    ["-3s", "-fpi87", "-zp4"],
    "3s_zp8":    ["-3s", "-fpi87", "-zp8"],
    "3s_s_of":   ["-3s", "-fpi87", "-s", "-of+"],
    "3s_of":     ["-3s", "-fpi87", "-of+"],
    "3s_d1":     ["-3s", "-fpi87", "-d1"],
    "3s_d2":     ["-3s", "-fpi87", "-d2"],
    "3s_s_d2":   ["-3s", "-fpi87", "-s", "-d2"],
    "3s_s_od":   ["-3s", "-fpi87", "-s", "-od"],
    "3s_s_d1":   ["-3s", "-fpi87", "-s", "-d1"],
    "3s_s":      ["-3s", "-fpi87", "-s"],
    "5s_s_d2":   ["-5s", "-fpi87", "-s", "-d2"],
    "5s_s_of":   ["-5s", "-fpi87", "-s", "-of+"],
    "4s_s_od":   ["-4s", "-fpi87", "-s", "-od"],
    "5s_s_od":   ["-5s", "-fpi87", "-s", "-od"],
    "6s_s_od":   ["-6s", "-fpi87", "-s", "-od"],
    "5s_s_od_zc":  ["-5s", "-fpi87", "-s", "-od", "-zc"],
    "5s_s_od_fpi": ["-5s", "-fpi", "-s", "-od"],
    "5s_s_od_fpc": ["-5s", "-fpc", "-s", "-od"],
    "5s_s_od_fp3": ["-5s", "-fpi87", "-fp3", "-s", "-od"],
    "5s_s_od_fp5": ["-5s", "-fpi87", "-fp5", "-s", "-od"],
    "5s_s_od_zp4": ["-5s", "-fpi87", "-s", "-od", "-zp4"],
}


def env():
    e = dict(os.environ)
    e["WATCOM"] = WATCOM
    e["INCLUDE"] = os.path.join(WATCOM, "H")
    e["PATH"] = os.path.join(WATCOM, "BINNT") + os.pathsep + e["PATH"]
    e.pop("WCC386", None)
    return e


def run(tag, flags):
    """Compile and disassemble one variant; return the flag list actually used."""
    src = os.path.join(OUT, "probe.c")
    obj = "%s.obj" % tag
    # -mf overrides the -ms in BASE; drop the loser so wcc386 sees one model
    base = [f for f in BASE if not (f == "-ms" and "-mf" in flags)]
    cmd = [os.path.join(WATCOM, "BINNT", "WCC386.EXE")] + base + flags + \
          ["-fo=" + obj, "probe.c"]
    r = subprocess.run(cmd, cwd=OUT, env=env(), capture_output=True, text=True)
    if r.returncode != 0:
        print("[FAIL] %-10s %s" % (tag, r.stdout.strip() or r.stderr.strip()))
        return None
    # WDISASM parses a leading '-' in any argument as an option switch, so it is
    # run with cwd set to the output directory and a bare relative file name.
    d = subprocess.run(
        [os.path.join(WATCOM, "BINNT", "WDISASM.EXE"), "-l=%s.dis" % tag,
         "-a", "-e", "-p", obj],
        cwd=OUT, env=env(), capture_output=True, text=True)
    if d.returncode != 0:
        print("[warn] %-10s wdisasm: %s" % (tag, d.stdout.strip() or d.stderr.strip()))
    print("[ok]   %-10s %s" % (tag, " ".join(base + flags)))
    return base + flags


def main():
    os.makedirs(OUT, exist_ok=True)
    shutil.copyfile(os.path.join(HERE, "probe.c"), os.path.join(OUT, "probe.c"))
    wanted = sys.argv[1:] or list(VARIANTS)
    for tag in wanted:
        run(tag, VARIANTS[tag])


if __name__ == "__main__":
    main()
