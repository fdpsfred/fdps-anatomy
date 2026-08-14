"""Compile probe.c with one fixed flag set across every installed Watcom version.

Some code shapes in FDPS.LE are not reproducible by every wcc386 release, so the
flag set and the compiler version have to be pinned together.  This runs the
settled flag set against every installed version and prints the shapes that
could discriminate between releases; the switch-scaling row is the one FDPS.LE
does differently from all of them.

Outputs land in workspace/build_flags/versions/<version>.{obj,dis}.
"""

import os
import re
import shutil
import subprocess
import sys

SERIES_ROOTS = [
    r"C:\Users\fdpsf\Documents\WATCOM_9.5_series",
    r"C:\Users\fdpsf\Documents\WATCOM_10_series",
]
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "versions")

FLAGS = ["-bt=dos4g", "-mf", "-zq", "-4s", "-fpi", "-s", "-od"]

# code shapes read off FDPS.LE; each is a line-level regex over the wdisasm
# listing.  "want" records what the original binary does.
SHAPES = [
    ("switch scale", r"^\s+(shl\s+eax,02H|lea\s+eax,\[eax\*4\])",
     "lea eax,[eax*4]  (FDPS: 8d 04 85 00000000)"),
    ("switch jump", r"jmp\s+dword ptr cs:", "jmp dword ptr cs:  (FDPS: 2e ff a0 ...)"),
    ("frame", r"^five_args:\s+(push\s+ebx|sub\s+esp|push\s+ebp)",
     "push ebx (FDPS: 53 56 57 55 89 e5)"),
    ("epilogue", r"^\s+(leave|mov\s+esp,ebp)",
     "mov esp,ebp (FDPS: 0 LEAVE, 333 MOV ESP,EBP in the game region)"),
]


def versions():
    for series in SERIES_ROOTS:
        for name in sorted(os.listdir(series)):
            d = os.path.join(series, name)
            if os.path.isdir(d) and os.path.isfile(os.path.join(d, "BINNT", "WCC386.EXE")):
                yield name, d


def build(name, root):
    env = dict(os.environ)
    env["WATCOM"] = root
    env["INCLUDE"] = os.path.join(root, "H")
    env["PATH"] = os.path.join(root, "BINNT") + os.pathsep + env["PATH"]
    env.pop("WCC386", None)
    obj = "%s.obj" % name
    r = subprocess.run([os.path.join(root, "BINNT", "WCC386.EXE")] + FLAGS +
                       ["-fo=" + obj, "probe.c"],
                       cwd=OUT, env=env, capture_output=True, text=True)
    if r.returncode != 0:
        return None, (r.stdout or r.stderr).strip()
    subprocess.run([os.path.join(root, "BINNT", "WDISASM.EXE"),
                    "-l=%s.dis" % name, "-a", "-e", "-p", obj],
                   cwd=OUT, env=env, capture_output=True, text=True)
    with open(os.path.join(OUT, "%s.dis" % name), "r", errors="replace") as fh:
        return fh.read(), None


def banner(root):
    r = subprocess.run([os.path.join(root, "BINNT", "WCC386.EXE")],
                       capture_output=True, text=True)
    m = re.search(r"Version\s+(\S+)", r.stdout or "")
    return m.group(1) if m else "?"


def main():
    os.makedirs(OUT, exist_ok=True)
    shutil.copyfile(os.path.join(HERE, "probe.c"), os.path.join(OUT, "probe.c"))
    print("flags: %s\n" % " ".join(FLAGS))
    for name, root in versions():
        text, err = build(name, root)
        if text is None:
            print("%-16s FAILED %s" % (name, err))
            continue
        cols = []
        for label, pat, _want in SHAPES:
            hits = sorted(set(m.group(0).strip()
                              for m in re.finditer(pat, text, re.M)))
            cols.append("%s=%s" % (label, ",".join(hits) if hits else "-"))
        print("%-16s v%-8s %s" % (name, banner(root), " | ".join(cols)))
    print("\nFDPS.LE wants:")
    for label, _pat, want in SHAPES:
        print("  %-14s %s" % (label, want))


if __name__ == "__main__":
    main()
