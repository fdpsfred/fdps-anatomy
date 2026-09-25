"""Tests for map_decode at its public seams: the five file parsers,
tile_info() / classify_cell() (the per-cell read the game does), and the
render / report outputs.

Expected values come from sources independent of the code under test: the
offsets the game's own readers use (src/maptile.c, src/rsrc.c, src/btlact.c,
src/btlturn.c, src/deploy.c), hand-built byte blobs, and -- where the extracted
game files are present -- the strategy guide's treasure list for chapter 1
(docs/guide/fdps/walkthrough.txt: (9,15) 1000 gold, (18,17) and *(21,19) item
0xB4 藥草).

    python -m unittest tools/map_decode/test_map_decode.py
"""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import map_decode  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DUMP = ROOT / "workspace" / "vfs_dump"
HAVE_DUMP = (DUMP / "FIELD1" / "M00.DTL").is_file()


def mpl(width, height, tiles):
    return (b"MPL\0" + bytes([1]) + struct.pack("<Hhh", 0x0B, width, height)
            + struct.pack(f"<{len(tiles)}h", *tiles))


def dtl(width, height, cells):
    return (b"DTL\0" + bytes([1]) + struct.pack("<Hhh", 0x0E, width, height)
            + bytes([1, 1, 0, 0xFF, 0]) + bytes(cells))


def attr(rows):
    return (b"ATR\0" + bytes([1]) + struct.pack("<HHHH", 0x0D, len(rows), 4, 4)
            + bytes([1, 1, 1, 1]) + b"".join(bytes(r) for r in rows))


def map_dat(cell_events=(), search=(), turn=(), spawns=()):
    b = bytearray(0x83)
    b[1] = 1
    b[2] = len(spawns)
    for i in range(16):
        b[3 + 3 * i:6 + 3 * i] = bytes(turn[i]) if i < len(turn) else b"\xff\xff\x00"
        b[0x33 + 2 * i:0x35 + 2 * i] = (bytes(cell_events[i]) if i < len(cell_events)
                                         else b"\xff\x00")
        if i < len(search):
            b[0x53 + 3 * i] = search[i][0]
            struct.pack_into("<h", b, 0x54 + 3 * i, search[i][1])
    for rec in spawns:
        b += bytes(rec)
    return bytes(b)


def small_map():
    """A 2x2 terrain layer over a 4-wide event layer, as the shipped maps do."""
    return map_decode.assemble_map(
        number=0,
        layers=[{"mpl": map_decode.parse_mpl(mpl(2, 2, [0, 1, 2, 3]), "t"),
                 "attr": map_decode.parse_attr(attr([
                     (0x00, 0, 0, 0),     # tile 0: plain ground
                     (0x20, 0, 5, 3),     # tile 1: chest
                     (0x60, 0, 6, 0),     # tile 2: repaint-only
                     (0x40, 0, 0, 1),     # tile 3: buried
                 ]), "t"),
                 "dsc": {"draw_depth": 9, "attr_mode": 1, "parallax": (8, 8),
                         "scroll": (0, 0), "step": (0, 0)}}],
        dtl=map_decode.parse_dtl(dtl(4, 2, [5, 2, 9, 9,
                                            3, 0, 9, 9]), "t"),
        dat=map_decode.parse_map_dat(map_dat(
            cell_events=[(7, 0), (8, 1), (9, 0), (10, 1), (0xFF, 0)],
            search=[(0, 0xB4), (1, -5), (2, 17), (1, 1000)]), "t"),
        cod=None)


class ParseTest(unittest.TestCase):
    def test_mpl_tiles_are_signed_row_major_from_0x0b(self):
        layer = map_decode.parse_mpl(mpl(3, 1, [4, -1, 0x7FFF]), "t")
        self.assertEqual((layer["width"], layer["height"]), (3, 1))
        self.assertEqual(layer["tiles"], [4, -1, 0x7FFF])

    def test_mpl_with_wrong_size_is_rejected(self):
        with self.assertRaises(map_decode.MapError):
            map_decode.parse_mpl(mpl(3, 2, [0, 0, 0]), "t")

    def test_dtl_cells_start_at_0x10(self):
        layer = map_decode.parse_dtl(dtl(2, 2, [1, 2, 3, 4]), "t")
        self.assertEqual(layer["cells"], bytes([1, 2, 3, 4]))
        self.assertEqual(layer["width"], 2)

    def test_attr_rows_are_flags_blend_terrain_backdrop_from_0x11(self):
        table = map_decode.parse_attr(attr([(0x42, 0, 5, 33)]), "t")
        self.assertEqual(table["rows"][0], {"flags": 0x42, "blend": 0,
                                            "terrain": 5, "backdrop": 33})

    def test_dsc_record_fields(self):
        raw = struct.pack("<i", 2)
        raw += struct.pack("<6i", 0, 0, 8, 8, 0, 0) + bytes([9, 0, 0, 0, 1, 0, 0, 0])
        raw += struct.pack("<6i", 1, 2, 4, 2, 3, -1) + bytes([11, 0, 0, 0, 2, 0, 0, 0])
        layers = map_decode.parse_dsc(raw, "t")
        self.assertEqual(len(layers), 2)
        self.assertEqual(layers[1], {"scroll": (1, 2), "parallax": (4, 2),
                                     "step": (3, -1), "draw_depth": 11,
                                     "attr_mode": 2})

    def test_map_dat_tables(self):
        spawn = bytes([0, 0x40, 0, 0, 5, 1, 2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                       0, 0, 0, 0, 0x05, 3, 4, 2, 7, 0xFF, 0xFF, 0xFF, 0])
        dat = map_decode.parse_map_dat(map_dat(turn=[(3, 6, 0)], spawns=[spawn]), "t")
        self.assertEqual(dat["player_slots"], 1)
        self.assertEqual(dat["turn_events"][0], {"turn": 3, "handler": 6, "side": 0})
        self.assertEqual(len(dat["spawns"]), 1)
        s = dat["spawns"][0]
        self.assertEqual((s["side"], s["char_id"], s["level"], s["ai"], s["dest"],
                          s["cell_code"], s["wave"], s["death_op"], s["death_arg"]),
                         (0, 0x40, 5, 5, (3, 4), 2, 7, 0xFF, -1))


class CellTest(unittest.TestCase):
    def test_event_code_is_read_with_the_event_layers_own_width(self):
        m = small_map()
        # (0,1) is event-layer index 1*4+0 = 4 -> code 3; the terrain width of 2
        # would have given index 2 -> code 9.
        self.assertEqual(map_decode.tile_info(m, 0, 1)["event_code"], 3)

    def test_tile_info_reads_the_attribute_row_of_the_tile(self):
        info = map_decode.tile_info(small_map(), 1, 0)
        self.assertEqual((info["tile"], info["flags"], info["terrain"],
                          info["backdrop"]), (1, 0x20, 5, 3))

    def test_chest_reads_the_search_record_at_code_times_three(self):
        c = map_decode.classify_cell(small_map(), 1, 0)   # chest, code 2
        self.assertEqual(c["kind"], "chest")
        self.assertEqual(c["record"], {"index": 2, "kind": 2, "payload": 17})

    def test_buried_cell_with_code_0_uses_record_0(self):
        c = map_decode.classify_cell(small_map(), 1, 1)   # buried, code 0
        self.assertEqual(c["kind"], "buried")
        self.assertEqual(c["record"], {"index": 0, "kind": 0, "payload": 0xB4})

    def test_repaint_cell_is_not_searchable_and_reports_no_tile_event(self):
        c = map_decode.classify_cell(small_map(), 0, 1)   # tile 2 is 0x60
        self.assertEqual(c["kind"], "repaint")
        self.assertNotIn("record", c)
        self.assertNotIn("tile_event", c)

    def test_plain_cell_with_a_code_reads_the_tile_event_at_code_minus_one(self):
        c = map_decode.classify_cell(small_map(), 0, 0)   # plain, code 5
        self.assertEqual(c["kind"], None)
        self.assertEqual(c["tile_event"], {"code": 5, "handler": None, "trigger": 0})


@unittest.skipUnless(HAVE_DUMP, "workspace/vfs_dump not extracted")
class ShippedDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = map_decode.load_map(DUMP, 0)

    def test_chapter_1_treasure_matches_the_guide(self):
        found = {(c["x"], c["y"]): (c["kind"], c["record"]["kind"], c["record"]["payload"])
                 for c in map_decode.searchable_cells(self.m)}
        self.assertEqual(found[(9, 15)], ("chest", 1, 1000))
        self.assertEqual(found[(18, 17)], ("chest", 0, 0xB4))
        self.assertEqual(found[(21, 19)], ("buried", 0, 0xB4))

    def test_map_00_event_layer_is_wider_than_the_terrain(self):
        self.assertEqual(self.m["dtl"]["width"], 64)
        self.assertEqual(self.m["layers"][0]["mpl"]["width"], 30)

    def test_render_is_24_pixels_per_cell(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "m.png"
            map_decode.render_map(self.m, map_decode.load_palette(DUMP), out)
            from PIL import Image
            with Image.open(out) as img:
                self.assertEqual(img.size, (30 * 24, 24 * 24))

    def test_report_has_one_row_per_map(self):
        rows = [line for line in map_decode.report_markdown(DUMP).splitlines()
                if line.startswith("| `")]
        self.assertEqual(len(rows), 64)   # M00..M64 without M30


if __name__ == "__main__":
    unittest.main()
