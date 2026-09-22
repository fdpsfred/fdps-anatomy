"""Sweep -oe=N over real src/ units and their call-spelled variants, and find
the N values whose expansions agree with FDPS.LE.

For every compiled listing, each (caller, callee) pair of functions routed to
the same src/ unit is looked up twice:

    image    does the original caller body contain an E8 CALL to the callee's
             address?  (read from fdps_game_files/FDPS.LE)
    rebuilt  does the rebuilt caller still CALL the callee at this N?

A pair counts as "not called" at every N where the rebuilt caller does not
CALL it (including N values where the pair does not appear at all: a large N
can expand an intermediate caller and make a new pair appear).
A pair is N-sensitive when its rebuilt state changes somewhere in the sweep;
those are the pairs that decide the threshold.  The matching window is every N
at which all N-sensitive pairs agree with the image, except the pairs listed
in TU_SPLIT: callers that src/ groups with their callee although the original
kept them in separate translation units (so the original could not expand
them at any N).  Pairs that disagree at every N are reported separately --
they come from how src/ spells the caller, not from the flag.

Also tallies fn_match verdicts per unit and N (exact / length / differs).

    python oe_threshold.py                 # default sweep
    python oe_threshold.py 20 25 30        # just these N (plus the default)
    python oe_threshold.py --reuse         # re-analyse the last run's listings

Base flags: -bt=dos4g -mf -zq -4s -fpi -s -ot -oe=N -d2.  Outputs land in
workspace/build_flags/oe_threshold/.
"""

import os
import sys

import fn_match
import fn_variants

OUT = os.path.join(fn_match.REPO, "workspace", "build_flags", "oe_threshold")
BASE = "-bt=dos4g -mf -zq -4s -fpi -s -ot"
N_DEFAULT = list(range(15, 41)) + [45, 50, 60, 70, 80]

# source tag -> (one-letter 8.3 prefix, kind)
SOURCES = [("gau", "g"), ("gauv", "v"), ("safv", "s"), ("saf", "f"),
           ("mpt", "m"), ("palv", "p"), ("chvv", "c"), ("chvw", "w"),
           ("spr", "r")]

# Callers that src/ puts in the same file as their callee while the original
# placed them in another translation unit: the original caller sits far from
# the callee's address run and calls it for real.
TU_SPLIT = {0x000222c0: "fdps_saf_play_over_scene (saf.c) is outside the "
                        "000140e0..00014550 run it calls into",
            0x0002e640: "fdps_cel_expand_sheet_24x24 (sprite.c) is outside the "
                        "run holding its callees"}


# Functions a variant adds to the unit it is compared against.
EXTRA = {"chvv": ["fdps_unit_is_retired"], "chvw": ["fdps_unit_is_retired"]}


def unit_functions(routing, unit, tag):
    """[(addr, name)] compared for one source: its unit plus EXTRA."""
    fns = list(routing.get(unit, []))
    want = set(EXTRA.get(tag, []))
    for lst in routing.values():
        fns.extend((a, n) for a, n in lst if n in want)
    return sorted(set(fns))


def image_calls(orig, fns):
    """{(caller, callee)} among fns that the original really CALLs."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    addrs = dict(fns)
    pairs = set()
    for addr, name in addrs.items():
        start = orig.file_off(addr)
        code = orig.data[start:start + orig.sizes[addr]]
        for ins in md.disasm(code, addr):
            if ins.bytes[0] == 0xE8 and ins.size == 5:
                tgt = (ins.address + 5 + int.from_bytes(ins.bytes[1:5], "little",
                                                        signed=True)) & 0xFFFFFFFF
                if tgt in addrs:
                    pairs.add((name, addrs[tgt]))
    return pairs, addrs


def main():
    # --reuse parses the listings of a previous run instead of recompiling
    reuse = "--reuse" in sys.argv
    ns = [int(a) for a in sys.argv[1:] if a != "--reuse"] or N_DEFAULT
    points = [None] + ns
    orig = fn_match.Original()
    routing = fn_match.load_routing()
    work = OUT
    sources, units = {}, {}
    for tag, _p in SOURCES:
        if tag in fn_variants.NAMES:
            text, unit = fn_variants.build(tag, fn_match.read_src)
        else:
            unit = fn_match.UNITS[tag]
            text = fn_match.read_src(unit)
        sources[tag], units[tag] = text, unit
    fn_match.stage(work, sources)

    listings = {}
    for tag, pre in SOURCES:            # one DOSBox-X session per source
        jobs = []
        for n in points:
            stem = "%s%s" % (pre, "d" if n is None else n)
            oe = "-oe" if n is None else "-oe=%d" % n
            jobs.append((stem, "%s %s -d2" % (BASE, oe), tag))
        if reuse:
            got = {stem: os.path.join(work, stem + ".lst") for stem, _f, _t in jobs}
            gone = [p for p in got.values() if not os.path.exists(p)]
            if gone:
                raise SystemExit("--reuse: missing listings %s" % gone[:5])
        else:
            got = fn_match.compile_jobs(work, jobs, timeout=1800)
        for n, (stem, _f, _t) in zip(points, jobs):
            listings[(tag, n)] = got[stem]
        print("listings for %s at %d points" % (tag, len(points)), flush=True)

    report = []
    sensitive_rows = []
    for tag, _p in SOURCES:
        unit = units[tag]
        fns = unit_functions(routing, unit, tag)
        img, addrs = image_calls(orig, fns)
        names = set(addrs.values())
        state = {}              # pair -> [called? per point]
        tallies = []
        for n in points:
            funcs = fn_match.parse_listing(listings[(tag, n)])
            t = {"exact": 0, "length": 0, "differs": 0, "missing": 0}
            for addr, name in fns:
                ob, mask = orig.body(addr)
                got = funcs.get(name)
                t[fn_match.verdict(ob, mask, got["code"] if got else None)[0]] += 1
            tallies.append(t)
            called = {(c, callee) for c, f in funcs.items() if c in names
                      for callee in f["calls"] if callee in names}
            for pair in called | img | set(state):
                state.setdefault(pair, [False] * len(points))
            for pair in state:
                state[pair][points.index(n)] = pair in called
        by_name = {v: k for k, v in addrs.items()}
        report.append("== %s (%s)" % (tag, unit))
        report.append("  exact per N: " + " ".join(
            "%s:%d" % ("def" if n is None else n, t["exact"])
            for n, t in zip(points, tallies)))
        for pair, row in sorted(state.items()):
            want = pair in img
            split = by_name.get(pair[0]) in TU_SPLIT
            if len(set(row)) > 1:
                agree = [n for n, r in zip(points, row) if r == want]
                sensitive_rows.append((tag, pair, want, split, agree))
                report.append("  N-sensitive %s -> %s: image %s; rebuilt CALL at %s%s"
                              % (pair[0], pair[1], "CALL" if want else "no CALL",
                                 ",".join("def" if n is None else str(n)
                                          for n, r in zip(points, row) if r) or "-",
                                 "  [TU split, ignored]" if split else ""))
            elif row[0] != want:
                report.append("  constant mismatch %s -> %s: image %s, rebuilt %s at "
                              "every N%s" % (pair[0], pair[1],
                                             "CALL" if want else "no CALL",
                                             "CALL" if row[0] else "expanded",
                                             "  [TU split]" if split else ""))
    window = [n for n in points
              if all(n in agree for _t, _p, _w, split, agree in sensitive_rows
                     if not split)]
    report.append("")
    report.append("matching N (every N-sensitive pair agrees with FDPS.LE): %s"
                  % ", ".join("default" if n is None else str(n) for n in window))
    text = "\n".join(report) + "\n"
    with open(os.path.join(work, "threshold.txt"), "w", encoding="utf-8",
              newline="\n") as fh:
        fh.write(text)
    print(text)


if __name__ == "__main__":
    main()
