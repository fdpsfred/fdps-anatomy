"""Compile real src/ units under several flag sets and compare every function
body, byte for byte, with the original bytes of the shipped FDPS.LE.

This is the whole-function counterpart of verify_flags.py: the probes there
check one code shape at a time, this checks entire functions the rebuild
actually contains.  A flag set that is closer to the original produces more
bodies that are identical once the fields the linker fills in are masked.

Original side
    Read straight from fdps_game_files/FDPS.LE, not from a Ghidra dump.  The
    function start and body size come from ghidra_snapshot/functions.txt, the
    name and owning src/ unit from tools/code_emit/data/routing.json, the
    relocation sites from the LE Fixup Record Table (tools/build_gate/lefixup.py).

Rebuilt side
    The unit is compiled by the DOS hosted wcc386 inside DOSBox-X
    (push_form.dos_run) and disassembled by wdisasm; the listing is parsed by
    segment offset, so a function is the byte range from its label to the next
    public label.

Comparison
    Trailing alignment filler (nop, mov r,r, lea r,[r+0]) is dropped from both
    sides.  Masked on both sides: the bytes of every LE relocation site inside
    the original body (absolute addresses; the OBJ holds 0 or a segment offset
    there) and the rel32 of every E8 call in the original (targets depend on
    layout).  Nothing else is masked.

    exact   same length, every unmasked byte equal
    length  same length, some unmasked bytes differ
    differs different length
    missing the listing has no function of that name (e.g. a static callee
            that -oe expanded everywhere and never emitted)

Usage
    python fn_match.py                      # the claims preset (see PRESETS)
    python fn_match.py --preset order       # one preset
    python fn_match.py --sets A,B --units palette.c,gauge.c [--variants gauv]
    python fn_match.py --set X="-bt=dos4g -mf -zq -4s -fpi -s -ot -d2" --units saf.c

Outputs land in workspace/build_flags/fn_match/<run>/: table.tsv (one row per
unit x function x set), tallies.txt, and the staged sources, OBJs and listings.
"""

import argparse
import json
import os
import re
import shutil
import sys

import capstone

import push_form
import fn_variants

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "build_gate"))
import lefixup  # noqa: E402

SRC = os.path.join(REPO, "src")
LE_PATH = os.path.join(REPO, "fdps_game_files", "FDPS.LE")
FUNCTIONS_TXT = os.path.join(REPO, "ghidra_snapshot", "functions.txt")
ROUTING = os.path.join(REPO, "tools", "code_emit", "data", "routing.json")
OUT = os.path.join(REPO, "workspace", "build_flags", "fn_match")

BASE = "-bt=dos4g -mf -zq -4s -fpi"
SETS = {
    "A": BASE + " -s -ot -od",              # the set build_flags.md documents
    "B": BASE + " -s -ot -oe -d2",
    "Q": BASE + " -s -ot -oe=25 -d2",
    "R1": BASE + " -s -d2 -oe=25 -ot",      # flag order variants of Q
    "R2": BASE + " -s -oe=25 -d2 -ot",
    "O": BASE + " -os",                     # CD units
    "P": BASE + " -os -d2",
}

# unit tag (<= 5 chars, so tag_set fits 8.3) -> src/ file
UNITS = {"pal": "palette.c", "saf": "saf.c", "gau": "gauge.c",
         "chv": "chevt6.c", "spr": "sprite.c", "mpt": "maptile.c",
         "pcy": "palcycle.c", "cd": "cd.c"}
GAME_UNITS = ["palette.c", "saf.c", "gauge.c", "chevt6.c", "sprite.c",
              "maptile.c", "palcycle.c"]

PRESETS = {
    "game": [(GAME_UNITS, ["A", "B", "Q"], [])],
    "cd": [(["cd.c"], ["O", "P", "A", "B"], [])],
    "variants": [([], ["A", "B", "Q"], fn_variants.NAMES)],
    "order": [(GAME_UNITS, ["Q", "R1", "R2"], [])],
}
PRESETS["claims"] = (PRESETS["game"] + PRESETS["cd"] + PRESETS["variants"]
                     + [(GAME_UNITS, ["R1", "R2"], [])])

FILLER = re.compile(r"^(nop|mov (\w+), \2|lea (\w+), \[\3(?: \+ 0)?\])$")
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


# ---------------------------------------------------------------- original side

def load_functions():
    """addr -> body size, from the Ghidra text snapshot."""
    sizes = {}
    with open(FUNCTIONS_TXT, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#") or "|" not in line or line[0] == " ":
                continue
            cols = [c.strip() for c in line.split("|")]
            sizes[int(cols[0], 16)] = int(cols[1], 16)
    return sizes


def load_routing():
    """unit file -> [(addr, name)] in address order."""
    with open(ROUTING, encoding="utf-8") as fh:
        funcs = json.load(fh)["functions"]
    by_unit = {}
    for addr, rec in funcs.items():
        by_unit.setdefault(rec["target"], []).append((int(addr, 16), rec["name"]))
    for lst in by_unit.values():
        lst.sort()
    return by_unit


class Original:
    """The shipped image, addressable by linear address in object 1."""

    def __init__(self, path=LE_PATH):
        with open(path, "rb") as fh:
            self.data = fh.read()
        hdr = lefixup.header(self.data)
        base = hdr["base"]
        obj_tbl = base + int.from_bytes(self.data[base + 0x40:base + 0x44], "little")
        vsize, self.obj_base, _flags, page_idx, _cnt, _r = [
            int.from_bytes(self.data[obj_tbl + 4 * i:obj_tbl + 4 * i + 4], "little")
            for i in range(6)]
        self.obj_end = self.obj_base + vsize
        self.obj_file = hdr["data_pages_off"] + (page_idx - 1) * hdr["page_size"]
        self.sites = sorted(lefixup.fixup_sites(self.data))
        self.sizes = load_functions()

    def file_off(self, addr):
        if not self.obj_base <= addr < self.obj_end:
            raise ValueError("0x%x is outside object 1" % addr)
        return self.obj_file + (addr - self.obj_base)

    def body(self, addr):
        """(bytes, set of masked offsets) for the function at addr."""
        size = self.sizes[addr]
        start = self.file_off(addr)
        code = self.data[start:start + size]
        mask = set()
        for off, width in self.sites:
            if off + width <= start or off >= start + size:
                continue
            mask.update(i - start for i in range(off, off + width)
                        if 0 <= i - start < size)
        for ins in MD.disasm(code, addr):
            if ins.bytes[0] == 0xE8 and ins.size == 5:
                rel = ins.address - addr
                mask.update(range(rel + 1, rel + 5))
        return strip_filler(code, addr), mask


def strip_filler(code, addr=0):
    """Drop trailing alignment filler instructions."""
    insns = list(MD.disasm(code, addr))
    covered = sum(i.size for i in insns)
    if covered != len(code):
        return code         # undecodable tail (data): leave it as it is
    end = len(code)
    for ins in reversed(insns):
        if FILLER.match(("%s %s" % (ins.mnemonic, ins.op_str)).strip()):
            end = ins.address - addr
        else:
            break
    return code[:end]


# ---------------------------------------------------------------- rebuilt side

LINE = re.compile(r"^ ([0-9a-f]{4,8})  ((?:[0-9a-f]{2} )+)")
CONT = re.compile(r"^ {7}((?:[0-9a-f]{2} )+)")
LABEL = re.compile(r"^ ([0-9a-f]{4,8}) {10,}(\w+):\s*$")
CALL = re.compile(r"\bcall\s+(?:near ptr\s+)?(\w+)\s*$")


def parse_listing(path):
    """-> {name: {"code": bytes, "calls": [callee...], "text": [insn...]}}.

    Only the _TEXT segment is read.  Bytes are placed by their segment offset,
    continuation lines append to the instruction above them, and a function
    runs from its label to the next non-local label.
    """
    seg = bytearray()
    labels = []                 # (offset, name)
    insn_at = []                # (offset, text)
    in_text = False
    cur = 0
    with open(path, errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\r\n")
            if line.startswith("Segment:"):
                in_text = line.split()[1] == "_TEXT"
                continue
            if not in_text:
                continue
            m = LABEL.match(line)
            if m:
                labels.append((int(m.group(1), 16), m.group(2)))
                continue
            m = LINE.match(line)
            if m:
                cur = int(m.group(1), 16)
                hx = m.group(2).split()
                # a short name sits in the label column of its first line
                inline = line[24:40].strip() if len(line) > 24 else ""
                if inline and not re.match(r"^L\d+$", inline):
                    labels.append((cur, inline))
                text = line[40:].strip() if len(line) > 40 else ""
                insn_at.append((cur, text))
            else:
                m = CONT.match(line)
                if not m:
                    continue
                hx = m.group(1).split()
                if len(line) > 40 and line[40:].strip() and insn_at:
                    insn_at[-1] = (insn_at[-1][0], line[40:].strip())
            if len(seg) < cur:
                seg.extend(b"\0" * (cur - len(seg)))
            pos = cur
            for h in hx:
                if pos < len(seg):
                    seg[pos] = int(h, 16)
                else:
                    seg.append(int(h, 16))
                pos += 1
            cur = pos
    funcs = {}
    labels.sort()
    for i, (off, name) in enumerate(labels):
        end = labels[i + 1][0] if i + 1 < len(labels) else len(seg)
        text = [t for o, t in insn_at if off <= o < end]
        calls = [m.group(1) for m in (CALL.search(t) for t in text) if m]
        funcs[name] = {"code": strip_filler(bytes(seg[off:end])),
                       "calls": calls, "text": text}
    return funcs


def verdict(orig, mask, new):
    if new is None:
        return "missing", 0
    if len(new) != len(orig):
        return "differs", 0
    bad = sum(1 for i, (a, b) in enumerate(zip(orig, new))
              if i not in mask and a != b)
    return ("exact", 0) if bad == 0 else ("length", bad)


# ---------------------------------------------------------------- driver

def stage(work, sources):
    """Copy the headers and write each staged unit as <tag>.c."""
    os.makedirs(work, exist_ok=True)
    for name in os.listdir(SRC):
        if name.endswith(".h"):
            shutil.copyfile(os.path.join(SRC, name), os.path.join(work, name))
    for tag, text in sources.items():
        with open(os.path.join(work, tag + ".c"), "w", encoding="latin-1",
                  newline="\n") as fh:
            fh.write(text)


def compile_jobs(work, jobs, timeout=1800):
    """jobs: [(obj_stem, flags, tag)].  Returns {obj_stem: listing path}.

    Raises when a listing is missing or older than this run."""
    lines, expect = [], []
    for stem, flags, tag in jobs:
        if len(stem) > 8 or len(tag) > 8:
            raise ValueError("8.3 name too long: %s / %s" % (stem, tag))
        lines.append("echo === %s %s >>build.out" % (stem, flags))
        lines.append("wcc386 %s -fo=%s.obj %s.c >>build.out" % (flags, stem, tag))
        lines.append("wdisasm -l=%s.lst -e -p %s.obj >>build.out" % (stem, stem))
        expect.append(stem + ".lst")
    started = push_form.dos_run(work, push_form.WATCOM, lines, expect, timeout)
    missing = [e for e in expect if not os.path.exists(os.path.join(work, e))
               or os.path.getmtime(os.path.join(work, e))
               < started - push_form.MTIME_SLACK]
    if missing:
        raise SystemExit("no fresh listing for %s; see %s"
                         % (missing, os.path.join(work, "build.out")))
    return {stem: os.path.join(work, stem + ".lst") for stem, _f, _t in jobs}


def read_src(name):
    with open(os.path.join(SRC, name), encoding="latin-1") as fh:
        return fh.read()


def unit_tag(name):
    for tag, unit in UNITS.items():
        if unit == name:
            return tag
    raise SystemExit("no short tag for unit %s; add it to UNITS" % name)


def run(run_name, plan, sets=SETS):
    """plan: [(units, set labels, variant names)].  Writes table + tallies."""
    work = os.path.join(OUT, run_name)
    sources, meta, jobs = {}, {}, []
    for units, labels, variants in plan:
        for unit in units:
            tag = unit_tag(unit)
            sources[tag] = read_src(unit)
            meta[tag] = unit
            for lab in labels:
                jobs.append(("%s_%s" % (tag, lab), sets[lab], tag, lab))
        for var in variants:
            text, unit = fn_variants.build(var, read_src)
            sources[var] = text
            meta[var] = unit
            for lab in labels:
                jobs.append(("%s_%s" % (var, lab), sets[lab], var, lab))
    seen, uniq = set(), []
    for job in jobs:
        if job[0] not in seen:
            seen.add(job[0])
            uniq.append(job)
    stage(work, sources)
    listings = compile_jobs(work, [(s, f, t) for s, f, t, _l in uniq])

    orig = Original()
    routing = load_routing()
    rows, tallies = [], {}
    for stem, flags, tag, lab in uniq:
        funcs = parse_listing(listings[stem])
        chk = sum(1 for f in funcs.values() for c in f["calls"] if c == "__CHK")
        for addr, name in routing.get(meta[tag], []):
            ob, mask = orig.body(addr)
            got = funcs.get(name)
            v, bad = verdict(ob, mask, got["code"] if got else None)
            rows.append((tag, lab, "%08x" % addr, name, len(ob),
                         len(got["code"]) if got else "-", v, bad,
                         ",".join(sorted(set(got["calls"]))) if got else ""))
            t = tallies.setdefault((tag, lab), {"exact": 0, "length": 0,
                                                "differs": 0, "missing": 0,
                                                "__CHK": chk})
            t[v] += 1

    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, "table.tsv"), "w", encoding="utf-8",
              newline="\n") as fh:
        fh.write("unit\tset\taddr\tname\torig_len\tnew_len\tverdict\t"
                 "diff_bytes\tcalls\n")
        for r in rows:
            fh.write("\t".join(str(x) for x in r) + "\n")
    lines = ["flag sets:"]
    for lab in sorted({j[3] for j in uniq}):
        lines.append("  %-3s %s" % (lab, sets[lab]))
    lines.append("")
    lines.append("%-6s %-3s %5s %6s %7s %7s %5s" % ("unit", "set", "exact",
                 "length", "differs", "missing", "__CHK"))
    agg = {}
    for (tag, lab), t in tallies.items():
        lines.append("%-6s %-3s %5d %6d %7d %7d %5d" % (
            tag, lab, t["exact"], t["length"], t["differs"], t["missing"],
            t["__CHK"]))
        if tag in fn_variants.NAMES:
            continue
        a = agg.setdefault((meta[tag] == "cd.c", lab), [0, 0, 0, 0, 0])
        for i, k in enumerate(("exact", "length", "differs", "missing")):
            a[i] += t[k]
        a[4] += t["__CHK"]
    lines.append("")
    lines.append("totals over the unmodified src/ units:")
    for (is_cd, lab), a in sorted(agg.items()):
        lines.append("  %-4s %-3s %d functions: exact %d, length %d, differs %d, "
                     "missing %d, __CHK calls %d"
                     % ("cd" if is_cd else "game", lab, sum(a[:4]), a[0], a[1],
                        a[2], a[3], a[4]))
    text = "\n".join(lines) + "\n"
    with open(os.path.join(work, "tallies.txt"), "w", encoding="utf-8",
              newline="\n") as fh:
        fh.write(text)
    print(text)
    print("per-function table: %s" % os.path.join(work, "table.tsv"))
    return rows, tallies


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preset", choices=sorted(PRESETS))
    ap.add_argument("--sets", help="comma list of set labels")
    ap.add_argument("--set", action="append", default=[],
                    help='LABEL="flags" (label <= 2 chars); may repeat')
    ap.add_argument("--units", help="comma list of src/ files")
    ap.add_argument("--variants", help="comma list of %s" % fn_variants.NAMES)
    ap.add_argument("--run", help="output subdirectory name")
    args = ap.parse_args()
    sets = dict(SETS)
    custom = []
    for item in args.set:
        lab, _, flags = item.partition("=")
        if not lab or len(lab) > 2 or not flags:
            raise SystemExit("bad --set %r" % item)
        sets[lab] = flags
        custom.append(lab)
    if args.units or args.variants or custom or args.sets:
        labels = (args.sets.split(",") if args.sets else []) + custom
        plan = [((args.units or "").split(",") if args.units else [],
                 labels or ["Q"],
                 args.variants.split(",") if args.variants else [])]
        name = args.run or "custom"
    else:
        preset = args.preset or "claims"
        plan = PRESETS[preset]
        name = args.run or preset
    run(name, plan, sets)


if __name__ == "__main__":
    main()
