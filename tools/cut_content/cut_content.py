"""Common entry for the cut_content/ knowledge-base folder (tickets 25.10-25.14).

    python tools/cut_content/cut_content.py index
    python tools/cut_content/cut_content.py check
    python tools/cut_content/cut_content.py media  <topic> [--game DIR]
    python tools/cut_content/cut_content.py verify-media [<topic>...] [--game DIR]

index         regenerate the summary table in cut_content/_index.md from the
              entries of every topic page.
check         the structural gate: entry headings and categories, unique ids,
              no id both listed and excluded, the summary table in sync, every
              relative link resolving, no workspace/ citation, every media file
              named by the rule, belonging to an entry and referenced by its page.
media         regenerate one topic's PNG/WAV files into cut_content/media/<topic>/
              from the original game files (the folder is replaced as a whole).
verify-media  regenerate into a scratch folder and compare byte for byte with
              what is committed; this is how "regenerating gives the same result"
              is proved.

A topic's media generator is a function (out_dir, game_dir) -> None registered
in GENERATORS below.  Tickets 25.11-25.14 add theirs; they read game files
through read_game_file / read_vfs_member and write through write_png / write_wav
(the owners are tools/cel_decode, tools/saf_decode and tools/vfs_dump).

CD audio tracks are too large for version control: they go to
cut_content/media/cdda/, which .gitignore excludes and the gate skips.
"""

import argparse
import filecmp
import re
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent.parent
ROOT = TOOLS_DIR.parent
CUT_DIR = ROOT / "cut_content"
DEFAULT_GAME = ROOT / "fdps_game_files"

# topic key -> (page, id prefix, label used in the summary table)
TOPICS = {
    "code": ("code.md", "C", "程式痕跡"),
    "units": ("units.md", "U", "角色、敵人、職業"),
    "items": ("items.md", "I", "道具"),
    "battle_assets": ("battle_assets.md", "B", "戰鬥資源"),
    "story": ("story.md", "S", "劇情與場景"),
}
PREFIX_ORDER = [prefix for _, prefix, _ in TOPICS.values()]

# The four classes of CONTEXT.md, plus the negative conclusions that live on the
# topic pages.  Exclusions are not a topic category: they are listed only in the
# exclusion section of _index.md, one line of reason each.
CATEGORIES = ("殘留內容", "空殼", "被封住的內容", "前作遺留", "否定性結論")

INDEX_HEADER = ["編號", "分類", "內容", "主題檔"]
EXCLUSION_HEADING = "## 排除清單"
MEDIA_DIR = "media"
CDDA_DIR = "cdda"          # gitignored, never checked
MEDIA_NAME = re.compile(r"^([a-z]\d+[a-z]?)-[a-z0-9]+(?:-[a-z0-9]+)*\.(?:png|wav)$")

HEADING = re.compile(r"^### ([A-Z]\d+[a-z]?) (.+?)\s*$")
CATEGORY_LINE = re.compile(r"^分類：([^｜|]+)")
LINK = re.compile(r"\]\(([^)\s]+)\)")

GENERATORS = {}


@dataclass
class Entry:
    id: str
    title: str
    category: str
    topic: str


def id_key(entry_id):
    """Sort key: topic order of the prefix, then the number, then the suffix."""
    match = re.match(r"([A-Z])(\d+)([a-z]?)$", entry_id)
    prefix, number, suffix = match.groups()
    rank = PREFIX_ORDER.index(prefix) if prefix in PREFIX_ORDER else len(PREFIX_ORDER)
    return (rank, int(number), suffix)


def parse_topic(text, topic):
    """Return (entries, problems) for one topic page."""
    _, prefix, _ = TOPICS[topic]
    entries, problems = [], []
    lines = text.split("\n")
    for i, line in enumerate(lines):
        match = HEADING.match(line)
        if not match:
            continue
        entry_id, title = match.groups()
        if not entry_id.startswith(prefix):
            problems.append(f"{topic}: entry {entry_id} does not carry this page's prefix {prefix}")
        following = next((l for l in lines[i + 1:] if l.strip()), "")
        category = CATEGORY_LINE.match(following)
        if not category:
            problems.append(f"{topic}: entry {entry_id} has no '分類：' line right after its heading")
            continue
        name = category.group(1).strip()
        if name not in CATEGORIES:
            problems.append(f"{topic}: entry {entry_id} has category {name!r}, "
                            f"not one of {', '.join(CATEGORIES)}")
            continue
        entries.append(Entry(entry_id, title, name, topic))
    return entries, problems


def index_table(entries):
    rows = ["| " + " | ".join(INDEX_HEADER) + " |", "| --- | --- | --- | --- |"]
    for e in sorted(entries, key=lambda e: id_key(e.id)):
        page, _, label = TOPICS[e.topic]
        rows.append(f"| {e.id} | {e.category} | {e.title} | [{label}]({page}) |")
    return "\n".join(rows) + "\n"


def split_row(line):
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def apply_index(text, table):
    """Put the summary table over the existing one (found by its header)."""
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if line.startswith("|") and split_row(line) == INDEX_HEADER:
            end = i
            while end < len(lines) and lines[end].startswith("|"):
                end += 1
            return "\n".join(lines[:i] + table.rstrip("\n").split("\n") + lines[end:])
    raise ValueError("no summary table with header " + " | ".join(INDEX_HEADER))


def exclusion_ids(text):
    """Ids in the first column of the tables under the exclusion heading."""
    ids, inside = [], False
    for line in text.split("\n"):
        if line.startswith("## "):
            inside = line.strip() == EXCLUSION_HEADING
            continue
        if inside and line.startswith("|"):
            first = split_row(line)[0]
            if re.fullmatch(r"[A-Z]\d+[a-z]?", first):
                ids.append(first)
    return ids


def pages(base):
    return sorted(p for p in base.glob("*.md"))


def collect(base):
    entries, problems = [], []
    for topic, (page, _, _) in TOPICS.items():
        path = base / page
        if path.exists():
            found, bad = parse_topic(path.read_text(encoding="utf-8"), topic)
            entries += found
            problems += bad
    return entries, problems


def check_tree(base):
    """The structural gate over a cut_content/ folder; returns problem lines."""
    base = Path(base)
    entries, problems = collect(base)
    index_path = base / "_index.md"
    index_text = index_path.read_text(encoding="utf-8")

    seen = {}
    for e in entries:
        if e.id in seen:
            problems.append(f"{e.id} appears twice ({seen[e.id]} and {e.topic})")
        seen[e.id] = e.topic
    for entry_id in exclusion_ids(index_text):
        if entry_id in seen:
            problems.append(f"{entry_id} is both an entry of {seen[entry_id]} and in the exclusion list")
        seen.setdefault(entry_id, "exclusion list")

    try:
        if apply_index(index_text, index_table(entries)) != index_text:
            problems.append("_index.md summary table is stale; run: cut_content.py index")
    except ValueError as error:
        problems.append(f"_index.md: {error}")

    for path in pages(base):
        text = path.read_text(encoding="utf-8")
        if "workspace/" in text or "workspace\\" in text:
            problems.append(f"{path.name} cites workspace/, which the knowledge base must not")
        for target in LINK.findall(text):
            if re.match(r"[a-z]+:", target) or target.startswith("#"):
                continue
            local = target.split("#", 1)[0]
            if local and not (path.parent / local).exists():
                problems.append(f"{path.name}: link target {target} does not exist")

    problems += check_media(base, entries)
    return problems


def check_media(base, entries):
    problems = []
    media_root = base / MEDIA_DIR
    if not media_root.exists():
        return problems
    ids = {e.id.lower(): e for e in entries}
    for folder in sorted(p for p in media_root.iterdir() if p.is_dir()):
        if folder.name == CDDA_DIR:
            continue
        if folder.name not in TOPICS:
            problems.append(f"media/{folder.name}/ is not a topic ({', '.join(TOPICS)})")
            continue
        page = base / TOPICS[folder.name][0]
        page_text = page.read_text(encoding="utf-8") if page.exists() else ""
        for path in sorted(folder.rglob("*")):
            if path.is_dir():
                continue
            rel = path.relative_to(base).as_posix()
            match = MEDIA_NAME.match(path.name)
            if not match or path.parent != folder:
                problems.append(f"{rel}: name does not follow <entry id>-<subject>[-<part>].png|wav "
                                f"(lower case, directly in media/{folder.name}/)")
                continue
            owner = ids.get(match.group(1))
            if owner is None or owner.topic != folder.name:
                problems.append(f"{rel}: no entry {match.group(1).upper()} on {page.name}")
            elif rel not in page_text:
                problems.append(f"{rel}: not referenced by {page.name}")
    return problems


# ------------------------------------------------------------------ media

def build_media(topic, base, game_dir, generators=None):
    """Regenerate one topic's media folder from the game files, replacing it."""
    generators = GENERATORS if generators is None else generators
    target = Path(base) / MEDIA_DIR / topic
    if topic not in generators:
        return target
    with tempfile.TemporaryDirectory() as scratch:
        out = Path(scratch) / topic
        out.mkdir()
        generators[topic](out, Path(game_dir))
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(out, target)
    return target


def verify_media(topic, base, game_dir, generators=None):
    """Regenerate into scratch and list every difference from the committed folder."""
    generators = GENERATORS if generators is None else generators
    if topic not in generators:
        return []
    target = Path(base) / MEDIA_DIR / topic
    with tempfile.TemporaryDirectory() as scratch:
        out = Path(scratch) / topic
        out.mkdir()
        generators[topic](out, Path(game_dir))
        made = {p.name for p in out.iterdir()}
        have = {p.name for p in target.iterdir()} if target.exists() else set()
        problems = [f"media/{topic}/{n}: generated but not committed" for n in sorted(made - have)]
        problems += [f"media/{topic}/{n}: committed but no longer generated" for n in sorted(have - made)]
        for name in sorted(made & have):
            if not filecmp.cmp(out / name, target / name, shallow=False):
                problems.append(f"media/{topic}/{name}: differs from a fresh regeneration")
    return problems


# Helpers for generators.  They go through the owners of each format.

def read_game_file(game_dir, name):
    """A top-level game file (FACE.CEL, ICON.CEL, ...), matched case-insensitively."""
    for path in Path(game_dir).iterdir():
        if path.name.upper() == name.upper():
            return path.read_bytes()
    raise FileNotFoundError(f"{name} is not in {game_dir}")


def read_vfs_member(game_dir, container, member):
    """One member of a .VFS container, matched case-insensitively."""
    sys.path.insert(0, str(TOOLS_DIR / "vfs_dump"))
    from vfs_dump import parse_container  # the container's owner
    data = read_game_file(game_dir, container)
    _, entries = parse_container(data, container)
    for entry in entries:
        if entry["name"].upper() == member.upper():
            return data[entry["offset"]:entry["offset"] + entry["size"]]
    raise FileNotFoundError(f"{member} is not in {container}")


def write_png(path, width, height, rows):
    """RGBA rows -> PNG, through tools/cel_decode (deterministic output)."""
    sys.path.insert(0, str(TOOLS_DIR / "cel_decode"))
    from cel_decode import write_png as owner
    owner(Path(path), width, height, rows)


def write_wav(path, sound):
    """A decoded SAF sound -> RIFF/WAVE, through tools/saf_decode."""
    sys.path.insert(0, str(TOOLS_DIR / "saf_decode"))
    from saf_decode import write_wav as owner
    owner(Path(path), sound)


# ------------------------------------------------------------------- main

def cmd_index(_args):
    entries, problems = collect(CUT_DIR)
    if problems:
        for p in problems:
            print("PROBLEM " + p)
        return 1
    path = CUT_DIR / "_index.md"
    text = path.read_text(encoding="utf-8")
    new = apply_index(text, index_table(entries))
    if new != text:
        path.write_text(new, encoding="utf-8", newline="\n")
        print(f"summary table rewritten: {len(entries)} entries")
    else:
        print(f"summary table already current: {len(entries)} entries")
    return 0


def cmd_check(_args):
    problems = check_tree(CUT_DIR)
    for p in problems:
        print("PROBLEM " + p)
    if problems:
        print(f"FAIL: {len(problems)} problems")
        return 1
    entries, _ = collect(CUT_DIR)
    print(f"OK: {len(entries)} entries, {len(exclusion_ids((CUT_DIR / '_index.md').read_text(encoding='utf-8')))} exclusions")
    return 0


def cmd_media(args):
    if args.topic not in TOPICS:
        print(f"unknown topic {args.topic}; one of {', '.join(TOPICS)}")
        return 1
    if args.topic not in GENERATORS:
        print(f"{args.topic}: no media generator registered")
        return 0
    target = build_media(args.topic, CUT_DIR, args.game)
    print(f"{args.topic}: {sum(1 for _ in target.iterdir())} files in {target}")
    return 0


def cmd_verify_media(args):
    problems = []
    for topic in args.topics or list(TOPICS):
        problems += verify_media(topic, CUT_DIR, args.game)
    for p in problems:
        print("DIFF " + p)
    if problems:
        print(f"FAIL: {len(problems)} differences")
        return 1
    print("OK: every registered topic regenerates byte for byte")
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("index").set_defaults(func=cmd_index)
    sub.add_parser("check").set_defaults(func=cmd_check)
    p = sub.add_parser("media")
    p.add_argument("topic")
    p.add_argument("--game", type=Path, default=DEFAULT_GAME)
    p.set_defaults(func=cmd_media)
    p = sub.add_parser("verify-media")
    p.add_argument("topics", nargs="*")
    p.add_argument("--game", type=Path, default=DEFAULT_GAME)
    p.set_defaults(func=cmd_verify_media)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
