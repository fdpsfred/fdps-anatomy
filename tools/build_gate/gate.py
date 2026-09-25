"""gate.py -- the self-regression build gate.

Rebuilds every registered target, compares the result against a recorded
baseline, and runs the test suites, returning one structured verdict.  What it
proves is narrow and worth stating exactly: the build output still matches *this
project's previous build*, not the original FDPS.EXE.  Functional equivalence
with the original is not a byte comparison at all (ADR-0001); this gate exists
so that an edit which was supposed to change nothing is caught the moment it
changes something.

Two comparisons, in this order:

  hash          sha256 of the produced image against the baseline.  Equal means
                nothing moved at all -- the strongest and cheapest answer.
  equivalence   when the hash differs, `lefixup.compare` decides whether the
                difference is confined to what the linker relocates.  Renaming a
                symbol permutes the LE fixup records and can shift a tentative
                definition by a few bytes; that is behaviour-neutral and must
                not read as a regression.  For a target that writes a linker
                map, the alignment gaps wcc386 never clears (lepad.py) are
                also set aside, so the same sources built from a CRLF checkout
                and from LF-written files read as `pad`.  Anything else is
                `different`.

A pass also requires zero build errors, no warning that the baseline did not
already record, zero undefined symbols, and every test suite green.

The verdict is a dict (also written as JSON), so a build script can call
`check()` in the foreground and branch on it instead of scraping text.

Subcommands:
    check      (default) build, compare, test, report
    update     move the baseline forward after a deliberate change
    show       print the recorded baselines
    selftest   prove every check in here can fail

Usage: python tools/build_gate/gate.py [check|update|show|selftest] [options]
Exit : 0 when the gate passes, 1 otherwise.
"""
import argparse
import datetime
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BASELINES = HERE / "data" / "baselines.json"
WORK = ROOT / "workspace" / "build_gate"

sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools" / "fdps_build"))
sys.path.insert(0, str(ROOT / "tools" / "ail_link"))
sys.path.insert(0, str(ROOT / "tools" / "code_emit"))
sys.path.insert(0, str(ROOT / "tools" / "game_build"))
import lefixup  # noqa: E402
import lepad  # noqa: E402
import build_min as bm  # noqa: E402
import link_ail as la  # noqa: E402
import build_emit as ce  # noqa: E402
import build_game as gb  # noqa: E402


# ------------------------------------------------------------------- targets

def _build_smoke(ctx):
    ok = bm.do_build(ctx["dosbox"], ctx["watcom"], ctx["disc"], ctx["timeout"])
    return {"builder_ok": ok,
            "exe": bm.find_ci(bm.OUT, "smoke.exe"),
            "build_out": bm.find_ci(bm.OUT, "build.out")}


def _build_ailsmoke(ctx):
    ok, _syms = la.do_build(ctx["dosbox"], ctx["watcom"], ctx["disc"],
                            ctx["timeout"])
    return {"builder_ok": ok,
            "exe": bm.find_ci(la.OUT, "ailsmok.exe"),
            "build_out": bm.find_ci(la.OUT, "build.out"),
            "pad": {"map": bm.find_ci(la.OUT, la.MAP), "objs": la.OBJ,
                    "sources": la.STAGE}}


def _build_emittest(ctx):
    """emittest links twice, so it reads its own transcripts.

    The first link deliberately carries no stubs: the symbols it reports are
    the data and functions the rebuild has not reached yet, which is a worklist
    (workspace/code_emit/undefined.json) and not a fault.  What has to resolve
    is the second link, the one carrying the generated stub module, and that is
    what `undefined` is judged on -- rebuild_info/build_gate.md.  Scanning only
    build.out would keep the gate red from the first function that borrows a
    ticket 23 global until the last one lands.

    build_emit.diagnostics() already draws that line and is selftested on it,
    so it is used here rather than restated; `transcripts` still lists both
    passes so the summary counts cover the stub module's compile too.
    """
    ok = ce.do_build(ctx["dosbox"], ctx["watcom"], ctx["disc"], ctx["timeout"])
    return {"builder_ok": ok,
            "exe": bm.find_ci(ce.OUT, ce.EXE),
            "build_out": bm.find_ci(ce.OUT, "build.out"),
            "transcripts": [p for p in (bm.find_ci(ce.OUT, "build.out"),
                                        bm.find_ci(ce.OUT, "build2.out"))
                            if p is not None],
            "diagnostics": ce.diagnostics}


def _build_game(ctx):
    """The game itself: src/ alone, one link, FDE.EXE (tools/game_build/)."""
    ok = gb.do_build(ctx["dosbox"], ctx["watcom"], ctx["disc"], ctx["timeout"])
    return {"builder_ok": ok,
            "exe": bm.find_ci(gb.OUT, gb.EXE),
            "build_out": bm.find_ci(gb.OUT, "build.out"),
            "pad": {"map": bm.find_ci(gb.OUT, gb.MAP), "objs": gb.OBJ,
                    "sources": gb.STAGE / gb.G_SRC}}


# The gate is target-parameterised because the thing being gated changed as the
# rebuild progressed: the two smoke programs, the emit test image, and now the
# game itself.
#
# `compare` says whether an image baseline is meaningful for the target.  It is
# for the smoke programs: their sources are frozen, so any byte that moves is
# news.  It is for the game too: its sources no longer grow by design, so an
# image that moves is either a deliberate fix -- recorded with `update
# --reason` naming it -- or a regression.  It is not for emittest, whose whole
# purpose is to grow by one function at a time -- a baseline there would fail
# on every emit and be re-recorded on every emit, which is a gate that has been
# trained to say yes.  What that
# target is gated on instead is what the emit pipeline actually promises: zero
# errors, zero unresolved symbols, no new warning, every test green.
TARGETS = {
    "smoke": {"build": _build_smoke, "compare": True,
              "desc": "tools/fdps_build smoke program (CRT + disc probe)"},
    "ailsmoke": {"build": _build_ailsmoke, "compare": True,
                 "desc": "tools/ail_link client linked against ailv3.lib"},
    "emittest": {"build": _build_emittest, "compare": False,
                 "desc": "tools/code_emit unit-test image over src/ + tests/"},
    "game": {"build": _build_game, "compare": True,
             "desc": "tools/game_build FDE.EXE, the game over src/ alone"},
}


# --------------------------------------------------------------------- tests

# `needs` gates a suite on what the machine actually has, so a missing disc
# image reports as skipped instead of failing the gate or, worse, being dropped
# without a word.  Audio also opens a window, so it is opt-in.  `target` ties a
# suite to the target it exercises: `--target smoke` must not run, or demand the
# dependencies of, the AIL suites whose preflight the run deliberately skipped.
TEST_SUITES = [
    {"name": "build_gate.selftest",
     "argv": ["tools/build_gate/gate.py", "selftest"],
     "needs": (), "target": None},
    {"name": "build_gate.pad_selftest",
     "argv": ["tools/build_gate/lepad.py", "selftest"],
     "needs": (), "target": None},
    {"name": "fdps_build.selftest",
     "argv": ["tools/fdps_build/build_min.py", "selftest"],
     "needs": (), "target": None},
    {"name": "ail_link.selftest",
     "argv": ["tools/ail_link/link_ail.py", "selftest"],
     "needs": ("dosbox",), "target": "ailsmoke"},
    {"name": "code_emit.selftest",
     "argv": ["tools/code_emit/build_emit.py", "selftest"],
     "needs": (), "target": None},
    {"name": "code_emit.stubs",
     "argv": ["tools/code_emit/gen_stubs.py", "--selftest"],
     "needs": (), "target": None},
    # Not a selftest but a staleness check: src/fdpstype.h and tests/fdpstype.c
    # are generated from the Ghidra snapshot, and a layout fixed in Ghidra but
    # not regenerated here would leave the build compiling against offsets the
    # database no longer claims.
    {"name": "code_emit.types",
     "argv": ["tools/code_emit/gen_types.py", "--check"],
     "needs": (), "target": None},
    {"name": "fdps_build.run",
     "argv": ["tools/fdps_build/build_min.py", "run"],
     "needs": ("dosbox", "disc"), "target": "smoke"},
    # The emitted code's own assertions.  This is the suite that decides
    # whether an emitted function behaves like the original, so it runs
    # whenever the emittest target is in scope.
    {"name": "code_emit.run",
     "argv": ["tools/code_emit/build_emit.py", "run"],
     "needs": ("dosbox",), "target": "emittest"},
    {"name": "ail_link.run",
     "argv": ["tools/ail_link/link_ail.py", "run"],
     "needs": ("dosbox", "disc", "audio"), "target": "ailsmoke"},
    # The global data src/ defines, compared with the shipped image byte for
    # byte (pointers by target name) and against the layout constraints the
    # manifest records.  The emittest image is the one it reads, so it rides
    # on that target; the shipped FDPS.LE is not in the repository, so a
    # machine without it reports the suite skipped.
    {"name": "data_emit.selftest",
     "argv": ["tools/data_emit/check_data.py", "--selftest"],
     "needs": ("gamefiles",), "target": None},
    {"name": "data_emit.check",
     "argv": ["tools/data_emit/check_data.py"],
     "needs": ("gamefiles",), "target": "emittest"},
    # The RLE blitters are assembly transcribed from the original, and their
    # bar is the original's instruction sequence rather than behaviour alone
    # (tools/rle_asm/).  The check reads the objects the emittest build just
    # assembled, so it judges what was linked; it needs the shipped FDPS.LE.
    {"name": "rle_asm.selftest",
     "argv": ["tools/rle_asm/asm_match.py", "selftest"],
     "needs": (), "target": None},
    {"name": "rle_asm.switch_selftest",
     "argv": ["tools/rle_asm/switch_impl.py", "selftest"],
     "needs": (), "target": None},
    {"name": "rle_asm.check",
     "argv": ["tools/rle_asm/asm_match.py", "check"],
     "needs": ("gamefiles",), "target": "emittest"},
    # The game image.  The test image is linked from a different object set,
    # so the linker places the globals and the blitters differently; the data
    # layout constraints and the assembly check have to hold in the image that
    # ships, not only in the one the tests run in.
    {"name": "game_build.selftest",
     "argv": ["tools/game_build/build_game.py", "selftest"],
     "needs": (), "target": None},
    {"name": "game_build.play_selftest",
     "argv": ["tools/game_build/play.py", "selftest"],
     "needs": (), "target": None},
    {"name": "game_build.locate_selftest",
     "argv": ["tools/game_build/locate.py", "selftest"],
     "needs": (), "target": None},
    {"name": "data_emit.check_game",
     "argv": ["tools/data_emit/check_data.py", "--image", "game"],
     "needs": ("gamefiles",), "target": "game"},
    {"name": "rle_asm.check_game",
     "argv": ["tools/rle_asm/asm_match.py", "check", "--objs",
              "workspace/game_build/out/obj"],
     "needs": ("gamefiles",), "target": "game"},
]


# --------------------------------------------------------------- diagnostics

# Watcom writes one summary line per translation unit and one "Warning!" /
# "Error!" line per diagnostic.  Both are read: the summary catches a count the
# text scan could miss, the text is what makes "no NEW warnings" decidable.
SUMMARY_RX = re.compile(r"(\d+)\s+warnings?,\s*(\d+)\s+errors?", re.I)
# wcc386 spells its warnings "Warning! W107: ..."; wlink puts the number in
# parentheses instead.  Matching only the compiler's spelling made every linker
# warning invisible here.
WARNING_RX = re.compile(r"\bWarning[!(]", re.I)
# Not a warning in any useful sense: the linker kept one definition and dropped
# the other, so the image carries a body nobody reviewed under the name that was
# just emitted, and every test still passes because something answers the call.
# It is an error, and deliberately not routed through the baseline -- there is
# no version of this a baseline should be able to accept.
REDEFINITION_RX = re.compile(r"redefinition of .+ ignored", re.I)
# The build links twice on purpose and the first link is SUPPOSED to name every
# data global ticket 23 has not emitted yet -- that list is the point of it, and
# the stub module built from it makes the second link resolve.  Those come back
# as warnings as well as errors, so they are dropped here for the same reason
# the errors are.  Nothing is lost: whether anything is still unresolved is
# decided by the `undefined` check, which reads the LAST link.
UNDEF_REF_RX = re.compile(r"is an undefined reference", re.I)


def diagnostics(text):
    lines = [l.strip() for l in text.splitlines()]
    summary_w = summary_e = 0
    for m in SUMMARY_RX.finditer(text):
        summary_w += int(m.group(1))
        summary_e += int(m.group(2))
    return {
        "errors": ([l for l in lines if "Error!" in l]
                   + [l for l in lines if REDEFINITION_RX.search(l)]),
        "warnings": [l for l in lines if WARNING_RX.search(l)
                     and not REDEFINITION_RX.search(l)
                     and not UNDEF_REF_RX.search(l)],
        "summary_warnings": summary_w,
        "summary_errors": summary_e,
        "undefined": bm.parse_undefined(text),
    }


def merge_diagnostics(texts, own=None):
    """One diagnostics row for a target, across every transcript it produced.

    `own` is the target's own reader, for a target whose build is not a single
    pass.  emittest is one: it links twice and only the second link's leftovers
    are a fault, a distinction build_emit.diagnostics() owns.  The summary
    counts are still taken from every transcript, because a translation unit
    that failed to compile in a later pass has to be seen either way.
    """
    diag = diagnostics("\n".join(texts))
    if own is not None:
        diag.update(own())
    return diag


def new_warnings(baseline_warnings, warnings):
    """Warnings the baseline never recorded -- the "zero new warnings" rule.

    Comparing counts alone would let one warning be traded for another; the
    texts are compared so a swap still fails.
    """
    known = set(baseline_warnings or [])
    return [w for w in warnings if w not in known]


# ------------------------------------------------------------------ baseline

BASELINE_DOC = (
    "Build-gate baselines: the previous build of each target, per ADR-0001 a "
    "self-regression reference and not a comparison against the original "
    "FDPS.EXE. `profile` is tools/build_gate/lefixup.py's relocation-aware "
    "fingerprint; `warnings` is the accepted warning text, so a new warning "
    "fails even when the count does not rise. Every advance appends the "
    "superseded entry to `history` with the reason it was superseded -- the "
    "chain is the audit trail for what the gate has been told to accept."
)


def load_baselines():
    if not BASELINES.is_file():
        return {"_doc": BASELINE_DOC, "targets": {}}
    return json.loads(BASELINES.read_text(encoding="utf-8"))


def save_baselines(data):
    data["_doc"] = BASELINE_DOC
    BASELINES.parent.mkdir(parents=True, exist_ok=True)
    BASELINES.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n",
                         encoding="utf-8")


def git_commit():
    try:
        out = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                             cwd=str(ROOT), capture_output=True, text=True)
        return out.stdout.strip() or None
    except OSError:
        return None


# ------------------------------------------------------------------ the gate

def disc_available(disc):
    """The same test the run stages apply: the cue AND the .bin it names.

    Checking only the cue would let a dangling one through, and the suites that
    do check would then fail the gate instead of being skipped -- reporting a
    regression where the machine is simply missing the disc image.
    """
    if not disc.is_file():
        return False
    try:
        return bm.cue_bin(disc).is_file()
    except (SystemExit, OSError):
        # cue_bin() raises SystemExit through build_min's fail() when the cue
        # has no FILE directive; an unreadable cue is just as unusable.
        return False


def _context(watcom, disc, timeout, targets):
    dosbox = bm.preflight(watcom, disc, need_disc=False)
    if "ailsmoke" in targets:
        la.preflight_wasm(watcom)
        la.preflight_extra(False)
    if "emittest" in targets:
        ce.preflight_extra(watcom, bool(ce.asm_sources(ce.SRC)))
    if "game" in targets:
        gb.preflight(watcom, disc)
    # The disc is mounted for the builds too, not because compilation reads it
    # but because the gate has to build the way the normal build does
    # (rebuild_info/build_pipeline.md keeps one mount definition for both
    # stages); a gate that builds in a different environment gates nothing.
    return {"dosbox": dosbox, "watcom": watcom, "timeout": timeout,
            "disc": disc if disc_available(disc) else None}


def build_and_compare(name, ctx, baselines):
    """Build one target and judge it against its baseline."""
    spec = TARGETS[name]
    print("[gate] === %s: %s ===" % (name, spec["desc"]))
    built = spec["build"](ctx)

    row = {"target": name, "checks": [], "verdict": None}

    def add(check, ok, detail):
        row["checks"].append({"check": check,
                              "status": "pass" if ok else "fail",
                              "detail": detail})

    def settle():
        """Decide the verdict and report it, on every path out of here."""
        row["verdict"] = ("pass" if all(c["status"] == "pass"
                                        for c in row["checks"]) else "fail")
        eq = (row.get("equivalence") or {}).get("verdict", "-")
        size = (row.get("profile") or {}).get("size", 0)
        print("[gate] %s: %s (%s, %d bytes)"
              % (name, row["verdict"].upper(), eq, size))
        for c in row["checks"]:
            if c["status"] != "pass":
                print("[gate]   FAIL %s: %s" % (c["check"], c["detail"]))
        return row

    exe, bout = built["exe"], built["build_out"]
    paths = built.get("transcripts") or ([bout] if bout else [])
    texts = [p.read_text(encoding="latin-1", errors="replace") for p in paths]
    diag = merge_diagnostics(texts, built.get("diagnostics"))
    row["diagnostics"] = diag

    add("build", bool(exe) and built["builder_ok"],
        "no executable produced" if not exe else
        ("builder reported failure" if not built["builder_ok"] else str(exe)))
    if not exe:
        return settle()

    row["exe"] = str(exe)
    add("errors", not diag["errors"] and diag["summary_errors"] == 0,
        "%d error lines, %d in summaries" % (len(diag["errors"]),
                                             diag["summary_errors"]))
    add("undefined", not diag["undefined"],
        ", ".join(diag["undefined"]) or "none")

    base = (baselines.get("targets") or {}).get(name)
    pad = None
    if spec.get("compare", True) and built.get("pad") is not None:
        # The alignment gaps wcc386 leaves uncleared, proved from this build's
        # own map, objects and staged sources (lepad.py).  A target that says
        # it has them and cannot produce them fails here rather than quietly
        # losing the `pad` tier.
        inputs = built["pad"]
        try:
            if inputs["map"] is None:
                raise lepad.PadError("the build wrote no linker map")
            found = lepad.analyse_build(exe, inputs["map"], inputs["objs"],
                                        inputs["sources"])
        except (lepad.PadError, lefixup.LeError, struct.error, OSError) as exc:
            add("pad", False, "alignment gaps not computable: %s" % exc)
            row["equivalence"] = None
            return settle()
        pad = found["ranges"]
        row["pad"] = {k: v for k, v in found.items() if k != "ranges"}
    try:
        fresh = lefixup.profile(exe.read_bytes(), pad=pad)
    except (lefixup.LeError, struct.error, OSError) as exc:
        # A truncated image, or one linked as something other than LE, is a
        # gate failure like any other -- never a traceback, which would skip
        # the remaining targets and leave no result.json for the caller.
        add("image", False, "not readable as an LE image: %s" % exc)
        row["equivalence"] = None
        return settle()
    row["profile"] = fresh

    if not spec.get("compare", True):
        # No image baseline for this target.  The warning rule still holds, and
        # with nothing recorded it holds in its strictest form: zero.  Whoever
        # wants a warning accepted has to record it with `update --reason`, the
        # same as everywhere else.
        # Both halves, exactly as the compared targets do it: the text scan is
        # what makes "no NEW warning" decidable, the per-unit summary catches a
        # count the text scan could miss when output is truncated or interleaved.
        base_count = (base or {}).get("summary_warnings", 0)
        add("warnings",
            (not new_warnings((base or {}).get("warnings"), diag["warnings"])
             and diag["summary_warnings"] <= base_count),
            "%d new, summary %d vs %d %s"
            % (len(new_warnings((base or {}).get("warnings"), diag["warnings"])),
               diag["summary_warnings"], base_count,
               "(recorded baseline)" if base else "(nothing accepted)"))
        row["equivalence"] = {"verdict": "not compared",
                              "detail": "this target has no image baseline "
                                        "-- it changes by design on every emit"}
        return settle()

    if base is None:
        # A gate with nothing to compare against must not report success: that
        # is exactly the state in which a real regression would slip through.
        add("warnings", not diag["warnings"] and diag["summary_warnings"] == 0,
            "%d warnings, no baseline to compare against" % len(diag["warnings"]))
        add("baseline", False,
            "no baseline recorded -- run `gate.py update --target %s --reason ...`"
            % name)
        row["equivalence"] = None
        return settle()

    fresh_new = new_warnings(base.get("warnings"), diag["warnings"])
    base_count = base.get("summary_warnings", 0)
    # Both halves are reported, because the same warning firing one more time
    # trips the count without adding any new text -- and a detail line reading
    # "0 new: none" on a failing check tells the operator nothing.
    add("warnings",
        not fresh_new and diag["summary_warnings"] <= base_count,
        "%d new (%s), summary %d vs baseline %d"
        % (len(fresh_new), "; ".join(fresh_new[:3]) or "none",
           diag["summary_warnings"], base_count))

    try:
        verdict, detail = lefixup.compare(base["profile"], fresh)
    except KeyError as exc:
        add("equivalence", False,
            "baseline profile has no %s -- re-record it with `update`" % exc)
        row["equivalence"] = None
        return settle()
    row["equivalence"] = {"verdict": verdict, "detail": detail}
    add("equivalence", verdict in lefixup.PASSING, verdict)
    settle()

    if verdict == "different":
        print("[gate]   a code or data byte outside every relocation and every")
        print("[gate]   provable alignment gap changed -- this is a real")
        print("[gate]   difference.  tools/build_gate/pad_diff.py lists the bytes.")
    elif verdict == "pad":
        print("[gate]   differences are relocations and uncleared alignment gaps only.")
    elif verdict == "size":
        print("[gate]   the image changed size; nothing finer is comparable.")
    elif verdict == "reloc":
        print("[gate]   differences are relocation values plus fixup reorder only.")
    return row


def run_tests(available, targets, timeout):
    rows = []
    for suite in TEST_SUITES:
        if suite["target"] is not None and suite["target"] not in targets:
            rows.append({"suite": suite["name"], "status": "skip",
                         "detail": "out of scope: target %s not selected"
                                   % suite["target"]})
            print("[gate] test %s: SKIP (target %s not selected)"
                  % (suite["name"], suite["target"]))
            continue
        missing = [n for n in suite["needs"] if n not in available]
        if missing:
            rows.append({"suite": suite["name"], "status": "skip",
                         "detail": "missing: %s" % ", ".join(missing)})
            print("[gate] test %s: SKIP (missing %s)"
                  % (suite["name"], ", ".join(missing)))
            continue
        argv = [sys.executable] + suite["argv"]
        print("[gate] test %s ..." % suite["name"])
        try:
            proc = subprocess.run(argv, cwd=str(ROOT), capture_output=True,
                                  text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            rows.append({"suite": suite["name"], "status": "fail",
                         "detail": "timed out after %ds" % timeout})
            print("[gate] test %s: FAIL (timeout)" % suite["name"])
            continue
        ok = proc.returncode == 0
        tail = "\n".join((proc.stdout or "").splitlines()[-6:])
        rows.append({"suite": suite["name"],
                     "status": "pass" if ok else "fail",
                     "detail": "exit %d" % proc.returncode,
                     "tail": tail})
        print("[gate] test %s: %s" % (suite["name"], "PASS" if ok else "FAIL"))
        if not ok:
            print("\n".join("  | " + l for l in tail.splitlines()))
    return rows


def check(targets, watcom, disc, timeout, with_audio=False, skip_tests=False):
    """Run the whole gate and return the structured verdict."""
    ctx = _context(watcom, disc, timeout, targets)
    baselines = load_baselines()

    rows = [build_and_compare(name, ctx, baselines) for name in targets]

    available = {"dosbox"}
    if ctx["disc"] is not None:
        available.add("disc")
    if with_audio:
        available.add("audio")
    if (ROOT / "fdps_game_files" / "FDPS.LE").is_file():
        available.add("gamefiles")
    tests = [] if skip_tests else run_tests(available, targets, timeout * 4)

    passed = (all(r["verdict"] == "pass" for r in rows)
              and all(t["status"] != "fail" for t in tests))
    result = {
        "verdict": "PASS" if passed else "FAIL",
        "generated": datetime.datetime.now().isoformat(timespec="seconds"),
        "commit": git_commit(),
        "targets": rows,
        "tests": tests,
        "skipped_tests": [t["suite"] for t in tests if t["status"] == "skip"],
    }
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / "result.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return result


def update(target, reason, watcom, disc, timeout):
    """Advance one target's baseline, refusing to record a build that is not clean."""
    ctx = _context(watcom, disc, timeout, [target])
    baselines = load_baselines()
    row = build_and_compare(target, ctx, baselines)

    blocking = [c for c in row["checks"]
                if c["status"] == "fail" and c["check"] in
                ("build", "errors", "undefined")]
    if blocking or not row.get("profile"):
        why = ", ".join(c["check"] for c in blocking) or "no image produced"
        print("[gate] refusing to advance the baseline: %s" % why)
        return False
    if row["diagnostics"]["warnings"]:
        # Recording warnings is allowed -- they become the accepted set -- but
        # never silently: they are printed so the reason can account for them.
        print("[gate] recording %d warning(s) into the baseline:"
              % len(row["diagnostics"]["warnings"]))
        for w in row["diagnostics"]["warnings"]:
            print("[gate]   %s" % w)

    entry = {
        "recorded": datetime.date.today().isoformat(),
        "commit": git_commit(),
        "reason": reason,
        "warnings": row["diagnostics"]["warnings"],
        "summary_warnings": row["diagnostics"]["summary_warnings"],
        "profile": row["profile"],
    }
    targets = baselines.setdefault("targets", {})
    prior = targets.get(target)
    if prior:
        history = prior.pop("history", [])
        was = row.get("equivalence", {}) or {}
        prior["superseded_by"] = reason
        prior["superseded_verdict"] = was.get("verdict")
        entry["history"] = [prior] + history
    else:
        entry["history"] = []
    targets[target] = entry
    save_baselines(baselines)
    print("[gate] baseline for %s advanced: %s (%d bytes, sha %s)"
          % (target, entry["recorded"], entry["profile"]["size"],
             entry["profile"]["sha256"][:16]))
    return True


def show():
    baselines = load_baselines()
    targets = baselines.get("targets") or {}
    if not targets:
        print("[gate] no baselines recorded")
        return
    for name, entry in sorted(targets.items()):
        print("%-10s %s  %s  %d bytes  %s"
              % (name, entry["recorded"], entry["profile"]["sha256"][:16],
                 entry["profile"]["size"], entry.get("commit") or "-"))
        print("           reason: %s" % entry.get("reason", "-"))
        print("           warnings accepted: %d, relocations: %d, alignment gaps: %s"
              % (len(entry.get("warnings") or []),
                 entry["profile"]["fixup_sites"],
                 "%d bytes" % entry["profile"]["pad_bytes"]
                 if "pad_bytes" in entry["profile"] else "not fingerprinted"))
        for old in entry.get("history") or []:
            print("           prior : %s %s (%s)"
                  % (old["recorded"], old["profile"]["sha256"][:16],
                     old.get("superseded_by", "-")))


# ------------------------------------------------------------------ selftest

def _rec_internal(off):
    """SRC, FLAGS, SRCOFF, OBJECT, TRGOFF -- the shape wlink actually emits."""
    body = (bytes([0x07, 0x00]) + struct.pack("<h", off) + bytes([1])
            + struct.pack("<H", off & 0xFFFF))
    return body, [(off, 4)]


def _rec_source_list(offsets, additive=False):
    """SRC, FLAGS, CNT, OBJECT, TRGOFF, [ADDITIVE], SRCOFF1..n.

    The offsets sit at the END of the record.  Reading them straight after the
    count consumes the same number of bytes, so the page-end check still lines
    up while every site is wrong -- which is precisely why this shape is in the
    selftest even though no image here currently contains one.
    """
    flags = 0x04 if additive else 0x00
    body = (bytes([0x07 | 0x20, flags, len(offsets), 1])
            + struct.pack("<H", offsets[0] & 0xFFFF))
    if additive:
        body += struct.pack("<H", 0x20)     # 16-bit additive (0x20 not set)
    body += b"".join(struct.pack("<h", o) for o in offsets)
    return body, [(o, 4) for o in offsets]


def _rec_import_ordinal(off, width):
    """Import by ordinal, whose ordinal field is 1, 2 or 4 bytes wide.

    Getting the width backwards desynchronises the walk, so these three records
    are what makes the 8-bit-flag-wins rule a tested claim rather than a comment.
    """
    flags = 0x01 | {1: 0x80, 2: 0x00, 4: 0x10}[width]
    body = (bytes([0x07, flags]) + struct.pack("<h", off) + bytes([2])
            + {1: bytes([3]), 2: struct.pack("<H", 3),
               4: struct.pack("<I", 3)}[width])
    return body, [(off, 4)]


def _synth_le(page_size=0x1000, stub=0x40):
    """A minimal LE image with relocations at known offsets.

    The parser is the part of this gate that decides which bytes are allowed to
    differ, so it is checked against ground truth rather than against itself: an
    image is built here with the sites chosen in advance, and the parser has to
    report exactly those.  Synthetic also means the selftest needs neither
    DOSBox-X nor an untracked artefact to run, and it can carry record shapes
    that today's builds happen not to emit but a later one will.
    """
    # Page 1 leads with two equal-length plain records so the reorder case has a
    # pair to swap; the rest are the shapes the walk has to skip exactly.
    page_recs = [
        [_rec_internal(0x010), _rec_internal(0x190),
         _rec_source_list([0x310, 0x490]),
         _rec_source_list([0x610, 0x790], additive=True)],
        [_rec_internal(0x020),
         _rec_import_ordinal(0x1a0, 1),
         _rec_import_ordinal(0x320, 2),
         _rec_import_ordinal(0x4a0, 4)],
    ]
    pages = len(page_recs)
    blobs = [b"".join(r[0] for r in recs) for recs in page_recs]

    hdr_size = 0xAC
    fpt = hdr_size + 4                      # relative to the LE header
    frt = fpt + 4 * (pages + 1)
    imt = frt + sum(len(b) for b in blobs)
    data_off = 0x2000

    body = bytearray(data_off + pages * page_size)
    body[0:2] = b"MZ"
    body[0x3C:0x40] = (stub).to_bytes(4, "little")

    def put32(off, val):
        body[stub + off:stub + off + 4] = int(val).to_bytes(4, "little")

    body[stub:stub + 2] = b"LE"
    put32(0x14, pages)
    put32(0x28, page_size)
    put32(0x68, fpt)
    put32(0x6C, frt)
    put32(0x70, imt)
    put32(0x80, data_off)

    running = 0
    for i in range(pages + 1):
        off = stub + fpt + 4 * i
        body[off:off + 4] = running.to_bytes(4, "little")
        if i < pages:
            running += len(blobs[i])

    pos = stub + frt
    truth, source_list_sites, ordinal_sites = [], [], []
    for index, (recs, blob) in enumerate(zip(page_recs, blobs)):
        body[pos:pos + len(blob)] = blob
        pos += len(blob)
        page_file_off = data_off + index * page_size
        for rec_bytes, sites in recs:
            placed = [(page_file_off + o, w) for o, w in sites]
            truth.extend(placed)
            if rec_bytes[0] & 0x20:
                source_list_sites.extend(placed)
            elif (rec_bytes[1] & 0x03) == 1:
                ordinal_sites.extend(placed)

    # Deterministic filler so any mutation below actually changes a byte.
    for i in range(data_off, len(body)):
        body[i] = (i * 37) & 0xFF
    geom = {"data_off": data_off, "page_size": page_size,
            "frt": stub + frt, "rec_len": len(page_recs[0][0][0]),
            "source_list_sites": source_list_sites,
            "ordinal_sites": ordinal_sites}
    return bytes(body), truth, geom


def _selftest_parser():
    rows = []
    img, truth, geom = _synth_le()

    # A parser regression usually shows up as a desynchronised walk, which
    # raises; catching it here keeps that a reported FAIL instead of a traceback
    # that hides every remaining row.
    try:
        got = lefixup.fixup_sites(img)
        detail = "%d sites" % len(got)
    except lefixup.LeError as exc:
        got, detail = [], str(exc)
    rows.append(("sites match ground truth", got == truth, detail))
    rows.append(("source-list offsets read after the target data",
                 all(s in got for s in geom["source_list_sites"]),
                 "%d sites" % len(geom["source_list_sites"])))
    rows.append(("import-by-ordinal widths keep the walk aligned",
                 all(s in got for s in geom["ordinal_sites"]),
                 "%d sites" % len(geom["ordinal_sites"])))

    lo, hi = lefixup.fixup_bounds(img)
    rows.append(("table bounds from header", lo == geom["frt"] and hi > lo,
                 "0x%x..0x%x" % (lo, hi)))

    bad = bytearray(img)
    bad[geom["frt"]] = 0x0F                     # source type that does not exist
    rows.append(("unknown source type raises",
                 _raises(lefixup.fixup_sites, bytes(bad)), "LeError"))

    bad = bytearray(img)
    bad[geom["frt"]] |= 0x20                    # claim a source list -> stream desyncs
    rows.append(("desynchronised record stream raises",
                 _raises(lefixup.fixup_sites, bytes(bad)), "LeError"))

    rows.append(("a non-LE file raises",
                 _raises(lefixup.fixup_sites, b"NOTANEXE" * 64), "LeError"))
    return rows, img, truth, geom


def _raises(fn, *args):
    try:
        fn(*args)
    except lefixup.LeError:
        return True
    except Exception:
        return False
    return False


def _selftest_compare(img, truth, geom):
    """Each verdict, produced by the edit that verdict is supposed to describe."""
    rows = []
    base = lefixup.profile(img)

    rows.append(("unchanged image is identical",
                 lefixup.compare(base, lefixup.profile(img))[0] == "identical",
                 "identical"))

    # Fixup reorder: swap two whole records inside one page.  Record order
    # within a page carries no meaning, so this is exactly what a rename does
    # to the table -- and nothing else in the image moves.
    swapped = bytearray(img)
    a = geom["frt"]
    b = a + geom["rec_len"]
    swapped[a:b], swapped[b:b + geom["rec_len"]] = (
        img[b:b + geom["rec_len"]], img[a:b])
    v = lefixup.compare(base, lefixup.profile(bytes(swapped)))[0]
    rows.append(("fixup reorder is STRICT", v == "strict", v))

    # Relocation value changed at a site: what moving a tentative definition
    # does to every reference to it.
    moved = bytearray(img)
    off = truth[0][0]
    moved[off] ^= 0xFF
    v = lefixup.compare(base, lefixup.profile(bytes(moved)))[0]
    rows.append(("changed relocation value is RELOC", v == "reloc", v))

    # A real edit: a byte in the data pages that no relocation covers.
    covered = set()
    for o, w in truth:
        covered.update(range(o, o + w))
    victim = next(i for i in range(geom["data_off"], len(img))
                  if i not in covered)
    real = bytearray(img)
    real[victim] ^= 0xFF
    v = lefixup.compare(base, lefixup.profile(bytes(real)))[0]
    rows.append(("a code/data byte is DIFFERENT", v == "different", v))

    v = lefixup.compare(base, lefixup.profile(img[:-1]))[0]
    rows.append(("a shorter image is SIZE", v == "size", v))
    return rows


def _selftest_diagnostics():
    rows = []
    text = ("SMOKE.C(12): Warning! W107: Missing return value\n"
            "SMOKE.C: 178 lines, included 618, 1 warnings, 0 errors\n"
            "Error! E2028: foo_ is an undefined reference\n")
    d = diagnostics(text)
    rows.append(("warning line counted", len(d["warnings"]) == 1,
                 str(len(d["warnings"]))))
    rows.append(("warning summary counted", d["summary_warnings"] == 1,
                 str(d["summary_warnings"])))
    rows.append(("error line counted", len(d["errors"]) == 1,
                 str(len(d["errors"]))))
    rows.append(("undefined symbol named", d["undefined"] == ["foo_"],
                 ", ".join(d["undefined"]) or "none"))

    clean = diagnostics("SMOKE.C: 178 lines, included 618, 0 warnings, 0 errors\n")
    rows.append(("a clean build has no diagnostics",
                 not clean["warnings"] and not clean["errors"]
                 and clean["summary_warnings"] == 0, "clean"))

    known = ["SMOKE.C(12): Warning! W107: Missing return value"]
    rows.append(("a recorded warning is not new",
                 new_warnings(known, known) == [], "none"))
    rows.append(("a different warning is new",
                 new_warnings(known, ["OTHER.C(3): Warning! W302: x"]) != [],
                 "detected"))

    # A two-link target: the first link's undefined list is the worklist, the
    # second link's is the fault.  Judging the first would hold the gate red
    # for the whole of ticket 23.
    first = ("=== link ===\n"
             "Warning(1028): data_fdps_thing is an undefined reference\n"
             "file OBJS\\A.OBJ(A.C): undefined symbol data_fdps_thing\n")
    second = ("=== stubs ===\n"
              "STB\\STUBS.C: 16 lines, 0 warnings, 0 errors\n"
              "=== link2 ===\n"
              "creating a DOS/4G executable\n")
    stubbed = merge_diagnostics(
        [first, second],
        lambda: {"errors": [], "warnings": [], "undefined": [],
                 "stubbed": ["data_fdps_thing"]})
    rows.append(("a stubbed symbol does not fail the gate",
                 not stubbed["undefined"]
                 and stubbed["stubbed"] == ["data_fdps_thing"],
                 ", ".join(stubbed["undefined"]) or "none"))
    rows.append(("both transcripts feed the summary counts",
                 stubbed["summary_errors"] == 0
                 and stubbed["summary_warnings"] == 0, "0/0"))
    left = merge_diagnostics(
        [first, second],
        lambda: {"errors": ["Error! E2028: mystery is an undefined reference"],
                 "warnings": [], "undefined": ["mystery"],
                 "stubbed": ["data_fdps_thing"]})
    rows.append(("a symbol left after the stubs does fail",
                 left["undefined"] == ["mystery"] and len(left["errors"]) == 1,
                 ", ".join(left["undefined"])))
    # Without a reader of its own a target still behaves exactly as before.
    plain = merge_diagnostics([first])
    rows.append(("a one-pass target still reads its one transcript",
                 plain["undefined"] == ["data_fdps_thing"]
                 and "stubbed" not in plain,
                 ", ".join(plain["undefined"])))
    return rows


def _selftest_real_images():
    """The synthetic image covers one record shape; the real ones cover what
    wlink actually emits, including the site that runs past the file end."""
    rows = []
    for rel in ("workspace/fdps_build/out/SMOKE.EXE",
                "workspace/ail_link/out/AILSMOK.EXE",
                "fdps_game_files/FDPS.EXE"):
        path = ROOT / rel
        if not path.is_file():
            rows.append(("parse %s" % rel, True, "skipped: absent"))
            continue
        try:
            prof = lefixup.profile(path.read_bytes())
            rows.append(("parse %s" % rel, prof["fixup_sites"] > 0,
                         "%d relocations" % prof["fixup_sites"]))
        except lefixup.LeError as exc:
            rows.append(("parse %s" % rel, False, str(exc)))
    return rows


def selftest():
    parser_rows, img, truth, geom = _selftest_parser()
    rows = (parser_rows + _selftest_compare(img, truth, geom)
            + _selftest_diagnostics() + _selftest_real_images())
    ok = True
    for name, passed, detail in rows:
        print("[selftest] %-38s %s (%s)"
              % (name, "ok" if passed else "FAIL", detail))
        ok = ok and passed
    return ok


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", nargs="?", default="check",
                    choices=("check", "update", "show", "selftest"))
    ap.add_argument("--target", action="append", choices=sorted(TARGETS),
                    help="repeatable; default is every registered target")
    ap.add_argument("--reason", help="why the baseline is being advanced "
                                     "(required by `update`)")
    ap.add_argument("--timeout", type=int, default=300,
                    help="backstop seconds per build stage")
    ap.add_argument("--with-audio", action="store_true",
                    help="also run the audio test suite; it opens a window and "
                         "needs fdps_game_files/")
    ap.add_argument("--skip-tests", action="store_true",
                    help="compare the build output only")
    ap.add_argument("--json", action="store_true",
                    help="print the structured result instead of a summary")
    args = ap.parse_args()

    if args.command == "selftest":
        ok = selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    if args.command == "show":
        show()
        return 0

    watcom = Path(os.environ.get("FDPS_WATCOM") or bm.WATCOM_DEFAULT)
    disc = Path(os.environ.get("FDPS_DISC1") or bm.DISC_DEFAULT)
    targets = args.target or sorted(TARGETS)

    if args.command == "update":
        if not args.reason:
            print("[gate] --reason is required: the baseline chain is the record "
                  "of what the gate has been told to accept")
            return 1
        if len(targets) != 1:
            print("[gate] update takes exactly one --target")
            return 1
        return 0 if update(targets[0], args.reason, watcom, disc,
                           args.timeout) else 1

    result = check(targets, watcom, disc, args.timeout,
                   with_audio=args.with_audio, skip_tests=args.skip_tests)
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    else:
        for row in result["targets"]:
            eq = (row.get("equivalence") or {}).get("verdict", "-")
            print("[gate] %-10s %-4s  %s" % (row["target"],
                                             row["verdict"].upper(), eq))
        if result["skipped_tests"]:
            print("[gate] skipped tests: %s" % ", ".join(result["skipped_tests"]))
    print("[result] %s" % result["verdict"])
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
