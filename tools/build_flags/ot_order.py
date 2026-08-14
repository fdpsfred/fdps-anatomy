"""Find the flag combination that gives FDPS.LE's frames *and* its scaling.

`-ot` (favour time) makes wcc386 scale indices with `lea reg,[reg*N]`, which is
what FDPS.LE does; but on its own it also turns on the optimiser, and FDPS.LE's
frames are plainly unoptimised.  Both properties have to come out of one command
line, and wcc386 processes options left to right, so ordering matters.

Reports three things per combination: whether the frame is the unoptimised
EBP form, whether locals round-trip through the stack, and which scaling
instruction is used.

All compiling runs the DOS hosted wcc386 inside DOSBox-X.
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
OUT = os.path.join(REPO, "workspace", "build_flags", "otorder")

BASE = "-bt=dos4g -mf -zq -4s -fpi -s"
BINDIRS = ["BIN", "BINB", "BINW"]
MTIME_SLACK = 4.0
# scale.c answers the scaling question, probe.c the frame question
SOURCES = ["scale.c", "probe.c"]

COMBOS = [
    "-od",
    "-ot",
    "-od -ot",
    "-ot -od",
    "-os",
    "-od -os",
    "-ot -od -ot",
    "-oad",
    "-odt",
    "-otd",
    "-od -ot -oa",
    "-ot -od -oi",
]


def write(path, text):
    with open(path, "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write(text)


def run(tags):
    path = ";".join("D:\\" + d for d in BINDIRS)
    lines = ["@echo off", "set WATCOM=D:\\", "set PATH=Z:\\;" + path,
             "set INCLUDE=D:\\H", "c:"]
    for tag, combo in tags.items():
        for src in SOURCES:
            stem = "%s%s" % (tag, src[0])
            lines.append("wcc386 %s %s -fo=%s.obj %s >>build.out"
                         % (BASE, combo, stem, src))
            lines.append("wdisasm -l=%s.dis -a -e -p %s.obj >>build.out"
                         % (stem, stem))
    lines += ["echo done >DONE.TXT", "exit"]
    write(os.path.join(OUT, "build.bat"), "\n".join(lines) + "\n")
    write(os.path.join(OUT, "run.conf"),
          "[cpu]\ncycles=max\n[autoexec]\n"
          'mount c "%s"\nmount d "%s"\nc:\ncall build.bat\n' % (OUT, WATCOM))
    for name in os.listdir(OUT):
        if name.endswith((".dis", ".obj")) or name in ("DONE.TXT", "build.out"):
            try:
                os.remove(os.path.join(OUT, name))
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


def read(stem, started):
    dis = os.path.join(OUT, "%s.dis" % stem)
    if not os.path.exists(dis) or os.path.getmtime(dis) < started - MTIME_SLACK:
        return None
    with open(dis, errors="replace") as fh:
        return fh.read()


def main():
    os.makedirs(OUT, exist_ok=True)
    for src in SOURCES:
        shutil.copyfile(os.path.join(HERE, src), os.path.join(OUT, src))
    combos = sys.argv[1:] or COMBOS
    tags = {"c%02d" % (i + 1): c for i, c in enumerate(combos)}
    started = run(tags)

    print("base: %s\n" % BASE)
    print("%-16s %-24s %-14s %s" % ("flags", "frame", "locals", "scaling"))
    print("-" * 74)
    for tag, combo in tags.items():
        scale_txt = read(tag + "s", started)
        frame_txt = read(tag + "p", started)
        if scale_txt is None or frame_txt is None:
            print("%-16s (rejected or no listing)" % combo)
            continue
        forms = sorted(set(re.sub(r"\s+", " ", m.group(1)) for m in re.finditer(
            r"(shl\s+e\w\w,02H|lea\s+e\w\w,\+0H\[e\w\w\*4\])", scale_txt)))
        frame = "?"
        m = re.search(r"^five_args:\s+(\S+\s+\S+)", frame_txt, re.M)
        if m:
            frame = re.sub(r"\s+", " ", m.group(1))
        # unoptimised code stores a result into a local and reads it straight back
        locals_ = "round-trip" if re.search(
            r"mov\s+dword ptr -4H\[ebp\],eax\n\s+mov\s+eax,dword ptr -4H\[ebp\]",
            frame_txt) else "kept in reg"
        print("%-16s %-24s %-14s %s" % (combo, frame, locals_,
                                        ",".join(forms) or "-"))
    print("\nFDPS.LE has: frame=push ebx (four pushes then EBP)  "
          "locals=round-trip  scaling=lea reg,[reg*4]")


if __name__ == "__main__":
    main()
