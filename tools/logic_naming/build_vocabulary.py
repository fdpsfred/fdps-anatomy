"""Collect the naming vocabulary a ticket-15 agent should be reaching for.

An agent that reads one function has no way to see how the rest of the program
was named, so left alone it invents its own dialect: one pass writes
fdps_draw_unit_status_panel, the next writes fdps_render_char_info_box for the
neighbouring function. The fix is to hand every agent the same page -- what this
program's already-settled names look like, and what the previous game's project
settled on for the same subsystems.

Two sources, both already in the repo:
  * the pool_fdps functions of FDPS.LE that already carry a semantic name, with
    the first line of their plate comment, from this ticket's own dump.
  * the FD2 project's symbol table, which named roughly a thousand functions of
    the predecessor game with the same conventions and the same domain.

Output is a Markdown page under workspace/logic_naming/. It is a reference, not
a lookup table: FDPS is a different binary and a name that fits FD2's function
is evidence about wording, never about identity.

Usage:
  python tools/logic_naming/build_vocabulary.py [--dump DIR] [--out FILE]
"""

import argparse
import json
import os
import re
import sys
from collections import Counter

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FD2_SYMBOLS = r"C:\Users\fdpsf\Documents\fd2-anatomy\tools\src_refine\data\src_info_by_name.json"
DEFAULT_DUMP = os.path.join(REPO, "workspace", "logic_naming", "dump")
DEFAULT_OUT = os.path.join(REPO, "workspace", "logic_naming", "vocabulary.md")


def head_line(text, limit=150):
    if not text:
        return ""
    line = text.split("\n", 1)[0].strip()
    return line[:limit] + "..." if len(line) > limit else line


def fd2_words():
    """The verbs and nouns FD2's names are built from, by frequency."""
    if not os.path.isfile(FD2_SYMBOLS):
        return None, None
    with open(FD2_SYMBOLS, encoding="utf-8") as fh:
        names = json.load(fh)
    verbs, nouns = Counter(), Counter()
    for name in names:
        if not name.startswith("fd2_"):
            continue
        parts = name[4:].split("_")
        if not parts:
            continue
        verbs[parts[0]] += 1
        for p in parts[1:]:
            if len(p) > 2 and not p.isdigit():
                nouns[p] += 1
    return verbs, nouns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()

    fpath = os.path.join(args.dump, "functions.json")
    if not os.path.isfile(fpath):
        sys.exit("no dump at %s -- run DumpNamingState.java first" % fpath)
    with open(fpath, encoding="utf-8") as fh:
        dump = json.load(fh)

    named = []
    for meta in dump["functions"]:
        if meta["default_name"]:
            continue
        ctx_path = os.path.join(args.dump, "ctx", meta["addr"] + ".json")
        summary = ""
        if os.path.isfile(ctx_path):
            with open(ctx_path, encoding="utf-8") as fh:
                summary = head_line(json.load(fh).get("current_plate", ""))
        named.append((meta["name"], meta["addr"], summary))
    named.sort()

    verbs, nouns = fd2_words()

    out = []
    out.append("# Naming vocabulary for pool_fdps\n")
    out.append("Regenerate with `python tools/logic_naming/build_vocabulary.py`.\n")
    out.append("\nThe canon is [`rebuild_info/naming.md`](../../rebuild_info/naming.md); "
               "this page only shows what the canon looks like when it is applied. "
               "Reach for a word that is already in use before inventing one.\n")

    out.append("\n## Already named in this program\n")
    out.append("\n| Name | Address | What it does |\n| --- | --- | --- |\n")
    for name, addr, summary in named:
        out.append("| `%s` | `%s` | %s |\n" % (name, addr, summary.replace("|", "\\|")))

    if verbs:
        out.append("\n## Leading verbs, from the FD2 project's %d names\n\n"
                   % sum(verbs.values()))
        out.append("The predecessor game, named by the same conventions in the same "
                   "domain. Wording evidence only -- a matching FD2 name never "
                   "establishes that this function is that function.\n\n")
        out.append("| Verb | Uses | Verb | Uses | Verb | Uses |\n"
                   "| --- | ---: | --- | ---: | --- | ---: |\n")
        top = [(v, c) for v, c in verbs.most_common(60)]
        for i in range(0, len(top), 3):
            row = top[i:i + 3]
            while len(row) < 3:
                row.append(("", ""))
            out.append("| %s |\n" % " | ".join(
                ("`%s`" % v if v else "") + " | " + str(c) for v, c in row))

        out.append("\n## Domain nouns in FD2 names\n\n")
        out.append(", ".join("`%s`" % n for n, _ in nouns.most_common(90)) + "\n")
        out.append("\nThe full FD2 table is at `%s`; grep it when a function looks "
                   "like something the previous game also had.\n" % FD2_SYMBOLS)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write("".join(out))
    print("named functions listed = %d" % len(named))
    print("fd2 vocabulary = %s" % ("yes" if verbs else "not found, skipped"))
    print("vocabulary -> %s" % os.path.abspath(args.out))


if __name__ == "__main__":
    main()
