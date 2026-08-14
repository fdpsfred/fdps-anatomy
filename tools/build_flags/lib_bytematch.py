"""Search every installed Watcom library for a byte sequence lifted from FDPS.LE.

Which library variant the original was linked against is the link-level proof of
the default calling convention: CLIB3S/MATH387S are the stack-convention builds,
CLIB3R/MATH387R the register-convention ones.  A relocation-free run of bytes
from a CRT routine in FDPS.LE will only be found in the variant that was
actually linked.

Usage:
    python lib_bytematch.py <hex bytes> [<hex bytes> ...]
    python lib_bytematch.py --named            # run the built-in probe set
"""

import os
import sys

SERIES_ROOTS = [
    r"C:\Users\fdpsf\Documents\WATCOM_9.5_series",
    r"C:\Users\fdpsf\Documents\WATCOM_10_series",
]

# name -> (address in FDPS.LE, relocation-free byte run)
PROBES = {
    # 0x000435f3: the upper-casing loop reached from the VFS member lookup
    "strupr": "8b4424048a1084d2741080ea6180fa19770580c241881040ebea8b442404c3",
    # 0x0004362d: body of the stack-overflow check called by __CHK
    "chk_body": "39e0730d29e0f7d8",
    # 0x00043657: __CHP, the double->int helper called ahead of every FISTP
    "chp": "535657558b6c241c8b7424148b7c24188b5c242089ea",
    # 0x00043310: first instructions of the DOS/4G CRT startup
    "cstart": "fb83e4fc89e3891d",
}


def libs():
    for series in SERIES_ROOTS:
        for version in sorted(os.listdir(series)):
            root = os.path.join(series, version)
            for sub in ("LIB386", os.path.join("LIB386", "DOS")):
                d = os.path.join(root, sub)
                if not os.path.isdir(d):
                    continue
                for name in sorted(os.listdir(d)):
                    if name.lower().endswith((".lib", ".obj")):
                        yield version, name, os.path.join(d, name)


def main():
    args = sys.argv[1:]
    if not args or args[0] == "--named":
        probes = PROBES
    else:
        probes = {"arg%d" % i: a for i, a in enumerate(args)}

    for label, hexstr in probes.items():
        needle = bytes.fromhex(hexstr)
        print("== %s (%d bytes) ==" % (label, len(needle)))
        hits = 0
        for version, name, path in libs():
            with open(path, "rb") as fh:
                data = fh.read()
            off = data.find(needle)
            if off >= 0:
                print("   %-16s %-16s @0x%x" % (version, name, off))
                hits += 1
        if not hits:
            print("   (no match)")


if __name__ == "__main__":
    main()
