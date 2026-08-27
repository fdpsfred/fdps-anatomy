"""build_min.py -- build and run the minimal DOS/4G smoke program.

Proves the rebuild toolchain works end to end before any real emit work
depends on it: wcc386 accepts the flag set pinned in
rebuild_info/build_flags.md, wlink produces a DOS/4G LE image, the DOS/4GW
extender loads it inside DOSBox-X, the Watcom CRT starts, and the mounted disc
image is readable from the guest. It is also the automation skeleton every
later build stage reuses -- preflight, generated conf/batch, multi-signal
completion, log-based fault scan.

Subcommands:
    build   compile + link smoke/smoke.c -> workspace/fdps_build/out/SMOKE.EXE
    run     stage that EXE into a run dir and execute it under DOSBox-X
    all     build then run (default)

Everything runs unattended: DOSBox-X is launched with -silent, the guest work
is driven from a generated autoexec, and completion is decided by three
independent signals rather than a fixed sleep --

    1. the guest writes a done marker (BUILD.DON / RUN.DON) as its last step
    2. the DOSBox-X process exits on its own
    3. the guest's heartbeat file (HB.TXT) stops changing

-- so a hang is reported as a hang, at the step it hung on, instead of being
padded out to a timeout. The overall timeout stays only as a backstop.

DOSBox-X exits 0 whether or not IMGMOUNT, the compiler, or the linker
succeeded, so nothing here trusts its exit status: the build is judged by
SMOKE.EXE existing with no undefined symbols in the linker output, and the run
by what SMOKE.EXE itself wrote into RESULT.TXT.

Usage: python tools/fdps_build/build_min.py [build|run|all] [--timeout N]
Exit : 0 when every stage it ran passed, 1 otherwise.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SMOKE_DIR = Path(__file__).resolve().parent / "smoke"
WORK = ROOT / "workspace" / "fdps_build"
OUT = WORK / "out"
OBJ = OUT / "obj"
RUN = WORK / "run"

DOSBOX_DEFAULT = Path(r"C:\DOSBox-X\dosbox-x.exe")

# The complete 10.0a tree. The sibling WATCOM_10.0a install is a damaged copy
# whose clib3s.lib is missing the stk386 module (rebuild_info/pitfalls.md), and
# nothing diagnoses that at link time -- it surfaces as undefined __CHK/__STK.
# Preflight checks for the module rather than trusting the path.
WATCOM_DEFAULT = (Path.home() / "Documents" / "WATCOM_10_series"
                  / "WATCOM_10.0a_infobase")
DISC_DEFAULT = Path(r"D:\Game\Flame Dragon\fdps_image\FDPS_DISC_1.cue")

# rebuild_info/build_flags.md. -ot before -od is load-bearing: wcc386 reads
# options left to right, so -ot sets the speed preference and -od then
# disables the optimizer without clearing it. Swapping them changes codegen.
CFLAGS = "-bt=dos4g -mf -4s -fpi -s -ot -od"

# Guest drive letters. E: is the disc because the installer's Disk.No names
# "CDROM at e:" and later playtest stages read that prefix
# (program_info/cd_audio.md).
DRV_SRC, DRV_WATCOM, DRV_DISC, DRV_WORK = "C", "D", "E", "F"

CRT_LIBS = [r"D:\LIB386\DOS\CLIB3S.LIB",
            r"D:\LIB386\MATH387S.LIB",
            r"D:\LIB386\DOS\EMU387.LIB"]

# Which bin directory holds the DOS-hosted tools moved between releases (BIN in
# 9.5, BINB in the 10.0 family, BINW from 10.5), and wcc386 and wlink do not
# even live in the same one under 10.0a. All three go on the guest PATH and the
# tools are invoked by bare name. BINNT is deliberately absent: its tools
# produce identical code, but the original was built by the DOS-hosted ones.
BINDIRS = ["BIN", "BINB", "BINW"]

FAULT_RX = re.compile(r"illegal descriptor|general protection|invalid opcode|"
                      r"privileged instruction|paging fault|cpu exception|fatal",
                      re.I)


# ---------------------------------------------------------------- preflight

def fail(msg):
    raise SystemExit("preflight: " + msg)


def resolve_dosbox():
    hit = shutil.which("dosbox-x") or shutil.which("dosbox-x.exe")
    if hit:
        return Path(hit)
    if DOSBOX_DEFAULT.is_file():
        return DOSBOX_DEFAULT
    fail("DOSBox-X not found on PATH nor at %s" % DOSBOX_DEFAULT)


def cue_bin(cue):
    """The .bin the .cue points at, so a dangling cue is caught before mounting."""
    name = None
    for line in cue.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.strip().split()
        if parts and parts[0].upper() == "FILE":
            quoted = line.split('"')
            name = quoted[1] if len(quoted) >= 3 else parts[1]
            break
    if name is None:
        fail("%s has no FILE directive" % cue)
    return cue.parent / name


def preflight(watcom, disc, need_disc):
    """Check every external dependency up front with a named failure each."""
    dosbox = resolve_dosbox()

    if not watcom.is_dir():
        fail("Watcom install not found: %s (override with FDPS_WATCOM)" % watcom)
    for rel in ("BIN/DOS4GW.EXE", "H/stdio.h", "LIB386/DOS/CLIB3S.LIB",
                "LIB386/MATH387S.LIB", "LIB386/DOS/EMU387.LIB"):
        if not (watcom / rel).is_file():
            fail("missing %s under %s" % (rel, watcom))
    for tool in ("WCC386.EXE", "WLINK.EXE"):
        if not any((watcom / d / tool).is_file() for d in BINDIRS):
            fail("%s not found in %s under %s"
                 % (tool, "/".join(BINDIRS), watcom))

    # The damaged-install check: stk386 carries __CHK/__STK/__GRO/__STKOVERFLOW,
    # which the 17 library modules that kept stack checking still reference.
    clib = watcom / "LIB386" / "DOS" / "CLIB3S.LIB"
    if b"stk386" not in clib.read_bytes().lower():
        fail("%s has no stk386 module -- this is the damaged 10.0a copy; use "
             "WATCOM_10.0a_infobase (rebuild_info/pitfalls.md)" % clib)

    if need_disc:
        if not disc.is_file():
            fail("disc image not found: %s (override with FDPS_DISC1)" % disc)
        binf = cue_bin(disc)
        if not binf.is_file():
            fail("%s names %s, which does not exist" % (disc, binf.name))

    if not (SMOKE_DIR / "smoke.c").is_file():
        fail("missing %s" % (SMOKE_DIR / "smoke.c"))
    return dosbox


# ------------------------------------------------------------- guest driving

def write_conf(path, mounts, disc, lines, logfile):
    """A DOSBox-X conf whose autoexec mounts, sets up Watcom, then runs lines.

    logfile is not optional: without it DOSBox-X prints a few init lines to
    stdout and then says "No logfile was given. All further logging will be
    discarded" -- so a fault scan over captured stdout would silently have
    nothing to find and pass everything.
    """
    body = ["[dosbox]", "memsize=32",
            "[sdl]", "output=surface", "autolock=false",
            "[cpu]", "core=auto", "cputype=pentium_mmx", "cycles=max",
            "[log]", "logfile=%s" % logfile,
            "[autoexec]"]
    for letter, host in mounts:
        body.append('mount %s "%s"' % (letter, host))
    if disc is not None:
        body.append('imgmount %s -t cdrom "%s"' % (DRV_DISC, disc))
    body += lines + ["exit"]
    path.write_text("\n".join(body) + "\n", encoding="latin-1")


def launch(dosbox, conf, log):
    fp = open(str(log), "wb")
    proc = subprocess.Popen([str(dosbox), "-silent", "-exit", "-conf", str(conf)],
                            stdout=fp, stderr=subprocess.STDOUT)
    return proc, fp


def find_ci(directory, name):
    """Case-insensitive lookup: the guest writes 8.3 names in its own case."""
    low = name.lower()
    if not directory.is_dir():
        return None
    for p in directory.iterdir():
        if p.is_file() and p.name.lower() == low:
            return p
    return None


def wait(proc, work_dir, done_name, timeout, poll=1, stall=45):
    """Wait on three signals; return (mode, last_heartbeat, elapsed).

    mode is one of completed / exited / stall / timeout. `stall` is deliberately
    longer than any single compile step: the heartbeat only advances between
    steps, so a shorter window would call a slow wcc386 run a hang.
    """
    start = time.time()
    last_hb = last_change = None
    mode = "timeout"
    while time.time() - start < timeout:
        if find_ci(work_dir, done_name):
            mode = "completed"
            break
        if proc.poll() is not None:
            mode = "exited"
            break
        hb = find_ci(work_dir, "hb.txt")
        if hb is not None:
            try:
                cur = hb.read_text(encoding="latin-1", errors="replace")
            except OSError:
                cur = last_hb
            now = time.time()
            if cur != last_hb:
                last_hb, last_change = cur, now
            elif last_change is not None and now - last_change > stall:
                mode = "stall"
                break
        time.sleep(poll)
    # the marker may land in the same tick the process exits
    if find_ci(work_dir, done_name):
        mode = "completed"
    elapsed = int(time.time() - start)
    if proc.poll() is None:
        try:
            proc.kill()
        except OSError:
            pass
    return mode, (last_hb or "").strip(), elapsed


def scan_fault(*logs):
    """Protected-mode faults out of DOSBox-X's own log and the captured stdout."""
    for log in logs:
        if not log.is_file():
            continue
        hits = [l.strip() for l in
                log.read_text(encoding="latin-1", errors="replace").splitlines()
                if l.strip() and FAULT_RX.search(l)]
        if hits:
            return "%s:\n%s" % (log.name, "\n".join(hits[-6:]))
    return None


# -------------------------------------------------------------------- build

def gen_lnk():
    """rebuild_info/build_flags.md: system dos4g pulls no CRT of its own, and
    wlink defaults the stack to 4K, so all three libraries and stack=8k are
    spelled out."""
    lines = ["system dos4g",
             r"name %s:\OUT\SMOKE.EXE" % DRV_WORK,
             "option stack=8k",
             r"file %s:\OUT\OBJ\SMOKE.OBJ" % DRV_WORK]
    lines += ["library %s" % lib for lib in CRT_LIBS]
    return "\n".join(lines) + "\n"


def gen_build_bat():
    """Compile then link, writing a heartbeat before each step and the done
    marker last. Flags travel in the WCC386 environment variable, not on the
    command line: COMMAND.COM silently truncates lines past ~176 characters
    after expansion, which mangles the trailing -fo= path."""
    w = DRV_WORK
    return "\r\n".join([
        r"echo compile > %s:\OUT\HB.TXT" % w,
        r"echo === compile === > %s:\OUT\BUILD.OUT" % w,
        r"WCC386 SMOKE.C -fo=%s:\OUT\OBJ\SMOKE.OBJ >> %s:\OUT\BUILD.OUT" % (w, w),
        r"echo link > %s:\OUT\HB.TXT" % w,
        r"echo === link === >> %s:\OUT\BUILD.OUT" % w,
        r"WLINK @%s:\SMOKE.LNK >> %s:\OUT\BUILD.OUT" % (w, w),
        r"echo done > %s:\OUT\HB.TXT" % w,
        r"echo done > %s:\OUT\BUILD.DON" % w,
        "",
    ])


def parse_undefined(txt):
    syms = set()
    for line in txt.splitlines():
        m = (re.search(r"undefined symbol\s+(\S+)", line)
             or re.search(r"(\S+)\s+is an undefined reference", line))
        if m:
            syms.add(m.group(1).strip())
    return sorted(syms)


def do_build(dosbox, watcom, disc, timeout):
    OBJ.mkdir(parents=True, exist_ok=True)
    for parent, name in ((OUT, "SMOKE.EXE"), (OUT, "BUILD.DON"),
                         (OUT, "BUILD.OUT"), (OUT, "HB.TXT"),
                         (OBJ, "SMOKE.OBJ"),
                         (WORK, "dosbox.log"), (WORK, "stdio.log")):
        p = find_ci(parent, name)
        if p is not None:
            p.unlink()

    (WORK / "SMOKE.LNK").write_text(gen_lnk(), encoding="latin-1")
    (WORK / "BUILD.BAT").write_text(gen_build_bat(), encoding="latin-1")
    write_conf(
        WORK / "build.conf",
        [(DRV_SRC, SMOKE_DIR), (DRV_WATCOM, watcom), (DRV_WORK, WORK)],
        disc,
        ["set WATCOM=%s:\\" % DRV_WATCOM,
         "set PATH=Z:\\;" + ";".join("%s:\\%s" % (DRV_WATCOM, d) for d in BINDIRS),
         "set INCLUDE=%s:\\H" % DRV_WATCOM,
         "set WCC386=" + CFLAGS,
         "%s:" % DRV_SRC,
         r"%s:\BUILD.BAT" % DRV_WORK],
        logfile=WORK / "dosbox.log",
    )

    print("[build] wcc386 %s" % CFLAGS)
    proc, fp = launch(dosbox, WORK / "build.conf", WORK / "stdio.log")
    mode, hb, secs = wait(proc, OUT, "build.don", timeout)
    fp.close()

    bout = find_ci(OUT, "build.out")
    txt = bout.read_text(encoding="latin-1", errors="replace") if bout else ""
    exe = find_ci(OUT, "smoke.exe")
    syms = parse_undefined(txt)
    fault = scan_fault(WORK / "dosbox.log", WORK / "stdio.log")

    print("[build] %s in %ds (last step: %s)" % (mode, secs, hb or "-"))
    if fault:
        print("[build] protected-mode fault in log:\n%s" % fault)
    if txt:
        print("\n".join("  | " + l for l in txt.splitlines()[-12:]))
    if syms:
        print("[build] %d undefined symbols: %s" % (len(syms), ", ".join(syms[:10])))
    if exe is None:
        print("[build] FAIL: SMOKE.EXE not produced")
        return False
    print("[build] SMOKE.EXE %d bytes -> %s" % (exe.stat().st_size, exe))
    if mode != "completed":
        # An .EXE on disk is not proof the build finished: wlink can have
        # written the image and the guest then wedged before the done marker,
        # leaving a truncated BUILD.OUT that parse_undefined reads as clean.
        print("[build] FAIL: no completion marker (mode=%s)" % mode)
        return False
    return not syms and not fault


# ---------------------------------------------------------------------- run

def do_run(dosbox, watcom, disc, timeout):
    exe = find_ci(OUT, "smoke.exe")
    if exe is None:
        print("[run] FAIL: no SMOKE.EXE -- build first")
        return False
    RUN.mkdir(parents=True, exist_ok=True)
    # logs included: DOSBox-X appends to its logfile, so a stale one would
    # carry a previous run's fault into this run's scan
    for name in ("RESULT.TXT", "RUN.DON", "HB.TXT", "dosbox.log", "stdio.log"):
        p = find_ci(RUN, name)
        if p is not None:
            p.unlink()
    shutil.copy2(exe, RUN / "SMOKE.EXE")
    # DOS/4GW is found on PATH, but a copy alongside the EXE is what a real
    # install looks like and keeps the run dir self-contained.
    shutil.copy2(watcom / "BIN" / "DOS4GW.EXE", RUN / "DOS4GW.EXE")

    write_conf(
        RUN / "run.conf",
        [(DRV_SRC, RUN), (DRV_WATCOM, watcom)],
        disc,
        ["set PATH=Z:\\;%s:\\;%s:\\BIN" % (DRV_SRC, DRV_WATCOM),
         "%s:" % DRV_SRC,
         "SMOKE.EXE %s:\\PACK.VFS" % DRV_DISC],
        logfile=RUN / "dosbox.log",
    )

    print("[run] executing SMOKE.EXE under DOS/4GW")
    proc, fp = launch(dosbox, RUN / "run.conf", RUN / "stdio.log")
    mode, hb, secs = wait(proc, RUN, "run.don", timeout)
    fp.close()

    fault = scan_fault(RUN / "dosbox.log", RUN / "stdio.log")
    res = find_ci(RUN, "result.txt")
    text = res.read_text(encoding="latin-1", errors="replace") if res else ""

    print("[run] %s in %ds (last step: %s)" % (mode, secs, hb or "-"))
    if fault:
        print("[run] protected-mode fault in log:\n%s" % fault)
    if text:
        print("\n".join("  | " + l for l in text.splitlines()))
    else:
        print("[run] FAIL: RESULT.TXT not written")
        return False

    # Whitespace-separated key=value; a value containing spaces (disc_sig) is
    # therefore truncated here, which is fine -- every checked key is one token.
    fields = dict(kv.split("=", 1) for kv in text.split() if "=" in kv)
    ok = True
    for key, want in (("crt", "ok"), ("disc_probe", "ok"), ("dpmi_int2f", "ok"),
                      ("verdict", "ok")):
        if fields.get(key) != want:
            print("[run] FAIL: %s=%s, expected %s" % (key, fields.get(key), want))
            ok = False
    if mode != "completed":
        print("[run] FAIL: no completion marker (mode=%s)" % mode)
        ok = False
    return ok and not fault


# ----------------------------------------------------------------- selftest

class _FakeProc(object):
    """Stands in for the DOSBox-X process so wait() can be driven directly."""

    def __init__(self, exit_after=None):
        self.exit_after = exit_after
        self.start = time.time()
        self.killed = False

    def poll(self):
        if self.exit_after is None:
            return None
        return 0 if time.time() - self.start >= self.exit_after else None

    def kill(self):
        self.killed = True


def _selftest_wait(tmp):
    """Each of wait()'s four verdicts, so a hang is never reported as success.

    Returns [(name, passed, detail)].
    """
    rows = []

    d = tmp / "completed"
    d.mkdir()
    (d / "BUILD.DON").write_text("done\n", encoding="latin-1")
    mode = wait(_FakeProc(), d, "build.don", 5, poll=0.1)[0]
    rows.append(("done marker", mode == "completed", mode))

    d = tmp / "exited"
    d.mkdir()
    mode = wait(_FakeProc(exit_after=0), d, "build.don", 5, poll=0.1)[0]
    rows.append(("process exit", mode == "exited", mode))

    # Heartbeat present but frozen: caught by the stall window well inside the
    # timeout, and it names the step it froze on. This is the signal a fixed
    # sleep cannot give.
    d = tmp / "stall"
    d.mkdir()
    (d / "HB.TXT").write_text("compile\n", encoding="latin-1")
    mode, hb, secs = wait(_FakeProc(), d, "build.don", 30, poll=0.1, stall=0.5)
    rows.append(("heartbeat stall", mode == "stall", mode))
    rows.append(("stall names the step", hb == "compile", hb))
    rows.append(("stall beats the timeout", secs < 30, "%ds" % secs))

    d = tmp / "timeout"  # no marker, no exit, no heartbeat: backstop only
    d.mkdir()
    mode = wait(_FakeProc(), d, "build.don", 1, poll=0.1)[0]
    rows.append(("timeout backstop", mode == "timeout", mode))
    return rows


def _selftest_preflight(watcom, disc):
    """Every preflight failure names its own cause instead of letting a later
    step fail obscurely. The damaged-install case uses the real damaged 10.0a
    copy next door, not a fabricated one."""
    rows = []

    def expect(name, fragment, fn):
        try:
            fn()
            rows.append((name, False, "no error raised"))
        except SystemExit as exc:
            rows.append((name, fragment in str(exc), str(exc)))

    expect("missing watcom named", "Watcom install not found",
           lambda: preflight(watcom.parent / "NO_SUCH_WATCOM", disc, True))

    damaged = watcom.parent / "WATCOM_10.0a"
    if damaged.is_dir():
        expect("damaged clib3s named", "no stk386 module",
               lambda: preflight(damaged, disc, True))
    else:
        rows.append(("damaged clib3s named", True, "skipped: %s absent" % damaged))

    expect("missing disc named", "disc image not found",
           lambda: preflight(watcom, disc.parent / "NO_SUCH.cue", True))
    return rows


def do_selftest(watcom, disc):
    import tempfile
    tmp = Path(tempfile.mkdtemp(prefix="fdps_selftest_"))
    try:
        rows = _selftest_wait(tmp) + _selftest_preflight(watcom, disc)
    finally:
        shutil.rmtree(str(tmp), ignore_errors=True)
    ok = True
    for name, passed, detail in rows:
        print("[selftest] %-24s %s (%s)"
              % (name, "ok" if passed else "FAIL", detail))
        ok = ok and passed
    return ok


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", nargs="?", default="all",
                    choices=("build", "run", "all", "selftest"))
    ap.add_argument("--timeout", type=int, default=300,
                    help="backstop seconds per stage; the heartbeat normally "
                         "ends a hung stage long before this")
    args = ap.parse_args()

    watcom = Path(os.environ.get("FDPS_WATCOM") or WATCOM_DEFAULT)
    disc = Path(os.environ.get("FDPS_DISC1") or DISC_DEFAULT)

    if args.stage == "selftest":
        ok = do_selftest(watcom, disc)
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    dosbox = preflight(watcom, disc, need_disc=True)
    print("[preflight] dosbox=%s\n[preflight] watcom=%s\n[preflight] disc=%s"
          % (dosbox, watcom, disc))

    ok = True
    if args.stage in ("build", "all"):
        ok = do_build(dosbox, watcom, disc, args.timeout)
    if ok and args.stage in ("run", "all"):
        ok = do_run(dosbox, watcom, disc, args.timeout)
    print("[result] %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
