"""Tests for the chapter-page generator and gate (ticket 25.8).

Run: python -m unittest tools/chapter_docs/test_chapter_docs.py

Everything here reads the shipped data (workspace/vfs_dump and
fdps_game_files); without it the tests are skipped.
"""
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import chapter_facts as facts  # noqa: E402
import check_chapter as gate  # noqa: E402

HAVE_DATA = (facts.DUMP / "FIELD" / "MAP02.DAT").is_file() and (facts.GAME / "ICONANI.VFS").is_file()

PROSE = {
    "概要": "蘭迪斯一行人來到石巨神的遺跡。",
    "加入與離隊": "本章沒有人入隊，見 [人物加入](../assets/characters.md#加入)。",
    "勝敗條件與特殊機制": "第 22 回合由 `fdps_chapter_03_event_turn_limit_game_over`（`0x36f10`）判定敗北。",
    "敵人配置": "石巨神分批出現。",
    "處理流程": ("進入：`fdps_chapter_03_init`（`0x20f30`）。行動後：`fdps_chapter_03_post_action`"
                 "（`0x3a4b0`）。勝利：`fdps_chapter_03_end`（`0x3a520`）。"),
}


def draft_text(n, prose=PROSE):
    out = []
    for line in gate.template(n).splitlines():
        out.append(line)
        if line.startswith("## ") and line[3:] in prose:
            out += ["", prose[line[3:]]]
    return "\n".join(out) + "\n"


def full_judgement(n):
    m = facts.battle_map(n - 1)
    waves = [{"wave": w, "deployed": True, "when": "測試", "by": ""}
             for w in facts.wave_list(m) if w != facts.OPENING_WAVE]
    readers = facts.text_readers(n)
    missing = [i for i in range(len(facts.text_block(n)))
               if i not in readers and facts.entry_line(n, i)]
    return {"waves": waves, "text_readers": [],
            "never_shown": [{"entry": f"0x{i:02x}", "why": "測試"} for i in missing]}


@unittest.skipUnless(HAVE_DATA, "shipped data not present")
class Generator(unittest.TestCase):
    def test_handler_tables_are_the_chapter_c_initializers(self):
        t = facts.handler_tables()
        self.assertEqual(t["init"][0], "fdps_chapter_01_init")
        self.assertEqual(t["end"][29], "fdps_chapter_30_end")
        self.assertEqual(t["event"][49], "fdps_chapter_30_event_deploy_wave_3")

    def test_village_before_follows_the_shop_and_text_numbering(self):
        self.assertIsNone(facts.village_before(1))          # a new game goes straight in
        self.assertEqual(facts.village_before(3), (2, 3))    # SHOP02 + FDETXT03
        self.assertIsNone(facts.village_before(18))          # index 17 has no village
        self.assertIsNone(facts.village_before(27))          # 26 and later have none

    def test_script_and_header_readers_are_found(self):
        r = facts.text_readers(3)
        self.assertIn("`ICON02.DAT` `0x01e`", r[0x09])
        self.assertTrue(any("fdps_battle_show_win_fail_window" in x for x in r[0x02]))
        self.assertTrue(any("fdps_draw_save_slot_panel" in x for x in r[0x01]))
        self.assertTrue(any("fdps_run_weapon_shop" in x for x in r[0x05]))

    def test_the_three_way_prompt_is_not_a_reader_of_every_chapter(self):
        for entry in facts.text_readers(13).values():
            self.assertFalse(any("prompt_three_way" in x for x in entry))

    def test_village_readers_only_where_a_village_comes_first(self):
        self.assertFalse(any("fdps_run_weapon_shop" in x
                             for v in facts.text_readers(1).values() for x in v))

    def test_transcript_marks_speakers_breaks_and_pages(self):
        lines = facts.transcript(3, 0x09)
        self.assertEqual(lines[0], "【蘭迪斯】（角色 0）")
        self.assertIn("【尤利安】（角色 6）", lines)
        self.assertIn("▼", facts.transcript(3, 0x05))

    def test_a_wave_judged_not_deployed_is_one_line_to_cut_content(self):
        j = full_judgement(3)
        j["waves"][0] = {"wave": j["waves"][0]["wave"], "deployed": False, "why": "沒有呼叫端"}
        block = facts.block_deployments(3, j)
        self.assertIn(f"永遠不會部署：沒有呼叫端，見 [刪減與未用]({facts.CUT})", block)

    def test_a_never_shown_entry_has_no_text(self):
        block = facts.block_dialogue(3, full_judgement(3))
        section = block.split("### `0x00`")[1].split("###")[0]
        self.assertIn(facts.NEVER_SHOWN_TEXT, section)
        self.assertNotIn("第三章", section)

    def test_scene_text_is_inlined_but_own_block_is_referenced(self):
        block = facts.block_scripts(10, None)
        self.assertIn("FDETXT38#0x09 「", block)
        own = facts.block_scripts(3, None)
        self.assertIn("FDETXT03#0x09 |", own)


@unittest.skipUnless(HAVE_DATA, "shipped data not present")
class Judgement(unittest.TestCase):
    def test_a_complete_judgement_passes(self):
        self.assertEqual(facts.validate_judgement(3, full_judgement(3)), [])

    def test_every_wave_must_be_judged(self):
        j = full_judgement(3)
        j["waves"] = j["waves"][1:]
        self.assertTrue(any("wave 1 has records" in p for p in facts.validate_judgement(3, j)))

    def test_wave_zero_and_unknown_waves_are_refused(self):
        j = full_judgement(3)
        j["waves"].append({"wave": 0, "deployed": True, "when": "x"})
        self.assertTrue(any("wave 0" in p for p in facts.validate_judgement(3, j)))

    def test_every_unread_entry_must_be_judged(self):
        j = full_judgement(3)
        j["never_shown"] = []
        self.assertTrue(any("0x00" in p for p in facts.validate_judgement(3, j)))

    def test_an_entry_with_a_scanned_reader_cannot_be_judged(self):
        j = full_judgement(3)
        j["never_shown"].append({"entry": "0x09", "why": "x"})
        self.assertTrue(any("0x09" in p for p in facts.validate_judgement(3, j)))


@unittest.skipUnless(HAVE_DATA, "shipped data not present")
class Gate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.names, cls.idents = gate.mech.load_names(), gate.mech.load_idents()

    def codes(self, text, judgement):
        filled = gate.fill(text, 3, judgement)
        return {f[1] for f in gate.check_page_text(filled, 3, judgement, self.names,
                                                   self.idents, draft=True)
                if f[0] == "error"}

    def test_a_complete_draft_is_clean(self):
        self.assertEqual(self.codes(draft_text(3), full_judgement(3)), set())

    def test_fill_is_idempotent(self):
        once = gate.fill(draft_text(3), 3, full_judgement(3))
        self.assertEqual(gate.fill(once, 3, full_judgement(3)), once)

    def test_missing_prose_is_an_error(self):
        prose = dict(PROSE)
        del prose["概要"]
        self.assertIn("empty-section", self.codes(draft_text(3, prose), full_judgement(3)))

    def test_the_flow_section_must_cite_all_three_handlers(self):
        prose = dict(PROSE, 處理流程="進入：`fdps_chapter_03_init`（`0x20f30`）。")
        self.assertIn("missing-handler", self.codes(draft_text(3, prose), full_judgement(3)))

    def test_sections_out_of_order_are_a_structure_error(self):
        text = draft_text(3).replace("## 概要", "## 摘要")
        self.assertIn("structure", self.codes(text, full_judgement(3)))

    def test_a_marker_moved_to_another_section_is_a_structure_error(self):
        text = draft_text(3).replace("<!-- chapter_docs:treasure -->\n", "")
        text = text.replace("## 村莊\n", "## 村莊\n\n<!-- chapter_docs:treasure -->\n")
        self.assertIn("structure", self.codes(text, full_judgement(3)))

    def test_an_undecided_judgement_is_an_error(self):
        self.assertIn("undecided", self.codes(draft_text(3), {"waves": [], "text_readers": [],
                                                                "never_shown": []}))

    def test_a_wrong_address_is_an_error(self):
        prose = dict(PROSE, 概要="由 `fdps_chapter_03_init`（`0x20f31`）開始。")
        self.assertIn("citation-mismatch", self.codes(draft_text(3, prose), full_judgement(3)))

    def test_narrative_words_count_in_prose_but_not_in_game_text(self):
        lines = ["## 概要", "起初以為如此。", "<!-- chapter_docs:dialogue -->", "起初他說。",
                 "<!-- /chapter_docs:dialogue -->"]
        found = [f[2] for f in gate.check_symbols(lines, {}, set()) if f[1] == "narrative"]
        self.assertEqual(found, [2])

    def test_regions_detect_a_hand_edited_block(self):
        filled = gate.fill(draft_text(3), 3, full_judgement(3))
        edited = filled.replace("| 3／2 |", "| 3／9 |")
        fresh = gate.regions(gate.fill(edited, 3, full_judgement(3)))
        self.assertNotEqual(gate.regions(edited)["header"], fresh["header"])


if __name__ == "__main__":
    unittest.main()
