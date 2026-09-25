"""Unit tests for tools/cut_items/acquisition.py.

    python -m unittest tools/cut_items/test_acquisition.py

The survey rules are tested on small synthetic inputs; the last class runs the
real survey against workspace/vfs_dump when it is there and checks the counts
cut_content/items.md states (226 items with content, 171 obtainable, 55 not,
32 of them carried by no deployment record at all).
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import acquisition  # noqa: E402


def spawn(side=0, equip=(0xFF, 0xFF), carried=(0xFF,) * 6, death_op=0xFF, death_arg=0):
    return {"side": side, "equip": equip, "carried": carried,
            "death_op": death_op, "death_arg": death_arg}


def game_map(number, spawns=(), search=(), live=()):
    records = list(search) + [{"kind": 0, "payload": 0}] * (16 - len(search))
    return {"number": number, "spawns": list(spawns), "search": records,
            "live_codes": set(live)}


def survey(items=(0x10,), maps=(), shops=None, appearance=(), grants=()):
    content = {i: bytes([1]) + bytes(22) for i in items}
    return acquisition.survey(content, appearance=list(appearance),
                              shops=shops or {}, maps=list(maps), grants=grants)


class Shops(unittest.TestCase):
    def test_village_rows_are_reachable(self):
        s = survey(shops={1: [[0x10], [], []]})
        self.assertTrue(s[0x10]["reach"])

    def test_secret_row_needs_a_code_row(self):
        # Chapter index 25 has a village but its code reads past the 24-row table.
        s = survey(shops={25: [[], [], [0x10]]})
        self.assertFalse(s[0x10]["reach"])
        self.assertIn(("sealed_shop", 25), s[0x10]["seen"])

    def test_secret_row_below_25_is_reachable(self):
        s = survey(shops={24: [[], [], [0x10]]})
        self.assertTrue(s[0x10]["reach"])

    def test_placeholder_shops_are_not_reachable(self):
        for chapter in (0, 16, 17, 21, 22, 26, 29):
            s = survey(shops={chapter: [[0x10], [], []]})
            self.assertFalse(s[0x10]["reach"], chapter)


class Maps(unittest.TestCase):
    def test_record_behind_a_searchable_cell_is_reachable(self):
        s = survey(maps=[game_map(5, search=[{"kind": 0, "payload": 0x10}], live={0})])
        self.assertTrue(s[0x10]["reach"])

    def test_record_without_a_cell_is_only_seen(self):
        s = survey(maps=[game_map(5, search=[{"kind": 0, "payload": 0x10}])])
        self.assertFalse(s[0x10]["reach"])
        self.assertTrue(s[0x10]["seen"])

    def test_money_and_event_records_are_not_items(self):
        s = survey(maps=[game_map(5, search=[{"kind": 1, "payload": 0x10},
                                             {"kind": 2, "payload": 0x10}], live={0, 1})])
        self.assertFalse(s[0x10]["reach"])
        self.assertFalse(s[0x10]["seen"])

    def test_chapter_drop_is_reachable(self):
        s = survey(maps=[game_map(3, spawns=[spawn(death_op=0, death_arg=0x10)])])
        self.assertTrue(s[0x10]["reach"])

    def test_enemy_equipment_is_only_seen(self):
        s = survey(maps=[game_map(3, spawns=[spawn(side=0, equip=(0x10, 0xFF))])])
        self.assertFalse(s[0x10]["reach"])
        self.assertTrue(s[0x10]["deployed"])

    def test_guest_on_the_player_side_can_hand_items_over(self):
        s = survey(maps=[game_map(3, spawns=[spawn(side=2, carried=(0x10,) + (0xFF,) * 5)])])
        self.assertTrue(s[0x10]["reach"])

    def test_scene_maps_give_nothing(self):
        s = survey(maps=[game_map(51, spawns=[spawn(side=2, equip=(0x10, 0xFF),
                                                    death_op=0, death_arg=0x10)],
                                  search=[{"kind": 0, "payload": 0x10}], live={0})])
        self.assertFalse(s[0x10]["reach"])
        self.assertTrue(s[0x10]["seen"])


class Other(unittest.TestCase):
    def test_joining_character_equipment_is_reachable(self):
        raw = bytearray(24)
        raw[0x0C:0x12] = bytes([0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
        s = survey(appearance=[{"raw": bytes(raw)}])
        self.assertTrue(s[0x10]["reach"])

    def test_code_grant_is_reachable(self):
        s = survey(grants=[(0x10, "src/chevt2.c")])
        self.assertTrue(s[0x10]["reach"])

    def test_lottery_grand_prizes_are_only_seen(self):
        s = survey(items=(0xBF, 0xB4))
        self.assertFalse(s[0xBF]["reach"])
        self.assertTrue(s[0xBF]["seen"])
        self.assertTrue(s[0xB4]["reach"])


class RealData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (acquisition.DEFAULT_DUMP / "MISC" / "ITEM.DAT").is_file():
            raise unittest.SkipTest("workspace/vfs_dump is not there")
        cls.result = acquisition.run(acquisition.DEFAULT_DUMP)

    def test_counts_match_the_page(self):
        counts = acquisition.counts(self.result)
        self.assertEqual((counts["content"], counts["obtainable"], counts["unobtainable"],
                          counts["not_deployed"]), (226, 171, 55, 32))

    def test_the_page_table_matches_the_data(self):
        self.assertEqual(acquisition.check(self.result), [])

    def test_types_no_player_can_get(self):
        self.assertEqual(acquisition.unmet_types(self.result),
                         [0x08, 0x12, 0x19, 0x1D, 0x22, 0x26])


if __name__ == "__main__":
    unittest.main()
