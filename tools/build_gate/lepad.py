"""lepad.py -- the alignment gaps wcc386 leaves uncleared, located exactly.

wcc386 aligns what it emits into a data segment but never writes the bytes it
skips: the 1-3 bytes after a string literal's terminating NUL up to the next
4-byte boundary in CONST, and the gap after a small object in _DATA up to the
next object's alignment.  Those bytes carry whatever the compiler's buffer
held before, which follows the text it compiled: the same src/ with its headers
checked out as CRLF instead of written as LF is enough to change them.  No
program reads them, yet the relocation-aware comparison in
lefixup.py sees data bytes outside every relocation and calls the build
`different` (rebuild_info/build_gate.md).

This module finds those bytes, for one linked image, from what the build itself
left behind -- and only where that can be proved:

  literal gap   in CONST.  Every literal the program uses is the target of a
                relocation, so its start is known.  An item counts as a string
                only when it starts on a 4-byte boundary and every reference to
                it takes its address as a value (a pointer stored in a data
                object, or the imm32 of `push imm32` / `mov r32,imm32`).  That
                excludes the floating-point constants wcc386 also puts in CONST,
                which are read through an x87 memory operand.  The gap is the
                bytes after the first NUL up to the next 4-byte boundary, and
                only when the next referenced item or symbol starts exactly at
                that boundary.
  symbol gap    in _DATA and CONST2.  Symbol starts come from the linker map
                (publics) and the compiled objects (statics, placed through the
                module's publics); every relocation target is a start as well.
                A symbol's size comes from its definition in the source that was
                compiled, and only for a scalar, pointer or array of those with
                literal dimensions.  The gap is from the symbol's end to the
                next start, and only when that start is the first 2- or 4-byte
                boundary at or after the end.

Anything that cannot be proved stays compared: an unsized symbol, a module whose
statics cannot be placed, a referenced item not proved to be a string.  A gap
never contains a relocation site, and the gap positions themselves are part of
the fingerprint (lefixup.profile), so a declaration that grows or shrinks moves
the mask and still reads as `different`.

    python tools/build_gate/lepad.py selftest
"""
import bisect
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lefixup  # noqa: E402

ALIGN = 4
EXEC_FLAG = 0x0004                      # LE object flag: executable

LITERAL_SEGMENTS = ("CONST",)
SYMBOL_SEGMENTS = ("_DATA", "CONST2")

# Declared size of a scalar, keyed by its type specifiers in source order.
# Anything else (a struct, a typedef, an enum) is unknown and never sized.
SCALARS = {}
for _names, _size in (
        (("char", "signed char", "unsigned char"), 1),
        (("short", "short int", "signed short", "signed short int",
          "unsigned short", "unsigned short int"), 2),
        (("int", "signed", "signed int", "unsigned", "unsigned int",
          "long", "long int", "signed long", "signed long int",
          "unsigned long", "unsigned long int", "float"), 4),
        (("double",), 8)):
    for _n in _names:
        SCALARS[_n] = _size


class PadError(Exception):
    """A map or object file is not in the shape this module reads."""


def align(value, unit):
    return (value + unit - 1) // unit * unit


# ------------------------------------------------------------------ LE image

def le_objects(data):
    """{object number: {base, flags, start, end}} with file extents.

    wlink writes each object's pages in order, so an object's bytes run from
    its first page for `npages` pages; the last page of the file is truncated
    and the loader zero-fills the rest, so the extent is clamped to the file.
    """
    le = lefixup.le_base(data)

    def u32(off):
        return struct.unpack_from("<I", data, le + off)[0]

    page, table, count, pages = u32(0x28), le + u32(0x40), u32(0x44), u32(0x80)
    out = {}
    for n in range(count):
        _vsize, base, flags, first, npages, _ = struct.unpack_from(
            "<6I", data, table + 24 * n)
        start = pages + (first - 1) * page
        end = max(start, min(start + npages * page, len(data)))
        out[n + 1] = {"base": base, "flags": flags, "start": start, "end": end}
    return out


# ---------------------------------------------------------------- linker map

SEG_RX = re.compile(r"^(\S+)\s+(\S+)\s+(?:(\S+)\s+)?([0-9a-f]{4}):([0-9a-f]{8})"
                    r"\s+([0-9a-f]{8})\s*$", re.I)
SYM_RX = re.compile(r"^([0-9a-f]{4}):([0-9a-f]{8})[*+]?\s+(\S+)", re.I)
MODULE_RX = re.compile(r"^Module:\s*([^(]+?)\s*(?:\(([^)]*)\))?\s*$")
BOX_RX = re.compile(r"^\s*\|\s*(.*?)\s*\|\s*$")


def _basename(path):
    return re.split(r"[\\/]", path.strip())[-1].upper()


def parse_map(text):
    """Segments and per-module public symbols out of a wlink map.

    Returns {"segments": [{name, cls, obj, off, size}], "modules": [{file,
    source, symbols: [(obj, off, name)]}]}.  Object-relative offsets: for a
    DOS/4G image the map's `0002:` is LE object 2.
    """
    segments, modules = [], []
    section = None
    for line in text.splitlines():
        box = BOX_RX.match(line)
        if box:
            # Every section of a wlink map opens with a boxed title.
            section = {"Segments": "segments",
                       "Memory Map": "memory"}.get(box.group(1))
            continue
        if section == "segments":
            m = SEG_RX.match(line)
            if m:
                segments.append({"name": m.group(1), "cls": m.group(2),
                                 "obj": int(m.group(4), 16),
                                 "off": int(m.group(5), 16),
                                 "size": int(m.group(6), 16)})
        elif section == "memory":
            m = MODULE_RX.match(line)
            if m:
                modules.append({"file": _basename(m.group(1)),
                                "source": _basename(m.group(2) or ""),
                                "symbols": []})
                continue
            m = SYM_RX.match(line)
            if m and modules:
                modules[-1]["symbols"].append(
                    (int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    if not segments:
        raise PadError("no segment table in the map")
    return {"segments": segments, "modules": modules}


# --------------------------------------------------------------- OMF objects

def _omf_index(body, i):
    b = body[i]
    if b & 0x80:
        return ((b & 0x7F) << 8) | body[i + 1], i + 2
    return b, i + 1


def read_obj(data, label="object"):
    """Segment lengths and every public and static symbol of one OMF object.

    Returns {"segments": {name: length}, "symbols": [(segment, name, offset,
    is_static)]}.  Statics are LPUBDEF records: wcc386 writes one for every
    file-scope `static`, and the map never lists them.
    """
    lnames, segs = [None], [None]
    seglen, symbols = {}, []
    off = 0
    while off < len(data):
        if off + 3 > len(data):
            raise PadError("%s: truncated record header at 0x%x" % (label, off))
        rt = data[off]
        rl = struct.unpack_from("<H", data, off + 1)[0]
        if off + 3 + rl > len(data) or rl < 1:
            raise PadError("%s: record 0x%02x at 0x%x runs past the file"
                           % (label, rt, off))
        body = data[off + 3: off + 3 + rl - 1]
        if rt == 0x96:                                      # LNAMES
            i = 0
            while i < len(body):
                n = body[i]
                lnames.append(body[i + 1: i + 1 + n].decode("latin-1"))
                i += 1 + n
        elif rt in (0x98, 0x99):                            # SEGDEF
            acbp = body[0]
            i = 1
            if (acbp >> 5) & 7 == 0:
                i += 3
            wide = rt == 0x99
            length = struct.unpack_from("<I" if wide else "<H", body, i)[0]
            i += 4 if wide else 2
            name_idx, i = _omf_index(body, i)
            segs.append(lnames[name_idx])
            seglen[lnames[name_idx]] = length
        elif rt in (0x90, 0x91, 0xB6, 0xB7):                # (L)PUBDEF
            wide = rt in (0x91, 0xB7)
            i = 0
            _grp, i = _omf_index(body, i)
            seg, i = _omf_index(body, i)
            if seg == 0:
                i += 2
            while i < len(body):
                n = body[i]
                name = body[i + 1: i + 1 + n].decode("latin-1")
                i += 1 + n
                value = struct.unpack_from("<I" if wide else "<H", body, i)[0]
                i += 4 if wide else 2
                _type, i = _omf_index(body, i)
                if seg == 0 or seg >= len(segs):
                    raise PadError("%s: symbol %s in unknown segment %d"
                                   % (label, name, seg))
                symbols.append((segs[seg], name, value, rt >= 0xB0))
        elif rt in (0x8A, 0x8B):                            # MODEND
            break
        off += 3 + rl
    return {"segments": seglen, "symbols": symbols}


# ------------------------------------------------------ declarations in src/

def _code_only(text):
    """The source with comments blanked and literal contents emptied.

    Offsets are not preserved; what matters is that a brace, semicolon or
    identifier inside a comment or a literal can no longer be read as code.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            out.append(" ")
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


DIM_RX = re.compile(r"\[\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*\]")


def declared_size(text, name):
    """Bytes the definition of `name` in this C source allocates, or None.

    Only a file-scope definition (not `extern`, not inside a body) counts, and
    exactly one must exist.  The type must be a scalar from SCALARS or a
    pointer; each array dimension must be an integer literal.  Anything else --
    a struct, a typedef, a macro dimension, `[]` -- is unknown, and an unknown
    size is never guessed: the gap after that symbol stays compared.
    """
    code = _code_only(text)
    found = []
    for m in re.finditer(r"(?<![\w$])%s(?![\w$])" % re.escape(name), code):
        tail = re.match(r"\s*((?:\[[^\]\[]*\]\s*)*)[=;]", code[m.end():])
        if not tail:
            continue
        head = code[:m.start()]
        if head.count("{") != head.count("}"):
            continue                                        # not file scope
        stmt = head[max(head.rfind(";"), head.rfind("{"), head.rfind("}")) + 1:]
        stmt = " ".join(l for l in stmt.splitlines()
                        if not l.lstrip().startswith("#"))
        spec = re.match(r"^\s*((?:(?:static|const|volatile)\s+)*)"
                        r"([A-Za-z_]\w*(?:\s+[A-Za-z_]\w*)*)\s*"
                        r"((?:\*\s*(?:const\s*)?)*)$", stmt)
        if not spec or "extern" in stmt.split():
            continue
        found.append((spec.group(2), bool(spec.group(3).strip()),
                      tail.group(1)))
    if len(found) != 1:
        return None
    type_words, pointer, dims = found[0]
    words = [w for w in type_words.split() if w not in ("const", "volatile")]
    if pointer:
        size = 4
    else:
        size = SCALARS.get(" ".join(words))
        if size is None:
            return None
    for raw in re.findall(r"\[[^\]]*\]", dims):
        m = DIM_RX.fullmatch(raw)
        if not m:
            return None
        size *= int(m.group(1), 0)
    return size


# ------------------------------------------------------------------ analysis

def address_taken(image, objects, site):
    """True when the relocation at `site` uses its target as an address value.

    A site inside a data object is a stored pointer.  In code the byte before
    the imm32 has to be the opcode of `push imm32` (68) or `mov r32,imm32`
    (B8-BF).  An x87 load of a CONST double is `D8-DF modrm disp32`, whose byte
    before the site is a ModRM, never one of these.  Any other encoding is not
    proof, and an item without proof keeps its bytes compared.
    """
    for obj in objects.values():
        if obj["start"] <= site < obj["end"]:
            if not obj["flags"] & EXEC_FLAG:
                return True
            prev = image[site - 1]
            return prev == 0x68 or 0xB8 <= prev <= 0xBF
    return False


def analyse(image, map_text, objs, sources):
    """The provable alignment gaps of one linked image.

    `objs` maps an object file's upper-case basename to its bytes, `sources`
    maps a source file's upper-case basename to its text -- both from the build
    that produced `image`.  Returns a dict: `ranges` [(file offset, length)],
    `literal_gaps`, `symbol_gaps`, `bytes`, `unsized` (symbols of a compiled
    module whose size is unknown), `unplaced` (objects whose statics could not
    be placed), `unproved` (CONST targets not proved to be strings).
    """
    lay = parse_map(map_text)
    objects = le_objects(image)

    refs, covered = {}, set()
    for rec in lefixup.fixup_records(image):
        for off, width in rec["sites"]:
            covered.update(range(off, off + width))
            if rec["target"] is not None:
                refs.setdefault(rec["target"], []).append(off)

    named = set()                       # (obj, off) of every map or static symbol
    for mod in lay["modules"]:
        for obj, off, _ in mod["symbols"]:
            named.add((obj, off))
    bounds = set()
    for seg in lay["segments"]:
        bounds.add((seg["obj"], seg["off"]))
        bounds.add((seg["obj"], seg["off"] + seg["size"]))

    # Symbols of the compiled modules, statics placed through the publics.
    sized, unsized, unplaced = [], [], []
    for mod in lay["modules"]:
        data = objs.get(mod["file"])
        if data is None:
            continue
        obj = read_obj(data, mod["file"])
        text = sources.get(mod["source"])
        where = {name: (o, off) for o, off, name in mod["symbols"]}
        for segname in SYMBOL_SEGMENTS + LITERAL_SEGMENTS:
            syms = [s for s in obj["symbols"] if s[0] == segname]
            if not syms:
                continue
            bases = {(where[n][0], where[n][1] - off)
                     for _, n, off, local in syms if not local and n in where}
            if len(bases) != 1:
                if any(local for *_, local in syms):
                    unplaced.append("%s:%s" % (mod["file"], segname))
                continue
            o, base = bases.pop()
            for _, name, off, local in syms:
                named.add((o, base + off))
                if segname not in SYMBOL_SEGMENTS:
                    continue
                size = declared_size(text, name) if text is not None else None
                if size is None:
                    unsized.append(name)
                else:
                    sized.append((o, base + off, size, name))

    starts = {}
    for o, off in named | bounds | set(refs):
        starts.setdefault(o, []).append(off)
    for o in starts:
        starts[o] = sorted(set(starts[o]))

    def next_start(o, after, inclusive=False):
        """The first start > after (>= after when inclusive), or None."""
        table = starts.get(o, [])
        k = (bisect.bisect_left if inclusive else bisect.bisect_right)(table, after)
        return table[k] if k < len(table) else None

    def between(o, lo, hi):
        """Starts strictly inside (lo, hi)."""
        table = starts.get(o, [])
        return table[bisect.bisect_right(table, lo):bisect.bisect_left(table, hi)]

    ranges, literal_gaps, symbol_gaps, unproved = [], 0, 0, 0

    def take(o, lo, hi):
        obj = objects.get(o)
        if obj is None:
            return False
        a, b = obj["start"] + lo, obj["start"] + hi
        # Defence in depth: the start rules above already exclude a gap that
        # holds a relocated pointer, but a relocation site is never padding.
        if b > obj["end"] or any(i in covered for i in range(a, b)):
            return False
        ranges.append((a, b - a))
        return True

    for seg in lay["segments"]:
        o, lo, hi = seg["obj"], seg["off"], seg["off"] + seg["size"]
        obj = objects.get(o)
        if obj is None or seg["name"] not in LITERAL_SEGMENTS:
            continue
        for t in sorted(off for (to, off) in refs if to == o and lo <= off < hi):
            if t % ALIGN:
                continue
            if not all(address_taken(image, objects, s) for s in refs[(o, t)]):
                unproved += 1
                continue
            nul = image.find(b"\0", obj["start"] + t, obj["start"] + hi)
            if nul < 0:
                continue
            end = nul - obj["start"] + 1                # one past the NUL
            gap_end = align(end, ALIGN)
            if gap_end == end:
                continue
            inner = between(o, t, end)
            if any((o, s) in named or (o, s) not in refs
                   or not all(address_taken(image, objects, x)
                              for x in refs[(o, s)]) for s in inner):
                continue
            if next_start(o, end - 1) == gap_end and take(o, end, gap_end):
                literal_gaps += 1

    for o, s, size, name in sorted(sized):
        end = s + size
        if any((o, x) in named for x in between(o, s, end)):
            continue                                    # size disagrees with layout
        nxt = next_start(o, end, inclusive=True)
        if nxt is None or nxt == end:
            continue
        if nxt in (align(end, 2), align(end, ALIGN)) and take(o, end, nxt):
            symbol_gaps += 1

    ranges.sort()
    return {"ranges": ranges, "literal_gaps": literal_gaps,
            "symbol_gaps": symbol_gaps, "bytes": sum(l for _, l in ranges),
            "unsized": sorted(set(unsized)), "unplaced": unplaced,
            "unproved": unproved}


def analyse_build(exe, map_path, obj_dir, src_dir):
    """analyse() over a build's files on disk: the image, its map, the object
    directory and the staged source directory that was compiled."""
    objs = {p.name.upper(): p.read_bytes() for p in Path(obj_dir).iterdir()
            if p.suffix.upper() == ".OBJ"}
    sources = {p.name.upper(): p.read_text(encoding="latin-1")
               for p in Path(src_dir).iterdir()
               if p.suffix.upper() in (".C", ".H")}
    return analyse(Path(exe).read_bytes(),
                   Path(map_path).read_text(encoding="latin-1"), objs, sources)


# ------------------------------------------------------------------ selftest
#
# One small synthetic build -- an LE image with a code and a data object, its
# wlink map, the OMF object of the data module and that module's source -- in
# which every kind of byte the gate has to tell apart sits at a known place:
#
#   CONST   04 "Cusor.cel\0" + 2 gap   (mov eax, imm32)
#           10 "ok\0"        + 1 gap   (push imm32)
#           14 double 41 42 43 44 00 01 f0 3f   (fld qword ptr) -- its first
#              NUL is followed by bytes up to a 4-byte boundary, exactly the
#              shape of a literal gap, and it must not be read as one
#           1c "x\0"         + 2 gap   (mov eax, imm32; a pointer in _DATA)
#           20 "abc\0"       no gap
#   _DATA   24 data_flag  unsigned char
#           25 data_mode  unsigned char    + 2 gap before the static
#           28 align_below static int      (only the object file names it)
#           2c data_table int[2]
#           34 data_ptr   char *           (a relocation site)
#           38 data_blob  struct blob      + 3 bytes after it, unsized
#           3c data_last  int

PAGE = 0x100
CODE_BASE = 0x10000
DATA_BASE = 0x20000
DATA_FILE = 0x500                       # file offset of the data object

GAMEDATA_C = """#include "gamedata.h"

/* Looks like code, is a comment: int data_last[9]; { */
unsigned char data_flag = 1;
%s data_mode = 2;
static int align_below = 0;
int data_table[2] = { 3, 4 };
char *data_ptr = "x";
struct blob data_blob = { 5 };
int data_last = 6;

int read_flag(void)
{
    unsigned short data_flag = 0;
    return data_flag;
}
"""

MAP = r"""WATCOM Linker Version 10.0
                        +--------------+
                        |   Segments   |
                        +--------------+

Segment                Class          Group          Address         Size
=======                =====          =====          =======         ====

_TEXT                  CODE                          0001:00000000   00000020
_NULL                  BEGDATA        DGROUP         0002:00000000   00000004
CONST                  DATA           DGROUP         0002:00000004   00000020
CONST2                 DATA           DGROUP         0002:00000024   00000000
_DATA                  DATA           DGROUP         0002:00000024   0000001c
_BSS                   BSS            DGROUP         0002:00000040   00000010

                        +----------------+
                        |   Memory Map   |
                        +----------------+

* = unreferenced symbol
+ = symbol only referenced locally

Address        Symbol
=======        ======

Module: F:\OUT\OBJ\MAIN.OBJ(C:\SRC\MAIN.C)
0001:00000000  read_flag
Module: F:\OUT\OBJ\GAMEDATA.OBJ(C:\SRC\GAMEDATA.C)
0002:00000024  data_flag
0002:00000025* data_mode
0002:0000002c  data_table
0002:00000034+ data_ptr
0002:00000038  data_blob
0002:0000003c  data_last

                        +--------------------+
                        |   Libraries Used   |
                        +--------------------+

D:\LIB386\DOS\CLIB3S.LIB
"""


def _omf(rt, body):
    rec = bytes([rt]) + struct.pack("<H", len(body) + 1) + body
    return rec + bytes([-sum(rec) & 0xFF])


def _synth_obj(static=True):
    """GAMEDATA.OBJ: the segment list, the publics and the one static."""
    def names(*items):
        return b"".join(bytes([len(n)]) + n.encode() for n in items)

    recs = [_omf(0x80, names("GAMEDATA.C")),
            _omf(0x96, names("_TEXT", "CODE", "CONST", "DATA", "CONST2",
                             "_DATA", "_BSS", "BSS"))]
    for name_idx, class_idx, length in ((1, 2, 0), (3, 4, 0), (5, 4, 0),
                                        (6, 4, 0x1c), (7, 8, 0)):
        recs.append(_omf(0x99, bytes([0xA9]) + struct.pack("<I", length)
                         + bytes([name_idx, class_idx, 1])))

    def pubdef(rt, entries):
        body = bytes([0, 4])                            # no group, segment _DATA
        for name, off in entries:
            body += names(name) + struct.pack("<I", off) + b"\0"
        return _omf(rt, body)

    recs.append(pubdef(0x91, [("data_flag", 0), ("data_mode", 1),
                              ("data_table", 8), ("data_ptr", 0x10),
                              ("data_blob", 0x14), ("data_last", 0x18)]))
    if static:
        recs.append(pubdef(0xB7, [("align_below", 4)]))
    recs.append(_omf(0x8A, b"\0"))
    return b"".join(recs)


def _synth_build(gap=b"\xa5\x5a\x3c\xc3\x96\x69\x0f", ok=b"ok", cusor=b"Cusor.cel",
                 double=b"\x41\x42\x43\x44\x00\x01\xf0\x3f", table1=4, ret=0xC3,
                 blob_gap=b"\x11\x22\x33", mode_type="unsigned char",
                 mode_bytes=None, ptr_target=0x1c):
    """(image, map text, objects, sources) for one variant of the build."""
    code = bytearray(b"\x90" * 0x20)
    relocs = []                                         # (page, offset, target)

    def ref(at, opcode, target):
        code[at:at + len(opcode)] = opcode
        site = at + len(opcode)
        code[site:site + 4] = struct.pack("<I", DATA_BASE + target)
        relocs.append((1, site, target))

    ref(0x00, b"\xb8", 0x04)            # mov eax, offset "Cusor.cel"
    ref(0x05, b"\x68", 0x10)            # push offset "ok"
    ref(0x0a, b"\xdd\x05", 0x14)        # fld qword ptr [double]
    ref(0x10, b"\xb8", 0x20)            # mov eax, offset "abc"
    ref(0x15, b"\xa1", 0x24)            # mov eax, [data_flag]
    code[0x1a] = ret
    ref(0x1b, b"\xb8", 0x1c)            # mov eax, offset "x"

    data = bytearray(0x50)
    lit = cusor + b"\0"
    data[0x04:0x04 + len(lit)] = lit
    data[0x04 + len(lit):0x10] = gap[0:0x0c - len(lit)]
    data[0x10:0x13] = ok + b"\0"
    data[0x13] = gap[2]
    data[0x14:0x1c] = double
    data[0x1c:0x1e] = b"x\0"
    data[0x1e:0x20] = gap[3:5]
    data[0x20:0x24] = b"abc\0"
    data[0x24] = 1
    data[0x25:0x28] = mode_bytes or (b"\x02" + gap[5:7])
    data[0x2c:0x34] = struct.pack("<2i", 3, table1)
    data[0x34:0x38] = struct.pack("<I", DATA_BASE + ptr_target)
    relocs.append((2, 0x34, ptr_target))
    data[0x38] = 5
    data[0x39:0x3c] = blob_gap
    data[0x3c:0x40] = struct.pack("<i", 6)

    le, hdr = 0x40, 0xB0
    obj_tbl, page_map = hdr, hdr + 48
    fpt = page_map + 8
    recs = []
    for page in (1, 2):
        recs.append(b"".join(bytes([0x07, 0x10]) + struct.pack("<h", off)
                             + bytes([2]) + struct.pack("<I", target)
                             for p, off, target in relocs if p == page))
    frt = fpt + 12
    imt = frt + sum(len(r) for r in recs)
    image = bytearray(DATA_FILE + len(data))
    image[0:2] = b"MZ"
    image[0x3C:0x40] = struct.pack("<I", le)
    image[le:le + 2] = b"LE"
    for off, val in ((0x14, 2), (0x28, PAGE), (0x2C, len(data)), (0x40, obj_tbl),
                     (0x44, 2), (0x48, page_map), (0x68, fpt), (0x6C, frt),
                     (0x70, imt), (0x80, 0x400)):
        struct.pack_into("<I", image, le + off, val)
    struct.pack_into("<6I", image, le + obj_tbl, 0x20, CODE_BASE, 0x2005, 1, 1, 0)
    struct.pack_into("<6I", image, le + obj_tbl + 24, 0x50, DATA_BASE, 0x2003, 2, 1, 0)
    struct.pack_into("<3I", image, le + fpt, 0, len(recs[0]), len(recs[0]) + len(recs[1]))
    image[le + frt:le + imt] = recs[0] + recs[1]
    image[0x400:0x400 + len(code)] = code
    image[DATA_FILE:] = data
    return (bytes(image), MAP, {"GAMEDATA.OBJ": _synth_obj()},
            {"GAMEDATA.C": GAMEDATA_C % mode_type})


def _verdict(base_build, got_build, base_pad=True):
    base = analyse(*base_build)["ranges"] if base_pad else None
    got = analyse(*got_build)["ranges"]
    return lefixup.compare(lefixup.profile(base_build[0], pad=base),
                           lefixup.profile(got_build[0], pad=got))[0]


def _selftest_rows():
    rows = []

    for text, name, want in (
            ("unsigned char x = 1;", "x", 1),
            ("static unsigned char t[3] = { 0, 0, 0 };", "t", 3),
            ("void *p = 0;", "p", 4),
            ("char *names[0x10];", "names", 64),
            ("short s;", "s", 2),
            ("#define N 4\nint a[N];", "a", None),
            ("int u[] = { 1, 2 };", "u", None),
            ("extern int e;", "e", None),
            ("int d = 1;\nint d = 2;", "d", None),
            ("struct s v;", "v", None),
            ("int f(void)\n{\n    int loc = 1;\n    return loc;\n}\n", "loc", None),
            ("/* int c; */ const char *c = \"; int c;\";", "c", 4)):
        got = declared_size(text, name)
        rows.append(("declared size of %-24s" % text.splitlines()[-1][:24],
                     got == want, "%s (want %s)" % (got, want)))

    base = _synth_build()
    rep = analyse(*base)
    want = [(DATA_FILE + 0x0e, 2), (DATA_FILE + 0x13, 1), (DATA_FILE + 0x1e, 2),
            (DATA_FILE + 0x26, 2)]
    rows.append(("gaps are exactly the four built in", rep["ranges"] == want,
                 ", ".join("%x+%d" % r for r in rep["ranges"])))
    rows.append(("the double is not read as a literal",
                 rep["unproved"] == 1 and all(not (DATA_FILE + 0x14 <= a < DATA_FILE + 0x1c)
                                              for a, _ in rep["ranges"]),
                 "%d unproved" % rep["unproved"]))
    rows.append(("an unsized symbol's gap stays compared",
                 rep["unsized"] == ["data_blob"], ", ".join(rep["unsized"])))
    no_obj = analyse(base[0], base[1], {}, base[3])
    rows.append(("without the object the static is unknown and its gap kept",
                 (DATA_FILE + 0x26, 2) not in no_obj["ranges"]
                 and len(no_obj["ranges"]) == 3,
                 "%d gaps" % len(no_obj["ranges"])))
    # data_mode ends at 26 and the next start this object names is 2c: six
    # bytes, more than any alignment, so they are not a gap.
    no_static = analyse(base[0], base[1], {"GAMEDATA.OBJ": _synth_obj(static=False)},
                        base[3])
    rows.append(("a gap wider than the alignment is not a gap",
                 len(no_static["ranges"]) == 3
                 and all(not (DATA_FILE + 0x26 <= a < DATA_FILE + 0x2c)
                         for a, _ in no_static["ranges"]),
                 "%d gaps" % len(no_static["ranges"])))
    inside = analyse(*_synth_build(ptr_target=0x1e))
    rows.append(("a referenced byte after a NUL is not a gap",
                 (DATA_FILE + 0x1e, 2) not in inside["ranges"]
                 and len(inside["ranges"]) == 3,
                 "%d gaps" % len(inside["ranges"])))

    other_gap = b"\x01\x02\x03\x04\x05\x06\x07"
    cases = [
        ("the same build is IDENTICAL", _synth_build(), "identical"),
        ("an edit that moves only gap bytes is PAD",
         _synth_build(gap=other_gap), "pad"),
        ("a changed string is DIFFERENT", _synth_build(ok=b"oK", gap=other_gap),
         "different"),
        ("a shortened string is DIFFERENT",
         _synth_build(cusor=b"Cusor.ce", gap=other_gap), "different"),
        ("a changed constant is DIFFERENT",
         _synth_build(table1=5, gap=other_gap), "different"),
        ("a changed code byte is DIFFERENT", _synth_build(ret=0xC2, gap=other_gap),
         "different"),
        ("a changed double exponent is DIFFERENT",
         _synth_build(double=b"\x41\x42\x43\x44\x00\x01\xf8\x3f"), "different"),
        ("a byte after an unsized symbol is DIFFERENT",
         _synth_build(blob_gap=b"\x44\x55\x66"), "different"),
        ("a widened declaration moves the gap: DIFFERENT",
         _synth_build(mode_type="unsigned short", mode_bytes=b"\x02\x00\x69",
                      gap=other_gap), "different"),
    ]
    for name, build, want in cases:
        got = _verdict(base, build)
        rows.append((name, got == want, got))
    got = _verdict(base, _synth_build(gap=other_gap), base_pad=False)
    rows.append(("a baseline without the gap fingerprint never says PAD",
                 got == "different", got))

    bad = bytearray(_synth_obj())
    del bad[-6:]
    try:
        read_obj(bytes(bad) + b"\x8c\xff", "cut.obj")
        rows.append(("a truncated object raises", False, "no error"))
    except PadError as exc:
        rows.append(("a truncated object raises", True, str(exc)[:40]))
    return rows


def main(argv):
    if argv[1:] != ["selftest"]:
        print(__doc__)
        return 2
    ok = True
    for name, passed, detail in _selftest_rows():
        print("[selftest] %-58s %s (%s)" % (name, "ok" if passed else "FAIL", detail))
        ok = ok and passed
    print("[result] %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
