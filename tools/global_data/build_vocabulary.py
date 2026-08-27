"""Build the page every ticket-17 agent reads before it names anything.

Three sources, in the order an agent should weigh them:

  1. What FDPS itself already calls things. Names landed by earlier rounds, so
     that round forty does not invent a second word for what round three already
     named.
  2. What the predecessor project settled on. FD2 is the same engine one game
     earlier, and its knowledge base is finished: the layouts in its types.h are
     solved, and its global inventory is 598 lines of names for the very
     subsystems this binary has. Reaching for its word first is not laziness, it
     is the only way the two projects stay comparable.
  3. The naming canon, quoted rather than paraphrased.

Regenerated after every round, so it grows with the run.

Usage:  python tools/global_data/build_vocabulary.py
"""

import collections
import json
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.join(REPO, "workspace", "global_data")
VERDICTS = os.path.join(WORK, "verdicts")
FD2 = r"C:\Users\fdpsf\Documents\fd2-anatomy"

MAX_FD2_NAMES = 160


def fdps_names():
    """What this project has decided so far, grouped by prefix."""
    groups = collections.defaultdict(list)
    if not os.path.isdir(VERDICTS):
        return groups
    for entry in sorted(os.listdir(VERDICTS)):
        if not entry.endswith(".json"):
            continue
        try:
            with open(os.path.join(VERDICTS, entry), encoding="utf-8") as fh:
                verdict = json.load(fh)
        except Exception:                                     # noqa: BLE001
            continue
        name = ((verdict.get("name") or {}).get("verdict") or "").strip()
        if not name:
            continue
        kind = (verdict.get("type") or {}).get("verdict") or ""
        groups[verdict.get("pool") or "unknown"].append(
            (entry[:-5], name, kind))
    return groups


def fd2_globals():
    """FD2's global names with how often its sources mention them."""
    counts = collections.Counter()
    pattern = re.compile(r"data_fd2_[a-z0-9_]+")
    for root, dirs, files in os.walk(FD2):
        dirs[:] = [d for d in dirs
                   if d not in ("legacy", "workspace", "fd2_game_files", ".git")]
        for fn in files:
            if not fn.endswith((".md", ".c", ".h")):
                continue
            try:
                with open(os.path.join(root, fn), encoding="utf-8", errors="ignore") as fh:
                    for line in fh:
                        counts.update(pattern.findall(line))
            except Exception:                                 # noqa: BLE001
                continue
    return counts


def fd2_types():
    path = os.path.join(FD2, "src", "include", "types.h")
    if not os.path.isfile(path):
        return ""
    with open(path, encoding="utf-8", errors="ignore") as fh:
        return fh.read()


def main():
    os.makedirs(WORK, exist_ok=True)
    out = []
    out.append("# Ticket 17 naming vocabulary")
    out.append("")
    out.append("Regenerated after every round. Reach for a word already on this")
    out.append("page before inventing one.")
    out.append("")

    out.append("## The canon, in one paragraph")
    out.append("")
    out.append("Game globals are `data_fdps_` + snake_case. Globals reached only")
    out.append("by the sound library are `data_ail_` + snake_case, or the AIL_")
    out.append("name the library itself uses. CRT globals carry the library's own")
    out.append("symbol where it can be identified, `L$N_<module>_<use>` where it")
    out.append("is a file-static that cannot, and no other prefix. Anything a")
    out.append("linker or compiler emitted -- switch tables, alignment padding,")
    out.append("literal images -- is `binary_artifact_` + description +")
    out.append("`_<address>`, and that family is the only one allowed an address")
    out.append("in its name. THERE IS NO BARE `crt_` PREFIX IN THIS PROJECT: the")
    out.append("only legal form is the compound `crt_equivalent_`, for something")
    out.append("hand written because it could not be matched to a library")
    out.append("object. Struct type names take the same two rules -- a game")
    out.append("record is `fdps_` + snake_case, a CRT or AIL record carries the")
    out.append("library's own name with no prefix (`tm`, `FILE`, `_iobuf`,")
    out.append("`rt_init`), and only a structure whose library name cannot be")
    out.append("identified falls back to `L$N_<module>_<use>`. No `_t` suffix.")
    out.append("The Ghidra name and the future C name are the same string, so it")
    out.append("has to be a legal C identifier. Full text: rebuild_info/naming.md.")
    out.append("")

    groups = fdps_names()
    total = sum(len(v) for v in groups.values())
    out.append("## Named in FDPS so far (%d)" % total)
    out.append("")
    if not total:
        out.append("Nothing yet -- this is the first round.")
    for pool in sorted(groups):
        rows = sorted(groups[pool], key=lambda r: r[1])
        out.append("### pool %s (%d)" % (pool, len(rows)))
        out.append("")
        out.append("| addr | name | type |")
        out.append("| --- | --- | --- |")
        for addr, name, kind in rows:
            out.append("| `%s` | `%s` | `%s` |" % (addr, name, kind))
        out.append("")

    counts = fd2_globals()
    out.append("## What FD2 called the same things (top %d of %d)"
               % (min(MAX_FD2_NAMES, len(counts)), len(counts)))
    out.append("")
    out.append("FD2 is this engine one game earlier and its naming is finished.")
    out.append("Where FDPS has the same concept, use FD2's word with `fd2`")
    out.append("swapped for `fdps` -- and say in the evidence that you did, so a")
    out.append("later reader can check the two against each other. Where FDPS")
    out.append("genuinely differs, do not force the analogy.")
    out.append("")
    for name, n in counts.most_common(MAX_FD2_NAMES):
        out.append("- `%s` (%d mentions)" % (name, n))
    out.append("")

    types = fd2_types()
    if types:
        out.append("## FD2's solved layouts")
        out.append("")
        out.append("Verbatim from `%s`. The unit record in FDPS is 0x50 bytes,"
                   % os.path.join(FD2, "src", "include", "types.h"))
        out.append("the same size as FD2's `runtime_char`, which makes this the")
        out.append("first thing to check a field against -- and the first thing")
        out.append("to be sceptical about, because a matching size is not a")
        out.append("matching layout. Confirm every field against FDPS's own")
        out.append("code before you carry it over, and say in the evidence which")
        out.append("fields you confirmed and which you assumed.")
        out.append("")
        out.append("```c")
        out.append(types.rstrip())
        out.append("```")
        out.append("")

    path = os.path.join(WORK, "vocabulary.md")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(out) + "\n")
    print("wrote %s (%d FDPS names, %d FD2 names)" % (path, total, len(counts)))


if __name__ == "__main__":
    main()
