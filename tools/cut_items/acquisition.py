"""Which ITEM.DAT items the player can obtain, and where the others appear.

    python tools/cut_items/acquisition.py summary [--dump DIR]
    python tools/cut_items/acquisition.py table   [--dump DIR]
    python tools/cut_items/acquisition.py check   [--dump DIR]

summary  counts (items with content, obtainable, not, carried by no deployment
         record) and the equipment types a player class can wear but no
         obtainable item has.
table    the Markdown table of the items no path gives the player, with every
         place in the data they appear in anyway.
check    regenerate that table and compare it cell by cell with the one in
         cut_content/items.md; non-zero exit on any difference.

<DIR> defaults to workspace/vfs_dump (the tools/vfs_dump output), like
tools/data_tables and tools/map_decode.

This is a survey over the whole item table, not a judgement per item: every
path the program has for putting an item into a unit's bag is one rule below,
and each rule is transcribed from src/ (the owners are named at each rule).
The unit tests pin the counts cut_content/items.md states.
"""

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "data_tables"))
sys.path.insert(0, str(ROOT / "tools" / "map_decode"))
import data_tables as dt  # noqa: E402  (names, shops, promotions, village rule)
import map_decode as md  # noqa: E402  (owner of the MAPnn.DAT / terrain parse)

DEFAULT_DUMP = ROOT / "workspace" / "vfs_dump"
PAGE = ROOT / "cut_content" / "items.md"

ITEM_STRIDE = 23
PROEQU_STRIDE = 6
PROEQU_NONE = 0xFF
ITEM_NONE = 0xFF

# FRIAPRDA.DAT +0x0C..+0x11: the two equipped and four carried items that
# fdps_roster_add_character (src/roster.c) copies into a joining member.
JOIN_ITEMS = slice(0x0C, 0x12)
JOINING_CHARACTERS = range(0x0C)

LAST_BATTLE_MAP = 29          # maps 0..29 are chapters 1..30; 31 and up are scenes
SIDE_ENEMY, SIDE_NPC, SIDE_PLAYER = 0, 1, 2
DEATH_OP_ITEM = 0             # src/death.c: opcode 0 hands the operand to fdps_unit_add_item
SEARCH_KIND_ITEM = 0          # src/btlact.c: record kind 0 is an item id

SHOP_ITEM_ROW, SHOP_WEAPON_ROW, SHOP_SECRET_ROW = 0, 1, 2
# fdps_check_secret_code_key (src/village.c) takes row "chapter index - 1" of a
# 24-row table; chapter index 25 reads past it (cut_content/code.md C1).
SECRET_CODE_ROWS = 24

# fdps_run_bonus_lottery (src/vilbar.c): the three grand prizes are never paid
# out (cut_content/items.md I06); the consolation 藥草 is.
LOTTERY_SEALED = (0xBF, 0xC0, 0xC1, 0xB9)
LOTTERY_PAID = 0xB4

# Every fdps_unit_add_item call with a constant item id, outside the shop,
# search, drop, thief and hand-over paths (which take the id from data), with
# the file it is in.  BE is the 高能量裝置 combination (use effect 23).
GRANTS = (
    (0xC8, "src/chevt2.c"), (0xDA, "src/chevt2.c"), (0xDD, "src/chevt2.c"),
    (0xA0, "src/chevt3.c"), (0xA9, "src/chevt3.c"), (0xA3, "src/chevt3.c"),
    (0xA1, "src/chevt4.c"), (0xBA, "src/chevt4.c"), (0xDC, "src/chevt4.c"),
    (0xB1, "src/chevt4.c"), (0xA2, "src/chevt5.c"), (0x4A, "src/chevt5.c"),
    (0xA5, "src/chpost1.c"), (0xA6, "src/chpost2.c"),
    (0xA7, "src/chpost2.c"), (0xDB, "src/chend2.c"), (0x62, "src/chend2b.c"),
    (0xBE, "src/item.c"),
)

TABLE_HEADER = ("編號", "名稱", "類型", "資料裡出現在哪裡（都到不了）")


# The places an item can appear without a path, in the order the table prints
# them: kind -> how its collected details read.
SEEN_LABELS = {
    "enemy": lambda d: f"第 {dt.compact(d)} 章的敵兵",
    "npc": lambda d: f"第 {dt.compact(d)} 章的友軍 NPC",
    "orphan": lambda d: "沒有格子引用的寶物記錄 " + "、".join(
        f"`MAP{n:02d}` 碼 {code}" for n, code in sorted(d)),
    "scene_unit": lambda d: f"過場地圖 {dt.compact(d)} 的部署記錄",
    "scene_record": lambda d: f"過場地圖 {dt.compact(d)} 的寶物記錄",
    "scene_drop": lambda d: f"過場地圖 {dt.compact(d)} 的死亡掉落",
    "placeholder_shop": lambda d: "占位商店檔 " + "、".join(f"`SHOP{n:02d}`" for n in sorted(d)),
    "sealed_shop": lambda d: "`SHOP25.DAT` 神秘商店",
    "lottery": lambda d: "酒館抽獎的大獎",
}


# ---- the survey --------------------------------------------------------------

def chapter_of(map_number):
    return map_number + 1


def survey(content, appearance, shops, maps, grants=GRANTS):
    """For each item id in `content`: the paths that give it to the player
    ("reach"), the places it appears without such a path ("seen", as
    (kind, detail) pairs) and whether any deployment record carries it."""
    out = {i: {"reach": [], "seen": [], "deployed": False} for i in content}

    def reach(item, how):
        if item in out:
            out[item]["reach"].append(how)

    def seen(item, kind, detail=None):
        if item in out:
            out[item]["seen"].append((kind, detail))

    for character, record in enumerate(appearance):
        if character in JOINING_CHARACTERS:
            for item in record["raw"][JOIN_ITEMS]:
                if item != ITEM_NONE:
                    reach(item, "入隊裝備")

    for chapter, rows in shops.items():
        for row_number, row in enumerate(rows):
            for item in row:
                if item == ITEM_NONE:
                    continue
                if dt.no_village(chapter) or chapter == 0:
                    seen(item, "placeholder_shop", chapter)
                elif row_number == SHOP_SECRET_ROW and chapter > SECRET_CODE_ROWS:
                    seen(item, "sealed_shop", chapter)
                else:
                    reach(item, "商店")

    for m in maps:
        n, battle = m["number"], m["number"] <= LAST_BATTLE_MAP
        for code, record in enumerate(m["search"]):
            item = record["payload"]
            live = code in m["live_codes"]
            # (0, 0) is how an unused record is filled; behind a live cell it
            # would still hand over item 00, so only unreferenced ones are skipped.
            if record["kind"] != SEARCH_KIND_ITEM or (item == 0 and not live):
                continue
            if battle and live:
                reach(item, "寶箱與埋藏")
            elif battle:
                seen(item, "orphan", (n, code))
            else:
                seen(item, "scene_record", n)
        for s in m["spawns"]:
            carried = [i for i in tuple(s["equip"]) + tuple(s["carried"]) if i != ITEM_NONE]
            for item in carried:
                if item in out:
                    out[item]["deployed"] = True
                if not battle:
                    seen(item, "scene_unit", n)
                elif s["side"] == SIDE_PLAYER:
                    reach(item, "客串單位轉交")
                elif s["side"] == SIDE_NPC:
                    seen(item, "npc", chapter_of(n))
                else:
                    seen(item, "enemy", chapter_of(n))
            if s["death_op"] == DEATH_OP_ITEM:
                item = s["death_arg"] & 0xFF
                if battle:
                    reach(item, "敵人掉落")
                else:
                    seen(item, "scene_drop", n)

    for item, where in grants:
        reach(item, f"事件給予（{where}）")
    for item in LOTTERY_SEALED:
        seen(item, "lottery")
    reach(LOTTERY_PAID, "酒館抽獎")
    return out


# ---- loading -----------------------------------------------------------------

def load_maps(dump):
    maps = []
    for n in md.map_numbers(dump):
        m = md.load_map(dump, n)
        if not m["dat"]:
            continue
        live = set()
        if m["layers"][0]["mpl"] and m["layers"][0]["attr"]:
            live = {c["code"] for c in md.searchable_cells(m)}
        maps.append({"number": n, "spawns": m["dat"]["spawns"],
                     "search": m["dat"]["search"], "live_codes": live})
    return maps


def player_classes(game):
    """Every class a party member can be: the twelve joining characters' own
    and every promotion route the church offers them (src/church.c)."""
    classes = {game.appearance[c]["class"] for c in JOINING_CHARACTERS}
    for character, routes in enumerate(game.promotions):
        for _n, (_form, clazz, _move) in dt.offered_routes(character, routes):
            classes.add(clazz)
    return classes


def run(dump=DEFAULT_DUMP):
    dump = Path(dump)
    game = dt.load(dump)
    raw = (dump / "MISC" / "ITEM.DAT").read_bytes()
    records = [raw[i:i + ITEM_STRIDE] for i in range(0, len(raw), ITEM_STRIDE)]
    content = {i: r for i, r in enumerate(records) if any(r)}
    shops = {}
    for n in range(dt.CHAPTER_COUNT):
        path = dump / "FIELD" / f"SHOP{n:02d}.DAT"
        if path.is_file():
            data = path.read_bytes()
            shops[n] = [list(data[k * dt.SHOP_SLOTS:(k + 1) * dt.SHOP_SLOTS])
                        for k in range(dt.SHOP_ROWS)]
    proequ = (dump / "MISC" / "PROEQU.DAT").read_bytes()
    wearable = set()
    for clazz in player_classes(game):
        row = proequ[clazz * PROEQU_STRIDE:(clazz + 1) * PROEQU_STRIDE]
        wearable |= {t for t in row if t != PROEQU_NONE}
    return {"game": game, "records": records, "content": content,
            "survey": survey(content, game.appearance, shops, load_maps(dump)),
            "wearable": wearable}


# ---- reports -----------------------------------------------------------------

def unobtainable(result):
    return sorted(i for i, s in result["survey"].items() if not s["reach"])


def counts(result):
    s = result["survey"]
    missing = unobtainable(result)
    return {"records": len(result["records"]), "content": len(s),
            "obtainable": len(s) - len(missing), "unobtainable": len(missing),
            "not_deployed": sum(1 for i in missing if not s[i]["deployed"])}


def item_type(result, item):
    return result["content"][item][0]


def unmet_types(result):
    """Types a party member's class may wear, that some item has, and that no
    obtainable item has."""
    s = result["survey"]
    have = {item_type(result, i) for i in s}
    obtainable = {item_type(result, i) for i in s if s[i]["reach"]}
    return sorted(t for t in result["wearable"] if t in have and t not in obtainable)


def render_seen(seen):
    groups = {}
    for kind, detail in seen:
        details = groups.setdefault(kind, [])
        if detail is not None and detail not in details:
            details.append(detail)
    parts = [label(groups[kind]) for kind, label in SEEN_LABELS.items() if kind in groups]
    return "；".join(parts) if parts else "—"


def table_rows(result):
    s, game = result["survey"], result["game"]
    return [[dt.code(i), game.item_name(i) or dt.BLANK, dt.code(item_type(result, i)),
             render_seen(s[i]["seen"])]
            for i in unobtainable(result)]


def markdown(rows):
    lines = ["| " + " | ".join(TABLE_HEADER) + " |", "| ---: | --- | ---: | --- |"]
    lines += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(lines) + "\n"


def check(result, page=PAGE):
    """Differences between the generated table and the one on the page."""
    want = table_rows(result)
    have = dt.read_table(Path(page), TABLE_HEADER)
    if have is None:
        return [f"{page.name}: no table with header {' | '.join(TABLE_HEADER)}"]
    problems = []
    for k in range(max(len(want), len(have))):
        w = want[k] if k < len(want) else None
        h = have[k] if k < len(have) else None
        if w != h:
            problems.append(f"row {k + 1}: page has {h}, data gives {w}")
    return problems


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=("summary", "table", "check"))
    ap.add_argument("--dump", type=Path, default=DEFAULT_DUMP)
    args = ap.parse_args(argv)
    result = run(args.dump)
    if args.command == "summary":
        c = counts(result)
        print(" ".join(f"{k}={v}" for k, v in c.items()))
        s = result["survey"]
        print("not deployed:", " ".join(f"{i:02X}" for i in unobtainable(result)
                                        if not s[i]["deployed"]))
        print("types wearable by a party class, none obtainable:",
              " ".join(f"{t:02X}" for t in unmet_types(result)))
        return 0
    if args.command == "table":
        print(markdown(table_rows(result)), end="")
        return 0
    problems = check(result)
    for p in problems:
        print("DIFF " + p)
    print(f"FAIL: {len(problems)} differences" if problems
          else f"OK: {len(table_rows(result))} rows match")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
