"""gen_types.py -- write src/fdpstype.h from ticket 17's struct layouts.

Every emitted function that touches a unit record, a spell entry or a save slot
needs the same struct definition, and there are 514 of them.  Letting each
emitter transcribe the layout out of Ghidra would give 514 chances to get a
field offset or a signedness wrong, in a file the next emitter then trusts; and
the layouts are not a per-function judgement in the first place -- ticket 17
settled them once, for the whole program.  So they are transcribed once, by
this script, from the versioned snapshot of the Ghidra database
(ghidra_snapshot/data_types.txt), and rebuild_info/code_layout.md names the
result as the one place a game struct is defined.

Two things the generated header must get right, both of which are silent when
wrong:

  * Holes.  Ghidra shows the fields that have names; the bytes between two of
    them are real and the fields after them move if they are dropped.  Every
    gap becomes an explicit filler array.
  * Packing.  wcc386's default is already byte packing, but a record whose
    fields are laid out at the offsets the original used must not depend on a
    flag staying where it is, so the header says #pragma pack(1) itself
    (rebuild_info/pitfalls.md).

Field descriptions are deliberately NOT copied here.  They live in Ghidra and
in the knowledge base, and a second copy inside a generated file is a copy that
goes stale without anybody noticing.

Usage: python tools/code_emit/gen_types.py [--check] [--selftest]
  --check   fail if src/fdpstype.h differs from what would be generated
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT = ROOT / "ghidra_snapshot" / "data_types.txt"
HEADER = ROOT / "src" / "fdpstype.h"
TEST = ROOT / "tests" / "fdpstype.c"

STRUCT_RX = re.compile(r"^struct (/fdps/(fdps_[A-Za-z0-9_]+))\s*\|\s*"
                       r"size=(0x[0-9a-f]+)")
FIELD_RX = re.compile(r"^\s+\+(0x[0-9a-f]+)\s*\|\s*(0x[0-9a-f]+)\s*\|\s*"
                      r"([^|]+?)\s*\|\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:\||$)")
DESC_RX = re.compile(r"^\s+#\s?(.*)$")
ANY_RX = re.compile(r"^(struct|union|enum|typedef|functiondef)\s")

BASE = {
    "byte": "unsigned char",
    "uchar": "unsigned char",
    "undefined1": "unsigned char",
    "char": "char",
    "sbyte": "signed char",
    "word": "unsigned short",
    "ushort": "unsigned short",
    "undefined2": "unsigned short",
    "short": "short",
    "dword": "unsigned int",
    "uint": "unsigned int",
    "undefined4": "unsigned int",
    "int": "int",
    "long": "long",
    "ulong": "unsigned long",
    "float": "float",
    "double": "double",
    "void": "void",
    "bool": "unsigned char",
    "pointer": "void *",
}

TYPE_RX = re.compile(r"^(?P<base>[^\[\]*]+?)\s*(?P<ptr>\*+)?\s*"
                     r"(?:\[(?P<count>\d+)\])?$")


def parse(text):
    """Every /fdps/fdps_* struct in the snapshot, in the order it appears."""
    structs = []
    current = None
    for line in text.splitlines():
        m = STRUCT_RX.match(line)
        if m:
            current = {"name": m.group(2), "size": int(m.group(3), 16),
                       "fields": [], "desc": ""}
            structs.append(current)
            continue
        if ANY_RX.match(line):
            current = None
            continue
        if current is None:
            continue
        m = FIELD_RX.match(line)
        if m:
            current["fields"].append({
                "offset": int(m.group(1), 16), "size": int(m.group(2), 16),
                "type": m.group(3).strip(), "name": m.group(4)})
            continue
        m = DESC_RX.match(line)
        if m and not current["desc"]:
            current["desc"] = m.group(1).strip()
    return structs


def spell(type_text, name, size):
    """One field, as C.  None when the type cannot be spelled."""
    m = TYPE_RX.match(type_text)
    if not m:
        return None
    base = m.group("base").strip()
    ptr = len(m.group("ptr") or "")
    count = m.group("count")
    count = int(count) if count else None

    if base.startswith("/fdps/") or base.startswith("/"):
        leaf = base.rstrip("/").split("/")[-1]
    else:
        leaf = base
    if leaf in BASE:
        ctype = BASE[leaf]
    elif leaf.startswith("fdps_"):
        ctype = "struct " + leaf
    else:
        return None

    stars = "*" * ptr
    if count:
        return "%s %s%s[%d];" % (ctype, stars, name, count)
    return "%s %s%s;" % (ctype, stars, name)


def render_struct(st):
    """A struct, with every hole in it made explicit.

    A named field that does not start where the previous one ended means bytes
    nobody named, and leaving them out would silently move every field after
    them.  They come out as `gap_<offset>` so that the offset is checkable
    against Ghidra by reading the name.
    """
    lines = []
    if st["desc"]:
        lines.append("/* %s */" % st["desc"])
    lines.append("struct %s {" % st["name"])
    cursor = 0
    for f in st["fields"]:
        if f["offset"] > cursor:
            lines.append("    unsigned char gap_%03x[%d];"
                         % (cursor, f["offset"] - cursor))
        elif f["offset"] < cursor:
            return None, ("%s: field %s at +0x%x overlaps the field before it"
                          % (st["name"], f["name"], f["offset"]))
        decl = spell(f["type"], f["name"], f["size"])
        if decl is None:
            return None, ("%s: cannot spell the type %s of field %s"
                          % (st["name"], f["type"], f["name"]))
        lines.append("    " + decl)
        cursor = f["offset"] + f["size"]
    if cursor < st["size"]:
        lines.append("    unsigned char gap_%03x[%d];"
                     % (cursor, st["size"] - cursor))
    elif cursor > st["size"]:
        return None, ("%s: fields run to 0x%x, past the declared size 0x%x"
                      % (st["name"], cursor, st["size"]))
    lines.append("};")
    return lines, None


def order(structs):
    """Definitions before the structs that embed them.

    Only whole-struct members create an ordering constraint; a pointer to a
    struct does not, but there are none of those here and treating them the
    same costs nothing.
    """
    by_name = {st["name"]: st for st in structs}
    needs = {}
    for st in structs:
        want = set()
        for f in st["fields"]:
            leaf = f["type"].split("[")[0].strip().rstrip("*").strip()
            leaf = leaf.rstrip("/").split("/")[-1]
            if leaf in by_name and leaf != st["name"]:
                want.add(leaf)
        needs[st["name"]] = want

    out = []
    placed = set()
    remaining = [st["name"] for st in structs]
    while remaining:
        progressed = False
        for name in list(remaining):
            if needs[name] <= placed:
                out.append(by_name[name])
                placed.add(name)
                remaining.remove(name)
                progressed = True
        if not progressed:
            sys.exit("struct definitions are cyclic: %s" % ", ".join(remaining))
    return out


def render(structs):
    body = [
        "/* fdpstype.h -- the game's own record layouts.",
        " *",
        " * GENERATED by tools/code_emit/gen_types.py from",
        " * ghidra_snapshot/data_types.txt, which is ticket 17's settled answer",
        " * for every one of these.  Do not edit by hand: fix the layout in",
        " * Ghidra, re-export the snapshot, and regenerate.",
        " *",
        " * What each field means is written on the field in Ghidra and in the",
        " * knowledge base; it is deliberately not copied here, because a second",
        " * copy is one that goes stale.",
        " *",
        " * Every record is byte-packed.  wcc386's default is already byte",
        " * packing, but these offsets are the original's and must not depend on",
        " * a compiler flag staying where it is (rebuild_info/pitfalls.md).",
        " */",
        "#ifndef FDPSTYPE_H",
        "#define FDPSTYPE_H",
        "",
        "#pragma pack(1)",
        "",
    ]
    problems = []
    for st in order(structs):
        lines, err = render_struct(st)
        if err:
            problems.append(err)
            continue
        body.extend(lines)
        body.append("")
    body.append("#pragma pack()")
    body.append("")
    body.append("#endif")
    body.append("")
    return "\n".join(body), problems


def render_test(structs):
    """A test unit that measures the header against the offsets it came from.

    The header is generated, so it cannot disagree with the snapshot by
    accident -- but the compiler can disagree with both of them.  A packing
    default that moves, a field type that widens, an embedded struct that
    grows: all three compile clean and all three shift every field after the
    one that changed.  Only the compiler's own sizeof and offsetof can say
    whether the layout the original used is the layout that was built, so this
    asserts every field of every record, not a sample.
    """
    lines = [
        "/* fdpstype.c -- the game record layouts, measured as the compiler",
        " * built them.",
        " *",
        " * GENERATED by tools/code_emit/gen_types.py alongside src/fdpstype.h.",
        " * Do not edit by hand.",
        " *",
        " * Expected values come from ghidra_snapshot/data_types.txt, which is",
        " * ticket 17's reading of the original's own field offsets -- not from",
        " * the header, which would only prove the header equals itself.",
        " */",
        '#include <stddef.h>',
        '#include "testharn.h"',
        '#include "fdpstype.h"',
        "",
    ]
    ordered = order(structs)
    for st in ordered:
        lines.append("static void t_%s(void)" % st["name"])
        lines.append("{")
        lines.append("    CHECK_EQ((int) sizeof(struct %s), %d);"
                     % (st["name"], st["size"]))
        for f in st["fields"]:
            lines.append("    CHECK_EQ((int) offsetof(struct %s, %s), %d);"
                         % (st["name"], f["name"], f["offset"]))
        lines.append("}")
        lines.append("")
    lines.append("void run_fdpstype_tests(void)")
    lines.append("{")
    for st in ordered:
        lines.append("    RUN_TEST(t_%s);" % st["name"])
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


# ----------------------------------------------------------------- selftest

SAMPLE = """struct /fdps/fdps_inner | size=0x2 | align=0x1 | packing=disabled
    +0x000 | 0x1    | /byte | a | first
    +0x001 | 0x1    | /byte | b | second
    # A two-byte thing.
struct /fdps/fdps_outer | size=0x10 | align=0x1 | packing=disabled
    +0x000 | 0x2    | /fdps/fdps_inner | inner | embedded
    +0x004 | 0x4    | /dword | big | after a hole
    +0x008 | 0x6    | /byte[6] | tail | an array
    # An outer thing with a hole in it.
struct /crt/crt_thing | size=0x4 | align=0x1 | packing=disabled
    +0x000 | 0x4    | /dword | x | not ours
"""


def _selftest_rows():
    rows = []
    structs = parse(SAMPLE)
    rows.append(("only the game's structs are taken",
                 [s["name"] for s in structs]
                 == ["fdps_inner", "fdps_outer"],
                 ", ".join(s["name"] for s in structs)))
    text, problems = render(structs)
    rows.append(("nothing unspellable", not problems,
                 "; ".join(problems) or "none"))
    rows.append(("embedded struct is defined first",
                 text.index("struct fdps_inner {") < text.index("struct fdps_outer {"),
                 "yes"))
    rows.append(("hole between fields is filled",
                 "unsigned char gap_002[2];" in text, "yes"))
    rows.append(("hole at the end is filled",
                 "unsigned char gap_00e[2];" in text, "yes"))
    rows.append(("array keeps its count",
                 "unsigned char tail[6];" in text, "yes"))
    rows.append(("packing is stated", "#pragma pack(1)" in text
                 and "#pragma pack()" in text, "yes"))

    overlap = parse("struct /fdps/fdps_bad | size=0x4 | align=0x1 | "
                    "packing=disabled\n"
                    "    +0x000 | 0x4    | /dword | a | x\n"
                    "    +0x002 | 0x2    | /word | b | y\n")
    _, err = render_struct(overlap[0])
    rows.append(("overlapping fields are refused", err is not None,
                 err or "ACCEPTED"))

    unknown = parse("struct /fdps/fdps_odd | size=0x4 | align=0x1 | "
                    "packing=disabled\n"
                    "    +0x000 | 0x4    | /mystery/thing | a | x\n")
    _, err = render_struct(unknown[0])
    rows.append(("unknown field type is refused", err is not None,
                 err or "ACCEPTED"))
    return rows


def do_selftest():
    ok = True
    for name, passed, detail in _selftest_rows():
        print("[selftest] %-36s %s (%s)"
              % (name, "ok" if passed else "FAIL", detail))
        ok = ok and passed
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="do not write; fail if the header is out of date")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        ok = do_selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    if not SNAPSHOT.is_file():
        sys.exit("missing %s -- export the Ghidra snapshot first" % SNAPSHOT)
    structs = parse(SNAPSHOT.read_text(encoding="utf-8", errors="replace"))
    text, problems = render(structs)
    for line in problems:
        print("PROBLEM %s" % line, file=sys.stderr)
    if problems:
        return 1

    test = render_test(structs)

    if args.check:
        stale = []
        for path, want in ((HEADER, text), (TEST, test)):
            have = path.read_text(encoding="utf-8") if path.is_file() else ""
            if have != want:
                stale.append(str(path))
        if stale:
            print("out of date -- regenerate: %s" % ", ".join(stale),
                  file=sys.stderr)
            return 1
        print("%s and %s are up to date (%d struct(s))"
              % (HEADER, TEST, len(structs)))
        return 0

    HEADER.write_text(text, encoding="utf-8", newline="\n")
    TEST.write_text(test, encoding="utf-8", newline="\n")
    print("%d struct(s) -> %s, %s" % (len(structs), HEADER, TEST))
    return 0


if __name__ == "__main__":
    sys.exit(main())
