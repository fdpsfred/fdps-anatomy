"""Decode an FDPS battle map -- terrain, event-code layer, deployment script --
and render it as a whole-map PNG.

Every layout here was read off the loaders and readers in FDPS.LE (their C is
in src/), not guessed:

    fdps_field_load_chapter_resources (src/rsrc.c)  which files make up map nn
    fdps_map_load_tile_info           (src/maptile.c) how one cell is read
    fdps_draw_scene_layer(s)          (src/mapdraw.c) how the layers are drawn
    fdps_battle_search_cell_at_cursor (src/btlact.c)  the +0x53 search records
    fdps_battle_run_turn_events       (src/btlturn.c) the +0x03 turn events
    fdps_deploy_unit / _wave          (src/deploy.c)  the +0x83 deployments

    M%02d.DTL    event-code layer: "DTL\\0", width i16 at +7, one byte per cell
                 from +0x10, indexed with ITS OWN width (64 on most maps, never
                 the terrain width)
    M%02d%d.MPL  terrain layer n: "MPL\\0", width/height i16 at +7/+9, i16 tile
                 ids from +0x0b, row-major
    ATTR%02d%d.DAT  per tile id of layer n: "ATR\\0", 4-byte rows from +0x11
                 (flags, blend level, terrain class, combat backdrop id)
    DSC%02d.DAT  i32 layer count, then 0x20 bytes per layer: six i32 (scroll x,
                 scroll y, parallax x, parallax y, step x, step y), draw depth at
                 +0x18, attribute mode at +0x1c
    M%02d%d.CEL  the tile sheet of layer n (decoded by tools/cel_decode)
    MAP%02d.DAT / .COD  deployment script and placement table (resource_info/map.md)

The whole-map PNG composites the layers that scroll in lockstep with the map
(parallax 8/8, no drift) in the game's own draw order -- ascending draw depth,
stable, depth 10 skipped. A parallax layer has no fixed position relative to
the map, so it is written to its own PNG instead.

Usage:
    python map_decode.py check   [<vfs_dump dir>]
    python map_decode.py show    <map no> [<vfs_dump dir>]
    python map_decode.py render  <out dir> [--annotate] [--maps N,N..] [<vfs_dump dir>]
    python map_decode.py report  [<vfs_dump dir>]

<vfs_dump dir> defaults to workspace/vfs_dump (tools/vfs_dump output).
"""

import argparse
import collections
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "cel_decode"))
import cel_decode  # noqa: E402  (the owner of .CEL decoding, tools/_index.md)

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DUMP = ROOT / "workspace" / "vfs_dump"
DATA_SKILL = ROOT / ".claude" / "skills" / "fdps-data" / "fdps_data.json"

TILE = 24                       # SCENE_TILE_SIZE, src/mapdraw.c
MPL_HEADER = 0x0B
DTL_HEADER = 0x10
ATTR_HEADER = 0x11
DSC_RECORD = 0x20
DAT_TURN_EVENTS = 0x03
DAT_CELL_EVENTS = 0x33          # indexed (code - 1) * 2, src/maptile.c
DAT_SEARCH = 0x53               # indexed code * 3, src/btlact.c
DAT_SPAWNS = 0x83
SPAWN_BYTES = 0x1A
COD_HEADER = 9
COD_RECORD = 6
TABLE_ENTRIES = 16
MIDDLE_DEPTH = 10               # SCENE_LAYER_MID_DEPTH, src/mapdraw.c
LOCKSTEP_PARALLAX = (8, 8)      # 8/8 = the view origin unscaled
ANIM_FRAME_STRIDE = 0x60        # TILE_ANIM_FOUR_FRAME_STRIDE, src/mapdraw.c
CELL_CLASS = {0x00: None, 0x20: "chest", 0x40: "buried", 0x60: "repaint"}
SEARCHABLE = ("chest", "buried")
MAX_MAP = 64


class MapError(Exception):
    """A field did not hold what the format requires."""


def fail(source, field, detail):
    raise MapError(f"{source}: field '{field}': {detail}")


def expect_magic(data, magic, source):
    if data[:4] != magic:
        fail(source, "magic", f"expected {magic!r}, got {data[:4]!r}")


# ---- parsers -----------------------------------------------------------------

def parse_mpl(data, source):
    """M%02d%d.MPL: width, height and the row-major signed tile ids."""
    expect_magic(data, b"MPL\0", source)
    width, height = struct.unpack_from("<hh", data, 7)
    if width < 1 or height < 1:
        fail(source, "width/height", f"{width}x{height}")
    need = MPL_HEADER + 2 * width * height
    if len(data) != need:
        fail(source, "size", f"{width}x{height} needs {need} bytes, file is {len(data)}")
    tiles = list(struct.unpack_from(f"<{width * height}h", data, MPL_HEADER))
    return {"width": width, "height": height, "tiles": tiles}


def parse_dtl(data, source):
    """M%02d.DTL: the event-code plane the game reads (+7 width, cells at +0x10).

    Only the first plane is read; `extra` is whatever follows it (M31.DTL
    declares two planes and carries a second one there).
    """
    expect_magic(data, b"DTL\0", source)
    width, height = struct.unpack_from("<hh", data, 7)
    if width < 1 or height < 1:
        fail(source, "width/height", f"{width}x{height}")
    end = DTL_HEADER + width * height
    if len(data) < end:
        fail(source, "size", f"{width}x{height} needs {end} bytes, file is {len(data)}")
    return {"width": width, "height": height, "planes": data[0x0B],
            "cells": data[DTL_HEADER:end], "extra": len(data) - end}


def parse_attr(data, source):
    """ATTR%02d%d.DAT: one 4-byte row per tile id from +0x11."""
    expect_magic(data, b"ATR\0", source)
    count = struct.unpack_from("<H", data, 7)[0]
    if len(data) != ATTR_HEADER + 4 * count:
        fail(source, "size", f"{count} rows need {ATTR_HEADER + 4 * count} bytes, "
                             f"file is {len(data)}")
    rows = []
    for i in range(count):
        flags, blend, terrain, backdrop = data[ATTR_HEADER + 4 * i:ATTR_HEADER + 4 * i + 4]
        rows.append({"flags": flags, "blend": blend, "terrain": terrain,
                     "backdrop": backdrop})
    return {"count": count, "rows": rows}


def parse_dsc(data, source):
    """DSC%02d.DAT: one descriptor per scene layer."""
    count = struct.unpack_from("<i", data, 0)[0]
    if not 1 <= count <= 6 or len(data) != 4 + DSC_RECORD * count:
        fail(source, "layer count", f"{count} layers in {len(data)} bytes")
    layers = []
    for i in range(count):
        base = 4 + DSC_RECORD * i
        sx, sy, px, py, dx, dy = struct.unpack_from("<6i", data, base)
        layers.append({"scroll": (sx, sy), "parallax": (px, py), "step": (dx, dy),
                       "draw_depth": data[base + 0x18], "attr_mode": data[base + 0x1C]})
    return layers


def parse_map_dat(data, source):
    """MAP%02d.DAT: the resident chapter script block (resource_info/map.md)."""
    if len(data) < DAT_SPAWNS:
        fail(source, "size", f"{len(data)} bytes, shorter than the 0x83 header")
    count = data[2]
    if len(data) < DAT_SPAWNS + SPAWN_BYTES * count:
        fail(source, "spawn count", f"{count} records do not fit in {len(data)} bytes")
    turn = [{"turn": data[3 + 3 * i], "handler": data[4 + 3 * i], "side": data[5 + 3 * i]}
            for i in range(TABLE_ENTRIES)]
    cell = [{"handler": data[DAT_CELL_EVENTS + 2 * i], "trigger": data[DAT_CELL_EVENTS + 1 + 2 * i]}
            for i in range(TABLE_ENTRIES)]
    search = [{"kind": data[DAT_SEARCH + 3 * i],
               "payload": struct.unpack_from("<h", data, DAT_SEARCH + 1 + 3 * i)[0]}
              for i in range(TABLE_ENTRIES)]
    spawns = []
    for i in range(count):
        r = data[DAT_SPAWNS + SPAWN_BYTES * i:DAT_SPAWNS + SPAWN_BYTES * (i + 1)]
        spawns.append({
            "index": i, "side": r[0], "char_id": r[1], "unread_02": (r[2], r[3]),
            "level": r[4], "equip": (r[5], r[6]), "carried": tuple(r[7:13]),
            "spell_mask": bytes(r[0x0D:0x11]) + bytes([r[0x19]]),
            "ai": r[0x11], "dest": (r[0x12], r[0x13]), "cell_code": r[0x14],
            "wave": r[0x15], "death_op": r[0x16],
            "death_arg": struct.unpack_from("<h", r, 0x17)[0]})
    trailing = len(data) - DAT_SPAWNS - SPAWN_BYTES * count
    return {"raw": bytes(data), "player_slots": data[1], "spawn_count": count,
            "turn_events": turn, "cell_events": cell, "search": search,
            "spawns": spawns, "trailing_bytes": trailing}


def parse_map_cod(data, source):
    """MAP%02d.COD: count at +7, then 6-byte records (u16 id, i16 x, i16 y) from +9.

    The game reads only the x/y pair of each record (src/deploy.c).
    """
    expect_magic(data, b"COD\0", source)
    count = struct.unpack_from("<H", data, 7)[0]
    if len(data) != COD_HEADER + COD_RECORD * count:
        fail(source, "size", f"{count} records need {COD_HEADER + COD_RECORD * count} "
                             f"bytes, file is {len(data)}")
    return [struct.unpack_from("<Hhh", data, COD_HEADER + COD_RECORD * i) for i in range(count)]


# ---- one map -------------------------------------------------------------------

def assemble_map(number, layers, dtl, dat, cod):
    return {"number": number, "layers": layers, "dtl": dtl, "dat": dat, "cod": cod}


def load_map(root, number):
    """Load every file of map `number` out of a tools/vfs_dump output tree.

    Files the shipped data lacks come back as None -- the game's own loader
    would end the process on them, see resource_info/terrain.md.
    """
    root = Path(root)
    field, field1, field2 = root / "FIELD", root / "FIELD1", root / "FIELD2"

    def read(path, parser):
        return parser(path.read_bytes(), path.name) if path.is_file() else None

    dsc = read(field2 / f"DSC{number:02d}.DAT", parse_dsc)
    layers = []
    for k in range(len(dsc) if dsc else 1):
        tag = f"{number:02d}{k}"
        layers.append({
            "mpl": read(field1 / f"M{tag}.MPL", parse_mpl),
            "attr": read(field2 / f"ATTR{tag}.DAT", parse_attr),
            "cel_path": field1 / f"M{tag}.CEL",
            "dsc": dsc[k] if dsc else None})
    return assemble_map(number, layers,
                        read(field1 / f"M{number:02d}.DTL", parse_dtl),
                        read(field / f"MAP{number:02d}.DAT", parse_map_dat),
                        read(field / f"MAP{number:02d}.COD", parse_map_cod))


def map_numbers(root):
    return [n for n in range(MAX_MAP + 1)
            if (Path(root) / "FIELD1" / f"M{n:02d}.DTL").is_file()]


def grid_size(m):
    base = m["layers"][0]["mpl"]
    return base["width"], base["height"]


def tile_info(m, x, y):
    """What fdps_map_load_tile_info latches for cell (x, y) of layer 0."""
    layer = m["layers"][0]
    width = layer["mpl"]["width"]
    tile = layer["mpl"]["tiles"][y * width + x]
    rows = layer["attr"]["rows"]
    row = rows[tile] if 0 <= tile < len(rows) else None
    dtl = m["dtl"]
    code = 0
    if dtl:
        at = y * dtl["width"] + x
        code = dtl["cells"][at] if at < len(dtl["cells"]) else 0
    info = {"tile": tile, "event_code": code}
    info.update(row or {"flags": None, "blend": None, "terrain": None, "backdrop": None})
    return info


def search_record(m, code):
    # Read from the raw block rather than dat["search"]: the game does no bound
    # test, so a code above 15 reads on into the deployment records.
    raw = m["dat"]["raw"]
    return {"index": code, "kind": raw[DAT_SEARCH + 3 * code],
            "payload": struct.unpack_from("<h", raw, DAT_SEARCH + 1 + 3 * code)[0]}


def classify_cell(m, x, y):
    """What the game does with cell (x, y): search record, repaint flag or tile event."""
    info = tile_info(m, x, y)
    kind = CELL_CLASS[(info["flags"] or 0) & 0x60]
    code = info["event_code"]
    out = {"x": x, "y": y, "kind": kind, "code": code, "info": info}
    if kind in SEARCHABLE:
        if m["dat"]:
            out["record"] = search_record(m, code)
    elif kind == "repaint":
        out["flag"] = code
    elif code and m["dat"]:
        raw = m["dat"]["raw"]
        handler = raw[DAT_CELL_EVENTS - 2 + 2 * code]
        out["tile_event"] = {"code": code, "handler": None if handler == 0xFF else handler,
                             "trigger": raw[DAT_CELL_EVENTS - 1 + 2 * code]}
    return out


def all_cells(m):
    width, height = grid_size(m)
    return [classify_cell(m, x, y) for y in range(height) for x in range(width)]


def searchable_cells(m):
    return [c for c in all_cells(m) if c["kind"] in SEARCHABLE]


# ---- names (optional, for readable listings) -----------------------------------

def item_names():
    try:
        data = json.loads(DATA_SKILL.read_text(encoding="utf-8"))
        return {r["code"]: r["name"] for r in data["tables"]["item"]["records"]}
    except (OSError, KeyError, ValueError):
        return {}


def describe_record(record, names):
    kind, payload = record["kind"], record["payload"]
    if kind == 0:
        return f"item {payload:02X} {names.get(payload, '?')}"
    if kind == 1:
        return f"gold {payload}"
    return f"handler slot {payload} (kind {kind})"


# ---- rendering -----------------------------------------------------------------

def load_palette(root):
    return cel_decode.load_palette(Path(root) / "MISC" / "FDE.PAL")


def sprite_images(cel_path, palette):
    from PIL import Image
    data = cel_path.read_bytes()
    header, _, decoded = cel_decode.decode_sheet(data, cel_path.name)
    w, h = header["width"], header["height"]
    images = []
    for pixels, mask in decoded:
        rgba = bytearray()
        for p, k in zip(pixels, mask):
            rgba += bytes(palette[p]) if k else b"\0\0\0\0"
        images.append(Image.frombytes("RGBA", (w, h), bytes(rgba)))
    return images


def lockstep(layer):
    d = layer["dsc"]
    return d is None or (d["parallax"] == LOCKSTEP_PARALLAX and d["step"] == (0, 0))


def draw_order(m):
    """Layer indices in the game's order: stable ascending depth, 10 never drawn."""
    order = sorted(range(len(m["layers"])),
                   key=lambda k: (m["layers"][k]["dsc"] or {"draw_depth": 9})["draw_depth"])
    return [k for k in order
            if (m["layers"][k]["dsc"] or {"draw_depth": 9})["draw_depth"] != MIDDLE_DEPTH]


def paint_layer(canvas, layer, sprites, width, height):
    mpl = layer["mpl"]
    for y in range(height):
        for x in range(width):
            tile = mpl["tiles"][(y % mpl["height"]) * mpl["width"] + (x % mpl["width"])]
            if 0 <= tile < len(sprites):
                canvas.alpha_composite(sprites[tile], (x * TILE, y * TILE))


def render_map(m, palette, out, annotate=False):
    """Whole-map PNG of the lockstep layers; one more PNG per parallax layer."""
    from PIL import Image
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    width, height = grid_size(m)
    canvas = Image.new("RGBA", (width * TILE, height * TILE), (0, 0, 0, 0))
    extra = []
    for k in draw_order(m):
        layer = m["layers"][k]
        if layer["mpl"] is None or not layer["cel_path"].is_file():
            continue
        sprites = sprite_images(layer["cel_path"], palette)
        if lockstep(layer):
            paint_layer(canvas, layer, sprites, width, height)
        else:
            own = Image.new("RGBA", (layer["mpl"]["width"] * TILE,
                                     layer["mpl"]["height"] * TILE), (0, 0, 0, 0))
            paint_layer(own, layer, sprites, layer["mpl"]["width"], layer["mpl"]["height"])
            path = out.with_name(f"{out.stem}_L{k}{out.suffix}")
            own.save(path)
            extra.append(path)
    if annotate:
        annotate_canvas(canvas, m)
    canvas.save(out)
    return [out] + extra


ANNOTATION_COLOURS = {"chest": (255, 220, 0, 255), "buried": (255, 120, 0, 255),
                      "repaint": (0, 220, 255, 255), "event": (255, 0, 255, 255),
                      "party": (0, 255, 0, 255)}


def annotate_canvas(canvas, m):
    """Outline and label the cells a reader of the map cares about."""
    from PIL import ImageDraw, ImageFont
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()
    marks = []
    for c in all_cells(m):
        if c["kind"] in SEARCHABLE:
            marks.append((c["x"], c["y"], c["kind"], f"{c['kind'][0].upper()}{c['code']}"))
        elif c["kind"] == "repaint" and c["code"]:
            marks.append((c["x"], c["y"], "repaint", f"R{c['code']}"))
        elif c.get("tile_event"):
            marks.append((c["x"], c["y"], "event", f"E{c['code']}"))
    if m["dat"] and m["cod"]:
        n = m["dat"]["spawn_count"]
        for slot, (_, x, y) in enumerate(m["cod"][n:n + m["dat"]["player_slots"]]):
            marks.append((x, y, "party", f"P{slot}"))
    for x, y, kind, label in marks:
        colour = ANNOTATION_COLOURS[kind]
        box = (x * TILE, y * TILE, x * TILE + TILE - 1, y * TILE + TILE - 1)
        draw.rectangle(box, outline=colour, width=2)
        draw.text((x * TILE + 3, y * TILE + 3), label, fill=colour, font=font,
                  stroke_width=1, stroke_fill=(0, 0, 0, 255))


# ---- statistics ----------------------------------------------------------------

def wave_distribution(m):
    waves = collections.Counter(s["wave"] for s in m["dat"]["spawns"])
    return ", ".join(f"{'FF' if w == 0xFF else w}×{n}" for w, n in sorted(waves.items()))


def map_stats(m):
    width, height = grid_size(m)
    cells = all_cells(m)
    kinds = collections.Counter(c["kind"] for c in cells)
    events = [c for c in cells if c.get("tile_event")]
    live_events = [c for c in events if c["tile_event"]["handler"] is not None]
    dat = m["dat"]
    referenced = sorted({c["code"] for c in cells if c["kind"] in SEARCHABLE})
    stats = {
        "number": m["number"], "size": f"{width}×{height}",
        "dtl": f"{m['dtl']['width']}×{m['dtl']['height']}",
        "layers": len(m["layers"]),
        "chest": kinds["chest"], "buried": kinds["buried"], "repaint": kinds["repaint"],
        "event_cells": len(events), "live_event_cells": len(live_events),
        "records": len(referenced)}
    if dat:
        unreferenced = [i for i, r in enumerate(dat["search"])
                        if i not in referenced and (r["kind"], r["payload"]) != (0, 0)]
        stats.update({
            "player_slots": dat["player_slots"], "spawns": dat["spawn_count"],
            "waves": wave_distribution(m) or "—",
            "turn_events": sum(1 for t in dat["turn_events"]
                               if (t["turn"], t["handler"]) != (0xFF, 0xFF)),
            "unreferenced": unreferenced})
    return stats


def report_markdown(root):
    lines = ["| 地圖 | 章 | 地形 | 事件碼層 | 圖層 | 我方 slot | 部署 | 波次分佈 "
             "| 寶箱 | 埋藏 | 重繪格 | 事件格（有處理函式） | 寶物記錄 | 未引用的非空記錄 | 回合事件 |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: "
             "| ---: | ---: | --- | ---: |"]
    for n in map_numbers(root):
        s = map_stats(load_map(root, n))
        chapter = str(n + 1) if n < 30 else "—"
        if "spawns" in s:
            dat_cols = (f"{s['player_slots']} | {s['spawns']} | {s['waves']}",
                        ", ".join(str(i) for i in s["unreferenced"]) or "—",
                        str(s["turn_events"]))
        else:
            dat_cols = ("— | — | 無 `MAP` 檔", "—", "—")
        lines.append(
            f"| `M{n:02d}` | {chapter} | {s['size']} | {s['dtl']} | {s['layers']} | "
            f"{dat_cols[0]} | {s['chest']} | {s['buried']} | {s['repaint']} | "
            f"{s['event_cells']}（{s['live_event_cells']}） | {s['records']} | "
            f"{dat_cols[1]} | {dat_cols[2]} |")
    return "\n".join(lines)


# ---- consistency check -----------------------------------------------------------

def check(root):
    """Parse every map and test the invariants resource_info/terrain.md relies on.

    Returns a list of (invariant, [exceptions]) pairs; parse failures raise.
    """
    results = collections.OrderedDict()

    def note(name, ok, detail=None):
        results.setdefault(name, [])
        if not ok:
            results[name].append(detail)

    for n in map_numbers(root):
        m = load_map(root, n)
        tag = f"M{n:02d}"
        width, height = grid_size(m)
        dtl = m["dtl"]
        note("event-layer width differs from terrain width", dtl["width"] != width, tag)
        note("event layer covers the terrain grid",
             dtl["width"] >= width and dtl["height"] >= height, tag)
        stray = [(i % dtl["width"], i // dtl["width"]) for i, c in enumerate(dtl["cells"])
                 if c and (i % dtl["width"] >= width or i // dtl["width"] >= height)]
        note("no event code outside the terrain grid", not stray, f"{tag} {stray[:5]}")
        codes = [c["code"] for c in all_cells(m) if c["code"]]
        note("event codes stay in 0..15", all(c <= 15 for c in codes), f"{tag} {max(codes, default=0)}")
        note("DTL declares one plane and ends with it",
             dtl["planes"] == 1 and dtl["extra"] == 0,
             f"{tag} planes={dtl['planes']} extra={dtl['extra']}")
        note("DSC present", m["layers"][0]["dsc"] is not None, tag)
        note("MAP.DAT present", m["dat"] is not None, tag)
        for k, layer in enumerate(m["layers"]):
            lt = f"{tag}{k}"
            if layer["mpl"] is None or layer["attr"] is None:
                note("every DSC layer has MPL and ATTR", False, lt)
                continue
            cel_count = None
            if layer["cel_path"].is_file():
                cel_count = cel_decode.parse_header(layer["cel_path"].read_bytes(),
                                                    layer["cel_path"].name)[0]["count"]
            note("ATTR rows equal tile-sheet sprites", layer["attr"]["count"] == cel_count,
                 f"{lt} attr={layer['attr']['count']} cel={cel_count}")
            top = max(layer["mpl"]["tiles"])
            note("tile ids inside ATTR and sheet",
                 min(layer["mpl"]["tiles"]) >= 0 and top < layer["attr"]["count"]
                 and (cel_count is None or top < cel_count), f"{lt} max={top}")
            rows = layer["attr"]["rows"]
            anim = [t for t in set(layer["mpl"]["tiles"]) if rows[t]["flags"] & 7 == 2]
            note("four-frame tiles stay inside the sheet",
                 all(t + 3 * ANIM_FRAME_STRIDE < (cel_count or 0) for t in anim), lt)
            note("blend level is always 0", all(r["blend"] == 0 for r in rows), lt)
            note("no translucent or two-frame flag",
                 all(r["flags"] & 0x81 == 0 for r in rows), lt)
        dat, cod = m["dat"], m["cod"]
        if dat:
            note("MAP.DAT ends with its last deployment", dat["trailing_bytes"] == 0,
                 f"{tag} +{dat['trailing_bytes']} bytes")
            note("MAP.DAT fits the 0x8a3-byte save block", len(dat["raw"]) <= 0x8A3, tag)
        if dat and cod:
            note("COD records = deployments + player slots",
                 len(cod) == dat["spawn_count"] + dat["player_slots"],
                 f"{tag} cod={len(cod)} n={dat['spawn_count']} pl={dat['player_slots']}")
    return list(results.items())


# ---- listing ------------------------------------------------------------------

def show(root, number):
    m = load_map(root, number)
    names = item_names()
    width, height = grid_size(m)
    out = [f"M{number:02d}: terrain {width}x{height}, event layer "
           f"{m['dtl']['width']}x{m['dtl']['height']}, {len(m['layers'])} layer(s)"]
    for k, layer in enumerate(m["layers"]):
        out.append(f"  layer {k}: {layer['dsc']}")
    dat = m["dat"]
    if not dat:
        out.append("  no MAP.DAT")
        return "\n".join(out)
    out.append(f"  player slots {dat['player_slots']}, deployments {dat['spawn_count']}")
    out.append("  turn events (turn, handler slot, side):")
    for i, t in enumerate(dat["turn_events"]):
        if (t["turn"], t["handler"]) != (0xFF, 0xFF):
            out.append(f"    [{i:2d}] turn {t['turn']} slot {t['handler']} side {t['side']}")
    out.append("  cells:")
    for c in all_cells(m):
        if c["kind"] in SEARCHABLE:
            out.append(f"    ({c['x']:2d},{c['y']:2d}) {c['kind']:6s} code {c['code']:2d} -> "
                       f"{describe_record(c['record'], names)}")
        elif c["kind"] == "repaint" and c["code"]:
            out.append(f"    ({c['x']:2d},{c['y']:2d}) repaint flag {c['code']}")
        elif c.get("tile_event"):
            e = c["tile_event"]
            out.append(f"    ({c['x']:2d},{c['y']:2d}) event  code {e['code']:2d} -> "
                       f"slot {e['handler']} trigger {e['trigger']}")
    out.append("  search records (index: kind payload):")
    for i, r in enumerate(dat["search"]):
        out.append(f"    [{i:2d}] {describe_record(r, names)}")
    out.append("  deployments (idx side char lv ai dest cell wave death):")
    for s in dat["spawns"]:
        out.append(f"    [{s['index']:2d}] side {s['side']} char {s['char_id']:02X} "
                   f"lv {s['level']} ai {s['ai']:02X} dest {s['dest']} cell {s['cell_code']} "
                   f"wave {s['wave']} death {s['death_op']:02X}/{s['death_arg']}")
    return "\n".join(out)


def main(argv):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("check")
    p.add_argument("dump", nargs="?", default=DEFAULT_DUMP)
    p = sub.add_parser("show")
    p.add_argument("map", type=int)
    p.add_argument("dump", nargs="?", default=DEFAULT_DUMP)
    p = sub.add_parser("render")
    p.add_argument("out")
    p.add_argument("--annotate", action="store_true")
    p.add_argument("--maps")
    p.add_argument("dump", nargs="?", default=DEFAULT_DUMP)
    p = sub.add_parser("report")
    p.add_argument("dump", nargs="?", default=DEFAULT_DUMP)
    args = parser.parse_args(argv[1:])
    try:
        if args.command == "check":
            for name, exceptions in check(args.dump):
                print(f"{'OK ' if not exceptions else '-- '} {name}"
                      + (f": {exceptions}" if exceptions else ""))
        elif args.command == "show":
            print(show(args.dump, args.map))
        elif args.command == "render":
            palette = load_palette(args.dump)
            wanted = ([int(x) for x in args.maps.split(",")] if args.maps
                      else map_numbers(args.dump))
            suffix = "_annotated" if args.annotate else ""
            for n in wanted:
                paths = render_map(load_map(args.dump, n), palette,
                                   Path(args.out) / f"M{n:02d}{suffix}.png", args.annotate)
                print(" ".join(str(p) for p in paths))
        elif args.command == "report":
            print(report_markdown(args.dump))
    except (MapError, cel_decode.CelError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
