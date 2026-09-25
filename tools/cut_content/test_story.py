"""Unit tests for story.py (python -m unittest tools/cut_content/test_story.py)."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import story  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "text_decode"))
from text_decode import Entry, Token  # noqa: E402


class ApplyBlocksTest(unittest.TestCase):
    PAGE = ("intro\n\n<!-- story:a -->\nold a\n<!-- /story:a -->\n\nmiddle\n\n"
            "<!-- story:b -->\n<!-- /story:b -->\nend\n")

    def test_every_marked_block_is_replaced_and_the_prose_kept(self):
        text, problems = story.apply_blocks(self.PAGE, {"a": "new a\nline 2", "b": "new b"})
        self.assertEqual(problems, [])
        self.assertEqual(text, "intro\n\n<!-- story:a -->\nnew a\nline 2\n<!-- /story:a -->\n\n"
                               "middle\n\n<!-- story:b -->\nnew b\n<!-- /story:b -->\nend\n")

    def test_a_block_without_a_marker_and_a_marker_without_a_block_are_reported(self):
        _, problems = story.apply_blocks(self.PAGE, {"a": "x", "c": "y"})
        self.assertEqual(sorted(problems), ["block c has no marker in the page",
                                            "marker b has no generated block"])


class TranscriptTest(unittest.TestCase):
    def test_speakers_line_and_page_breaks(self):
        glyphs = {1: "好", 2: "！", 3: "走"}
        tokens = (Token("speaker_char", -0x11, 12, 0), Token("glyph", 1, None, 4),
                  Token("glyph", 2, None, 6), Token("line_break", -2, None, 8),
                  Token("glyph", 3, None, 10), Token("page_break", -3, None, 12),
                  Token("glyph", 3, None, 14), Token("speaker_unit", -0x12, 3, 16),
                  Token("glyph", 1, None, 20))
        lines = story.transcript(Entry(9, 0, tokens), glyphs, {12: "索爾"}.get)
        self.assertEqual(lines, ["【索爾】（角色 12）", "好！", "走", "▼", "走",
                                 "【地圖單位 3】", "好"])

    def test_empty_entry_is_no_lines(self):
        self.assertEqual(story.transcript(Entry(0, 0, ()), {}, str), [])


class OwnershipTest(unittest.TestCase):
    def test_owned_entries_must_be_never_shown_and_every_never_shown_one_owned(self):
        never = {(50, 9), (50, 10), (33, 9)}
        owners = {(50, 9): "S1", (50, 10): "S1", (7, 14): "S1"}
        problems, pending = story.check_ownership(never, owners, known_ids={"S1"},
                                                  unsettled_blocks=set())
        self.assertEqual(sorted(problems), [
            "FDETXT07 0x0e is owned by S1 but is shown",
            "FDETXT33 0x09 is never shown and has no owner"])
        self.assertEqual(pending, [])

    def test_owner_must_be_a_known_id_and_pending_owners_are_counted(self):
        never = {(41, 0), (41, 1)}
        owners = {(41, 0): "S99", (41, 1): "?T14-12"}
        problems, pending = story.check_ownership(never, owners, known_ids={"S1"},
                                                  unsettled_blocks=set())
        self.assertEqual(problems, ["FDETXT41 0x00 names S99, which is neither an entry "
                                    "nor an exclusion"])
        self.assertEqual(pending, [((41, 1), "T14-12")])

    def test_blocks_not_settled_yet_only_need_their_owners_listed(self):
        problems, pending = story.check_ownership(set(), {(9, 17): "S4"}, known_ids={"S4"},
                                                  unsettled_blocks={9})
        self.assertEqual(problems, [])
        self.assertEqual(pending, [((9, 17), "chapter judgement")])


class SlugTest(unittest.TestCase):
    def test_headings_become_github_anchors(self):
        self.assertEqual(story.slug("S1 地圖 49：索爾被假索爾擒住"), "s1-地圖-49索爾被假索爾擒住")
        self.assertEqual(story.slug("S6 `ICON0032.SAF`：沒有腳本播放的開寶箱動畫"),
                         "s6-icon0032saf沒有腳本播放的開寶箱動畫")


class CueTest(unittest.TestCase):
    def test_track_span_runs_to_the_next_track_or_the_end_of_the_image(self):
        tracks = [(1, "MODE1/2352", 0), (2, "AUDIO", 100), (3, "AUDIO", 250)]
        self.assertEqual(story.track_span(tracks, 2, 1000 * 2352), (100 * 2352, 250 * 2352))
        self.assertEqual(story.track_span(tracks, 3, 1000 * 2352), (250 * 2352, 1000 * 2352))
        with self.assertRaises(ValueError):
            story.track_span(tracks, 1, 1000 * 2352)

    def test_wav_header_is_44100_hz_16_bit_stereo(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.wav"
            story.write_cdda_wav(path, b"\x01\x02\x03\x04" * 3)
            data = path.read_bytes()
        self.assertEqual(data[:4], b"RIFF")
        self.assertEqual(struct.unpack_from("<I", data, 4)[0], 36 + 12)
        self.assertEqual(struct.unpack_from("<HHIIHH", data, 20), (1, 2, 44100, 176400, 4, 16))
        self.assertEqual(data[36:40], b"data")
        self.assertEqual(struct.unpack_from("<I", data, 40)[0], 12)
        self.assertEqual(data[44:], b"\x01\x02\x03\x04" * 3)


if __name__ == "__main__":
    unittest.main()
