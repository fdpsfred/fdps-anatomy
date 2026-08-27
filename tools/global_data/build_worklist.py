"""Work out which global data anchors ticket 17 still has to judge.

Reads the dump DumpGlobalState.java produced and the verdicts written so far,
and writes workspace/global_data/worklist.json.

Two jobs, and the second is the one that matters:

  * list the anchors with no verdict yet, in the order the workflow should send
    them out;
  * retire any verdict whose region no longer matches the program. An anchor
    keeps its address when the bytes around it are retyped or split, so
    "a verdict file exists" is not the same as "this anchor is settled"
    (ADR-0007 5.7). The verdict records the region_sha it was written against;
    when that no longer matches the dump, the verdict is moved aside and the
    anchor comes back onto the list.

String literals are not on the list. They already carry Ghidra's s_ labels, and
in the rebuilt C they are literals inside the statements that use them, not
named globals -- naming them would invent a symbol the original never had. They
are still in the dump, because a pointer table full of them is very much this
ticket's business.

Usage:  python tools/global_data/build_worklist.py
"""

import json
import os
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.join(REPO, "workspace", "global_data")
DUMP = os.path.join(WORK, "dump")
VERDICTS = os.path.join(WORK, "verdicts")


def load_globals():
    path = os.path.join(DUMP, "globals.json")
    if not os.path.isfile(path):
        sys.exit("no dump at %s -- run DumpGlobalState.java first" % path)
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)["globals"]


def retire(path, why):
    """Move a verdict aside rather than deleting it: it is still evidence."""
    dest = path + ".superseded-" + why
    n = 0
    while os.path.exists(dest):
        n += 1
        dest = path + ".superseded-%s-%d" % (why, n)
    shutil.move(path, dest)


def main():
    entries = load_globals()
    os.makedirs(VERDICTS, exist_ok=True)

    settled = []
    todo = []
    retired = []
    unparseable = []

    for entry in entries:
        addr = entry["addr"]
        if entry["is_string"]:
            continue
        path = os.path.join(VERDICTS, addr + ".json")
        if not os.path.isfile(path):
            todo.append(entry)
            continue
        try:
            with open(path, encoding="utf-8") as fh:
                verdict = json.load(fh)
        except Exception as exc:                              # noqa: BLE001
            unparseable.append("%s: %s" % (addr, exc))
            retire(path, "unparseable")
            todo.append(entry)
            continue
        covers = verdict.get("covers") or {}
        if covers.get("region_sha") != entry["region_sha"]:
            retired.append("%s: the bytes changed since the verdict was written" % addr)
            retire(path, "region")
            todo.append(entry)
            continue
        # A verdict that called this address a variable of its own is wrong once
        # a neighbour's type has swallowed it. That is a real outcome -- somebody
        # decided the table starts lower down -- and the honest response is to
        # re-judge this address, which will now see it is an interior.
        owner = entry.get("inside_defined_data")
        classification = verdict.get("classification")
        if owner and classification not in ("interior", "padding"):
            retired.append("%s: now sits inside the object at %s, but its verdict calls it %s"
                           % (addr, owner, classification or "(unstated)"))
            retire(path, "absorbed")
            todo.append(entry)
            continue
        settled.append(addr)

    # Ascending address, but anchors nothing references go last. An unreferenced
    # anchor is almost always the interior of a table whose start is on the list
    # already, and it reads far better once that start has a verdict.
    referenced = [e for e in todo if e["xref_count"] > 0]
    unreferenced = [e for e in todo if e["xref_count"] == 0]
    referenced.sort(key=lambda e: e["addr"])
    unreferenced.sort(key=lambda e: e["addr"])
    full = [e["addr"] for e in referenced + unreferenced]

    # Owners a verdict named that the reference sweep never saw. Their fields
    # are judged and point at them; they themselves have no evidence file and no
    # verdict, so they would stay unjudged for ever. Writing them here puts them
    # into the next dump, which puts them onto the next worklist. Nothing is
    # decided in this step -- an agent already decided the base is there.
    known = {e["addr"] for e in entries}
    extra = set()
    missing = set()
    for addr in settled + full:
        path = os.path.join(VERDICTS, addr + ".json")
        if not os.path.isfile(path):
            continue
        try:
            with open(path, encoding="utf-8") as fh:
                owner = ((json.load(fh).get("belongs_to")) or "").strip()
        except Exception:                                     # noqa: BLE001
            continue
        owner = owner.lower().replace("0x", "")
        # Four hex digits is the shortest thing that can be an address here --
        # the program starts at 0x10000. Anything shorter is a verdict that put
        # something else in the field, and zero-padding it would manufacture the
        # address 00000000 out of a typo.
        if len(owner) < 4 or len(owner) > 8 or any(c not in "0123456789abcdef" for c in owner):
            continue
        owner = owner.zfill(8)
        extra.add(owner)
        if owner not in known:
            missing.add(owner)
    # EVERY owner goes in the file, not only the ones the dump is missing right
    # now. Writing only the missing ones makes the file empty itself: an owner
    # is added, the next dump picks it up, the next run of this script sees it
    # is already an anchor and drops it from the file, and the dump after that
    # loses it again. Three of the six owners this ticket found were oscillating
    # exactly like that. Listing an address that is already an anchor costs
    # nothing -- DumpGlobalState adds it to a set.
    with open(os.path.join(WORK, "extra_anchors.txt"), "w", encoding="utf-8") as fh:
        fh.write("# Owners named by a verdict's belongs_to field.\n")
        fh.write("# Regenerated by build_worklist.py; read by DumpGlobalState.java.\n")
        for addr in sorted(extra):
            fh.write(addr + "\n")

    out = {
        "full": full,
        "off_anchor_owners": sorted(extra),
        "owners_not_yet_anchors": sorted(missing),
        "referenced": [e["addr"] for e in referenced],
        "unreferenced": [e["addr"] for e in unreferenced],
        "counts": {
            "anchors": len(entries),
            "strings_excluded": sum(1 for e in entries if e["is_string"]),
            "in_scope": len(full) + len(settled),
            "settled": len(settled),
            "todo": len(full),
            "retired_this_run": len(retired),
        },
        "retired": retired,
        "unparseable": unparseable,
    }
    path = os.path.join(WORK, "worklist.json")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2)

    print("anchors            %d" % out["counts"]["anchors"])
    print("string literals    %d (not on the list)" % out["counts"]["strings_excluded"])
    print("in scope           %d" % out["counts"]["in_scope"])
    print("settled            %d" % out["counts"]["settled"])
    print("todo               %d (%d referenced, %d not)"
          % (len(full), len(referenced), len(unreferenced)))
    print("owners from verdicts %d in extra_anchors.txt" % len(extra))
    if missing:
        print("  %d of them are not anchors yet; re-run DumpGlobalState.java "
              "to bring them onto the list" % len(missing))
        for addr in sorted(missing):
            print("    " + addr)
    if retired:
        print("retired this run   %d" % len(retired))
        for line in retired[:20]:
            print("  " + line)
    if unparseable:
        print("unparseable        %d" % len(unparseable))
        for line in unparseable[:20]:
            print("  " + line)
    print("wrote %s" % path)


if __name__ == "__main__":
    main()
