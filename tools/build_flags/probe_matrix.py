"""Compile probe.c under a matrix of wcc386 flag sets and disassemble each OBJ.

The point is differential evidence: run the real compiler under DOS, then read
which flag combination reproduces the code shapes observed in FDPS.LE, rather
than guessing from the machine code.  One DOSBox-X session runs the whole
matrix; nothing is compiled on the Windows host.

Variant tags are v01..vNN because DOS tools only see 8.3 names; the legend maps
each tag back to its flags.  Outputs land in workspace/build_flags/probe/.

Usage:
    python probe_matrix.py            # run the whole matrix
    python probe_matrix.py v03 v07    # only the named variants
    python probe_matrix.py --legend   # print the tag -> flags mapping
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
OUT = os.path.join(REPO, "workspace", "build_flags", "probe")

SRC = "probe.c"
BINDIRS = ["BIN", "BINB", "BINW"]
BASE = ["-bt=dos4g", "-zq"]
MTIME_SLACK = 4.0

# Sweep of everything that plausibly moves one of the observed shapes.  The
# settled flag set is -mf -4s -fpi -s -od; verify_flags.py is the gate for it.
VARIANT_FLAGS = [
    ["-ms", "-3s", "-fpi87"],
    ["-ms", "-3r", "-fpi87"],
    ["-ms", "-4s", "-fpi87"],
    ["-ms", "-5s", "-fpi87"],
    ["-mf", "-3s", "-fpi87"],
    ["-ms", "-3s", "-fpi87", "-zc"],
    ["-ms", "-3s", "-fpi"],
    ["-ms", "-3s", "-fpc"],
    ["-ms", "-3s", "-fpi87", "-fp3"],
    ["-ms", "-3s", "-fpi87", "-fp5"],
    ["-ms", "-3s", "-fpi87", "-s"],
    ["-ms", "-3s", "-fpi87", "-ox"],
    ["-ms", "-3s", "-fpi87", "-zp1"],
    ["-ms", "-3s", "-fpi87", "-zp4"],
    ["-ms", "-3s", "-fpi87", "-zp8"],
    ["-ms", "-3s", "-fpi87", "-s", "-of+"],
    ["-ms", "-3s", "-fpi87", "-of+"],
    ["-ms", "-3s", "-fpi87", "-d1"],
    ["-ms", "-3s", "-fpi87", "-d2"],
    ["-ms", "-3s", "-fpi87", "-s", "-d2"],
    ["-ms", "-3s", "-fpi87", "-s", "-od"],
    ["-ms", "-4s", "-fpi87", "-s", "-od"],
    ["-ms", "-5s", "-fpi87", "-s", "-od"],
    ["-mf", "-4s", "-fpi", "-s", "-od"],          # the settled set
    ["-mf", "-4s", "-fpi87", "-s", "-od"],
    ["-mf", "-4s", "-fpc", "-s", "-od"],
    ["-mf", "-3s", "-fpi", "-s", "-od"],
    ["-mf", "-5s", "-fpi", "-s", "-od"],
    ["-mf", "-4s", "-fpi", "-s", "-od", "-zc"],
    ["-mf", "-4s", "-fpi", "-s", "-od", "-zp4"],
    ["-mf", "-4s", "-fpi", "-s", "-od", "-fp3"],
    ["-mf", "-4s", "-fpi", "-s", "-od", "-fp5"],
    ["-mf", "-4s", "-fpi", "-s", "-d2"],
    ["-mf", "-4s", "-fpi", "-od"],
]
VARIANTS = {"v%02d" % (i + 1): f for i, f in enumerate(VARIANT_FLAGS)}

# shapes worth reading off each variant
SHAPES = [
    ("frame", r"five_args:\s+(push\s+ebx|sub\s+esp|push\s+ebp|push\s+0)"),
    ("probe", r"(call\s+near ptr __CHK)"),
    ("epilogue", r"^\s+(leave|mov\s+esp,ebp)"),
    ("scale", r"(shl\s+eax,02H|lea\s+eax,\[eax\*4\])"),
]
# segment membership is resolved by walking the listing, not by a spanning
# regex: every listing opens with `_TEXT SEGMENT`
CONST_SYMBOL = r"^const_table\s+DB"


def write(path, text):
    with open(path, "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write(text)


def segment_of(text, pattern):
    """Name of the SEGMENT block holding the line matching `pattern`."""
    current = None
    for line in text.splitlines():
        seg = re.match(r"^(\w+)\s+SEGMENT\b", line)
        if seg:
            current = seg.group(1)
            continue
        if re.match(r"^(\w+)\s+ENDS\b", line):
            current = None
            continue
        if re.search(pattern, line):
            return current
    return None


def run(tags):
    os.makedirs(OUT, exist_ok=True)
    shutil.copyfile(os.path.join(HERE, SRC), os.path.join(OUT, SRC))
    path = ";".join("D:\\" + d for d in BINDIRS)
    lines = ["@echo off", "set WATCOM=D:\\", "set PATH=Z:\\;" + path,
             "set INCLUDE=D:\\H", "c:"]
    for tag in tags:
        flags = " ".join(BASE + VARIANTS[tag])
        lines.append("wcc386 %s -fo=%s.obj %s >>build.out" % (flags, tag, SRC))
        lines.append("wdisasm -l=%s.dis -a -e -p %s.obj >>build.out" % (tag, tag))
    lines += ["echo done >DONE.TXT", "exit"]
    write(os.path.join(OUT, "build.bat"), "\n".join(lines) + "\n")
    write(os.path.join(OUT, "run.conf"),
          "[cpu]\ncycles=max\n[autoexec]\n"
          'mount c "%s"\nmount d "%s"\nc:\ncall build.bat\n' % (OUT, WATCOM))

    for name in ["DONE.TXT", "build.out"] + ["%s.dis" % t for t in tags]:
        p = os.path.join(OUT, name)
        if os.path.exists(p):
            try:
                os.remove(p)
            except OSError:
                pass
    started = time.time()
    proc = subprocess.Popen([DOSBOX, "-silent", "-conf", "run.conf"], cwd=OUT)
    deadline = time.time() + 600
    while time.time() < deadline:
        if os.path.exists(os.path.join(OUT, "DONE.TXT")) or proc.poll() is not None:
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.terminate()
    return started


def main():
    if "--legend" in sys.argv:
        for tag, flags in VARIANTS.items():
            print("%s  %s" % (tag, " ".join(BASE + flags)))
        return
    wanted = [a for a in sys.argv[1:] if not a.startswith("-")] or list(VARIANTS)
    unknown = [t for t in wanted if t not in VARIANTS]
    if unknown:
        raise SystemExit("unknown variant(s): %s\nrun --legend for the list"
                         % ", ".join(unknown))
    started = run(wanted)
    for tag in wanted:
        dis = os.path.join(OUT, "%s.dis" % tag)
        flags = " ".join(BASE + VARIANTS[tag])
        if not os.path.exists(dis) or os.path.getmtime(dis) < started - MTIME_SLACK:
            print("%-5s %-42s (no listing)" % (tag, flags))
            continue
        with open(dis, errors="replace") as fh:
            text = fh.read()
        cells = []
        for label, pat in SHAPES:
            hits = sorted(set(re.sub(r"\s+", " ", m.group(1))[:22]
                              for m in re.finditer(pat, text, re.M)))
            cells.append("%s=%s" % (label, ",".join(hits) if hits else "-"))
        cells.append("const home=%s" % segment_of(text, CONST_SYMBOL))
        print("%-5s %-42s %s" % (tag, flags, " | ".join(cells)))


if __name__ == "__main__":
    main()
