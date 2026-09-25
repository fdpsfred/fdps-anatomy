"""Unit tests for units_media.py (python -m unittest tools/cut_content/test_units_media.py)."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cut_content  # noqa: E402
import units_media  # noqa: E402


class UnionBoxTest(unittest.TestCase):
    def test_the_box_covers_every_written_pixel_of_every_frame(self):
        # two 4x3 masks: one pixel at (1,0), one at (3,2)
        first = bytes([0, 1, 0, 0,
                       0, 0, 0, 0,
                       0, 0, 0, 0])
        second = bytes([0, 0, 0, 0,
                        0, 0, 0, 0,
                        0, 0, 0, 1])
        self.assertEqual(units_media.union_box([first, second], 4, 3), (1, 0, 4, 3))

    def test_an_animation_that_draws_nothing_has_no_box(self):
        self.assertIsNone(units_media.union_box([bytes(6)], 3, 2))


class GridTest(unittest.TestCase):
    def test_cells_run_left_to_right_then_wrap(self):
        # three 2x1 cells, at most two per row, gap 1
        width, height, places = units_media.grid(3, 2, 1, gap=1, max_width=5)
        self.assertEqual((width, height), (5, 3))
        self.assertEqual(places, [(0, 0), (3, 0), (0, 2)])

    def test_a_single_row_when_everything_fits(self):
        width, height, places = units_media.grid(12, 24, 24, gap=1, max_width=2048)
        self.assertEqual((width, height), (12 * 24 + 11, 24))
        self.assertEqual(places[-1], (11 * 25, 0))


GAME = cut_content.DEFAULT_GAME


@unittest.skipUnless((GAME / "FACE.CEL").exists(), "original game files are not present")
class GenerateTest(unittest.TestCase):
    def test_every_file_follows_the_naming_rule_and_belongs_to_a_units_entry(self):
        with tempfile.TemporaryDirectory() as scratch:
            out = Path(scratch)
            units_media.generate(out, GAME)
            names = sorted(p.name for p in out.iterdir())
        for name in names:
            match = cut_content.MEDIA_NAME.match(name)
            self.assertIsNotNone(match, name)
            self.assertTrue(match.group(1).startswith("u"), name)
        # the four enemies of U01: a portrait, an icon group, two animations
        self.assertIn("u01-face-087.png", names)
        self.assertIn("u01-icon-111.png", names)
        self.assertIn("u01-stand092-sheet.png", names)
        self.assertIn("u01-act110-f16.png", names)
        self.assertNotIn("u01-act110-f17.png", names)   # ACT110 has 17 frames
        self.assertIn("u01-act087-s0.wav", names)
        self.assertIn("u03-icon-058.png", names)
        self.assertNotIn("u03-face-058.png", names)     # that portrait is blank

    def test_two_runs_are_byte_identical(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            units_media.generate(Path(a), GAME)
            units_media.generate(Path(b), GAME)
            for path in sorted(Path(a).iterdir()):
                self.assertEqual(path.read_bytes(), (Path(b) / path.name).read_bytes(), path.name)


if __name__ == "__main__":
    unittest.main()
