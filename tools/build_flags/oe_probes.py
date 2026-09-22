"""Probe what -oe and -d2 do in Watcom 10.0a C, and check each answer against
the shape FDPS.LE has.

Probes (oe_probes/, all 8.3 names):

    kwinl.c / kwuinl.c / kwplain.c
        `_inline`, `__inline`, `inline` on a static function.  Expected: every
        one is a syntax error E1009 -- 10.0a C has no inline keyword, so the
        expansions in FDPS.LE cannot come from the source.
    expand.c
        a plain static callee called once, plus a table-indirect call with a
        memory argument and one with an immediate argument.  Compiled under a
        matrix of -od / -d2 / -oe; reports whether the callee is expanded, the
        push form of each call, and the register the table call indexes with.
    context.c
        one tiny callee called from one context per function; reports which
        contexts -oe leaves as a real CALL.  A call in the right operand of
        && / || is never expanded; in an expression holding two calls the
        left one is expanded and the right one stays a CALL.
    thresh.c
        callees of 1..16 identical statements; reports, per -oe=N, the largest
        one still expanded.  Shows that N is a size limit and where the default
        sits; the window that matches FDPS.LE comes from oe_threshold.py.

Image side (no compiling): every table-indirect call `CALL [reg+disp]` in a
pool_fdps function of fdps_game_files/FDPS.LE is classified by the four
instructions before it, so the probe result can be compared with a count.

Compiling runs in DOSBox-X through push_form.dos_run.  Outputs land in
workspace/build_flags/oe_probes/.  Exit code 0 only when every PASS/FAIL row
passes.

    python oe_probes.py
"""

import collections
import os
import re
import shutil
import sys

import capstone

import fn_match
import push_form

HERE = os.path.dirname(os.path.abspath(__file__))
PROBES = os.path.join(HERE, "oe_probes")
OUT = os.path.join(fn_match.REPO, "workspace", "build_flags", "oe_probes")

BASE = "-bt=dos4g -mf -zq -4s -fpi -s -ot"
KEYWORDS = [("kwinl", "_inline"), ("kwuinl", "__inline"), ("kwplain", "inline")]
EXPAND_SETS = [("xa", "-od"), ("xd", "-d2"), ("xe", "-oe -od"),
               ("xb", "-oe -d2"), ("xh", "-oe -od -d2"), ("xr", "-d2 -oe"),
               ("xq", "-oe=25 -d2")]
CONTEXT_SETS = [("cb", "-oe -d2"), ("cq", "-oe=25 -d2"), ("c9", "-oe=90 -d2")]
THRESH_N = [None, 5, 10, 15, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 30, 35, 40,
            50, 60, 80, 100]
# contexts the original never expands (000107be, 0002db22, 0002b034..)
NOT_EXPANDED = {"c_rhsand", "c_rhsor", "c_rhsao", "c_two", "c_rgb"}

PUSH = re.compile(r"^push\s+(.*)$")
MOV_TO = re.compile(r"^mov\s+(e[a-d]x),(.*)$")


def push_forms(text):
    """Classify every push in one function's instruction list."""
    out = []
    for i, ins in enumerate(text):
        m = PUSH.match(ins)
        if not m:
            continue
        op = m.group(1).strip()
        if "[" in op:
            out.append("direct")
        elif re.match(r"^e(?:ax|bx|cx|dx|si|di|bp)$", op):
            prev = MOV_TO.match(text[i - 1]) if i else None
            if prev and prev.group(1) == op and "[" in prev.group(2):
                out.append("staged")
            elif op in ("ebx", "esi", "edi", "ebp") and i < 4:
                continue            # prologue register saves
            else:
                out.append("reg")
        else:
            out.append("imm")
    return out


def table_call(text):
    """(index register, push form of the argument) for a table call body."""
    for i, ins in enumerate(text):
        m = re.match(r"^call\s+(?:dword ptr\s+)?\S*\[(e\w\w)\]$", ins)
        if m:
            forms = push_forms(text[:i])
            return m.group(1), forms[-1] if forms else "-"
    return "-", "-"


def compile_probes(work):
    os.makedirs(work, exist_ok=True)
    for name in os.listdir(PROBES):
        shutil.copyfile(os.path.join(PROBES, name), os.path.join(work, name))
    lines, expect = [], []
    for stem, _kw in KEYWORDS:
        lines.append("wcc386 %s -oe -d2 -fo=%s.obj %s.c >%s.err" % (BASE, stem, stem, stem))
        expect.append(stem + ".err")
    for tag, fl in EXPAND_SETS:
        lines.append("wcc386 %s %s -fo=%s.obj expand.c >>build.out" % (BASE, fl, tag))
        lines.append("wdisasm -l=%s.lst -e -p %s.obj >>build.out" % (tag, tag))
        expect.append(tag + ".lst")
    for tag, fl in CONTEXT_SETS:
        lines.append("wcc386 %s %s -fo=%s.obj context.c >>build.out" % (BASE, fl, tag))
        lines.append("wdisasm -l=%s.lst -e -p %s.obj >>build.out" % (tag, tag))
        expect.append(tag + ".lst")
    for n in THRESH_N:
        tag = "t%s" % ("def" if n is None else n)
        oe = "-oe" if n is None else "-oe=%d" % n
        lines.append("wcc386 %s %s -d2 -fo=%s.obj thresh.c >>build.out" % (BASE, oe, tag))
        lines.append("wdisasm -l=%s.lst -e -p %s.obj >>build.out" % (tag, tag))
        expect.append(tag + ".lst")
    started = push_form.dos_run(work, push_form.WATCOM, lines, expect, timeout=900)
    stale = [e for e in expect if not os.path.exists(os.path.join(work, e))
             or os.path.getmtime(os.path.join(work, e)) < started - push_form.MTIME_SLACK]
    if stale:
        raise SystemExit("no fresh output for %s; see %s"
                         % (stale, os.path.join(work, "build.out")))
    return started


def image_table_calls():
    """Classify every CALL [reg+disp] in pool_fdps functions of FDPS.LE."""
    orig = fn_match.Original()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    shapes = collections.defaultdict(list)
    with open(fn_match.FUNCTIONS_TXT, encoding="utf-8") as fh:
        for line in fh:
            cols = [c.strip() for c in line.split("|")]
            if line.startswith("#") or len(cols) < 7 or "pool_fdps" not in cols[6]:
                continue
            addr, size = int(cols[0], 16), int(cols[1], 16)
            start = orig.file_off(addr)
            ins = list(md.disasm(orig.data[start:start + size], addr))
            for i, x in enumerate(ins):
                m = re.match(r"dword ptr \[(e\w\w) \+ 0x[0-9a-f]+\]$", x.op_str)
                if x.mnemonic != "call" or not m:
                    continue
                reg = m.group(1)
                prev = ins[max(0, i - 4):i]
                pushed = [p for p in prev if p.mnemonic == "push"]
                last = pushed[-1] if pushed else None
                if last is None:
                    arg = "no push"
                elif "[" in last.op_str:
                    arg = "direct memory push"
                elif last.op_str.startswith("e"):
                    j = prev.index(last)
                    arg = ("staged memory push"
                           if j and prev[j - 1].mnemonic == "mov"
                           and "[" in prev[j - 1].op_str else "register push")
                else:
                    arg = "immediate push"
                shapes[(reg, arg)].append(x.address)
    return shapes


def main():
    work = OUT
    compile_probes(work)
    rows = []

    def check(label, ok, detail):
        rows.append(ok)
        print("[%s] %-58s %s" % ("PASS" if ok else "FAIL", label, detail))

    print("== inline keywords (flags: %s -oe -d2)" % BASE)
    for stem, kw in KEYWORDS:
        with open(os.path.join(work, stem + ".err"), errors="replace") as fh:
            err = fh.read()
        m = re.search(r"Error! (E\d+): ([^\r\n]*)", err)
        obj = os.path.exists(os.path.join(work, stem + ".obj"))
        check("`%s` is rejected with E1009" % kw,
              bool(m and m.group(1) == "E1009") and not obj,
              m.group(0) if m else "(no error; obj=%s)" % obj)

    print("\n== expansion and push form (expand.c)")
    print("%-4s %-14s %-9s %-34s %-10s %-16s %s" % ("tag", "flags", "expanded",
          "outer pushes", "twin", "tcall", "tcall_imm"))
    got = {}
    for tag, fl in EXPAND_SETS:
        funcs = fn_match.parse_listing(os.path.join(work, tag + ".lst"))
        outer = funcs["outer"]
        expanded = "inl" not in outer["calls"]
        forms = push_forms(outer["text"])
        twin = push_forms(funcs["inl"]["text"]) if "inl" in funcs else []
        tc = table_call(funcs["tcall"]["text"])
        ti = table_call(funcs["tcall_imm"]["text"])
        got[tag] = (expanded, forms, twin, tc, ti)
        print("%-4s %-14s %-9s %-34s %-10s %-16s %s" % (
            tag, fl, "yes" if expanded else "no", ",".join(forms),
            ",".join(sorted(set(twin))), "%s/%s" % tc, "%s/%s" % ti))
    want_outer = ["direct", "direct", "staged", "staged"]
    for tag in ("xb", "xh", "xr", "xq"):
        exp, forms, twin, tc, ti = got[tag]
        check("%s: expanded, copy direct + own call staged" % tag,
              exp and forms == want_outer and set(twin) == {"staged"},
              "outer=%s twin=%s" % (",".join(forms), ",".join(twin)))
        check("%s: table call EDX + staged, immediate arg EAX" % tag,
              tc == ("edx", "staged") and ti == ("eax", "imm"),
              "tcall=%s/%s tcall_imm=%s/%s" % (tc + ti))
    exp, forms, twin, tc, ti = got["xa"]
    check("xa (-od): not expanded, direct pushes, table call EAX + direct",
          not exp and set(forms) == {"direct"} and tc == ("eax", "direct"),
          "outer=%s tcall=%s/%s" % (",".join(forms), tc[0], tc[1]))
    exp, forms, _t, _tc, _ti = got["xe"]
    check("xe (-oe -od): expanded but every push direct",
          exp and set(forms) == {"direct"}, "outer=%s" % ",".join(forms))
    exp = got["xd"][0]
    check("xd (-d2, no -oe): not expanded", not exp, "")

    print("\n== FDPS.LE table-indirect calls in pool_fdps functions")
    shapes = image_table_calls()
    for (reg, arg), sites in sorted(shapes.items()):
        print("  CALL [%s+disp] after %-20s %2d  %s" % (
            reg, arg, len(sites), " ".join("%08x" % a for a in sites)))
    staged = shapes.get(("edx", "staged memory push"), [])
    direct = [a for (r, g), s in shapes.items() if g == "direct memory push" for a in s]
    other_edx = [a for (r, g), s in shapes.items() if r == "edx"
                 and g != "staged memory push" for a in s]
    check("7 memory-argument table calls, all EDX + staged",
          len(staged) == 7 and not direct and not other_edx,
          "staged/EDX=%d direct=%d other EDX=%d"
          % (len(staged), len(direct), len(other_edx)))

    print("\n== call contexts (context.c)")
    for tag, fl in CONTEXT_SETS:
        funcs = fn_match.parse_listing(os.path.join(work, tag + ".lst"))
        kept = sorted(n for n, f in funcs.items() if n.startswith("c_")
                      and any(c in ("s1", "red_of") for c in f["calls"]))
        ctx = sorted(n for n in funcs if n.startswith("c_"))
        print("  %-12s callers %d, CALL kept in: %s" % (fl, len(ctx), ", ".join(kept)))
        check("%s: exactly the RHS-of-&&/|| and two-call contexts keep CALL" % fl,
              set(kept) == NOT_EXPANDED, "")
        two = funcs["c_two"]["calls"].count("s1")
        rgb = funcs["c_rgb"]["calls"].count("red_of")
        # Only the later call of the pair stays a CALL; the left one is
        # expanded.  (FDPS.LE's three-call expression at 0002b034 keeps all
        # three, see oe_threshold.py / fn_match.py palv for that case.)
        check("%s: two-call expression: left expanded, right kept" % fl,
              two == 1 and rgb == 1, "CALLs left: c_two %d/2, c_rgb %d/2" % (two, rgb))

    print("\n== -oe=N size limit (thresh.c, -d2)")
    for n in THRESH_N:
        tag = "t%s" % ("def" if n is None else n)
        funcs = fn_match.parse_listing(os.path.join(work, tag + ".lst"))
        exp = [k for k in range(1, 17)
               if "f%02d" % k not in funcs["c%02d" % k]["calls"]]
        mono = exp == list(range(1, len(exp) + 1))
        print("  %-8s expanded f01..f%02d%s" % (
            "-oe" if n is None else "-oe=%d" % n, max(exp) if exp else 0,
            "" if mono else "  (not a prefix: %s)" % exp))

    failed = rows.count(False)
    print("\n%d/%d checks pass" % (len(rows) - failed, len(rows)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
