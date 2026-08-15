"""Turn per-function verdict files into the artefacts the apply step needs.

Each reader agent writes exactly one file, ``verdicts/<addr>.json``, holding its
full judgement of that one function. This script gathers a named subset of them
and emits:

* ``apply_<tag>.json``  -- what ``ApplyBackboneWalk.java`` transcribes into
  Ghidra: name, plate comment, no-return flag.
* ``protos_<tag>.txt``  -- one prototype per line for the writer agent to feed
  to the MCP ``set_function_prototype`` tool, which validates the signature.
* ``index.json``        -- the compact roll-up of every verdict collected so
  far: address, name, pool, subsystem, one-line role, confidence. This is what
  the synthesis step reads, so that summarising the walk never requires loading
  a hundred plate comments.

Usage:
  python collect_verdicts.py <verdict_dir> <out_dir> <tag> [addr ...]

With no addresses, every verdict in the directory is collected.
"""

import io
import json
import os
import sys

REQUIRED = ("addr", "name", "pool", "plate_comment", "prototype", "calling_convention")


def load(path):
    with io.open(path, encoding="utf-8") as fh:
        return json.load(fh)


def dump(path, obj):
    with io.open(path, "w", encoding="utf-8") as fh:
        fh.write(json.dumps(obj, indent=2, ensure_ascii=False))


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    verdict_dir, out_dir, tag = sys.argv[1], sys.argv[2], sys.argv[3]
    wanted = [a.lower() for a in sys.argv[4:]]

    if not os.path.isdir(verdict_dir):
        raise SystemExit("no verdict directory: %s" % verdict_dir)
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    all_verdicts = {}
    for name in sorted(os.listdir(verdict_dir)):
        if not name.endswith(".json"):
            continue
        v = load(os.path.join(verdict_dir, name))
        missing = [k for k in REQUIRED if not v.get(k)]
        if missing:
            print("SKIP %s: missing %s" % (name, ",".join(missing)))
            continue
        all_verdicts[v["addr"].lower()] = v

    selected = [all_verdicts[a] for a in wanted if a in all_verdicts] if wanted \
        else [all_verdicts[a] for a in sorted(all_verdicts)]
    absent = [a for a in wanted if a not in all_verdicts]

    apply_items = [{
        "addr": v["addr"],
        "name": v["name"],
        "plate_comment": v["plate_comment"],
        "no_return": bool(v.get("no_return")),
    } for v in selected]
    dump(os.path.join(out_dir, "apply_%s.json" % tag), apply_items)

    proto_path = os.path.join(out_dir, "protos_%s.txt" % tag)
    with io.open(proto_path, "w", encoding="utf-8") as fh:
        for v in selected:
            fh.write("%s\t%s\t%s\n" % (v["addr"], v["calling_convention"], v["prototype"]))

    index = [{
        "addr": v["addr"],
        "name": v["name"],
        "pool": v["pool"],
        "subsystem": v.get("subsystem", "unknown"),
        "role": v.get("role", ""),
        "confidence": v.get("confidence", ""),
    } for v in (all_verdicts[a] for a in sorted(all_verdicts))]
    dump(os.path.join(out_dir, "index.json"), index)

    # Names must be unique across the whole program, so report duplicates over
    # everything collected, not just this round.
    seen = {}
    duplicates = []
    for v in index:
        if v["name"] in seen and seen[v["name"]] != v["addr"]:
            duplicates.append((v["name"], seen[v["name"]], v["addr"]))
        seen.setdefault(v["name"], v["addr"])

    print("collected %d of %d verdict(s) for tag %s" % (len(selected), len(all_verdicts), tag))
    if absent:
        print("NO VERDICT FILE: %s" % ", ".join(absent))
    if duplicates:
        for name, a, b in duplicates:
            print("DUPLICATE NAME: %s used by %s and %s" % (name, a, b))
    else:
        print("no duplicate names")


if __name__ == "__main__":
    main()
