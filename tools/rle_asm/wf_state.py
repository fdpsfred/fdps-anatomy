"""wf_state.py -- the file side of ticket 22.3's workflow.

A workflow script has no filesystem, so everything it needs to know about
progress comes from here, and it is always read off the files rather than taken
from what an agent says it did (ADR-0007 5.1):

  transcription   workspace/rle_asm/frag/<prefix>.asm, the routine as a
                  standalone WASM module, and verdicts/<address>.json, which
                  records the hash of the fragment it vouches for.  A fragment
                  edited after its verdict was stamped is not done (5.7).
  landing         the five src/*.asm files exist, the tree is in the assembly
                  state (switch_impl.py), they are committed, the check against
                  the objects the emittest build assembled passes, and the gate
                  result is newer than the assembly.
  tests           workspace/rle_asm/tests/<address>.json maps every C-translation
                  case the routine owns to the dispatcher case that replaces it,
                  every named case is registered in its runner, the build result
                  is green and newer than the test file, and the file is
                  committed.

Subcommands (all print one JSON object):
    status                  every routine's progress, for the workflow's resume
    stamp ADDR              run the fragment check and write its result, the
                            fragment hash and the instruction count into the
                            verdict (the transcriber runs this; it judges nothing)
    verify-frag ADDR        is the transcription done
    verify-land             is the landing done
    verify-tests ADDR       is the routine's dispatcher test port done
    coverage                does the union of the test verdicts account for every
                            case of the four C-translation test files
    selftest

Exit: 0 when the verdict asked about is `ok`.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import asm_match as am  # noqa: E402

ROOT = am.ROOT
FRAG = am.WORK / "frag"
VERDICTS = am.WORK / "verdicts"
TESTV = am.WORK / "tests"
EMIT_RESULT = ROOT / "workspace" / "code_emit" / "result.json"
GATE_RESULT = ROOT / "workspace" / "build_gate" / "result.json"

# The C-translation test file each assembly file's kernels were covered by,
# and the dispatcher test file that covers them now.
C_TESTS = {"rlebase": "tests/rle.c", "rlepal": "tests/rlecolor.c",
           "rleturn": "tests/rlerot.c", "rlemix": "tests/rleblend.c"}

RUN_TEST_RX = re.compile(r"RUN_TEST\(\s*([A-Za-z_]\w*)\s*\)")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def row(addr):
    return am.roster_entry(addr)


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8")), None
    except (OSError, ValueError) as exc:
        return None, str(exc)


def git_dirty(rel):
    out = subprocess.run(["git", "status", "--porcelain", "--", rel], cwd=str(ROOT),
                         capture_output=True, text=True)
    return bool(out.stdout.strip())


def runner_cases(text, stem):
    """RUN_TEST names inside run_<stem>_tests, outside any #if 0."""
    sys.path.insert(0, str(ROOT / "tools" / "code_emit"))
    import build_emit
    live = build_emit.strip_if0(text)
    m = re.search(r"void\s+run_%s_tests\s*\(\s*void\s*\)\s*\{(.*?)^\}" % stem, live,
                  re.S | re.M)
    return RUN_TEST_RX.findall(m.group(1)) if m else []


# ------------------------------------------------------------ transcription

def frag_result(addr):
    addr, name, _f, prefix = row(addr)
    path = FRAG / (prefix + ".asm")
    if not path.is_file():
        return None, ["missing %s" % path]
    img, names = am.load_original()
    objs, diags, _t = am.assemble([path], am.WORK / "tmp" / ("st-" + prefix))
    problems = list(diags)
    problems += am.scan_source(path.read_text(encoding="latin-1"), img.ranges())
    obj = objs.get(prefix.upper())
    count = None
    if obj is None:
        problems.append("no object")
    else:
        if set(obj.publics) != {name}:
            problems.append("publics are %s, expected only %s"
                            % (sorted(obj.publics), name))
        if name in obj.publics:
            problems += am.check_routine(img, names, obj, int(addr, 16), name)
            count = len(am.decode(am.rebuilt_side(obj, name))[0])
    return {"path": path, "sha": sha(path), "instructions": count}, problems


def cmd_stamp(addr):
    addr, name, _f, prefix = row(addr)
    vpath = VERDICTS / (addr + ".json")
    v, err = read_json(vpath)
    if v is None:
        return {"ok": False, "problems": ["verdict unreadable: %s" % err]}
    info, problems = frag_result(addr)
    if info is None:
        return {"ok": False, "problems": problems}
    v.update({"address": addr, "name": name,
              "frag": "workspace/rle_asm/frag/%s.asm" % prefix,
              "frag_sha256": info["sha"], "instructions": info["instructions"],
              "check": "pass" if not problems else "fail",
              "check_problems": problems[:40]})
    vpath.write_text(json.dumps(v, indent=2) + "\n", encoding="utf-8")
    return {"ok": not problems, "problems": problems[:40]}


def cmd_verify_frag(addr):
    addr, name, _f, prefix = row(addr)
    v, err = read_json(VERDICTS / (addr + ".json"))
    if v is None:
        return {"ok": False, "problems": ["verdict: %s" % err]}
    problems = []
    for key in ("address", "name", "frag", "frag_sha256", "check", "instructions",
                "labels", "notes", "concerns"):
        if key not in v:
            problems.append("verdict lacks %s" % key)
    if v.get("address") != addr or v.get("name") != name:
        problems.append("verdict names %s %s" % (v.get("address"), v.get("name")))
    info, fproblems = frag_result(addr)
    if info is None:
        return {"ok": False, "problems": problems + fproblems}
    if v.get("frag_sha256") != info["sha"]:
        problems.append("the fragment changed after the verdict was stamped")
    if v.get("check") != "pass":
        problems.append("verdict records check=%r" % v.get("check"))
    problems += fproblems
    open_concerns = [c for c in v.get("concerns", []) if not c.get("resolved")]
    return {"ok": not problems, "problems": problems[:40],
            "open_concerns": len(open_concerns)}


# ------------------------------------------------------------------ landing

def cmd_verify_land():
    problems = []
    sys.path.insert(0, str(HERE))
    import switch_impl
    try:
        st = switch_impl.state()
        if st != "asm":
            problems.append("tree is in the %s state" % st)
    except switch_impl.SwitchError as exc:
        problems.append(str(exc))
    newest = 0
    for stem in am.ASM_FILES:
        p = am.SRC / (stem + ".asm")
        if not p.is_file():
            problems.append("missing src/%s.asm" % stem)
            continue
        newest = max(newest, p.stat().st_mtime)
        if git_dirty("src/%s.asm" % stem):
            problems.append("src/%s.asm is not committed" % stem)
    r = subprocess.run([sys.executable, str(HERE / "asm_match.py"), "check", "--json"],
                       cwd=str(ROOT), capture_output=True, text=True)
    if r.returncode != 0:
        problems.append("asm_match check fails: %s" % r.stdout[-1500:])
    g, err = read_json(GATE_RESULT)
    if g is None:
        problems.append("no gate result: %s" % err)
    else:
        if g.get("verdict") != "PASS":
            problems.append("the gate result is not a pass")
        if GATE_RESULT.stat().st_mtime < newest:
            problems.append("the gate result is older than the assembly")
    return {"ok": not problems, "problems": problems}


# -------------------------------------------------------------------- tests

def cmd_verify_tests(addr):
    addr, name, stem, _p = row(addr)
    if stem == "rledisp":
        return {"ok": False, "problems": ["the dispatcher's cases were moved, not ported"]}
    v, err = read_json(TESTV / (addr + ".json"))
    if v is None:
        return {"ok": False, "problems": ["test verdict: %s" % err]}
    problems = []
    for key in ("address", "name", "test_file", "cases", "uncovered"):
        if key not in v:
            problems.append("test verdict lacks %s" % key)
    test_rel = "tests/%s.c" % stem
    if v.get("test_file") != test_rel:
        problems.append("test_file is %r, expected %s" % (v.get("test_file"), test_rel))
    tpath = ROOT / test_rel
    live = runner_cases(tpath.read_text(encoding="utf-8"), stem) if tpath.is_file() else []
    ctext = (ROOT / C_TESTS[stem]).read_text(encoding="utf-8")
    c_cases = set(RUN_TEST_RX.findall(ctext))
    for c in v.get("cases", []):
        if c.get("c_case") not in c_cases:
            problems.append("c_case %r is not a case of %s" % (c.get("c_case"),
                                                              C_TESTS[stem]))
        for d in c.get("dispatch_cases", []):
            if d not in live:
                problems.append("dispatch case %r is not registered in run_%s_tests"
                                % (d, stem))
        if not c.get("dispatch_cases"):
            problems.append("c_case %r maps to no dispatcher case" % c.get("c_case"))
    for u in v.get("uncovered", []):
        if u.get("c_case") not in c_cases or not u.get("why"):
            problems.append("uncovered entry %r needs a real case and a reason" % u)
    r, err = read_json(EMIT_RESULT)
    if r is None:
        problems.append("no build result: %s" % err)
    else:
        if not r.get("gate_pass"):
            problems.append("the last build_emit run did not pass")
        if tpath.is_file() and EMIT_RESULT.stat().st_mtime < tpath.stat().st_mtime:
            problems.append("the build result is older than %s" % test_rel)
    if git_dirty(test_rel):
        problems.append("%s is not committed" % test_rel)
    return {"ok": not problems, "problems": problems,
            "uncovered": len(v.get("uncovered", []))}


def cmd_coverage():
    claimed = {}
    for addr, _n, stem, _p in am.ROSTER:
        if stem == "rledisp":
            continue
        v, _e = read_json(TESTV / (addr + ".json"))
        if v is None:
            continue
        for c in v.get("cases", []) + v.get("uncovered", []):
            claimed.setdefault(C_TESTS[stem], set()).add(c.get("c_case"))
    missing = {}
    for stem, rel in C_TESTS.items():
        cases = set(RUN_TEST_RX.findall((ROOT / rel).read_text(encoding="utf-8")))
        left = sorted(cases - claimed.get(rel, set()))
        if left:
            missing[rel] = left
    return {"ok": not missing, "unclaimed": missing}


# --------------------------------------------------------------------- status

def cmd_status():
    out = []
    for addr, name, stem, prefix in am.ROSTER:
        v, _e = read_json(VERDICTS / (addr + ".json"))
        f = FRAG / (prefix + ".asm")
        frag_state = "none"
        if v is not None and f.is_file():
            frag_state = ("stamped" if v.get("check") == "pass"
                          and v.get("frag_sha256") == sha(f) else "stale")
        elif f.is_file():
            frag_state = "unstamped"
        t, _e = read_json(TESTV / (addr + ".json"))
        out.append({"address": addr, "name": name, "file": stem, "prefix": prefix,
                    "frag": frag_state,
                    "open_concerns": len([c for c in (v or {}).get("concerns", [])
                                          if not c.get("resolved")]),
                    "tests": "n/a" if stem == "rledisp" else
                             ("verdict" if t is not None else "none")})
    landed = all((am.SRC / (s + ".asm")).is_file() for s in am.ASM_FILES) and \
        not any(git_dirty("src/%s.asm" % s) for s in am.ASM_FILES)
    return {"ok": True, "routines": out, "landed": landed}


def selftest():
    rows = []
    text = ("#if 0 /* ref */\nvoid run_rlebase_tests(void)\n{\n    RUN_TEST(old);\n}\n"
            "#endif\nvoid run_rlebase_tests(void)\n{\n    RUN_TEST(pass_a);\n"
            "    RUN_TEST(pass_b);\n}\n")
    got = runner_cases(text, "rlebase")
    rows.append(("runner cases skip #if 0", got == ["pass_a", "pass_b"], got))
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "x.asm"
        p.write_text("a", encoding="latin-1")
        h1 = sha(p)
        p.write_text("b", encoding="latin-1")
        rows.append(("an edited fragment has another hash", h1 != sha(p), ""))
    ok = True
    for label, passed, detail in rows:
        print("[selftest] %-40s %s %s" % (label, "ok" if passed else "FAIL",
                                          "" if passed else detail))
        ok = ok and passed
    print("[result] %s" % ("PASS" if ok else "FAIL"))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("status", "stamp", "verify-frag", "verify-land",
                                    "verify-tests", "coverage", "selftest"))
    ap.add_argument("addr", nargs="?")
    args = ap.parse_args()
    if args.cmd == "selftest":
        return 0 if selftest() else 1
    try:
        if args.cmd == "status":
            res = cmd_status()
        elif args.cmd == "stamp":
            res = cmd_stamp(args.addr)
        elif args.cmd == "verify-frag":
            res = cmd_verify_frag(args.addr)
        elif args.cmd == "verify-land":
            res = cmd_verify_land()
        elif args.cmd == "verify-tests":
            res = cmd_verify_tests(args.addr)
        else:
            res = cmd_coverage()
    except am.MatchError as exc:
        res = {"ok": False, "problems": ["tool error: %s" % exc]}
    print(json.dumps(res, indent=2))
    return 0 if res.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
