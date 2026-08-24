"""Ask every confirmed CRT function which Watcom releases it could have come from.

For each function dumped by DumpCrtBodies.java this walks every extracted
library module of every installed release and looks for a placement where the
function's bytes equal the module's, ignoring only the bytes that module's own
FIXUPP records say the linker patches.  A release where such a placement exists
is compatible with the function; a release where it does not is excluded by it.

The output is one evidence packet per function, never a verdict: deciding what
a hit or a miss means for a given function is per-function work and belongs to
an agent that has read the assembly (ADR-0002).

Usage:
    python tools/crt_version/sweep_versions.py [--bodies <json>] [--out <dir>]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from omf_image import read_module            # noqa: E402

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
WORK = REPO / "workspace" / "crt_version"

MIN_ANCHOR = 4          # shorter runs put the candidate list into the thousands
MAX_ANCHORS = 4
MAX_CANDIDATES = 4000   # per module, guards against a one-byte anchor blowing up


def load_versions(lib_index: dict) -> dict:
    """version -> list of (lib, module, segment_name, image, mask, publics)."""
    out = {}
    for version, entry in lib_index["versions"].items():
        mods = []
        for lib, modules in entry["libs"].items():
            for name, info in modules.items():
                try:
                    mod = read_module(Path(info["file"]))
                except Exception as exc:                       # noqa: BLE001
                    print("  WARN %s/%s/%s: %s" % (version, lib, name, exc))
                    continue
                for seg in mod.segments:
                    if not seg.image:
                        continue
                    mods.append((lib, name, seg.name, bytes(seg.image),
                                 bytes(seg.mask), mod.publics))
        out[version] = mods
    return out


def runs(length: int, masked: set[int]):
    """Maximal runs of unmasked byte offsets, longest first."""
    spans, start = [], None
    for i in range(length):
        if i in masked:
            if start is not None:
                spans.append((start, i - start))
                start = None
        elif start is None:
            start = i
    if start is not None:
        spans.append((start, length - start))
    spans.sort(key=lambda s: -s[1])
    return spans


def verify(body: bytes, masked: set[int], image: bytes, mask: bytes, at: int) -> int:
    """Mismatching byte count with the module's own fixup fields ignored.

    The image-side mask is authoritative: it is what the linker would have
    overwritten.  The FDPS-side mask is folded in as well because a reference
    Ghidra resolved but the module records differently (a thread-based fixup on
    a neighbouring record) would otherwise read as a real difference.
    """
    if at < 0 or at + len(body) > len(image):
        return -1
    bad = 0
    for i in range(len(body)):
        if mask[at + i]:
            continue
        if i in masked:
            continue
        if body[i] != image[at + i]:
            bad += 1
    return bad


def scan_module(body: bytes, masked: set[int], anchors, image: bytes, mask: bytes):
    """Best (mismatch, offset) for this function inside one segment image."""
    best = (None, None)
    for a_off, a_len in anchors:
        needle = body[a_off:a_off + a_len]
        pos = image.find(needle)
        seen = 0
        while pos >= 0 and seen < MAX_CANDIDATES:
            seen += 1
            at = pos - a_off
            bad = verify(body, masked, image, mask, at)
            if bad == 0:
                return 0, at
            if bad >= 0 and (best[0] is None or bad < best[0]):
                best = (bad, at)
            pos = image.find(needle, pos + 1)
        if best[0] == 0:
            break
    return best


def public_at(publics, offset: int) -> str:
    names = [n for n, _s, o in publics if o == offset]
    return names[0] if names else ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bodies", type=Path, default=WORK / "crt_bodies.json")
    ap.add_argument("--index", type=Path, default=WORK / "lib_index.json")
    ap.add_argument("--out", type=Path, default=WORK / "packets")
    ap.add_argument("--only", nargs="*", default=None, help="addresses to process")
    args = ap.parse_args()

    bodies = json.loads(args.bodies.read_text(encoding="utf-8"))["functions"]
    lib_index = json.loads(args.index.read_text(encoding="utf-8"))
    print("loading library modules ...")
    versions = load_versions(lib_index)
    for v, mods in versions.items():
        print("  %-6s %d segments" % (v, len(mods)))

    args.out.mkdir(parents=True, exist_ok=True)
    order = sorted(versions)
    summary = []
    for fn in bodies:
        if args.only and fn["address"] not in args.only:
            continue
        body = bytes.fromhex(fn["bytes"])
        masked = set(fn["masked"])
        anchors = [s for s in runs(len(body), masked) if s[1] >= MIN_ANCHOR][:MAX_ANCHORS]
        packet = {
            "address": fn["address"],
            "name": fn["name"],
            "body_size": fn["body_size"],
            "dump_len": fn["dump_len"],
            "contiguous": fn["contiguous"],
            "ranges": fn.get("ranges", []),
            "calling_convention": fn.get("calling_convention", ""),
            "plate": fn.get("plate", ""),
            "masked_bytes": len(masked),
            "comparable_bytes": len(body) - len(masked),
            "anchors": [{"offset": o, "length": n, "hex": body[o:o + n].hex()}
                        for o, n in anchors],
            "versions": {},
            "listing": fn.get("listing", []),
        }
        if not anchors:
            packet["anchorless"] = True

        for version in order:
            hits, near = [], None
            for lib, module, seg, image, mask, publics in versions[version]:
                if len(image) < len(body):
                    continue
                bad, at = scan_module(body, masked, anchors, image, mask)
                if bad is None:
                    continue
                rec = {"lib": lib, "module": module, "segment": seg,
                       "offset": at, "mismatch": bad,
                       "public": public_at(publics, at)}
                if bad == 0:
                    hits.append(rec)
                elif near is None or bad < near["mismatch"]:
                    near = rec
            # A generic thunk matches fifteen or twenty modules of one release,
            # and listing them all would bury the packet.  But the truncation
            # has to announce itself: an agent that reasoned about "which
            # modules matched" from a silently shortened list once picked the
            # wrong module because the only one defining the symbol had been
            # cut off.  hits_truncated says the list is a sample, and the tail
            # keeps the module names so nothing is unrecoverable.
            packet["versions"][version] = {
                "match": bool(hits),
                "hits": hits[:8],
                "hit_count": len(hits),
                "hits_truncated": len(hits) > 8,
                "hit_modules": sorted({h["module"] for h in hits}),
                "hit_publics": sorted({h["public"] for h in hits if h["public"]}),
                "nearest": near,
            }

        matched = [v for v in order if packet["versions"][v]["match"]]
        packet["compatible_versions"] = matched
        packet["excluded_versions"] = [v for v in order if v not in matched]
        packet["discriminating"] = 0 < len(matched) < len(order)
        (args.out / ("%s.json" % fn["address"])).write_text(
            json.dumps(packet, indent=1), encoding="utf-8")
        summary.append({"address": fn["address"], "name": fn["name"],
                        "size": fn["body_size"],
                        "compatible": matched,
                        "discriminating": packet["discriminating"]})
        print("  %s %-38s %s" % (fn["address"], fn["name"][:38],
                                 ",".join(matched) if matched else "(none)"))

    (WORK / "sweep_summary.json").write_text(json.dumps(summary, indent=1),
                                             encoding="utf-8")
    print("packets=%d  wrote %s" % (len(summary), WORK / "sweep_summary.json"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
