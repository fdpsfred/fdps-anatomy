"""Tests for the growth tables at their public seams: the rules in
gen_growth (gain range, entry figures, promotion rows), the model built from
the game data, the independent replay of src/ (src_replay), and the page's
JavaScript against the Python output (verify_js).

Expected values come from sources independent of the code under test:
hand-built records, and the strategy guide's own printed figures in
docs/guide/fdps/list.txt -- its first-appearance rows, and its 40(m) / 99(m)
rows, which are every level-up rolling its largest gain with the promotion
taken at LV40.  The tests that read game data skip when tools/vfs_dump has not
been run; the JavaScript check skips without Chrome or Edge.

    python -m unittest tools/growth_table/test_growth.py
"""
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "data_tables"))
import data_tables  # noqa: E402
import gen_growth  # noqa: E402

ROOT = HERE.parents[1]
DUMP = ROOT / "workspace" / "vfs_dump"
HAVE_DUMP = (DUMP / "MISC" / "FRILEVUP.DAT").is_file() and (DUMP / "FIELD" / "FDETXT00.TXT").is_file()


def five(ap, dp, dx, hp, mp):
    """The guide prints AP DP DX HP MP; the model keys them by name."""
    return {"ap": ap, "dp": dp, "dx": dx, "hp": hp, "mp": mp}


class Rules(unittest.TestCase):
    def test_differing_pair_gains_up_to_one_below_the_upper_byte(self):
        self.assertEqual(gen_growth.gain_range((4, 6)), (4, 5))

    def test_equal_pair_gains_exactly_its_value(self):
        self.assertEqual(gen_growth.gain_range((2, 2)), (2, 2))
        self.assertEqual(gen_growth.gain_range((0, 0)), (0, 0))

    def test_inverted_pair_is_refused(self):
        with self.assertRaises(gen_growth.GrowthError):
            gen_growth.gain_range((5, 3))

    def test_entry_scales_ap_dp_dx_by_level_and_hp_mp_by_level_minus_one(self):
        base = five(20, 10, 3, 42, 40)
        growth = {"ap": (2, 4), "dp": (2, 4), "dx": (2, 3), "hp": (6, 9), "mp": (7, 10)}
        self.assertEqual(gen_growth.entry_stats(base, growth, 8),
                         five(20 + 16, 10 + 16, 3 + 16, 42 + 42, 40 + 49))

    def test_promotion_adds_the_raw_upper_byte_then_rolls_from_level_two(self):
        carry = {"min": five(100, 100, 100, 100, 100), "max": five(200, 200, 200, 200, 200)}
        growth = {"ap": (6, 9), "dp": (1, 1), "dx": (0, 0), "hp": (10, 14), "mp": (3, 4)}
        rows = gen_growth.promo_rows(carry, growth)
        self.assertEqual([r["lv"] for r in rows], list(range(1, 41)))
        self.assertEqual(rows[0]["min"], five(109, 101, 100, 114, 104))
        self.assertEqual(rows[0]["max"], five(209, 201, 200, 214, 204))
        self.assertEqual(rows[1]["min"], five(115, 102, 100, 124, 107))
        self.assertEqual(rows[1]["max"], five(217, 202, 200, 227, 207))

    def test_level_cap_is_99_for_the_machine_soldier_only(self):
        self.assertEqual(gen_growth.level_cap(9), 99)
        self.assertEqual(gen_growth.level_cap(0), 40)
        self.assertEqual(gen_growth.level_cap(0x0B), 40)

    def test_promotion_levels_start_at_20_or_the_join_level(self):
        self.assertEqual(gen_growth.promote_levels(1), list(range(20, 41)))
        self.assertEqual(gen_growth.promote_levels(20), list(range(20, 41)))


@unittest.skipUnless(HAVE_DUMP, "needs workspace/vfs_dump (tools/vfs_dump)")
class GameData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.game = data_tables.load(DUMP)
        cls.model = gen_growth.model(cls.game)
        cls.full = gen_growth.expand(cls.model)
        cls.by_name = {c["name"]: c for c in cls.model["chars"]}
        cls.full_by_id = {c["id"]: c for c in cls.full["chars"]}

    def rows(self, name, join, cls_name=None, promote_at=40):
        c = self.by_name[name]
        j = next(j for j in self.full_by_id[c["id"]]["joins"] if j["level"] == join)
        if cls_name is None:
            return j["base_rows"]
        k = next(i for i, r in enumerate(c["routes"]) if r["cls"] == cls_name)
        return j["routes"][k]["by_promote_level"][str(promote_at)]

    def at(self, rows, lv):
        return next(r for r in rows if r["lv"] == lv)

    def test_first_appearance_matches_the_guide(self):
        # list.txt 「各人物職業滿級屬性」: the first row of each character.
        cases = [("蘭迪斯", 1, five(18, 7, 5, 56, 0)), ("法蓮娜", 8, five(36, 26, 19, 84, 89)),
                 ("費塔加", 15, five(53, 36, 38, 162, 163)), ("瑪麗安", 20, five(150, 80, 70, 250, 38)),
                 ("蘭斯洛特", 2, five(240, 160, 66, 434, 0)), ("珊", 15, five(170, 190, 115, 420, 400)),
                 ("蓋亞", 16, five(112, 80, 0, 240, 45))]
        for name, join, want in cases:
            with self.subTest(name):
                first = self.rows(name, join)[0]
                self.assertEqual((first["lv"], first["min"], first["max"]), (join, want, want))

    def test_base_form_ceiling_at_the_cap_matches_the_guide(self):
        # list.txt 40(m) / 99(m) rows.
        cases = [("蘭迪斯", 1, 40, five(213, 163, 83, 446, 117)),
                 ("裘娜", 15, 40, five(355, 185, 40, 600, 103)),
                 ("蘭斯洛特", 2, 40, five(810, 730, 180, 1004, 0)),
                 ("珊", 15, 40, five(345, 365, 215, 745, 725)),
                 ("蓋亞", 16, 40, five(304, 224, 0, 576, 141)),
                 ("蓋亞", 16, 99, five(776, 578, 0, 1402, 377))]
        for name, join, lv, want in cases:
            with self.subTest(f"{name} LV{lv}"):
                self.assertEqual(self.at(self.rows(name, join), lv)["max"], want)

    def test_base_form_floor_is_twice_the_guide_average_less_its_ceiling(self):
        # list.txt: 蘭迪斯 劍士 40 = 193.5 143.5 83 426.5 117, 40(m) as above.
        self.assertEqual(self.at(self.rows("蘭迪斯", 1), 40)["min"], five(174, 124, 83, 407, 117))

    def test_promoted_ceiling_at_40_matches_the_guide(self):
        cases = [("蘭迪斯", 1, "英雄", five(894, 644, 204, 1207, 798)),
                 ("蘭迪斯", 1, "劍聖", five(534, 684, 164, 1207, 478)),
                 ("裘娜", 15, "狂戰士", five(1196, 546, 120, 1521, 224)),
                 ("費塔加", 15, "法師", five(539, 682, 194, 858, 1029)),
                 ("瑪麗安", 20, "神箭手", five(751, 521, 250, 1111, 299)),
                 ("琴琴", 15, "武神", five(1071, 562, 241, 1139, 236)),
                 ("布蘭多", 14, "機械大師", five(781, 435, 240, 988, 584))]
        for name, join, cls_name, want in cases:
            with self.subTest(f"{name} {cls_name}"):
                self.assertEqual(self.at(self.rows(name, join, cls_name), 40)["max"], want)

    def test_routes_and_badges_match_the_guide_advice(self):
        # list.txt 「轉職建議」: 費塔加：法師（需光之徽章）、巫師 ...
        def summary(name):
            return {r["cls"]: r["badge"] for r in self.by_name[name]["routes"]}
        self.assertEqual(summary("蘭迪斯"), {"劍聖": None, "劍帝": "暗之徽章", "英雄": "勇者徽章"})
        self.assertEqual(summary("費塔加"), {"巫師": None, "法師": "光之徽章"})
        self.assertEqual(summary("瑪麗安"), {"狙擊王": None, "神箭手": "光之徽章"})
        for name in ("蓋亞", "珊", "蘭斯洛特"):
            self.assertEqual(self.by_name[name]["routes"], [])

    def test_uncertain_arrivals_give_two_join_levels(self):
        self.assertEqual([j["level"] for j in self.by_name["蘭斯洛特"]["joins"]], [2, 15])
        self.assertEqual([j["level"] for j in self.by_name["珊"]["joins"]], [15, 10])
        self.assertEqual([j["level"] for j in self.by_name["法蓮娜"]["joins"]], [8])
        self.assertEqual([j["how"] for j in self.by_name["蘭斯洛特"]["joins"]], ["arrives", "early"])
        self.assertEqual([j["how"] for j in self.by_name["瑪麗安"]["joins"]], ["stays"])
        self.assertEqual([j["how"] for j in self.by_name["蘭迪斯"]["joins"]], ["roster"])

    def test_promotion_at_20_carries_the_level_20_row(self):
        base20 = self.at(self.rows("蘭迪斯", 1), 20)
        promo = self.rows("蘭迪斯", 1, "英雄", promote_at=20)[0]
        hero = self.by_name["蘭迪斯"]["routes"][2]["growth"]
        for v in ("min", "max"):
            self.assertEqual(promo[v], {s: base20[v][s] + hero[s][1] for s in gen_growth.STATS})

    def test_src_replay_agrees_on_every_value(self):
        import src_replay
        report = src_replay.compare(self.game, self.model)
        self.assertEqual(report["mismatches"], [], "\n".join(report["mismatches"][:20]))
        self.assertGreater(report["values"], 100000)


@unittest.skipUnless(HAVE_DUMP, "needs workspace/vfs_dump (tools/vfs_dump)")
class Page(unittest.TestCase):
    def test_published_page_is_what_a_build_writes(self):
        import build_page
        self.assertEqual(build_page.main(["--check"]), 0)

    def test_page_javascript_equals_python_and_every_control_runs(self):
        import verify_js
        if verify_js.find_browser() is None:
            self.skipTest("needs Chrome or Edge")
        import build_page
        # Only the workspace preview: the published copy is the other test's
        # to judge, never this one's to rewrite.
        build_page.build_preview(DUMP)
        self.assertEqual(verify_js.main([]), 0)


if __name__ == "__main__":
    unittest.main()
