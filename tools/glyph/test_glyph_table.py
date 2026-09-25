"""Tests for glyph_table's answer rule: one glyph is one character, except the
two glyphs that draw a symbol twice, which are that character twice.

    python -m unittest tools/glyph/test_glyph_table.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import glyph_table  # noqa: E402


class AnswerRuleTest(unittest.TestCase):
    def test_ordinary_glyph_takes_one_character(self):
        glyph_table.check_answer("0x0041", "？")

    def test_ordinary_glyph_rejects_two_characters(self):
        with self.assertRaises(glyph_table.TableError):
            glyph_table.check_answer("0x0041", "？？")

    def test_double_symbol_glyph_takes_the_character_twice(self):
        glyph_table.check_answer("0x00C4", "？？")

    def test_double_symbol_glyph_rejects_one_character(self):
        with self.assertRaises(glyph_table.TableError):
            glyph_table.check_answer("0x0196", "！")

    def test_double_symbol_glyph_rejects_two_different_characters(self):
        with self.assertRaises(glyph_table.TableError):
            glyph_table.check_answer("0x0196", "！？")

    def test_page_answer_for_a_double_symbol_glyph_is_doubled(self):
        self.assertEqual(glyph_table.page_answer_to_text("0x00C4", "？"), "？？")
        self.assertEqual(glyph_table.page_answer_to_text("0x0041", "？"), "？")


if __name__ == "__main__":
    unittest.main()
