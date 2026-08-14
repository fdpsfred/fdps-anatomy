"""Build the queryable game-data set that ships with the `fdps-data` skill.

Numbers come from the real bytes of `MISC.VFS`.  Names come from the knowledge base
tables under `assets/` and `chapters/`, which own the hand-transcribed guide labels
(the guide itself is never parsed -- see ADR-0006).

Every label row is checked against the record it claims to name, so a table that has
drifted, or a parse that has slipped a row, fails loudly instead of shipping a data
set with the wrong names on the wrong records.

Usage:
    python tools/data_skill/build.py <MISC.VFS> <output json>
"""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

VFS_HEADER = 35
VFS_ENTRY = 26

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
# MISC.VFS
# ---------------------------------------------------------------------------
def read_entries(data):
    """Return [(name, offset, size)] from a VFS container held in memory."""
    if data[:3] != b"VFS":
        raise SystemExit("not a VFS container")
    table = struct.unpack_from("<H", data, 5)[0]
    count = struct.unpack_from("<I", data, 7)[0]
    out = []
    for i in range(count):
        base = table + i * VFS_ENTRY
        name = data[base:base + 13].split(b"\0")[0].decode("ascii")
        size = struct.unpack_from("<I", data, base + 13)[0]
        offset = struct.unpack_from("<I", data, base + 22)[0]
        out.append((name, offset, size))
    return out


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


def decode_tables(data):
    """Decode all six tables straight from their members' first byte."""
    by_name = {name: (offset, size) for name, offset, size in read_entries(data)}
    out = {}
    for key, (member, stride) in TABLES.items():
        if member not in by_name:
            raise SystemExit("%s is not a member of this container" % member)
        offset, size = by_name[member]
        if size % stride:
            raise SystemExit("%s is %d bytes, not a multiple of %d" % (member, size, stride))
        rows = [DECODERS[key](data[offset + i * stride:offset + (i + 1) * stride])
                for i in range(size // stride)]
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

    def done(self):
        if self.failures:
            for f in self.failures:
                print("MISMATCH  " + f)
            raise SystemExit("%d of %d label checks failed" % (len(self.failures), self.compared))
        return self.compared


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

    names = {int(row[0], 16): row[1] for row in named}
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
LEARN_HEADER = ("習得索引", "人物／職業", "習得")


def mask_to_spells(mask):
    return [i for i in range(32) if mask >> i & 1]


def build_characters(appearance, levelup, learn, spell_names, item_names, class_names, check):
    path = REPO / "assets/characters.md"

    documented = set()
    for row in read_md_table(path, APPEARANCE_HEADER):
        index = int(row[0], 16)
        documented.add(index)
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

    for row in read_md_table(path, LEARN_HEADER):
        index = int(row[0], 16)
        pairs = [(int(lv), int(sp, 16))
                 for lv, sp in re.findall(r"Lv(\d+)\s*→\s*`?([0-9A-F]{2})`?", row[2])]
        check.eq("learn %02X" % index, pairs,
                 [(p["level"], p["spell"])
                  for p in record_at(learn, index, "assets/characters.md 法術習得")
                  if p["level"] != 0xFF])

    out = []
    for index, growth in enumerate(levelup):
        label = labels.get(index, "")
        character, _, class_label = label.partition("／")
        if label.startswith("（"):
            character, class_label = "", ""
        learned = []
        if growth["learn_index"] != 0xFF:
            for p in record_at(learn, growth["learn_index"],
                               "FRILEVUP.DAT %02X 的習得索引" % index):
                if p["level"] != 0xFF:
                    learned.append({"level": p["level"], "spell": p["spell"],
                                    "spell_name": spell_names.get(p["spell"])})
        row = {
            "code": index, "code_hex": "%02X" % index,
            "name": character or None, "class": class_label or None,
            "label": label or None,
            # Only the twelve rows assets/characters.md documents carry appearance
            # values.  The rest of FRIAPRDA.DAT is duplicated template rows that mean
            # nothing per index -- see that document.
            "appearance_documented": index in documented,
            "learn_index": growth["learn_index"],
            "learn": learned,
        }
        app = appearance[index] if index in documented else None
        row.update({
            "class_code": app["class_code"] if app else None,
            "class_name": class_names.get(app["class_code"]) if app else None,
            "race": app["race"] if app else None,
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
        row["blank"] = index not in labels
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


CHAPTER_HEADER = ("章號", "章節索引", "標題", "文件")


def build_chapters(check):
    rows = read_md_table(REPO / "chapters/_index.md", CHAPTER_HEADER)
    out = []
    for row in rows:
        chapter, index = int(row[0]), int(row[1])
        check.eq("chapter %d index" % chapter, chapter - 1, index)
        out.append({"code": chapter, "code_hex": "%02X" % chapter, "chapter": chapter,
                    "index": index, "name": row[2],
                    "doc": None if row[3] == "（尚無）" else row[3]})
    return out


# ---------------------------------------------------------------------------
def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    misc = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    data = misc.read_bytes()
    decoded = decode_tables(data)
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
                                  spell_names, item_names, class_names, check)
    chapters = build_chapters(check)
    check_discrepancies({"item": items, "spell": spells, "character": characters,
                         "class": classes, "chapter": chapters}, check)
    compared = check.done()

    out = {
        "generator": "tools/data_skill/build.py",
        "source": {
            "file": misc.name,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        },
        "provenance": {
            "dump": "MISC.VFS 內對應 .DAT 成員的實際 byte",
            "guide": "攻略站的名稱，經知識庫 assets/ 與 chapters/ 轉錄",
            "rule": "兩者不一致時以 dump 為準，差異全部列在 discrepancies",
        },
        "label_checks": compared,
        "enums": {
            "hit_effect": {"%02X" % k: v for k, v in sorted(hit_effect.items())},
            "spell_target": {"%02X" % k: v for k, v in sorted(SPELL_TARGET.items())},
        },
        "discrepancies": DISCREPANCIES,
        "tables": {
            "item": {
                "canon": "assets/items.md", "member": "ITEM.DAT", "index": "物品編號",
                "name_source": "guide", "value_source": "dump", "records": items,
            },
            "spell": {
                "canon": "assets/spells.md", "member": "MAGICDAT.DAT", "index": "法術編號",
                "name_source": "guide", "value_source": "dump", "records": spells,
            },
            "character": {
                "canon": "assets/characters.md",
                "member": "FRIAPRDA.DAT + FRILEVUP.DAT + GETMGTAB.DAT",
                "index": "肖像編號",
                "name_source": "guide", "value_source": "dump", "records": characters,
            },
            "class": {
                "canon": "assets/classes.md", "member": "PROMAP.DAT", "index": "職業代碼",
                "name_source": "guide", "value_source": "dump", "records": classes,
            },
            "chapter": {
                "canon": "chapters/_index.md", "member": None, "index": "章號",
                "name_source": "guide", "value_source": "guide", "records": chapters,
            },
        },
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=2, ensure_ascii=False)
        fh.write("\n")

    print("%s  %d bytes  sha256 %s" % (misc.name, len(data), out["source"]["sha256"][:16]))
    for name, table in out["tables"].items():
        print("%-10s %4d records" % (name, len(table["records"])))
    print("%d label checks passed, %d known guide discrepancies" % (compared, len(DISCREPANCIES)))


if __name__ == "__main__":
    main()
