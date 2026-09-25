"""lefixup.py -- LE relocation parsing and the relocation-aware image profile.

A SHA-256 over the whole executable answers "did the build output change".  It
cannot answer "did the build output change in a way that matters", and for a
Watcom/wlink DOS/4G image those are different questions: some edits to `src/`
are behaviour-neutral yet still move bytes.  Two such effects are known from the
FD2 project and both apply here unchanged, because the same linker lays out the
same container:

  1. FIXUP REORDER.  wlink emits the LE fixup records in an order that depends
     on symbol names, so renaming a symbol permutes the bytes of the Fixup
     Record Table without changing the set of relocations.

  2. TENTATIVE-DEFINITION RELOCATION.  Uninitialized globals are Watcom COMDEF
     (tentative) symbols and wlink places them in name order.  Renaming one can
     shift it and its neighbours by a few bytes, which changes the value stored
     at every fixup SITE that targets a moved symbol and the target field of the
     matching record.  The loaded image still behaves identically.

This module turns an image into a profile that is blind to exactly those two
effects and to nothing else:

    outside_sha256          sha256 of everything except the Fixup Record Table
    fixup_multiset_sha256   sha256 of the table's bytes, sorted
    residual_sha256         sha256 of the image with every fixup site value AND
                            the whole Fixup Record Table zeroed

STRICT equivalence is `outside` + `multiset` equal (effect 1 only).  RELOC
equivalence is `residual` equal (effects 1 and 2).  Any real code or data edit
lands outside every fixup site and breaks the residual.

**What RELOC cannot see.**  It zeroes the fixup sites and the record table
before comparing, so changing which symbol a fixup points at is invisible to it:
that edit touches only the site's displacement and the record's target field.  A
relocation-aware PASS therefore means "no new code/data bytes", not "every fixup
still points where it did".  See rebuild_info/pitfalls.md.

Every table bound and every site is parsed live from the header; nothing is
hardcoded, so the profile stays correct as the image grows.
"""
import hashlib
import struct

# LE fixup source type (low nibble of the source byte) -> how many bytes the
# loader patches at the site.  Taken from the LE/LX specification rather than
# from what this binary happens to use: a type masked with the wrong width
# silently compares the wrong bytes, which is the one failure a gate must not
# have.  Unknown types raise instead of defaulting to a guess.
SRC_SIZE = {
    0x00: 1,   # byte
    0x02: 2,   # 16-bit selector
    0x03: 4,   # 16:16 pointer
    0x05: 2,   # 16-bit offset
    0x06: 6,   # 16:32 pointer
    0x07: 4,   # 32-bit offset
    0x08: 4,   # 32-bit self-relative offset
}

# The selector fixup needs only the object number, so its record carries no
# target offset field.
NO_TARGET_OFFSET = {0x02}


class LeError(Exception):
    """The image is not the LE shape this module knows how to read."""


def sha(data):
    return hashlib.sha256(data).hexdigest()


def le_base(data):
    """File offset of the LE header: 0 for a bare module, e_lfanew behind a stub."""
    if data[:2] == b"LE":
        return 0
    if data[:2] == b"MZ":
        if len(data) < 0x40:
            raise LeError("MZ file is %d bytes, too short to hold e_lfanew"
                          % len(data))
        off = struct.unpack_from("<I", data, 0x3C)[0]
        if data[off:off + 2] == b"LE":
            return off
        raise LeError("MZ stub does not point at an LE header (found %r at 0x%x)"
                      % (data[off:off + 2], off))
    raise LeError("not an LE executable (starts with %r)" % data[:2])


def header(data):
    base = le_base(data)

    def u32(off):
        return struct.unpack_from("<I", data, base + off)[0]

    hdr = {
        "base": base,
        "num_pages": u32(0x14),
        "page_size": u32(0x28),
        "fixup_page_tbl": base + u32(0x68),
        "fixup_rec_tbl": base + u32(0x6C),
        # The import module table starts where the fixup records end.
        "import_mod_tbl": base + u32(0x70),
        # Absolute from the start of the file, not from the LE header: for
        # SMOKE.EXE it is 0x4000 while the header sits at 0x2a50, and only the
        # absolute reading makes the last page end exactly at the file size.
        "data_pages_off": u32(0x80),
    }
    lo, hi = hdr["fixup_rec_tbl"], hdr["import_mod_tbl"]
    if not (base < lo <= hi <= len(data)):
        raise LeError("implausible fixup table bounds: 0x%x..0x%x in a %d byte file"
                      % (lo, hi, len(data)))
    return hdr


def fixup_bounds(data):
    """(lo, hi) file offsets of the LE Fixup Record Table."""
    hdr = header(data)
    return hdr["fixup_rec_tbl"], hdr["import_mod_tbl"]


def fixup_records(data):
    """Walk the Fixup Record Table; yield one dict per record.

    Keys: page, start, end (file offsets of the record itself), sites (list of
    (file_offset, width) it relocates), target ((object, offset) of an
    internal reference, else None).

    The table is a stream of variable-length records with no length prefix, so
    each record's target data has to be skipped exactly or the next record is
    read from the middle of this one.  The per-page ranges in the Fixup Page
    Table are the check on that: a page whose records do not end precisely on
    its range boundary means the walk desynchronised, and that raises rather
    than returning a plausible-looking wrong answer.

    That check is necessary but not sufficient, which is why the record layout
    below follows the spec field by field.  A source list is
    `SRC, FLAGS, CNT, OBJECT, TRGOFF, [ADDITIVE], SRCOFF1..n` -- the offsets
    come *after* the target data, not straight after the count.  Reading them in
    the wrong place still consumes the same number of bytes, so the page-end
    check passes while every reported site is garbage.
    """
    hdr = header(data)
    page_size = hdr["page_size"]
    fpt, frt = hdr["fixup_page_tbl"], hdr["fixup_rec_tbl"]
    data_off, pages = hdr["data_pages_off"], hdr["num_pages"]

    table = [struct.unpack_from("<I", data, fpt + 4 * i)[0]
             for i in range(pages + 1)]
    for page in range(1, pages + 1):
        pos, end = frt + table[page - 1], frt + table[page]
        if not (frt <= pos <= end <= hdr["import_mod_tbl"]):
            raise LeError("fixup page table entry %d out of range: 0x%x..0x%x"
                          % (page, pos, end))
        page_file_off = data_off + (page - 1) * page_size
        while pos < end:
            start = pos
            src, trg = data[pos], data[pos + 1]
            pos += 2
            stype = src & 0x0F
            if stype not in SRC_SIZE:
                raise LeError("unknown fixup source type 0x%02x at 0x%x"
                              % (stype, start))
            source_list = bool(src & 0x20)
            if source_list:
                count = data[pos]
                pos += 1
                offsets = None                              # they come last
            else:
                offsets = [struct.unpack_from("<h", data, pos)[0]]
                pos += 2

            ttype = trg & 0x03
            target = None
            if ttype == 0:                                  # internal reference
                wide_obj = bool(trg & 0x40)
                obj = (struct.unpack_from("<H", data, pos)[0] if wide_obj
                       else data[pos])
                pos += 2 if wide_obj else 1                 # object number
                if stype not in NO_TARGET_OFFSET:
                    wide_off = bool(trg & 0x10)
                    target = (obj, struct.unpack_from(
                        "<I" if wide_off else "<H", data, pos)[0])
                    pos += 4 if wide_off else 2             # target offset
            elif ttype == 1:                                # import by ordinal
                pos += 2 if (trg & 0x40) else 1             # module ordinal
                # 8-bit ordinal flag wins, then the 32-bit target offset flag,
                # otherwise 16 bits.
                pos += 1 if (trg & 0x80) else (4 if (trg & 0x10) else 2)
            elif ttype == 2:                                # import by name
                pos += 2 if (trg & 0x40) else 1
                pos += 4 if (trg & 0x10) else 2
            else:                                           # via entry table
                pos += 2 if (trg & 0x40) else 1
            if trg & 0x04:                                  # additive fixup
                pos += 4 if (trg & 0x20) else 2
            if source_list:
                offsets = [struct.unpack_from("<h", data, pos + 2 * i)[0]
                           for i in range(count)]
                pos += 2 * count
            if pos > end:
                raise LeError("fixup record at 0x%x overruns page %d (ends 0x%x > 0x%x)"
                              % (start, page, pos, end))

            width = SRC_SIZE[stype]
            yield {
                "page": page,
                "start": start,
                "end": pos,
                "sites": [(page_file_off + off, width) for off in offsets
                          if width],
                # (object number, offset) the record points at, for an
                # internal reference; None for imports and selector fixups.
                # lepad.py reads these as the starts of referenced data.
                "target": target,
            }
        if pos != end:
            raise LeError("fixup records for page %d ended at 0x%x, expected 0x%x"
                          % (page, pos, end))


def fixup_sites(data):
    """Every relocation site as (file_offset, length)."""
    sites = []
    for rec in fixup_records(data):
        sites.extend(rec["sites"])
    return sites


def blank(data):
    """The image with every fixup site value and the whole record table zeroed."""
    out = bytearray(data)
    lo, hi = fixup_bounds(data)
    for i in range(lo, hi):
        out[i] = 0
    for off, width in fixup_sites(data):
        for i in range(off, off + width):
            # wlink can place a relocation whose last bytes fall past the file
            # end: the final page is truncated to last_page_size and the loader
            # zero-fills the rest, so a site there is only partly on disk.  It
            # happens in the shipped FDPS.EXE and in every build here, so clamp
            # rather than treating it as corruption.
            if 0 <= i < len(out):
                out[i] = 0
    return bytes(out)


def pad_mask_digest(pad):
    """sha256 of the gap positions themselves, so a moved gap is a difference."""
    return sha(";".join("%x+%x" % r for r in sorted(pad)).encode("ascii"))


def profile(data, pad=None):
    """The relocation-aware fingerprint of one image.

    `pad` is the list of (file offset, length) alignment gaps lepad.py proved
    for this image.  When given, the profile also carries the gaps' positions
    and the residual with those bytes zeroed as well, which is what the `pad`
    verdict compares.
    """
    lo, hi = fixup_bounds(data)
    table = data[lo:hi]
    out = {
        "size": len(data),
        "sha256": sha(data),
        "fixup_lo": lo,
        "fixup_hi": hi,
        "fixup_len": len(table),
        "fixup_sites": len(fixup_sites(data)),
        "outside_sha256": sha(data[:lo] + data[hi:]),
        "fixup_multiset_sha256": sha(bytes(sorted(table))),
        "residual_sha256": sha(blank(data)),
    }
    if pad is not None:
        blanked = bytearray(blank(data))
        for off, length in pad:
            blanked[off:off + length] = bytes(length)
        out["pad_bytes"] = sum(length for _, length in pad)
        out["pad_mask_sha256"] = pad_mask_digest(pad)
        out["pad_residual_sha256"] = sha(bytes(blanked))
    return out


# Verdicts, worst last.  `identical` and `size` are decided by their own field;
# the middle four are the relocation-aware tiers, `pad` adding the alignment
# gaps wcc386 leaves uncleared (lepad.py).
VERDICTS = ("identical", "strict", "reloc", "pad", "different", "size")
PASSING = ("identical", "strict", "reloc", "pad")


def compare(base, got):
    """Classify profile `got` against baseline profile `base`.

    Returns (verdict, detail):
        identical   the same bytes
        strict      differs only by a permutation of the fixup record table
        reloc       differs only by relocation values plus that permutation
        pad         additionally differs inside alignment gaps, which sit at
                    the same positions in both images
        different   a code or data byte outside every fixup site and every
                    alignment gap changed, or the gaps themselves moved
        size        different sizes, so nothing finer is comparable

    `pad` needs the gap fingerprint on both sides; a profile taken without it
    (a target with no map, or a baseline recorded before it existed) can never
    read as `pad`.
    """
    detail = {
        "size_equal": got["size"] == base["size"],
        "sha_equal": got["sha256"] == base["sha256"],
        "outside_equal": got["outside_sha256"] == base["outside_sha256"],
        "multiset_equal":
            got["fixup_multiset_sha256"] == base["fixup_multiset_sha256"],
        "residual_equal": got["residual_sha256"] == base["residual_sha256"],
    }
    if detail["sha_equal"]:
        return "identical", detail
    if not detail["size_equal"]:
        return "size", detail
    if detail["outside_equal"] and detail["multiset_equal"]:
        return "strict", detail
    if detail["residual_equal"]:
        return "reloc", detail
    if "pad_residual_sha256" in base and "pad_residual_sha256" in got:
        detail["pad_mask_equal"] = got["pad_mask_sha256"] == base["pad_mask_sha256"]
        detail["pad_residual_equal"] = (got["pad_residual_sha256"]
                                        == base["pad_residual_sha256"])
        if detail["pad_mask_equal"] and detail["pad_residual_equal"]:
            return "pad", detail
    return "different", detail
