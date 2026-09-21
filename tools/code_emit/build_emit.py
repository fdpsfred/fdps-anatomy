"""build_emit.py -- build and run the unit-test executable over the emitted C.

This is the build side of the emit pipeline: it compiles every production
source under src/ together with every test unit under tests/, links them into
one DOS/4G image with the Watcom CRT, and runs it inside DOSBox-X.  What comes
back is a structured verdict -- compiler errors, compiler warnings, unresolved
symbols, tests run, tests failed -- which is what "the emit gate" means in
rebuild_info/emit_pipeline.md.

It reuses tools/fdps_build/build_min.py wholesale (preflight, generated
conf/batch, three-signal completion, fault scan) rather than repeating it; the
only things that differ are which sources go in and what the run produces.

Wiring is derived, never hand-maintained.  Sources are whatever src/ and tests/
contain, and the runner list in the generated TESTMAIN.C is every
`run_<stem>_tests` a tests/<stem>.c actually defines.  Adding a test file is
therefore the whole of adding a test.

Sources are staged into two guest directories, C:\\SRC and C:\\TST, because a
test unit mirrors its source unit by name -- src/menu.c is covered by
tests/menu.c -- and a flat staging area would have them overwrite each other.
Their objects are kept apart for the same reason, in OBJS\\ and OBJT\\.

Subcommands:
    build    compile + link  -> workspace/code_emit/out/EMITTEST.EXE
    run      execute it under DOSBox-X and read the transcript back
    all      build then run (default)
    selftest generator and parser checks that do not need DOSBox-X

Usage: python tools/code_emit/build_emit.py [build|run|all|selftest] [--json]
Exit : 0 when every stage it ran passed, 1 otherwise.
"""
import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

sys.path.insert(0, str(ROOT / "tools" / "fdps_build"))
import build_min as bm  # noqa: E402

sys.path.insert(0, str(HERE))
import gen_stubs  # noqa: E402

SRC = ROOT / "src"
TESTS = ROOT / "tests"
GAME = ROOT / "fdps_game_files"
LIBS = ROOT / "libs" / "ailv3"

WORK = ROOT / "workspace" / "code_emit"
STAGE = WORK / "stage"
OUT = WORK / "out"
OBJ_SRC = OUT / "objs"
OBJ_TST = OUT / "objt"
OBJ_STB = OUT / "objz"
GUEST_LIB = WORK / "lib"
RUN = WORK / "run"

# How much memory the guest that RUNS the tests gets, in megabytes.  The
# toolchain's own guest keeps build_min.py's default of 32, which is what the
# DOS-hosted Watcom is known to work in; this one needs more because the test
# suite reads whole shipped containers into memory -- tests/anim.c takes all
# 27.4 MB of MISC.VFS -- and at 32 the whole executable plus that block sat so
# close to the ceiling that any growth of EMITTEST.EXE at all faulted DOS/4GW
# partway through the run.
RUN_MEMSIZE_MB = 64

EXE = "EMITTEST.EXE"
LNK = "EMITTEST.LNK"
# The second link, the one that carries the generated stub module.
LNK2 = "EMITTES2.LNK"
# The undefined symbols the first link reported: everything the emitted code
# references and nothing has defined yet.  This file is ticket 23's worklist,
# regenerated on every build so it cannot drift from the code.
UNDEF_JSON = WORK / "undefined.json"

DRV_SRC = bm.DRV_SRC
DRV_WATCOM = bm.DRV_WATCOM
DRV_WORK = bm.DRV_WORK

# Guest subdirectories under C:.  Three characters each, so every generated
# command line stays far inside COMMAND.COM's silent truncation limit.
G_SRC = "SRC"
G_TST = "TST"
G_STB = "STB"

# The AIL library the game's audio code calls into, and the seven game-side
# symbols its EXTDEFs reach back for.  tools/ail_link/link_ail.py owns the
# alias list; it is imported rather than repeated, because two copies of a
# name-mapping table is how one of them goes stale unnoticed
# (rebuild_info/ail_link.md).
sys.path.insert(0, str(ROOT / "tools" / "ail_link"))
import link_ail  # noqa: E402

AIL_LIB = LIBS / "ailv3.lib"
AIL_HDR = LIBS / "ailv3.h"

# Real game files a test asks for, one 8.3 name per line, '#' comments allowed.
# They are copied next to the executable (which is the guest's cwd) only when
# absent or a different size, so a run does not recopy tens of megabytes.
# Tests that parse game data must read the real file: a fabricated stand-in
# proves the parser agrees with the fabrication and nothing else.
GAMEFILE_LIST = TESTS / "gamefile.lst"

RUNNER_RX = re.compile(r"^\s*void\s+run_([A-Za-z0-9_]+)_tests\s*\(\s*void\s*\)",
                       re.M)


# --------------------------------------------------------------- source scan

def c_sources(directory):
    return sorted((p for p in directory.glob("*.c")), key=lambda p: p.name.lower())


def asm_sources(directory):
    return sorted((p for p in directory.glob("*.asm")), key=lambda p: p.name.lower())


def headers(directory):
    return sorted((p for p in directory.glob("*.h")), key=lambda p: p.name.lower())


# ------------------------------------------------------- decompiler names
#
# The rule these enforce is rebuild_info/naming.md: no Ghidra decompiler
# default name survives into src/.  Only the mechanical half lives here.
# Whether a name that is not a default actually says what the value is can only
# be judged by someone who has read the assembly, and that is the reviewer's
# item 13; asking a regex about it would produce confident nonsense.  Putting
# the cheap half in the build instead of in the review is deliberate: the
# emitter runs this itself, so it finds out in seconds rather than after a full
# review round, and the reviewer's expensive attention is spent only on the
# half a machine cannot answer.

DEFAULT_NAME_RX = re.compile(
    # iVar1 / puVar3 / auVar2, and uStack_8 / aiStack_20.  No trailing \b: the
    # half measure `iVar1_index` is not a name either, and it has to match.
    r"\b[a-z]{1,4}Var[0-9]+"
    r"|\b[a-z]{1,4}Stack_[0-9a-f]+"
    r"|\bparam_[0-9]+"
    # Hex digits only, so a hand-written local_count is not a false positive.
    r"|\blocal_(?:res)?[0-9a-f]+\b"
    r"|\bin_(?:[A-Z]{2,3}|stack_[0-9a-f]+|FS_OFFSET|GS_OFFSET)\b"
    r"|\bunaff_\w+"
    r"|\bextraout_\w+"
    r"|\buRam[0-9a-f]+\b"
    r"|\b_?(?:DAT|PTR|FUN|LAB|UNK|SUB)_[0-9a-fA-F]{4,}"
    # The decompiler's own pseudo-operations, which are not C at all.
    r"|\b(?:CONCAT|SUB|ZEXT|SEXT)[0-9]{2}\b"
    r"|__return_storage_ptr__")


def strip_c(text):
    """Blank out comments and literals, keeping every line number intact.

    A plate comment is allowed to quote the decompiler -- "Ghidra calls this
    iVar1, it is the tile index" is exactly the kind of note worth writing --
    and so is a string literal that happens to contain one.  Scanning the raw
    file would make the check punish the explanation for naming the thing it
    explains.  Newlines are preserved rather than the text deleted so the line
    number in the message points at the real line.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("\n" * text.count("\n", i, end))
            i = end
        elif text[i] in "\"'":
            quote, j = text[i], i + 1
            while j < n and text[j] != quote:
                j += 2 if text[j] == "\\" else 1
            out.append("\n" * text.count("\n", i, min(j + 1, n)))
            i = min(j + 1, n)
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def default_names(paths):
    """[(path, line number, the offending text)] for every default name found."""
    found = []
    for path in paths:
        text = strip_c(path.read_text(encoding="utf-8", errors="replace"))
        for lineno, line in enumerate(text.splitlines(), 1):
            for m in DEFAULT_NAME_RX.finditer(line):
                found.append((path, lineno, m.group(0)))
    return found


def name_report(found):
    """Findings shaped like the compiler's own diagnostics.

    Wearing Watcom's format is not decoration: diagnostics() collects anything
    saying "Error!" out of the build transcripts, and the gate counts those, so
    a finding written this way is reported and gated through the path that
    already exists instead of through a second one that would have to be kept
    in step with it.
    """
    return "".join(
        "%s(%d): Error! E9001: %s is a Ghidra decompiler default name "
        "(rebuild_info/naming.md)\n" % (path.name.upper(), lineno, name)
        for path, lineno, name in found)


def runners(test_files):
    """The `run_<stem>_tests` each test unit defines, in file order.

    A tests/ unit that defines none is a support unit -- the harness itself,
    shared fixtures, stub globals -- and is compiled but not called.  Requiring
    the definition to match the file's own stem keeps the mapping one to one:
    a runner cannot be silently registered from the wrong file.
    """
    found = []
    for path in test_files:
        stem = path.stem.lower()
        text = path.read_text(encoding="utf-8", errors="replace")
        if any(m.group(1).lower() == stem for m in RUNNER_RX.finditer(text)):
            found.append("run_%s_tests" % stem)
    return found


def gen_testmain(names):
    """The generated entry point: declare each runner, call them in order.

    Nothing here is checked into the repository -- it is written into the
    staging area at build time, so it can never drift from what tests/ holds.
    """
    lines = ["/* GENERATED by tools/code_emit/build_emit.py -- do not edit.",
             " * Regenerated from tests/ on every build. */",
             '#include "testharn.h"',
             ""]
    lines += ["extern void %s(void);" % n for n in names]
    lines += ["",
              "int main(void)",
              "{",
              "    test_start();"]
    lines += ["    %s();" % n for n in names]
    lines += ["    test_report();",
              "    return test_failed == 0 ? 0 : 1;",
              "}",
              ""]
    return "\n".join(lines)


def wanted_gamefiles():
    if not GAMEFILE_LIST.is_file():
        return []
    out = []
    for line in GAMEFILE_LIST.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.append(line.upper())
    return out


# ------------------------------------------------------------------ preflight

def mountable_disc(disc):
    """The cue AND the .bin it names -- a dangling cue mounts as nothing."""
    if not disc.is_file():
        return False
    try:
        return bm.cue_bin(disc).is_file()
    except (SystemExit, OSError):
        return False


def preflight_extra(watcom, need_asm):
    if not TESTS.is_dir():
        bm.fail("missing %s -- the test units are the gate's only assertions"
                % TESTS)
    if bm.find_ci(TESTS, "testharn.c") is None:
        bm.fail("missing %s" % (TESTS / "testharn.c"))
    if need_asm and not any((watcom / d / "WASM.EXE").is_file()
                            for d in bm.BINDIRS):
        bm.fail("WASM.EXE not found in %s under %s -- src/ has assembly sources"
                % ("/".join(bm.BINDIRS), watcom))
    for name in wanted_gamefiles():
        if bm.find_ci(GAME, name) is None:
            bm.fail("%s lists %s, which is not in %s"
                    % (GAMEFILE_LIST.name, name, GAME))
    preflight_ail()


# ---------------------------------------------------------------- generation

def gen_lnk(src_objs, tst_objs, with_stubs=False):
    """system dos4g pulls in no CRT of its own and wlink's 4K default stack is
    not enough for the harness, so both are spelled out -- same as every other
    build target in this project (rebuild_info/build_flags.md).

    `with_stubs` is the difference between the two links of a build.  The first
    one deliberately has no stub module, so that what it cannot resolve is
    exactly what the rebuild is still missing; the second adds the generated
    module and is the one that has to come out clean.
    """
    w = DRV_WORK
    lines = ["system dos4g",
             r"name %s:\OUT\%s" % (w, EXE),
             "option stack=8k"]
    lines += ["alias %s=%s" % (a, b) for a, b in link_ail.ALIASES]
    lines += [r"file %s:\OUT\OBJS\%s.OBJ" % (w, s) for s in src_objs]
    lines += [r"file %s:\OUT\OBJT\%s.OBJ" % (w, s) for s in tst_objs]
    if with_stubs:
        lines.append(r"file %s:\OUT\OBJZ\STUBS.OBJ" % w)
    lines.append(r"library %s:\LIB\AILV3.LIB" % w)
    lines += ["library %s" % lib for lib in bm.CRT_LIBS]
    return "\n".join(lines) + "\n"


# The unit that defines the game's C entry point.  src/main.c defines `main`
# because the Watcom CRT's __CMain calls exactly that symbol, and the generated
# TESTMAIN.C defines `main` too, because it is the test image's entry.  Two
# definitions of one symbol are a link error, so in THIS image only the game's
# entry is renamed away at compile time with a command-line define.  No test
# calls it (it would exit or never return), src/ carries no test hook
# (ADR-0003), and a build of the game itself compiles the unit without the
# define.  Same answer as the FD2 rebuild's tests/genbuild.py.
GAME_ENTRY_UNIT = "MAIN"
GAME_ENTRY_TEST_NAME = "fdps_game_main"


def gen_build_bat(src_c, src_asm, tst_c):
    """Compile every unit, then link.  A heartbeat naming the current unit is
    written before each step, so a compiler that wedges is reported as hung on
    that file instead of being padded out to the timeout."""
    w = DRV_WORK
    steps = []
    for stem in src_c:
        extra = (" -dmain=%s" % GAME_ENTRY_TEST_NAME
                 if stem.upper() == GAME_ENTRY_UNIT else "")
        steps.append((stem.lower(),
                      r"WCC386 %s\%s.C%s -fo=%s:\OUT\OBJS\%s.OBJ >> %s:\OUT\BUILD.OUT"
                      % (G_SRC, stem, extra, w, stem, w)))
    for stem in src_asm:
        steps.append((stem.lower(),
                      r"WASM %s\%s.ASM -fo=%s:\OUT\OBJS\%s.OBJ >> %s:\OUT\BUILD.OUT"
                      % (G_SRC, stem, w, stem, w)))
    for stem in tst_c:
        steps.append(("t_" + stem.lower(),
                      r"WCC386 %s\%s.C -fo=%s:\OUT\OBJT\%s.OBJ >> %s:\OUT\BUILD.OUT"
                      % (G_TST, stem, w, stem, w)))
    steps.append(("link", r"WLINK @%s:\%s >> %s:\OUT\BUILD.OUT" % (w, LNK, w)))

    out = []
    for label, cmd in steps:
        out.append(r"echo %s > %s:\OUT\HB.TXT" % (label, w))
        out.append(r"echo === %s === >> %s:\OUT\BUILD.OUT" % (label, w))
        out.append(cmd)
    out.append(r"echo done > %s:\OUT\HB.TXT" % w)
    out.append(r"echo done > %s:\OUT\BUILD.DON" % w)
    return "\r\n".join(out) + "\r\n"


def gen_stub_bat():
    """The second pass: compile the generated stub module and link again.

    Nothing else is recompiled.  The objects from the first pass are still on
    disk and none of their sources changed between the two links -- the only
    new input is the stub module, which was generated from what the first link
    complained about.
    """
    w = DRV_WORK
    out = []
    for label, cmd in (
            ("stubs", r"WCC386 %s\STUBS.C -fo=%s:\OUT\OBJZ\STUBS.OBJ >> %s:\OUT\BUILD2.OUT"
             % (G_STB, w, w)),
            ("link2", r"WLINK @%s:\%s >> %s:\OUT\BUILD2.OUT" % (w, LNK2, w))):
        out.append(r"echo %s > %s:\OUT\HB.TXT" % (label, w))
        out.append(r"echo === %s === >> %s:\OUT\BUILD2.OUT" % (label, w))
        out.append(cmd)
    out.append(r"echo done > %s:\OUT\HB.TXT" % w)
    out.append(r"echo done > %s:\OUT\BUILD2.DON" % w)
    return "\r\n".join(out) + "\r\n"


def stage_sources(test_files):
    """Copy sources into the guest tree under their 8.3 upper-case names.

    The staging area is rebuilt from scratch each time.  Reusing it would let a
    source file that was deleted or renamed in the repository go on being
    compiled from a stale copy, which is exactly the kind of drift the gate is
    supposed to catch rather than cause.
    """
    if STAGE.is_dir():
        shutil.rmtree(str(STAGE))
    (STAGE / G_SRC).mkdir(parents=True)
    (STAGE / G_TST).mkdir(parents=True)
    (STAGE / G_STB).mkdir(parents=True)
    for path in c_sources(SRC) + asm_sources(SRC) + headers(SRC):
        shutil.copy2(path, STAGE / G_SRC / path.name.upper())
    # The AIL header goes in with the sources so an emitted audio unit can
    # include it by plain name, exactly as tools/ail_link stages it.
    shutil.copy2(AIL_HDR, STAGE / G_SRC / AIL_HDR.name.upper())
    for path in test_files + headers(TESTS):
        shutil.copy2(path, STAGE / G_TST / path.name.upper())
    names = runners(test_files)
    (STAGE / G_TST / "TESTMAIN.C").write_text(gen_testmain(names),
                                              encoding="latin-1")
    return names


# --------------------------------------------------------------------- build

def preflight_ail():
    for path in (AIL_LIB, AIL_HDR):
        if not path.is_file():
            bm.fail("missing %s -- the emitted game code links against the AIL "
                    "library (rebuild_info/ail_link.md)" % path)


def stage_ail_lib():
    GUEST_LIB.mkdir(parents=True, exist_ok=True)
    dst = GUEST_LIB / "AILV3.LIB"
    if not dst.is_file() or dst.stat().st_size != AIL_LIB.stat().st_size:
        shutil.copy2(AIL_LIB, dst)


def do_build(dosbox, watcom, disc, timeout, quiet=False):
    OBJ_SRC.mkdir(parents=True, exist_ok=True)
    OBJ_TST.mkdir(parents=True, exist_ok=True)
    OBJ_STB.mkdir(parents=True, exist_ok=True)
    for parent, name in ((OUT, EXE), (OUT, "BUILD.DON"), (OUT, "BUILD.OUT"),
                         (OUT, "BUILD2.DON"), (OUT, "BUILD2.OUT"),
                         (OUT, "HB.TXT"), (OUT, "NAMES.OUT"),
                         (WORK, "dosbox.log"), (WORK, "stdio.log")):
        p = bm.find_ci(parent, name)
        if p is not None:
            p.unlink()

    # Before DOSBox is started at all: this costs milliseconds and the build
    # costs minutes, and a source carrying decompiler names is going to be
    # rewritten whatever the compiler thinks of it.
    found = default_names(c_sources(SRC) + headers(SRC)
                          + c_sources(TESTS) + headers(TESTS))
    if found:
        report = name_report(found)
        (OUT / "NAMES.OUT").write_text(report, encoding="latin-1")
        if not quiet:
            print("[build] FAIL: %d Ghidra decompiler default name(s) in the "
                  "sources" % len(found))
            print("".join("  | " + l for l in report.splitlines(True)[:25]))
        return False
    # Stale objects would let a source that no longer compiles still link.
    for objdir in (OBJ_SRC, OBJ_TST, OBJ_STB):
        for p in objdir.iterdir() if objdir.is_dir() else []:
            if p.is_file():
                p.unlink()
    stage_ail_lib()

    test_files = c_sources(TESTS)
    names = stage_sources(test_files)
    src_c = [p.stem.upper() for p in c_sources(SRC)]
    src_asm = [p.stem.upper() for p in asm_sources(SRC)]
    tst_c = [p.stem.upper() for p in test_files] + ["TESTMAIN"]

    (WORK / LNK).write_text(gen_lnk(src_c + src_asm, tst_c), encoding="latin-1")
    (WORK / "BUILD.BAT").write_text(gen_build_bat(src_c, src_asm, tst_c),
                                    encoding="latin-1")
    bm.write_conf(
        WORK / "build.conf",
        [(DRV_SRC, STAGE), (DRV_WATCOM, watcom), (DRV_WORK, WORK)],
        disc,
        ["set WATCOM=%s:\\" % DRV_WATCOM,
         "set PATH=Z:\\;" + ";".join("%s:\\%s" % (DRV_WATCOM, d)
                                     for d in bm.BINDIRS),
         # The staged directories are on the include path so a test unit can
         # include the header of the source unit it covers by plain name.
         "set INCLUDE=%s:\\H;%s:\\%s;%s:\\%s"
         % (DRV_WATCOM, DRV_SRC, G_SRC, DRV_SRC, G_TST),
         "set WCC386=" + bm.CFLAGS,
         "%s:" % DRV_SRC,
         r"%s:\BUILD.BAT" % DRV_WORK],
        logfile=WORK / "dosbox.log",
    )

    if not quiet:
        print("[build] %d src unit(s), %d test unit(s), %d runner(s): %s"
              % (len(src_c) + len(src_asm), len(tst_c) - 1, len(names),
                 ", ".join(names) or "none"))
    proc, fp = bm.launch(dosbox, WORK / "build.conf", WORK / "stdio.log")
    mode, hb, secs = bm.wait(proc, OUT, "build.don", timeout)
    fp.close()

    bout = bm.find_ci(OUT, "build.out")
    txt = bout.read_text(encoding="latin-1", errors="replace") if bout else ""
    syms = bm.parse_undefined(txt)
    fault = bm.scan_fault(WORK / "dosbox.log", WORK / "stdio.log")

    if not quiet:
        print("[build] %s in %ds (last step: %s)" % (mode, secs, hb or "-"))
        if fault:
            print("[build] protected-mode fault in log:\n%s" % fault)
        if txt:
            print("\n".join("  | " + l for l in txt.splitlines()[-25:]))
    if mode != "completed":
        # An image on disk is not proof the build finished: wlink can have
        # written it and the guest then wedged, leaving a truncated BUILD.OUT
        # that reads as clean.
        if not quiet:
            print("[build] FAIL: no completion marker (mode=%s)" % mode)
        return False
    if fault:
        return False

    if not stub_pass(dosbox, watcom, disc, timeout, syms, src_c + src_asm,
                     tst_c, quiet):
        return False

    exe = bm.find_ci(OUT, EXE)
    if exe is None:
        if not quiet:
            print("[build] FAIL: %s not produced" % EXE)
        return False
    if not quiet:
        print("[build] %s %d bytes -> %s" % (EXE, exe.stat().st_size, exe))
    return True


def record_undefined(syms, resolved, unresolved):
    """Land the first link's complaint as ticket 23's worklist."""
    by_symbol = {r["symbol"]: r for r in resolved}
    WORK.mkdir(parents=True, exist_ok=True)
    UNDEF_JSON.write_text(json.dumps({
        "_doc": "Symbols the emitted code references and nothing defines yet, "
                "as reported by the first of the build's two links. This is "
                "ticket 23's authoritative worklist: it is regenerated on "
                "every build, so it shrinks as real definitions land and can "
                "never disagree with the code. The second link fills these in "
                "with the zero-filled module tools/code_emit/gen_stubs.py "
                "writes; nothing here is checked into src/.",
        "count": len(syms),
        "symbols": [dict(by_symbol.get(s, {"symbol": s}),
                         symbol=s) for s in syms],
        "unresolved": unresolved,
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def stub_pass(dosbox, watcom, disc, timeout, syms, src_objs, tst_objs, quiet):
    """The second link: fill the first link's holes with generated stubs.

    Returns True when the program is fully linked -- either because the first
    link already resolved everything, or because the stub module closed the
    gap.  A symbol that must not be stubbed (rebuild_info/emit_pipeline.md)
    fails the build here rather than being papered over.
    """
    text, resolved, unresolved = gen_stubs.plan(syms, src_dir=SRC)
    record_undefined(syms, resolved, unresolved)

    if not syms:
        if not quiet:
            print("[build] first link resolved everything; no stubs needed")
        return True
    if not quiet:
        data = sum(1 for r in resolved if r["kind"] == "data")
        fns = sum(1 for r in resolved if r["kind"] == "function")
        print("[build] first link left %d undefined symbol(s): %d global(s) "
              "for ticket 23, %d function(s) not emitted yet"
              % (len(syms), data, fns))
    if unresolved:
        print("[build] FAIL: %d symbol(s) must not be stubbed:" % len(unresolved))
        for row in unresolved:
            print("  | %s -- %s" % (row["symbol"], row["why"]))
        return False

    (STAGE / G_STB / "STUBS.C").write_text(text, encoding="latin-1")
    (WORK / LNK2).write_text(gen_lnk(src_objs, tst_objs, with_stubs=True),
                             encoding="latin-1")
    (WORK / "BUILD2.BAT").write_text(gen_stub_bat(), encoding="latin-1")
    # The image from the first link is deleted so that its presence cannot be
    # mistaken for the second link's output.
    for name in (EXE,):
        p = bm.find_ci(OUT, name)
        if p is not None:
            p.unlink()
    bm.write_conf(
        WORK / "build2.conf",
        [(DRV_SRC, STAGE), (DRV_WATCOM, watcom), (DRV_WORK, WORK)],
        disc,
        ["set WATCOM=%s:\\" % DRV_WATCOM,
         "set PATH=Z:\\;" + ";".join("%s:\\%s" % (DRV_WATCOM, d)
                                     for d in bm.BINDIRS),
         "set INCLUDE=%s:\\H;%s:\\%s;%s:\\%s"
         % (DRV_WATCOM, DRV_SRC, G_SRC, DRV_SRC, G_TST),
         "set WCC386=" + bm.CFLAGS,
         "%s:" % DRV_SRC,
         r"%s:\BUILD2.BAT" % DRV_WORK],
        logfile=WORK / "dosbox2.log",
    )
    proc, fp = bm.launch(dosbox, WORK / "build2.conf", WORK / "stdio2.log")
    mode, hb, secs = bm.wait(proc, OUT, "build2.don", timeout)
    fp.close()

    bout = bm.find_ci(OUT, "build2.out")
    txt = bout.read_text(encoding="latin-1", errors="replace") if bout else ""
    left = bm.parse_undefined(txt)
    fault = bm.scan_fault(WORK / "dosbox2.log", WORK / "stdio2.log")
    if not quiet:
        print("[build] stub link %s in %ds (last step: %s)"
              % (mode, secs, hb or "-"))
        if txt:
            print("\n".join("  | " + l for l in txt.splitlines()[-15:]))
        if left:
            print("[build] still undefined after stubs: %s" % ", ".join(left))
    if mode != "completed":
        if not quiet:
            print("[build] FAIL: stub link produced no completion marker "
                  "(mode=%s)" % mode)
        return False
    return not left and not fault


# ----------------------------------------------------------------------- run

def parse_transcript(text):
    """Read TEST.OUT back: how many checks ran, how many failed, which ones."""
    total = failed = None
    verdict = None
    failures = []
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r"total=(\d+)\s+failed=(\d+)$", line)
        if m:
            total, failed = int(m.group(1)), int(m.group(2))
            continue
        if line.startswith("verdict="):
            verdict = line.split("=", 1)[1]
            continue
        if line.startswith("FAIL "):
            failures.append(line)
    return {"total": total, "failed": failed, "verdict": verdict,
            "failures": failures}


def stage_gamefiles():
    """Copy the real game files the tests asked for next to the executable."""
    staged = []
    for name in wanted_gamefiles():
        srcf = bm.find_ci(GAME, name)
        dst = RUN / name
        if not dst.is_file() or dst.stat().st_size != srcf.stat().st_size:
            shutil.copy2(srcf, dst)
        staged.append(name)
    return staged


def do_run(dosbox, watcom, disc, timeout, quiet=False):
    exe = bm.find_ci(OUT, EXE)
    if exe is None:
        print("[run] FAIL: no %s -- build first" % EXE)
        return False, {}
    RUN.mkdir(parents=True, exist_ok=True)
    for name in ("TEST.OUT", "RUN.DON", "HB.TXT", "dosbox.log", "stdio.log"):
        p = bm.find_ci(RUN, name)
        if p is not None:
            p.unlink()
    shutil.copy2(exe, RUN / EXE)
    shutil.copy2(watcom / "BIN" / "DOS4GW.EXE", RUN / "DOS4GW.EXE")
    staged = stage_gamefiles()

    bm.write_conf(
        RUN / "run.conf",
        [(DRV_SRC, RUN), (DRV_WATCOM, watcom)],
        disc,
        ["set PATH=Z:\\;%s:\\;%s:\\BIN" % (DRV_SRC, DRV_WATCOM),
         "%s:" % DRV_SRC,
         EXE],
        logfile=RUN / "dosbox.log",
        memsize=RUN_MEMSIZE_MB,
    )

    if not quiet:
        print("[run] executing %s under DOS/4GW%s"
              % (EXE, (" (staged %s)" % ", ".join(staged)) if staged else ""))
    proc, fp = bm.launch(dosbox, RUN / "run.conf", RUN / "stdio.log")
    mode, hb, secs = bm.wait(proc, RUN, "run.don", timeout)
    fp.close()

    fault = bm.scan_fault(RUN / "dosbox.log", RUN / "stdio.log")
    res = bm.find_ci(RUN, "test.out")
    text = res.read_text(encoding="latin-1", errors="replace") if res else ""
    parsed = parse_transcript(text)
    parsed["mode"] = mode
    parsed["hung_on"] = hb if mode == "stall" else None
    parsed["fault"] = fault

    if not quiet:
        print("[run] %s in %ds (last test: %s)" % (mode, secs, hb or "-"))
        if fault:
            print("[run] protected-mode fault in log:\n%s" % fault)
        for f in parsed["failures"]:
            print("  | " + f)
        print("[run] %s checks, %s failed, verdict=%s"
              % (parsed["total"], parsed["failed"], parsed["verdict"]))

    ok = (mode == "completed" and not fault
          and parsed["verdict"] == "ok" and parsed["failed"] == 0)
    if mode != "completed" and not quiet:
        print("[run] FAIL: no completion marker (mode=%s%s)"
              % (mode, ", hung on %s" % hb if mode == "stall" else ""))
    return ok, parsed


# ------------------------------------------------------------------- verdict

# wcc386 writes "Warning! W107: ...", wlink writes its number in parentheses
# instead.  Matching only the compiler's spelling made every linker warning
# invisible to this verdict, which is how a redefinition once passed the build.
WARNING_RX = re.compile(r"\bWarning[!(]", re.I)
REDEFINITION_RX = re.compile(r"redefinition of .+ ignored", re.I)
# The first link is supposed to name every symbol ticket 23 has not emitted
# yet; it reports each one as a warning as well as an error, and both are
# dropped for the same reason.  What must be empty is the SECOND link's
# undefined list, which is gated separately.
UNDEF_REF_RX = re.compile(r"is an undefined reference", re.I)


def diagnostics(out_dir=None):
    """The compiler's and linker's own words, across both of the build's links.

    Errors and warnings are collected from both passes.  Undefined symbols are
    read from the LAST link only: the first link is expected to report the
    symbols the rebuild has not reached yet, and that expected list is the
    point of it, so counting those as gate failures would keep the gate red
    until the very last function of ticket 23 lands.  What must be empty is
    what the second link, the one carrying the stubs, still cannot resolve.

    NAMES.OUT is read the same way.  The decompiler-name check runs before the
    compiler does and writes its findings in the compiler's format, so they are
    reported and gated as ordinary errors rather than through a parallel
    channel that would have to be kept in step with this one.
    """
    out_dir = out_dir or OUT
    first = bm.find_ci(out_dir, "build.out")
    second = bm.find_ci(out_dir, "build2.out")
    names = bm.find_ci(out_dir, "names.out")
    txt1 = first.read_text(encoding="latin-1", errors="replace") if first else ""
    txt2 = second.read_text(encoding="latin-1", errors="replace") if second else ""
    txt0 = names.read_text(encoding="latin-1", errors="replace") if names else ""
    lines = [l.strip() for l in (txt0 + "\n" + txt1 + "\n" + txt2).splitlines()]
    # An "Error!" naming an undefined symbol from the first link is that same
    # expected list wearing the linker's error prefix; it is dropped here for
    # the same reason and reported through undefined.json instead.
    first_undef = set(bm.parse_undefined(txt1))

    def expected_undef(line):
        return any(sym in line for sym in first_undef) and "undefined" in line

    errors = [l for l in lines if "Error!" in l and not expected_undef(l)]
    # A redefinition the linker resolved by dropping one of the definitions is
    # reported as an ordinary warning, and it is not one: it means the object
    # carries somebody else's body under the name just emitted, so the build
    # passes while the code under review is not in the executable at all.  Seen
    # for real on 0003cb01/0003cb93, where an older hand-written stand-in in
    # src/aildpmi.c won the link and the newly emitted definitions were dead.
    # Promoted to an error so no baseline can ever accept it.
    errors += [l for l in lines if REDEFINITION_RX.search(l)]
    return {"errors": errors,
            "warnings": [l for l in lines if WARNING_RX.search(l)
                         and not REDEFINITION_RX.search(l)
                         and not UNDEF_REF_RX.search(l)],
            "undefined": bm.parse_undefined(txt2) if second else
                         bm.parse_undefined(txt1),
            "stubbed": sorted(first_undef)}


def write_result(result):
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / "result.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")


# ----------------------------------------------------------------- selftest

def _selftest_rows():
    """Prove the derived wiring and the transcript parser can each be wrong.

    A generator that silently registered nothing, or a parser that read a
    failing run as clean, would make this whole gate report PASS on broken
    code -- which is the one failure mode a gate must not have.
    """
    import tempfile
    rows = []
    tmp = Path(tempfile.mkdtemp(prefix="fdps_emit_selftest_"))
    try:
        (tmp / "menu.c").write_text(
            "#include \"testharn.h\"\n"
            "static void t_one(void) { CHECK_EQ(1, 1); }\n"
            "void run_menu_tests(void)\n{\n    RUN_TEST(t_one);\n}\n",
            encoding="utf-8")
        # Right shape, wrong stem: must NOT be registered, or a runner could be
        # picked up from a file that does not own it.
        (tmp / "other.c").write_text(
            "void run_menu_tests_again(void) {}\n"
            "void run_wrong_tests(void)\n{\n}\n", encoding="utf-8")
        # A support unit with no runner at all.
        (tmp / "testharn.c").write_text("int test_total = 0;\n",
                                        encoding="utf-8")
        found = runners(c_sources(tmp))
        rows.append(("runner found", found == ["run_menu_tests"],
                     ", ".join(found) or "none"))

        main = gen_testmain(found)
        rows.append(("generated main calls it", "run_menu_tests();" in main,
                     "yes" if "run_menu_tests();" in main else "no"))
        rows.append(("generated main reports", "test_report();" in main,
                     "yes" if "test_report();" in main else "no"))
        empty = gen_testmain([])
        rows.append(("empty suite still links", "int main(void)" in empty,
                     "yes" if "int main(void)" in empty else "no"))
    finally:
        shutil.rmtree(str(tmp), ignore_errors=True)

    good = parse_transcript("=== run_menu_tests\ntotal=3 failed=0\nverdict=ok\n")
    rows.append(("clean transcript read",
                 good["total"] == 3 and good["failed"] == 0
                 and good["verdict"] == "ok", json.dumps(good)))
    bad = parse_transcript("=== run_menu_tests\n"
                           "FAIL t_all_disabled check 1: f(d) -> 1, expected -1\n"
                           "total=3 failed=1\nverdict=fail\n")
    rows.append(("failing transcript read",
                 bad["failed"] == 1 and bad["verdict"] == "fail"
                 and len(bad["failures"]) == 1, json.dumps(bad)))
    truncated = parse_transcript("=== run_menu_tests\n")
    rows.append(("truncated transcript is not a pass",
                 truncated["verdict"] is None and truncated["failed"] is None,
                 json.dumps(truncated)))

    bat = gen_build_bat(["AILDPMI"], ["AILFLAGS"], ["MENU", "TESTMAIN"])
    rows.append(("src and test objects kept apart",
                 r"OBJS\AILDPMI.OBJ" in bat and r"OBJT\MENU.OBJ" in bat,
                 "yes"))
    rows.append(("other units compile without the entry rename",
                 "-dmain=" not in bat, "yes"))
    bat_main = gen_build_bat(["MAIN", "MENU"], [], ["TESTMAIN"])
    main_lines = [l for l in bat_main.splitlines() if "WCC386 SRC\\" in l]
    rows.append(("game entry renamed away in the test image only",
                 len([l for l in main_lines if "-dmain=" in l]) == 1
                 and r"SRC\MAIN.C -dmain=%s" % GAME_ENTRY_TEST_NAME in bat_main,
                 "; ".join(main_lines)))
    rows.append(("assembly assembled with wasm",
                 r"WASM SRC\AILFLAGS.ASM" in bat, "yes"))
    longest = max(len(l) for l in bat.splitlines())
    # COMMAND.COM truncates past ~176 characters after expansion, silently.
    rows.append(("batch lines stay short", longest < 120, "%d chars" % longest))

    lnk = gen_lnk(["AILDPMI"], ["MENU"])
    rows.append(("link file lists both trees",
                 r"OBJS\AILDPMI.OBJ" in lnk and r"OBJT\MENU.OBJ" in lnk, "yes"))
    rows.append(("first link carries no stub module",
                 "STUBS.OBJ" not in lnk, "yes"))
    rows.append(("AIL library and its aliases are linked",
                 "AILV3.LIB" in lnk
                 and "alias fd2_dpmi_lock_size=fdps_dpmi_lock_size" in lnk
                 and len([l for l in lnk.splitlines()
                          if l.startswith("alias ")]) == len(link_ail.ALIASES),
                 "%d alias(es)" % len(link_ail.ALIASES)))
    lnk2 = gen_lnk(["AILDPMI"], ["MENU"], with_stubs=True)
    rows.append(("second link carries the stub module",
                 r"OBJZ\STUBS.OBJ" in lnk2, "yes"))

    bat2 = gen_stub_bat()
    rows.append(("stub pass compiles only the stubs",
                 bat2.count("WCC386") == 1 and "STUBS.C" in bat2
                 and "WLINK" in bat2, "yes"))
    rows.append(("stub pass writes its own transcript and marker",
                 "BUILD2.OUT" in bat2 and "BUILD2.DON" in bat2, "yes"))

    # The gate must not go red just because the rebuild has not reached a
    # symbol yet -- that is every build until the last function of ticket 23.
    # It must go red the moment the second link cannot resolve something.
    import tempfile as _tempfile
    tmp = Path(_tempfile.mkdtemp(prefix="fdps_diag_selftest_"))
    try:
        (tmp / "build.out").write_text(
            "Error! E2028: data_fdps_thing is an undefined reference\n",
            encoding="latin-1")
        (tmp / "build2.out").write_text("creating a DOS/4G executable\n",
                                        encoding="latin-1")
        d = diagnostics(tmp)
        rows.append(("expected undefined does not fail the gate",
                     not d["errors"] and not d["undefined"]
                     and d["stubbed"] == ["data_fdps_thing"], json.dumps(d)))
        (tmp / "build2.out").write_text(
            "Error! E2028: mystery_symbol is an undefined reference\n",
            encoding="latin-1")
        d = diagnostics(tmp)
        rows.append(("undefined after stubs does fail the gate",
                     d["undefined"] == ["mystery_symbol"], json.dumps(d)))

        # A finding written in the compiler's format has to arrive as an
        # error, or the check runs and the gate stays green anyway.
        (tmp / "names.out").write_text(
            "AITARGET.C(41): Error! E9001: iVar1 is a Ghidra decompiler "
            "default name (rebuild_info/naming.md)\n", encoding="latin-1")
        (tmp / "build2.out").write_text("creating a DOS/4G executable\n",
                                        encoding="latin-1")
        d = diagnostics(tmp)
        rows.append(("a default name reaches the gate as an error",
                     len(d["errors"]) == 1 and "E9001" in d["errors"][0],
                     json.dumps(d["errors"])))
    finally:
        shutil.rmtree(str(tmp), ignore_errors=True)

    rows += _selftest_default_names()
    return rows


def _selftest_default_names():
    """The scanner has to be wrong in both directions before it is trusted.

    A check that flags nothing passes every file, and a check that flags the
    plate comment explaining a name makes the honest note the expensive one to
    write.  Both are worse than no check, so both are pinned here.
    """
    import tempfile as _tempfile
    rows = []
    tmp = Path(_tempfile.mkdtemp(prefix="fdps_names_selftest_"))
    try:
        def scan(body):
            p = tmp / "probe.c"
            p.write_text(body, encoding="utf-8")
            return [name for _, _, name in default_names([p])]

        for label, body, expect in (
            ("iVar1 is caught", "int f(void){int iVar1; return iVar1;}", True),
            ("a pointer default is caught",
             "void f(char *p){char *puVar3 = p;}", True),
            ("param_1 is caught", "int f(int param_1){return param_1;}", True),
            ("local_1c is caught", "void f(void){int local_1c;}", True),
            ("in_EAX is caught", "int f(void){return in_EAX;}", True),
            ("DAT_ is caught", "int f(void){return DAT_00069cd8;}", True),
            ("CONCAT44 is caught", "void f(void){x = CONCAT44(a,b);}", True),
            # The half measure the ticket names explicitly.
            ("iVar1_index is still a default name",
             "void f(void){int iVar1_index;}", True),
            # Real names that share a prefix with a default one.
            ("index survives", "void f(void){int index;}", False),
            ("local_count survives", "void f(void){int local_count;}", False),
            ("input survives", "void f(void){int input;}", False),
            ("a name in a comment survives",
             "/* Ghidra calls this iVar1; it is the tile index. */\n"
             "void f(void){int tile_index;}", False),
            ("a name in a string survives",
             'void f(void){puts(\"param_1\");}', False),
        ):
            hits = scan(body)
            rows.append(("names: " + label, bool(hits) == expect,
                         ", ".join(hits) or "none"))

        # The line number has to point at the real line, which is the whole
        # reason strip_c blanks comments instead of deleting them.
        p = tmp / "probe.c"
        p.write_text("/* a\n   multi\n   line note */\nint f(void){int iVar1;}\n",
                     encoding="utf-8")
        found = default_names([p])
        rows.append(("names: line number survives a stripped comment",
                     len(found) == 1 and found[0][1] == 4,
                     str(found[0][1]) if found else "no finding"))
    finally:
        shutil.rmtree(str(tmp), ignore_errors=True)
    return rows


def do_selftest():
    rows = _selftest_rows()
    ok = True
    for name, passed, detail in rows:
        print("[selftest] %-34s %s (%s)"
              % (name, "ok" if passed else "FAIL", detail))
        ok = ok and passed
    return ok


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", nargs="?", default="all",
                    choices=("build", "run", "all", "selftest"))
    ap.add_argument("--timeout", type=int, default=1800,
                    help="backstop seconds per stage; the heartbeat normally "
                         "ends a hung stage long before this, so this only "
                         "has to sit above the healthy run of the whole "
                         "suite, which grows with every emitted function")
    ap.add_argument("--json", action="store_true",
                    help="print the structured verdict on stdout as well")
    args = ap.parse_args()

    watcom = Path(os.environ.get("FDPS_WATCOM") or bm.WATCOM_DEFAULT)
    disc = Path(os.environ.get("FDPS_DISC1") or bm.DISC_DEFAULT)

    if args.stage == "selftest":
        ok = do_selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    dosbox = bm.preflight(watcom, disc, need_disc=False)
    preflight_extra(watcom, bool(asm_sources(SRC)))
    # Nothing linked into the test image reads the disc, so it is mounted when
    # it happens to be there and simply left out when it is not.  Making it a
    # hard dependency would report "this machine has no disc image" as a failed
    # gate, which is the one thing a gate must never say.
    mount = disc if mountable_disc(disc) else None

    result = {"build_ok": None, "run_ok": None, "tests": {}, "diagnostics": {}}
    ok = True
    if args.stage in ("build", "all"):
        ok = do_build(dosbox, watcom, mount, args.timeout)
        result["build_ok"] = ok
    if ok and args.stage in ("run", "all"):
        ok, parsed = do_run(dosbox, watcom, mount, args.timeout)
        result["run_ok"] = ok
        result["tests"] = parsed

    # Read the compiler's own output whichever stage ran: `run` on its own is
    # judged against the build that produced the image it just executed, so a
    # standalone run cannot report clean over a build that was not.
    diag = diagnostics()
    result["diagnostics"] = diag
    checks = [not diag["errors"], not diag["warnings"], not diag["undefined"]]
    if args.stage in ("build", "all"):
        checks.append(bool(result["build_ok"]))
    if args.stage in ("run", "all"):
        checks.append(bool(result["run_ok"]))
    result["gate_pass"] = all(checks)
    write_result(result)
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    print("[result] %s" % ("PASS" if result["gate_pass"] else "FAIL"))
    return 0 if result["gate_pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
