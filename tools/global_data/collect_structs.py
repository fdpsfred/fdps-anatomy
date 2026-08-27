"""Work out which structs ticket 17 has to lay out, and which are still to do.

The struct list is not something anyone can write down in advance. It falls out
of the global-data verdicts: an agent that judged one anchor and found it to be
the base of a table of fixed-stride records, or a pointer to one, says so in its
struct_candidate field. This gathers those, folds in the structs the ticket names
explicitly, and reports which already have a layout file.

Candidates are grouped by the name the agents proposed. Two anchors pointing at
the same record type is the normal case -- the unit array and the pointer to it
-- and both belong to one layout job, not two.

Usage:  python tools/global_data/collect_structs.py
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.join(REPO, "workspace", "global_data")
VERDICTS = os.path.join(WORK, "verdicts")
STRUCTS = os.path.join(WORK, "structs")

# The layouts the ticket names in its own words. They go on the list whether or
# not any verdict happened to propose them, because "the item table has a
# layout" is a claim the ticket makes and owes an answer on -- including the
# answer "the game reads this one from a file, there is no struct in the image".
SEEDS = [
    {"name": "fdps_unit_record",
     "hint": "the 0x50-byte battle unit record; the array base is held at 00069cd8"},
    {"name": "fdps_item_entry",
     "hint": "one row of the item table; cross-check the fields against docs/guide"},
    {"name": "fdps_spell_entry",
     "hint": "one row of the spell table; cross-check against docs/guide"},
    {"name": "fdps_class_entry",
     "hint": "one row of the character-class table; cross-check against docs/guide"},
    {"name": "fdps_chapter_entry",
     "hint": "one row of the per-chapter table; chapters are 0..29 internally"},
    {"name": "fdps_save_slot",
     "hint": "the saved-game record; ticket 17 also owes the mapping to FD2's"},
]


def resembles(a, b):
    """Do two proposed names look like two spellings of one record?

    Deliberately crude, and it only decides what gets shown to an agent, never
    what gets merged. One name being the other with a pool prefix in front
    (tm / crt_tm), or the two sharing everything but one word in the middle
    (crt_scale10_pow10_entry / crt_scale10_power_entry), is enough to be worth
    a look.
    """
    if a.endswith("_" + b) or b.endswith("_" + a):
        return True
    aw, bw = a.split("_"), b.split("_")
    if abs(len(aw) - len(bw)) > 1:
        return False
    shared = sum(1 for w in aw if w in bw)
    if shared < 2 or shared < max(len(aw), len(bw)) - 1:
        return False
    odd_a = [w for w in aw if w not in bw]
    odd_b = [w for w in bw if w not in aw]
    if not odd_a or not odd_b:
        # One name is the other with a word added: crt_extended_real against
        # crt_emu387_extended_real. Worth a look.
        return True
    if len(odd_a) > 1 or len(odd_b) > 1:
        return False
    # Both differ by exactly one word, so everything turns on those two words.
    # pow10 against power is one record spelled twice; item against class is
    # two records that happen to share a naming shape, and flagging every pair
    # in an _entry family would bury the real duplicates in noise.
    x, y = odd_a[0], odd_b[0]
    if x.startswith(y) or y.startswith(x):
        return True
    common = 0
    for cx, cy in zip(x, y):
        if cx != cy:
            break
        common += 1
    return common >= 3


def load_verdicts():
    if not os.path.isdir(VERDICTS):
        return []
    out = []
    for entry in sorted(os.listdir(VERDICTS)):
        if not entry.endswith(".json"):
            continue
        try:
            with open(os.path.join(VERDICTS, entry), encoding="utf-8") as fh:
                out.append((entry[:-5], json.load(fh)))
        except Exception:                                     # noqa: BLE001
            continue
    return out


def main():
    os.makedirs(STRUCTS, exist_ok=True)
    proposals = {}

    for name_entry in SEEDS:
        proposals[name_entry["name"]] = {
            "name": name_entry["name"],
            "from": ["ticket seed"],
            "hint": name_entry["hint"],
            "strides": [],
        }

    for addr, verdict in load_verdicts():
        cand = verdict.get("struct_candidate") or {}
        name = (cand.get("name") or "").strip()
        if not name:
            continue
        rec = proposals.setdefault(name, {"name": name, "from": [], "hint": "", "strides": []})
        rec["from"].append(addr)
        stride = cand.get("stride") or 0
        if stride and stride not in rec["strides"]:
            rec["strides"].append(stride)
        if not rec["hint"]:
            rec["hint"] = cand.get("evidence") or ""

    # Agents judging one anchor each cannot see what the others proposed, so the
    # same record arrives under two spellings -- crt_scale10_pow10_entry and
    # crt_scale10_power_entry, tm and crt_tm. Merging them is a judgement about
    # what the records are, so this only measures the resemblance and hands it to
    # the agent that lays the struct out; it does not merge anything itself.
    for name in proposals:
        proposals[name]["similar"] = sorted(
            other for other in proposals
            if other != name and resembles(name, other))

    todo = []
    settled = []
    aliases = {}
    for name in sorted(proposals):
        path = os.path.join(STRUCTS, name + ".json")
        if not os.path.isfile(path):
            todo.append(name)
            continue
        settled.append(name)
        try:
            with open(path, encoding="utf-8") as fh:
                alias_of = (json.load(fh).get("alias_of") or "").strip()
        except Exception:                                     # noqa: BLE001
            alias_of = ""
        if alias_of:
            aliases[name] = alias_of

    out = {
        "todo": todo,
        "settled": settled,
        "aliases": aliases,
        "proposals": [proposals[n] for n in sorted(proposals)],
        "counts": {"total": len(proposals), "todo": len(todo), "settled": len(settled),
                   "aliases": len(aliases)},
    }
    path = os.path.join(WORK, "structs.json")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2)

    print("struct candidates  %d" % len(proposals))
    print("settled            %d" % len(settled))
    print("aliases folded in  %d" % len(aliases))
    for name, target in sorted(aliases.items()):
        print("  %s -> %s" % (name, target))
    print("todo               %d" % len(todo))
    for name in todo:
        rec = proposals[name]
        line = "  %-32s from %s" % (name, ", ".join(rec["from"][:4]))
        if rec["similar"]:
            line += "   (resembles %s)" % ", ".join(rec["similar"])
        print(line)
    print("wrote %s" % path)


if __name__ == "__main__":
    main()
