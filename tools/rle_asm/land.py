"""land.py -- put the transcribed RLE routines into src/ as five assembly files.

The transcription stage of ticket 22.3 writes one standalone WASM module per
routine into workspace/rle_asm/frag/, each already checked against the original
by asm_match.py.  This script does not judge anything: it takes those modules
as they are and joins the ones that belong to the same file under src/ into
one module -- one header, the union of their EXTRNs minus the symbols the file
now defines itself, every PUBLIC, and each routine's text verbatim in the
original's address order.

A fragment that is missing, that has not passed its check, or that does not
have the shape below is reported and nothing is written: the fix belongs in
the transcription, not here (ADR-0007 5.3).

    ; ... anything, kept as the routine's own header only if after `public`
            .386p
            extrn   <symbol>:<type>          (zero or more, one per line)
    _TEXT   segment byte public use32 'CODE'
            assume  cs:_TEXT
            public  <routine>
    <the routine's comment block and its proc ... endp>
    _TEXT   ends
            end

Usage: python tools/rle_asm/land.py [--check]
       --check   verify every fragment is ready and show what would be written
Exit : 0 when all five files were written (or would be).
"""
import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import asm_match as am  # noqa: E402

FRAG = am.WORK / "frag"
VERDICTS = am.WORK / "verdicts"

# What each file is.  The routines' own comment blocks come from their
# fragments; this is the paragraph above them.
FILE_HEADERS = {
    "rledisp": """\
; rledisp.asm -- fdps_blit_dispatch, the one entry point into the RLE sprite
; blitters (000568db in the original).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3): the same instructions, the same lengths, the same
; NOP padding behind every forward short branch.  The C spelling of the same
; routine is kept for reading in src/blit.c under #if 0; this file is what is
; linked.  rebuild_info/code_layout.md says how to switch back to the C.
;
; The dispatcher is called from C with the stack convention blit.h declares
; (seven arguments, caller pops), keeps EBX, ESI, EDI and EBP, and hands the
; stream to its kernel in ESI, the destination in EDI and the row advance in
; EDX.  The kernels in rlebase.asm, rlepal.asm, rleturn.asm and rlemix.asm
; read the rest of their input out of this routine's own EBP frame.
""",
    "rlebase": """\
; rlebase.asm -- the plain RLE kernels: pass-through (mode 0), scaled (mode
; 4), the row skipper the scaling kernels share, and the two mirrors (modes 7
; and 8).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3).  The C spelling is kept for reading in src/rle.c
; under #if 0; this file is what is linked.  The kernels are entered only from
; fdps_blit_dispatch (rledisp.asm), with ESI the stream, EDI the destination
; and EDX the row advance, and they read the rest out of the dispatcher's EBP
; frame and the rectangle globals gamedata.c defines.
""",
    "rlepal": """\
; rlepal.asm -- the palette kernels: remap sprite and backdrop (mode 1),
; remap the sprite only (mode 2) and recolor (mode 3).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3).  The C spelling is kept for reading in
; src/rlecolor.c under #if 0; this file is what is linked.  Entered only from
; fdps_blit_dispatch (rledisp.asm); the mode operand is read out of the
; dispatcher's EBP frame.
""",
    "rleturn": """\
; rleturn.asm -- the rotating kernels: rotated (mode 5) and rotated and
; scaled (mode 6).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3).  The C spelling is kept for reading in
; src/rlerot.c under #if 0, and the rotation globals are still defined there;
; this file is what is linked.  Entered only from fdps_blit_dispatch
; (rledisp.asm); the geometry is read out of the dispatcher's EBP frame.
""",
    "rlemix": """\
; rlemix.asm -- the blending kernels: translucent (mode 9), tint sprite and
; backdrop (mode 10), tint (mode 11) and translucent colour range (mode 12).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3).  The C spelling is kept for reading in
; src/rleblend.c under #if 0, and the colour-range globals are still defined
; there; this file is what is linked.  Entered only from fdps_blit_dispatch
; (rledisp.asm); the blend descriptor is read out of the dispatcher's EBP
; frame, and four of these kernels use the dispatcher's argument slots as
; their own scratch.
""",
}

EXTRN_RX = re.compile(r"^\s*extrn\s+([A-Za-z_]\w*)\s*:\s*(\w+)\s*(?:;.*)?$", re.I)
PUBLIC_RX = re.compile(r"^\s*public\s+([A-Za-z_]\w*)\s*(?:;.*)?$", re.I)


class LandError(Exception):
    pass


def frag_path(prefix):
    return FRAG / (prefix + ".asm")


def parse_fragment(path, name):
    """(externs {symbol: type}, body text) of one fragment, or LandError."""
    lines = path.read_text(encoding="latin-1").splitlines()
    externs, body = {}, None
    state = "head"
    seg_open = False
    for n, line in enumerate(lines, 1):
        s = line.strip()
        low = s.lower()
        if state == "head":
            if not s or s.startswith(";") or low == ".386p":
                continue
            m = EXTRN_RX.match(line)
            if m:
                externs[m.group(1)] = m.group(2).lower()
                continue
            if re.match(r"^_TEXT\s+segment\s+byte\s+public\s+use32\s+'CODE'$", s, re.I):
                seg_open = True
                continue
            if seg_open and re.match(r"^assume\s+cs\s*:\s*_TEXT$", s, re.I):
                continue
            m = PUBLIC_RX.match(line)
            if seg_open and m:
                if m.group(1) != name:
                    raise LandError("%s:%d: public %s, expected %s"
                                    % (path.name, n, m.group(1), name))
                state = "body"
                body = []
                continue
            raise LandError("%s:%d: unexpected line before the routine: %s"
                            % (path.name, n, s))
        elif state == "body":
            if re.match(r"^_TEXT\s+ends$", s, re.I):
                state = "tail"
                continue
            body.append(line.rstrip())
        else:
            if not s or s.startswith(";") or low == "end":
                continue
            raise LandError("%s:%d: unexpected line after the segment: %s"
                            % (path.name, n, s))
    if state != "tail":
        raise LandError("%s: no `_TEXT ends` after the routine" % path.name)
    while body and not body[0].strip():
        body.pop(0)
    while body and not body[-1].strip():
        body.pop()
    text = "\n".join(body)
    if not re.search(r"^%s\s+proc\s+near\b" % re.escape(name), text, re.M | re.I):
        raise LandError("%s: no `%s proc near`" % (path.name, name))
    return externs, text


def verdict_ok(addr):
    p = VERDICTS / (addr + ".json")
    if not p.is_file():
        return False, "no verdict file"
    try:
        v = json.loads(p.read_text(encoding="utf-8"))
    except ValueError as exc:
        return False, "verdict unreadable: %s" % exc
    if v.get("check") != "pass":
        return False, "verdict check is %r" % v.get("check")
    return True, ""


def build_file(stem):
    rows = [r for r in am.ROSTER if r.file == stem]
    defined = {r.name for r in rows}
    externs, bodies = {}, []
    for addr, name, _f, prefix in rows:
        ok, why = verdict_ok(addr)
        if not ok:
            raise LandError("%s %s: %s" % (addr, name, why))
        path = frag_path(prefix)
        if not path.is_file():
            raise LandError("%s %s: missing %s" % (addr, name, path))
        ex, body = parse_fragment(path, name)
        for sym, typ in ex.items():
            if sym in defined:
                continue
            if sym in externs and externs[sym] != typ:
                raise LandError("%s: %s is %s here and %s in another routine"
                                % (path.name, sym, typ, externs[sym]))
            externs[sym] = typ
        bodies.append(body)
    out = [FILE_HEADERS[stem].rstrip(), "", "        .386p", ""]
    for sym in sorted(externs):
        out.append("        extrn   %s:%s" % (sym, externs[sym]))
    if externs:
        out.append("")
    out += ["_TEXT   segment byte public use32 'CODE'",
            "        assume  cs:_TEXT", ""]
    for _a, name, _f, _p in rows:
        out.append("        public  %s" % name)
    for body in bodies:
        out += ["", body]
    out += ["", "_TEXT   ends", "", "        end", ""]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    texts, problems = {}, []
    for stem in am.ASM_FILES:
        try:
            texts[stem] = build_file(stem)
        except LandError as exc:
            problems.append(str(exc))
    if problems:
        for p in problems:
            print("[land] NOT READY: " + p)
        print("[result] FAIL")
        return 1
    for stem, text in texts.items():
        target = am.SRC / (stem + ".asm")
        if args.check:
            print("[land] would write %s (%d lines)" % (target, text.count("\n")))
        else:
            target.write_bytes(text.encode("latin-1"))
            print("[land] wrote %s (%d lines)" % (target, text.count("\n")))
    print("[result] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
