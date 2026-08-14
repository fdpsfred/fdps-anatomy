"""Compile the probe set with every installed Watcom's DOS-hosted compiler.

The original was built under DOS, so every compiler measurement here runs the
DOS executables inside DOSBox-X — nothing is run on the Windows host.  Each
installed version gets one DOSBox session that compiles all five probes and
disassembles them with the same version's wdisasm.

The report is one row per version, one column per code shape that FDPS.LE
pins down.  A version that matches the original has to match every column.

Outputs land in workspace/build_flags/versions/<version>/.

Usage:
    python version_sweep.py                 # every installed version
    python version_sweep.py WATCOM_10.0a    # only the named ones
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
DOSBOX = r"C:\DOSBox-X\dosbox-x.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "versions")

CFLAGS = "-bt=dos4g -mf -zq -4s -fpi -s -od"
# every name is 8.3: DOS tools see mangled short names otherwise
SOURCES = ["probe.c", "probesw.c", "shorts.c", "locinit.c", "scale.c"]
# the wcc386/wdisasm of a given release live in different directories across
# releases, so all candidates go on the PATH
BINDIRS = ["BIN", "BINB", "BINW", "BINP"]
MTIME_SLACK = 4.0

# label -> (source, regex, what FDPS.LE has)
SHAPES = [
    ("scale", "scale.c", r"(shl\s+e\w\w,02H|lea\s+e\w\w,\[e\w\w\*4\])",
     "lea reg,[reg*4]"),
    ("epilogue", "probe.c", r"(leave|mov\s+esp,ebp)", "mov esp,ebp"),
    ("16-bit load", "shorts.c", r"(movsx\s+eax,word ptr|sar\s+eax,10H)",
     "movsx"),
    ("switch scale", "probesw.c", r"(shl\s+eax,02H|lea\s+eax,\[eax\*4\])",
     "lea eax,[eax*4]"),
]

# which segment a datum ends up in, checked by walking the listing rather than
# by a spanning regex (every listing opens with `_TEXT SEGMENT`, so a spanning
# regex matches data in any later segment too)
SEGMENT_SHAPES = [
    ("const home", "locinit.c", r"^file_const\s+DB", "_TEXT"),
]


def installs():
    """Yield (version, root) for every install with a DOS-hosted wcc386."""
    for series in SERIES_ROOTS:
        for name in sorted(os.listdir(series)):
            root = os.path.join(series, name)
            if not os.path.isdir(root):
                continue
            if any(os.path.isfile(os.path.join(root, d, "WCC386.EXE"))
                   for d in BINDIRS):
                yield name, root


def write(path, text):
    with open(path, "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write(text)


def run_version(version, root):
    work = os.path.join(OUT, version)
    os.makedirs(work, exist_ok=True)
    for src in SOURCES:
        shutil.copyfile(os.path.join(HERE, src), os.path.join(work, src))
    path = ";".join("D:\\" + d for d in BINDIRS)
    lines = ["@echo off", "set WATCOM=D:\\", "set PATH=Z:\\;" + path,
             "set INCLUDE=D:\\H", "c:"]
    for src in SOURCES:
        tag = src[:-2]
        lines.append("wcc386 %s -fo=%s.obj %s >>build.out" % (CFLAGS, tag, src))
        lines.append("wdisasm -l=%s.dis -a -e -p %s.obj >>build.out" % (tag, tag))
    lines += ["echo done >DONE.TXT", "exit"]
    write(os.path.join(work, "build.bat"), "\n".join(lines) + "\n")
    write(os.path.join(work, "run.conf"),
          "[cpu]\ncycles=max\n[autoexec]\n"
          'mount c "%s"\nmount d "%s"\nc:\ncall build.bat\n' % (work, root))

    for name in os.listdir(work):
        if name.endswith((".dis", ".obj")) or name in ("DONE.TXT", "build.out"):
            try:
                os.remove(os.path.join(work, name))
            except OSError:
                pass
    started = time.time()
    proc = subprocess.Popen([DOSBOX, "-silent", "-conf", "run.conf"], cwd=work)
    deadline = time.time() + 240
    while time.time() < deadline:
        if os.path.exists(os.path.join(work, "DONE.TXT")) or proc.poll() is not None:
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.terminate()

    listings = {}
    for src in SOURCES:
        dis = os.path.join(work, "%s.dis" % src[:-2])
        if os.path.exists(dis) and os.path.getmtime(dis) >= started - MTIME_SLACK:
            with open(dis, errors="replace") as fh:
                listings[src] = fh.read()
    return listings


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


def main():
    os.makedirs(OUT, exist_ok=True)
    wanted = sys.argv[1:]
    print("flags: %s   (all compiling done by the DOS hosted wcc386 in DOSBox-X)\n"
          % CFLAGS)
    labels = [s[0] for s in SHAPES] + [s[0] for s in SEGMENT_SHAPES]
    header = "%-22s %s" % ("version", "  ".join("%-14s" % t for t in labels))
    print(header)
    print("-" * len(header))
    for version, root in installs():
        if wanted and version not in wanted:
            continue
        listings = run_version(version, root)
        cells = []
        for _label, src, pat, _want in SHAPES:
            text = listings.get(src)
            if text is None:
                cells.append("%-14s" % "(no listing)")
                continue
            hits = sorted(set(m.group(1).strip() for m in re.finditer(pat, text)))
            cells.append("%-14s" % (",".join(hits)[:14] if hits else "-"))
        for _label, src, pat, _want in SEGMENT_SHAPES:
            text = listings.get(src)
            cells.append("%-14s" % (segment_of(text, pat) if text else "(no listing)"))
        print("%-22s %s" % (version, "  ".join(cells)))
    print("\nFDPS.LE has: %s"
          % "  ".join(["%s=%s" % (s[0], s[3]) for s in SHAPES] +
                      ["%s=%s" % (s[0], s[3]) for s in SEGMENT_SHAPES]))


if __name__ == "__main__":
    main()
