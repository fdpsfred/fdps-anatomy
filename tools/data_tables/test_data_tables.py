"""Tests for data_tables at its public seams: the record decoders, the game-text
name lookups, the deployment census, and the knowledge-base check.

Expected values come from sources independent of the code under test:
hand-built byte records, the strategy guide's own printed numbers
(docs/guide/fdps/walkthrough.txt), and the class-change rules src/church.h
spells out.  The tests that read game data skip when tools/vfs_dump has not
been run.

    python -m unittest tools/data_tables/test_data_tables.py
"""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DUMP = ROOT / "workspace" / "vfs_dump"
HAVE_DUMP = (DUMP / "MISC" / "ENEMYDAT.DAT").is_file() and (DUMP / "FIELD" / "FDETXT00.TXT").is_file()


class RecordDecoders(unittest.TestCase):
    def test_enemy_record_is_ten_bytes_with_a_word_of_hp(self):
        raw = bytes([6, 0x1A]) + struct.pack("<H", 600) + bytes([255, 8, 0, 0, 0, 255])
        (rec,) = data_tables.decode_enemies(raw)
        self.assertEqual(rec, {"race": 6, "class": 0x1A, "hp": 600, "mp": 255, "ap": 8,
                               "dp": 0, "dx": 0, "mv": 0, "exp": 255})

    def test_enemy_file_must_be_whole_records(self):
        with self.assertRaises(data_tables.TableError):
            data_tables.decode_enemies(bytes(15))

    def test_promotion_record_is_four_routes_of_form_class_move(self):
        raw = bytes([0x0F, 1, 1, 0x0F, 1, 1, 0x18, 2, 1, 0x21, 3, 2])
        (rec,) = data_tables.decode_promotions(raw)
        self.assertEqual(rec, [(0x0F, 1, 1), (0x0F, 1, 1), (0x18, 2, 1), (0x21, 3, 2)])

    def test_shop_rows_skip_empty_slots_but_keep_stock_after_them(self):
        # src/gamedata.h: SHOP01.DAT's weapon row, stock after two empty slots.
        raw = (bytes([0xB4, 0xDE]) + b"\xff" * 10
               + bytes([0x02, 0x71, 0xFF, 0xFF, 0x72, 0x73]) + b"\xff" * 6
               + b"\xff" * 12)
        self.assertEqual(data_tables.decode_shop(raw),
                         [[0xB4, 0xDE], [0x02, 0x71, 0x72, 0x73], []])

    def test_shop_file_is_exactly_three_rows_of_twelve(self):
        with self.assertRaises(data_tables.TableError):
            data_tables.decode_shop(bytes(35))


class CompactList(unittest.TestCase):
    def test_runs_of_three_or_more_become_ranges(self):
        self.assertEqual(data_tables.compact([1, 2, 3, 5, 7, 8, 9, 10]), "1–3、5、7–10")

    def test_pairs_stay_separate(self):
        self.assertEqual(data_tables.compact([4, 5]), "4、5")

    def test_empty_is_a_dash(self):
        self.assertEqual(data_tables.compact([]), "—")


class ApplyTable(unittest.TestCase):
    TABLE = "| a | b |\n| --- | --- |\n| 1 | 2 |\n"

    def test_marker_line_becomes_the_table(self):
        doc = "intro\n\n<!-- data_tables:t -->\n\nafter\n"
        out = data_tables.apply_table(doc, "t", ("a", "b"), self.TABLE)
        self.assertEqual(out, "intro\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\nafter\n")

    def test_existing_table_is_replaced_and_prose_kept(self):
        doc = "intro\n| a | b |\n| --- | --- |\n| 9 | 9 |\n| 8 | 8 |\nafter\n"
        out = data_tables.apply_table(doc, "t", ("a", "b"), self.TABLE)
        self.assertEqual(out, "intro\n| a | b |\n| --- | --- |\n| 1 | 2 |\nafter\n")

    def test_a_document_with_neither_is_an_error(self):
        with self.assertRaises(data_tables.TableError):
            data_tables.apply_table("nothing here\n", "t", ("a", "b"), self.TABLE)


@unittest.skipUnless(HAVE_DUMP, "needs workspace/vfs_dump (tools/vfs_dump)")
class GameData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.game = data_tables.load(DUMP)

    def test_character_names_sit_one_entry_past_the_id(self):
        self.assertEqual(self.game.char_name(0x00), "蘭迪斯")
        self.assertEqual(self.game.char_name(0x0B), "蘭斯洛特")
        self.assertEqual(self.game.char_name(0x3C), "平衡之神")

    def test_class_item_spell_and_race_names(self):
        self.assertEqual(self.game.class_name(0x1A), "魔神")
        self.assertEqual(self.game.item_name(0x00), "岩石")
        self.assertEqual(self.game.item_name(0xE1), "暗之徽章")
        self.assertEqual(self.game.spell_name(0x27), "萬神降臨")
        self.assertEqual(self.game.race_name(0), "人類")
        self.assertEqual(self.game.race_name(6), "其他")

    def test_balance_god_hp_matches_the_guide_at_level_40(self):
        # walkthrough.txt, chapter 30: the three 平衡之神 at 6000 / 12000 / 24000 HP.
        self.assertEqual([self.game.enemies[i]["hp"] * 40 for i in range(3)],
                         [6000, 12000, 24000])

    def test_census_finds_the_chapter_2_ice_mage(self):
        # walkthrough.txt, chapter 2: LV8 冰魔導士, the ENEMYDAT row of portrait 0x65.
        maps = {d["map"] for d in self.game.deployments(0x65)}
        self.assertIn(1, maps)
        self.assertTrue(any(d["level"] == 8 for d in self.game.deployments(0x65)))

    def test_randis_hero_route_is_the_hero_class(self):
        # src/church.h: route 3 is 勇者徽章, offered to portrait id 0 only.
        form, clazz, _move = self.game.promotions[0][3]
        self.assertEqual(self.game.class_name(clazz), "英雄")

    def test_chapter_2_weapon_shop_is_what_the_guide_lists(self):
        # walkthrough.txt 第2章 武器店: 長劍、布衣、旅行衣、元素服 -- SHOP01.DAT.
        names = [self.game.item_name(i) for i in self.game.shops[1][1]]
        self.assertEqual(names, ["長劍", "布衣", "旅行衣", "元素服"])

    def test_knowledge_base_agrees_with_the_data(self):
        failures = data_tables.check(self.game)
        self.assertEqual(failures, [], "\n".join(failures[:20]))


if __name__ == "__main__":
    unittest.main()
