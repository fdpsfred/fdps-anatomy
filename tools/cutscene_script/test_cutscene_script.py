"""Tests for cutscene_script.py.

Two seams are tested: decode_script (bytes in, instruction list out) and
trace_script (instruction list plus the map resources in, per-step context
out).  The expected values are worked by hand from the interpreter in
src/icon.c, not recomputed the way the decoder computes them.

The last class runs the real ICONANI.VFS / FIELD.VFS when fdps_game_files/
is present and is skipped otherwise.

Run: python -m unittest tools/cutscene_script/test_cutscene_script.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cutscene_script as cs  # noqa: E402

GAME_DIR = Path(__file__).resolve().parents[2] / "fdps_game_files"


class DecodeScriptTest(unittest.TestCase):
    def test_walk_units_takes_two_bytes_per_listed_unit(self):
        # 01 frames=3 tiles=2 count=2 (unit 5 down)(unit 6 right), then end
        steps = cs.decode_script(bytes.fromhex("01 03 02 02 05 00 06 03 00"))
        self.assertEqual([s.mnemonic for s in steps], ["WALK_UNITS", "END"])
        walk = steps[0]
        self.assertEqual(walk.offset, 0)
        self.assertEqual(walk.length, 8)
        self.assertEqual(walk.operands["frames_per_sub_step"], 3)
        self.assertEqual(walk.operands["tiles"], 2)
        self.assertEqual(walk.operands["units"], [{"unit": 5, "facing": 0},
                                                  {"unit": 6, "facing": 3}])
        self.assertEqual(steps[1].offset, 8)

    def test_blink_list_is_one_byte_per_unit(self):
        steps = cs.decode_script(bytes.fromhex("09 02 0d 0e 00"))
        self.assertEqual(steps[0].length, 4)
        self.assertEqual(steps[0].operands["units"], [0x0d, 0x0e])
        self.assertEqual(steps[1].offset, 4)

    def test_shake_pairs_are_signed_bytes(self):
        steps = cs.decode_script(bytes.fromhex("10 01 02 fa 00 7f 80 00"))
        self.assertEqual(steps[0].length, 7)
        self.assertEqual(steps[0].operands["offsets"], [[-6, 0], [127, -128]])

    def test_set_map_cell_value_is_a_little_endian_word(self):
        steps = cs.decode_script(bytes.fromhex("14 03 04 34 12 00"))
        self.assertEqual(steps[0].operands, {"x": 3, "y": 4, "value": 0x1234})

    def test_deploy_wave_from_choice(self):
        steps = cs.decode_script(bytes.fromhex("63 04 ff 01 00"))
        self.assertEqual([s.mnemonic for s in steps], ["ASK_THREE_WAY", "DEPLOY_WAVE", "END"])
        self.assertEqual(steps[1].operands, {"wave": "choice", "place_exact": 1})

    def test_music_ff_is_silence(self):
        steps = cs.decode_script(bytes.fromhex("07 ff 07 03 00"))
        self.assertEqual(steps[0].operands, {"music": "silence"})
        self.assertEqual(steps[1].operands, {"music": 3, "cd_track": 4})

    def test_unknown_opcode_ends_the_script_like_opcode_0(self):
        steps = cs.decode_script(bytes.fromhex("0e 04 16 99"))
        self.assertEqual(steps[-1].mnemonic, "END")
        self.assertEqual(steps[-1].opcode, 0x16)
        self.assertEqual(steps[-1].offset, 2)

    def test_truncated_operand_is_an_error(self):
        with self.assertRaises(cs.ScriptError):
            cs.decode_script(bytes.fromhex("01 03 02 02 05 00"))

    def test_running_off_the_end_without_a_terminator_is_an_error(self):
        with self.assertRaises(cs.ScriptError):
            cs.decode_script(bytes.fromhex("0e 04"))


class FakeResources:
    """Two maps: 3 has one player slot and records (char, wave); 52 has two."""

    maps = {
        3: cs.MapInfo(player_slots=1, records=[(0x3c, 0), (0x02, 1), (0x3d, 0), (0x3e, 1)]),
        52: cs.MapInfo(player_slots=2, records=[(0x10, 0)]),
    }
    text_counts = {4: 12, 53: 5}

    def map_info(self, map_no):
        return self.maps.get(map_no)

    def text_entry_count(self, block_no):
        return self.text_counts.get(block_no)

    def has_saf(self, number):
        return number == 7


class TraceScriptTest(unittest.TestCase):
    def trace(self, hexbytes, initial_map, units_known=True):
        steps = cs.decode_script(bytes.fromhex(hexbytes))
        return cs.trace_script(steps, initial_map, FakeResources(), units_known=units_known)

    def test_text_block_follows_the_map_and_switch_map_moves_it(self):
        trace = self.trace("03 02 11 34 03 04 00", initial_map=3)
        first, switch, second = trace.steps[0], trace.steps[1], trace.steps[2]
        self.assertEqual(first.context["text"], {"block": 4, "entry": 2})
        self.assertEqual(switch.context["map"], 52)
        self.assertEqual(second.context["text"], {"block": 53, "entry": 4})
        self.assertEqual(trace.maps_visited, [3, 52])
        self.assertEqual(trace.final_map, 52)
        self.assertEqual(trace.problems, [])

    def test_text_entry_past_the_block_is_a_problem(self):
        trace = self.trace("03 0c 00", initial_map=3)
        self.assertEqual(len(trace.problems), 1)

    def test_opening_units_are_party_slots_then_wave_0_in_record_order(self):
        # map 3: party slot 0, then records 0 and 2 (wave 0); wave 1 adds 1 and 3
        trace = self.trace("0b 02 04 01 00 0b 04 00", initial_map=3)
        self.assertEqual(trace.steps[0].context["units"],
                         {2: {"map": 3, "record": 2, "char_id": 0x3d}})
        self.assertEqual(trace.steps[1].context["deployed"],
                         [{"map": 3, "record": 1, "char_id": 0x02},
                          {"map": 3, "record": 3, "char_id": 0x3e}])
        self.assertEqual(trace.steps[2].context["units"],
                         {4: {"map": 3, "record": 3, "char_id": 0x3e}})

    def test_party_slot_is_named_as_such(self):
        trace = self.trace("0b 00 00", initial_map=3)
        self.assertEqual(trace.steps[0].context["units"], {0: {"party_slot": 0}})

    def test_a_battle_array_keeps_its_fresh_load_prefix_but_not_its_tail(self):
        # map 3 loads as party 0, record 0, record 2; the battle may have added more
        trace = self.trace("02 00 02 01 00 05 00 11 34 02 00 01 05 00 00",
                           initial_map=3, units_known=False)
        self.assertEqual(trace.steps[0].context["units"],
                         {1: {"map": 3, "record": 0, "char_id": 0x3c}, 5: None})
        # after the switch map 52 has exactly 3 units, so u5 is out of bounds
        self.assertEqual(len(trace.findings), 1)
        self.assertIn("0x0009", trace.findings[0])

    def test_choice_wave_opens_the_tail(self):
        trace = self.trace("63 04 ff 00 02 00 01 05 00 00", initial_map=3)
        self.assertEqual(trace.steps[2].context["units"], {5: None})
        self.assertEqual(trace.findings, [])

    def test_face_past_the_unit_count_is_an_out_of_bounds_finding(self):
        trace = self.trace("02 00 01 03 00 00", initial_map=3)
        self.assertEqual(trace.steps[0].context["units"], {3: "out_of_range"})
        self.assertEqual(len(trace.findings), 1)
        self.assertEqual(trace.problems, [])

    def test_retire_past_the_unit_count_is_skipped_not_a_finding(self):
        trace = self.trace("0b 03 00", initial_map=3)
        self.assertEqual(trace.findings, [])
        self.assertEqual(len(trace.steps[0].context["notes"]), 1)

    def test_a_text_renderer_puts_the_words_next_to_the_reference(self):
        trace = self.trace("03 02 00", initial_map=3)
        line = cs.describe(trace.steps[0], lambda block, entry: f"b{block}e{entry}")
        self.assertEqual(line, "顯示文字 FDETXT04#0x02 「b4e2」")
        self.assertEqual(cs.describe(trace.steps[0]), "顯示文字 FDETXT04#0x02")

    def test_missing_saf_member_is_a_problem(self):
        self.assertEqual(self.trace("06 07 00", initial_map=3).problems, [])
        self.assertEqual(len(self.trace("06 08 00", initial_map=3).problems), 1)

    def test_switch_to_a_map_with_no_data_is_a_problem(self):
        self.assertTrue(self.trace("11 09 00", initial_map=3).problems)


@unittest.skipUnless((GAME_DIR / "ICONANI.VFS").is_file(), "fdps_game_files not present")
class ShippedScriptsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.reports = cs.decode_all(GAME_DIR, cs.scan_callers())

    def test_every_script_ends_on_its_last_byte_with_opcode_0(self):
        self.assertEqual(len(self.reports), 66)
        for report in self.reports:
            last = report.steps[-1]
            with self.subTest(script=report.member):
                self.assertEqual(last.opcode, 0)
                self.assertEqual(last.offset, report.size - 1)

    def test_every_script_traces_without_problems(self):
        for report in self.reports:
            with self.subTest(script=report.member):
                self.assertEqual(report.trace.problems, [])

    def test_the_only_out_of_bounds_unit_writes_are_the_five_known_ones(self):
        # Checked by hand against the step lists: each unit is faced before the
        # wave that brings it on (ICON00 at 0x454/0x48c, ICON11 at 0x8) or is
        # never deployed at all (ICON23's unit 38).
        found = {(r.member, f.split()[0]) for r in self.reports for f in r.trace.findings}
        self.assertEqual(found, {("ICON00.DAT", "0x0454"), ("ICON00.DAT", "0x048c"),
                                 ("ICON11.DAT", "0x0008"), ("ICON23.DAT", "0x0108"),
                                 ("ICON23.DAT", "0x010d")})

    def test_only_win24_trips_the_bounds_checked_opcodes(self):
        # WIN24 retires units 32..66 on map 22 (32 units) and places unit 22
        # on map 59 (2 units): 35 + 1 skipped instructions.
        skipped = [(r.member, t.step.mnemonic) for r in self.reports for t in r.trace.steps
                   if t.context.get("notes") and "skips" in t.context["notes"][0]]
        self.assertEqual(len(skipped), 36)
        self.assertEqual({m for m, _ in skipped}, {"WIN24.DAT"})

    @unittest.skipUnless((GAME_DIR.parent / "tools" / "text_decode" / "text_decode.py").is_file(),
                         "tools/text_decode not present")
    def test_text_decode_renderer_reads_the_referenced_entry(self):
        render = cs.text_decode_renderer(GAME_DIR)
        # WIN01's only line is FDETXT02 entry 0x0e, 尤利安 joining the party
        self.assertIn("尤利安", render(2, 0x0E))
        self.assertIsNone(render(2, 0x7F))

    def test_icon11_switches_to_map_52(self):
        report = next(r for r in self.reports if r.member == "ICON11.DAT")
        self.assertIn(52, report.trace.maps_visited)

    def test_goodend_starts_where_win29_leaves_off(self):
        by_name = {r.member: r for r in self.reports}
        self.assertEqual(by_name["GOODEND.DAT"].initial_map,
                         by_name["WIN29.DAT"].trace.final_map)


if __name__ == "__main__":
    unittest.main()
