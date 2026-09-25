"""Tests for glyph_match at its seams: the ET3 index -> character layout and
the exact-match rule.

The layout anchors are the ones fd2-anatomy verified by looking at the bitmaps
(resource_info/chinese_glyph_encoding.md there): index 0 is 一 (Big5 0xA440),
index 66 is 中, and the ETEN extension 裏 is cp950 0xF9D8.

    python -m unittest tools/glyph/test_glyph_match.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import glyph_match  # noqa: E402


class Et3LayoutTest(unittest.TestCase):
    def setUp(self):
        self.et3 = glyph_match.load_et3()

    def test_first_glyph_is_yi(self):
        self.assertEqual(self.et3.char(0), "一")

    def test_zhong_sits_at_66(self):
        self.assertEqual(self.et3.char(66), "中")

    def test_eten_extension_decodes_through_cp950(self):
        index = self.et3.index_of_big5(0xF9D8)
        self.assertEqual(self.et3.char(index), "裏")

    def test_level_one_ends_after_63_entries_of_lead_c6(self):
        self.assertEqual(self.et3.big5(5400), 0xC67E)
        self.assertEqual(self.et3.big5(5401), 0xC940)


class ExactMatchTest(unittest.TestCase):
    def setUp(self):
        self.et3 = glyph_match.load_et3()

    def test_et3_glyph_top_aligned_in_16_rows_is_exact(self):
        rows = list(self.et3.std[66]) + [0]
        result = glyph_match.match_glyph(rows, self.et3)
        self.assertEqual(result["exact"], "中")
        self.assertEqual(result["candidates"][0]["distance"], 0)

    def test_a_pixel_in_the_sixteenth_row_breaks_exactness(self):
        rows = list(self.et3.std[66]) + [0x8000]
        result = glyph_match.match_glyph(rows, self.et3)
        self.assertIsNone(result["exact"])
        self.assertEqual(result["candidates"][0]["char"], "中")
        self.assertEqual(result["candidates"][0]["distance"], 1)

    def test_blank_glyph_has_no_exact_match(self):
        result = glyph_match.match_glyph([0] * 16, self.et3)
        self.assertIsNone(result["exact"])
        self.assertTrue(result["blank"])


if __name__ == "__main__":
    unittest.main()
