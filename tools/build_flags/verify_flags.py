"""Re-check the derived wcc386 flag set against the code shapes seen in FDPS.LE.

Compiles probe.c / probe_switch.c / shorts.c / localinit.c with the derived flag
set and asserts, one signature at a time, that the generated code has the shape
the original has.  Every row names the FDPS.LE observation it is standing in
for, so a failing row means either the flag set is wrong or the observation was
misread.

Run after any change to the derived flag set:
    python verify_flags.py
Exit code is 0 only when every signature holds.
"""

import os
import re
import shutil
import subprocess
import sys

WATCOM = r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "verify")

FLAGS = ["-bt=dos4g", "-mf", "-4s", "-fpi", "-s", "-od", "-zq"]
SOURCES = ["probe.c", "probe_switch.c", "shorts.c", "localinit.c"]

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
    ("switch table sits inside _TEXT after the prologue", "probe_switch.c",
     r"sub\s+esp,\S+\n\s+jmp\s+short L2\n(?:.*\n)?L1\s+DD",
     None, "0x2f6dc jmp short 0x2f700 over the table at 0x2f6e0"),
    ("switch dispatches through cs:", "probe_switch.c",
     r"jmp\s+dword ptr cs:L\d+\[eax\]", None,
     "0002f75a 2e ff a0 e0 f6 02 00"),
    ("const objects land in _TEXT", "localinit.c",
     r"_TEXT\s+SEGMENT(?:.|\n)*?file_const\s+DB", None,
     "const tables at 0x146d2 / 0x2b27a / 0x31037 sit between game functions"),
    ("string literals stay in CONST", "probe.c",
     r"CONST\s+SEGMENT(?:.|\n)*?DB\s+46H,69H,67H,68H", None,
     '"Fight.vfs" at 0x6001c is in object 2'),
    ("local array init copies without reloading ES", "localinit.c",
     r"lea\s+edi,\S+\n\s+mov\s+esi,offset L1", r"mov\s+es,ax",
     "00014abc lea edi,[ebp-0x48] / mov esi,0x146e0 / movsd, no ES load"),
    ("x87 inline with __CHP before FISTP", "probe.c",
     r"call\s+near ptr __CHP\n\s+fistp", None,
     "__CHP at 0x43657 is called from game FP code ahead of FISTP"),
]


def compile_all():
    os.makedirs(OUT, exist_ok=True)
    listings = {}
    env = dict(os.environ)
    env["WATCOM"] = WATCOM
    env["INCLUDE"] = os.path.join(WATCOM, "H")
    env.pop("WCC386", None)
    for src in SOURCES:
        shutil.copyfile(os.path.join(HERE, src) if os.path.exists(os.path.join(HERE, src))
                        else os.path.join(REPO, "workspace", "build_flags", "probe", src),
                        os.path.join(OUT, src))
        tag = os.path.splitext(src)[0]
        r = subprocess.run([os.path.join(WATCOM, "BINNT", "WCC386.EXE")] + FLAGS +
                           ["-fo=%s.obj" % tag, src],
                           cwd=OUT, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("compile %s failed: %s" % (src, r.stdout or r.stderr))
        subprocess.run([os.path.join(WATCOM, "BINNT", "WDISASM.EXE"),
                        "-l=%s.dis" % tag, "-a", "-e", "-p", "%s.obj" % tag],
                       cwd=OUT, env=env, capture_output=True, text=True)
        with open(os.path.join(OUT, "%s.dis" % tag), errors="replace") as fh:
            listings[src] = fh.read()
    return listings


def main():
    print("flags: %s\n" % " ".join(FLAGS))
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
    print("\n%d/%d signatures reproduced" % (len(CHECKS) - failed, len(CHECKS)))
    print("known residual: the one game-side switch scales its index with "
          "`lea eax,[eax*4]` where every installed Watcom emits `shl eax,2`; "
          "functionally identical, no flag reproduces it.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
