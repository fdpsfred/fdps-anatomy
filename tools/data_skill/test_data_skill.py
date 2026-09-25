"""Tests for the fdps-data query skill at its two seams: the data set build.py
produces (build.build), and the query commands (query.main) run against it.

Expected values come from sources independent of the code under test: the
knowledge-base tables as a person reads them (assets/enemies.md,
assets/characters.md, assets/shops.md, cut_content/story.md) and the chapter
judgements' own wording.  The build reads the shipped game files; without
them the tests are skipped.

    python -m unittest tools/data_skill/test_data_skill.py
"""
import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / ".claude" / "skills" / "fdps-data"))

import build  # noqa: E402
import query  # noqa: E402

GAME = ROOT / "fdps_game_files"
HAVE_GAME = (GAME / "MISC.VFS").is_file() and (GAME / "FIELD.VFS").is_file()

_BUILT = {}


def built():
    """(data, text, out dir): one build per test run, written to a scratch dir."""
    if not _BUILT:
        out = Path(tempfile.mkdtemp(prefix="data_skill_test_"))
        data, text = build.build(GAME, check_chapter_pages=False)
        build.write(out, data, text)
        _BUILT.update(data=data, text=text, out=out)
    return _BUILT["data"], _BUILT["text"], _BUILT["out"]


def tearDownModule():
    if _BUILT:
        shutil.rmtree(_BUILT["out"], ignore_errors=True)


def record(table, code):
    data, _, _ = built()
    return next(r for r in data["tables"][table]["records"] if r["code"] == code)


def run(*argv):
    """query.main's printed output for argv, against the scratch build."""
    _, _, out = built()
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        query.main(list(argv), data_dir=out)
    return buf.getvalue()


@unittest.skipUnless(HAVE_GAME, "fdps_game_files/ not present")
class Enemies(unittest.TestCase):
    def test_enemy_row_carries_names_numbers_and_where_it_is_deployed(self):
        # assets/enemies.md row 0: `3C` 平衡之神, `06` 其他, `1A` 魔神, HP 150, EXP 255, chapter 30
        e = record("enemy", 0x3C)
        self.assertEqual((e["row"], e["name"], e["race_name"], e["class_name"]),
                         (0, "平衡之神", "其他", "魔神"))
        self.assertEqual((e["hp"], e["ap"], e["exp"]), (150, 8, 255))
        self.assertEqual(e["deployed_chapters"], [30])

    def test_never_deployed_template_row_is_flagged(self):
        # assets/enemies.md row 50: `6E` 寶箱怪, 樣板列, 未部署
        e = record("enemy", 0x6E)
        self.assertEqual((e["name"], e["template"], e["deployed"]), ("寶箱怪", True, False))


@unittest.skipUnless(HAVE_GAME, "fdps_game_files/ not present")
class Characters(unittest.TestCase):
    def test_non_party_character_read_by_deployments_has_appearance_values(self):
        # assets/characters.md, 非隊員角色: `0C` 索爾 職業 03、LV 10、HP 960、MP 480、
        # MV 6、AP 300、DP 100、DX 160、配備 62／78
        c = record("character", 0x0C)
        self.assertTrue(c["appearance_documented"])
        self.assertEqual((c["name"], c["class_code"], c["level"], c["hp_base"], c["mp_base"],
                          c["move"], c["ap_base"], c["dp_base"], c["dx_base"]),
                         ("索爾", 0x03, 10, 960, 480, 6, 300, 100, 160))
        self.assertEqual([i["code"] for i in c["initial_items"]], [0x62, 0x78])

    def test_every_index_the_game_reads_has_appearance_and_no_other(self):
        # assets/characters.md: the readers are 00-0B (roster) and the deployed
        # 0C, 0D, 0E, 23, 24-27, 3B; 22 and the promoted forms are read by nothing
        data, _, _ = built()
        documented = [r["code"] for r in data["tables"]["character"]["records"]
                      if r["appearance_documented"]]
        self.assertEqual(documented, list(range(0x0F)) + [0x23, 0x24, 0x25, 0x26, 0x27, 0x3B])

    def test_guard_and_crowd_rows(self):
        # 3B 侍衛: 職業 00、LV 16、HP 60、MV 4、配備 3F／99; 24: 職業 06、LV 1、HP 1、MV 1
        guard, crowd = record("character", 0x3B), record("character", 0x24)
        self.assertEqual((guard["name"], guard["class_code"], guard["level"], guard["hp_base"],
                          guard["move"]), ("侍衛", 0x00, 16, 60, 4))
        self.assertEqual([i["code"] for i in guard["initial_items"]], [0x3F, 0x99])
        self.assertEqual((crowd["name"], crowd["class_code"], crowd["level"], crowd["hp_base"],
                          crowd["move"], crowd["blank"]), (None, 0x06, 1, 1, 1, False))

    def test_promotion_routes_ride_on_the_character(self):
        # assets/characters.md 轉職路線: 蘭迪斯 無徽章 `0F` 劍聖 MV+1 ... 勇者徽章 `21` 英雄 MV+2
        routes = record("character", 0x00)["promotions"]
        self.assertEqual([(r["route_name"], r["form"], r["class_name"], r["move_bonus"])
                          for r in routes],
                         [("無徽章", 0x0F, "劍聖", 1), ("光之徽章", 0x0F, "劍聖", 1),
                          ("暗之徽章", 0x18, "劍帝", 1), ("勇者徽章", 0x21, "英雄", 2)])
        # only 蘭迪斯 is offered the 勇者徽章 route (src/church.c)
        self.assertNotIn("勇者徽章", [r["route_name"] for r in record("character", 0x04)["promotions"]])


@unittest.skipUnless(HAVE_GAME, "fdps_game_files/ not present")
class Chapters(unittest.TestCase):
    def test_chapter_header_comes_from_the_chapter_text_block(self):
        c = record("chapter", 1)
        self.assertEqual((c["name"], c["win"], c["index"], c["map"]), ("英雄之出發", "敵人全滅", 0, 0))

    def test_treasure_and_drops(self):
        # chapters/ch03.md: code 2 at (11, 2) holds `D8` 力量藥水, code 0 1000 金;
        # record #10 `47` 石巨神 of wave 5 drops 800 金
        c = record("chapter", 3)
        chest = next(t for t in c["treasure"] if t["code"] == 2)
        self.assertEqual((chest["item"], chest["item_name"], chest["cells"][0]["x"],
                          chest["cells"][0]["y"]), (0xD8, "力量藥水", 11, 2))
        self.assertEqual(next(t for t in c["treasure"] if t["code"] == 0)["gold"], 1000)
        drop = next(d for d in c["drops"] if d["index"] == 10)
        self.assertEqual((drop["name"], drop["wave"], drop["gold"]), ("石巨神", 5, 800))

    def test_wave_the_turn_halving_never_reaches_is_not_deployed(self):
        # tools/chapter_docs/judgements/ch28.json: wave 4 is never deployed, wave 3 twice
        waves = {w["wave"]: w for w in record("chapter", 28)["waves"]}
        self.assertFalse(waves[4]["deployed"])
        self.assertIn("回合 ÷ 2", waves[4]["why"])
        self.assertTrue(waves[3]["deployed"])
        undeployed = [d for d in record("chapter", 28)["deployments"] if d["wave"] == 4]
        self.assertTrue(undeployed and not any(d["deployed"] for d in undeployed))


def entry(block, index):
    _, text, _ = built()
    return text["blocks"][block]["entries"][index]


@unittest.skipUnless(HAVE_GAME, "fdps_game_files/ not present")
class Text(unittest.TestCase):
    def test_every_block_is_there(self):
        _, text, _ = built()
        self.assertEqual([b["name"] for b in text["blocks"]],
                         ["FDETXT%02d.TXT" % n for n in range(66)])

    def test_never_shown_text_names_its_cut_content_owner(self):
        # cut_content/_index.md: S14 is 全域文字第 0 條的字模列; story.md S1 owns
        # map 49's text FDETXT50 0x09..
        glyph_row = entry(0, 0x000)
        self.assertEqual((glyph_row["status"], glyph_row["owner"]["id"],
                          glyph_row["owner"]["page"]), ("never_shown", "S14",
                                                        "cut_content/story.md"))
        self.assertEqual(entry(50, 0x09)["owner"]["id"], "S1")

    def test_shown_chapter_entry_lists_its_readers(self):
        # chapters/ch01.md: 0x02 (the win condition) is read by the win/fail window
        e = entry(1, 0x02)
        self.assertEqual(e["status"], "shown")
        self.assertTrue(any("fdps_battle_show_win_fail_window" in r for r in e["readers"]))

    def test_global_text_entry_knows_its_region(self):
        # assets/text/global_text.md: 0x0c9 is the first item name, 岩石
        e = entry(0, 0x0C9)
        self.assertEqual((e["region"], e["lines"], e["status"]), ("物品名", ["岩石"], "shown"))


@unittest.skipUnless(HAVE_GAME, "fdps_game_files/ not present")
class QueryCommands(unittest.TestCase):
    def test_new_tables_answer_show_and_name(self):
        self.assertIn("平衡之神", run("show", "enemy", "3C"))
        self.assertIn("enemy     6E   寶箱怪", run("name", "寶箱怪"))
        self.assertIn("藥草", run("show", "shop", "1"))           # SHOP01.DAT, 道具店
        self.assertIn("高能量砲", run("show", "use_effect", "23"))  # 布蘭多's 金屬礦 exchange

    def test_chapter_treasure_section(self):
        out = run("chapter", "3", "--treasure")
        self.assertIn("(11, 2)", out)
        self.assertIn("力量藥水", out)
        self.assertNotIn("turn event", out)

    def test_chapter_enemies_mark_the_wave_never_deployed(self):
        out = run("chapter", "28", "--enemies")
        wave4 = out[out.index("wave 4"):]
        self.assertIn("never deployed", wave4.splitlines()[0] + wave4.splitlines()[1])

    def test_full_text_search_crosses_line_breaks(self):
        # FDETXT01 0x03, the loss condition: 蘭迪斯死亡{br}索爾死亡
        out = run("text", "蘭迪斯死亡索爾死亡", "--block", "1")
        self.assertIn("FDETXT01 0x03", out)

    def test_entry_explains_why_text_is_never_shown(self):
        out = run("entry", "0", "0")
        self.assertIn("never_shown", out)
        self.assertIn("S14", out)

    def test_deploy_lists_every_chapter_record_of_a_unit(self):
        out = run("deploy", "3B")                   # 侍衛, chapter 26 only
        self.assertIn("ch26", out)
        self.assertNotIn("ch25", out)


if __name__ == "__main__":
    unittest.main()
