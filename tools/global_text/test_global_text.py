"""Tests for global_text at its two seams: scan_readers() over a source tree and
classify_scene_block() over a decoded block plus the scripts' text references.

The source trees here are written by hand in the shape src/ uses, so the
expected readers come from reading those few lines, not from the scanner.

    python -m unittest tools/global_text/test_global_text.py
"""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import global_text  # noqa: E402


def source_tree(files):
    directory = tempfile.TemporaryDirectory()
    for name, text in files.items():
        (Path(directory.name) / name).write_text(text, encoding="utf-8")
    return directory


class ScanReadersTest(unittest.TestCase):
    def scan(self, files):
        with source_tree(files) as src:
            return global_text.scan_readers(Path(src))

    def test_a_macro_entry_id_is_a_fixed_reader_in_its_function(self):
        readers = self.scan({"menu.c": (
            "#define TEXT_QUIT_QUESTION 0x1f0\n"
            "void fdps_menu(void)\n"
            "{\n"
            "    fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_QUIT_QUESTION,\n"
            "                   (unsigned char *) 0xa0000, 0x140, 0xd0, 0, 0x6d);\n"
            "}\n")})
        self.assertEqual([(r.function, r.file, r.via, r.ids, r.base) for r in readers],
                         [("fdps_menu", "menu.c", "draw", (0x1f0,), None)])

    def test_a_literal_id_and_a_macro_sum_are_fixed_readers(self):
        readers = self.scan({"menu.c": (
            "#define FIRST 0x1f0\n"
            "void fdps_menu(void)\n"
            "{\n"
            "    fdps_draw_text(data_fdps_all_game_text_ptr, 0x10, d, p, 1, 2, 3);\n"
            "    fdps_draw_text(data_fdps_all_game_text_ptr, FIRST + 010, d, p, 1, 2, 3);\n"
            "}\n")})
        # 010 is C octal: eight.
        self.assertEqual([r.ids for r in readers], [(0x10,), (0x1f8,)])

    def test_a_local_that_is_also_added_to_is_not_guessed(self):
        with self.assertRaises(global_text.ScanError):
            self.scan({"odd.c": (
                "#define FIRST 0x20a\n"
                "void fdps_odd(int k)\n"
                "{\n"
                "    int id;\n"
                "    id = FIRST;\n"
                "    id += k;\n"
                "    fdps_draw_text(data_fdps_all_game_text_ptr, id, d, p, 1, 2, 3);\n"
                "}\n")})

    def test_calls_inside_comments_are_not_readers(self):
        readers = self.scan({"menu.c": (
            "/* fdps_draw_text(data_fdps_all_game_text_ptr, 0x10, x, 1, 2, 3, 4) */\n"
            "void fdps_menu(void)\n"
            "{\n"
            "}\n")})
        self.assertEqual(readers, [])

    def test_a_record_field_plus_a_bias_is_an_indexed_reader(self):
        readers = self.scan({"panel.c": (
            "#define NAME_TEXT_ID_BIAS 1\n"
            "void fdps_panel(struct unit *member)\n"
            "{\n"
            "    fdps_draw_text(data_fdps_all_game_text_ptr,\n"
            "                   (int) member->char_id + NAME_TEXT_ID_BIAS, d, p, 1, 2, 3);\n"
            "}\n")})
        self.assertEqual([(r.base, r.ids, r.index) for r in readers],
                         [(1, None, "(int) member->char_id")])

    def test_a_local_holding_macros_resolves_to_every_value_assigned(self):
        readers = self.scan({"search.c": (
            "#define CHEST_PROMPT 0x20a\n"
            "#define BURIED_PROMPT 0x20b\n"
            "void fdps_search(int kind)\n"
            "{\n"
            "    int prompt_text_id;\n"
            "    if (kind) {\n"
            "        prompt_text_id = CHEST_PROMPT;\n"
            "    } else {\n"
            "        prompt_text_id = BURIED_PROMPT;\n"
            "    }\n"
            "    fdps_draw_text(data_fdps_all_game_text_ptr, prompt_text_id, d, p, 1, 2, 3);\n"
            "}\n")})
        self.assertEqual([r.ids for r in readers], [(0x20a, 0x20b)])

    def test_a_local_table_element_plus_a_base_reaches_base_plus_each_value(self):
        readers = self.scan({"bar.c": (
            "#define PRIZE_FIRST 0x225\n"
            "#define CURSOR_FRAME 2\n"
            "void fdps_lottery(void)\n"
            "{\n"
            "    int prize_of_frame[4] = {\n"
            "        0, 3, 1, 3\n"
            "    };\n"
            "    fdps_draw_text(data_fdps_all_game_text_ptr,\n"
            "                   prize_of_frame[cursor[CURSOR_FRAME]] + PRIZE_FIRST,\n"
            "                   d, p, 1, 2, 3);\n"
            "}\n")})
        self.assertEqual([r.ids for r in readers], [(0x225, 0x226, 0x228)])

    def test_a_substitution_slot_store_is_a_reader_through_that_slot(self):
        readers = self.scan({"shop.c": (
            "#define ITEM_NAME_BASE 0xc9\n"
            "void fdps_shop(int item_id)\n"
            "{\n"
            "    data_fdps_dialog_subst_text_id_2 =\n"
            "        item_id + ITEM_NAME_BASE;\n"
            "}\n")})
        self.assertEqual([(r.via, r.base) for r in readers], [("subst2", 0xc9)])

    def test_the_substitution_dispatch_itself_is_not_a_reader(self):
        readers = self.scan({"text.c": (
            "unsigned char *fdps_draw_text(unsigned char *b, int id)\n"
            "{\n"
            "    fdps_draw_text(data_fdps_all_game_text_ptr,\n"
            "                   data_fdps_dialog_last_action_text_id_param, d, p, 1, 2, 3);\n"
            "}\n")})
        self.assertEqual(readers, [])

    def test_an_expression_it_cannot_resolve_is_reported_not_guessed(self):
        with self.assertRaises(global_text.ScanError):
            self.scan({"odd.c": (
                "void fdps_odd(int a, int b)\n"
                "{\n"
                "    fdps_draw_text(data_fdps_all_game_text_ptr, a * b, d, p, 1, 2, 3);\n"
                "}\n")})


class ClassifySceneBlockTest(unittest.TestCase):
    def test_entries_split_into_shown_never_shown_and_empty(self):
        texts = ["第十二章", "", "看！", "孩子"]
        refs = [("ICON11", 0x40, 2)]
        shown, unshown, empty = global_text.classify_scene_block(texts, refs)
        self.assertEqual(shown, {2: [("ICON11", 0x40)]})
        self.assertEqual(unshown, [0, 3])
        self.assertEqual(empty, [1])


GAME_DIR = global_text.DEFAULT_GAME


@unittest.skipUnless((GAME_DIR / global_text.FIELD_ARCHIVE).exists(), "needs fdps_game_files/")
class ShippedDataTest(unittest.TestCase):
    """Pins what the two pages rest on, against the shipped game and src/."""

    @classmethod
    def setUpClass(cls):
        cls.blocks = global_text.load_blocks(GAME_DIR)
        cls.readers = global_text.scan_readers()
        cls.reports = global_text.load_scripts(GAME_DIR)

    def test_every_global_gate_passes(self):
        self.assertEqual(global_text.check_global(self.blocks[0], self.readers), [])

    def test_every_scene_gate_passes(self):
        self.assertEqual(global_text.check_scene(self.blocks, self.reports), [])

    def test_the_known_names_sit_where_the_formulas_put_them(self):
        texts = self.blocks[0]
        self.assertEqual(texts[0x00 + 1], "蘭迪斯")       # character 0
        self.assertEqual(texts[0x3c + 1], "平衡之神")     # ENEMYDAT row 0
        self.assertEqual(texts[0x97 + 0], "人類")         # race 0
        self.assertEqual(texts[0xa1 + 0], "劍士")         # class 0
        self.assertEqual(texts[0xc9 + 0xff], "裂地術")    # the guide's item FF
        self.assertEqual(texts[0x1be + 0], "業火")        # spell 0

    def test_only_maps_30_31_33_and_49_are_never_switched_to(self):
        switched = {step.context["map"] for r in self.reports for step in r.trace.steps
                    if step.step.mnemonic == "SWITCH_MAP"}
        never = {b - 1 for b in range(global_text.FIRST_SCENE_BLOCK,
                                      global_text.LAST_SCENE_BLOCK + 1)} - switched
        self.assertEqual(never, {30, 31, 33, 49})


if __name__ == "__main__":
    unittest.main()
