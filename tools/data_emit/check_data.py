"""check_data.py -- does each landed global hold what the original's does?

Ticket 23 writes the game's global data into src/ as C definitions.  The
compiler proves those definitions agree with their headers; nothing in the
build proves they agree with FDPS.LE.  This does, from the linked output rather
than from the C text: for every symbol in tools/data_emit/data/manifest.json it
finds the symbol's address in the unit-test image's wlink map, reads its bytes
out of EMITTEST.EXE, and compares them with the bytes at the symbol's Ghidra
address in the shipped FDPS.LE.

Two kinds of byte cannot be compared as bytes:

  * A relocated pointer.  Its value is an address, and the rebuild puts
    everything somewhere else.  What must agree is WHICH symbol it points at, so
    both sides' fixup targets are resolved to a name -- the original's through
    the Ghidra snapshot, the rebuild's through the map -- and the names are
    compared.  A pointer in one image with no fixup in the other is a mismatch.
  * Nothing else.  A zero-filled (bss) symbol is compared like any other: the
    original's zero-fill area reads as zeros, so a global the image actually
    initialises cannot slip through as bss.

It also checks the layout constraints the manifest records -- `follows` (this
symbol starts exactly where the named one ends) and `zero_guard_before` (the
four bytes below this symbol are zero and belong to no other public symbol) --
because those are the reads the original gets out of adjacency, and a separate
definition per symbol reproduces them only if the linker put the symbols where
the constraint says.

What it cannot see: a type or signedness choice that stores the same bytes
(the compiler and the headers answer that), and a layout dependency nobody
wrote into the manifest.

Usage: python tools/data_emit/check_data.py [--json] [--only SYM,...]
       python tools/data_emit/check_data.py --selftest
Exit : 0 when every checked symbol and constraint passes.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tools" / "data_emit" / "data" / "manifest.json"
ORIGINAL = ROOT / "fdps_game_files" / "FDPS.LE"
OUT = ROOT / "workspace" / "code_emit" / "out"
SNAPSHOT = ROOT / "ghidra_snapshot"
RESULT = ROOT / "workspace" / "data_emit" / "check.json"

# LE fixup source type -> bytes patched at the site (LE/LX specification).
SRC_SIZE = {0x00: 1, 0x02: 2, 0x03: 4, 0x05: 2, 0x06: 6, 0x07: 4, 0x08: 4}
NO_TARGET_OFFSET = {0x02}


class LeError(Exception):
    pass


# ------------------------------------------------------------------ LE image

class LeImage:
    """Objects, their bytes as loaded, and every internal fixup.

    Addresses are (object number, offset in object).  An object's bytes past
    its file-backed pages up to its virtual size are zero, which is what the
    loader gives a zero-filled tail.
    """

    def __init__(self, data):
        self.data = data
        if data[:2] == b"LE":
            base = 0
        elif data[:2] == b"MZ":
            base = struct.unpack_from("<I", data, 0x3C)[0]
            if data[base:base + 2] != b"LE":
                raise LeError("MZ stub does not point at an LE header")
        else:
            raise LeError("not an LE executable")
        u32 = lambda off: struct.unpack_from("<I", data, base + off)[0]  # noqa: E731
        self.page_size = u32(0x28)
        self.last_page_size = u32(0x2C)
        self.num_pages = u32(0x14)
        self.data_pages_off = u32(0x80)
        obj_tbl, nobj = base + u32(0x40), u32(0x44)
        page_map = base + u32(0x48)
        self.objects = {}
        for i in range(nobj):
            vsize, reloc, flags, pidx, pcnt, _ = struct.unpack_from(
                "<IIIIII", data, obj_tbl + 24 * i)
            self.objects[i + 1] = {"vsize": vsize, "base": reloc,
                                   "first_page": pidx, "pages": pcnt}
        # Watcom writes the page map as the identity; anything else would need
        # real handling here, so it is refused rather than half-supported.
        for page in range(1, self.num_pages + 1):
            e = data[page_map + 4 * (page - 1): page_map + 4 * page]
            number = (e[0] << 16) | (e[1] << 8) | e[2]
            if number != page:
                raise LeError("object page map entry %d names page %d" % (page, number))
        self.fixup_page_tbl = base + u32(0x68)
        self.fixup_rec_tbl = base + u32(0x6C)
        self.import_mod_tbl = base + u32(0x70)
        self._fixups = None

    def page_owner(self, page):
        for num, o in self.objects.items():
            if o["first_page"] <= page < o["first_page"] + o["pages"]:
                return num, (page - o["first_page"]) * self.page_size
        raise LeError("page %d belongs to no object" % page)

    def read(self, obj, off, n):
        o = self.objects[obj]
        out = bytearray(n)
        for k in range(n):
            pos = off + k
            if pos >= o["vsize"]:
                raise LeError("read past object %d end (0x%x)" % (obj, pos))
            page = o["first_page"] + pos // self.page_size
            if pos // self.page_size >= o["pages"]:
                continue                                  # zero-filled tail
            in_page = pos % self.page_size
            if page == self.num_pages and in_page >= self.last_page_size:
                continue
            fo = self.data_pages_off + (page - 1) * self.page_size + in_page
            if fo < len(self.data):
                out[k] = self.data[fo]
        return bytes(out)

    def fixups(self):
        """{(obj, offset): (width, target_obj or None, target_off)} for every site."""
        if self._fixups is not None:
            return self._fixups
        d = self.data
        table = [struct.unpack_from("<I", d, self.fixup_page_tbl + 4 * i)[0]
                 for i in range(self.num_pages + 1)]
        sites = {}
        for page in range(1, self.num_pages + 1):
            pos = self.fixup_rec_tbl + table[page - 1]
            end = self.fixup_rec_tbl + table[page]
            obj, page_base = self.page_owner(page)
            while pos < end:
                src, trg = d[pos], d[pos + 1]
                pos += 2
                stype = src & 0x0F
                if stype not in SRC_SIZE:
                    raise LeError("unknown fixup source type 0x%02x" % stype)
                if src & 0x20:
                    count = d[pos]
                    pos += 1
                    offsets = None
                else:
                    offsets = [struct.unpack_from("<h", d, pos)[0]]
                    pos += 2
                tobj = None
                toff = 0
                ttype = trg & 0x03
                if ttype == 0:
                    if trg & 0x40:
                        tobj = struct.unpack_from("<H", d, pos)[0]
                        pos += 2
                    else:
                        tobj = d[pos]
                        pos += 1
                    if stype not in NO_TARGET_OFFSET:
                        if trg & 0x10:
                            toff = struct.unpack_from("<I", d, pos)[0]
                            pos += 4
                        else:
                            toff = struct.unpack_from("<H", d, pos)[0]
                            pos += 2
                elif ttype in (1, 2):
                    pos += 2 if (trg & 0x40) else 1
                    if ttype == 1:
                        pos += 1 if (trg & 0x80) else (4 if (trg & 0x10) else 2)
                    else:
                        pos += 4 if (trg & 0x10) else 2
                else:
                    pos += 2 if (trg & 0x40) else 1
                if trg & 0x04:
                    pos += 4 if (trg & 0x20) else 2
                if src & 0x20:
                    offsets = [struct.unpack_from("<h", d, pos + 2 * i)[0]
                               for i in range(count)]
                    pos += 2 * count
                if pos > end:
                    raise LeError("fixup record overruns page %d" % page)
                for so in offsets:
                    sites[(obj, page_base + so)] = (SRC_SIZE[stype], tobj, toff)
            if pos != end:
                raise LeError("fixup records for page %d misaligned" % page)
        self._fixups = sites
        return sites

    def sites_in(self, obj, off, n):
        """Fixup sites whose first byte lies in [off, off+n), as relative offsets."""
        fx = self.fixups()
        return {so - off: fx[(ob, so)] for (ob, so) in fx
                if ob == obj and off <= so < off + n}


# --------------------------------------------------------------- name tables

def load_map(path):
    """{symbol: (seg, offset, module_basename)} from a wlink map."""
    out = {}
    module = None
    for line in path.read_text(encoding="latin-1").splitlines():
        if line.startswith("Module:"):
            m = re.search(r"([^\\/:(]+?)\.OBJ", line, re.I)
            module = m.group(1).upper() if m else line.split()[-1]
            continue
        m = re.match(r"^([0-9a-fA-F]{4}):([0-9a-fA-F]{8})[+*]?\s+(\S+)\s*$", line)
        if m and module is not None:
            out[m.group(3)] = (int(m.group(1), 16), int(m.group(2), 16), module)
    return out


def load_original_names():
    """{linear address: name} for every named function and data symbol."""
    names = {}
    for line in (SNAPSHOT / "functions.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip() or line.startswith(" "):
            continue
        parts = [p.strip() for p in line.split("|")]
        m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", parts[-1])
        if m:
            names[int(parts[0], 16)] = m.group(1)
    for fname in ("data.txt", "labels.txt"):
        for line in (SNAPSHOT / fname).read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or "|" not in line:
                continue
            parts = [p.strip() for p in line.split("|")]
            if not re.fullmatch(r"[0-9a-fA-F]{8}", parts[0]):
                continue
            name = parts[3] if fname == "data.txt" else parts[-1]
            if name and name != "-":
                names.setdefault(int(parts[0], 16), name)
    return names


# ------------------------------------------------------------------- checks

def compare_symbol(entry, orig, orig_names, reb, reb_map, reb_by_addr):
    """One manifest entry -> (ok, detail)."""
    sym = entry["symbol"]
    size = int(entry["size"])
    addr = int(entry["addr"], 16)
    if sym not in reb_map:
        return False, "not in the map (still stubbed, or never linked)"
    seg, roff, module = reb_map[sym]
    if module in ("STUBS",):
        return False, "defined by the stub module, not by src/"
    want_module = Path(entry["target"]).stem.upper()
    if module != want_module:
        return False, "defined in %s.OBJ, manifest says %s" % (module, entry["target"])
    oobj = next((n for n, o in orig.objects.items()
                 if o["base"] <= addr < o["base"] + o["vsize"]), None)
    if oobj is None:
        return False, "original address %s is in no object" % entry["addr"]
    ooff = addr - orig.objects[oobj]["base"]
    ob = orig.read(oobj, ooff, size)
    rb = reb.read(seg, roff, size)
    ofx = orig.sites_in(oobj, ooff, size)
    rfx = reb.sites_in(seg, roff, size)
    if set(ofx) != set(rfx):
        return False, ("pointer sites differ: original at %s, rebuild at %s"
                       % (sorted(ofx), sorted(rfx)))
    masked = set()
    for rel, (width, tobj, toff) in ofx.items():
        rw, rtobj, rtoff = rfx[rel]
        masked.update(range(rel, rel + width))
        otarget = orig_names.get(orig.objects[tobj]["base"] + toff) if tobj else None
        rtarget = reb_by_addr.get((rtobj, rtoff)) if rtobj else None
        if otarget is None or rtarget is None or otarget != rtarget:
            return False, ("pointer at +0x%x: original -> %s, rebuild -> %s"
                           % (rel, otarget or "(unnamed 0x%x)" % (
                               orig.objects[tobj]["base"] + toff if tobj else 0),
                              rtarget or "(unnamed)"))
    for i in range(size):
        if i in masked:
            continue
        if ob[i] != rb[i]:
            return False, ("byte +0x%x: original %02x, rebuild %02x"
                           % (i, ob[i], rb[i]))
    return True, "%d byte(s), %d pointer(s)" % (size, len(ofx))


def check_layout(entry, manifest_by_sym, reb, reb_map):
    """The manifest's adjacency constraints for one entry -> [(ok, detail)]."""
    out = []
    sym = entry["symbol"]
    if sym not in reb_map:
        return out
    seg, off, _ = reb_map[sym]
    layout = entry.get("layout") or {}
    prev = layout.get("follows")
    if prev:
        if prev not in reb_map or prev not in manifest_by_sym:
            out.append((False, "follows %s, which is not landed" % prev))
        else:
            pseg, poff, _ = reb_map[prev]
            # A predecessor that owns a zero tail ends where its tail ends.
            ptail = int((manifest_by_sym[prev].get("layout") or {}).get("zero_pad_after") or 0)
            want = poff + int(manifest_by_sym[prev]["size"]) + ptail
            ok = pseg == seg and off == want
            out.append((ok, "follows %s: at %04x:%08x, expected %04x:%08x"
                        % (prev, seg, off, pseg, want)))
    pad = layout.get("zero_pad_after")
    if pad:
        end = off + int(entry["size"])
        after = reb.read(seg, end, int(pad))
        owners = [s for s, (sg, so, _) in reb_map.items()
                  if sg == seg and end <= so < end + int(pad)]
        ok = after == b"\0" * int(pad) and not owners
        out.append((ok, "zero pad after (%d): bytes %s, public symbols there: %s"
                    % (int(pad), after.hex(), owners or "none")))
    if layout.get("zero_guard_before"):
        below = reb.read(seg, off - 4, 4)
        owners = [s for s, (sg, so, _) in reb_map.items()
                  if sg == seg and off - 4 <= so < off and s != sym]
        ok = below == b"\0\0\0\0" and not owners
        out.append((ok, "zero guard below: bytes %s, public symbols there: %s"
                    % (below.hex(), owners or "none")))
    return out


def run(only=None):
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8")) if MANIFEST.is_file() else {"symbols": []}
    entries = [e for e in manifest.get("symbols", [])
               if not only or e["symbol"] in only]
    if not entries:
        return {"ok": True, "checked": 0, "failed": [], "layout_failed": [],
                "symbols": [], "layout": []}
    exe = next((p for p in OUT.iterdir() if p.name.upper() == "EMITTEST.EXE"), None) if OUT.is_dir() else None
    mp = next((p for p in OUT.iterdir() if p.name.upper() == "EMITTEST.MAP"), None) if OUT.is_dir() else None
    if exe is None or mp is None:
        return {"ok": False, "error": "no EMITTEST.EXE / EMITTEST.MAP -- build first",
                "symbols": [], "layout": []}
    orig = LeImage(ORIGINAL.read_bytes())
    reb = LeImage(exe.read_bytes())
    reb_map = load_map(mp)
    reb_by_addr = {(sg, so): s for s, (sg, so, _) in reb_map.items()}
    names = load_original_names()
    by_sym = {e["symbol"]: e for e in manifest.get("symbols", [])}
    rows, lay = [], []
    for e in entries:
        ok, detail = compare_symbol(e, orig, names, reb, reb_map, reb_by_addr)
        rows.append({"symbol": e["symbol"], "ok": ok, "detail": detail})
        for lok, ldetail in check_layout(e, by_sym, reb, reb_map):
            lay.append({"symbol": e["symbol"], "ok": lok, "detail": ldetail})
    result = {"ok": all(r["ok"] for r in rows) and all(r["ok"] for r in lay),
              "checked": len(rows),
              "failed": [r for r in rows if not r["ok"]],
              "layout_failed": [r for r in lay if not r["ok"]],
              "symbols": rows, "layout": lay}
    RESULT.parent.mkdir(parents=True, exist_ok=True)
    RESULT.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n",
                      encoding="utf-8")
    return result


# ----------------------------------------------------------------- selftest

def selftest():
    """Against the original itself: it has to be able to say both yes and no."""
    rows = []
    orig = LeImage(ORIGINAL.read_bytes())
    names = load_original_names()
    rows.append(("objects parsed", sorted(o["base"] for o in orig.objects.values())[:3]
                 == [0x10000, 0x60000, 0x70000], str(sorted(o["base"] for o in orig.objects.values()))))
    # 0x60014 holds 15 in the image (ticket 22 handoff 00015be0#0).
    b = orig.read(2, 0x14, 4)
    rows.append(("initialised dword read", b == b"\x0f\0\0\0", b.hex()))
    # Deep in the zero-filled tail of object 2.
    b = orig.read(2, 0xc000, 4)
    rows.append(("zero-filled tail reads zero", b == b"\0\0\0\0", b.hex()))
    # The chapter init table is 30 code pointers: every slot a relocation to a
    # named function.
    fx = orig.sites_in(2, 0x74, 120)
    targets = [names.get(orig.objects[t]["base"] + o) for (_, t, o) in fx.values()]
    rows.append(("handler table resolves to functions",
                 len(fx) == 30 and all(t and t.startswith("fdps_") for t in targets),
                 "%d sites, first %s" % (len(fx), targets[:1])))

    # The comparison itself, image against image: identical must pass, one
    # changed byte and one retargeted pointer must each fail.
    class Fake:
        pass
    reb_map = {"s_int": (2, 0x14, "PALCYCLE"), "s_tab": (2, 0x74, "CHAPTER")}
    by_addr = {(2, 0x74 + r): "x" for r in range(0, 120, 4)}
    for (ob, so), (w, t, o) in orig.fixups().items():
        if ob == 2 and 0x74 <= so < 0x74 + 120:
            by_addr[(t, o)] = names.get(orig.objects[t]["base"] + o)
    e_int = {"symbol": "s_int", "size": 4, "addr": "00060014", "target": "palcycle.c"}
    e_tab = {"symbol": "s_tab", "size": 120, "addr": "00060074", "target": "chapter.c"}
    ok, d = compare_symbol(e_int, orig, names, orig, reb_map, by_addr)
    rows.append(("same bytes pass", ok, d))
    ok, d = compare_symbol(e_tab, orig, names, orig, reb_map, by_addr)
    rows.append(("same pointer targets pass", ok, d))
    wrong = dict(e_int, addr="00060018")
    ok, d = compare_symbol(wrong, orig, names, orig, reb_map, by_addr)
    rows.append(("different bytes fail", not ok, d))
    by_addr_bad = dict(by_addr)
    first = min(k for k in orig.sites_in(2, 0x74, 120))
    _, t, o = orig.sites_in(2, 0x74, 120)[first]
    by_addr_bad[(t, o)] = "fdps_somebody_else"
    ok, d = compare_symbol(e_tab, orig, names, orig, reb_map, by_addr_bad)
    rows.append(("retargeted pointer fails", not ok, d))
    ok, d = compare_symbol(dict(e_int, target="audio.c"), orig, names, orig, reb_map, by_addr)
    rows.append(("wrong defining module fails", not ok, d))
    ok, d = compare_symbol(dict(e_int, symbol="s_missing"), orig, names, orig, reb_map, by_addr)
    rows.append(("unmapped symbol fails", not ok, d))
    stub_map = {"s_int": (2, 0x14, "STUBS")}
    ok, d = compare_symbol(e_int, orig, names, orig, stub_map, by_addr)
    rows.append(("stubbed symbol fails", not ok, d))

    # Layout: 0x60040 ap[6] then 0x60058 def[6] in the original.
    lay_map = {"ap": (2, 0x40, "GAMEDATA"), "def": (2, 0x58, "GAMEDATA"),
               "gap": (2, 0x5c, "GAMEDATA")}
    man = {"ap": {"symbol": "ap", "size": 24}, "def": {"symbol": "def", "size": 24},
           "gap": {"symbol": "gap", "size": 4}}
    res = check_layout({"symbol": "def", "layout": {"follows": "ap"}}, man, orig, lay_map)
    rows.append(("adjacent follows passes", res and all(r[0] for r in res), str(res)))
    res = check_layout({"symbol": "gap", "layout": {"follows": "ap"}}, man, orig, lay_map)
    rows.append(("non-adjacent follows fails", res and not res[0][0], str(res)))
    # 0x60074 (the init table) is preceded by 0x60070, the village flag.
    res = check_layout({"symbol": "t", "layout": {"zero_guard_before": True}},
                       man, orig, {"t": (2, 0x74, "X"), "flag": (2, 0x70, "X")})
    rows.append(("guard owned by a public symbol fails", res and not res[0][0], str(res)))
    # 0x60070 is the one-byte village flag; its next three bytes are zero pad
    # up to the init table at 0x60074.
    fmap = {"flag": (2, 0x70, "X"), "t": (2, 0x74, "X")}
    fman = {"flag": {"symbol": "flag", "size": 1}}
    res = check_layout({"symbol": "flag", "size": 1, "layout": {"zero_pad_after": 3}},
                       fman, orig, fmap)
    rows.append(("zero pad after passes", res and res[0][0], str(res)))
    res = check_layout({"symbol": "flag", "size": 1, "layout": {"zero_pad_after": 4}},
                       fman, orig, fmap)
    rows.append(("pad reaching a public symbol fails", res and not res[0][0], str(res)))
    # The init table at 0x60074 follows the one-byte flag at 0x60070 only once
    # the flag's three-byte tail is counted.
    tman = {"flag": {"symbol": "flag", "size": 1, "layout": {"zero_pad_after": 3}},
            "t": {"symbol": "t", "size": 120}}
    res = check_layout({"symbol": "t", "layout": {"follows": "flag"}}, tman, orig, fmap)
    rows.append(("follows counts the predecessor's tail", res and res[0][0], str(res)))
    ok_all = True
    for name, passed, detail in rows:
        print("[selftest] %-40s %s (%s)" % (name, "ok" if passed else "FAIL", detail))
        ok_all = ok_all and passed
    return ok_all


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        ok = selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    only = set(s for s in a.only.split(",") if s) or None
    res = run(only)
    if a.json:
        print(json.dumps(res, indent=2, ensure_ascii=False))
    else:
        if res.get("error"):
            print("[check] " + res["error"])
        for r in res["failed"]:
            print("  FAIL  %-58s %s" % (r["symbol"], r["detail"]))
        for r in res["layout_failed"]:
            print("  FAIL  %-58s %s" % (r["symbol"], r["detail"]))
        print("[check] %d symbol(s) checked, %d failed, %d layout constraint(s) failed"
              % (res.get("checked", 0), len(res.get("failed", [])),
                 len(res.get("layout_failed", []))))
    print("[result] %s" % ("PASS" if res["ok"] else "FAIL"))
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
