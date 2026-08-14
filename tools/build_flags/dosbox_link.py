"""Compile and link the link probes inside DOSBox-X with Watcom 10.0a.

The NT hosted WLINK.EXE that ships with 10.0a hangs on this machine, and the
project builds under DOS anyway, so the linking half of the flag forensics runs
in DOSBox-X (silent, fully automated).  Compilation still uses the NT hosted
wcc386 because its codegen was verified identical.

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
    "fpi87_16k": (["-fpi87"], "16k"),
}
BASE_CFLAGS = ["-bt=dos4g", "-ms", "-zq", "-5s", "-s", "-od"]
EMU_SIG = bytes.fromhex("8bec8b75388e5d3c668b4d04668b5506")


def compile_objs():
    env = dict(os.environ)
    env["WATCOM"] = WATCOM
    env["INCLUDE"] = os.path.join(WATCOM, "H")
    env.pop("WCC386", None)
    for tag, (cflags, _stack) in VARIANTS.items():
        r = subprocess.run(
            [os.path.join(WATCOM, "BINNT", "WCC386.EXE")] + BASE_CFLAGS +
            cflags + ["-fo=%s.obj" % tag, "main.c"],
            cwd=OUT, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("compile %s failed: %s" % (tag, r.stdout or r.stderr))
        print("[cc]   %s %s" % (tag, " ".join(cflags)))


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
        fh.write("set PATH=Z:\\;D:\\BIN;D:\\BINB\r\n")
        fh.write("set INCLUDE=D:\\H\r\n")
        fh.write("c:\r\n")
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
    for stale in ("DONE.TXT", "build.out"):
        p = os.path.join(OUT, stale)
        if os.path.exists(p):
            os.remove(p)
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
    return ok


def le_base(data):
    return struct.unpack_from("<I", data, 0x3C)[0] if data[:2] == b"MZ" else 0


def report():
    with open(os.path.join(GAME, "FDPS.EXE"), "rb") as fh:
        gdata = fh.read()
    gbase = le_base(gdata)
    print("\nFDPS.EXE stub = %d bytes" % gbase)
    for tag in VARIANTS:
        path = os.path.join(OUT, "%s.exe" % tag)
        if not os.path.exists(path):
            print("%-10s (not built)" % tag)
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
    compile_objs()
    if "--report-only" not in sys.argv:
        run_dosbox(conf)
    report()


if __name__ == "__main__":
    main()
