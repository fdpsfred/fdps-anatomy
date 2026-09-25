"""Decode the FDPS data tables assets/ documents, generate the knowledge-base
tables from them, and check the knowledge base against the data.

Every number comes from the shipped bytes; every name comes from the game's
own text block FDETXT00.TXT through tools/text_decode, at the entry bases the
program itself adds (src/ references in NAME_BASES).  Nothing is read from the
strategy guide.

    ENEMYDAT.DAT  10-byte records, portrait id - 0x3C       (src/table.c)
    RANKUP.DAT    12-byte records, character id 0..8: four  (src/church.c)
                  (form, class, move bonus) routes
    SHOPnn.DAT    three rows of twelve item ids, 0xFF empty (src/shop.c)
    MAPnn.DAT     deployment records, read through tools/map_decode

Usage:
    python tools/data_tables/data_tables.py gen   [--dump DIR] [--out DIR]
    python tools/data_tables/data_tables.py apply [--dump DIR]
    python tools/data_tables/data_tables.py check [--dump DIR]

`gen` writes each generated table as Markdown to workspace/data_tables/ (one
file per table).  `apply` writes them into the knowledge-base files: over the
table with the same header, or in place of a `<!-- data_tables:<key> -->`
line on the first run; the prose around the tables is not touched.  `check` reads the same
tables back out of the knowledge-base files and compares them cell by cell
with what `gen` would write, plus the name columns of the older tables; it
exits non-zero on any difference.  <DIR> defaults to workspace/vfs_dump.
"""
import argparse
import collections
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "text_decode"))
sys.path.insert(0, str(ROOT / "tools" / "map_decode"))
import text_decode  # noqa: E402  (owner of the text block format)
import map_decode  # noqa: E402  (owner of the MAPnn.DAT parse)

DEFAULT_DUMP = ROOT / "workspace" / "vfs_dump"
DEFAULT_OUT = ROOT / "workspace" / "data_tables"
# Never-deployed enemies and classes nobody has are entries of the units
# page (ticket 25.11).
CUT_UNITS = "../cut_content/units.md"
# Use-effect codes with no handler are entries of the items page (ticket 25.12).
CUT_ITEMS = "../cut_content/items.md"

# Entry bases in FDETXT00.TXT, each the constant the program adds to an id.
NAME_BASES = {
    "char": 0x01,    # char_id + 1, src/statunit.c TEXT_ID_FIRST_CHARACTER_NAME
    "race": 0x97,    # src/statunit.c TEXT_ID_FIRST_RACE_NAME
    "class": 0xA1,   # src/statunit.c TEXT_ID_FIRST_CLASS_NAME
    "item": 0xC9,    # src/btlact.c ITEM_NAME_TEXT_BASE
    "spell": 0x1BE,  # src/spellmnu.c SPELL_NAME_TEXT_BASE
}
BLANK = "（空白）"

ENEMY_BASE = 0x3C          # src/deploy.c ENEMY_CHAR_ID_BASE
ENEMY_STRIDE = 10
PROMOTION_STRIDE = 12
HERO_ROUTE = 3             # route 3 is the 勇者徽章 one
HERO_ROUTE_CHARACTER = 0   # src/church.c: offered only to portrait id 0, 蘭迪斯
ROUTES = ("無徽章", "光之徽章", "暗之徽章", "勇者徽章")
CLASS_COUNT = 0x28         # class codes 00..27: the class-name block up to the item names


def offered_routes(character, routes):
    """(route number, (form, class, move)) for every route the church can pick."""
    return [(n, r) for n, r in enumerate(routes)
            if n != HERO_ROUTE or character == HERO_ROUTE_CHARACTER]
SHOP_ROWS = 3
SHOP_SLOTS = 12
SHOP_EMPTY = 0xFF
ROSTER_CHARACTERS = range(0x0C)   # the twelve fdps_roster_add_character adds
LAST_BATTLE_MAP = 29
FIRST_SCENE_MAP = 31
CHAPTER_COUNT = 30


def no_village(chapter_id):
    """src/village.c CHAPTER_HAS_NO_VILLAGE."""
    return 0x10 <= chapter_id <= 0x11 or chapter_id in (0x15, 0x16) or chapter_id > 0x19


# Use-effect codes of ITEM.DAT +0x0d, as fdps_apply_item_effect_to_targets
# (000262a0, src/item.c) and fdps_battle_item_menu (000252b0) treat them.
# The descriptions are transcribed from that source; which items carry each
# code is read from ITEM.DAT.
USE_EFFECTS = {
    0x01: ("火系傷害：播 `EMg00.saf`，對每個目標造成以數量為基準的傷害", "消耗"),
    0x02: ("雷系傷害：播 `EMg05.saf`，其餘同 `01`", "消耗"),
    0x03: ("冰系傷害：播 `EMg08.saf`，其餘同 `01`", "消耗"),
    0x04: ("地系傷害：播 `EarQu.wav` 並震動畫面，其餘同 `01`", "消耗"),
    0x05: (f"沒有處理分支，使用後什麼都不做（[空殼]({CUT_ITEMS})，I01）", "不消耗"),
    0x06: ("同 `05`", "不消耗"),
    0x07: ("同 `01`", "不消耗"),
    0x08: ("同 `02`", "不消耗"),
    0x09: ("同 `03`", "不消耗"),
    0x0A: ("同 `04`", "不消耗"),
    0x0B: ("回復 HP：以數量為基準", "消耗"),
    0x0C: ("回復 MP：以數量為基準；MP 上限為 0 的目標顯示 MISS", "消耗"),
    0x0D: ("同 `05`", "不消耗"),
    0x0F: ("MHP +15（寫死在程式裡，不讀數量）", "消耗"),
    0x10: ("MMP +15（同上）", "消耗"),
    0x11: ("AP 基礎值 +7（同上）", "消耗"),
    0x12: ("DP 基礎值 +7（同上）", "消耗"),
    0x13: ("DX +7（同上）", "消耗"),
    0x14: ("MV +1（同上）", "消耗"),
    0x16: ("解除中毒", "消耗"),
    0x18: ("解除麻痺", "消耗"),
    0x1B: ("同 `05`", "不消耗"),
    0x1E: ("直接傷害：對每個目標直接扣 HP，不播元素動畫", "不消耗"),
    0x20: ("同 `0B`", "不消耗"),
    0x21: ("只對型態 `09`（蓋亞）有效：AP、DP 基礎值各 +30、MV +1、學會 `1D` 轟神砲；"
           "對別人顯示「這是什麼？」", "成功時消耗"),
    0x22: ("只對型態 `09` 有效：MHP +100", "成功時消耗"),
    0x23: ("只對角色 `08`（布蘭多）有效：身上有 `A3` 金屬礦時，把這件與金屬礦換成 `BE` 高能量砲",
           "成功時消耗"),
}
# Codes the target picker knows but no item carries (fdps_battle_item_menu).
PICKER_ONLY_EFFECTS = {
    0x19: f"選完目標後再開一次傳送目的地游標，效果函式沒有分支（[殘留內容]({CUT_ITEMS})，I02）",
    0x1C: "同 `19`，目的地排除施用者所在格",
}


class TableError(Exception):
    """A table member does not hold what its format requires."""


# ---- record decoders ---------------------------------------------------------

def _records(data, stride, what):
    if len(data) % stride:
        raise TableError(f"{what}: {len(data)} bytes is not a whole number of {stride}-byte records")
    return [data[i:i + stride] for i in range(0, len(data), stride)]


def decode_enemies(data):
    out = []
    for r in _records(data, ENEMY_STRIDE, "ENEMYDAT.DAT"):
        out.append({"race": r[0], "class": r[1], "hp": struct.unpack_from("<H", r, 2)[0],
                    "mp": r[4], "ap": r[5], "dp": r[6], "dx": r[7], "mv": r[8], "exp": r[9]})
    return out


def decode_promotions(data):
    return [[tuple(r[k * 3:k * 3 + 3]) for k in range(4)]
            for r in _records(data, PROMOTION_STRIDE, "RANKUP.DAT")]


def decode_shop(data):
    if len(data) != SHOP_ROWS * SHOP_SLOTS:
        raise TableError(f"shop file is {len(data)} bytes, not {SHOP_ROWS * SHOP_SLOTS}")
    return [[b for b in data[k * SHOP_SLOTS:(k + 1) * SHOP_SLOTS] if b != SHOP_EMPTY]
            for k in range(SHOP_ROWS)]


def decode_appearance(data):
    out = []
    for r in _records(data, 24, "FRIAPRDA.DAT"):
        out.append({"race": r[0], "class": r[1], "level": r[2],
                    "spells": [i for i in range(32) if struct.unpack_from("<I", r, 8)[0] >> i & 1],
                    "raw": bytes(r)})
    return out


def decode_growth(data):
    return [{"learn": r[10]} for r in _records(data, 11, "FRILEVUP.DAT")]


def decode_learning(data):
    return [[(r[i * 2], r[i * 2 + 1]) for i in range(6) if r[i * 2] != 0xFF]
            for r in _records(data, 12, "GETMGTAB.DAT")]


def decode_items(data):
    return [{"use_effect": r[13]} for r in _records(data, 23, "ITEM.DAT")]


# ---- the loaded game ---------------------------------------------------------

class Game:
    """Everything the tables are generated from, loaded out of a vfs_dump tree."""

    def __init__(self, text, enemies, enemy_raw, promotions, shops, appearance,
                 growth, learning, items, spawns):
        self.text = text
        self.enemies = enemies
        self.enemy_raw = enemy_raw
        self.promotions = promotions
        self.shops = shops
        self.appearance = appearance
        self.growth = growth
        self.learning = learning
        self.items = items
        self._spawns = spawns

    def _name(self, kind, index):
        entry = NAME_BASES[kind] + index
        return self.text[entry] if entry < len(self.text) else ""

    def char_name(self, char_id):
        return self._name("char", char_id)

    def race_name(self, race):
        return self._name("race", race)

    def class_name(self, clazz):
        return self._name("class", clazz)

    def item_name(self, item):
        return self._name("item", item)

    def spell_name(self, spell):
        return self._name("spell", spell)

    def unit_label(self, char_id):
        """A character id's name as the tables print it."""
        entry = NAME_BASES["char"] + char_id
        name = self.char_name(char_id)
        if entry >= NAME_BASES["race"]:
            return f"（名稱區外：{name}）"
        return name or BLANK

    def deployments(self, char_id):
        return [s for s in self._spawns if s["char_id"] == char_id]

    def deployed_maps(self, char_id):
        maps = sorted({s["map"] for s in self.deployments(char_id)})
        return ([m + 1 for m in maps if m <= LAST_BATTLE_MAP],
                [m for m in maps if m >= FIRST_SCENE_MAP])

    def base_record_readers(self):
        """FRIAPRDA.DAT indices the game reads: the roster adds and every deployment below 0x3C."""
        read = set(ROSTER_CHARACTERS)
        read |= {s["char_id"] for s in self._spawns if s["char_id"] < ENEMY_BASE}
        return sorted(read)

    def template_enemy_rows(self):
        counts = collections.Counter(self.enemy_raw)
        pattern, n = counts.most_common(1)[0]
        return {i for i, raw in enumerate(self.enemy_raw) if raw == pattern} if n > 1 else set()

    def village_chapters(self):
        return [c for c in range(1, CHAPTER_COUNT) if not no_village(c)]


def load(dump=DEFAULT_DUMP):
    dump = Path(dump)
    misc, field = dump / "MISC", dump / "FIELD"
    glyphs = text_decode.load_glyph_table()
    text = [text_decode.render_entry(e, glyphs)
            for e in text_decode.parse_block((field / "FDETXT00.TXT").read_bytes())]
    enemy_bytes = (misc / "ENEMYDAT.DAT").read_bytes()
    spawns = []
    for n in range(map_decode.MAX_MAP + 1):
        path = field / f"MAP{n:02d}.DAT"
        if path.is_file():
            for s in map_decode.parse_map_dat(path.read_bytes(), path.name)["spawns"]:
                spawns.append({"map": n, "char_id": s["char_id"], "side": s["side"],
                               "level": s["level"], "wave": s["wave"], "index": s["index"]})
    shops = {n: decode_shop((field / f"SHOP{n:02d}.DAT").read_bytes())
             for n in range(CHAPTER_COUNT) if (field / f"SHOP{n:02d}.DAT").is_file()}
    return Game(text=text,
                enemies=decode_enemies(enemy_bytes),
                enemy_raw=[enemy_bytes[i:i + ENEMY_STRIDE]
                           for i in range(0, len(enemy_bytes), ENEMY_STRIDE)],
                promotions=decode_promotions((misc / "RANKUP.DAT").read_bytes()),
                shops=shops,
                appearance=decode_appearance((misc / "FRIAPRDA.DAT").read_bytes()),
                growth=decode_growth((misc / "FRILEVUP.DAT").read_bytes()),
                learning=decode_learning((misc / "GETMGTAB.DAT").read_bytes()),
                items=decode_items((misc / "ITEM.DAT").read_bytes()),
                spawns=spawns)


# ---- cell formatting ---------------------------------------------------------

def compact(numbers):
    """1, 2, 3, 5 -> '1–3、5'; runs of three or more become ranges."""
    numbers = sorted(numbers)
    if not numbers:
        return "—"
    parts, start = [], 0
    for i in range(1, len(numbers) + 1):
        if i == len(numbers) or numbers[i] != numbers[i - 1] + 1:
            run = numbers[start:i]
            if len(run) >= 3:
                parts.append(f"{run[0]}–{run[-1]}")
            else:
                parts.extend(str(x) for x in run)
            start = i
    return "、".join(parts)


def code(value):
    return f"`{value:02X}`"


def coded(value, name):
    return f"{code(value)} {name or BLANK}"


def listing(items):
    return "、".join(items) if items else "—"


def unique(seq):
    seen, out = set(), []
    for x in seq:
        if x not in seen:
            seen.add(x)
            out.append(x)
    return out


# ---- generated tables --------------------------------------------------------

def enemy_rows(game):
    template = game.template_enemy_rows()
    rows = []
    for i, e in enumerate(game.enemies):
        pid = ENEMY_BASE + i
        chapters, scenes = game.deployed_maps(pid)
        notes = []
        if i in template:
            notes.append("樣板列")
        if not chapters and not scenes:
            notes.append(f"[未部署]({CUT_UNITS})")
        rows.append([str(i), code(pid), game.unit_label(pid),
                     coded(e["race"], game.race_name(e["race"])),
                     coded(e["class"], game.class_name(e["class"])),
                     str(e["hp"]), str(e["mp"]), str(e["ap"]), str(e["dp"]), str(e["dx"]),
                     str(e["mv"]), str(e["exp"]), compact(chapters), compact(scenes),
                     "；".join(notes) or "—"])
    return rows


def _named(label):
    return label != BLANK and not label.startswith("（名稱區外")


def race_rows(game):
    rows = []
    for race in range(NAME_BASES["class"] - NAME_BASES["race"]):
        name = game.race_name(race)
        if not name:
            continue
        ours = unique(game.unit_label(c) for c in game.base_record_readers()
                      if game.appearance[c]["race"] == race and _named(game.unit_label(c)))
        theirs = unique(game.unit_label(ENEMY_BASE + i) for i, e in enumerate(game.enemies)
                        if e["race"] == race and game.deployments(ENEMY_BASE + i)
                        and _named(game.unit_label(ENEMY_BASE + i)))
        rows.append([code(race), name, f"`0x{NAME_BASES['race'] + race:02x}`",
                     listing(ours), listing(theirs)])
    return rows


def class_user_rows(game):
    rows = []
    for clazz in range(CLASS_COUNT):
        ours = [game.unit_label(c) for c in game.base_record_readers()
                if game.appearance[c]["class"] == clazz and _named(game.unit_label(c))]
        for owner, routes in enumerate(game.promotions):
            for _route, (_form, target, _move) in offered_routes(owner, routes):
                if target == clazz:
                    ours.append(f"{game.char_name(owner)}（轉職）")
        theirs = [game.unit_label(ENEMY_BASE + i) for i, e in enumerate(game.enemies)
                  if e["class"] == clazz and game.deployments(ENEMY_BASE + i)
                  and _named(game.unit_label(ENEMY_BASE + i))]
        unnamed = sum(1 for i, e in enumerate(game.enemies)
                      if e["class"] == clazz and game.deployments(ENEMY_BASE + i)
                      and not _named(game.unit_label(ENEMY_BASE + i)))
        theirs = unique(theirs)
        if unnamed:
            theirs.append(f"無名演員 {unnamed} 列")
        ours = unique(ours)
        # A class the name table leaves blank is the table-tail row (U10); the
        # named ones nobody has are U05.
        if ours or theirs:
            note = "—"
        elif not game.class_name(clazz):
            note = f"[沒有單位使用]({CUT_UNITS})（表尾列，U10）"
        else:
            note = f"[沒有單位使用]({CUT_UNITS})（U05）"
        rows.append([code(clazz), game.class_name(clazz) or BLANK, listing(ours),
                     listing(theirs), note])
    return rows


def promotion_rows(game):
    rows = []
    for owner, routes in enumerate(game.promotions):
        cells = ["—"] * len(routes)
        for route, (form, clazz, move) in offered_routes(owner, routes):
            cells[route] = f"{code(form)} {game.class_name(clazz)}，MV +{move}"
        rows.append([game.char_name(owner), code(owner)] + cells)
    return rows


def _forms(game, char_id):
    """The form indices a roster character can be in, with each form's class."""
    forms = [(char_id, game.appearance[char_id]["class"])]
    if char_id < len(game.promotions):
        for _route, (form, clazz, _move) in offered_routes(char_id, game.promotions[char_id]):
            if form not in [f for f, _ in forms]:
                forms.append((form, clazz))
    return forms


def spell_schedule_rows(game):
    rows = []
    for char_id in ROSTER_CHARACTERS:
        for form, clazz in _forms(game, char_id):
            learn = game.growth[form]["learn"]
            initial = (listing([coded(s, game.spell_name(s))
                                for s in game.appearance[char_id]["spells"]])
                       if form == char_id else "—")
            if learn == 0xFF:
                learned = "—"
            else:
                learned = listing([f"Lv{lv} {coded(sp, game.spell_name(sp))}"
                                   for lv, sp in game.learning[learn]])
            rows.append([game.char_name(char_id), code(form), game.class_name(clazz),
                         "—" if learn == 0xFF else code(learn), initial, learned])
    return rows


def shop_rows(game):
    rows = []
    for chapter in game.village_chapters():
        stock = game.shops[chapter]
        rows.append([f"`SHOP{chapter:02d}.DAT`", str(chapter),
                     f"第 {chapter} 章勝利後、第 {chapter + 1} 章之前"]
                    + [listing([coded(i, game.item_name(i)) for i in row]) for row in stock])
    return rows


def use_effect_rows(game):
    carried = collections.defaultdict(list)
    for item, rec in enumerate(game.items):
        if rec["use_effect"]:
            carried[rec["use_effect"]].append(item)
    unknown = sorted(set(carried) - set(USE_EFFECTS))
    if unknown:
        raise TableError("ITEM.DAT carries use-effect codes with no description: "
                         + ", ".join(f"{c:02X}" for c in unknown))
    rows = []
    for effect in sorted(set(USE_EFFECTS) | set(PICKER_ONLY_EFFECTS)):
        if effect in USE_EFFECTS:
            what, consumed = USE_EFFECTS[effect]
        else:
            what, consumed = PICKER_ONLY_EFFECTS[effect], "—"
        items = [coded(i, game.item_name(i)) for i in carried.get(effect, [])]
        rows.append([code(effect), what, consumed, listing(items)])
    return rows


# (knowledge-base file, header, builder).  The header is also how `check`
# finds the table inside the file.
TABLES = {
    "enemies": ("assets/enemies.md",
                ("列", "肖像編號", "名稱", "種族", "職業", "HP", "MP", "AP", "DP", "DX", "MV",
                 "EXP", "部署的章", "部署的過場地圖", "備註"), enemy_rows),
    "races": ("assets/races.md",
              ("代碼", "名稱", "文字條目", "我方與客串單位", "敵方與 NPC 單位"), race_rows),
    "class_users": ("assets/classes.md",
                    ("代碼", "職業", "我方與客串單位", "敵方與 NPC 單位", "備註"), class_user_rows),
    "promotions": ("assets/characters.md",
                   ("人物", "索引") + ROUTES, promotion_rows),
    "spell_schedule": ("assets/characters.md",
                       ("人物", "型態", "職業", "習得索引", "出場時已會", "升級習得"),
                       spell_schedule_rows),
    "shops": ("assets/shops.md",
              ("檔案", "章節索引", "村莊的時機", "道具店", "武器店", "秘密商店"), shop_rows),
    "use_effects": ("assets/items.md",
                    ("代碼", "效果", "使用後", "帶這個碼的物品"), use_effect_rows),
}
RIGHT_ALIGNED = {"列", "HP", "MP", "AP", "DP", "DX", "MV", "EXP", "章節索引"}


def markdown(header, rows):
    sep = ["---:" if h in RIGHT_ALIGNED else "---" for h in header]
    lines = ["| " + " | ".join(header) + " |", "| " + " | ".join(sep) + " |"]
    lines += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(lines) + "\n"


# ---- reading the knowledge base ----------------------------------------------

def split_row(line):
    """Cells of one Markdown table row, whitespace-trimmed and otherwise untouched."""
    body = line.strip()
    if body.startswith("|"):
        body = body[1:]
    if body.endswith("|"):
        body = body[:-1]
    return [c.strip() for c in body.split("|")]


def read_table(path, header):
    """The data rows of the table in `path` whose header row is exactly `header`,
    or None when the file or the table is not there."""
    if not path.is_file():
        return None
    lines = path.read_text(encoding="utf-8").splitlines()
    for i, line in enumerate(lines):
        if line.startswith("|") and split_row(line) == list(header):
            rows = []
            for row in lines[i + 2:]:
                if not row.startswith("|"):
                    break
                rows.append(split_row(row))
            return rows
    return None


# Name columns of the tables older than this tool, checked against the game text.
NAME_COLUMNS = (
    ("assets/items.md", ("編號", "名稱", "類型", "AP", "HIT", "DP", "EV", "距離", "附加屬性", "價格"),
     "item"),
    ("assets/items.md", ("編號", "名稱", "K1", "數量", "距離", "範圍", "對象", "選取模式"), "item"),
    ("assets/spells.md", ("編號", "名稱", "威力", "命中率", "距離", "範圍", "MP", "對象"), "spell"),
    ("assets/classes.md", ("代碼", "職業", "地形消耗", "暴擊率", "魔法抗性"), "class"),
)
CHARACTER_LABEL_TABLES = (
    ("assets/characters.md", ("索引", "人物／職業", "種族", "職業", "等級", "HP 基礎", "MP 基礎",
                              "移動力", "法術", "物品", "AP 基礎", "DP 基礎", "DX 基礎")),
    ("assets/characters.md", ("索引", "人物／職業", "AP", "DP", "DX", "HP", "MP", "習得索引")),
)


def check(game, root=ROOT):
    failures = []
    for key, (doc, header, builder) in TABLES.items():
        rows = read_table(root / doc, header)
        if rows is None:
            failures.append(f"{doc}: no table with the {key} header")
            continue
        expected = builder(game)
        if len(rows) != len(expected):
            failures.append(f"{doc} {key}: {len(rows)} rows, the data gives {len(expected)}")
        for n, (got, want) in enumerate(zip(rows, expected)):
            if got != want:
                cols = [header[c] for c in range(len(header))
                        if c >= len(got) or c >= len(want) or got[c] != want[c]]
                failures.append(f"{doc} {key} row {n}: {', '.join(cols)} differ\n"
                                f"    kb:   {got}\n    data: {want}")
    names = {"item": game.item_name, "spell": game.spell_name, "class": game.class_name}
    for doc, header, kind in NAME_COLUMNS:
        rows = read_table(root / doc, header)
        if rows is None:
            failures.append(f"{doc}: no table with header {header[:3]}...")
            continue
        for row in rows:
            index = int(row[0].strip("`"), 16)
            want = names[kind](index) or BLANK
            if row[1] != want:
                failures.append(f"{doc} {kind} {index:02X}: name {row[1]!r}, game text {want!r}")
    for doc, header in CHARACTER_LABEL_TABLES:
        rows = read_table(root / doc, header)
        if rows is None:
            failures.append(f"{doc}: no table with header {header[:3]}...")
            continue
        for row in rows:
            index = int(row[0].strip("`"), 16)
            game_name = game.char_name(index)
            label = row[1].split("／")[0]
            if game_name and label != game_name:
                failures.append(f"{doc} character {index:02X}: label {row[1]!r}, "
                                f"game text {game_name!r}")
    return failures


# ---- CLI ---------------------------------------------------------------------

def cmd_gen(args):
    game = load(args.dump)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for key, (doc, header, builder) in TABLES.items():
        (out / f"{key}.md").write_text(markdown(header, builder(game)), encoding="utf-8")
        print(f"{key:15s} -> {out / (key + '.md')}  (for {doc})")
    return 0


MARKER = "<!-- data_tables:{} -->"


def apply_table(text, key, header, table_md):
    """Put a freshly generated table into a document's text: over the existing
    table with the same header, or in place of the line `<!-- data_tables:key -->`."""
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if line.startswith("|") and split_row(line) == list(header):
            end = i
            while end < len(lines) and lines[end].startswith("|"):
                end += 1
            return "\n".join(lines[:i] + table_md.rstrip("\n").split("\n") + lines[end:])
        if line.strip() == MARKER.format(key):
            return "\n".join(lines[:i] + table_md.rstrip("\n").split("\n") + lines[i + 1:])
    raise TableError(f"no {key} table and no {MARKER.format(key)} marker to put one at")


def cmd_apply(args):
    game = load(args.dump)
    for key, (doc, header, builder) in TABLES.items():
        path = ROOT / doc
        text = path.read_text(encoding="utf-8")
        new = apply_table(text, key, header, markdown(header, builder(game)))
        if new != text:
            path.write_text(new, encoding="utf-8", newline="\n")
            print(f"{key:15s} written into {doc}")
    return 0


def cmd_check(args):
    failures = check(load(args.dump))
    for f in failures:
        print("MISMATCH " + f)
    if failures:
        print(f"FAIL: {len(failures)} differences between the knowledge base and the data")
        return 1
    print(f"OK: {len(TABLES)} generated tables and every name column agree with the data")
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name, func in (("gen", cmd_gen), ("apply", cmd_apply), ("check", cmd_check)):
        p = sub.add_parser(name)
        p.add_argument("--dump", default=DEFAULT_DUMP)
        if name == "gen":
            p.add_argument("--out", default=DEFAULT_OUT)
        p.set_defaults(func=func)
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (TableError, OSError, text_decode.TextBlockError, map_decode.MapError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
