"""locate.py -- turn an address seen while playing into a function.

A playtest deviation has to end at a function (ticket 24).  What a crash
hands over is an address in the REBUILT image, and what the knowledge base,
Ghidra and src/ are organised by is the function name and its address in the
ORIGINAL.

DOS/4GW 1.97's crash dump carries the address twice, and only one of them is
usable here.  `CS:IP 160:001A002A` is the linear address after DOS/4GW moved
the image above 1 MB -- a probe linked at object 1 = 0x10000 ran with main at
0x1a0040 -- and the objects are not moved by one common delta, so no fixed
base turns it back.  The dump's last line, `Crash address (unrelocated) =
1:0000002A`, is object:offset in the linker's own coordinates, which is
exactly how the map spells addresses.  That line is the input.

Two facts make the bridge from there exact rather than approximate:

  * wlink's map (workspace/game_build/out/FDE.MAP) lists every public symbol
    of the rebuilt image with its object:offset and the object file it came
    from, so the containing function and its source unit are a lookup.
  * rebuild_info/naming.md: the C name is the Ghidra name, character for
    character, so the same name in ghidra_snapshot/functions.txt is the
    original's address of the same function.

The offset inside the function is reported too, but only the name crosses
between the two images: the bodies were compiled with different flags
(rebuild_info/build_flags.md), so offset N in one is not offset N in the other.

Accepted forms:
    1:0000002A          object:offset -- the dump's "Crash address
                        (unrelocated)", or the map's own 0001:0002b116
    fdps_title_screen   a symbol name, looked up in both images
    --original 2a4b1    an address in the ORIGINAL (Ghidra's), to find the
                        function and where the rebuild put it

A CS:EIP pair (selector above 0x10) is refused with a pointer to the right
line rather than guessed at.

Usage: python tools/game_build/locate.py 1:0000002A [...]
       python tools/game_build/locate.py --original 2a4b1
       python tools/game_build/locate.py selftest
"""
import argparse
import bisect
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "data_emit"))
import check_data as cd  # noqa: E402

OUT = ROOT / "workspace" / "game_build" / "out"
MAP = OUT / "FDE.MAP"


class Rebuilt:
    """The rebuilt image's symbols, by object and by name."""

    def __init__(self, map_path):
        self.by_name = cd.load_map(map_path)
        self.sorted = {}
        for name, (seg, off, module) in self.by_name.items():
            self.sorted.setdefault(seg, []).append((off, name, module))
        for rows in self.sorted.values():
            rows.sort()

    def containing(self, seg, off):
        rows = self.sorted.get(seg) or []
        i = bisect.bisect_right([r[0] for r in rows], off) - 1
        if i < 0:
            return None
        start, name, module = rows[i]
        return name, module, off - start


def load_original_functions():
    """[(start, body size, name)] from ghidra_snapshot/functions.txt, sorted."""
    rows = []
    path = cd.SNAPSHOT / "functions.txt"
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip() or line.startswith(" "):
            continue
        parts = [p.strip() for p in line.split("|")]
        m = re.search(r"([A-Za-z_][A-Za-z0-9_@]*)\s*\(", parts[-1])
        if m:
            rows.append((int(parts[0], 16), int(parts[1], 16), m.group(1)))
    rows.sort()
    return rows


class Original:
    """The original's names (Ghidra snapshot).

    Name lookup covers functions and data.  Address lookup covers function
    bodies only: the snapshot also names switch-case labels and other
    addresses inside bodies, and the nearest preceding name would then be a
    `caseD_1` rather than the function the address is in.
    """

    def __init__(self):
        self.by_name = {}
        for addr, name in cd.load_original_names().items():
            self.by_name.setdefault(name, addr)
        self.functions = load_original_functions()
        self.starts = [f[0] for f in self.functions]
        for start, _size, name in self.functions:
            self.by_name[name] = start

    def containing(self, addr):
        """(function name, offset) when addr is inside a function body."""
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0:
            return None
        start, size, name = self.functions[i]
        if addr >= start + size:
            return None
        return name, addr - start


def parse(text):
    """-> ("objoff", seg, off) | ("linear", addr) | ("name", text).

    "linear" is recognised only so it can be refused with a reason.
    """
    t = text.strip()
    m = re.fullmatch(r"([0-9a-fA-F]{1,4}):([0-9a-fA-F]{1,8})", t)
    if m:
        seg, off = int(m.group(1), 16), int(m.group(2), 16)
        # A DOS/4GW flat selector (0x160, 0x170 ...) is not an LE object
        # number; LE objects are numbered from 1 and FDE.EXE has three.
        if seg > 0x10:
            return ("linear", off)
        return ("objoff", seg, off)
    # Bare hex needs a digit in it, so a symbol spelled only in a-f is a name.
    m = re.fullmatch(r"0[xX]([0-9a-fA-F]+)", t) or (
        re.fullmatch(r"([0-9a-fA-F]+)", t) if re.search(r"[0-9]", t) else None)
    if m:
        return ("linear", int(m.group(1), 16))
    return ("name", t)


LINEAR_REFUSED = ("%s is not an object:offset address. If it came from a "
                  "DOS/4GW dump it is a relocated linear address and cannot be "
                  "mapped back -- use the dump's last line, \"Crash address "
                  "(unrelocated) = 1:XXXXXXXX\". If it is an address in the "
                  "original (Ghidra), pass --original.")


def describe(query, reb, orig):
    kind = parse(query)
    if kind[0] == "name":
        name = kind[1]
        rows = []
        if name in reb.by_name:
            seg, off, module = reb.by_name[name]
            rows.append("rebuilt  %04x:%08x  %s  (unit %s)" % (seg, off, name, module))
        else:
            rows.append("rebuilt  -- %s is not a public symbol of FDE.EXE" % name)
        if name in orig.by_name:
            rows.append("original %08x  %s" % (orig.by_name[name], name))
        else:
            rows.append("original -- %s is not named in ghidra_snapshot/" % name)
        return "\n".join(rows)
    if kind[0] == "linear":
        return LINEAR_REFUSED % query
    _, seg, off = kind
    found = reb.containing(seg, off)
    if found is None:
        return "%s: %04x:%08x precedes every symbol in its object" % (query, seg, off)
    name, module, delta = found
    rows = ["rebuilt  %04x:%08x = %s+0x%x  (unit %s)" % (seg, off, name, delta, module)]
    if name in orig.by_name:
        rows.append("original %08x  %s  (same name; offsets inside the body do "
                    "not carry over)" % (orig.by_name[name], name))
    else:
        rows.append("original -- %s has no Ghidra name (library code or a "
                    "rebuild-only symbol)" % name)
    return "\n".join(rows)


def describe_original(addr, reb, orig):
    found = orig.containing(addr)
    if found is None:
        return "original %08x is inside no function body (data or padding)" % addr
    name, delta = found
    rows = ["original %08x = %s+0x%x" % (addr, name, delta)]
    if name in reb.by_name:
        seg, off, module = reb.by_name[name]
        rows.append("rebuilt  %04x:%08x  %s  (unit %s)" % (seg, off, name, module))
    else:
        rows.append("rebuilt  -- %s is not a public symbol of FDE.EXE" % name)
    return "\n".join(rows)


# ----------------------------------------------------------------- selftest

def selftest():
    rows = []
    rows.append(("DOS/4GW CS:EIP is linear", parse("160:001A002A") == ("linear", 0x1a002a),
                 str(parse("160:001A002A"))))
    rows.append(("the unrelocated crash address is object:offset",
                 parse("1:0000002A") == ("objoff", 1, 0x2a), str(parse("1:0000002A"))))
    rows.append(("a linear address is refused, not guessed",
                 describe("160:001A002A", None, None).startswith("160:001A002A is not"),
                 "refused"))
    rows.append(("map form is object:offset", parse("0001:0002b116") == ("objoff", 1, 0x2b116),
                 str(parse("0001:0002b116"))))
    rows.append(("bare hex is linear", parse("3cd17") == ("linear", 0x3cd17),
                 str(parse("3cd17"))))
    rows.append(("0x hex is linear", parse("0x2a4b1") == ("linear", 0x2a4b1),
                 str(parse("0x2a4b1"))))
    rows.append(("a name is a name", parse("fdps_title_screen") == ("name", "fdps_title_screen"),
                 str(parse("fdps_title_screen"))))
    rows.append(("a hex-looking name is a name", parse("faded") == ("name", "faded"),
                 str(parse("faded"))))
    if MAP.is_file():
        reb, orig = Rebuilt(MAP), Original()
        seg, off, module = reb.by_name["fdps_title_screen"]
        got = reb.containing(seg, off + 5)
        rows.append(("inside a function resolves to it",
                     got is not None and got[0] == "fdps_title_screen" and got[2] == 5,
                     str(got)))
        o = orig.by_name.get("fdps_title_screen")
        got = orig.containing(o + 3) if o else None
        rows.append(("original address resolves by name",
                     got == ("fdps_title_screen", 3), str(got)))
        # 0002f6d0 fdps_transition_slide carries switch-case labels in its
        # body; an address past one must still resolve to the function.
        o = orig.by_name.get("fdps_transition_slide")
        got = orig.containing(o + 0x100) if o else None
        rows.append(("a case label inside the body is not the answer",
                     got == ("fdps_transition_slide", 0x100), str(got)))
    else:
        rows.append(("image checks", True, "skipped: build the game first"))
    ok = True
    for name, passed, detail in rows:
        print("[selftest] %-44s %s (%s)" % (name, "ok" if passed else "FAIL", detail))
        ok = ok and passed
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("query", nargs="+",
                    help="rebuilt address or symbol name; `selftest` runs the checks")
    ap.add_argument("--original", action="store_true",
                    help="the addresses are the ORIGINAL's (Ghidra) addresses")
    args = ap.parse_args()
    if args.query == ["selftest"]:
        ok = selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    if not MAP.is_file():
        raise SystemExit("locate: no %s -- python tools/game_build/build_game.py"
                         % MAP)
    reb, orig = Rebuilt(MAP), Original()
    for q in args.query:
        if args.original:
            try:
                addr = int(q, 16)
            except ValueError:
                print("%s: --original takes a hex address" % q)
                continue
            print(describe_original(addr, reb, orig))
        else:
            print(describe(q, reb, orig))
    return 0


if __name__ == "__main__":
    sys.exit(main())
