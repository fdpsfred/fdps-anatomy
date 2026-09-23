"""asm_match.py -- do the RLE blitters assembled from src/*.asm run the
original's instructions?

Ticket 22.3 puts the fifteen hand-written RLE routines (fdps_blit_dispatch and
the fourteen routines under it) back into the rebuild as assembly transcribed
from FDPS.LE.  The bar is not byte identity.  It is that each routine executes
the same instructions as the original, instruction for instruction:

  1. The same instruction sequence -- the same mnemonics, the same operands,
     in the same order, with every branch landing on the corresponding
     instruction.  The original's NOP padding counts: a conditional branch that
     is not taken falls through those NOPs, so dropping them changes how many
     instructions run.
  2. Every instruction the same length as the original's, so the layout inside
     the routine, every branch distance and every instruction's timing agree.
  3. Bytes may differ only where the same instruction has another encoding of
     the same length (WASM 10.0a picks the other direction bit for reg,reg
     MOV/ADD/SUB/XOR/AND/OR/CMP and the imm8 form of CMP AX,0) and inside
     relocated fields.

How it is decided.  Both sides are disassembled with capstone after every
relocated field has been zeroed, and compared instruction by instruction on
length, mnemonic and operand text.  A branch is compared by where it lands --
"instruction #k of this routine" inside the routine, a symbol name outside it
-- never by its displacement, so a branch that is one byte out is caught even
where the text would look plausible.  Relocations are compared as a set: every
relocated field of the original must be a relocated field of the rebuild at the
same offset, aimed at the same symbol plus the same offset, and the rebuild
must have none the original does not.  The original's targets are named
through the Ghidra snapshot, the rebuild's through the object file's own
external names -- they must be the same names (rebuild_info/naming.md).

Two properties of the SOURCE are checked as well, because the object file
cannot show them: no number in it may fall inside the original image's address
ranges (an absolute address copied out of the listing points at whatever the
rebuild happens to put there -- rebuild_info/pitfalls.md), and no DB/DW/DD may
stand in for an instruction.

Where the rebuild's side comes from.  WASM's own object file (OMF), not the
linked executable: every routine's extent, its public name, its relocations
and its branch targets are all in the object, so the check needs no link and a
single routine can be checked on its own while it is being written.  The
parser is written for what WASM 10.0a emits and refuses anything it does not
understand rather than guessing; its record handling follows the FD2 project's
tools/program_analysis/crt_callee_match/extract_obj_bytes.py.

Subcommands:
    roster              the fifteen routines, their files and label prefixes
    listing ADDR        the original routine as the transcriber needs to see it
    frag ASM [ASM...]   assemble standalone modules in DOSBox-X and check each
                        routine they define
    check               check every routine in src/'s assembly files, against
                        the objects the emittest build produced (--objs) or
                        freshly assembled (--fresh)
    selftest            prove every kind of failure is caught

Exit: 0 when everything checked passes.
"""
import argparse
import json
import os
import re
import shutil
import struct
import sys
from pathlib import Path

import capstone
from capstone import x86

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = ROOT / "fdps_game_files" / "FDPS.LE"
SNAPSHOT = ROOT / "ghidra_snapshot"
SRC = ROOT / "src"
WORK = ROOT / "workspace" / "rle_asm"
BUILT_OBJS = ROOT / "workspace" / "code_emit" / "out" / "objs"
RESULT = WORK / "check.json"

# The fifteen routines, in the original's address order.  Each routine's extent
# in the original is its entry up to the next entry, the last one up to the end
# of object 1 -- NOT the function size Ghidra records, which leaves out the NOP
# padding behind every forward short JMP (flow never reaches it).
#
# `file` is the assembly file under src/ that holds the routine and `prefix` the
# label prefix its local labels carry: WASM labels are module-wide, so two
# routines in one file cannot both have a label called `next_row`.
ROSTER = [
    ("000568db", "fdps_blit_dispatch", "rledisp", "disp"),
    ("00056a0d", "fdps_rle_blit_passthrough", "rlebase", "pass"),
    ("00056a8d", "fdps_rle_blit_remap_sprite_and_backdrop", "rlepal", "rmsb"),
    ("00056b25", "fdps_rle_blit_with_palette_remap", "rlepal", "rmpal"),
    ("00056bb7", "fdps_rle_blit_recolor", "rlepal", "rcol"),
    ("00056c5e", "fdps_rle_blit_scaled", "rlebase", "scal"),
    ("00056dc9", "fdps_rle_skip_row", "rlebase", "skip"),
    ("00056e2a", "fdps_rle_blit_rotated", "rleturn", "rot"),
    ("00057114", "fdps_rle_blit_rotated_scaled", "rleturn", "rots"),
    ("00057551", "fdps_rle_blit_mirrored_horizontal", "rlebase", "mirh"),
    ("000575ed", "fdps_rle_blit_mirrored_vertical", "rlebase", "mirv"),
    ("0005761b", "fdps_rle_blit_translucent", "rlemix", "tran"),
    ("00057793", "fdps_rle_blit_tint_sprite_and_backdrop", "rlemix", "tsb"),
    ("00057916", "fdps_rle_blit_tint", "rlemix", "tint"),
    ("00057a74", "fdps_rle_blit_translucent_color_range", "rlemix", "tcr"),
]
ASM_FILES = ["rledisp", "rlebase", "rlepal", "rleturn", "rlemix"]

# LE fixup source type -> bytes patched at the site (LE/LX specification).
SRC_SIZE = {0x00: 1, 0x02: 2, 0x03: 4, 0x05: 2, 0x06: 6, 0x07: 4, 0x08: 4}
NO_TARGET_OFFSET = {0x02}


class MatchError(Exception):
    """An input is not the shape this tool knows how to read."""


# ------------------------------------------------------------------ LE image
#
# The same reader tools/data_emit/check_data.py carries; tools are
# self-contained (tools/_index.md), so it is repeated rather than imported.

class LeImage:
    def __init__(self, data):
        self.data = data
        if data[:2] == b"LE":
            base = 0
        elif data[:2] == b"MZ":
            base = struct.unpack_from("<I", data, 0x3C)[0]
            if data[base:base + 2] != b"LE":
                raise MatchError("MZ stub does not point at an LE header")
        else:
            raise MatchError("not an LE executable")
        u32 = lambda off: struct.unpack_from("<I", data, base + off)[0]  # noqa: E731
        self.page_size = u32(0x28)
        self.last_page_size = u32(0x2C)
        self.num_pages = u32(0x14)
        self.data_pages_off = u32(0x80)
        obj_tbl, nobj = base + u32(0x40), u32(0x44)
        page_map = base + u32(0x48)
        self.objects = {}
        for i in range(nobj):
            vsize, reloc, _flags, pidx, pcnt, _ = struct.unpack_from(
                "<IIIIII", data, obj_tbl + 24 * i)
            self.objects[i + 1] = {"vsize": vsize, "base": reloc,
                                   "first_page": pidx, "pages": pcnt}
        for page in range(1, self.num_pages + 1):
            e = data[page_map + 4 * (page - 1): page_map + 4 * page]
            if ((e[0] << 16) | (e[1] << 8) | e[2]) != page:
                raise MatchError("object page map entry %d is not the identity"
                                 % page)
        self.fixup_page_tbl = base + u32(0x68)
        self.fixup_rec_tbl = base + u32(0x6C)
        self._fixups = None

    def ranges(self):
        """[(first linear address, one past the last)] of every object."""
        return [(o["base"], o["base"] + o["vsize"]) for o in self.objects.values()]

    def object_at(self, addr):
        for num, o in self.objects.items():
            if o["base"] <= addr < o["base"] + o["vsize"]:
                return num
        raise MatchError("0x%x lies in no object" % addr)

    def page_owner(self, page):
        for num, o in self.objects.items():
            if o["first_page"] <= page < o["first_page"] + o["pages"]:
                return num, (page - o["first_page"]) * self.page_size
        raise MatchError("page %d belongs to no object" % page)

    def read(self, addr, n):
        obj = self.object_at(addr)
        o = self.objects[obj]
        out = bytearray(n)
        for k in range(n):
            pos = addr - o["base"] + k
            if pos >= o["vsize"]:
                raise MatchError("read past object %d end" % obj)
            if pos // self.page_size >= o["pages"]:
                continue
            page = o["first_page"] + pos // self.page_size
            in_page = pos % self.page_size
            if page == self.num_pages and in_page >= self.last_page_size:
                continue
            fo = self.data_pages_off + (page - 1) * self.page_size + in_page
            if fo < len(self.data):
                out[k] = self.data[fo]
        return bytes(out)

    def fixups(self):
        """{site linear address: (width, target linear address or None)}."""
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
                    raise MatchError("unknown fixup source type 0x%02x" % stype)
                if src & 0x20:
                    count = d[pos]
                    pos += 1
                    offsets = None
                else:
                    offsets = [struct.unpack_from("<h", d, pos)[0]]
                    pos += 2
                target = None
                ttype = trg & 0x03
                if ttype == 0:
                    if trg & 0x40:
                        tobj = struct.unpack_from("<H", d, pos)[0]
                        pos += 2
                    else:
                        tobj = d[pos]
                        pos += 1
                    toff = 0
                    if stype not in NO_TARGET_OFFSET:
                        if trg & 0x10:
                            toff = struct.unpack_from("<I", d, pos)[0]
                            pos += 4
                        else:
                            toff = struct.unpack_from("<H", d, pos)[0]
                            pos += 2
                    target = self.objects[tobj]["base"] + toff
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
                    raise MatchError("fixup record overruns page %d" % page)
                obase = self.objects[obj]["base"]
                for so in offsets:
                    # A site that straddles a page boundary is listed under
                    # both pages, the second time with a negative offset; the
                    # linear address is the same either way.
                    sites[obase + page_base + so] = (SRC_SIZE[stype], target)
            if pos != end:
                raise MatchError("fixup records for page %d misaligned" % page)
        self._fixups = sites
        return sites


# ------------------------------------------------------------ Ghidra names

class GhidraNames:
    """Names the original's addresses the way the Ghidra snapshot does.

    An address inside a sized item (a function, a data item) is that item's
    name plus the offset into it; an address that starts a label is the label.
    Anything else is reported as a bare address, which can never equal a
    rebuild symbol name and so always fails -- as it should.
    """

    def __init__(self, snapshot=SNAPSHOT):
        self.items = []          # (start, size, name)
        self.exact = {}
        fn = snapshot / "functions.txt"
        for line in fn.read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or not line.strip() or line.startswith(" "):
                continue
            parts = [p.strip() for p in line.split("|")]
            m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", parts[-1])
            if m and re.fullmatch(r"[0-9a-fA-F]{8}", parts[0]):
                addr = int(parts[0], 16)
                self.exact[addr] = m.group(1)
        for line in (snapshot / "data.txt").read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or "|" not in line:
                continue
            parts = [p.strip() for p in line.split("|")]
            if not re.fullmatch(r"[0-9a-fA-F]{8}", parts[0]):
                continue
            if parts[3] and parts[3] != "-":
                self.items.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
        for line in (snapshot / "labels.txt").read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or "|" not in line:
                continue
            parts = [p.strip() for p in line.split("|")]
            if re.fullmatch(r"[0-9a-fA-F]{8}", parts[0]):
                self.exact.setdefault(int(parts[0], 16), parts[-1].split("::")[-1])
        self.items.sort()

    def name(self, addr):
        """(symbol, offset) for a linear address."""
        if addr in self.exact:
            return self.exact[addr], 0
        for start, size, name in self.items:
            if start <= addr < start + max(size, 1):
                return name, addr - start
        return "0x%08x" % addr, 0


# ------------------------------------------------------------------ OMF object

def _index(buf, i):
    b = buf[i]
    if b & 0x80:
        return ((b & 0x7F) << 8) | buf[i + 1], i + 2
    return b, i + 1


class OmfObject:
    """The one code segment of a WASM object, its publics and its fixups.

    `code` is the segment's bytes, `publics` {name: offset}, and `fixups` a
    list of dicts: site (offset in the segment), width, relative (True for a
    self-relative fixup -- a CALL or JMP to another module), target (an external
    name, or ("seg", offset) for a target inside this module's own code) and
    addend (the value WASM left in the field, which the linker adds).
    """

    def __init__(self, data, label="object"):
        self.label = label
        lnames = [None]
        segments = [None]
        externs = [None]
        self.publics = {}
        pub_seg = {}
        seg_bytes = {}
        raw_fixups = []
        frame_threads = {}
        target_threads = {}
        last = None                      # (segment index, record offset)
        off = 0
        while off + 3 <= len(data):
            rt = data[off]
            rl = struct.unpack_from("<H", data, off + 1)[0]
            body = data[off + 3: off + 3 + rl - 1]
            if rt == 0x96:
                i = 0
                while i < len(body):
                    n = body[i]
                    lnames.append(body[i + 1: i + 1 + n].decode("latin-1"))
                    i += 1 + n
            elif rt in (0x98, 0x99):
                acbp = body[0]
                i = 1
                if (acbp >> 5) & 7 == 0:
                    i += 3
                if rt == 0x99:
                    length = struct.unpack_from("<I", body, i)[0]
                    i += 4
                else:
                    length = struct.unpack_from("<H", body, i)[0]
                    i += 2
                name_idx, i = _index(body, i)
                class_idx, i = _index(body, i)
                segments.append({"name": lnames[name_idx], "class": lnames[class_idx],
                                 "length": length, "use32": bool(acbp & 1)})
                seg_bytes[len(segments) - 1] = bytearray(length)
            elif rt in (0x8C, 0xB4):
                i = 0
                while i < len(body):
                    n = body[i]
                    externs.append(body[i + 1: i + 1 + n].decode("latin-1"))
                    i += 1 + n
                    _, i = _index(body, i)
            elif rt in (0x90, 0x91):
                i = 0
                _grp, i = _index(body, i)
                seg, i = _index(body, i)
                if seg == 0:
                    i += 2
                while i < len(body):
                    n = body[i]
                    name = body[i + 1: i + 1 + n].decode("latin-1")
                    i += 1 + n
                    if rt == 0x91:
                        value = struct.unpack_from("<I", body, i)[0]
                        i += 4
                    else:
                        value = struct.unpack_from("<H", body, i)[0]
                        i += 2
                    _, i = _index(body, i)
                    self.publics[name] = value
                    pub_seg[name] = seg
            elif rt in (0xA0, 0xA1):
                seg, i = _index(body, 0)
                if rt == 0xA1:
                    at = struct.unpack_from("<I", body, i)[0]
                    i += 4
                else:
                    at = struct.unpack_from("<H", body, i)[0]
                    i += 2
                payload = body[i:]
                if seg not in seg_bytes:
                    raise MatchError("%s: LEDATA for undefined segment %d" % (label, seg))
                if at + len(payload) > len(seg_bytes[seg]):
                    raise MatchError("%s: LEDATA runs past segment end" % label)
                seg_bytes[seg][at: at + len(payload)] = payload
                last = (seg, at)
            elif rt in (0x9C, 0x9D):
                i = 0
                while i < len(body):
                    first = body[i]
                    if not first & 0x80:                       # THREAD
                        method = (first >> 2) & 7
                        thread = first & 3
                        is_frame = bool(first & 0x40)
                        i += 1
                        idx = None
                        if method < 3:
                            idx, i = _index(body, i)
                        (frame_threads if is_frame else target_threads)[thread] = (method, idx)
                        continue
                    if last is None:
                        raise MatchError("%s: FIXUPP before any LEDATA" % label)
                    relative = not (first & 0x40)
                    loc = (first >> 2) & 0xF
                    rec_off = ((first & 3) << 8) | body[i + 1]
                    fixdata = body[i + 2]
                    i += 3
                    if not fixdata & 0x80:
                        fmethod = (fixdata >> 4) & 7
                        if fmethod < 3:
                            _, i = _index(body, i)
                    if fixdata & 0x08:
                        tmethod, tidx = target_threads[fixdata & 3]
                    else:
                        tmethod = fixdata & 3
                        tidx, i = _index(body, i)
                    disp = None
                    if not fixdata & 0x04:
                        if rt == 0x9D:
                            disp = struct.unpack_from("<i", body, i)[0]
                            i += 4
                        else:
                            disp = struct.unpack_from("<h", body, i)[0]
                            i += 2
                    seg = last[0]
                    use32 = segments[seg]["use32"]
                    if loc in (9, 13) or (loc in (1, 5) and use32):
                        width = 4
                    else:
                        raise MatchError("%s: fixup location type %d is not a "
                                         "32-bit offset" % (label, loc))
                    raw_fixups.append((seg, last[1] + rec_off, width, relative,
                                       tmethod, tidx, disp))
            elif rt in (0x8A, 0x8B):
                break
            elif rt in (0x80, 0x88, 0x9A, 0x94, 0x95):
                pass
            else:
                raise MatchError("%s: OMF record 0x%02x is not handled" % (label, rt))
            off += 3 + rl

        code_segs = [k for k in range(1, len(segments))
                     if segments[k]["class"].upper() == "CODE"
                     and segments[k]["length"] > 0]
        if len(code_segs) != 1:
            raise MatchError("%s: expected one non-empty CODE segment, found %d"
                             % (label, len(code_segs)))
        cs = code_segs[0]
        for k in range(1, len(segments)):
            if k != cs and segments[k]["length"]:
                raise MatchError("%s: segment %s is not empty -- the assembly "
                                 "must define no data" % (label, segments[k]["name"]))
        self.code = bytes(seg_bytes[cs])
        for name, seg in pub_seg.items():
            if seg != cs:
                raise MatchError("%s: public %s is not in the code segment"
                                 % (label, name))
        self.fixups = []
        for seg, site, width, relative, tmethod, tidx, disp in raw_fixups:
            if seg != cs:
                raise MatchError("%s: fixup outside the code segment" % label)
            inline = struct.unpack_from("<i", self.code, site)[0]
            addend = inline + (disp or 0)
            if tmethod == 2:
                target = externs[tidx]
            elif tmethod == 0 and tidx == cs:
                target = ("seg", addend)
                addend = 0
            else:
                raise MatchError("%s: fixup target method %d is not handled"
                                 % (label, tmethod))
            self.fixups.append({"site": site, "width": width, "relative": relative,
                                "target": target, "addend": addend})

    def extent(self, name):
        """(start, end) of a public routine: up to the next public or the end."""
        start = self.publics[name]
        later = [v for v in self.publics.values() if v > start]
        return start, (min(later) if later else len(self.code))

    def name_at(self, offset):
        """(public, delta) for a code offset in this module."""
        best = None
        for name, value in self.publics.items():
            if value <= offset and (best is None or value > self.publics[best]):
                best = name
        if best is None:
            return "<module+0x%x>" % offset, 0
        return best, offset - self.publics[best]


# ---------------------------------------------------------------- one routine
#
# Both sides are brought to the same shape before they are compared: the bytes
# of the routine, the relocated fields inside it as {offset: (width, symbol,
# addend)}, and a function that names where a branch goes.

class Side:
    def __init__(self, label, code, relocs, rel_calls, outside):
        self.label = label
        self.code = code
        # {offset: (width, symbol, addend)} -- absolute relocated fields
        self.relocs = relocs
        # {offset of the field: (symbol, addend)} -- self-relative fields whose
        # target the linker fills in (a CALL to another module)
        self.rel_calls = rel_calls
        # offset relative to the routine start -> (symbol, delta), for a branch
        # that leaves the routine without a fixup
        self.outside = outside


def original_side(img, names, start, end):
    code = img.read(start, end - start)
    relocs = {}
    for site, (width, target) in img.fixups().items():
        if start <= site < end:
            sym, delta = names.name(target) if target is not None else ("?", 0)
            relocs[site - start] = (width, sym, delta)
    return Side("original", code, relocs, {},
                lambda rel: names.name(start + rel))


def rebuilt_side(obj, name):
    start, end = obj.extent(name)
    relocs, rel_calls = {}, {}
    for f in obj.fixups:
        if not start <= f["site"] < end:
            continue
        if isinstance(f["target"], tuple):
            sym, delta = obj.name_at(f["target"][1])
            target = (sym, delta + f["addend"])
        else:
            target = (f["target"], f["addend"])
        if f["relative"]:
            rel_calls[f["site"] - start] = target
        else:
            relocs[f["site"] - start] = (f["width"],) + target
    return Side("rebuilt", obj.code[start:end], relocs, rel_calls,
                lambda rel: obj.name_at(start + rel))


_MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_MD.detail = True


def _is_branch(insn):
    m = insn.mnemonic
    return (m.startswith("j") or m == "call" or m.startswith("loop")) \
        and insn.operands and insn.operands[0].type == x86.X86_OP_IMM


def decode(side):
    """Instructions of a side with every relocated field zeroed.

    Returns (insns, error): insns is a list of dicts; error is set when the
    bytes do not decode to the end, which is itself a failure.
    """
    code = bytearray(side.code)
    for off, (width, _s, _d) in side.relocs.items():
        code[off: off + width] = bytes(width)
    for off in side.rel_calls:
        code[off: off + 4] = bytes(4)
    out = []
    pos = 0
    for insn in _MD.disasm(bytes(code), 0):
        out.append(insn)
        pos = insn.address + insn.size
    err = None if pos == len(code) else "bytes at +0x%x do not decode" % pos
    return out, err


def _target(insn):
    """A branch's destination relative to the routine start, signed: the
    routines are disassembled at address 0, so a branch back out of one comes
    out of capstone as a large unsigned number."""
    tgt = insn.operands[0].imm & 0xFFFFFFFF
    return tgt - 0x100000000 if tgt >= 0x80000000 else tgt


def branch_target(side, insn, starts):
    """Where a branch lands, as a comparable label."""
    field = insn.address + insn.size - 4 if insn.size >= 5 else None
    if field is not None and field in side.rel_calls:
        sym, delta = side.rel_calls[field]
        return "%s%s" % (sym, "+0x%x" % delta if delta else "")
    tgt = _target(insn)
    if 0 <= tgt < len(side.code):
        if tgt in starts:
            return "#%d" % starts[tgt]
        return "mid-instruction +0x%x" % tgt
    sym, delta = side.outside(tgt)
    return "%s%s" % (sym, "+0x%x" % delta if delta else "")


def render(side, insn, starts):
    if _is_branch(insn):
        return "%s %s" % (insn.mnemonic, branch_target(side, insn, starts))
    return ("%s %s" % (insn.mnemonic, insn.op_str)).strip()


def compare(orig, reb):
    """[problem, ...] -- empty when the rebuilt routine runs the same instructions."""
    problems = []
    oi, oerr = decode(orig)
    ri, rerr = decode(reb)
    if oerr:
        problems.append("original: " + oerr)
    if rerr:
        problems.append("rebuilt: " + rerr)
    ostarts = {x.address: k for k, x in enumerate(oi)}
    rstarts = {x.address: k for k, x in enumerate(ri)}
    for k in range(max(len(oi), len(ri))):
        if k >= len(oi):
            problems.append("#%d: rebuilt has an extra instruction: %s"
                            % (k, render(reb, ri[k], rstarts)))
            break
        if k >= len(ri):
            problems.append("#%d: rebuilt ends early; original continues with %s"
                            % (k, render(orig, oi[k], ostarts)))
            break
        o, r = oi[k], ri[k]
        ot, rt = render(orig, o, ostarts), render(reb, r, rstarts)
        if o.size != r.size:
            problems.append("#%d +0x%x: length %d vs %d (%s | %s)"
                            % (k, o.address, o.size, r.size, ot, rt))
            break                         # everything after is shifted
        if ot != rt:
            problems.append("#%d +0x%x: %s | rebuilt: %s" % (k, o.address, ot, rt))
    if not problems or all(not p.startswith("#") for p in problems):
        for off in sorted(set(orig.relocs) | set(reb.relocs)):
            a, b = orig.relocs.get(off), reb.relocs.get(off)
            if a is None:
                problems.append("reloc +0x%x: rebuilt has one (%s+%d) the original "
                                "does not" % (off, b[1], b[2]))
            elif b is None:
                problems.append("reloc +0x%x: original has one (%s+%d) the rebuilt "
                                "does not" % (off, a[1], a[2]))
            elif a != b:
                problems.append("reloc +0x%x: original %s+%d (%d bytes), rebuilt "
                                "%s+%d (%d bytes)" % (off, a[1], a[2], a[0],
                                                      b[1], b[2], b[0]))
    return problems


# ---------------------------------------------------------------- source scan

NUMBER_RX = re.compile(r"\b(?:[0-9][0-9A-Fa-f]*[Hh]|0[xX][0-9A-Fa-f]+|[0-9]+)\b")
DATA_DIRECTIVE_RX = re.compile(r"^\s*(?:[A-Za-z_$?@][\w$?@]*\s*:?\s+)?(db|dw|dd|df|dq|dt)\b",
                               re.I)


def _number(tok):
    t = tok.lower()
    if t.startswith("0x"):
        return int(t, 16)
    if t.endswith("h"):
        return int(t[:-1], 16)
    return int(t, 10)


def scan_source(text, image_ranges):
    """[problem, ...] for a numeric literal inside the original image's address
    ranges, or a data directive, anywhere outside a comment."""
    problems = []
    for lineno, line in enumerate(text.splitlines(), 1):
        code = line.split(";", 1)[0]
        code = re.sub(r"'[^']*'|\"[^\"]*\"", "", code)
        if DATA_DIRECTIVE_RX.search(code):
            problems.append("line %d: data directive in the code: %s"
                            % (lineno, code.strip()))
        for m in NUMBER_RX.finditer(code):
            try:
                value = _number(m.group(0))
            except ValueError:
                continue
            if any(lo <= value < hi for lo, hi in image_ranges):
                problems.append("line %d: %s lies in the original image's address "
                                "range -- use the symbol" % (lineno, m.group(0)))
    return problems


# ------------------------------------------------------------------ assembling

def assemble(asm_paths, work, timeout=180):
    """Assemble each file with WASM 10.0a inside DOSBox-X; {stem: OmfObject}.

    One throwaway work directory per call, so concurrent callers (one agent per
    routine) never share a guest tree.  The toolchain is the DOS one the build
    uses, driven through tools/fdps_build/build_min.py.
    """
    sys.path.insert(0, str(ROOT / "tools" / "fdps_build"))
    import build_min as bm
    work = Path(work)
    if work.exists():
        shutil.rmtree(str(work))
    (work / "S").mkdir(parents=True)
    stems = []
    for p in asm_paths:
        p = Path(p)
        stem = p.stem.upper()
        if len(stem) > 8:
            raise MatchError("%s: not an 8.3 name" % p.name)
        shutil.copy2(str(p), str(work / "S" / (stem + ".ASM")))
        stems.append(stem)
    lines = []
    for stem in stems:
        lines.append(r"echo %s > F:\HB.TXT" % stem)
        lines.append(r"echo === %s === >> F:\ASM.OUT" % stem)
        lines.append(r"WASM F:\S\%s.ASM -fo=F:\%s.OBJ >> F:\ASM.OUT" % (stem, stem))
    lines.append(r"echo done > F:\ASM.DON")
    (work / "A.BAT").write_text("\r\n".join(lines) + "\r\n", encoding="latin-1")
    watcom = Path(os.environ.get("FDPS_WATCOM") or bm.WATCOM_DEFAULT)
    bm.write_conf(work / "a.conf", [("D", watcom), ("F", work)], None,
                  ["set WATCOM=D:\\",
                   "set PATH=Z:\\;" + ";".join("D:\\%s" % d for d in bm.BINDIRS),
                   "F:", r"F:\A.BAT"],
                  logfile=work / "dosbox.log")
    proc, fp = bm.launch(bm.resolve_dosbox(), work / "a.conf", work / "stdio.log")
    mode, _hb, _secs = bm.wait(proc, work, "asm.don", timeout)
    fp.close()
    out = bm.find_ci(work, "asm.out")
    transcript = out.read_text(encoding="latin-1", errors="replace") if out else ""
    if mode != "completed":
        raise MatchError("WASM run did not complete (mode=%s)\n%s" % (mode, transcript))
    objs, diags = {}, []
    for line in transcript.splitlines():
        if re.search(r"\b(Error|Warning)\b", line) and "0 warnings, 0 errors" not in line:
            diags.append(line.strip())
    for stem in stems:
        p = bm.find_ci(work, stem + ".OBJ")
        if p is None:
            diags.append("%s.OBJ was not produced" % stem)
            continue
        objs[stem] = OmfObject(p.read_bytes(), stem)
    return objs, diags, transcript


# ------------------------------------------------------------------- driving

def load_original():
    if not ORIGINAL.is_file():
        raise MatchError("missing %s" % ORIGINAL)
    return LeImage(ORIGINAL.read_bytes()), GhidraNames()


def original_extent(img, addr):
    starts = sorted(int(a, 16) for a, _n, _f, _p in ROSTER)
    i = starts.index(addr)
    if i + 1 < len(starts):
        return addr, starts[i + 1]
    o1 = img.objects[img.object_at(addr)]
    return addr, o1["base"] + o1["vsize"]


def check_routine(img, names, obj, addr, name):
    start, end = original_extent(img, addr)
    if name not in obj.publics:
        return ["%s is not a public of %s" % (name, obj.label)]
    return compare(original_side(img, names, start, end), rebuilt_side(obj, name))


def roster_entry(key):
    for row in ROSTER:
        if key.lower() in (row[0], row[0].lstrip("0"), row[1].lower()):
            return row
    raise MatchError("%s is not one of the fifteen routines" % key)


def cmd_listing(key):
    addr_s, name, fstem, prefix = roster_entry(key)
    img, names = load_original()
    start, end = original_extent(img, int(addr_s, 16))
    side = original_side(img, names, start, end)
    insns, err = decode(side)
    starts = {x.address: k for k, x in enumerate(insns)}
    targets = {}
    for x in insns:
        if _is_branch(x) and 0 <= _target(x) < len(side.code):
            targets.setdefault(_target(x), []).append(x.address)
    print("; %s  %s .. %08x  (%d bytes, %d instructions)  -> src/%s.asm, "
          "label prefix %s_" % (name, addr_s, end, end - start, len(insns), fstem,
                                prefix))
    print("; offset  address   bytes                    instruction")
    for k, x in enumerate(insns):
        raw = side.code[x.address: x.address + x.size]
        notes = []
        text = render(side, x, starts)
        for off, (w, sym, d) in side.relocs.items():
            if x.address <= off < x.address + x.size:
                # The field was zeroed for decoding; show what it names.  The
                # zero stands where capstone printed the address, as a bare
                # 0 inside the brackets or as the immediate.
                ref = "%s%s" % (sym, "+%d" % d if d else "")
                text = re.sub(r"\[0\]", "[%s]" % ref, text, count=1) \
                    if "[0]" in text else re.sub(r"(\+ )0\]", r"\g<1>%s]" % ref, text,
                                                 count=1) \
                    if re.search(r"\+ 0\]", text) else re.sub(r", 0$", ", offset " + ref,
                                                            text)
                notes.append("reloc field +%d -> %s" % (off - x.address, ref))
        if _is_branch(x):
            tgt = _target(x)
            fwd = tgt > x.address
            notes.append("%s %s" % ("forward" if fwd else "backward",
                                    "short" if x.size == 2 else "long"))
            if fwd and x.size in (5, 6) and 0 <= tgt < len(side.code):
                shrink = x.size - 2
                if tgt - (x.address + 2) - shrink <= 127:
                    notes.append("would reach as a short branch once shortened")
        if x.address in targets:
            notes.insert(0, "branch target of %s" % ", ".join(
                "#%d" % starts[a] for a in targets[x.address]))
        print("%4d +%04x %08x  %-24s %s%s" % (
            k, x.address, start + x.address, raw.hex(" "), text,
            ("   ; " + "; ".join(notes)) if notes else ""))
    if err:
        print("; ERROR: " + err)


def cmd_frag(paths, as_json):
    img, names = load_original()
    ranges = img.ranges()
    tag = "frag-%d" % os.getpid()
    objs, diags, _t = assemble(paths, WORK / "tmp" / tag)
    results = []
    for p in paths:
        p = Path(p)
        src_problems = scan_source(p.read_text(encoding="latin-1"), ranges)
        obj = objs.get(p.stem.upper())
        routines = []
        if obj is not None:
            for addr_s, name, _f, _p in ROSTER:
                if name in obj.publics:
                    routines.append({"address": addr_s, "name": name,
                                     "problems": check_routine(img, names, obj,
                                                               int(addr_s, 16), name)})
            known = {n for _a, n, _f, _p in ROSTER}
            for pub in obj.publics:
                if pub not in known:
                    src_problems.append("public %s is not one of the fifteen "
                                        "routines" % pub)
        results.append({"file": str(p), "assembler": diags,
                        "source": src_problems, "routines": routines})
    shutil.rmtree(str(WORK / "tmp" / tag), ignore_errors=True)
    ok = report(results, as_json)
    return ok


def report(results, as_json):
    ok = True
    for r in results:
        if r["assembler"] or r["source"] or not r["routines"]:
            ok = False
        for rt in r["routines"]:
            ok = ok and not rt["problems"]
    if as_json:
        print(json.dumps({"pass": ok, "results": results}, indent=2))
    else:
        for r in results:
            print("== %s" % r["file"])
            for d in r["assembler"]:
                print("  assembler: " + d)
            for s in r["source"]:
                print("  source: " + s)
            if not r["routines"]:
                print("  no routine of the fifteen is defined here")
            for rt in r["routines"]:
                print("  %s %s: %s" % (rt["address"], rt["name"],
                                       "PASS" if not rt["problems"] else "FAIL"))
                for pr in rt["problems"][:20]:
                    print("    " + pr)
        print("[result] %s" % ("PASS" if ok else "FAIL"))
    return ok


def cmd_check(objs_dir, fresh, as_json):
    img, names = load_original()
    ranges = img.ranges()
    paths = [SRC / (s + ".asm") for s in ASM_FILES]
    missing = [str(p) for p in paths if not p.is_file()]
    results = []
    if missing:
        results.append({"file": ", ".join(missing), "assembler": ["missing"],
                        "source": [], "routines": []})
        return report(results, as_json)
    if fresh:
        objs, diags, _t = assemble(paths, WORK / "tmp" / "check")
    else:
        objs, diags = {}, []
        for p in paths:
            q = None
            if objs_dir.is_dir():
                for c in objs_dir.iterdir():
                    if c.name.lower() == p.stem.lower() + ".obj":
                        q = c
            if q is None:
                diags.append("%s.OBJ not in %s -- build first" % (p.stem.upper(),
                                                                  objs_dir))
            else:
                objs[p.stem.upper()] = OmfObject(q.read_bytes(), q.name)
    seen = set()
    for p in paths:
        stem = p.stem.upper()
        obj = objs.get(stem)
        routines = []
        src_problems = scan_source(p.read_text(encoding="latin-1"), ranges)
        if obj is not None:
            want = [row for row in ROSTER if row[2] == p.stem]
            for addr_s, name, _f, _p in want:
                routines.append({"address": addr_s, "name": name,
                                 "problems": check_routine(img, names, obj,
                                                           int(addr_s, 16), name)})
                seen.add(name)
            for pub in obj.publics:
                if pub not in {row[1] for row in want}:
                    src_problems.append("public %s does not belong in this file"
                                        % pub)
        results.append({"file": "src/%s.asm" % p.stem,
                        "assembler": [d for d in diags if stem in d.upper()] or [],
                        "source": src_problems, "routines": routines})
    unassigned = [n for _a, n, _f, _p in ROSTER if n not in seen]
    if unassigned:
        results.append({"file": "(roster)", "assembler": [],
                        "source": ["not checked: " + ", ".join(unassigned)],
                        "routines": []})
    ok = report(results, as_json)
    WORK.mkdir(parents=True, exist_ok=True)
    RESULT.write_text(json.dumps({"pass": ok, "results": results}, indent=2) + "\n",
                      encoding="utf-8")
    return ok


# ------------------------------------------------------------------ selftest

def _side(code, relocs=None, rel_calls=None, outside=None):
    return Side("t", bytes.fromhex(code.replace(" ", "")), relocs or {},
                rel_calls or {}, outside or (lambda rel: ("0x%x" % rel, 0)))


def _omf(records):
    out = bytearray()
    for rt, body in records:
        rec = bytes([rt]) + struct.pack("<H", len(body) + 1) + body
        out += rec + bytes([(-sum(rec)) & 0xFF])
    return bytes(out)


def _name(s):
    return bytes([len(s)]) + s.encode("latin-1")


def selftest():
    rows = []

    def row(label, passed, detail=""):
        rows.append((label, passed, detail))

    # A small routine in the original's shape:
    #   +0  66 89 15 [data_a]   mov word ptr [data_a], dx
    #   +7  66 0b db            or bx, bx
    #   +a  75 04               jnz short +0x10   (forward short, padded)
    #   +c  90 90 90 90
    #   +10 e8 <rel32>          call ext_fn
    #   +15 49                  dec ecx
    #   +16 75 f8               jnz +0x10          (backward short)
    #   +18 c3                  ret
    base = "668915 00000700 660bdb 7504 90909090 e8 {call} 49 75f8 c3"
    names = {0x70000: ("data_a", 0), 0x70002: ("data_a", 2), 0x9000: ("ext_fn", 0)}

    def orig_outside(rel):
        return names.get(0x5000 + rel, ("0x%x" % (0x5000 + rel), 0))

    # The original's CALL is resolved by address (no fixup inside one object).
    call_rel = (0x9000 - (0x5000 + 0x15)) & 0xFFFFFFFF
    orig = _side(base.format(call=struct.pack("<I", call_rel).hex()),
                 relocs={3: (4, "data_a", 0)}, outside=orig_outside)

    def reb(code=None, relocs=None, rel_calls=None):
        return _side(code or base.format(call="00000000"),
                     relocs={3: (4, "data_a", 0)} if relocs is None else relocs,
                     rel_calls={0x11: ("ext_fn", 0)} if rel_calls is None else rel_calls)

    p = compare(orig, reb())
    row("identical routine passes", not p, "; ".join(p))
    alt = base.replace("660bdb", "6609db").format(call="00000000")
    p = compare(orig, reb(code=alt))
    row("the other reg,reg encoding passes", not p, "; ".join(p))
    p = compare(_side("66 3d 0000 c3"), _side("66 83f8 00 c3"))
    row("cmp ax,0 in its imm8 form passes", not p, "; ".join(p))

    bad = base.replace("660bdb", "6623db").format(call="00000000")
    p = compare(orig, reb(code=bad))
    row("a different instruction fails", bool(p), "; ".join(p))
    bad = base.replace("7504 90909090", "7503 909090").format(call="00000000")
    p = compare(orig, reb(code=bad, rel_calls={0x10: ("ext_fn", 0)}))
    row("a dropped NOP fails", bool(p), "; ".join(p))
    bad = base.replace("7504 90909090", "0f8500000000").format(call="00000000")
    p = compare(orig, reb(code=bad))
    row("a long form where the original is short fails", bool(p), "; ".join(p))
    bad = base.replace("7504", "7505").format(call="00000000")
    p = compare(orig, reb(code=bad))
    row("a branch one byte out fails", bool(p), "; ".join(p))
    bad = base.replace("75f8", "75f7").format(call="00000000")
    p = compare(orig, reb(code=bad))
    row("a backward branch one byte out fails", bool(p), "; ".join(p))
    p = compare(orig, reb(relocs={}))
    row("a missing relocation fails", bool(p), "; ".join(p))
    p = compare(orig, reb(relocs={3: (4, "data_b", 0)}))
    row("a relocation to another symbol fails", bool(p), "; ".join(p))
    p = compare(orig, reb(relocs={3: (4, "data_a", 2)}))
    row("a relocation to another offset fails", bool(p), "; ".join(p))
    p = compare(_side("a1 00000000 c3", relocs={1: (4, "data_a", 0)}),
                _side("a1 00000000 a1 00000000", relocs={1: (4, "data_a", 0),
                                                          6: (4, "data_a", 0)}))
    row("an extra relocation or instruction fails", bool(p), "; ".join(p))
    p = compare(_side("b8 00000000 c3"), _side("b8 00000000 c3",
                                               relocs={1: (4, "data_a", 0)}))
    row("a relocation the original does not have fails", bool(p), "; ".join(p))
    p = compare(orig, reb(rel_calls={0x11: ("other_fn", 0)}))
    row("a call to another routine fails", bool(p), "; ".join(p))
    p = compare(orig, reb(code=base.format(call="00000000") + "c3"))
    row("a trailing extra instruction fails", bool(p), "; ".join(p))
    p = compare(orig, reb(code=base.format(call="00000000")[:-2]))
    row("a missing final instruction fails", bool(p), "; ".join(p))

    ranges = [(0x10000, 0x57c60), (0x60000, 0x6c3c0), (0x70000, 0x70054)]
    p = scan_source("        mov  ax,word ptr [7002eh]\n", ranges)
    row("an image address in the source fails", bool(p), "; ".join(p))
    p = scan_source("        add  edi,140h ; 0x7002e in a comment\n"
                    "        mov  eax,0a0000h\n", ranges)
    row("constants outside the image pass", not p, "; ".join(p))
    p = scan_source("        db   90h\n", ranges)
    row("a DB in the code fails", bool(p), "; ".join(p))
    p = scan_source("lbl:    dd   0\n", ranges)
    row("a labelled DD fails", bool(p), "; ".join(p))

    # OMF: a module with one code segment, one public, one external, one
    # self-relative and one segment-relative fixup, and one internal reference.
    code = bytes.fromhex("e8 00000000 a1 04000000 b8 00000000 c3".replace(" ", ""))
    lnames = _name("") + _name("CODE") + _name("_TEXT")
    recs = [
        (0x80, _name("T.ASM")),
        (0x96, lnames),
        (0x99, bytes([0x29]) + struct.pack("<I", len(code)) + bytes([3, 2, 1])),
        (0x8C, _name("ext_fn") + b"\x00" + _name("data_a") + b"\x00"),
        (0x90, bytes([0, 1]) + _name("pub_fn") + struct.pack("<H", 0) + b"\x00"),
        (0xA0, bytes([1]) + struct.pack("<H", 0) + code),
        # self-relative (M=0), loc 9, offset 1; frame 5, target EXTDEF #1, P=1
        # segment-relative (M=1), loc 9, offset 6; target EXTDEF #2
        # segment-relative, offset 0xb, target SEGDEF #1 (internal), P=1
        (0x9D, bytes([0x80 | (9 << 2), 0x01, 0x56, 0x01,
                      0xC0 | (9 << 2), 0x06, 0x56, 0x02,
                      0xC0 | (9 << 2), 0x0B, 0x54, 0x01])),
        (0x8A, b"\x00"),
    ]
    try:
        o = OmfObject(_omf(recs), "selftest")
        f = {x["site"]: x for x in o.fixups}
        good = (o.publics == {"pub_fn": 0} and o.code == code
                and f[1]["relative"] and f[1]["target"] == "ext_fn"
                and not f[6]["relative"] and f[6]["target"] == "data_a"
                and f[6]["addend"] == 4 and f[0xB]["target"] == ("seg", 0))
        row("OMF reader: code, publics, both fixup kinds", good,
            json.dumps(o.fixups))
        s = rebuilt_side(o, "pub_fn")
        row("OMF side: relative call and absolute relocation are told apart",
            s.rel_calls == {1: ("ext_fn", 0)}
            and s.relocs == {6: (4, "data_a", 4), 11: (4, "pub_fn", 0)},
            "%r %r" % (s.rel_calls, s.relocs))
    except MatchError as exc:
        row("OMF reader: code, publics, both fixup kinds", False, str(exc))
    try:
        OmfObject(_omf(recs[:5] + [(0x9D, bytes([0xC0, 0x06, 0x56, 0x02]))]
                       + recs[7:]), "selftest")
        row("OMF reader refuses a fixup before any data", False, "accepted")
    except MatchError as exc:
        row("OMF reader refuses a fixup before any data", True, str(exc))
    data_seg = recs[:3] + [
        (0x96, _name("_DATA") + _name("DATA")),
        (0x99, bytes([0x29]) + struct.pack("<I", 4) + bytes([4, 5, 1]))] + recs[3:]
    try:
        OmfObject(_omf(data_seg), "selftest")
        row("OMF reader refuses a module that defines data", False, "accepted")
    except MatchError as exc:
        row("OMF reader refuses a module that defines data", True, str(exc))

    ok = True
    for label, passed, detail in rows:
        print("[selftest] %-62s %s%s" % (label, "ok" if passed else "FAIL",
                                         "" if passed else "  (%s)" % detail))
        ok = ok and passed
    print("[result] %s" % ("PASS" if ok else "FAIL"))
    return ok


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("roster")
    s = sub.add_parser("listing")
    s.add_argument("routine")
    s = sub.add_parser("frag")
    s.add_argument("asm", nargs="+")
    s.add_argument("--json", action="store_true")
    s = sub.add_parser("check")
    s.add_argument("--objs", type=Path, default=BUILT_OBJS)
    s.add_argument("--fresh", action="store_true",
                   help="assemble src/'s files now instead of reading the "
                        "emittest build's objects")
    s.add_argument("--json", action="store_true")
    sub.add_parser("selftest")
    args = ap.parse_args()
    try:
        if args.cmd == "roster":
            print(json.dumps([{"address": a, "name": n, "file": f, "prefix": p}
                              for a, n, f, p in ROSTER], indent=2))
            return 0
        if args.cmd == "listing":
            cmd_listing(args.routine)
            return 0
        if args.cmd == "frag":
            return 0 if cmd_frag(args.asm, args.json) else 1
        if args.cmd == "check":
            return 0 if cmd_check(args.objs, args.fresh, args.json) else 1
        if args.cmd == "selftest":
            return 0 if selftest() else 1
    except MatchError as exc:
        print("[error] %s" % exc)
        return 1
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
