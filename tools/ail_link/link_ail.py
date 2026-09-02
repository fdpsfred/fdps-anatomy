"""link_ail.py -- link the AIL library carried over from FD2 and prove it runs.

ADR-0004 decided to reuse the Miles AIL static library the FD2 project
extracted from FD2.LE instead of extracting one from FDPS.LE again. Every
piece of evidence behind that decision was a comparison; nothing had actually
been linked. This is the experiment: build a client with FDPS's own compile
and link flags against that library, then run it under DOSBox-X and play a
tone through the real driver.

Two things the game side must supply, because the library does not contain
them and references them as externals:

  * six DPMI service routines (src/dpmi.c) -- the call direction is
    library -> game, so no amount of vendor library covers them
  * the four-byte EFLAGS-save routine (src/ailflags.asm)

The library was built for FD2 and its EXTDEF records carry FD2's spelling of
those names. FDPS's symbols are named for FDPS, so the link script bridges the
two with wlink `alias` directives rather than renaming either side.

The DOSBox-X driving mechanics -- preflight, generated conf, three-signal
completion, fault scan -- are shared with tools/fdps_build/build_min.py rather
than reimplemented, so a fix to hang detection lands in one place.

Subcommands:
    build   compile + assemble + link -> workspace/ail_link/out/AILSMOK.EXE
    run     stage that EXE with DIG.INI and the SB16 driver, run it
    all     build then run (default)

Usage: python tools/ail_link/link_ail.py [build|run|all] [--timeout N]
Exit : 0 when every stage it ran passed, 1 otherwise.
"""
import argparse
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "fdps_build"))
import build_min as bm  # noqa: E402

HERE = Path(__file__).resolve().parent
CLIENT_DIR = HERE / "ailsmoke"
SRC = ROOT / "src"
LIBS = ROOT / "libs" / "ailv3"
GAME = ROOT / "fdps_game_files"

WORK = ROOT / "workspace" / "ail_link"
STAGE = WORK / "src"
OUT = WORK / "out"
OBJ = OUT / "obj"
RUN = WORK / "run"
GUEST_LIB = WORK / "lib"

DRV_SRC = bm.DRV_SRC
DRV_WATCOM = bm.DRV_WATCOM
DRV_DISC = bm.DRV_DISC
DRV_WORK = bm.DRV_WORK

# The AIL library's own EXTDEF spelling on the left, FDPS's symbol on the
# right. Aliasing at link time keeps rebuild_info/naming.md's rule intact --
# the C name still matches the Ghidra name on both sides of the bridge.
ALIASES = [
    ("fd2_dpmi_alloc_dos_memory", "fdps_dpmi_alloc_dos_memory"),
    ("fd2_dpmi_free_dos_memory", "fdps_dpmi_free_dos_memory"),
    ("fd2_dpmi_lock_region", "fdps_dpmi_lock_region"),
    ("fd2_dpmi_unlock_region", "fdps_dpmi_unlock_region"),
    ("fd2_dpmi_lock_size", "fdps_dpmi_lock_size"),
    ("fd2_dpmi_unlock_size", "fdps_dpmi_unlock_size"),
    # FD2 filed the four-byte EFLAGS routine under crt_equivalent_ and its
    # library reaches the body through a thunk. FDPS reads the same bytes as
    # AIL's own (program_info/code_pools.md), and the thunk is a vendor-library
    # linking artifact with nothing to reproduce, so the alias lands directly
    # on the body.
    ("crt_equivalent_get_eflags_thunk", "AIL_internal_isr_eflags_save_cli"),
]

# C sources staged onto the guest's C: drive, and the objects they produce.
C_SOURCES = [(CLIENT_DIR / "ailsmoke.c", "AILSMOKE"),
             (SRC / "dpmi.c", "DPMI")]
ASM_SOURCES = [(SRC / "ailflags.asm", "AILFLAGS")]
HEADERS = [SRC / "dpmi.h", LIBS / "ailv3.h"]

# Staged next to the EXE so AIL finds them where the game would: DIG.INI names
# the driver, and the driver image is what AIL loads and calls.
RUN_ASSETS = ["DIG.INI", "SB16.DIG"]

# DOSBox-X's own SB16 defaults, spelled out here and repeated into BLASTER so
# the guest never depends on what a future DOSBox-X release picks by default.
# DIG.INI asks for IRQ/DMA autodetection, and BLASTER is where that comes from.
SB_BASE = 0x220
SB_IRQ = 7
SB_DMA = 1
SB_HDMA = 5


def preflight_extra(need_assets):
    for path in [LIBS / "ailv3.lib", LIBS / "ailv3.h", CLIENT_DIR / "ailsmoke.c",
                 SRC / "dpmi.c", SRC / "dpmi.h", SRC / "ailflags.asm"]:
        if not path.is_file():
            bm.fail("missing %s" % path)
    if not need_assets:
        return
    for name in RUN_ASSETS:
        if bm.find_ci(GAME, name) is None:
            bm.fail("missing %s under %s -- the run stage needs the original "
                    "driver and INI" % (name, GAME))


def preflight_wasm(watcom):
    """wcc386 and wlink are checked by build_min; WASM is this build's own
    dependency and lives in yet another directory (BINB under 10.0a)."""
    if not any((watcom / d / "WASM.EXE").is_file() for d in bm.BINDIRS):
        bm.fail("WASM.EXE not found in %s under %s"
                % ("/".join(bm.BINDIRS), watcom))


def gen_lnk(aliases):
    w = DRV_WORK
    lines = ["system dos4g",
             r"name %s:\OUT\AILSMOK.EXE" % w,
             "option stack=8k"]
    lines += ["alias %s=%s" % (a, b) for a, b in aliases]
    lines += [r"file %s:\OUT\OBJ\%s.OBJ" % (w, stem)
              for _, stem in C_SOURCES + ASM_SOURCES]
    lines.append(r"library %s:\LIB\AILV3.LIB" % w)
    lines += ["library %s" % lib for lib in bm.CRT_LIBS]
    return "\n".join(lines) + "\n"


def gen_build_bat():
    w = DRV_WORK
    steps = []
    for path, stem in C_SOURCES:
        steps.append((stem.lower(),
                      r"WCC386 %s -fo=%s:\OUT\OBJ\%s.OBJ >> %s:\OUT\BUILD.OUT"
                      % (path.name.upper(), w, stem, w)))
    for path, stem in ASM_SOURCES:
        steps.append((stem.lower(),
                      r"WASM %s -fo=%s:\OUT\OBJ\%s.OBJ >> %s:\OUT\BUILD.OUT"
                      % (path.name.upper(), w, stem, w)))
    steps.append(("link", r"WLINK @%s:\AILSMOK.LNK >> %s:\OUT\BUILD.OUT" % (w, w)))

    out = []
    for label, cmd in steps:
        out.append(r"echo %s > %s:\OUT\HB.TXT" % (label, w))
        out.append(r"echo === %s === >> %s:\OUT\BUILD.OUT" % (label, w))
        out.append(cmd)
    out.append(r"echo done > %s:\OUT\HB.TXT" % w)
    out.append(r"echo done > %s:\OUT\BUILD.DON" % w)
    return "\r\n".join(out) + "\r\n"


def stage_sources():
    if STAGE.is_dir():
        shutil.rmtree(str(STAGE))
    STAGE.mkdir(parents=True)
    for path, _ in C_SOURCES + ASM_SOURCES:
        shutil.copy2(path, STAGE / path.name.upper())
    for path in HEADERS:
        shutil.copy2(path, STAGE / path.name.upper())
    GUEST_LIB.mkdir(parents=True, exist_ok=True)
    shutil.copy2(LIBS / "ailv3.lib", GUEST_LIB / "AILV3.LIB")


def do_build(dosbox, watcom, disc, timeout, aliases=None, quiet=False):
    """Compile, assemble and link. Returns (ok, undefined_symbols)."""
    if aliases is None:
        aliases = ALIASES
    OBJ.mkdir(parents=True, exist_ok=True)
    for parent, name in ((OUT, "AILSMOK.EXE"), (OUT, "BUILD.DON"),
                         (OUT, "BUILD.OUT"), (OUT, "HB.TXT"),
                         (WORK, "dosbox.log"), (WORK, "stdio.log")):
        p = bm.find_ci(parent, name)
        if p is not None:
            p.unlink()
    for _, stem in C_SOURCES + ASM_SOURCES:
        p = bm.find_ci(OBJ, stem + ".OBJ")
        if p is not None:
            p.unlink()

    stage_sources()
    (WORK / "AILSMOK.LNK").write_text(gen_lnk(aliases), encoding="latin-1")
    (WORK / "BUILD.BAT").write_text(gen_build_bat(), encoding="latin-1")
    bm.write_conf(
        WORK / "build.conf",
        [(DRV_SRC, STAGE), (DRV_WATCOM, watcom), (DRV_WORK, WORK)],
        disc,
        ["set WATCOM=%s:\\" % DRV_WATCOM,
         "set PATH=Z:\\;" + ";".join("%s:\\%s" % (DRV_WATCOM, d)
                                     for d in bm.BINDIRS),
         "set INCLUDE=%s:\\H" % DRV_WATCOM,
         "set WCC386=" + bm.CFLAGS,
         "%s:" % DRV_SRC,
         r"%s:\BUILD.BAT" % DRV_WORK],
        logfile=WORK / "dosbox.log",
    )

    if not quiet:
        print("[build] wcc386 %s" % bm.CFLAGS)
        print("[build] %d link aliases bridging FD2 names to FDPS symbols"
              % len(aliases))
    proc, fp = bm.launch(dosbox, WORK / "build.conf", WORK / "stdio.log")
    mode, hb, secs = bm.wait(proc, OUT, "build.don", timeout)
    fp.close()

    bout = bm.find_ci(OUT, "build.out")
    txt = bout.read_text(encoding="latin-1", errors="replace") if bout else ""
    exe = bm.find_ci(OUT, "ailsmok.exe")
    syms = bm.parse_undefined(txt)
    fault = bm.scan_fault(WORK / "dosbox.log", WORK / "stdio.log")

    if not quiet:
        print("[build] %s in %ds (last step: %s)" % (mode, secs, hb or "-"))
        if fault:
            print("[build] protected-mode fault in log:\n%s" % fault)
        if txt:
            print("\n".join("  | " + l for l in txt.splitlines()[-20:]))
        if syms:
            print("[build] %d undefined symbols: %s"
                  % (len(syms), ", ".join(syms[:20])))
    if exe is None:
        if not quiet:
            print("[build] FAIL: AILSMOK.EXE not produced")
        return False, syms
    if not quiet:
        print("[build] AILSMOK.EXE %d bytes -> %s" % (exe.stat().st_size, exe))
    if mode != "completed":
        if not quiet:
            print("[build] FAIL: no completion marker (mode=%s)" % mode)
        return False, syms
    return (not syms and not fault), syms


def do_run(dosbox, watcom, disc, timeout):
    exe = bm.find_ci(OUT, "ailsmok.exe")
    if exe is None:
        print("[run] FAIL: no AILSMOK.EXE -- build first")
        return False
    RUN.mkdir(parents=True, exist_ok=True)
    for name in ("RESULT.TXT", "RUN.DON", "HB.TXT", "dosbox.log", "stdio.log"):
        p = bm.find_ci(RUN, name)
        if p is not None:
            p.unlink()
    shutil.copy2(exe, RUN / "AILSMOK.EXE")
    shutil.copy2(watcom / "BIN" / "DOS4GW.EXE", RUN / "DOS4GW.EXE")
    for name in RUN_ASSETS:
        shutil.copy2(bm.find_ci(GAME, name), RUN / name)

    conf = RUN / "run.conf"
    bm.write_conf(
        conf, [(DRV_SRC, RUN), (DRV_WATCOM, watcom)], disc,
        ["set BLASTER=A%X I%d D%d H%d T6" % (SB_BASE, SB_IRQ, SB_DMA, SB_HDMA),
         "set PATH=Z:\\;%s:\\;%s:\\BIN" % (DRV_SRC, DRV_WATCOM),
         "%s:" % DRV_SRC,
         "AILSMOK.EXE",
         r"echo done > %s:\RUN.DON" % DRV_SRC],
        logfile=RUN / "dosbox.log",
    )
    # write_conf has no sound section of its own; the client needs a real SB16
    # to talk to, so it is appended here rather than pushed into the shared
    # helper, which the CRT-only smoke build has no use for.
    text = conf.read_text(encoding="latin-1")
    text = text.replace(
        "[autoexec]",
        "[sblaster]\nsbtype=sb16\nsbbase=%x\nirq=%d\ndma=%d\nhdma=%d\n[autoexec]"
        % (SB_BASE, SB_IRQ, SB_DMA, SB_HDMA))
    conf.write_text(text, encoding="latin-1")

    print("[run] executing AILSMOK.EXE against SB16 at %03Xh IRQ %d"
          % (SB_BASE, SB_IRQ))
    # No -silent here: it silences the SB16 as well as the console, and the
    # driver install then fails with error code 0 (rebuild_info/pitfalls.md).
    proc, fp = bm.launch(dosbox, conf, RUN / "stdio.log", silent=False)
    mode, hb, secs = bm.wait(proc, RUN, "run.don", timeout)
    fp.close()

    fault = bm.scan_fault(RUN / "dosbox.log", RUN / "stdio.log")
    res = bm.find_ci(RUN, "result.txt")
    text = res.read_text(encoding="latin-1", errors="replace") if res else ""

    print("[run] %s in %ds (last step: %s)" % (mode, secs, hb or "-"))
    if fault:
        print("[run] protected-mode fault in log:\n%s" % fault)
    if not text:
        print("[run] FAIL: RESULT.TXT not written")
        return False
    print("\n".join("  | " + l for l in text.splitlines()))

    fields = dict(kv.split("=", 1) for kv in text.split() if "=" in kv)
    ok = True
    for key, want in (("startup", "ok"), ("dig", "ok"), ("sample", "ok"),
                      ("playing", "ok"), ("timer", "ok"), ("shutdown", "ok"),
                      ("verdict", "ok")):
        if fields.get(key) != want:
            print("[run] FAIL: %s=%s, expected %s" % (key, fields.get(key), want))
            ok = False
    if mode != "completed":
        print("[run] FAIL: no completion marker (mode=%s)" % mode)
        ok = False
    return ok and not fault


def do_selftest(dosbox, watcom, disc, timeout):
    """Prove the undefined-symbol check can fail.

    "0 undefined symbols" is the whole claim of this ticket, and a check that
    never fires reports exactly that on a broken link too. So drop one alias
    and require the build to come back with the symbol it can no longer
    resolve -- if this passes silently, the real build's clean result means
    nothing.
    """
    dropped, target = ALIASES[0]
    ok, syms = do_build(dosbox, watcom, disc, timeout,
                        aliases=ALIASES[1:], quiet=True)
    rows = [("dropping an alias fails the link", not ok, "ok=%s" % ok),
            ("the missing symbol is named", dropped in syms,
             ", ".join(syms) or "(none)")]
    passed = True
    for name, good, detail in rows:
        print("[selftest] %-34s %s (%s)"
              % (name, "ok" if good else "FAIL", detail))
        passed = passed and good
    print("[selftest] dropped alias was %s=%s" % (dropped, target))
    # Leave the tree holding a good executable, not the deliberately broken one.
    rebuilt, _ = do_build(dosbox, watcom, disc, timeout, quiet=True)
    print("[selftest] %-34s %s" % ("rebuilt with all aliases",
                                   "ok" if rebuilt else "FAIL"))
    return passed and rebuilt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", nargs="?", default="all",
                    choices=("build", "run", "all", "selftest"))
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()

    watcom = Path(os.environ.get("FDPS_WATCOM") or bm.WATCOM_DEFAULT)
    disc = Path(os.environ.get("FDPS_DISC1") or bm.DISC_DEFAULT)

    # Only the run stage needs the disc image and the game's driver files.
    # Demanding them for `build` and `selftest` would make the undefined-symbol
    # self-check -- the one thing this tool must always be able to run --
    # depend on a mounted CD and an unversioned asset directory.
    needs_run_env = args.stage in ("run", "all")
    dosbox = bm.preflight(watcom, disc, need_disc=needs_run_env)
    preflight_wasm(watcom)
    preflight_extra(needs_run_env)
    print("[preflight] dosbox=%s\n[preflight] watcom=%s\n[preflight] ailv3=%s"
          % (dosbox, watcom, LIBS / "ailv3.lib"))

    ok = True
    if args.stage == "selftest":
        ok = do_selftest(dosbox, watcom, disc, args.timeout)
    if args.stage in ("build", "all"):
        ok, _ = do_build(dosbox, watcom, disc, args.timeout)
    if ok and args.stage in ("run", "all"):
        ok = do_run(dosbox, watcom, disc, args.timeout)
    print("[result] %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
