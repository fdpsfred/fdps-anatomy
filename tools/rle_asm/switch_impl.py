"""switch_impl.py -- link the RLE blitters from the transcribed assembly or from
the C translation kept for reference.

The rebuild links the fifteen RLE routines (fdps_blit_dispatch and the
fourteen under it) from src/rledisp.asm, rlebase.asm, rlepal.asm, rleturn.asm
and rlemix.asm.  Their C translations, the kernels' C prototypes and the tests
that call those kernels directly are kept in the tree, each inside a region
that opens with the marker line

    #if 0 /* RLE_C_REFERENCE -- rebuild_info/code_layout.md */

and closes with `#endif /* RLE_C_REFERENCE */`.  Switching to the C is: turn
every one of those `#if 0` into `#if 1`, and take the five .asm files out of
src/ (the build compiles whatever src/ holds, so a file left there would define
every routine twice).  Switching back is the reverse.  The .asm files are
parked under workspace/rle_asm/parked/ rather than deleted, so the way back
does not depend on a commit.

rebuild_info/code_layout.md owns the procedure and the checks to run after
each switch; this script only performs the edit, and refuses a tree that is
half in one state and half in the other.

Usage: python tools/rle_asm/switch_impl.py status
       python tools/rle_asm/switch_impl.py c
       python tools/rle_asm/switch_impl.py asm
       python tools/rle_asm/switch_impl.py selftest
"""
import argparse
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASM = ["rledisp", "rlebase", "rlepal", "rleturn", "rlemix"]
PARKED = ROOT / "workspace" / "rle_asm" / "parked"
MARK = "/* RLE_C_REFERENCE -- rebuild_info/code_layout.md */"
OFF = "#if 0 " + MARK
ON = "#if 1 " + MARK

# Every file that carries a marked region, and how many.  A region added or
# lost without this table changing is an error, not something to adapt to.
REGIONS = {
    "src/blit.c": 1, "src/rle.c": 1, "src/rlecolor.c": 1, "src/rlerot.c": 1,
    "src/rleblend.c": 1, "src/rle.h": 1, "src/rlecolor.h": 1, "src/rlerot.h": 1,
    "src/rleblend.h": 1, "tests/rle.c": 1, "tests/rlecolor.c": 1,
    "tests/rlerot.c": 1, "tests/rleblend.c": 1,
}


class SwitchError(Exception):
    pass


def state(root=ROOT, parked=PARKED):
    """'asm', 'c', or SwitchError naming what is inconsistent."""
    offs = ons = 0
    for rel, want in REGIONS.items():
        text = (root / rel).read_text(encoding="utf-8")
        n_off, n_on = text.count(OFF), text.count(ON)
        if n_off + n_on != want:
            raise SwitchError("%s has %d marked region(s), expected %d"
                              % (rel, n_off + n_on, want))
        offs += n_off
        ons += n_on
    present = [s for s in ASM if (root / "src" / (s + ".asm")).is_file()]
    if offs and not ons and len(present) == len(ASM):
        return "asm"
    if ons and not offs and not present:
        return "c"
    raise SwitchError("mixed state: %d region(s) off, %d on, assembly files in "
                      "src/: %s" % (offs, ons, ", ".join(present) or "none"))


def _flip(root, old, new):
    for rel in REGIONS:
        p = root / rel
        p.write_text(p.read_text(encoding="utf-8").replace(old, new),
                     encoding="utf-8")


def to_c(root=ROOT, parked=PARKED):
    if state(root, parked) != "asm":
        raise SwitchError("not in the assembly state")
    parked.mkdir(parents=True, exist_ok=True)
    for s in ASM:
        shutil.move(str(root / "src" / (s + ".asm")), str(parked / (s + ".asm")))
    _flip(root, OFF, ON)
    return state(root, parked)


def to_asm(root=ROOT, parked=PARKED):
    if state(root, parked) != "c":
        raise SwitchError("not in the C state")
    missing = [s for s in ASM if not (parked / (s + ".asm")).is_file()]
    if missing:
        raise SwitchError("no parked copy of %s -- restore them with "
                          "`git checkout -- src/` instead" % ", ".join(missing))
    for s in ASM:
        shutil.move(str(parked / (s + ".asm")), str(root / "src" / (s + ".asm")))
    _flip(root, ON, OFF)
    return state(root, parked)


def selftest():
    rows = []
    tmp = Path(tempfile.mkdtemp(prefix="rle_switch_"))
    try:
        for rel in REGIONS:
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
            (tmp / rel).write_text("int keep;\n%s\nint ref;\n#endif\n" % OFF,
                                   encoding="utf-8")
        for s in ASM:
            (tmp / "src" / (s + ".asm")).write_text("; %s\n" % s, encoding="utf-8")
        parked = tmp / "parked"
        rows.append(("starts in the assembly state", state(tmp, parked) == "asm"))
        rows.append(("switches to C", to_c(tmp, parked) == "c"
                     and not list((tmp / "src").glob("*.asm"))))
        rows.append(("switches back", to_asm(tmp, parked) == "asm"
                     and (tmp / "src" / "rlemix.asm").read_text() == "; rlemix\n"))
        (tmp / "src" / "rlebase.asm").unlink()
        try:
            state(tmp, parked)
            rows.append(("a missing .asm is a mixed state", False))
        except SwitchError:
            rows.append(("a missing .asm is a mixed state", True))
        (tmp / "src" / "rlebase.asm").write_text("; rlebase\n", encoding="utf-8")
        p = tmp / "src" / "rle.h"
        p.write_text(p.read_text(encoding="utf-8").replace(OFF, ""), encoding="utf-8")
        try:
            state(tmp, parked)
            rows.append(("a lost marker is refused", False))
        except SwitchError:
            rows.append(("a lost marker is refused", True))
    finally:
        shutil.rmtree(str(tmp), ignore_errors=True)
    ok = True
    for label, passed in rows:
        print("[selftest] %-36s %s" % (label, "ok" if passed else "FAIL"))
        ok = ok and passed
    print("[result] %s" % ("PASS" if ok else "FAIL"))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=("status", "c", "asm", "selftest"))
    args = ap.parse_args()
    try:
        if args.action == "selftest":
            return 0 if selftest() else 1
        if args.action == "status":
            print(state())
        elif args.action == "c":
            print("now linking the C translation: %s" % to_c())
        else:
            print("now linking the assembly: %s" % to_asm())
    except SwitchError as exc:
        print("[error] %s" % exc)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
