"""The story-and-scenes page of cut_content/ (ticket 25.14): generated blocks,
media and CD audio.

    python tools/cut_content/story.py build              rewrite the generated blocks of story.md
    python tools/cut_content/story.py check [--final]    blocks current + never-shown text owned
    python tools/cut_content/story.py cdda [--disc1 CUE] [--disc2 CUE]

cut_content/story.md is prose written by hand around blocks this script
generates, fenced as

    <!-- story:KEY -->
    ...
    <!-- /story:KEY -->

The blocks hold what can be read straight off the shipped game files, so they
are never typed: full transcripts of every never-shown text entry an entry
talks about, deployment tables, the index of every never-shown text entry and
the media tables.  `build` rewrites them; `check` regenerates and compares, and
is the gate for the page together with cut_content.py check.

Never-shown text ownership.  cut_content/ owns "text that is never shown"
(ticket 25).  Every such entry must belong to exactly one entry of the page or
one exclusion of cut_content/_index.md; OWNERS below says which.  Which entries
are never shown comes from
    FDETXT00     global_text.NO_READER (the entries the source scan finds no reader for)
    FDETXT31-65  global_text.classify_scene_block over the cutscene_script traces
    FDETXT01-30  the never_shown lists of tools/chapter_docs/judgements/chNN.json
                 (ticket 25.8's per-chapter judgements); a chapter without one
                 is "unsettled" and its owners are only listed, not checked
An owner written "?REASON" is pending; `check --final` refuses pending owners.

Media (`cut_content.py media story`, registered in cut_content.GENERATORS):
whole-map PNGs of the maps no path reaches, every frame of the unused
ICON0032.SAF with a sheet of it and of ICON0033 for comparison, its two sounds,
and the sheets of M09.CEL and M090.CEL.

CD audio: `cdda` cuts disc 1 track 12 and disc 2 tracks 15 and 16 out of the
disc images into cut_content/media/cdda/ (gitignored) as 44.1 kHz 16-bit
stereo WAV, the raw CD-DA samples unchanged.
"""

import argparse
import atexit
import difflib
import json
import re
import shutil
import struct
import sys
import tempfile
from functools import lru_cache
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = TOOLS.parent
for _sub in ("text_decode", "cutscene_script", "global_text", "map_decode", "cel_decode",
             "saf_decode", "vfs_dump", "data_tables", "cd_scope"):
    sys.path.insert(0, str(TOOLS / _sub))
sys.path.insert(0, str(HERE))

import cut_content  # noqa: E402  (owner of the cut_content/ page structure)
import text_decode  # noqa: E402  (owner of the text block format)

PAGE = cut_content.CUT_DIR / "story.md"
JUDGEMENTS = TOOLS / "chapter_docs" / "judgements"
DEFAULT_GAME = cut_content.DEFAULT_GAME
DISC_DIR = Path(r"D:\Game\Flame Dragon\fdps_image")
CDDA_OUT = cut_content.CUT_DIR / cut_content.MEDIA_DIR / cut_content.CDDA_DIR

MARKER = re.compile(r"(<!-- story:([\w-]+) -->\n)(.*?)(<!-- /story:\2 -->)", re.S)
FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK = 31, 65
CHAPTER_BLOCKS = range(1, 31)
RAW_SECTOR = 2352
TILE = 24


class StoryError(Exception):
    """The game data does not hold what this page relies on."""


# ---------------------------------------------------------------------------
# Generated blocks inside the page
# ---------------------------------------------------------------------------

def apply_blocks(text, blocks):
    """Put each generated block between its markers.  Returns (text, problems)."""
    problems = []
    seen = set()

    def replace(m):
        key = m.group(2)
        seen.add(key)
        if key not in blocks:
            problems.append(f"marker {key} has no generated block")
            return m.group(0)
        body = blocks[key].rstrip("\n")
        return m.group(1) + (body + "\n" if body else "") + m.group(4)

    new = MARKER.sub(replace, text)
    problems += [f"block {key} has no marker in the page" for key in blocks if key not in seen]
    return new, problems


def transcript(entry, glyphs, name_of):
    """One text entry as lines: 【speaker】 lines, a line per {br}, ▼ per {page}."""
    out, line = [], ""
    for t in entry.tokens:
        if t.kind == "line_break":
            out.append(line)
            line = ""
        elif t.kind == "page_break":
            out.append(line)
            out.append("▼")
            line = ""
        elif t.kind in ("speaker_char", "speaker_unit"):
            if line:
                out.append(line)
            out.append(f"【{name_of(t.operand)}】（角色 {t.operand}）" if t.kind == "speaker_char"
                       else f"【地圖單位 {t.operand}】")
            line = ""
        else:
            line += text_decode.render_token(t, glyphs)
    if line:
        out.append(line)
    while out and out[-1] == "":
        out.pop()
    return out


def check_ownership(never_shown, owners, known_ids, unsettled_blocks):
    """The never-shown text gate.  Returns (problems, pending).

    never_shown: {(block, entry)} known never to be shown.  owners: {(block,
    entry): id or "?reason"}.  A block in unsettled_blocks has no settled list
    yet, so its owners are pending and nothing is claimed about it."""
    problems, pending = [], []
    for key in sorted(set(owners) | set(never_shown)):
        block, entry = key
        where = f"FDETXT{block:02d} 0x{entry:02x}"
        owner = owners.get(key)
        if block in unsettled_blocks:
            if owner is not None:
                pending.append((key, "chapter judgement"))
                if not owner.startswith("?") and owner not in known_ids:
                    problems.append(f"{where} names {owner}, which is neither an entry "
                                    "nor an exclusion")
            continue
        if owner is None:
            problems.append(f"{where} is never shown and has no owner")
            continue
        if owner.startswith("?"):
            pending.append((key, owner[1:]))
            if key not in never_shown:
                problems.append(f"{where} is pending ({owner[1:]}) but is shown")
            continue
        if owner not in known_ids:
            problems.append(f"{where} names {owner}, which is neither an entry nor an exclusion")
        if key not in never_shown:
            problems.append(f"{where} is owned by {owner} but is shown")
    return problems, pending


# ---------------------------------------------------------------------------
# CD audio
# ---------------------------------------------------------------------------

def track_span(tracks, number, image_size):
    """(start, end) byte offsets of audio track `number` in a single-file .bin."""
    ordered = sorted(tracks, key=lambda t: t[2])
    for i, (no, mode, lba) in enumerate(ordered):
        if no != number:
            continue
        if mode != "AUDIO":
            raise ValueError(f"track {number} is {mode}, not audio")
        end = ordered[i + 1][2] * RAW_SECTOR if i + 1 < len(ordered) else image_size
        return lba * RAW_SECTOR, end
    raise ValueError(f"no track {number}")


def write_cdda_wav(path, samples):
    """Raw CD-DA (44.1 kHz, 16-bit little-endian, stereo) as a RIFF/WAVE file."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", 36 + len(samples)) + b"WAVEfmt "
                 + struct.pack("<IHHIIHH", 16, 1, 2, 44100, 176400, 4, 16)
                 + b"data" + struct.pack("<I", len(samples)))
        fh.write(samples)


CDDA_TRACKS = ((1, 12), (2, 15), (2, 16))


def cut_cdda(cues, out_dir=CDDA_OUT):
    """Write every CDDA_TRACKS track as out_dir/disc<n>-track<t>.wav."""
    from inventory_discs import parse_cue  # the .cue reader's owner (tools/cd_scope)
    written = []
    for disc, track in CDDA_TRACKS:
        bin_path, tracks = parse_cue(cues[disc])
        size = bin_path.stat().st_size
        start, end = track_span(tracks, track, size)
        with open(bin_path, "rb") as fh:
            fh.seek(start)
            samples = fh.read(end - start)
        path = Path(out_dir) / f"disc{disc}-track{track:02d}.wav"
        write_cdda_wav(path, samples)
        written.append((path, (end - start) / 176400))
    return written


# ---------------------------------------------------------------------------
# Game data
# ---------------------------------------------------------------------------

DUMPED = ("FIELD.VFS", "FIELD1.VFS", "FIELD2.VFS", "MISC.VFS")


@lru_cache(maxsize=None)
def dump_tree(game_dir):
    """The containers map_decode and data_tables read, unpacked into a scratch
    tree laid out like tools/vfs_dump's output, straight from the game files."""
    from vfs_dump import parse_container
    root = Path(tempfile.mkdtemp(prefix="cut_story_"))
    atexit.register(shutil.rmtree, root, True)
    for container in DUMPED:
        data = cut_content.read_game_file(game_dir, container)
        _, entries = parse_container(data, container)
        folder = root / container.split(".")[0]
        folder.mkdir()
        for e in entries:
            (folder / e["name"]).write_bytes(data[e["offset"]:e["offset"] + e["size"]])
    return root


class Game:
    """Everything the page's blocks are generated from."""

    def __init__(self, game_dir):
        import cutscene_script
        self.game_dir = Path(game_dir)
        self.field = cutscene_script.read_container(self.game_dir / "FIELD.VFS")
        self.glyphs = text_decode.load_glyph_table()

    @lru_cache(maxsize=None)
    def block(self, n):
        return text_decode.parse_block(self.field[f"FDETXT{n:02d}.TXT"])

    def line(self, n, i):
        return text_decode.render_entry(self.block(n)[i], self.glyphs)

    @lru_cache(maxsize=None)
    def tables(self):
        import data_tables
        return data_tables.load(dump_tree(self.game_dir))

    def name(self, char_id):
        return self.tables().unit_label(char_id)

    def transcript(self, n, i):
        return transcript(self.block(n)[i], self.glyphs, self.name)

    @lru_cache(maxsize=None)
    def scripts(self):
        import cutscene_script
        return cutscene_script.decode_all(self.game_dir, cutscene_script.scan_callers())

    @lru_cache(maxsize=None)
    def map(self, n):
        import map_decode
        return map_decode.load_map(dump_tree(self.game_dir), n)

    def raw_map_dat(self, n):
        return self.field[f"MAP{n:02d}.DAT"]

    def spawns_past_count(self, n):
        """Every whole 0x1A-byte record in MAPnn.DAT, the count byte ignored."""
        import map_decode
        raw = bytearray(self.raw_map_dat(n))
        raw[2] = (len(raw) - map_decode.DAT_SPAWNS) // map_decode.SPAWN_BYTES
        return map_decode.parse_map_dat(bytes(raw), f"MAP{n:02d}.DAT")["spawns"]


# ---------------------------------------------------------------------------
# Which text is never shown, and who owns it
# ---------------------------------------------------------------------------

S1_TEXT = [(50, e) for e in range(0x09, 0x0E)] + [(7, 0x0E), (7, 0x0F)]

S4_GROUPS = (
    ([(3, 0x0F)], "亞克入隊後，蘭迪斯與尤利安的歡迎"),
    ([(9, 0x11), (56, 0x0D)], "蓋亞與布蘭多"),
    ([(19, 0x0B)], "蘭斯洛特的入隊宣言"),
    ([(21, 0x09), (52, 0x09)], "亞克說這裡祀奉平衡之神"),
    ([(22, 0x0B)], "法蓮娜分失魂藥"),
    ([(24, 0x0D)], "珊來遲一步"),
    ([(24, 0x0F)], "蘭迪斯的沉默"),
    ([(25, 0x0B)], "魔戰將軍撤退"),
    ([(25, 0x0C)], "魔戰將軍撤退"),
    ([(25, 0x0D)], "窮寇莫追"),
    ([(27, 0x0C)], "逃出崩塌的神殿"),
    ([(27, 0x0D)], "被打昏"),
    ([(27, 0x15)], "幹部臨死台詞"),
    ([(27, 0x16)], "幹部臨死台詞"),
    ([(27, 0x17)], "幹部臨死台詞"),
    ([(27, 0x18)], "幹部臨死台詞"),
    ([(30, 0x0D)], "第二型態平衡之神的叫陣"),
    ([(30, 0x0E)], "第三型態平衡之神喊出鬼動死靈陣"),
    ([(30, 0x13)], "另一版的道別（前半）"),
    ([(30, 0x14)], "另一版的道別（後半）"),
)


S5_BLOCKS = (53, 54, 57, 58, 59, 60, 61)


def _span(owner, block, first, last=None):
    return [((block, e), owner) for e in range(first, (first if last is None else last) + 1)]


def _owners():
    rows = []
    rows += _span("S14", 0, 0x000)
    # S1: map 49 and the other wording kept in chapter 7's block
    rows += [(key, "S1") for key in S1_TEXT]
    # S2: the blocks of maps 31 and 33
    rows += _span("S2", 32, 0x09, 0x12) + _span("S2", 34, 0x09, 0x12)
    # S4: written dialogue nothing draws
    rows += [(key, "S4") for keys, _ in S4_GROUPS for key in keys]
    rows += _span("S4a", 27, 0x1C)
    # S5: the cut-scene blocks that are copies of a chapter block, header and
    # village lines included
    for block in S5_BLOCKS:
        rows += _span("S5", block, 0x00, 0x08)
    # S13a: the chapter title text of every chapter block
    for n in CHAPTER_BLOCKS:
        rows += _span("S13a", n, 0x00)
    # S13b: chapter-block drafts whose used version is in a cut-scene block
    for block, first, last in ((1, 0x0E, None), (6, 0x0B, 0x0C), (7, 0x09, 0x0A),
                               (8, 0x09, None), (9, 0x09, 0x10), (9, 0x14, 0x16),
                               (10, 0x09, 0x11), (12, 0x09, 0x0B), (18, 0x0F, None),
                               (19, 0x0C, None), (20, 0x09, 0x0D), (21, 0x0B, 0x13),
                               (23, 0x0E, 0x13), (25, 0x09, None), (25, 0x0E, 0x11),
                               (27, 0x13, None), (30, 0x15, 0x1F)):
        rows += _span("S13b", block, first, last)
    # S15: copies of an entry shown elsewhere
    rows += _span("S15", 1, 0x04, 0x06)
    rows += _span("S15", 41, 0x09) + _span("S15", 41, 0x16)
    rows += _span("S15", 42, 0x09, 0x16) + _span("S15", 43, 0x09, 0x16)
    rows += _span("S15", 52, 0x0A)
    rows += _span("S15", 59, 0x0E, 0x11)
    rows += _span("S15", 62, 0x0B)
    rows += _span("S15", 64, 0x17, 0x21)
    # pending: the new traces the ticket-25.14 workflow judges
    for block in list(range(41, 53)) + [55, 56] + list(range(62, 66)):
        rows += _span("?T14-02/T14-12", block, 0x00, 0x03)
    rows += _span("?T14-10", 63, 0x09, 0x0B)
    owners = {}
    for key, owner in rows:
        if key in owners:
            raise StoryError(f"FDETXT{key[0]:02d} 0x{key[1]:02x} is owned twice "
                             f"({owners[key]} and {owner})")
        owners[key] = owner
    return owners


OWNERS = _owners()

# Owners whose entries must be verbatim copies of an entry that is shown.
COPY_OWNERS = {"S15"}
# ...which may differ a little from their closest shown twin, down to this share.
NEAR_COPY_MIN = 0.8
# Owners whose rows in the text index name the closest shown twin.
TWIN_NOTE_OWNERS = COPY_OWNERS | {"S13b"}


def scene_shown(game):
    """{(block, entry)} of FDETXT31-65 that some script shows."""
    import global_text
    refs = global_text.scene_refs(game.scripts())
    shown = set()
    for b in range(FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK + 1):
        texts = [game.line(b, i) for i in range(len(game.block(b)))]
        s, _, _ = global_text.classify_scene_block(texts, refs.get(b, []))
        shown |= {(b, e) for e in s}
    return shown


def chapter_judgement(n):
    path = JUDGEMENTS / f"ch{n:02d}.json"
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


# Entries a chapter judgement leaves out of never_shown although nothing shows
# them, with the reason.  16:0x00: the source scan counts the wandering smith's
# reply draw as a reader of 0, but that draw only runs when the reply id is not
# CH16_NO_SWORD_FOUND (0), so it draws 0x0d or 0x0e and never 0x00
# (src/chevt3.c fdps_chapter_16_event_wandering_smith_forge).
NEVER_SHOWN_OVERRIDES = {(16, 0x00)}


def never_shown_text(game):
    """({(block, entry)} never shown, {unsettled chapter blocks})."""
    import global_text
    never = {(0, e) for e in global_text.NO_READER}
    shown = scene_shown(game)
    for b in range(FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK + 1):
        for i in range(len(game.block(b))):
            if (b, i) not in shown and game.line(b, i):
                never.add((b, i))
    unsettled = set()
    for n in CHAPTER_BLOCKS:
        j = chapter_judgement(n)
        if j is None:
            unsettled.add(n)
            continue
        for item in j.get("never_shown", []):
            never.add((n, int(str(item["entry"]), 16)))
        never |= {key for key in NEVER_SHOWN_OVERRIDES if key[0] == n}
    return never, unsettled


def all_shown(game, never, unsettled):
    """Every non-empty entry not known to be never shown: the candidates for
    "the twin that is shown".  In an unsettled chapter block, every entry no
    owner claims counts, which is exact once the block is settled."""
    out = set()
    for n in range(0, LAST_SCENE_BLOCK + 1):
        for i in range(len(game.block(n))):
            gone = (n, i) in OWNERS if n in unsettled else (n, i) in never
            if not gone and game.line(n, i):
                out.add((n, i))
    return out


def plain(text):
    return re.sub(r"\{[^}]*\}", "", text)


def coverage(a, b):
    """Share of a's characters (control tags dropped) matched in b."""
    a, b = plain(a), plain(b)
    if not a:
        return 0.0
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return sum(m.size for m in sm.get_matching_blocks()) / len(a)


def twin(game, key, shown):
    """(kind, (block, entry)) of the closest shown entry: kind is 'same' for a
    verbatim copy, else the coverage."""
    text = game.line(*key)
    same = sorted(k for k in shown if game.line(*k) == text)
    if same:
        return "same", same[0]
    best = max(sorted(shown), key=lambda k: coverage(text, game.line(*k)))
    return coverage(text, game.line(*best)), best


def known_ids():
    entries, _ = cut_content.collect(cut_content.CUT_DIR)
    index = (cut_content.CUT_DIR / "_index.md").read_text(encoding="utf-8")
    return {e.id for e in entries} | set(cut_content.exclusion_ids(index))


def ownership_problems(game):
    never, unsettled = never_shown_text(game)
    problems, pending = check_ownership(never, OWNERS, known_ids(), unsettled)
    shown = all_shown(game, never, unsettled)
    for key, owner in sorted(OWNERS.items()):
        if owner not in COPY_OWNERS or key[0] in unsettled:
            continue
        kind, other = twin(game, key, shown)
        if kind != "same" and kind < NEAR_COPY_MIN:
            problems.append(f"FDETXT{key[0]:02d} 0x{key[1]:02x} is owned by {owner} as a copy, "
                            f"but its closest shown twin FDETXT{other[0]:02d} "
                            f"0x{other[1]:02x} covers only {kind:.2f}")
    return problems, pending, never, unsettled, shown


# ---------------------------------------------------------------------------
# Block rendering
# ---------------------------------------------------------------------------

def ref(block, entry):
    return f"`FDETXT{block:02d}` `0x{entry:02x}`"


def fence(lines):
    return ["```text"] + (lines or ["（空字串）"]) + ["```"]


def text_section(game, keys, title=None):
    """Transcript of the first key; the others are stated identical to it."""
    first = keys[0]
    for k in keys[1:]:
        if game.line(*k) != game.line(*first):
            raise StoryError(f"{ref(*k)} is not identical to {ref(*first)}")
    head = "**" + "、".join(ref(*k) for k in keys) + "**"
    if len(keys) > 1:
        head += f"（{len(keys)} 條逐字相同）"
    return [head + (f"：{title}" if title else ""), ""] + fence(game.transcript(*first)) + [""]


def cell(text):
    return str(text).replace("|", "｜").replace("\n", " ")


def spawn_rows(game, spawns, cod, extra_columns=()):
    rows = []
    for s in spawns:
        anchor = cod[s["index"]] if cod and s["index"] < len(cod) else None
        cells = [str(s["index"]), f"`{s['char_id']:02X}` {cell(game.name(s['char_id']))}",
                 str(s["level"]), "FF" if s["wave"] == 0xFF else str(s["wave"]),
                 f"({anchor[1]}, {anchor[2]})" if anchor else "—"]
        cells += [c(s) for c in extra_columns]
        rows.append("| " + " | ".join(cells) + " |")
    return rows


def death_text(game, s):
    op, arg = s["death_op"], s["death_arg"]
    if op == 0xFF:
        return "—"
    if op == 0:
        return f"掉落 `{arg:02X}` {game.tables().item_name(arg)}"
    if op == 1:
        return f"掉落 {arg} 金"
    return f"opcode {op}，參數 {arg}"


def block_s1_deploy(game):
    m = game.map(49)
    dat = m["dat"]
    lines = ["| # | 單位 | 等級 | 波次 | 錨點 | 死亡腳本 |", "| ---: | --- | ---: | ---: | --- | --- |"]
    lines += spawn_rows(game, dat["spawns"], m["cod"], (lambda s: death_text(game, s),))
    return "\n".join(lines)


def block_s1_text(game):
    out = []
    for key in S1_TEXT:
        out += text_section(game, [key])
    return "\n".join(out)


def block_s2_deploy(game):
    import map_decode
    ours = game.spawns_past_count(31)
    theirs = game.map(32)["dat"]["spawns"]
    counted = game.map(31)["dat"]["spawn_count"]
    cod = game.map(31)["cod"]
    lines = ["| # | 單位 | 等級 | 波次 | 錨點 | 在筆數內 | `MAP32.DAT` 同一筆 |",
             "| ---: | --- | ---: | ---: | --- | --- | --- |"]
    lines += spawn_rows(game, ours, cod, (
        lambda s: "是" if s["index"] < counted else "否",
        lambda s: (f"`{theirs[s['index']]['char_id']:02X}` {cell(game.name(theirs[s['index']]['char_id']))}"
                   if s["index"] < len(theirs) else "—")))
    size = len(game.raw_map_dat(31))
    lines += ["", f"檔長 {size} byte＝檔頭 `0x{map_decode.DAT_SPAWNS:x}` ＋ {len(ours)} × "
                  f"`0x{map_decode.SPAWN_BYTES:x}`；檔頭的部署筆數寫 {counted}。"
                  f"`MAP31.COD` 有 {len(cod) if cod else 0} 筆座標。"]
    return "\n".join(lines)


def block_s2_text(game):
    same34 = game.field["FDETXT34.TXT"] == game.field["FDETXT33.TXT"]
    if not same34:
        raise StoryError("FDETXT34 is no longer byte-identical to FDETXT33")
    b32, b33 = game.block(32), game.block(33)
    if len(b32) != len(b33):
        raise StoryError("FDETXT32 and FDETXT33 no longer have the same entry count")
    differ = [i for i in range(len(b32)) if game.line(32, i) != game.line(33, i)]
    out = [f"`FDETXT34.TXT` 與 `FDETXT33.TXT` 逐 byte 相同（{len(game.field['FDETXT33.TXT'])} byte）。"
           f"`FDETXT32.TXT` 的 {len(b32)} 條裡只有 "
           + "、".join(f"`0x{i:02x}`" for i in differ) + " 與 `FDETXT33` 不同，其餘逐字相同：", ""]
    for i in differ:
        out += text_section(game, [(32, i)], "地圖 31 的版本")
        out += text_section(game, [(33, i)], "地圖 32 實際顯示的版本")
    return "\n".join(out)




def block_s4_text(game):
    out = []
    for keys, title in S4_GROUPS:
        out += text_section(game, keys, title)
    return "\n".join(out)


S5_CHAPTER_OF = {53: 12, 54: 12, 57: 25, 58: 9, 59: 20, 60: 25, 61: 25}
VILLAGE_SCREEN = {4: "道具店", 5: "武器店", 6: "酒館", 7: "教會", 8: "神秘商店"}


def block_s5_text(game):
    out = ["| 區塊 | 章名（`0x01`） | 對應的正式章節區塊 |", "| --- | --- | --- |"]
    for b in S5_BLOCKS:
        c = S5_CHAPTER_OF[b]
        out.append(f"| `FDETXT{b:02d}` | {cell(plain(game.line(b, 1)))} | `FDETXT{c:02d}` |")
    out.append("")
    for entry in range(4, 9):
        groups = {}
        for b in S5_BLOCKS:
            groups.setdefault(game.line(b, entry), []).append((b, entry))
        for keys in groups.values():
            c = S5_CHAPTER_OF[keys[0][0]]
            ours, theirs = game.line(*keys[0]), game.line(c, entry)
            if ours == theirs:
                relation = f"與正式版 {ref(c, entry)} 逐字相同"
            elif plain(ours) == plain(theirs):
                relation = f"與正式版 {ref(c, entry)} 只差控制碼（換行、換頁或說話者）"
            else:
                relation = f"與正式版 {ref(c, entry)} 不同"
            out += text_section(game, keys, f"{VILLAGE_SCREEN[entry]}；{relation}")
            if plain(ours) != plain(theirs):
                out += [f"正式版 {ref(c, entry)}：", ""] + fence(game.transcript(c, entry)) + [""]
    return "\n".join(out)


def block_s10_deploy(game):
    m = game.map(27)
    turns = [t for t in m["dat"]["turn_events"] if (t["turn"], t["handler"]) != (0xFF, 0xFF)]
    lines = ["| 回合 | 事件 slot | 部署的波次（回合 ÷ 2） |", "| ---: | ---: | ---: |"]
    lines += [f"| {t['turn']} | {t['handler']} | {t['turn'] // 2} |" for t in turns]
    lines += ["", "| # | 單位 | 等級 | 波次 | 錨點 |", "| ---: | --- | ---: | ---: | --- |"]
    lines += spawn_rows(game, [s for s in m["dat"]["spawns"] if s["wave"] in (3, 4)], m["cod"])
    return "\n".join(lines)


def block_s11_deploy(game):
    lines = ["| 章 | 地圖 | # | 單位 | 等級 | 錨點 |", "| ---: | --- | ---: | --- | ---: | --- |"]
    total = 0
    for n in range(0, 30):
        m = game.map(n)
        cod = m["cod"]
        for s in m["dat"]["spawns"]:
            if s["wave"] != 0xFF:
                continue
            total += 1
            anchor = cod[s["index"]] if cod and s["index"] < len(cod) else None
            lines.append(f"| {n + 1} | `MAP{n:02d}.DAT` | {s['index']} | "
                         f"`{s['char_id']:02X}` {cell(game.name(s['char_id']))} | {s['level']} | "
                         f"{f'({anchor[1]}, {anchor[2]})' if anchor else '—'} |")
    lines += ["", f"共 {total} 筆。"]
    return "\n".join(lines)


def block_s14_text(game):
    return "\n".join(text_section(game, [(0, 0x000)]))


def block_s6_media(game):
    import saf_decode
    data = cut_content.read_vfs_member(game.game_dir, "ICONANI.VFS", "ICON0032.SAF")
    _, _, decoded = saf_decode.decode_animation(data, "ICON0032.SAF")
    lines = ["| 格 | 停留（tick） | 音效 | 圖 |", "| ---: | ---: | --- | --- |"]
    for f in decoded["frames"]:
        sound = "—" if f["sound"] < 0 else f"[s{f['sound']}](media/story/s6-icon0032-s{f['sound']}.wav)"
        lines.append(f"| {f['index']} | {f['duration']} | {sound} | "
                     f"![第 {f['index']} 格](media/story/s6-icon0032-f{f['index']:02d}.png) |")
    lines.append("")
    for s in decoded["sounds"]:
        lines.append(f"- `s{s['index']}`：{s['rate']} Hz、{s['bits']} bit、"
                     f"{s['length'] // (s['bits'] // 8) // s['channels']} 取樣"
                     f"（[`s6-icon0032-s{s['index']}.wav`](media/story/s6-icon0032-s{s['index']}.wav)）")
    return "\n".join(lines)


def block_text_index(game):
    problems, pending, never, unsettled, shown = ownership_problems(game)
    if problems:
        raise StoryError("the never-shown text gate fails:\n  " + "\n  ".join(problems))
    rows = []
    for key in sorted(never):
        if key[0] in unsettled:
            continue
        owner = OWNERS[key]
        note = ""
        if owner in TWIN_NOTE_OWNERS:
            kind, other = twin(game, key, shown)
            if kind == "same":
                note = f"與 {ref(*other)} 逐字相同"
            elif plain(game.line(*key)) == plain(game.line(*other)):
                note = f"與 {ref(*other)} 只差控制碼（換行、換頁或說話者）"
            else:
                note = f"最接近 {ref(*other)}（相符 {kind:.0%}）"
        rows.append((key, owner, note))
    anchors = page_anchors()
    lines = ["| 區塊 | 條目 | 歸屬 | 說明 |", "| --- | --- | --- | --- |"]
    i = 0
    while i < len(rows):
        (block, first), owner, note = rows[i]
        j = i
        while (j + 1 < len(rows) and rows[j + 1][0][0] == block and rows[j + 1][1] == owner
               and rows[j + 1][0][1] == rows[j][0][1] + 1 and not note and not rows[j + 1][2]):
            j += 1
        last = rows[j][0][1]
        entries = f"`0x{first:02x}`" + (f"–`0x{last:02x}`" if last != first else "")
        label = ("待判定" if owner.startswith("?") else
                 f"[{owner}](#{anchors[owner]})" if owner in anchors
                 else f"{owner}（[排除清單](_index.md#排除清單)）")
        lines.append(f"| `FDETXT{block:02d}` | {entries} | {label} | {note} |")
        i = j + 1
    if unsettled:
        lines += ["", "章節區塊 " + "、".join(f"`FDETXT{n:02d}`" for n in sorted(unsettled))
                  + " 還沒有逐條的判定，不在上表；它們已知屬於各條目的條目寫在各條目裡。"]
    return "\n".join(lines)


def slug(heading):
    """The anchor a Markdown renderer gives a heading (GitHub's rule)."""
    text = heading.strip().lower().replace("`", "")
    text = "".join(ch for ch in text if ch.isalnum() or ch in " -_")
    return text.replace(" ", "-")


def page_anchors():
    """{entry id: anchor} of this page's entries."""
    entries, _ = cut_content.collect(cut_content.CUT_DIR)
    return {e.id: slug(f"{e.id} {e.title}") for e in entries if e.topic == "story"}


BLOCKS = {
    "S1-deploy": block_s1_deploy,
    "S1-text": block_s1_text,
    "S2-deploy": block_s2_deploy,
    "S2-text": block_s2_text,
    "S4-text": block_s4_text,
    "S5-text": block_s5_text,
    "S6-media": block_s6_media,
    "S10-deploy": block_s10_deploy,
    "S11-deploy": block_s11_deploy,
    "S14-text": block_s14_text,
    "text-index": block_text_index,
}


def render_blocks(game):
    return {key: fn(game) for key, fn in BLOCKS.items()}


# ---------------------------------------------------------------------------
# Media
# ---------------------------------------------------------------------------

def _rgba_rows(image):
    w, h = image.size
    raw = image.tobytes()
    return w, h, [raw[y * w * 4:(y + 1) * w * 4] for y in range(h)]


def render_whole_map(m, palette):
    """The map's lockstep layers in the game's draw order, as RGBA rows."""
    import map_decode
    from PIL import Image
    width, height = map_decode.grid_size(m)
    canvas = Image.new("RGBA", (width * TILE, height * TILE), (0, 0, 0, 0))
    for k in map_decode.draw_order(m):
        layer = m["layers"][k]
        if layer["mpl"] is None or not layer["cel_path"].is_file():
            continue
        if not map_decode.lockstep(layer):
            raise StoryError(f"map {m['number']} layer {k} scrolls on its own")
        sprites = map_decode.sprite_images(layer["cel_path"], palette)
        map_decode.paint_layer(canvas, layer, sprites, width, height)
    return _rgba_rows(canvas)


MAPS = ((1, 49), (2, 31), (2, 33))


def generate_media(out_dir, game_dir):
    """cut_content.GENERATORS["story"]: every media file story.md shows."""
    import cel_decode
    import saf_decode
    out_dir, game = Path(out_dir), Game(game_dir)
    dump = dump_tree(game.game_dir)
    palette = cel_decode.load_palette(dump / "MISC" / "FDE.PAL")

    for entry, n in MAPS:
        w, h, rows = render_whole_map(game.map(n), palette)
        cut_content.write_png(out_dir / f"s{entry}-m{n:02d}.png", w, h, rows)

    for member in ("ICON0032", "ICON0033"):
        data = cut_content.read_vfs_member(game.game_dir, "ICONANI.VFS", member + ".SAF")
        header, _, decoded = saf_decode.decode_animation(data, member + ".SAF")
        saf_decode.write_filmstrip(out_dir / f"s6-{member.lower()}-sheet.png",
                                   decoded["frames"], decoded, header, palette)
        if member != "ICON0032":
            continue
        for f in decoded["frames"]:
            pixels, mask = saf_decode.compose_frame(f, decoded, header)
            rows = []
            for y in range(saf_decode.SCREEN_HEIGHT):
                row = bytearray()
                for x in range(saf_decode.SCREEN_WIDTH):
                    at = y * saf_decode.SCREEN_WIDTH + x
                    row += bytes(palette[pixels[at]]) if mask[at] else b"\0\0\0\0"
                rows.append(bytes(row))
            cut_content.write_png(out_dir / f"s6-icon0032-f{f['index']:02d}.png",
                                  saf_decode.SCREEN_WIDTH, saf_decode.SCREEN_HEIGHT, rows)
        for s in decoded["sounds"]:
            cut_content.write_wav(out_dir / f"s6-icon0032-s{s['index']}.wav", s)

    for member in ("M09", "M090"):
        data = (dump / "FIELD1" / f"{member}.CEL").read_bytes()
        header, _, decoded = cel_decode.decode_sheet(data, member + ".CEL")
        cel_decode.write_contact_sheet(out_dir / f"s12-{member.lower()}-sheet.png",
                                       header, decoded, palette)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def cmd_build(args):
    game = Game(args.game)
    text = PAGE.read_text(encoding="utf-8")
    new, problems = apply_blocks(text, render_blocks(game))
    for p in problems:
        print("PROBLEM " + p)
    if problems:
        return 1
    if new != text:
        PAGE.write_text(new, encoding="utf-8", newline="\n")
        print("story.md: generated blocks rewritten")
    else:
        print("story.md: generated blocks already current")
    return 0


def cmd_check(args):
    game = Game(args.game)
    problems, pending, _, unsettled, _ = ownership_problems(game)
    text = PAGE.read_text(encoding="utf-8")
    if not problems:
        new, block_problems = apply_blocks(text, render_blocks(game))
        problems += block_problems
        if new != text:
            problems.append("story.md: a generated block is stale; run: story.py build")
    for p in problems:
        print("PROBLEM " + p)
    by_reason = {}
    for key, reason in pending:
        by_reason.setdefault(reason, []).append(key)
    for reason, keys in sorted(by_reason.items()):
        print(f"PENDING {reason}: {len(keys)} entries")
    if unsettled:
        print("UNSETTLED chapter blocks: " + " ".join(f"{n:02d}" for n in sorted(unsettled)))
    if problems or (args.final and (pending or unsettled)):
        print("FAIL")
        return 1
    print("OK" + ("" if not (pending or unsettled) else " (with pending owners)"))
    return 0


def cmd_cdda(args):
    for path, seconds in cut_cdda({1: args.disc1, 2: args.disc2}):
        print(f"{path}  {seconds:.2f} s")
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("build")
    p.add_argument("--game", type=Path, default=DEFAULT_GAME)
    p.set_defaults(func=cmd_build)
    p = sub.add_parser("check")
    p.add_argument("--game", type=Path, default=DEFAULT_GAME)
    p.add_argument("--final", action="store_true")
    p.set_defaults(func=cmd_check)
    p = sub.add_parser("cdda")
    p.add_argument("--disc1", type=Path, default=DISC_DIR / "FDPS_DISC_1.cue")
    p.add_argument("--disc2", type=Path, default=DISC_DIR / "FDPS_DISC_2.cue")
    p.set_defaults(func=cmd_cdda)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
