"""Sweep single extra flags on top of the derived set, hunting the `lea` form.

FDPS.LE scales indices with `lea reg,[reg*N]`; the derived flag set produces
`shl reg,N` on every installed Watcom.  Since the CRT copyright string pins the
toolchain to the 10 series, the difference has to come from a flag (or a source
level directive) that has not been tried yet.  This adds one flag at a time to
the derived set and reports what each one does to the scaling shape.

All compiling runs the DOS hosted wcc386 inside DOSBox-X.

Usage:
    python opt_sweep.py            # sweep the built-in candidate list
    python opt_sweep.py --help-text   # dump wcc386's own option list
    python opt_sweep.py -oa -ol       # sweep just these
"""

import os
import re
import shutil
import subprocess
import sys
import time

WATCOM = r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a"
DOSBOX = r"C:\DOSBox-X\dosbox-x.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "optsweep")

SRC = "scale.c"
BASE = "-bt=dos4g -mf -zq -4s -fpi -s -ot -od"
BINDIRS = ["BIN", "BINB", "BINW"]
MTIME_SLACK = 4.0

# every -o sub-option, then the -z and code-generation options not yet tried
CANDIDATES = [
    "-oa", "-oc", "-od", "-oe", "-of", "-of+", "-oi", "-ol", "-ol+", "-om",
    "-on", "-oo", "-op", "-or", "-os", "-ot", "-ou", "-ox",
    "-zdf", "-zdp", "-zdl", "-zff", "-zfp", "-zgf", "-zgp", "-zu", "-zc",
    "-zm", "-zg", "-za", "-ze", "-zt0", "-zt32767", "-zp1", "-zp2", "-zp4",
    "-zp8", "-zk0", "-zk1", "-zl", "-zld",
    "-sg", "-st", "-j", "-r", "-k", "-ei", "-em", "-en",
    "-fpr", "-fp2", "-fp3", "-fp5", "-d1", "-d2", "-db",
    "-bm", "-bd", "-bw", "-4r", "-5r", "-3r",
]


def write(path, text):
    with open(path, "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write(text)


def dos_run(lines, expect, timeout=600):
    """Run a batch inside DOSBox-X; return the run start time."""
    path = ";".join("D:\\" + d for d in BINDIRS)
    head = ["@echo off", "set WATCOM=D:\\", "set PATH=Z:\\;" + path,
            "set INCLUDE=D:\\H", "c:"]
    write(os.path.join(OUT, "build.bat"),
          "\n".join(head + lines + ["echo done >DONE.TXT", "exit"]) + "\n")
    write(os.path.join(OUT, "run.conf"),
          "[cpu]\ncycles=max\n[autoexec]\n"
          'mount c "%s"\nmount d "%s"\nc:\ncall build.bat\n' % (OUT, WATCOM))
    for name in ["DONE.TXT", "build.out"] + expect:
        p = os.path.join(OUT, name)
        if os.path.exists(p):
            try:
                os.remove(p)
            except OSError:
                pass
    started = time.time()
    proc = subprocess.Popen([DOSBOX, "-silent", "-conf", "run.conf"], cwd=OUT)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(os.path.join(OUT, "DONE.TXT")) or proc.poll() is not None:
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.terminate()
    return started


def help_text():
    os.makedirs(OUT, exist_ok=True)
    dos_run(["wcc386 >help.txt"], ["help.txt"])
    with open(os.path.join(OUT, "help.txt"), errors="replace") as fh:
        print(fh.read())


def main():
    if "--help-text" in sys.argv:
        help_text()
        return
    flags = [a for a in sys.argv[1:] if a.startswith("-")] or CANDIDATES
    os.makedirs(OUT, exist_ok=True)
    shutil.copyfile(os.path.join(HERE, SRC), os.path.join(OUT, SRC))
    tags = {"o%02d" % (i + 1): f for i, f in enumerate(flags)}
    lines = []
    for tag, flag in tags.items():
        lines.append("wcc386 %s %s -fo=%s.obj %s >>build.out"
                     % (BASE, flag, tag, SRC))
        lines.append("wdisasm -l=%s.dis -a -e -p %s.obj >>build.out" % (tag, tag))
    started = dos_run(lines, ["%s.dis" % t for t in tags])

    print("base: %s\n" % BASE)
    hits = []
    for tag, flag in tags.items():
        dis = os.path.join(OUT, "%s.dis" % tag)
        if not os.path.exists(dis) or os.path.getmtime(dis) < started - MTIME_SLACK:
            print("%-10s (rejected or no listing)" % flag)
            continue
        with open(dis, errors="replace") as fh:
            text = fh.read()
        # wdisasm writes the scaled-index-only form as `+0H[eax*4]`
        forms = sorted(set(m.group(1) for m in re.finditer(
            r"(shl\s+e\w\w,02H|lea\s+e\w\w,\+0H\[e\w\w\*4\])", text)))
        forms = [re.sub(r"\s+", " ", f) for f in forms]
        mark = ""
        if any("lea" in f for f in forms):
            mark = "  <== matches FDPS.LE"
            hits.append(flag)
        print("%-10s %s%s" % (flag, ",".join(forms) or "-", mark))
    print("\nflags producing the FDPS.LE form: %s" % (hits or "none"))


if __name__ == "__main__":
    main()
