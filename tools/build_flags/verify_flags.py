"""Re-check the derived wcc386 flag set against the code shapes seen in FDPS.LE.

Compiles the probe set with the derived flag set and asserts, one signature at
a time, that the generated code has the shape the original has.  Every row names
the FDPS.LE observation it is standing in for, so a failing row means either the
flag set is wrong or the observation was misread.

Compilation runs the DOS hosted wcc386 inside DOSBox-X, the same way the
original was built; nothing is compiled on the Windows host.

Run after any change to the derived flag set:
    python verify_flags.py
Exit code is 0 only when every signature holds.
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
OUT = os.path.join(REPO, "workspace", "build_flags", "verify")

# -ot before -od is not redundant: wcc386 reads options left to right, so -ot
# sets the favour-time preference that picks `lea` for index scaling and -od
# then switches the optimiser off without clearing that preference.  Swapping
# them, or dropping either one, changes the generated code.
CFLAGS = "-bt=dos4g -mf -4s -fpi -s -ot -od -zq"
SOURCES = ["probe.c", "probesw.c", "shorts.c", "locinit.c", "scale.c"]
BINDIRS = ["BIN", "BINB", "BINW"]
MTIME_SLACK = 4.0

# (label, source, regex that must match, regex that must NOT match, FDPS evidence)
CHECKS = [
    ("prologue saves all four registers", "probe.c",
     r"five_args:\s+push\s+ebx\n\s+push\s+esi\n\s+push\s+edi\n\s+push\s+ebp\n\s+mov\s+ebp,esp",
     None, "53 56 57 55 89 e5 at every game function entry"),
    ("first stack argument at [ebp+14h]", "probe.c",
     r"\+14H\[ebp\]", None,
     "0002f746 cmp dword ptr [ebp+0x34],7 — the 9th stack slot, counting from +0x14"),
    ("caller pops the arguments", "probe.c",
     r"call\s+near ptr sink\n\s+add\s+esp,00000014H", r"\bret\s+0",
     "2464 of 2937 call sites targeting game functions are followed by "
     "ADD ESP,n; the game region has zero RET imm"),
    ("no stack probe", "probe.c", None, r"call\s+near ptr __CHK",
     "no game function calls __CHK at 0x4361a; all 17 callers are library code"),
    ("epilogue avoids LEAVE", "probe.c",
     r"mov\s+esp,ebp\n\s+pop\s+ebp\n\s+pop\s+edi\n\s+pop\s+esi\n\s+pop\s+ebx\n\s+ret",
     r"^\s+leave", "game region has 333 MOV ESP,EBP and 0 LEAVE"),
    ("16-bit loads keep MOVSX", "shorts.c",
     r"movsx\s+eax,word ptr", r"sar\s+eax,10H",
     "game region has 208 MOVSX word and 0 SAR reg,0x10"),
    ("switch table sits inside _TEXT after the prologue", "probesw.c",
     r"sub\s+esp,\S+\n\s+jmp\s+short L2\n(?:.*\n)?L1\s+DD",
     None, "0x2f6dc jmp short 0x2f700 over the table at 0x2f6e0"),
    ("switch dispatches through cs:", "probesw.c",
     r"jmp\s+dword ptr cs:L\d+\[eax\]", None,
     "0002f75a 2e ff a0 e0 f6 02 00"),
    # segment membership is checked by SEGMENT_CHECKS below, not by regex: a
    # regex spanning from `_TEXT SEGMENT` to a symbol matches whatever comes
    # after it, because _TEXT is the first segment in every listing
    ("local array init copies without reloading ES", "locinit.c",
     r"lea\s+edi,\S+\n\s+mov\s+esi,offset L1", r"mov\s+es,ax",
     "00014abc lea edi,[ebp-0x48] / mov esi,0x146e0 / movsd, no ES load"),
    ("x87 inline with __CHP before FISTP", "probe.c",
     r"call\s+near ptr __CHP\n\s+fistp", None,
     "__CHP at 0x43657 is called from game FP code ahead of FISTP"),
    ("index scaling uses LEA", "scale.c",
     r"lea\s+e\w\w,\+0H\[e\w\w\*4\]", r"shl\s+e\w\w,02H",
     "304 LEA reg,[reg*N+0] in the game region, 0 SHL reg,2 for addressing"),
    ("locals still round-trip through the stack", "probe.c",
     r"mov\s+dword ptr -4H\[ebp\],eax\n\s+mov\s+eax,dword ptr -4H\[ebp\]", None,
     "0002f723 mov [ebp-4],eax then 0002f732 mov eax,[ebp-4]"),
]
# Deliberately not a check: the argument push form.  The original uses BOTH
# MOV EAX,slot / PUSH EAX and PUSH slot, so no single flag reproduces it and
# there is nothing here for a pass/fail row to assert.  push_form.py measures
# it; rebuild_info/build_flags.md records where that got to.

# (label, source, line pattern identifying the datum, expected segment, evidence)
# Data items are matched by their bytes, not by label name: wdisasm numbers
# compiler-generated labels differently under different flags.
SEGMENT_CHECKS = [
    ("named const objects land in _TEXT", "locinit.c",
     r"^file_const\s+DB", "_TEXT",
     "const tables at 0x146d2 / 0x2b27a / 0x31037 sit between game functions"),
    ("local array init images land in _TEXT", "locinit.c",
     r"^\w+\s+DB\s+32H,00H,00H,00H", "_TEXT",
     "0x146e0 is read by MOVSD from inside object 1"),
    ("string literals stay in CONST", "probe.c",
     r"^\w+\s+DB\s+46H,69H,67H,68H", "CONST",
     '"Fight.vfs" at 0x6001c is in object 2 (DGROUP)'),
]


def segment_of(text, pattern):
    """Return the name of the segment holding the line matching `pattern`.

    wdisasm emits `<name> SEGMENT ... <name> ENDS` blocks, so the owning
    segment is whichever block the line falls in.  Walked line by line rather
    than matched by a spanning regex, because every listing opens with
    `_TEXT SEGMENT` and such a regex would match data in any later segment.
    """
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


def write(path, text):
    with open(path, "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write(text)


def compile_all():
    os.makedirs(OUT, exist_ok=True)
    for src in SOURCES:
        shutil.copyfile(os.path.join(HERE, src), os.path.join(OUT, src))
    path = ";".join("D:\\" + d for d in BINDIRS)
    lines = ["@echo off", "set WATCOM=D:\\", "set PATH=Z:\\;" + path,
             "set INCLUDE=D:\\H", "c:"]
    for src in SOURCES:
        tag = src[:-2]
        lines.append("wcc386 %s -fo=%s.obj %s >>build.out" % (CFLAGS, tag, src))
        lines.append("wdisasm -l=%s.dis -a -e -p %s.obj >>build.out" % (tag, tag))
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
    deadline = time.time() + 240
    while time.time() < deadline:
        if os.path.exists(os.path.join(OUT, "DONE.TXT")) or proc.poll() is not None:
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.terminate()

    listings = {}
    for src in SOURCES:
        dis = os.path.join(OUT, "%s.dis" % src[:-2])
        if not os.path.exists(dis) or os.path.getmtime(dis) < started - MTIME_SLACK:
            raise SystemExit("no fresh listing for %s; see %s"
                             % (src, os.path.join(OUT, "build.out")))
        with open(dis, errors="replace") as fh:
            listings[src] = fh.read()
    return listings


def main():
    global CFLAGS
    if len(sys.argv) > 1:
        CFLAGS = " ".join(sys.argv[1:])
    print("flags: %s   (DOS hosted wcc386, Watcom 10.0a, in DOSBox-X)\n" % CFLAGS)
    listings = compile_all()
    failed = 0
    for label, src, want, reject, evidence in CHECKS:
        text = listings[src]
        ok = True
        if want and not re.search(want, text, re.M):
            ok = False
        if reject and re.search(reject, text, re.M):
            ok = False
        print("[%s] %-45s  <- %s" % ("PASS" if ok else "FAIL", label, evidence))
        if not ok:
            failed += 1
    for label, src, pattern, want_seg, evidence in SEGMENT_CHECKS:
        got = segment_of(listings[src], pattern)
        ok = got == want_seg
        print("[%s] %-45s  <- %s  (found in %s)"
              % ("PASS" if ok else "FAIL", label, evidence, got))
        if not ok:
            failed += 1
    total = len(CHECKS) + len(SEGMENT_CHECKS)
    print("\n%d/%d signatures reproduced" % (total - failed, total))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
