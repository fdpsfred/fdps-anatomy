"""List every byte two builds of the same target differ in, and why.

The gate answers with one word; when that word is `different` this is the tool
that says where.  Each differing byte is put in one of three classes, using the
same rules the gate uses (lefixup.py for relocations, lepad.py for the
alignment gaps wcc386 leaves uncleared):

    relocation   a fixup site or the Fixup Record Table
    gap          an alignment gap proved in BOTH builds at the same place
    unexplained  anything else -- a real difference, or a gap lepad.py could
                 not prove (an unsized symbol, a literal it could not show to
                 be a string); listed with the map symbols on either side

    python tools/build_gate/pad_diff.py [--target game|ailsmoke] OLD_ROOT NEW_ROOT

OLD_ROOT and NEW_ROOT are repository roots that have each built the target --
typically a clean `git worktree` at HEAD and the main working directory.  Each
side's gaps are computed from its own image, map, objects and staged sources.

Exit status 0: nothing unexplained and the gaps sit at the same places.
1: something is unexplained, or the gap positions differ.  2: bad arguments,
missing build files, or the images differ in size.
"""
import argparse
import bisect
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lefixup  # noqa: E402
import lepad  # noqa: E402

# Where each target's build leaves its image, map, objects and the sources it
# compiled, relative to the repository root (tools/game_build/build_game.py,
# tools/ail_link/link_ail.py).
LAYOUT = {
    "game": ("workspace/game_build", "out/FDE.EXE", "out/FDE.MAP", "out/obj",
             "stage/SRC"),
    "ailsmoke": ("workspace/ail_link", "out/AILSMOK.EXE", "out/AILSMOK.MAP",
                 "out/obj", "src"),
}


def load(root, target):
    work, exe, mapf, objs, src = LAYOUT[target]
    base = Path(root) / work
    paths = [base / exe, base / mapf, base / objs, base / src]
    missing = [str(p) for p in paths if not p.exists()]
    if missing:
        raise FileNotFoundError(", ".join(missing))
    image = paths[0].read_bytes()
    found = lepad.analyse_build(*paths)
    syms = {}
    for mod in lepad.parse_map(paths[1].read_text(encoding="latin-1"))["modules"]:
        for obj, off, name in mod["symbols"]:
            syms.setdefault(obj, []).append((off, name))
    return image, found, {k: sorted(v) for k, v in syms.items()}


def covered(ranges):
    out = set()
    for off, length in ranges:
        out.update(range(off, off + length))
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", choices=sorted(LAYOUT), default="game")
    ap.add_argument("old_root")
    ap.add_argument("new_root")
    args = ap.parse_args(argv[1:])
    try:
        a, pa, _ = load(args.old_root, args.target)
        b, pb, syms = load(args.new_root, args.target)
    except (FileNotFoundError, lepad.PadError, lefixup.LeError) as exc:
        print("cannot read the builds: %s" % exc)
        return 2
    if len(a) != len(b):
        print("size differs: %d vs %d" % (len(a), len(b)))
        return 2

    reloc = set()
    for img in (a, b):
        lo, hi = lefixup.fixup_bounds(img)
        reloc.update(range(lo, hi))
        reloc |= covered(lefixup.fixup_sites(img))
    gaps = covered(pa["ranges"]) & covered(pb["ranges"])
    same_gaps = pa["ranges"] == pb["ranges"]

    diff = [i for i in range(len(a)) if a[i] != b[i]]
    in_reloc = [i for i in diff if i in reloc]
    in_gap = [i for i in diff if i not in reloc and i in gaps]
    unexplained = [i for i in diff if i not in reloc and i not in gaps]
    print("differing bytes: %d  relocation: %d  gap: %d  unexplained: %d"
          % (len(diff), len(in_reloc), len(in_gap), len(unexplained)))
    print("gaps: old %d bytes, new %d bytes, %s"
          % (pa["bytes"], pb["bytes"],
             "same positions" if same_gaps else "POSITIONS DIFFER"))

    runs = []
    for i in unexplained:
        if runs and i == runs[-1][1] + 1:
            runs[-1][1] = i
        else:
            runs.append([i, i])
    objects = lepad.le_objects(b)
    for s, e in runs:
        where = "file 0x%x..0x%x" % (s, e)
        for n, obj in objects.items():
            if obj["start"] <= s < obj["end"]:
                off = s - obj["start"]
                where += "  obj %d +0x%x" % (n, off)
                table = syms.get(n, [])
                j = bisect.bisect_right(table, (off, "\xff")) - 1
                if j >= 0:
                    where += "  after %s (+0x%x)" % (table[j][1], table[j][0])
                if j + 1 < len(table):
                    where += "  before %s (+0x%x)" % (table[j + 1][1], table[j + 1][0])
        print("  " + where + "  old " + a[s:e + 1].hex() + "  new " + b[s:e + 1].hex())
    if pb["unsized"]:
        print("symbols without a provable size (the bytes after them are "
              "compared): %s" % ", ".join(pb["unsized"]))
    return 1 if unexplained or not same_gaps else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
