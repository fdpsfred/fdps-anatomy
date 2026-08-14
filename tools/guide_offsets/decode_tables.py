"""Locate the six data tables the strategy guide gives offsets for, and decode them.

The guide (docs/guide/fdps/modify2.txt) states raw byte offsets into MISC.VFS for the
item, spell, character-appearance, level-up, spell-learning and class tables.  This
script resolves those offsets against the container's own entry table, decodes every
table from its member's first byte, and writes both JSON and a readable dump.

Usage:
    python tools/guide_offsets/decode_tables.py <MISC.VFS> <output dir>
"""
import json
import struct
import sys
from pathlib import Path

VFS_HEADER = 35
VFS_ENTRY = 26

# Offsets printed by the guide.  Reported against the container, never trusted as
# the place to start decoding from.
GUIDE_OFFSETS = {
    "item": 0x7A6234,
    "spell": 0x96DF9E,
    "appearance": 0x7892F8,
    "levelup": 0x789898,
    "learn": 0x79C427,
    "class": 0x19D513A,
}

# Member each table actually lives in, and the record stride FDPS.LE multiplies by.
TABLES = {
    "item": ("ITEM.DAT", 23),
    "spell": ("MAGICDAT.DAT", 7),
    "appearance": ("FRIAPRDA.DAT", 24),
    "levelup": ("FRILEVUP.DAT", 11),
    "learn": ("GETMGTAB.DAT", 12),
    "class": ("PROMAP.DAT", 10),
}


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


def locate(entries):
    """Match every guide offset against the container, and every table against a member."""
    by_name = {name: (offset, size) for name, offset, size in entries}
    report = []
    members = {}
    for key, (member, stride) in TABLES.items():
        if member not in by_name:
            raise SystemExit(f"{member} is not a member of this container")
        offset, size = by_name[member]
        if size % stride:
            raise SystemExit(f"{member} is {size} bytes, not a multiple of {stride}")
        members[key] = (member, offset, size, size // stride)

        guide_offset = GUIDE_OFFSETS[key]
        landed = next((n for n, o, s in entries if o <= guide_offset < o + s), None)
        report.append({
            "table": key,
            "member": member,
            "member_offset": offset,
            "member_size": size,
            "records": size // stride,
            "guide_offset": guide_offset,
            "guide_lands_in": landed,
            "delta_from_member_start": guide_offset - offset,
        })
    return members, report


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
        "race": rec[0], "clazz": rec[1], "level": rec[2],
        "hp_base": i16(rec, 3), "mp_base": i16(rec, 5), "move": rec[7],
        "spells": struct.unpack_from("<I", rec, 8)[0],
        "items": list(rec[12:18]),
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


def decode(data, members):
    tables = {}
    for key, (member, offset, size, count) in members.items():
        stride = TABLES[key][1]
        rows = []
        for i in range(count):
            rec = data[offset + i * stride:offset + (i + 1) * stride]
            rows.append(DECODERS[key](rec))
        tables[key] = {"member": member, "stride": stride, "count": count, "records": rows}
    return tables


def render(report, tables):
    lines = ["# 攻略偏移歸屬與解表結果", "", "## 偏移歸屬", "",
             "| 表 | 成員 | 成員起點 | 大小 | 筆數 | 攻略偏移 | 落在 | 與成員起點的差 |",
             "| --- | --- | ---: | ---: | ---: | ---: | --- | ---: |"]
    for r in report:
        lines.append("| {table} | `{member}` | {member_offset} | {member_size} | {records} "
                     "| `{guide_offset:#x}` | `{guide_lands_in}` | {delta_from_member_start} |"
                     .format(**r))

    lines += ["", "## 物品表 ITEM.DAT", "",
              "| 編號 | 類型 | AP | HIT | DP | EV | 附加 | 率 | 距離 | 使用效果 | 量 | 距 | 對象 | 範圍 | 價格 | 選取 |",
              "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for i, r in enumerate(tables["item"]["records"]):
        lines.append("| {:02X} | {type:02X} | {ap} | {hit} | {dp} | {ev} | {hit_effect:02X} "
                     "| {hit_effect_rate} | {range_min}-{range_max} | {use_effect:02X} "
                     "| {use_amount} | {use_distance:02X} | {use_target:02X} | {use_radius} "
                     "| {price} | {select_mode} |".format(i, **r))

    lines += ["", "## 法術表 MAGICDAT.DAT", "",
              "| 編號 | 威力 | 命中 | 距離 | 範圍 | MP | 對象 |",
              "| ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for i, r in enumerate(tables["spell"]["records"]):
        lines.append("| {:02X} | {power} | {hit} | {distance:02X} | {radius} | {mp} "
                     "| {target} |".format(i, **r))

    lines += ["", "## 出場屬性 FRIAPRDA.DAT", "",
              "| 編號 | 種族 | 職業 | 等級 | HP | MP | MV | 法術遮罩 | 物品 | AP | DP | DX |",
              "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: |"]
    for i, r in enumerate(tables["appearance"]["records"]):
        items = " ".join("%02X" % x for x in r["items"])
        lines.append("| {:02X} | {race:02X} | {clazz:02X} | {level} | {hp_base} | {mp_base} "
                     "| {move} | {spells:08X} | {} | {ap_base} | {dp_base} | {dx_base} |"
                     .format(i, items, **r))

    lines += ["", "## 升級成長 FRILEVUP.DAT", "",
              "| 編號 | AP | DP | DX | HP | MP | 習得索引 |",
              "| ---: | --- | --- | --- | --- | --- | ---: |"]
    for i, r in enumerate(tables["levelup"]["records"]):
        lines.append("| {:02X} | {ap_min}-{ap_max} | {dp_min}-{dp_max} | {dx_min}-{dx_max} "
                     "| {hp_min}-{hp_max} | {mp_min}-{mp_max} | {learn_index:02X} |"
                     .format(i, **r))

    lines += ["", "## 法術習得 GETMGTAB.DAT", "", "| 編號 | 等級/法術 |", "| ---: | --- |"]
    for i, rows in enumerate(tables["learn"]["records"]):
        pairs = " ".join("%02X:%02X" % (p["level"], p["spell"]) for p in rows)
        lines.append("| %02X | %s |" % (i, pairs))

    lines += ["", "## 職業表 PROMAP.DAT", "",
              "| 索引 | 地形消耗 | 暴擊 | 100-魔抗 |", "| ---: | --- | ---: | ---: |"]
    for i, r in enumerate(tables["class"]["records"]):
        cost = " ".join("%02X" % x for x in r["move_cost"])
        lines.append("| %02X | %s | %d | %d |"
                     % (i, cost, r["critical"], r["magic_resist_complement"]))

    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    misc = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    data = misc.read_bytes()
    entries = read_entries(data)
    members, report = locate(entries)
    tables = decode(data, members)

    (out_dir / "tables.json").write_text(
        json.dumps({"source": misc.name, "locate": report, "tables": tables},
                   indent=2, ensure_ascii=False), encoding="utf-8")
    (out_dir / "report.md").write_text(render(report, tables), encoding="utf-8")
    for r in report:
        print("%-11s -> %-13s %5d records  guide %#09x lands in %s (+%d)"
              % (r["table"], r["member"], r["records"], r["guide_offset"],
                 r["guide_lands_in"], r["delta_from_member_start"]))


if __name__ == "__main__":
    main()
