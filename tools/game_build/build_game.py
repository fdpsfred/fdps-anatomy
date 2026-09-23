"""build_game.py -- build the game itself: src/ only, linked into FDE.EXE.

This is the executable that replaces the shipped FDPS.EXE.  It is built from
the same sources, with the same flags, the same AIL library and the same seven
link aliases as the unit-test image (tools/code_emit/build_emit.py), and from
nothing else: no tests/ unit is compiled, and src/main.c keeps its `main` --
the test image renames it away, this build must not.

It reuses the shared DOSBox-X machinery of tools/fdps_build/build_min.py and
the source scan of build_emit.py rather than restating either, so a source that
fails the decompiler-name check fails here for the same reason and with the
same message.

The link is rebuild_info/build_flags.md's link command: `system dos4g`, an 8K
stack, the three CRT libraries named explicitly, output named FDE.EXE (the
shipped LE's resident name is `fde`; FDPS.EXE is a rename made afterwards), and
the module holding `main` listed first.  One link, no stub pass: every symbol
the game references has a definition in src/ or a library, and anything still
undefined is a failure, never a worklist.

Subcommands:
    build     compile + link  -> workspace/game_build/out/FDE.EXE (+ FDE.MAP)
    selftest  link-file and batch generation checks that need no DOSBox-X

Usage: python tools/game_build/build_game.py [build|selftest] [--json]
Exit : 0 when the build is clean (zero errors, zero warnings, zero undefined).
"""
import argparse
import json
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

sys.path.insert(0, str(ROOT / "tools" / "fdps_build"))
sys.path.insert(0, str(ROOT / "tools" / "ail_link"))
sys.path.insert(0, str(ROOT / "tools" / "code_emit"))
import build_min as bm  # noqa: E402
import link_ail  # noqa: E402
import build_emit as ce  # noqa: E402

SRC = ROOT / "src"

WORK = ROOT / "workspace" / "game_build"
STAGE = WORK / "stage"
OUT = WORK / "out"
OBJ = OUT / "obj"
GUEST_LIB = WORK / "lib"

EXE = "FDE.EXE"
MAP = "FDE.MAP"
LNK = "FDE.LNK"

DRV_SRC = bm.DRV_SRC
DRV_WATCOM = bm.DRV_WATCOM
DRV_WORK = bm.DRV_WORK

G_SRC = "SRC"

# The unit that defines `main`.  build_flags.md: the shipped image's module
# holding main was fde.obj and came first; the object name itself does not
# reach the image once `name` is given, but the order is kept.
ENTRY_UNIT = "MAIN"


# ---------------------------------------------------------------- generation

def link_order(objs):
    """The entry unit first, the rest in the order given."""
    head = [o for o in objs if o.upper() == ENTRY_UNIT]
    return head + [o for o in objs if o.upper() != ENTRY_UNIT]


def gen_lnk(objs):
    w = DRV_WORK
    lines = ["system dos4g",
             r"name %s:\OUT\%s" % (w, EXE),
             "option stack=8k",
             # The map is what a playtest bug is located with: it gives every
             # symbol's linked address in THIS image, which is the bridge from
             # a fault address back to a function.
             r"option map=%s:\OUT\%s" % (w, MAP)]
    lines += ["alias %s=%s" % (a, b) for a, b in link_ail.ALIASES]
    lines += [r"file %s:\OUT\OBJ\%s.OBJ" % (w, s) for s in link_order(objs)]
    lines.append(r"library %s:\LIB\AILV3.LIB" % w)
    lines += ["library %s" % lib for lib in bm.CRT_LIBS]
    return "\n".join(lines) + "\n"


def gen_build_bat(src_c, src_asm):
    """Compile every unit, then link, with a heartbeat before each step."""
    w = DRV_WORK
    steps = []
    for stem in src_c:
        steps.append((stem.lower(),
                      r"WCC386 %s\%s.C -fo=%s:\OUT\OBJ\%s.OBJ >> %s:\OUT\BUILD.OUT"
                      % (G_SRC, stem, w, stem, w)))
    for stem in src_asm:
        steps.append((stem.lower(),
                      r"WASM %s\%s.ASM -fo=%s:\OUT\OBJ\%s.OBJ >> %s:\OUT\BUILD.OUT"
                      % (G_SRC, stem, w, stem, w)))
    steps.append(("link", r"WLINK @%s:\%s >> %s:\OUT\BUILD.OUT" % (w, LNK, w)))

    out = []
    for label, cmd in steps:
        out.append(r"echo %s > %s:\OUT\HB.TXT" % (label, w))
        out.append(r"echo === %s === >> %s:\OUT\BUILD.OUT" % (label, w))
        out.append(cmd)
    out.append(r"echo done > %s:\OUT\HB.TXT" % w)
    out.append(r"echo done > %s:\OUT\BUILD.DON" % w)
    return "\r\n".join(out) + "\r\n"


def stage_sources():
    """src/ into the guest tree under 8.3 upper-case names, rebuilt each time
    so a file deleted from the repository cannot be compiled from a stale copy."""
    if STAGE.is_dir():
        shutil.rmtree(str(STAGE))
    (STAGE / G_SRC).mkdir(parents=True)
    for path in ce.c_sources(SRC) + ce.asm_sources(SRC) + ce.headers(SRC):
        shutil.copy2(path, STAGE / G_SRC / path.name.upper())
    shutil.copy2(ce.AIL_HDR, STAGE / G_SRC / ce.AIL_HDR.name.upper())
    GUEST_LIB.mkdir(parents=True, exist_ok=True)
    dst = GUEST_LIB / "AILV3.LIB"
    if not dst.is_file() or dst.stat().st_size != ce.AIL_LIB.stat().st_size:
        shutil.copy2(ce.AIL_LIB, dst)


# --------------------------------------------------------------------- build

def preflight(watcom, disc):
    dosbox = bm.preflight(watcom, disc, need_disc=False)
    link_ail.preflight_wasm(watcom)
    ce.preflight_ail()
    if bm.find_ci(SRC, "main.c") is None:
        bm.fail("missing %s -- the game has no entry point" % (SRC / "main.c"))
    return dosbox


def do_build(dosbox, watcom, disc, timeout, quiet=False):
    OBJ.mkdir(parents=True, exist_ok=True)
    for parent, name in ((OUT, EXE), (OUT, MAP), (OUT, "BUILD.DON"),
                         (OUT, "BUILD.OUT"), (OUT, "HB.TXT"), (OUT, "NAMES.OUT"),
                         (WORK, "dosbox.log"), (WORK, "stdio.log")):
        p = bm.find_ci(parent, name)
        if p is not None:
            p.unlink()
    for p in OBJ.iterdir():
        if p.is_file():
            p.unlink()

    found = ce.default_names(ce.c_sources(SRC) + ce.headers(SRC))
    if found:
        report = ce.name_report(found)
        (OUT / "NAMES.OUT").write_text(report, encoding="latin-1")
        if not quiet:
            print("[build] FAIL: %d Ghidra decompiler default name(s) in src/"
                  % len(found))
            print("".join("  | " + l for l in report.splitlines(True)[:25]))
        return False

    stage_sources()
    src_c = [p.stem.upper() for p in ce.c_sources(SRC)]
    src_asm = [p.stem.upper() for p in ce.asm_sources(SRC)]
    (WORK / LNK).write_text(gen_lnk(src_c + src_asm), encoding="latin-1")
    (WORK / "BUILD.BAT").write_text(gen_build_bat(src_c, src_asm),
                                    encoding="latin-1")
    bm.write_conf(
        WORK / "build.conf",
        [(DRV_SRC, STAGE), (DRV_WATCOM, watcom), (DRV_WORK, WORK)],
        disc,
        ["set WATCOM=%s:\\" % DRV_WATCOM,
         "set PATH=Z:\\;" + ";".join("%s:\\%s" % (DRV_WATCOM, d)
                                     for d in bm.BINDIRS),
         "set INCLUDE=%s:\\H;%s:\\%s" % (DRV_WATCOM, DRV_SRC, G_SRC),
         "set WCC386=" + bm.CFLAGS,
         "%s:" % DRV_SRC,
         r"%s:\BUILD.BAT" % DRV_WORK],
        logfile=WORK / "dosbox.log",
    )

    if not quiet:
        print("[build] %d C unit(s), %d assembly unit(s) -> %s"
              % (len(src_c), len(src_asm), EXE))
    proc, fp = bm.launch(dosbox, WORK / "build.conf", WORK / "stdio.log")
    mode, hb, secs = bm.wait(proc, OUT, "build.don", timeout)
    fp.close()

    fault = bm.scan_fault(WORK / "dosbox.log", WORK / "stdio.log")
    diag = diagnostics()
    exe = bm.find_ci(OUT, EXE)
    if not quiet:
        print("[build] %s in %ds (last step: %s)" % (mode, secs, hb or "-"))
        if fault:
            print("[build] protected-mode fault in log:\n%s" % fault)
        for key in ("errors", "warnings", "undefined"):
            for line in diag[key][:20]:
                print("  | %s: %s" % (key, line))
    if mode != "completed":
        if not quiet:
            print("[build] FAIL: no completion marker (mode=%s)" % mode)
        return False
    if exe is None:
        if not quiet:
            print("[build] FAIL: %s not produced" % EXE)
        return False
    if not quiet:
        print("[build] %s %d bytes -> %s" % (EXE, exe.stat().st_size, exe))
    return (not fault and not diag["errors"] and not diag["warnings"]
            and not diag["undefined"])


def diagnostics(out_dir=None):
    """Errors, warnings and undefined symbols out of the build's transcripts.

    One link, so unlike the test image nothing undefined is expected: every
    undefined symbol is an error.  NAMES.OUT carries the decompiler-name
    findings in the compiler's own format, so they arrive as errors too.
    """
    out_dir = out_dir or OUT
    texts = []
    for name in ("names.out", "build.out"):
        p = bm.find_ci(out_dir, name)
        if p is not None:
            texts.append(p.read_text(encoding="latin-1", errors="replace"))
    text = "\n".join(texts)
    lines = [l.strip() for l in text.splitlines()]
    return {"errors": ([l for l in lines if "Error!" in l]
                       + [l for l in lines if ce.REDEFINITION_RX.search(l)]),
            "warnings": [l for l in lines if ce.WARNING_RX.search(l)
                         and not ce.REDEFINITION_RX.search(l)],
            "undefined": bm.parse_undefined(text)}


# ----------------------------------------------------------------- selftest

def _selftest_rows():
    import tempfile
    rows = []

    lnk = gen_lnk(["AIACT", "MAIN", "RLEBASE"])
    files = [l for l in lnk.splitlines() if l.startswith("file ")]
    rows.append(("entry unit is linked first",
                 files[0].endswith(r"\MAIN.OBJ") and len(files) == 3,
                 files[0] if files else "no file lines"))
    rows.append(("output is FDE.EXE", (r"name %s:\OUT\FDE.EXE" % DRV_WORK) in lnk,
                 "yes"))
    rows.append(("8K stack spelled out", "option stack=8k" in lnk, "yes"))
    rows.append(("map written", ("option map=%s:\\OUT\\FDE.MAP" % DRV_WORK) in lnk,
                 "yes"))
    rows.append(("AIL library and every alias",
                 "AILV3.LIB" in lnk
                 and len([l for l in lnk.splitlines() if l.startswith("alias ")])
                 == len(link_ail.ALIASES), "%d alias(es)" % len(link_ail.ALIASES)))
    rows.append(("three CRT libraries",
                 all(("library %s" % lib) in lnk for lib in bm.CRT_LIBS), "yes"))
    rows.append(("no test or stub object", "OBJT" not in lnk and "STUBS" not in lnk,
                 "yes"))

    bat = gen_build_bat(["MAIN", "MENU"], ["RLEBASE"])
    rows.append(("main keeps its name", "-dmain" not in bat.lower(), "yes"))
    rows.append(("assembly assembled with wasm", r"WASM SRC\RLEBASE.ASM" in bat,
                 "yes"))
    longest = max(len(l) for l in bat.splitlines())
    rows.append(("batch lines stay short", longest < 120, "%d chars" % longest))

    tmp = Path(tempfile.mkdtemp(prefix="fdps_game_selftest_"))
    try:
        (tmp / "build.out").write_text(
            "creating a DOS/4G executable\n", encoding="latin-1")
        d = diagnostics(tmp)
        rows.append(("clean transcript reads clean",
                     not d["errors"] and not d["warnings"] and not d["undefined"],
                     json.dumps(d)))
        (tmp / "build.out").write_text(
            "Error! E2028: fdps_thing is an undefined reference\n"
            "file OBJ\\MENU.OBJ(MENU.C): undefined symbol fdps_thing\n",
            encoding="latin-1")
        d = diagnostics(tmp)
        rows.append(("an undefined symbol fails the build",
                     d["undefined"] == ["fdps_thing"] and d["errors"],
                     json.dumps(d["undefined"])))
        (tmp / "build.out").write_text(
            "MENU.C(12): Warning! W202: Symbol 'x' has been defined, but not "
            "referenced\nWarning(1027): file X.OBJ: redefinition of y ignored\n",
            encoding="latin-1")
        d = diagnostics(tmp)
        rows.append(("warnings are read, redefinition is an error",
                     len(d["warnings"]) == 1 and len(d["errors"]) == 1,
                     json.dumps(d)))
        (tmp / "build.out").write_text("", encoding="latin-1")
        (tmp / "names.out").write_text(
            "MENU.C(4): Error! E9001: iVar1 is a Ghidra decompiler default "
            "name (rebuild_info/naming.md)\n", encoding="latin-1")
        d = diagnostics(tmp)
        rows.append(("a default name arrives as an error", len(d["errors"]) == 1,
                     json.dumps(d["errors"])))
    finally:
        shutil.rmtree(str(tmp), ignore_errors=True)
    return rows


def do_selftest():
    ok = True
    for name, passed, detail in _selftest_rows():
        print("[selftest] %-44s %s (%s)"
              % (name, "ok" if passed else "FAIL", detail))
        ok = ok and bool(passed)
    return ok


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", nargs="?", default="build",
                    choices=("build", "selftest"))
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if args.stage == "selftest":
        ok = do_selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    watcom = Path(os.environ.get("FDPS_WATCOM") or bm.WATCOM_DEFAULT)
    disc = Path(os.environ.get("FDPS_DISC1") or bm.DISC_DEFAULT)
    dosbox = preflight(watcom, disc)
    # Compilation never reads the disc; it is mounted when present only so the
    # build runs under the same mount definition as every other stage
    # (rebuild_info/build_pipeline.md).
    mount = disc if ce.mountable_disc(disc) else None
    ok = do_build(dosbox, watcom, mount, args.timeout)
    result = {"build_ok": ok, "diagnostics": diagnostics(),
              "exe": str(bm.find_ci(OUT, EXE) or "")}
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / "result.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    print("[result] %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
