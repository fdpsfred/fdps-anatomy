"""Tests for text_decode at its public seam: parse_block() and render_entry().

The blocks here are built by hand from the format the game's own reader
(fdps_draw_text, src/text.c) walks, so the expected values come from that
contract rather than from the decoder under test.

    python -m unittest tools/text_decode/test_text_decode.py
"""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import text_decode  # noqa: E402


def block(*streams):
    """Assemble a block: a signed 16-bit offset table, then each stream + -1."""
    table_bytes = 2 * len(streams)
    offsets, payload = [], b""
    for stream in streams:
        offsets.append(table_bytes + len(payload))
        payload += struct.pack(f"<{len(stream) + 1}h", *stream, -1)
    return struct.pack(f"<{len(offsets)}h", *offsets) + payload


class ParseBlockTest(unittest.TestCase):
    def test_entries_are_found_through_the_offset_table(self):
        entries = text_decode.parse_block(block([5, 6], [7]))
        self.assertEqual([e.index for e in entries], [0, 1])
        self.assertEqual([t.value for t in entries[0].tokens], [5, 6])
        self.assertEqual([t.value for t in entries[1].tokens], [7])

    def test_glyph_zero_is_a_glyph_not_a_terminator(self):
        entries = text_decode.parse_block(block([0, 0]))
        self.assertEqual([(t.kind, t.value) for t in entries[0].tokens],
                         [("glyph", 0), ("glyph", 0)])

    def test_speaker_codes_carry_one_operand_word(self):
        entries = text_decode.parse_block(block([-0x11, 11, 3, -0x12, 2, 4]))
        tokens = entries[0].tokens
        self.assertEqual([(t.kind, t.operand) for t in tokens],
                         [("speaker_char", 11), ("glyph", None),
                          ("speaker_unit", 2), ("glyph", None)])

    def test_operand_equal_to_terminator_is_still_an_operand(self):
        # The reader steps over the operand word unread by its loop test, so an
        # operand of -1 must not end the entry.
        entries = text_decode.parse_block(block([-0x12, -1, 9]))
        self.assertEqual([(t.kind, t.operand) for t in entries[0].tokens],
                         [("speaker_unit", -1), ("glyph", None)])

    def test_control_codes_are_named(self):
        entries = text_decode.parse_block(block([-2, -3, -4, -5, -6]))
        self.assertEqual([t.kind for t in entries[0].tokens],
                         ["line_break", "page_break", "subst_1", "subst_2",
                          "number"])

    def test_unknown_negative_code_is_rejected(self):
        with self.assertRaises(text_decode.TextBlockError):
            text_decode.parse_block(block([-7]))

    def test_unterminated_entry_is_rejected(self):
        data = struct.pack("<hh", 2, 5)
        with self.assertRaises(text_decode.TextBlockError):
            text_decode.parse_block(data)


class RenderEntryTest(unittest.TestCase):
    def test_known_glyphs_become_characters_and_codes_become_tags(self):
        entries = text_decode.parse_block(
            block([-0x11, 0x0b, 1, 2, -2, 3, -6, -3, -4, -5]))
        table = {1: "索", 2: "爾", 3: "好"}
        self.assertEqual(text_decode.render_entry(entries[0], table),
                         "{speaker char=11}索爾{br}好{number}{page}"
                         "{subst1}{subst2}")

    def test_unknown_glyph_is_tagged_with_its_index(self):
        entries = text_decode.parse_block(block([1, 0x123]))
        self.assertEqual(text_decode.render_entry(entries[0], {1: "索"}),
                         "索{glyph 0x0123}")

    def test_speaker_by_unit_is_tagged_with_the_unit_index(self):
        entries = text_decode.parse_block(block([-0x12, 4]))
        self.assertEqual(text_decode.render_entry(entries[0], {}),
                         "{speaker unit=4}")


if __name__ == "__main__":
    unittest.main()
