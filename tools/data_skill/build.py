"""Build the queryable game-data set that ships with the `fdps-data` skill.

Two files, both regenerated from the shipped game files:

    fdps_data.json   the tables: item, spell, character, class (MISC.VFS records
                     decoded here, names from the assets/ tables, every row checked
                     against its record); enemy, race, shop, use_effect (from
                     tools/data_tables' Game); chapter (from tools/chapter_docs'
                     chapter_facts and the landed chapter judgements)
    fdps_text.json   every entry of the 66 text blocks FDETXT00..65, with who shows
                     it, or -- for text nothing shows -- which cut_content/ entry
                     owns it (tools/cut_content/story.py)

The knowledge base is never parsed for anything it did not write by hand: the
tables tools/data_tables and tools/chapter_docs generate are checked by their own
gates (data_tables.check, check_chapter's landed-page staleness), run here against
the same data the records are built from, so a data set only ships when the
knowledge base agrees with it.

Usage:
    python tools/data_skill/build.py [--game DIR] [--out DIR] [--no-chapter-page-gate]

--game defaults to fdps_game_files/, --out to .claude/skills/fdps-data/.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TOOLS = REPO / "tools"
for _sub in ("data_tables", "chapter_docs", "cut_content", "global_text", "text_decode",
             "map_decode", "cutscene_script"):
    sys.path.insert(0, str(TOOLS / _sub))
DEFAULT_GAME = REPO / "fdps_game_files"
DEFAULT_OUT = REPO / ".claude" / "skills" / "fdps-data"
DATA_FILE = "fdps_data.json"
TEXT_FILE = "fdps_text.json"

# Member holding each table, and the record stride FDPS.LE multiplies by.
# Owner of these facts: resource_info/data_tables.md.
TABLES = {
    "item": ("ITEM.DAT", 23),
    "spell": ("MAGICDAT.DAT", 7),
    "appearance": ("FRIAPRDA.DAT", 24),
    "levelup": ("FRILEVUP.DAT", 11),
    "learn": ("GETMGTAB.DAT", 12),
    "class": ("PROMAP.DAT", 10),
}

# Guide values that the data file contradicts.  This list mirrors the ACCEPTED table in
# tools/guide_offsets/crosscheck.py, which is the gate over the fields it compares: it
# fails on any mismatch that is not accepted there, so no new divergence in those fields
# can appear unnoticed.  Its coverage is what bounds this list -- fields outside its
# comparison (item type, use target, the record-only columns) are ungated, and an entry
# added there has to be added here by hand.  The dump side of every entry below is
# checked against the record at build time.
DISCREPANCIES = [
    {"table": "item", "code": 0x4A, "field": "hit", "guide": 100, "dump": 150,
     "note": "風神弓的 HIT，攻略站寫 100"},
    {"table": "spell", "code": 0x17, "field": "hit", "guide": 50, "dump": 60,
     "note": "咒殺術的命中率，攻略站寫 50%"},
    {"table": "spell", "code": 0x16, "field": "target", "guide": None, "dump": 3,
     "note": "神行術的作用對象是 3，攻略站的欄位說明只列了 00 敵方與 01 己方"},
    {"table": "class", "code": 0x18, "field": "move_cost[7]", "guide": 0x01, "dump": 0xFF,
     "note": "機械大師的第八個地形消耗，攻略站寫 01"},
    {"table": "character", "code": 0x00, "field": "initial_spells", "guide": [0x00], "dump": [],
     "note": "攻略站把業火列為劍士蘭迪斯的初始法術，資料檔的遮罩是空的"},
    {"table": "character", "code": 0x02, "field": "initial_spells", "guide": [0x08],
     "dump": [0x00, 0x08, 0x09],
     "note": "攻略站只給費塔加冰爆術，資料檔另有 00 業火與 09 絕殺冰封"
             "（09 是他 Lv15 的習得，攻略站列他以 15 級出場）"},
    {"table": "character", "code": 0x09, "field": "initial_spells", "guide": [0x1D],
     "dump": [],
     "note": "攻略站列蓋亞有轟神砲，資料檔的遮罩是空的——那來自 A4 強化套件"},
    {"table": "character", "code": 0x0A, "field": "initial_spells",
     "guide": [0x05, 0x06, 0x07, 0x0C, 0x20], "dump": [0x00, 0x01, 0x05, 0x06, 0x0C, 0x0E, 0x0F],
     "note": "珊的法術，攻略站法術頁列的五個與資料檔的七個不同"},
    {"table": "character", "code": 0x0B, "field": "initial_spells", "guide": [], "dump": [0x06],
     "note": "蘭斯洛特的遮罩裡有奔雷彈，攻略站沒有列他"},
]

# 00 敵方 / 01 己方 are the two values assets/spells.md states; 03 appears on 神行術
# and has no documented meaning, so it is deliberately absent rather than guessed.
SPELL_TARGET = {0: "敵方", 1: "己方"}


# ---------------------------------------------------------------------------
# MISC.VFS members, read out of the unpacked tree (story.dump_tree, which uses
# vfs_dump.parse_container, the owner of the container format)
# ---------------------------------------------------------------------------
def u16(rec, at):
    return struct.unpack_from("<H", rec, at)[0]


def i16(rec, at):
    return struct.unpack_from("<h", rec, at)[0]


def decode_item(rec):
    return {
        "type": rec[0],
        "ap": i16(rec, 1), "hit": i16(rec, 3), "dp": i16(rec, 5), "ev": i16(rec, 7),
        "hit_effect": rec[9], "hit_effect_rate": rec[10],
        "range_min": rec[11], "range_max": rec[12],
        "use_effect": rec[13], "use_amount": u16(rec, 14),
        "use_distance": rec[16], "use_target": rec[17], "use_radius": rec[18],
        "price": u16(rec, 19), "select_mode": rec[21], "reserved": rec[22],
    }


def decode_spell(rec):
    return {
        "power": i16(rec, 0), "hit": rec[2], "distance": rec[3],
        "radius": rec[4], "mp": rec[5], "target": rec[6],
    }


def decode_appearance(rec):
    return {
        "race": rec[0], "class_code": rec[1], "level": rec[2],
        "hp_base": i16(rec, 3), "mp_base": i16(rec, 5), "move": rec[7],
        "spell_mask": struct.unpack_from("<I", rec, 8)[0],
        "item_slots": list(rec[12:18]),
        "ap_base": i16(rec, 18), "dp_base": i16(rec, 20), "dx_base": i16(rec, 22),
    }


def decode_levelup(rec):
    return {
        "ap_min": rec[0], "ap_max": rec[1], "dp_min": rec[2], "dp_max": rec[3],
        "dx_min": rec[4], "dx_max": rec[5], "hp_min": rec[6], "hp_max": rec[7],
        "mp_min": rec[8], "mp_max": rec[9], "learn_index": rec[10],
    }


def decode_learn(rec):
    return [{"level": rec[i * 2], "spell": rec[i * 2 + 1]} for i in range(6)]


def decode_class(rec):
    return {
        "move_cost": list(rec[0:8]),
        "critical": rec[8],
        "magic_resist_complement": rec[9],
    }


DECODERS = {
    "item": decode_item, "spell": decode_spell, "appearance": decode_appearance,
    "levelup": decode_levelup, "learn": decode_learn, "class": decode_class,
}


def decode_tables(misc_dir):
    """Decode all six tables from their members, each a whole number of records."""
    out = {}
    for key, (member, stride) in TABLES.items():
        path = Path(misc_dir) / member
        if not path.is_file():
            raise SystemExit("%s is not a member of MISC.VFS" % member)
        data = path.read_bytes()
        if len(data) % stride:
            raise SystemExit("%s is %d bytes, not a multiple of %d" % (member, len(data), stride))
        rows = [DECODERS[key](data[i:i + stride]) for i in range(0, len(data), stride)]
        out[key] = {"member": member, "stride": stride, "records": rows}
    return out


# ---------------------------------------------------------------------------
# Knowledge base tables
# ---------------------------------------------------------------------------
def split_row(line):
    return [c.strip().strip("`").strip() for c in line.strip().strip("|").split("|")]


def is_separator(line):
    return bool(line) and set(line.replace("|", "").replace(" ", "")) <= set("-:")


def read_md_table(path, header):
    """Return the data rows of the markdown table in `path` whose header is `header`."""
    lines = path.read_text(encoding="utf-8").splitlines()
    for i, line in enumerate(lines):
        if not line.startswith("|") or split_row(line) != list(header):
            continue
        if i + 1 >= len(lines) or not is_separator(lines[i + 1]):
            continue
        rows = []
        for row in lines[i + 2:]:
            if not row.startswith("|"):
                break
            rows.append(split_row(row))
        return rows
    raise SystemExit("%s: no table with header %s" % (path, list(header)))


class Check:
    """Counts every label-versus-record comparison, and collects the failures."""

    def __init__(self):
        self.compared = 0
        self.failures = []

    def eq(self, what, expected, actual):
        self.compared += 1
        if expected != actual:
            self.failures.append("%s: knowledge base says %r, data file has %r"
                                 % (what, expected, actual))

    def same(self, what, expected, actual):
        """Like eq, for expectations that come from this script rather than the tables."""
        self.compared += 1
        if expected != actual:
            self.failures.append("%s: the entry says %r, the record holds %r"
                                 % (what, expected, actual))


def record_at(records, index, where):
    """The record a knowledge base row claims to describe, or a readable failure."""
    if index >= len(records):
        raise SystemExit("%s names record %02X but the table holds %d records"
                         % (where, index, len(records)))
    return records[index]


def parse_range(cell):
    lo, hi = cell.split("-")
    return int(lo), int(hi)


def parse_distance(cell):
    """`直線 7` is the 0x10 bit plus the distance; anything else is a plain number."""
    m = re.fullmatch(r"直線\s+(\d+)", cell)
    return 0x10 | int(m.group(1)) if m else int(cell)


def parse_codes(cell):
    return [] if cell == "—" else [int(x, 16) for x in cell.split()]


# ---------------------------------------------------------------------------
# Per-domain assembly.  Each builder parses the knowledge base for the labels,
# checks them against the decoded records, and returns the merged rows.
# ---------------------------------------------------------------------------
ITEM_HEADER = ("編號", "名稱", "類型", "AP", "HIT", "DP", "EV", "距離", "附加屬性", "價格")
ITEM_USE_HEADER = ("編號", "名稱", "K1", "數量", "距離", "範圍", "對象", "選取模式")


def build_items(records, check):
    named = read_md_table(REPO / "assets/items.md", ITEM_HEADER)
    effects = {}
    for row in named:
        code = int(row[0], 16)
        rec = record_at(records, code, "assets/items.md")
        check.eq("item %02X type" % code, int(row[2], 16), rec["type"])
        check.eq("item %02X ap" % code, int(row[3]), rec["ap"])
        check.eq("item %02X hit" % code, int(row[4]), rec["hit"])
        check.eq("item %02X dp" % code, int(row[5]), rec["dp"])
        check.eq("item %02X ev" % code, int(row[6]), rec["ev"])
        check.eq("item %02X range" % code, parse_range(row[7]),
                 (rec["range_min"], rec["range_max"]))
        check.eq("item %02X price" % code, int(row[9]), rec["price"])
        if row[8] != "—":
            parts = row[8].split()
            if rec["hit_effect"] == 0:
                # Letting 0 into the map would name every effect-less item after it.
                raise SystemExit("item %02X is listed with 附加屬性 %s but its hit_effect "
                                 "byte is 0" % (code, parts[0]))
            effects.setdefault(rec["hit_effect"], set()).add(parts[0])
            if len(parts) == 2:
                check.eq("item %02X hit effect rate" % code, int(parts[1].rstrip("%")),
                         rec["hit_effect_rate"])
        else:
            check.eq("item %02X hit effect" % code, 0, rec["hit_effect"])

    codes = [int(row[0], 16) for row in named]
    check.eq("item codes are 00.. contiguous", list(range(len(named))), codes)

    with_use = set()
    for row in read_md_table(REPO / "assets/items.md", ITEM_USE_HEADER):
        code = int(row[0], 16)
        with_use.add(code)
        rec = record_at(records, code, "assets/items.md 使用效果")
        check.eq("item %02X use effect" % code, int(row[2], 16), rec["use_effect"])
        check.eq("item %02X use amount" % code, int(row[3]), rec["use_amount"])
        check.eq("item %02X use distance" % code, parse_distance(row[4]), rec["use_distance"])
        check.eq("item %02X use radius" % code, int(row[5]), rec["use_radius"])
        check.eq("item %02X use target" % code, int(row[6], 16), rec["use_target"])
        check.eq("item %02X select mode" % code, int(row[7]), rec["select_mode"])
    for code in codes:
        if code not in with_use:
            check.eq("item %02X has no use effect" % code, 0, records[code]["use_effect"])

    hit_effect = {}
    for code, labels in sorted(effects.items()):
        if len(labels) != 1:
            raise SystemExit("hit effect %02X is labelled %s" % (code, sorted(labels)))
        hit_effect[code] = labels.pop()

    # A name the game leaves blank is written （空白） in the table; it has no name.
    names = {int(row[0], 16): (None if row[1].startswith("（") else row[1]) for row in named}
    out = []
    for code, rec in enumerate(records):
        row = {"code": code, "code_hex": "%02X" % code, "name": names.get(code)}
        row.update(rec)
        row["hit_effect_name"] = hit_effect.get(rec["hit_effect"])
        row["use_distance_line"] = bool(rec["use_distance"] & 0x10)
        row["use_distance_value"] = rec["use_distance"] & 0x0F
        row["blank"] = code not in names
        out.append(row)
    return out, hit_effect


SPELL_HEADER = ("編號", "名稱", "威力", "命中率", "距離", "範圍", "MP", "對象")


def build_spells(records, check):
    named = read_md_table(REPO / "assets/spells.md", SPELL_HEADER)
    check.eq("spell row count", len(records), len(named))
    out = []
    for row, rec in zip(named, records):
        code = int(row[0], 16)
        check.eq("spell codes are 00.. contiguous", len(out), code)
        m = re.fullmatch(r"AP × (\d+\.\d\d)", row[2])
        power = -round(float(m.group(1)) * 100) if m else int(row[2])
        check.eq("spell %02X power" % code, power, rec["power"])
        check.eq("spell %02X hit" % code, int(row[3]), rec["hit"])
        check.eq("spell %02X distance" % code, parse_distance(row[4]), rec["distance"])
        check.eq("spell %02X radius" % code, int(row[5]), rec["radius"])
        check.eq("spell %02X mp" % code, int(row[6]), rec["mp"])
        check.eq("spell %02X target" % code, int(row[7]), rec["target"])

        merged = {"code": code, "code_hex": "%02X" % code, "name": row[1]}
        merged.update(rec)
        merged["ap_multiplier"] = -rec["power"] / 100 if rec["power"] < 0 else None
        merged["distance_line"] = bool(rec["distance"] & 0x10)
        merged["distance_value"] = rec["distance"] & 0x0F
        merged["target_name"] = SPELL_TARGET.get(rec["target"])
        out.append(merged)
    return out


CLASS_HEADER = ("代碼", "職業", "地形消耗", "暴擊率", "魔法抗性")


def build_classes(records, check):
    named = read_md_table(REPO / "assets/classes.md", CLASS_HEADER)
    out = []
    for row in named:
        code = int(row[0], 16)
        check.eq("class codes are 00.. contiguous", len(out), code)
        # The guide's class 00 sits one record in: record 0 of PROMAP.DAT is a default.
        rec = record_at(records, code + 1, "assets/classes.md")
        check.eq("class %02X move cost" % code, [int(x, 16) for x in row[2].split()],
                 rec["move_cost"])
        check.eq("class %02X critical" % code, int(row[3]), rec["critical"])
        check.eq("class %02X magic resist" % code, int(row[4].rstrip("%")),
                 100 - rec["magic_resist_complement"])
        out.append({
            "code": code, "code_hex": "%02X" % code, "name": row[1],
            "record_index": code + 1,
            "move_cost": rec["move_cost"], "critical": rec["critical"],
            "magic_resist": 100 - rec["magic_resist_complement"],
            "magic_resist_complement": rec["magic_resist_complement"],
        })
    check.eq("class record count", len(named) + 1, len(records))
    return out


APPEARANCE_HEADER = ("索引", "人物／職業", "種族", "職業", "等級", "HP 基礎", "MP 基礎",
                     "移動力", "法術", "物品", "AP 基礎", "DP 基礎", "DX 基礎")
LEVELUP_HEADER = ("索引", "人物／職業", "AP", "DP", "DX", "HP", "MP", "習得索引")


def mask_to_spells(mask):
    return [i for i in range(32) if mask >> i & 1]


def build_characters(appearance, levelup, learn, spell_names, item_names, class_names, game,
                     check):
    """FRIAPRDA/FRILEVUP/GETMGTAB merged per index, plus RANKUP routes and deployments.

    `game` is tools/data_tables' Game: which indices the program reads, the routes the
    church offers, and the deployment census all come from it."""
    import data_tables
    path = REPO / "assets/characters.md"

    documented, appearance_labels = set(), {}
    for row in read_md_table(path, APPEARANCE_HEADER):
        index = int(row[0], 16)
        documented.add(index)
        appearance_labels[index] = row[1]
        rec = record_at(appearance, index, "assets/characters.md 出場屬性")
        check.eq("character %02X race" % index, int(row[2], 16), rec["race"])
        check.eq("character %02X class" % index, int(row[3], 16), rec["class_code"])
        check.eq("character %02X level" % index, int(row[4]), rec["level"])
        check.eq("character %02X hp base" % index, int(row[5]), rec["hp_base"])
        check.eq("character %02X mp base" % index, int(row[6]), rec["mp_base"])
        check.eq("character %02X move" % index, int(row[7]), rec["move"])
        check.eq("character %02X spells" % index, parse_codes(row[8]),
                 mask_to_spells(rec["spell_mask"]))
        check.eq("character %02X items" % index, parse_codes(row[9]),
                 [x for x in rec["item_slots"] if x != 0xFF])
        check.eq("character %02X ap base" % index, int(row[10]), rec["ap_base"])
        check.eq("character %02X dp base" % index, int(row[11]), rec["dp_base"])
        check.eq("character %02X dx base" % index, int(row[12]), rec["dx_base"])
    # The appearance table lists exactly the indices the program reads: a row
    # nothing reads would ship look-alike values, a missing reader would ship none.
    readers = game.base_record_readers()
    check.compared += 1
    if readers != sorted(documented):
        check.failures.append("assets/characters.md 出場屬性 lists indices %s, the program "
                              "reads %s" % (["%02X" % i for i in sorted(documented)],
                                            ["%02X" % i for i in readers]))

    labels = {}
    for row in read_md_table(path, LEVELUP_HEADER):
        index = int(row[0], 16)
        labels[index] = row[1]
        rec = record_at(levelup, index, "assets/characters.md 升級成長")
        for cell, lo, hi in ((row[2], "ap_min", "ap_max"), (row[3], "dp_min", "dp_max"),
                             (row[4], "dx_min", "dx_max"), (row[5], "hp_min", "hp_max"),
                             (row[6], "mp_min", "mp_max")):
            check.eq("character %02X %s" % (index, lo), parse_range(cell), (rec[lo], rec[hi]))
        check.eq("character %02X learn index" % index, int(row[7], 16), rec["learn_index"])

    # The per-character spell table (learn indices and schedules) is generated by
    # tools/data_tables (spell_schedule); data_tables.check in kb_gates compares
    # it with the same GETMGTAB.DAT these records are decoded from.

    out = []
    for index, growth in enumerate(levelup):
        label = labels.get(index) or appearance_labels.get(index, "")
        character, _, class_label = label.partition("／")
        if character.startswith("（"):
            character = ""          # （空白）: the game gives this index no name
        learned = []
        if growth["learn_index"] != 0xFF:
            for p in record_at(learn, growth["learn_index"],
                               "FRILEVUP.DAT %02X 的習得索引" % index):
                if p["level"] != 0xFF:
                    learned.append({"level": p["level"], "spell": p["spell"],
                                    "spell_name": spell_names.get(p["spell"])})
        promotions = []
        if index < len(game.promotions):
            for route, (form, clazz, move) in data_tables.offered_routes(index,
                                                                          game.promotions[index]):
                promotions.append({"route": route, "route_name": data_tables.ROUTES[route],
                                   "form": form, "class_code": clazz,
                                   "class_name": class_names.get(clazz), "move_bonus": move})
        chapters, scenes = game.deployed_maps(index)
        row = {
            "code": index, "code_hex": "%02X" % index,
            "name": character or None, "class": class_label or None,
            "label": label or None,
            # Appearance values only for the indices the program reads (the
            # assets/characters.md table, checked against the readers above).  The
            # other FRIAPRDA.DAT rows are byte copies nothing reads -- a promoted
            # form's index lands on such a copy -- see that document.
            "appearance_documented": index in documented,
            "learn_index": growth["learn_index"],
            "learn": learned,
            "promotions": promotions,
            "deployed_chapters": chapters, "deployed_scene_maps": scenes,
        }
        app = appearance[index] if index in documented else None
        row.update({
            "class_code": app["class_code"] if app else None,
            "class_name": class_names.get(app["class_code"]) if app else None,
            "race": app["race"] if app else None,
            "race_name": (game.race_name(app["race"]) or None) if app else None,
            "level": app["level"] if app else None,
            "move": app["move"] if app else None,
            "hp_base": app["hp_base"] if app else None,
            "mp_base": app["mp_base"] if app else None,
            "ap_base": app["ap_base"] if app else None,
            "dp_base": app["dp_base"] if app else None,
            "dx_base": app["dx_base"] if app else None,
            "initial_spells": [{"code": s, "name": spell_names.get(s)}
                               for s in mask_to_spells(app["spell_mask"])] if app else [],
            "initial_items": [{"code": i, "name": item_names.get(i)}
                              for i in app["item_slots"] if i != 0xFF] if app else [],
        })
        row.update({k: growth[k] for k in growth if k != "learn_index"})
        row["blank"] = index not in labels and index not in documented
        out.append(row)
    return out


def check_discrepancies(merged, check):
    """A stale entry would put a wrong `攻略站寫…` note on a record, so pin the dump side."""
    for d in DISCREPANCIES:
        rec = next((r for r in merged[d["table"]] if r["code"] == d["code"]), None)
        if rec is None:
            raise SystemExit("discrepancy names %s %02X, which is not a record"
                             % (d["table"], d["code"]))
        field = d["field"]
        if field == "initial_spells":
            actual = [s["code"] for s in rec[field]]
        elif field.endswith("]"):
            name, _, index = field[:-1].partition("[")
            actual = rec[name][int(index)]
        else:
            actual = rec[field]
        check.same("discrepancy %s %02X %s" % (d["table"], d["code"], field),
                   d["dump"], actual)


# ---------------------------------------------------------------------------
# The tables tools/data_tables owns.  Its `check` compares the assets/ tables
# with its generators cell by cell; build() runs it on the same Game these
# records come from, so the records and the knowledge base cannot differ.
# ---------------------------------------------------------------------------
def coded_name(code_value, name):
    return {"code": code_value, "name": name or None}


def build_enemies(game):
    """ENEMYDAT.DAT, one record per row, keyed by portrait id like the map data."""
    import data_tables
    template = game.template_enemy_rows()
    out = []
    for row, e in enumerate(game.enemies):
        pid = data_tables.ENEMY_BASE + row
        chapters, scenes = game.deployed_maps(pid)
        out.append({
            "code": pid, "code_hex": "%02X" % pid, "row": row,
            "name": game.unit_label(pid),
            "race": e["race"], "race_name": game.race_name(e["race"]) or None,
            "class_code": e["class"], "class_name": game.class_name(e["class"]) or None,
            "hp": e["hp"], "mp": e["mp"], "ap": e["ap"], "dp": e["dp"], "dx": e["dx"],
            "mv": e["mv"], "exp": e["exp"],
            "deployed_chapters": chapters, "deployed_scene_maps": scenes,
            "deployed": bool(chapters or scenes),
            "template": row in template,
        })
    return out


def build_races(game):
    """The race names, and which deployed units are of each race."""
    import data_tables
    out = []
    for race in range(data_tables.NAME_BASES["class"] - data_tables.NAME_BASES["race"]):
        name = game.race_name(race)
        if not name:
            continue
        ours = [c for c in game.base_record_readers() if game.appearance[c]["race"] == race]
        theirs = [data_tables.ENEMY_BASE + i for i, e in enumerate(game.enemies)
                  if e["race"] == race and game.deployments(data_tables.ENEMY_BASE + i)]
        out.append({"code": race, "code_hex": "%02X" % race, "name": name,
                    "text_entry": data_tables.NAME_BASES["race"] + race,
                    "party_and_guest_units": [coded_name(c, game.unit_label(c)) for c in ours],
                    "enemy_and_npc_units": [coded_name(c, game.unit_label(c)) for c in theirs]})
    return out


SHOP_ROWS = ("item_shop", "weapon_shop", "secret_shop")   # 道具店、武器店、秘密商店


def build_shops(game):
    """The villages' SHOPnn.DAT, keyed by the chapter index the file is named for."""
    out = []
    for chapter in game.village_chapters():
        rec = {"code": chapter, "code_hex": "%02X" % chapter, "name": "SHOP%02d.DAT" % chapter,
               "after_chapter": chapter, "before_chapter": chapter + 1}
        for key, stock in zip(SHOP_ROWS, game.shops[chapter]):
            rec[key] = [coded_name(i, game.item_name(i)) for i in stock]
        out.append(rec)
    return out


def build_use_effects(game):
    """ITEM.DAT's use-effect codes, as src/item.c handles them (data_tables.USE_EFFECTS)."""
    import data_tables
    carried = collections.defaultdict(list)
    for item, rec in enumerate(game.items):
        if rec["use_effect"]:
            carried[rec["use_effect"]].append(item)
    out = []
    for effect in sorted(set(data_tables.USE_EFFECTS) | set(data_tables.PICKER_ONLY_EFFECTS)):
        if effect in data_tables.USE_EFFECTS:
            what, consumed = data_tables.USE_EFFECTS[effect]
        else:
            what, consumed = data_tables.PICKER_ONLY_EFFECTS[effect], None
        out.append({"code": effect, "code_hex": "%02X" % effect, "description": what,
                    "consumed": consumed,
                    "items": [coded_name(i, game.item_name(i)) for i in carried.get(effect, [])]})
    return out


# ---------------------------------------------------------------------------
# Chapters, out of tools/chapter_docs/chapter_facts and the landed judgements.
# The chapter pages hold the same facts rendered; chapter_page_problems() is the
# gate that they are current with the data these records come from.
# ---------------------------------------------------------------------------
def _function(cf, name):
    if name is None:
        return None
    addr = cf.snapshot_addresses().get(name)
    return {"name": name, "address": "0x%x" % addr if addr is not None else None}


def _wave_verdict(cf, judgement, wave):
    """(deployed, when, why) for a wave; deployed None when no judgement covers it."""
    if wave == cf.OPENING_WAVE:
        return True, "進入戰場時由 fdps_build_map_unit_array 部署在錨點上", None
    for w in (judgement or {}).get("waves", []):
        if isinstance(w, dict) and w.get("wave") == wave and isinstance(w.get("deployed"), bool):
            return w["deployed"], w.get("when"), w.get("why")
    return None, None, None


def build_chapter(n, src):
    # chapter_facts' _ai_text and _record_text are the page's wording for an AI
    # byte and a search record; imported rather than copied (tools/_index.md).
    import map_decode
    cf, g = src.facts, src.game
    map_no = n - 1
    m = cf.battle_map(map_no)
    dat, cod = m["dat"], m["cod"]
    judgement = cf.load_judgement(n)
    tables = cf.handler_tables()
    width, height = map_decode.grid_size(m)
    anchors = cod[:dat["spawn_count"]] if cod else []

    waves, deployed_wave = [], {}
    for w in cf.wave_list(m):
        deployed, when, why = _wave_verdict(cf, judgement, w)
        deployed_wave[w] = deployed
        waves.append({"wave": w, "deployed": deployed, "when": when, "why": why,
                      "records": [s["index"] for s in dat["spawns"] if s["wave"] == w]})

    def unit(s):
        return {"index": s["index"], "wave": s["wave"], "deployed": deployed_wave[s["wave"]],
                "char_id": s["char_id"], "code_hex": "%02X" % s["char_id"],
                "name": g.unit_label(s["char_id"])}

    deployments = []
    for s in dat["spawns"]:
        anchor = anchors[s["index"]] if s["index"] < len(anchors) else None
        rec = unit(s)
        rec.update({"side": s["side"], "side_name": cf.SIDE.get(s["side"]), "level": s["level"],
                    "ai": s["ai"], "ai_text": cf._ai_text(s, g.unit_label),
                    "anchor": [anchor[1], anchor[2]] if anchor else None,
                    "death_op": s["death_op"], "death_arg": s["death_arg"],
                    "death": None if s["death_op"] == 0xFF else cf.death_cell(n, s)})
        deployments.append(rec)

    by_code = collections.defaultdict(list)
    for c in map_decode.searchable_cells(m):
        by_code[c["code"]].append(c)
    treasure = []
    for code in sorted(by_code):
        group, rec = by_code[code], by_code[code][0]["record"]
        entry = {"code": code,
                 "cells": [{"x": c["x"], "y": c["y"], "kind": cf.CELL_KIND[c["kind"]]}
                           for c in group],
                 "record_kind": rec["kind"], "content": cf._record_text(rec),
                 "shared": len(group) > 1}
        if rec["kind"] == 0:
            entry.update(item=rec["payload"], item_name=g.item_name(rec["payload"]) or None)
        elif rec["kind"] == 1:
            entry.update(gold=rec["payload"])
        else:
            entry.update(event_slot=rec["payload"],
                         handler=_function(cf, cf.event_handler(rec["payload"])))
        treasure.append(entry)
    unreachable = [{"record": i, "kind": r["kind"], "payload": r["payload"]}
                   for i, r in enumerate(dat["search"])
                   if i not in by_code and (r["kind"], r["payload"]) != (0, 0)]
    drops = []
    for s in dat["spawns"]:
        if s["death_op"] in (0, 1):
            rec = unit(s)
            if s["death_op"] == 0:
                rec.update(item=s["death_arg"], item_name=g.item_name(s["death_arg"]) or None)
            else:
                rec.update(gold=s["death_arg"])
            rec["content"] = cf.death_cell(n, s)
            drops.append(rec)

    turn_events = []
    for t in dat["turn_events"]:
        if (t["turn"], t["handler"]) != (0xFF, 0xFF):
            turn_events.append({"turn": t["turn"], "side": t["side"],
                                "phase": cf.PHASE.get(t["side"]), "slot": t["handler"],
                                "handler": _function(cf, cf.event_handler(t["handler"]))})
    cells = collections.defaultdict(list)
    for c in map_decode.all_cells(m):
        e = c.get("tile_event")
        if e and e["handler"] is not None:
            cells[c["code"]].append(c)
    cell_events = []
    for code in sorted(cells):
        e = cells[code][0]["tile_event"]
        cell_events.append({"code": code, "cells": [[c["x"], c["y"]] for c in cells[code]],
                            "trigger": cf.TRIGGER.get(e["trigger"], e["trigger"]),
                            "slot": e["handler"],
                            "handler": _function(cf, cf.event_handler(e["handler"]))})
    death_events = []
    for s in dat["spawns"]:
        if 2 <= s["death_op"] < 0xFF:
            rec = unit(s)
            rec["death"] = cf.death_cell(n, s)
            death_events.append(rec)

    scripts = []
    for member, r, mine in cf.chapter_scripts(n):
        callers = []
        for c in mine:
            caller = {"kind": cf.KIND_LABEL[c.kind], "function": c.function}
            if caller not in callers:
                callers.append(caller)
        scripts.append({"member": member, "callers": callers, "size": r.size,
                        "steps": len(r.steps), "initial_map": r.initial_map,
                        "switches": list(r.trace.switches), "final_map": r.trace.final_map})

    village = cf.village_before(n)
    return {
        "code": n, "code_hex": "%02X" % n, "chapter": n, "index": map_no,
        "name": cf.chapter_title(n),
        "win": cf.one_line(cf.entry_line(n, 2)), "lose": cf.one_line(cf.entry_line(n, 3)),
        "doc": "chapters/ch%02d.md" % n,
        "judged": judgement is not None,
        "map": map_no, "map_size": [width, height], "player_slots": dat["player_slots"],
        "spawn_count": dat["spawn_count"],
        "text_block": "FDETXT%02d.TXT" % n, "text_entries": len(cf.text_block(n)),
        "handlers": {k: _function(cf, tables[k][map_no]) for k in ("init", "post", "end")},
        "event_slots": [{"slot": slot, "handler": _function(cf, cf.event_handler(slot)),
                         "used_by": where} for slot, where in cf.event_slots_used(m).items()],
        "village_shop": village[0] if village else None,
        "disc": cf.disc_of(map_no),
        "waves": waves, "deployments": deployments,
        "treasure": treasure, "unreachable_search_records": unreachable, "drops": drops,
        "turn_events": turn_events, "cell_events": cell_events, "death_events": death_events,
        "scripts": scripts,
    }


def build_chapters(src):
    return [build_chapter(n, src) for n in src.facts.CHAPTERS]


# ---------------------------------------------------------------------------
# Every text entry, with who shows it or who owns it for not being shown
# ---------------------------------------------------------------------------
def _owner(owner_id, cut_entries, exclusions):
    """The cut_content/ entry (or exclusion) a never-shown entry belongs to."""
    import cut_content
    if owner_id is None:
        return None
    if owner_id in cut_entries:
        e = cut_entries[owner_id]
        return {"id": owner_id, "title": e.title, "category": e.category,
                "page": "cut_content/" + cut_content.TOPICS[e.topic][0]}
    if owner_id in exclusions:
        return {"id": owner_id, "title": None, "category": "排除清單",
                "page": "cut_content/_index.md"}
    return {"id": owner_id, "title": None, "category": None, "page": None}


def _global_readers():
    """FDETXT00's readers: ({entry: [function]} for fixed ids, {base: {function}}
    for the name regions a code indexes)."""
    import global_text
    fixed = collections.defaultdict(list)
    indexed = collections.defaultdict(set)
    for r in global_text.scan_readers():
        if r.ids is not None:
            for entry in r.ids:
                if r.function not in fixed[entry]:
                    fixed[entry].append(r.function)
        else:
            indexed[r.base].add(r.function)
    return fixed, indexed


def _region_of(entry):
    import global_text
    for region in global_text.REGIONS:
        if region.first <= entry <= region.last:
            group = next((title for first, last, title in global_text.MESSAGE_GROUPS
                          if first <= entry <= last), None)
            return region, group
    return None, None


# Every entry ships with one of these; anything else fails the build.
TEXT_STATUSES = ("shown", "never_shown", "empty")


def build_text(src):
    import cut_content
    import global_text
    import story
    cf, sg = src.facts, src.story
    never, unsettled = story.never_shown_text(sg)
    cut_entries = {e.id: e for e in cut_content.collect(cut_content.CUT_DIR)[0]}
    exclusions = set(cut_content.exclusion_ids(
        (cut_content.CUT_DIR / "_index.md").read_text(encoding="utf-8")))
    scene = global_text.scene_refs(list(cf.scripts().values()))
    fixed, indexed = _global_readers()

    blocks = []
    for number in range(global_text.LAST_SCENE_BLOCK + 1):
        chapter = number in cf.CHAPTERS
        judged = {}
        for item in ((cf.load_judgement(number) or {}) if chapter else {}).get("text_readers", []):
            judged.setdefault(cf.parse_entry(item["entry"]), []).append(item["reader"])
        scanned = cf.text_readers(number) if chapter else {}
        scene_readers = collections.defaultdict(list)
        if number >= global_text.FIRST_SCENE_BLOCK:
            for script, offset, entry in scene.get(number, []):
                label = "`%s` `0x%03x`" % (script, offset)
                if label not in scene_readers[entry]:
                    scene_readers[entry].append(label)
        out = []
        for i in range(len(cf.text_block(number))):
            lines = cf.transcript(number, i)
            region = group = None
            if number == 0:
                region, group = _region_of(i)
                readers = list(fixed.get(i, []))
                if region is not None and region.base is not None:
                    readers.append("%s：程式以%s + 0x%03x 取這一條（%d 個函式）"
                                   % (region.title, region.code_name, region.base,
                                      len(indexed.get(region.base, ()))))
            elif chapter:
                readers = list(scanned.get(i, [])) + judged.get(i, [])
            else:
                readers = list(scene_readers.get(i, []))
            if (number, i) in never:
                status = "never_shown"
            elif not lines:
                status = "empty"
            elif readers:
                status = "shown"
            else:
                status = "unsettled" if number in unsettled else "no_reader"
            rec = {"entry": i, "lines": lines, "status": status, "readers": readers,
                   "owner": _owner(story.OWNERS.get((number, i)), cut_entries, exclusions)
                   if status == "never_shown" else None}
            if number == 0:
                rec["region"] = region.title if region else None
                rec["group"] = group
            out.append(rec)
        kind = ("global" if number == 0 else "chapter" if chapter
                else "scene" if number >= global_text.FIRST_SCENE_BLOCK else "other")
        blocks.append({"block": number, "name": "FDETXT%02d.TXT" % number, "kind": kind,
                       "entries": out})
    return blocks


# ---------------------------------------------------------------------------
# Knowledge-base gates owned by other tools, run against this build's data
# ---------------------------------------------------------------------------
def kb_gates(src, check_chapter_pages):
    """Problems (strings) between the knowledge base and the data the records
    are built from.  Each gate belongs to the tool that writes those pages."""
    import data_tables
    import global_text
    import story
    problems = ["data_tables: " + p for p in data_tables.check(src.game)]
    for path, text in global_text.build_pages(src.game_dir).items():
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            problems.append("global_text: %s differs from a rebuild" % path.relative_to(REPO))
    owner_problems, pending, _, unsettled, _ = story.ownership_problems(src.story)
    problems += ["story: " + p for p in owner_problems]
    problems += ["story: FDETXT%02d 0x%02x owner pending (%s)" % (k[0], k[1], why)
                 for k, why in pending]
    problems += ["story: chapter block FDETXT%02d has no judgement" % n for n in sorted(unsettled)]
    for n in src.facts.CHAPTERS:
        judgement = src.facts.load_judgement(n)
        if judgement is not None:
            problems += ["chapter %d judgement: %s" % (n, p)
                         for p in src.facts.validate_judgement(n, judgement)]
    if not owner_problems:
        page = story.PAGE.read_text(encoding="utf-8")
        new, block_problems = story.apply_blocks(page, story.render_blocks(src.story))
        problems += ["story: " + p for p in block_problems]
        if new != page:
            problems.append("story: cut_content/story.md has a stale generated block")
    if check_chapter_pages:
        problems += chapter_page_problems(src)
    return problems


def chapter_page_problems(src):
    """chapters/chNN.md and chapters/_index.md against a fresh regeneration."""
    import check_chapter
    import index as chapter_index
    problems = []
    for n in src.facts.CHAPTERS:
        path = check_chapter.LANDED / ("ch%02d.md" % n)
        judgement = src.facts.load_judgement(n)
        if not path.exists() or judgement is None:
            problems.append("chapters: ch%02d has no landed page or judgement" % n)
            continue
        text = path.read_text(encoding="utf-8")
        fresh = check_chapter.regions(check_chapter.fill(text, n, judgement))
        for key, body in check_chapter.regions(text).items():
            if fresh.get(key) != body:
                problems.append("chapters: ch%02d.md block %s is stale" % (n, key))
    current = chapter_index.INDEX.read_text(encoding="utf-8")
    fresh = check_chapter.regions(chapter_index.build_text(current))
    for key, body in check_chapter.regions(current).items():
        if fresh.get(key) != body:
            problems.append("chapters: _index.md table %s is stale" % key)
    return problems


# ---------------------------------------------------------------------------
# Loading everything once, and the build
# ---------------------------------------------------------------------------
class Sources:
    """The game files unpacked once, and the owner modules pointed at them."""

    loaded = None

    def __init__(self, game_dir):
        import chapter_facts
        import data_tables
        import story            # owner of the never-shown text, and of dump_tree
        self.game_dir = Path(game_dir)
        if Sources.loaded not in (None, self.game_dir.resolve()):
            # chapter_facts caches what it loaded from the first tree
            raise BuildError("one build per process: %s was loaded already" % Sources.loaded)
        Sources.loaded = self.game_dir.resolve()
        self.dump = story.dump_tree(self.game_dir)
        # chapter_facts reads its inputs through these two module globals; point
        # them at this build's game files before its first (cached) load.
        chapter_facts.DUMP = self.dump
        chapter_facts.GAME = self.game_dir
        self.facts = chapter_facts
        self.game = data_tables.load(self.dump)
        self.story = story.Game(self.game_dir)

    def read(self, name):
        import cut_content
        return cut_content.read_game_file(self.game_dir, name)


class BuildError(Exception):
    """The data and the knowledge base disagree; nothing is written."""


PROVENANCE = {
    "dump": "遊戲檔（MISC.VFS、FIELD.VFS、FIELD1.VFS、FIELD2.VFS、ICONANI.VFS）成員的實際 byte",
    "game_text": "遊戲內文字 FDETXT00.TXT 的名稱條目（物品、法術、職業、人物的名稱經 assets/ 的表"
                 "轉錄，tools/data_tables 的 check 逐條對過遊戲內文字；敵人、種族、商店的名稱"
                 "直接取自 FDETXT00），章名與勝敗條件取自各章的文字區塊",
    "src": "從 src/ 轉錄的說明（使用效果代碼的效果，tools/data_tables 的 USE_EFFECTS）",
    "judgement": "資料本身判斷不了的事——波次會不會部署、文字由誰顯示、為什麼不會顯示——"
                 "取自章節頁的逐章判定（tools/chapter_docs/judgements/）與 cut_content 的歸屬"
                 "（tools/cut_content/story.py 的 OWNERS）",
    "guide": "攻略站的說法，只出現在 discrepancies",
    "rule": "數值出自遊戲檔，名稱出自遊戲內文字；攻略站與資料檔不一致的地方以資料檔為準，"
            "全部列在 discrepancies",
}
SOURCE_FILES = ("MISC.VFS", "FIELD.VFS", "FIELD1.VFS", "FIELD2.VFS", "ICONANI.VFS")


def build(game_dir, check_chapter_pages=True):
    """(data, text): the two data sets.  Raises BuildError when a label row does
    not match its record or a knowledge-base gate fails."""
    src = Sources(game_dir)
    decoded = decode_tables(src.dump / "MISC")
    check = Check()

    items, hit_effect = build_items(decoded["item"]["records"], check)
    spells = build_spells(decoded["spell"]["records"], check)
    classes = build_classes(decoded["class"]["records"], check)
    spell_names = {s["code"]: s["name"] for s in spells}
    item_names = {i["code"]: i["name"] for i in items if i["name"]}
    class_names = {c["code"]: c["name"] for c in classes}
    characters = build_characters(decoded["appearance"]["records"],
                                  decoded["levelup"]["records"],
                                  decoded["learn"]["records"],
                                  spell_names, item_names, class_names, src.game, check)
    use_effects = build_use_effects(src.game)
    effect_text = {u["code"]: u["description"] for u in use_effects}
    for item in items:
        item["use_effect_description"] = effect_text.get(item["use_effect"])
    check_discrepancies({"item": items, "spell": spells, "character": characters,
                         "class": classes}, check)
    if check.failures:
        raise BuildError("\n".join("MISMATCH  " + f for f in check.failures)
                         + "\n%d of %d label checks failed" % (len(check.failures),
                                                                check.compared))
    problems = kb_gates(src, check_chapter_pages)
    blocks = build_text(src)
    problems += ["text: FDETXT%02d 0x%02x is %s" % (b["block"], e["entry"], e["status"])
                 for b in blocks for e in b["entries"] if e["status"] not in TEXT_STATUSES]
    if problems:
        raise BuildError("\n".join("KB  " + p for p in problems))

    def table(canon, member, index, name_source, value_source, records):
        return {"canon": canon, "member": member, "index": index,
                "name_source": name_source, "value_source": value_source, "records": records}

    data = {
        "generator": "tools/data_skill/build.py",
        "source": {name: {"size": len(blob), "sha256": hashlib.sha256(blob).hexdigest()}
                   for name, blob in ((n, src.read(n)) for n in SOURCE_FILES)},
        "provenance": PROVENANCE,
        "label_checks": check.compared,
        "chapter_page_gate": check_chapter_pages,
        "enums": {
            "hit_effect": {"%02X" % k: v for k, v in sorted(hit_effect.items())},
            "spell_target": {"%02X" % k: v for k, v in sorted(SPELL_TARGET.items())},
        },
        "discrepancies": DISCREPANCIES,
        "tables": {
            "item": table("assets/items.md", "ITEM.DAT", "物品編號", "game_text", "dump", items),
            "spell": table("assets/spells.md", "MAGICDAT.DAT", "法術編號", "game_text", "dump",
                           spells),
            "character": table("assets/characters.md",
                               "FRIAPRDA.DAT + FRILEVUP.DAT + GETMGTAB.DAT + RANKUP.DAT",
                               "肖像編號", "game_text", "dump", characters),
            "class": table("assets/classes.md", "PROMAP.DAT", "職業代碼", "game_text", "dump",
                           classes),
            "enemy": table("assets/enemies.md", "ENEMYDAT.DAT", "肖像編號", "game_text", "dump",
                           build_enemies(src.game)),
            "race": table("assets/races.md", "FDETXT00.TXT", "種族代碼", "game_text", "dump",
                          build_races(src.game)),
            "shop": table("assets/shops.md", "SHOPnn.DAT", "章節索引", "game_text", "dump",
                          build_shops(src.game)),
            "use_effect": table("assets/items.md", "ITEM.DAT +0x0d", "使用效果代碼", "src", "src",
                                use_effects),
            "chapter": table("chapters/_index.md", "MAPnn.DAT + ICONANI.VFS + FDETXTnn.TXT",
                             "章號", "game_text", "dump + judgement", build_chapters(src)),
        },
    }
    text = {
        "generator": "tools/data_skill/build.py",
        "provenance": {
            "lines": "tools/text_decode 解出、以章節頁的方式逐行呈現：【名稱】換說話者、▼ 換頁、"
                     "{subst1}／{subst2}／{number} 是執行期代入",
            "readers": "FDETXT00：tools/global_text 的 src/ 掃描；章節區塊：chapter_facts 的"
                       "讀取端掃描加逐章判定；額外場景區塊：過場腳本的追蹤",
            "owner": "永遠不會顯示的條目歸哪個 cut_content 條目（tools/cut_content/story.py 的 OWNERS）",
        },
        "blocks": blocks,
    }
    return data, text


def write(out_dir, data, text):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, content in ((DATA_FILE, data), (TEXT_FILE, text)):
        with (out_dir / name).open("w", encoding="utf-8", newline="\n") as fh:
            json.dump(content, fh, indent=1, ensure_ascii=False)
            fh.write("\n")


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=DEFAULT_GAME)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--no-chapter-page-gate", action="store_true",
                    help="build even when chapters/ is not current with the data")
    a = ap.parse_args(argv)
    try:
        data, text = build(a.game, check_chapter_pages=not a.no_chapter_page_gate)
    except BuildError as error:
        print(error)
        print("FAIL: nothing written")
        return 1
    write(a.out, data, text)
    for name, table in data["tables"].items():
        print("%-10s %4d records" % (name, len(table["records"])))
    entries = sum(len(b["entries"]) for b in text["blocks"])
    print("text       %4d entries in %d blocks" % (entries, len(text["blocks"])))
    print("%d label checks passed, %d known guide discrepancies, knowledge-base gates clean%s"
          % (data["label_checks"], len(DISCREPANCIES),
             "" if data["chapter_page_gate"] else " (chapter pages not checked)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
