"""Rebuild a Watcom .obj module's segment images and its fixup mask.

The version sweep compares a linked function body against the library module it
came from.  Two things stand in the way of a plain byte comparison, and both
live in the .obj rather than in the image:

  * the code arrives split across LEDATA records, so the bytes are not
    contiguous in the file, and
  * every field the linker patches still holds a placeholder, so those bytes
    differ from the image even when the code is identical.

This module walks the records once and produces, per segment, the assembled
image plus a boolean mask marking every byte a FIXUPP record claims.  The
comparison then ignores exactly those bytes and nothing else — which is what
makes an inequality elsewhere real evidence of a different build.

`wlib` does the library extraction (see rebuild_info/pitfalls.md); this reads
what it wrote and never opens a `.lib` itself.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

REC_THEADR = 0x80
REC_COMENT = 0x88
REC_MODEND_16 = 0x8A
REC_MODEND_32 = 0x8B
REC_EXTDEF = 0x8C
REC_PUBDEF = 0x90
REC_PUBDEF32 = 0x91
REC_LNAMES = 0x96
REC_SEGDEF = 0x98
REC_SEGDEF32 = 0x99
REC_GRPDEF = 0x9A
REC_FIXUPP = 0x9C
REC_FIXUPP32 = 0x9D
REC_LEDATA = 0xA0
REC_LEDATA32 = 0xA1
REC_LIDATA = 0xA2
REC_LIDATA32 = 0xA3
REC_LPUBDEF = 0xB6
REC_LPUBDEF32 = 0xB7

# FIXUPP location code -> width in bytes of the patched field
LOC_WIDTH = {0: 1, 1: 2, 2: 2, 3: 4, 4: 1, 5: 2, 9: 4, 11: 6, 13: 4}


@dataclass
class Segment:
    name: str
    image: bytearray = field(default_factory=bytearray)
    mask: bytearray = field(default_factory=bytearray)   # 1 = linker-patched


@dataclass
class Module:
    path: Path
    name: str
    segments: list[Segment] = field(default_factory=list)
    publics: list[tuple[str, int, int]] = field(default_factory=list)  # name, seg_idx, offset
    warnings: list[str] = field(default_factory=list)

    def segment(self, name: str) -> Segment | None:
        for seg in self.segments:
            if seg.name == name:
                return seg
        return None


def _index(data: bytes, off: int) -> tuple[int, int]:
    """Read an OMF index field; returns (value, bytes consumed)."""
    b = data[off]
    if b & 0x80:
        return ((b & 0x7F) << 8) | data[off + 1], 2
    return b, 1


def _names(data: bytes) -> list[str]:
    out, off = [], 0
    while off < len(data):
        n = data[off]
        out.append(data[off + 1: off + 1 + n].decode("latin-1"))
        off += 1 + n
    return out


def _grow(seg: Segment, end: int) -> None:
    if len(seg.image) < end:
        pad = end - len(seg.image)
        seg.image.extend(b"\x00" * pad)
        seg.mask.extend(b"\x00" * pad)


def _expand_lidata(data: bytes, off: int, is32: bool) -> tuple[bytes, int]:
    """Expand one iterated-data block; returns (bytes, new offset)."""
    if is32:
        count = int.from_bytes(data[off:off + 4], "little")
        off += 4
    else:
        count = int.from_bytes(data[off:off + 2], "little")
        off += 2
    nblocks = int.from_bytes(data[off:off + 2], "little")
    off += 2
    if nblocks == 0:
        n = data[off]
        off += 1
        chunk = data[off:off + n]
        off += n
        return chunk * count, off
    body = b""
    for _ in range(nblocks):
        piece, off = _expand_lidata(data, off, is32)
        body += piece
    return body * count, off


def read_module(path: Path) -> Module:
    """Parse one extracted .obj into segment images plus fixup masks."""
    data = path.read_bytes()
    mod = Module(path=path, name=path.stem)
    lnames: list[str] = [""]          # OMF name indices are 1-based
    segs: list[Segment | None] = [None]
    last_seg: Segment | None = None
    last_off = 0
    last_len = 0

    off = 0
    while off + 3 <= len(data):
        rt = data[off]
        rlen = data[off + 1] | (data[off + 2] << 8)
        body = data[off + 3: off + 3 + rlen - 1]     # drop the checksum byte
        if off + 3 + rlen > len(data):
            mod.warnings.append("truncated record at 0x%x" % off)
            break
        nxt = off + 3 + rlen

        if rt == REC_THEADR:
            if body:
                mod.name = body[1:1 + body[0]].decode("latin-1")
        elif rt == REC_LNAMES:
            lnames.extend(_names(body))
        elif rt in (REC_SEGDEF, REC_SEGDEF32):
            is32 = rt == REC_SEGDEF32
            p = 0
            acbp = body[p]
            p += 1
            if (acbp >> 5) & 0x07 == 0:              # absolute: frame + offset
                p += 3
            if (acbp & 0x02) == 0:                   # big bit clear -> length field
                p += 4 if is32 else 2
            else:
                p += 4 if is32 else 2
            name_idx, n = _index(body, p)
            p += n
            name = lnames[name_idx] if name_idx < len(lnames) else "SEG%d" % name_idx
            segs.append(Segment(name=name))
            mod.segments.append(segs[-1])
        elif rt in (REC_LEDATA, REC_LEDATA32):
            is32 = rt == REC_LEDATA32
            seg_idx, n = _index(body, 0)
            p = n
            if is32:
                doff = int.from_bytes(body[p:p + 4], "little")
                p += 4
            else:
                doff = int.from_bytes(body[p:p + 2], "little")
                p += 2
            chunk = body[p:]
            seg = segs[seg_idx] if 0 < seg_idx < len(segs) else None
            if seg is None:
                mod.warnings.append("LEDATA for unknown segment %d" % seg_idx)
            else:
                _grow(seg, doff + len(chunk))
                seg.image[doff:doff + len(chunk)] = chunk
            last_seg, last_off, last_len = seg, doff, len(chunk)
        elif rt in (REC_LIDATA, REC_LIDATA32):
            is32 = rt == REC_LIDATA32
            seg_idx, n = _index(body, 0)
            p = n
            if is32:
                doff = int.from_bytes(body[p:p + 4], "little")
                p += 4
            else:
                doff = int.from_bytes(body[p:p + 2], "little")
                p += 2
            try:
                expanded = b""
                while p < len(body):
                    piece, p = _expand_lidata(body, p, is32)
                    expanded += piece
            except (IndexError, ValueError):
                mod.warnings.append("LIDATA expansion failed at 0x%x" % off)
                expanded = b""
            seg = segs[seg_idx] if 0 < seg_idx < len(segs) else None
            if seg is not None and expanded:
                _grow(seg, doff + len(expanded))
                seg.image[doff:doff + len(expanded)] = expanded
            last_seg, last_off, last_len = seg, doff, len(expanded)
        elif rt in (REC_FIXUPP, REC_FIXUPP32):
            _apply_fixups(mod, body, rt == REC_FIXUPP32, last_seg, last_off, last_len)
        elif rt in (REC_PUBDEF, REC_PUBDEF32, REC_LPUBDEF, REC_LPUBDEF32):
            is32 = rt in (REC_PUBDEF32, REC_LPUBDEF32)
            p = 0
            _grp, n = _index(body, p)
            p += n
            seg_idx, n = _index(body, p)
            p += n
            if seg_idx == 0:
                p += 2                                # base frame for absolute
            while p < len(body):
                ln = body[p]
                p += 1
                name = body[p:p + ln].decode("latin-1")
                p += ln
                if is32:
                    poff = int.from_bytes(body[p:p + 4], "little")
                    p += 4
                else:
                    poff = int.from_bytes(body[p:p + 2], "little")
                    p += 2
                _t, n = _index(body, p)
                p += n
                mod.publics.append((name, seg_idx, poff))
        off = nxt
        if rt in (REC_MODEND_16, REC_MODEND_32):
            break
    return mod


def _apply_fixups(mod: Module, body: bytes, is32: bool,
                  seg: Segment | None, data_off: int, data_len: int) -> None:
    """Mark every byte a FIXUP subrecord patches inside the last data record."""
    p = 0
    while p < len(body):
        b0 = body[p]
        if not (b0 & 0x80):                            # THREAD subrecord
            method = (b0 >> 2) & 0x07
            p += 1
            if method <= 2:
                _v, n = _index(body, p)
                p += n
            continue
        b1 = body[p + 1]
        loc = (b0 >> 2) & 0x0F
        rec_off = ((b0 & 0x03) << 8) | b1
        p += 2
        fixdata = body[p]
        p += 1
        f_flag = (fixdata >> 7) & 1
        frame = (fixdata >> 4) & 0x07
        t_flag = (fixdata >> 3) & 1
        p_flag = (fixdata >> 2) & 1
        if not f_flag:
            if frame <= 2:
                _v, n = _index(body, p)
                p += n
            elif frame == 3:
                p += 2
        if not t_flag:
            _v, n = _index(body, p)
            p += n
        if not p_flag:
            p += 4 if is32 else 2
        if seg is None:
            continue
        width = LOC_WIDTH.get(loc, 4)
        start = data_off + rec_off
        _grow(seg, start + width)
        for i in range(start, min(start + width, len(seg.mask))):
            seg.mask[i] = 1
