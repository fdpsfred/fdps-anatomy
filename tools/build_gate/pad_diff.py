"""Tell whether two builds of the same LE image differ only in alignment fill.

wcc386 pads data out to alignment boundaries without zeroing the pad: the 1-3
bytes after a string literal's terminating NUL in the CONST pool, and the gap
after a small initialised object in _DATA, hold whatever was left in the
compiler's buffer, and that changes with the source text around it -- a
comment edit is enough.  No code reads those bytes, so a difference confined
to them is behaviour-neutral, but the gate's relocation-aware comparison sees
a data byte outside every relocation and reports `different`
(rebuild_info/build_gate.md).

    python tools/build_gate/pad_diff.py <old .EXE> <new .EXE> [<new .MAP>]

A differing byte is classified automatically as literal padding when both
images hold a NUL within the three bytes before it and the byte still lies
before the 4-byte boundary that follows that NUL.  Every other differing run is
listed with its object-relative offset and, when the wlink map is given, the
map symbols on either side of it, so the gap after a small object can be
checked by eye against the symbol's declared size in src/.

Exit status 0: every differing byte is literal padding.  1: some runs need the
manual check above.  2: bad arguments or the files differ in size.
"""
import bisect
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lefixup  # noqa: E402  (same folder; owns the LE header parsing)


def is_literal_padding(a, b, i):
    """True when byte i follows a NUL shared by both images, before the next 4-byte boundary."""
    for k in range(i - 1, max(i - 4, -1), -1):
        if a[k] == 0 and b[k] == 0:
            return i < ((k + 1 + 3) // 4) * 4
    return False


def le_objects(data):
    """(number, linear base, file start, file end) of every LE object."""
    le = lefixup.le_base(data)
    page = struct.unpack_from("<I", data, le + 0x28)[0]
    table = le + struct.unpack_from("<I", data, le + 0x40)[0]
    count = struct.unpack_from("<I", data, le + 0x44)[0]
    pages = struct.unpack_from("<I", data, le + 0x80)[0]
    out = []
    for n in range(count):
        _size, base, _flags, first, npages, _ = struct.unpack_from("<6I", data, table + 24 * n)
        start = pages + (first - 1) * page
        out.append((n + 1, base, start, start + npages * page))
    return out


def map_symbols(path):
    syms = {}
    for line in open(path, encoding="latin-1"):
        m = re.match(r"(\d{4}):([0-9a-f]{8})\*?\s+(\S+)", line)
        if m:
            syms.setdefault(int(m.group(1)), []).append((int(m.group(2), 16), m.group(3)))
    return {k: sorted(v) for k, v in syms.items()}


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__, file=sys.stderr)
        return 2
    a = open(argv[1], "rb").read()
    b = open(argv[2], "rb").read()
    if len(a) != len(b):
        print("size differs: %d vs %d" % (len(a), len(b)))
        return 2
    syms = map_symbols(argv[3]) if len(argv) == 4 else {}
    le_objs = le_objects(b)
    diff = [i for i in range(len(a)) if a[i] != b[i]]
    unexplained = [i for i in diff if not is_literal_padding(a, b, i)]
    print("differing bytes: %d, not literal padding: %d" % (len(diff), len(unexplained)))
    runs = []
    for i in unexplained:
        if runs and i == runs[-1][1] + 1:
            runs[-1][1] = i
        else:
            runs.append([i, i])
    for s, e in runs:
        where = "file 0x%x..0x%x" % (s, e)
        for n, _base, start, end in le_objs:
            if start <= s < end:
                off = s - start
                where += "  obj %d +0x%x" % (n, off)
                table = syms.get(n, [])
                j = bisect.bisect_right(table, (off, "\xff")) - 1
                if j >= 0:
                    where += "  after %s (+0x%x)" % (table[j][1], table[j][0])
                if j + 1 < len(table):
                    where += "  before %s (+0x%x)" % (table[j + 1][1], table[j + 1][0])
        print("  " + where + "  old " + a[s:e + 1].hex() + "  new " + b[s:e + 1].hex())
    return 1 if unexplained else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
