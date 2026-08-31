"""Find what makes wcc386 stage a call argument through EAX before pushing it.

FDPS.LE passes every argument as MOV EAX,<slot> / PUSH EAX.  The settled flag
set emits PUSH <slot> instead -- one byte shorter per argument, and the whole
size gap between an original body and its rebuilt twin wherever a call takes
memory operands.  Nothing behavioural turns on it (ADR-0001 puts instruction
selection outside the equivalence standard), but the flag set is derived
evidence and this is a shape it does not yet account for, so it gets measured
rather than assumed.

Two axes, the same two the rest of this directory uses:

    --flags     one extra flag at a time on top of the settled set, on 10.0a
    --versions  the settled set, on every installed DOS hosted wcc386

Both compile pusharg.c and disassemble it with the same version's wdisasm.
All compiling runs inside DOSBox-X; the host only writes inputs and reads
listings.  Outputs land in workspace/build_flags/pushform/.

Usage:
    python push_form.py                 # both axes
    python push_form.py --flags
    python push_form.py --versions
    python push_form.py --flags -oa -ol # sweep just these
"""

import os
import re
import shutil
import subprocess
import sys
import time

SERIES_ROOTS = [
    r"C:\Users\fdpsf\Documents\WATCOM_9.5_series",
    r"C:\Users\fdpsf\Documents\WATCOM_10_series",
]
WATCOM = r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a"
DOSBOX = r"C:\DOSBox-X\dosbox-x.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "pushform")

SRC = "pusharg.c"
BASE = "-bt=dos4g -mf -zq -4s -fpi -s -ot -od"
BINDIRS = ["BIN", "BINB", "BINW", "BINP"]
MTIME_SLACK = 4.0
TIMEOUT = 600

# The whole -o family plus the code generation options that could plausibly
# reach argument setup.  -oa..-ox are swept because the staged form is what an
# unoptimised compiler does naturally, so the answer may be a flag that turns
# something OFF rather than on.
CANDIDATES = [
    "-oa", "-oc", "-oe", "-of", "-of+", "-oi", "-ol", "-ol+", "-om",
    "-on", "-oo", "-op", "-or", "-os", "-ou", "-ox",
    "-od", "-ot", "-oat", "-ohtx",
    "-3s", "-5s", "-4r", "-3r", "-5r",
    "-zp1", "-zp2", "-zp4", "-zp8", "-zdf", "-zdp", "-zff", "-zgf",
    "-zu", "-zc", "-zm", "-zg", "-za", "-ze", "-zl",
    "-d1", "-d2", "-db", "-s", "-sg", "-st", "-j", "-r", "-k",
    "-fpr", "-fp2", "-fp3", "-fp5", "-bm", "-bd",
]

# A push whose operand is a memory reference is the direct form; a push of a
# register preceded by a load into it is the staged form.  wdisasm renders a
# frame slot as `-4H[ebp]` and may or may not prefix it with `dword ptr`.
PUSH_MEM = re.compile(r"\bpush\s+(?:dword ptr\s+)?[-+\w]*\[", re.I)
PUSH_REG = re.compile(r"\bpush\s+e(?:ax|bx|cx|dx|si|di)\b", re.I)


def write(path, text):
    with open(path, "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write(text)


def dos_run(work, watcom, lines, expect, timeout=TIMEOUT):
    """Run a batch inside DOSBox-X against one Watcom install."""
    path = ";".join("D:\\" + d for d in BINDIRS)
    head = ["@echo off", "set WATCOM=D:\\", "set PATH=Z:\\;" + path,
            "set INCLUDE=D:\\H", "c:"]
    write(os.path.join(work, "build.bat"),
          "\n".join(head + lines + ["echo done >DONE.TXT", "exit"]) + "\n")
    write(os.path.join(work, "run.conf"),
          "[cpu]\ncycles=max\n[autoexec]\n"
          'mount c "%s"\nmount d "%s"\nc:\ncall build.bat\n' % (work, watcom))
    for name in ["DONE.TXT", "build.out"] + expect:
        p = os.path.join(work, name)
        if os.path.exists(p):
            try:
                os.remove(p)
            except OSError:
                pass
    started = time.time()
    proc = subprocess.Popen([DOSBOX, "-silent", "-conf", "run.conf"], cwd=work)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(os.path.join(work, "DONE.TXT")) or proc.poll() is not None:
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.terminate()
    return started


def classify(path, started):
    """'staged' / 'direct' / 'mixed' / a reason string."""
    if not os.path.exists(path) or os.path.getmtime(path) < started - MTIME_SLACK:
        return "(rejected)"
    with open(path, errors="replace") as fh:
        text = fh.read()
    mem = len(PUSH_MEM.findall(text))
    reg = len(PUSH_REG.findall(text))
    if mem and reg:
        return "mixed(%d mem,%d reg)" % (mem, reg)
    if mem:
        return "direct(%d)" % mem
    if reg:
        return "staged(%d)" % reg
    return "(no pushes)"


def installs():
    for series in SERIES_ROOTS:
        if not os.path.isdir(series):
            continue
        for name in sorted(os.listdir(series)):
            root = os.path.join(series, name)
            if not os.path.isdir(root):
                continue
            if any(os.path.isfile(os.path.join(root, d, "WCC386.EXE"))
                   for d in BINDIRS):
                yield name, root


def sweep_flags(flags):
    work = os.path.join(OUT, "flags")
    os.makedirs(work, exist_ok=True)
    shutil.copyfile(os.path.join(HERE, SRC), os.path.join(work, SRC))
    tags = {"p%02d" % (i + 1): f for i, f in enumerate(flags)}
    lines = []
    for tag, flag in tags.items():
        lines.append("wcc386 %s %s -fo=%s.obj %s >>build.out"
                     % (BASE, flag, tag, SRC))
        lines.append("wdisasm -l=%s.dis -a -e -p %s.obj >>build.out" % (tag, tag))
    # the settled set on its own, as the control
    lines.append("wcc386 %s -fo=p00.obj %s >>build.out" % (BASE, SRC))
    lines.append("wdisasm -l=p00.dis -a -e -p p00.obj >>build.out")
    started = dos_run(work, WATCOM, lines,
                      ["%s.dis" % t for t in list(tags) + ["p00"]])

    print("base: %s   (10.0a)\n" % BASE)
    print("%-10s %s" % ("(none)", classify(os.path.join(work, "p00.dis"), started)))
    hits = []
    for tag, flag in tags.items():
        verdict = classify(os.path.join(work, "%s.dis" % tag), started)
        mark = ""
        if verdict.startswith("staged"):
            mark = "  <== matches FDPS.LE"
            hits.append(flag)
        print("%-10s %s%s" % (flag, verdict, mark))
    print("\nflags producing the FDPS.LE staged form: %s" % (hits or "none"))
    return hits


def sweep_versions():
    print("\nflags: %s\n" % BASE)
    print("%-22s %s" % ("version", "push form"))
    print("-" * 40)
    hits = []
    for version, root in installs():
        work = os.path.join(OUT, "versions", version)
        os.makedirs(work, exist_ok=True)
        shutil.copyfile(os.path.join(HERE, SRC), os.path.join(work, SRC))
        started = dos_run(work, root,
                          ["wcc386 %s -fo=pa.obj %s >>build.out" % (BASE, SRC),
                           "wdisasm -l=pa.dis -a -e -p pa.obj >>build.out"],
                          ["pa.dis"])
        verdict = classify(os.path.join(work, "pa.dis"), started)
        if verdict.startswith("staged"):
            hits.append(version)
            verdict += "  <== matches FDPS.LE"
        print("%-22s %s" % (version, verdict))
    print("\nversions producing the FDPS.LE staged form: %s" % (hits or "none"))
    return hits


def main():
    os.makedirs(OUT, exist_ok=True)
    argv = sys.argv[1:]
    want_flags = "--flags" in argv or not any(
        a in ("--flags", "--versions") for a in argv)
    want_versions = "--versions" in argv or not any(
        a in ("--flags", "--versions") for a in argv)
    picked = [a for a in argv if a.startswith("-") and a not in
              ("--flags", "--versions")]
    if want_flags:
        sweep_flags(picked or CANDIDATES)
    if want_versions:
        sweep_versions()


if __name__ == "__main__":
    main()
