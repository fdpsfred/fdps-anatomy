"""gen_stubs.py -- the zero-filled module that lets a half-emitted program link.

Ticket 22 emits the game's functions one at a time, and ticket 23 emits the
data afterwards.  In between, every build has to link something that is not
all there: a function that reads a global whose real contents nobody has
written yet, or calls a neighbour that has not been emitted yet.  Compiling is
not the problem -- a declaration is enough for that -- but the linker wants a
definition for every symbol somebody references.

So the build links twice.  The first link carries no stubs at all, and the
undefined symbols it reports are the exact set of things the emitted code needs
and does not yet have.  That list is written to disk, because it is also
ticket 23's authoritative worklist: every build regenerates it, so it shrinks
by itself as data lands, and it can never drift from what the code actually
references.  The second link adds the module this file generates -- one
zero-filled definition per undefined symbol, typed and sized from ticket 17's
symbol table in routing.json -- and passes.

The stub module is never written into src/.  It is a build artefact under
workspace/, regenerated from scratch every time, and it holds nothing anybody
decided: a stub is the absence of a decision, and putting it beside the emitted
code would make the two indistinguishable a hundred functions later.

Three things deliberately fail rather than being stubbed:

  * a symbol routing.json does not know at all -- that is a typo or a CRT name
    the link line is missing, and quietly defining it would turn a real link
    error into a zero;
  * a symbol on routing.json's `skipped` list -- string literals, local array
    initialisers, switch tables.  Those belong inside the function that uses
    them, so one of them turning up undefined means an emitter wrote a
    reference to a symbol that must not exist in the rebuild;
  * anything already defined by src/, which cannot happen through the undefined
    list but is checked because a duplicate definition is a link error that
    reads nothing like its cause.

Usage: python tools/code_emit/gen_stubs.py [--syms a,b,c] [--selftest]
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ROUTING = HERE / "data" / "routing.json"

# Watcom 32-bit: int and pointers are four bytes, long is four as well.
BASE_TYPES = {
    "int": ("int", 4),
    "uint": ("unsigned int", 4),
    "dword": ("unsigned int", 4),
    "short": ("short", 2),
    "ushort": ("unsigned short", 2),
    "word": ("unsigned short", 2),
    "char": ("char", 1),
    "byte": ("unsigned char", 1),
    "uchar": ("unsigned char", 1),
    "undefined1": ("unsigned char", 1),
    "undefined2": ("unsigned short", 2),
    "undefined4": ("unsigned int", 4),
    "void": ("void", 0),
    "bool": ("unsigned char", 1),
}

PTR_SIZE = 4

TYPE_RX = re.compile(r"^(?P<base>[^\[\]*]+?)\s*(?P<ptr>\*+)?\s*"
                     r"(?:\[(?P<count>\d+)\])?$")


def load_routing():
    return json.loads(ROUTING.read_text(encoding="utf-8"))


def parse_type(text):
    """'/fdps/void_fn *[30]' -> ('void_fn', 1, 30).  None when unparseable."""
    m = TYPE_RX.match(text.strip())
    if not m:
        return None
    base = m.group("base").strip().rstrip("/").split("/")[-1]
    ptr = len(m.group("ptr") or "")
    count = int(m.group("count")) if m.group("count") else None
    return base, ptr, count


def declare(type_text, name, size):
    """A zero-filled definition of `name`, or None if the type is not one we
    can spell.  The caller falls back to a byte array of the recorded size.

    The declared type matters for exactly one reason -- alignment.  The
    contents are zero either way, but a dword the code reads as an int wants to
    be four-byte aligned, and a char array is not.
    """
    parsed = parse_type(type_text)
    if parsed is None:
        return None
    base, ptr, count = parsed

    if base == "void_fn":
        # Ticket 17's placeholder for `void (*)(void)`: a slot Ghidra could
        # only say is a code pointer.  Only ever seen as a pointer.
        if ptr != 1:
            return None
        elem = PTR_SIZE
        decl = ("void (*%s[%d])(void);" % (name, count) if count
                else "void (*%s)(void);" % name)
    elif ptr:
        elem = PTR_SIZE
        if base == "void":
            ctype = "void"
        elif base in BASE_TYPES:
            ctype = BASE_TYPES[base][0]
        else:
            return None
        stars = "*" * ptr
        decl = ("%s %s%s[%d];" % (ctype, stars, name, count) if count
                else "%s %s%s;" % (ctype, stars, name))
    else:
        if base not in BASE_TYPES or base == "void":
            return None
        ctype, elem = BASE_TYPES[base]
        decl = ("%s %s[%d];" % (ctype, name, count) if count
                else "%s %s;" % (ctype, name))

    want = elem * (count or 1)
    if size is not None and want != size:
        # The type and the recorded size disagree; trust neither and let the
        # caller lay down raw bytes of the size that was measured.
        return None
    return decl


# A definition, not a declaration.  The distinction is the whole point: an
# emitted .c is full of `extern` lines naming symbols it does not define, and
# counting one of those as a definition would refuse to stub the very symbol
# the file is waiting for.
EXTERN_RX = re.compile(r"^[ \t]*extern\b[^;]*;", re.M | re.S)
DEF_FUNC_RX = re.compile(r"^[A-Za-z_][A-Za-z0-9_ \t*]*?\b"
                         r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}()]*\)\s*\{",
                         re.M | re.S)
DEF_DATA_RX = re.compile(r"^[A-Za-z_][A-Za-z0-9_ \t*]+?\b"
                         r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?"
                         r"\s*(?:=[^;]*)?;", re.M | re.S)


def defined_in_src(src_dir):
    """Symbols src/ already defines, as far as a text scan can tell.

    This is a guard against a duplicate definition, not an analysis, and it
    exists for the message rather than the detection: a symbol src/ defines
    cannot come back from the linker as undefined, so if one ever does, the
    cause is something stranger than a missing definition and saying so beats
    quietly laying a second definition on top of it.
    """
    found = set()
    for path in sorted(src_dir.glob("*.c")):
        text = path.read_text(encoding="utf-8", errors="replace")
        text = EXTERN_RX.sub("", text)
        for rx in (DEF_FUNC_RX, DEF_DATA_RX):
            for m in rx.finditer(text):
                found.add(m.group(1))
    return found


def plan(symbols, routing=None, src_dir=None):
    """Work out what to write for each undefined symbol.

    Returns (text, resolved, unresolved) where resolved is one row per stub and
    unresolved one row per symbol that must not be stubbed.
    """
    routing = routing or load_routing()
    globals_by_name = {v["symbol"]: v for v in routing["globals"].values()}
    funcs_by_name = {v["name"]: dict(v, addr=k)
                     for k, v in routing["functions"].items()}
    skipped_by_name = {}
    for addr, row in routing.get("skipped", {}).items():
        name = row.get("symbol") or row.get("name")
        if name:
            skipped_by_name[name] = dict(row, addr=addr)
    already = defined_in_src(src_dir) if src_dir else set()

    resolved = []
    unresolved = []
    for name in sorted(set(symbols)):
        if name in already:
            unresolved.append({"symbol": name, "why": "src/ already defines it "
                               "-- the link error is not a missing definition"})
            continue
        if name in skipped_by_name:
            row = skipped_by_name[name]
            carried = row.get("carried_by") or ["?"]
            if isinstance(carried, str):
                carried = [carried]
            unresolved.append({
                "symbol": name,
                "why": "routing.json marks it not-emittable (%s, carried by %s)"
                       " -- it belongs inside the function that uses it, not as"
                       " a symbol of its own"
                       % (row.get("target") or "skipped", ", ".join(carried))})
            continue
        if name in globals_by_name:
            row = globals_by_name[name]
            decl = declare(row["type"], name, row.get("size"))
            if decl is None:
                size = row.get("size") or 4
                decl = "unsigned char %s[%d];" % (name, size)
                note = "raw %d bytes; %s could not be spelled or did not " \
                       "match the recorded size" % (size, row["type"])
            else:
                note = row["type"]
            resolved.append({"symbol": name, "kind": "data", "decl": decl,
                             "target": row.get("target"), "note": note})
            continue
        if name in funcs_by_name:
            row = funcs_by_name[name]
            resolved.append({"symbol": name, "kind": "function",
                             "decl": None, "target": row.get("target"),
                             "note": "not emitted yet (%s)" % row["addr"]})
            continue
        unresolved.append({"symbol": name, "why": "routing.json has never "
                           "heard of it -- a misspelling, or a library symbol "
                           "the link line is missing"})

    return render(resolved), resolved, unresolved


def render(resolved):
    """The stub module itself."""
    data = [r for r in resolved if r["kind"] == "data"]
    funcs = [r for r in resolved if r["kind"] == "function"]
    out = [
        "/* GENERATED by tools/code_emit/gen_stubs.py -- do not edit, do not",
        " * check in, do not copy into src/.",
        " *",
        " * One zero-filled definition per symbol the first link could not",
        " * resolve.  Data is typed and sized from ticket 17's symbol table so",
        " * that its alignment is right; functions return zero and do nothing.",
        " * Every one of them disappears from this file the moment the real",
        " * thing is emitted, because the file is regenerated from the link's",
        " * own complaints on every build.",
        " *",
        " * A test that depends on what one of these returns is testing this",
        " * file. */",
        "",
    ]
    if data:
        out.append("/* %d global(s) awaiting ticket 23. */" % len(data))
        for row in data:
            out.append("%-64s /* %s */" % (row["decl"], row["note"]))
        out.append("")
    if funcs:
        out.append("/* %d function(s) not emitted yet. */" % len(funcs))
        for row in funcs:
            # "*" keeps the symbol undecorated and `parm caller []` is the -4s
            # stack convention, exactly as an emitted function declares itself
            # (rebuild_info/emit_pipeline.md).  A stub whose name came out
            # decorated would not resolve the reference it exists to resolve.
            out.append('#pragma aux %s "*" parm caller [];' % row["symbol"])
            out.append("int %s(void) { return 0; } /* %s */"
                       % (row["symbol"], row["note"]))
        out.append("")
    if not resolved:
        # An empty translation unit is not valid C89, and this file is only
        # generated when there was something to put in it -- but a build that
        # generates it from an empty list should still compile.
        out.append("/* Nothing to stub. */")
        out.append("extern int fdps_stub_module_is_empty;")
        out.append("")
    return "\n".join(out)


# ----------------------------------------------------------------- selftest

def _selftest_rows():
    rows = []
    routing = {
        "globals": {
            "00060004": {"symbol": "data_a", "type": "/int", "size": 4,
                         "target": "x.c"},
            "00060008": {"symbol": "data_b", "type": "/uchar[200]",
                         "size": 200, "target": "x.c"},
            "0006000c": {"symbol": "data_c", "type": "/byte *", "size": 4,
                         "target": "x.c"},
            "00060010": {"symbol": "data_d", "type": "/fdps/void_fn *[30]",
                         "size": 120, "target": "x.c"},
            "00060014": {"symbol": "data_e", "type": "/some/unknown_t",
                         "size": 7, "target": "x.c"},
            "00060018": {"symbol": "data_f", "type": "/int[6]", "size": 8,
                         "target": "x.c"},
        },
        "functions": {"00010010": {"name": "fdps_thing", "target": "x.c"}},
        "skipped": {"00050000": {"symbol": "s_hello",
                                 "target": "<inline:string-literal>",
                                 "carried_by": ["fdps_thing"]}},
    }
    text, ok, bad = plan(["data_a", "data_b", "data_c", "data_d", "data_e",
                          "data_f", "fdps_thing", "s_hello", "who_is_this"],
                         routing=routing)
    by = {r["symbol"]: r for r in ok}
    rows.append(("int keeps its type", by["data_a"]["decl"] == "int data_a;",
                 by["data_a"]["decl"]))
    rows.append(("array keeps its count",
                 by["data_b"]["decl"] == "unsigned char data_b[200];",
                 by["data_b"]["decl"]))
    rows.append(("pointer stays a pointer",
                 by["data_c"]["decl"] == "unsigned char *data_c;",
                 by["data_c"]["decl"]))
    rows.append(("code-pointer table spelled out",
                 by["data_d"]["decl"] == "void (*data_d[30])(void);",
                 by["data_d"]["decl"]))
    rows.append(("unknown type falls back to bytes",
                 by["data_e"]["decl"] == "unsigned char data_e[7];",
                 by["data_e"]["decl"]))
    # int[6] is 24 bytes; a recorded 8 means one of the two is wrong, and
    # laying down 24 would overlap whatever comes next.
    rows.append(("size disagreement falls back to bytes",
                 by["data_f"]["decl"] == "unsigned char data_f[8];",
                 by["data_f"]["decl"]))
    rows.append(("function gets a stub and a pragma",
                 '#pragma aux fdps_thing "*" parm caller [];' in text
                 and "int fdps_thing(void) { return 0; }" in text, "yes"))
    badly = {r["symbol"]: r["why"] for r in bad}
    rows.append(("not-emittable symbol refused",
                 "s_hello" in badly, badly.get("s_hello", "STUBBED")))
    rows.append(("unknown symbol refused",
                 "who_is_this" in badly, badly.get("who_is_this", "STUBBED")))
    rows.append(("nothing else was refused", len(bad) == 2,
                 ", ".join(sorted(badly)) or "none"))
    # An emitted .c declares everything it borrows; only what it defines counts.
    import tempfile
    tmp = Path(tempfile.mkdtemp(prefix="fdps_stub_selftest_"))
    try:
        (tmp / "x.c").write_text(
            "extern unsigned char data_a;\n"
            "extern int fdps_thing(void *rec);\n"
            "int data_owned = 3;\n"
            "unsigned char buf_owned[8];\n"
            "int fdps_owned(int *p)\n{\n    return *p;\n}\n",
            encoding="utf-8")
        seen = defined_in_src(tmp)
        rows.append(("extern is not a definition",
                     "data_a" not in seen and "fdps_thing" not in seen,
                     ", ".join(sorted(seen))))
        rows.append(("definitions are found",
                     {"data_owned", "buf_owned", "fdps_owned"} <= seen,
                     ", ".join(sorted(seen))))
    finally:
        import shutil
        shutil.rmtree(str(tmp), ignore_errors=True)

    empty, _, _ = plan([], routing=routing)
    rows.append(("empty stub module is still valid C",
                 "extern int fdps_stub_module_is_empty;" in empty, "yes"))
    return rows


def do_selftest():
    ok = True
    for name, passed, detail in _selftest_rows():
        print("[selftest] %-38s %s (%s)"
              % (name, "ok" if passed else "FAIL", detail))
        ok = ok and passed
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--syms", default="",
                    help="comma-separated symbol names to plan stubs for")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        ok = do_selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    syms = [s.strip() for s in args.syms.split(",") if s.strip()]
    text, ok, bad = plan(syms, src_dir=ROOT / "src")
    print(text)
    for row in bad:
        print("UNRESOLVED %s: %s" % (row["symbol"], row["why"]),
              file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
