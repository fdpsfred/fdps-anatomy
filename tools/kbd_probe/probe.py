"""Build probe.c with the rebuild toolchain and run it N times under DOSBox-X.

Outputs land in workspace/kbd_probe/.  Reuses tools/fdps_build/build_min.py's conf/launch/wait without modifying it.
Usage: python probe.py build | run N | all N
"""
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WS = ROOT / "workspace" / "kbd_probe"
sys.path.insert(0, str(ROOT / "tools" / "fdps_build"))
import build_min as bm  # noqa: E402

SRC = HERE
OUT = WS / "out"
RUN = WS / "run"
LOGS = WS / "logs"
WATCOM = bm.WATCOM_DEFAULT


def build(dosbox):
    WS.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    for n in ("PROBE.EXE", "BUILD.DON", "BUILD.OUT", "HB.TXT", "PROBE.OBJ"):
        p = bm.find_ci(OUT, n)
        if p:
            p.unlink()
    w = bm.DRV_WORK
    lnk = ["system dos4g", r"name %s:\OUT\PROBE.EXE" % w, "option stack=8k",
           r"file %s:\OUT\PROBE.OBJ" % w] + ["library %s" % l for l in bm.CRT_LIBS]
    (WS / "PROBE.LNK").write_text("\n".join(lnk) + "\n", encoding="latin-1")
    bat = "\r\n".join([
        r"echo c > %s:\OUT\HB.TXT" % w,
        r"WCC386 PROBE.C -fo=%s:\OUT\PROBE.OBJ > %s:\OUT\BUILD.OUT" % (w, w),
        r"WLINK @%s:\PROBE.LNK >> %s:\OUT\BUILD.OUT" % (w, w),
        r"echo done > %s:\OUT\BUILD.DON" % w, ""])
    (WS / "BUILD.BAT").write_text(bat, encoding="latin-1")
    bm.write_conf(WS / "build.conf",
                  [(bm.DRV_SRC, SRC), (bm.DRV_WATCOM, WATCOM), (w, WS)], None,
                  ["set WATCOM=%s:\\" % bm.DRV_WATCOM,
                   "set PATH=Z:\;" + ";".join("%s:\%s" % (bm.DRV_WATCOM, d)
                                              for d in bm.BINDIRS),
                   "set INCLUDE=%s:\H" % bm.DRV_WATCOM,
                   "set WCC386=" + bm.CFLAGS, "%s:" % bm.DRV_SRC,
                   r"%s:\BUILD.BAT" % w],
                  logfile=WS / "build.log")
    proc, fp = bm.launch(dosbox, WS / "build.conf", WS / "build_stdio.log")
    mode, hb, secs = bm.wait(proc, OUT, "build.don", 300)
    fp.close()
    print(bm.find_ci(OUT, "build.out").read_text(encoding="latin-1"))
    return bm.find_ci(OUT, "probe.exe") is not None


def run(dosbox, n):
    LOGS.mkdir(parents=True, exist_ok=True)
    RUN.mkdir(parents=True, exist_ok=True)
    shutil.copy2(OUT / "PROBE.EXE", RUN / "PROBE.EXE")
    shutil.copy2(WATCOM / "BIN" / "DOS4GW.EXE", RUN / "DOS4GW.EXE")
    for i in range(n):
        for name in ("PROBE.OUT", "RUN.DON", "HB.TXT", "dosbox.log", "stdio.log"):
            p = bm.find_ci(RUN, name)
            if p:
                p.unlink()
        bm.write_conf(RUN / "run.conf", [(bm.DRV_SRC, RUN), (bm.DRV_WATCOM, WATCOM)],
                      None, ["set PATH=Z:\;%s:\;%s:\BIN" % (bm.DRV_SRC, bm.DRV_WATCOM),
                             "%s:" % bm.DRV_SRC, "PROBE.EXE"],
                      logfile=RUN / "dosbox.log", memsize=64)
        proc, fp = bm.launch(dosbox, RUN / "run.conf", RUN / "stdio.log")
        mode, hb, secs = bm.wait(proc, RUN, "run.don", 120)
        fp.close()
        try:
            proc.wait(timeout=30)
        except Exception:
            proc.kill()
        res = bm.find_ci(RUN, "probe.out")
        txt = res.read_text(encoding="latin-1") if res else ""
        (LOGS / ("run%03d.txt" % i)).write_text(txt, encoding="latin-1")
        bad = [l for l in txt.splitlines() if " kb=0 " in l]
        print("run %d: %s, %d lines, %d with kb=0" % (i, mode, len(txt.splitlines()), len(bad)))


def main():
    dosbox = bm.resolve_dosbox()
    cmd = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    if cmd in ("build", "all") and not build(dosbox):
        print("build failed")
        return 1
    if cmd in ("run", "all"):
        run(dosbox, n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
