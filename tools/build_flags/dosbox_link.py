"""Compile and link the link probes inside DOSBox-X with Watcom 10.0a.

Everything runs the DOS hosted tools inside DOSBox-X, the way the original was
built; nothing runs on the Windows host.  (The NT hosted WLINK.EXE that ships
with 10.0a hangs on this machine anyway.)

Produces workspace/build_flags/link/<tag>.exe plus a per-variant .map, then
reports the container facts that the flag derivation depends on.
"""

import os
import struct
import subprocess
import sys
import time

WATCOM = r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a"
DOSBOX = r"C:\DOSBox-X\dosbox-x.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "link")
GAME = os.path.join(REPO, "fdps_game_files")

MAIN_C = """\
#include <stdio.h>
#include <string.h>
#include <math.h>

char big[40000];

int main(int argc, char **argv)
{
    double d = 0.0;
    int i;

    for (i = 0; i < argc; ++i)
        d += sqrt((double)strlen(argv[i]) * 1.5);
    memset(big, (int)d, sizeof(big));
    printf("%d %f\\n", (int)d, d);
    return big[0];
}
"""

# tag -> (extra compile flags, stack size)
VARIANTS = {
    "fpi87_8k": (["-fpi87"], "8k"),
    "fpi_8k":   (["-fpi"], "8k"),
    "fpi87_4k": (["-fpi87"], "4k"),
    # tags stay within 8.3: wlink under DOS cannot open a longer .lnk name
    "fpi87_16": (["-fpi87"], "16k"),
}
# DOSBox writes through a FAT-style layer whose timestamps have 2-second
# granularity and round down, so a freshly written file can look slightly older
# than the moment the run started.
MTIME_SLACK = 4.0
BASE_CFLAGS = ["-bt=dos4g", "-mf", "-zq", "-4s", "-s", "-od"]
EMU_SIG = bytes.fromhex("8bec8b75388e5d3c668b4d04668b5506")


def write_inputs():
    with open(os.path.join(OUT, "main.c"), "w", encoding="latin-1") as fh:
        fh.write(MAIN_C)
    for tag, (_cflags, stack) in VARIANTS.items():
        with open(os.path.join(OUT, "%s.lnk" % tag), "w",
                  encoding="latin-1", newline="\r\n") as fh:
            fh.write("system dos4g\n")
            fh.write("name %s.exe\n" % tag)
            fh.write("option stack=%s\n" % stack)
            fh.write("option map=%s.map\n" % tag)
            fh.write("file %s.obj\n" % tag)
            for lib in ("clib3s.lib", "math387s.lib", "emu387.lib"):
                fh.write("library %s\n" % lib)
    with open(os.path.join(OUT, "build.bat"), "w",
              encoding="latin-1", newline="\r\n") as fh:
        fh.write("@echo off\r\n")
        fh.write("set WATCOM=D:\\\r\n")
        fh.write("set PATH=Z:\\;D:\\BIN;D:\\BINB;D:\\BINW\r\n")
        fh.write("set INCLUDE=D:\\H\r\n")
        fh.write("c:\r\n")
        for tag, (cflags, _stack) in VARIANTS.items():
            fh.write("wcc386 %s %s -fo=%s.obj main.c >>build.out\r\n"
                     % (" ".join(BASE_CFLAGS), " ".join(cflags), tag))
        for tag in VARIANTS:
            fh.write("wlink @%s.lnk >>build.out\r\n" % tag)
        fh.write("echo done >DONE.TXT\r\n")
        fh.write("exit\r\n")
    conf = os.path.join(OUT, "link.conf")
    with open(conf, "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write("[sdl]\nautolock=false\n")
        fh.write("[cpu]\ncputype=pentium_mmx\ncycles=max\n")
        fh.write("[autoexec]\n")
        fh.write('mount c "%s"\n' % OUT)
        fh.write('mount d "%s"\n' % WATCOM)
        fh.write("c:\n")
        fh.write("call build.bat\n")
    return conf


def run_dosbox(conf, timeout=300):
    # Anything report() reads has to be cleared first, so a variant that fails
    # to link cannot be reported from the previous run's EXE — the
    # emulator-inclusion evidence would then silently come from stale output.
    # Deletion can fail while a scanner still holds a freshly written EXE, so
    # report() also checks mtimes against the start time returned here.
    stale = ["DONE.TXT", "build.out"] + ["%s.exe" % t for t in VARIANTS] + \
            ["%s.map" % t for t in VARIANTS]
    for name in stale:
        p = os.path.join(OUT, name)
        if os.path.exists(p):
            try:
                os.remove(p)
            except OSError as exc:
                print("[warn] could not remove %s (%s)" % (name, exc))
    started = time.time()
    proc = subprocess.Popen([DOSBOX, "-silent", "-conf", conf], cwd=OUT)
    done = os.path.join(OUT, "DONE.TXT")
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(done):
            break
        if proc.poll() is not None:
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.terminate()
    ok = os.path.exists(done)
    print("[dosbox] %s" % ("DONE" if ok else "TIMEOUT/EXIT"))
    return started


def le_base(data):
    return struct.unpack_from("<I", data, 0x3C)[0] if data[:2] == b"MZ" else 0


def report(started=0.0):
    with open(os.path.join(GAME, "FDPS.EXE"), "rb") as fh:
        gdata = fh.read()
    gbase = le_base(gdata)
    print("\nFDPS.EXE stub = %d bytes" % gbase)
    for tag in VARIANTS:
        path = os.path.join(OUT, "%s.exe" % tag)
        if not os.path.exists(path):
            print("%-10s (not built)" % tag)
            continue
        if os.path.getmtime(path) < started - MTIME_SLACK:
            print("%-10s (stale from an earlier run — not reported)" % tag)
            continue
        with open(path, "rb") as fh:
            data = fh.read()
        base = le_base(data)
        esp_obj, esp = struct.unpack_from("<II", data, base + 0x20)
        nobj = struct.unpack_from("<I", data, base + 0x44)[0]
        obj_off = base + struct.unpack_from("<I", data, base + 0x40)[0]
        objs = [struct.unpack_from("<IIIII", data, obj_off + i * 24)
                for i in range(nobj)]
        stub_diff = ("len %d vs %d" % (base, gbase) if base != gbase else
                     "%d differing bytes" % sum(1 for a, b in
                                                zip(data[:base], gdata[:gbase])
                                                if a != b))
        print("%-10s stub=%-6d objs=%d esp=obj%d+0x%x  emu=%-3s  stub_vs_game: %s"
              % (tag, base, nobj, esp_obj, esp,
                 "yes" if EMU_SIG in data else "no", stub_diff))
        for i, (vsize, addr, flags, _pi, pages) in enumerate(objs):
            print("            obj %d base=0x%06x vsize=0x%-6x flags=0x%04x pages=%d"
                  % (i + 1, addr, vsize, flags, pages))


def main():
    os.makedirs(OUT, exist_ok=True)
    conf = write_inputs()
    started = 0.0
    if "--report-only" not in sys.argv:
        started = run_dosbox(conf)
    report(started)


if __name__ == "__main__":
    main()
