"""Measure what `wlink system dos4g` does when a directive is left out.

Two directives in the reconstructed link file need to be told apart from the
linker's own defaults, otherwise the rebuild silently gets a different image:

  * `option stack=<n>` — if the default already were 8K the directive would be
    redundant, and FDPS.LE would carry no evidence that it was written.
  * `name <file>` — the LE resident-name entry is `fde`, which is either the
    output file name or the first object file's name; which one wlink uses
    decides what that entry proves.

Self-contained: compiles its own object with the derived flag set, then links
three ways inside DOSBox-X.
"""

import os
import struct
import subprocess
import time

WATCOM = r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a"
DOSBOX = r"C:\DOSBox-X\dosbox-x.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "defaults")

CFLAGS = ["-bt=dos4g", "-mf", "-4s", "-fpi", "-s", "-od", "-zq"]
SRC = "mainprb.c"
MAIN_C = """\
#include <stdio.h>

int main(void)
{
    printf("probe\\n");
    return 0;
}
"""

# tag -> directive lines after `system dos4g`
LINKS = {
    # no `option stack`: exposes the default stack size
    "defstack": ["name defstack.exe", "file %s.obj" % SRC[:-2]],
    # explicit stack, for the arithmetic cross-check
    "stack8k": ["name stack8k.exe", "option stack=8k", "file %s.obj" % SRC[:-2]],
    # no `name`: exposes where the resident-name entry comes from
    "noname": ["file %s.obj" % SRC[:-2]],
}
LIBS = ("clib3s.lib", "math387s.lib", "emu387.lib")


def w(name, text):
    with open(os.path.join(OUT, name), "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write(text)


def prepare():
    os.makedirs(OUT, exist_ok=True)
    w(SRC, MAIN_C)
    env = dict(os.environ)
    env["WATCOM"] = WATCOM
    env["INCLUDE"] = os.path.join(WATCOM, "H")
    env.pop("WCC386", None)
    r = subprocess.run([os.path.join(WATCOM, "BINNT", "WCC386.EXE")] + CFLAGS +
                       ["-fo=%s.obj" % SRC[:-2], SRC],
                       cwd=OUT, env=env, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("compile failed: %s" % (r.stdout or r.stderr))
    for tag, lines in LINKS.items():
        w("%s.lnk" % tag, "system dos4g\n" + "\n".join(lines) + "\n" +
          "option map=%s.map\n" % tag +
          "".join("library %s\n" % lib for lib in LIBS))
    w("build.bat", "@echo off\nset WATCOM=D:\\\nset PATH=Z:\\;D:\\BIN;D:\\BINB\nc:\n" +
      "".join("wlink @%s.lnk >>build.out\n" % tag for tag in LINKS) +
      "echo done >DONE.TXT\nexit\n")
    w("link.conf", "[cpu]\ncycles=max\n[autoexec]\n"
      'mount c "%s"\nmount d "%s"\nc:\ncall build.bat\n' % (OUT, WATCOM))


def clean():
    """Remove every artifact the report reads, so a failed run cannot be read
    as a successful one.  Deletion is best effort — a scanner can still hold a
    freshly written EXE — so the report also checks mtimes."""
    for name in os.listdir(OUT):
        if name.endswith((".exe", ".map")) or name in ("DONE.TXT", "build.out"):
            try:
                os.remove(os.path.join(OUT, name))
            except OSError as exc:
                print("[warn] could not remove %s (%s)" % (name, exc))


def run_dosbox(timeout=240):
    started = time.time()
    proc = subprocess.Popen([DOSBOX, "-silent", "-conf", "link.conf"], cwd=OUT)
    done = os.path.join(OUT, "DONE.TXT")
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(done) or proc.poll() is not None:
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.terminate()
    if not os.path.exists(done):
        print("DOSBox did not finish; see build.out")
    return started


def describe(path):
    with open(path, "rb") as fh:
        data = fh.read()
    base = struct.unpack_from("<I", data, 0x3C)[0] if data[:2] == b"MZ" else 0
    esp_obj, esp = struct.unpack_from("<II", data, base + 0x20)
    rn = base + struct.unpack_from("<I", data, base + 0x58)[0]
    name = data[rn + 1:rn + 1 + data[rn]].decode("latin-1")
    return esp_obj, esp, name


def main():
    prepare()
    clean()
    started = run_dosbox()
    for tag in LINKS:
        # `noname` has no `name` directive, so wlink names the output after the
        # first object file
        exe = os.path.join(OUT, "%s.exe" % (SRC[:-2] if tag == "noname" else tag))
        if not os.path.exists(exe):
            print("%-9s (not produced)" % tag)
            continue
        # DOSBox timestamps round down to two seconds, hence the slack
        if os.path.getmtime(exe) < started - 4.0:
            print("%-9s (stale from an earlier run — not reported)" % tag)
            continue
        esp_obj, esp, name = describe(exe)
        stack = ""
        mappath = os.path.join(OUT, "%s.map" % tag)
        if os.path.exists(mappath):
            with open(mappath, errors="replace") as fh:
                for line in fh:
                    if "Stack size" in line:
                        stack = line.strip()
        print("%-9s output=%-14s resident name=%-10s esp=obj%d+0x%x  %s"
              % (tag, os.path.basename(exe), name, esp_obj, esp, stack))


if __name__ == "__main__":
    main()
