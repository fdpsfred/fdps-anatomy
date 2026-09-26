"""Unit tests for kbverify.py and kbconsist.py.

    python -m unittest tools/kb_verify/test_kb_verify.py

The pages here are written by hand in a temporary repository, so every
expected unit, mask and edit decision comes from reading those few lines.
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kbverify  # noqa: E402
import kbconsist  # noqa: E402


class TempRepo:
    """Point kbverify at a scratch repository for the duration of a test."""

    def __init__(self, files):
        self.files = files

    def __enter__(self):
        self.dir = tempfile.TemporaryDirectory()
        root = Path(self.dir.name)
        for rel, text in self.files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        self.saved = (kbverify.REPO, kbverify.VERDICTS, kbverify.WORK, kbverify.APPLIED,
                      kbconsist.REPO)
        kbverify.REPO = root
        kbconsist.REPO = root
        kbverify.WORK = root / "workspace" / "kb_verify"
        kbverify.VERDICTS = kbverify.WORK / "verdicts"
        kbverify.APPLIED = kbverify.WORK / "applied.json"
        kbverify.units.cache_clear()
        return root

    def __exit__(self, *exc):
        (kbverify.REPO, kbverify.VERDICTS, kbverify.WORK, kbverify.APPLIED,
         kbconsist.REPO) = self.saved
        kbverify.units.cache_clear()
        self.dir.cleanup()


CHAPTER = ("# 第 1 章\n"
           "<!-- chapter_docs:header -->\n"
           "generated\n"
           "<!-- /chapter_docs:header -->\n"
           "\n"
           "## 概要\n"
           "\n"
           "蘭迪斯出門打獵。\n")


class MaskTest(unittest.TestCase):
    def test_generated_blocks_are_folded_and_prose_is_not(self):
        lines = CHAPTER.split("\n")
        ranges, gated = kbverify.masks(lines)
        self.assertEqual([(a, b) for a, b, _ in ranges], [(1, 3)])
        self.assertEqual(gated, {})

    def test_a_data_tables_table_is_folded_and_a_gated_hand_table_is_annotated(self):
        lines = ["| 代碼 | 名稱 | 文字條目 | 我方與客串單位 | 敵方與 NPC 單位 |",
                 "| --- | --- | --- | --- | --- |", "| 00 | 人類 | x | y | z |", "",
                 "| 代碼 | 職業 | 地形消耗 | 暴擊率 | 魔法抗性 |", "| --- | --- | --- | --- | --- |",
                 "| 00 | 劍士 | 1 | 2 | 3 |"]
        ranges, gated = kbverify.masks(lines)
        self.assertEqual([(a, b) for a, b, _ in ranges], [(0, 2)])
        self.assertEqual(list(gated), [4])


class UnitTest(unittest.TestCase):
    def test_a_long_page_is_cut_at_its_sections_and_short_pages_stay_whole(self):
        body = "x" * 9000
        page = "# P\n\n## A\n\n%s\n\n## B\n\n%s\n\n## C\n\nshort\n" % (body, body)
        with TempRepo({"program_info/p.md": page, "program_info/q.md": "# Q\n\n## A\n\nfine\n"}):
            got = [(u["id"], u["first"], u["last"]) for u in kbverify.units()]
        self.assertEqual(got, [("program_info__p__1", 0, 5), ("program_info__p__2", 6, 13),
                               ("program_info__q", 0, 5)])

    def test_a_frozen_cut_survives_an_edit_that_changes_a_units_size(self):
        body = "x" * 9000
        page = "# P\n\n## A\n\n%s\n\n## B\n\n%s\n\n## C\n\nshort\n" % (body, body)
        with TempRepo({"program_info/p.md": page}) as root:
            kbverify.freeze_units()
            before = [(u["id"], u["first"]) for u in kbverify.units()]
            (root / "program_info/p.md").write_text(page.replace("short", "s\n\nmore\n\nlines " * 400),
                                                    encoding="utf-8")
            kbverify.units.cache_clear()
            after = [(u["id"], u["first"]) for u in kbverify.units()]
        self.assertEqual(before, after)

    def test_generated_text_does_not_count_toward_a_unit(self):
        with TempRepo({"chapters/ch01.md": CHAPTER}):
            (u,) = kbverify.units()
        self.assertEqual(u["weight"], len("# 第 1 章") + 1 + 1 + len("## 概要") + 1 + 1
                         + len("蘭迪斯出門打獵。") + 1 + 1)


def verdict(uid, sha, findings, reread=None):
    v = {"unit": uid, "unit_sha1": sha, "claims_checked": 3, "findings": findings,
         "cross_doc": [], "pitfall_candidates": [], "summary": "三條查過，一條有誤。"}
    if reread is not None:
        v["_reread"] = reread
    return v


def finding(n, old, new, kind="wrong_fact"):
    return {"n": n, "kind": kind, "quote": old, "problem": "數字與資料不符，應為另一個值。",
            "evidence": [{"source": "src", "location": "src/a.c:10", "observation": "x = 4"}],
            "confidence": "high", "edit": {"old": old, "new": new}, "outside": ""}


class VerdictTest(unittest.TestCase):
    PAGE = "# P\n\n## A\n\n共有 3 個。\n\n## B\n\n共有 3 個。還有 5 個。\n"

    def test_an_edit_must_be_unique_in_the_file_and_inside_the_unit(self):
        with TempRepo({"program_info/p.md": self.PAGE}):
            (u,) = kbverify.units()
            lines = kbverify.read_lines(u["doc"])
            self.assertIn("occurs 2 times",
                          kbverify.check_edit({"old": "共有 3 個", "new": "共有 4 個"}, u, lines,
                                              set(), "e")[0])
            self.assertEqual(kbverify.check_edit({"old": "還有 5 個", "new": "還有 6 個"}, u, lines,
                                                 set(), "e"), [])
            narrow = dict(u, last=4)
            self.assertIn("outside this unit",
                          kbverify.check_edit({"old": "還有 5 個", "new": "還有 6 個"}, narrow,
                                              lines, set(), "e")[0])

    def test_an_edit_into_a_generated_block_is_refused(self):
        with TempRepo({"chapters/ch01.md": CHAPTER}):
            (u,) = kbverify.units()
            lines = kbverify.read_lines(u["doc"])
            hidden = kbverify.masked_set(kbverify.masks(lines)[0])
            self.assertIn("generated region",
                          kbverify.check_edit({"old": "generated", "new": "x"}, u, lines, hidden,
                                              "e")[0])

    def test_only_confirmed_or_amended_edits_land(self):
        with TempRepo({"program_info/p.md": self.PAGE}) as root:
            (u,) = kbverify.units()
            sha = kbverify.unit_sha1(u)
            v = verdict(u["id"], sha, [finding(1, "還有 5 個", "還有 6 個")])
            self.assertEqual(kbverify.check_verdict(v, u), [])
            self.assertEqual(kbverify.rescan_todo(), [])      # no file yet
            kbverify.VERDICTS.mkdir(parents=True)
            kbverify.verdict_path(u["id"]).write_text(json.dumps(v, ensure_ascii=False),
                                                      encoding="utf-8")
            self.assertEqual(kbverify.rescan_todo(), [{"id": u["id"], "findings": [1]}])
            applied, _ = kbverify.apply_edits()
            self.assertEqual(applied, [])                      # not re-read yet
            v["_reread"] = {"decisions": [{"n": 1, "decision": "amend", "why": "資料其實是 7 個，已查。",
                                           "edit": {"old": "還有 5 個", "new": "還有 7 個"}}]}
            self.assertEqual(kbverify.check_verdict(v, u), [])
            kbverify.verdict_path(u["id"]).write_text(json.dumps(v, ensure_ascii=False),
                                                      encoding="utf-8")
            applied, refused = kbverify.apply_edits()
            self.assertEqual((len(applied), refused), (1, []))
            self.assertIn("還有 7 個", (root / "program_info/p.md").read_text(encoding="utf-8"))

    def test_a_rejected_finding_does_not_land(self):
        v = verdict("u", "s", [finding(1, "a", "b")],
                    {"decisions": [{"n": 1, "decision": "reject", "why": "原文是對的，查過資料。"}]})
        self.assertEqual(kbverify.final_edits(v), [])

    def test_findings_that_need_first_hand_evidence_are_held_to_it(self):
        self.assertIn("no first-hand evidence", kbverify.check_evidence(
            [{"source": "kb", "location": "program_info/x.md", "observation": "says 3"}], True)[0])
        self.assertEqual(kbverify.check_evidence(
            [{"source": "tool", "location": "python tools/x/y.py", "observation": "4"}], True), [])


class ConsistTest(unittest.TestCase):
    def test_anchors_normalise_addresses_and_skip_small_numbers(self):
        self.assertEqual(kbconsist.anchors("`fdps_x`（`0x0003a2e0`）與 0x10、`0003a2e0`、MAP27.DAT"),
                         {"fdps_x", "0x3a2e0", "MAP27.DAT"})

    def test_the_same_prose_in_two_pages_is_one_group_but_shared_citations_are_not(self):
        same = "這一句話說明了一個很長的事實，名冊寫回時以角色編號比對，把戰場單位整筆寫回名冊記錄裡面去。"
        other = "完全不同的另一段敘述，講的是商店的庫存怎麼從檔案讀進來，以及秘密商店何時會出現在選單上。"
        files = {"program_info/a.md": "# A\n\n%s\n" % same,
                 "rebuild_info/b.md": "# B\n\n`fdps_x`（`0x3a2e0`）%s\n" % same,
                 "resource_info/c.md": "# C\n\n`fdps_x`（`0x3a2e0`）%s\n" % other}
        with TempRepo(files):
            groups = kbconsist.duplicate_groups()
        self.assertEqual([[g[0] for g in grp] for grp in groups],
                         [["program_info/a.md", "rebuild_info/b.md"]])

    def test_a_correction_propagates_to_other_pages_and_to_the_rest_of_its_own(self):
        files = {"program_info/a.md": "# A\n\n`fdps_x`（`0x3a2e0`）回傳 4。\n\n## B\n\n`fdps_x` 回傳 3。\n",
                 "rebuild_info/b.md": "# B\n\n`fdps_x` 回傳 3。\n\n無關的一行。\n"}
        applied = [{"doc": "program_info/a.md", "unit": "u", "n": 1, "kind": "wrong_fact",
                    "problem": "p", "old": "`fdps_x`（`0x3a2e0`）回傳 3。",
                    "new": "`fdps_x`（`0x3a2e0`）回傳 4。"}]
        with TempRepo(files):
            (item,) = kbconsist.propagation_items(applied, kbconsist.kb_lines())
        self.assertEqual([(h["doc"], h["line"]) for h in item["hits"]],
                         [("program_info/a.md", 7), ("rebuild_info/b.md", 3)])

    def test_two_verifiers_reporting_the_same_conflict_make_one_item(self):
        with TempRepo({"program_info/a.md": "# A\n\nx\n", "resource_info/b.md": "# B\n\ny\n"}):
            for uid, other in (("program_info__a", "resource_info/b.md"),
                               ("resource_info__b", "program_info/a.md")):
                kbverify.VERDICTS.mkdir(parents=True, exist_ok=True)
                kbverify.verdict_path(uid).write_text(json.dumps(
                    {"cross_doc": [{"other_doc": other, "anchor": "0x3a2e0", "note": "n"}]}),
                    encoding="utf-8")
            items = kbconsist.conflict_items()
        self.assertEqual([(i["docs"], len(i["notes"])) for i in items],
                         [(["program_info/a.md", "resource_info/b.md"], 2)])

    def test_a_refused_edit_whose_new_text_is_in_the_page_is_covered_the_rest_are_items(self):
        import kbrefused
        page = "# A\n\n共有 4 個，由 `fdps_x` 決定。\n\n另一句已被別人改寫。\n"
        with TempRepo({"program_info/a.md": page}) as root:
            consist = root / "workspace" / "kb_consist"
            (consist / "verdicts").mkdir(parents=True)
            (consist / "items.json").write_text(json.dumps([{"id": "X1", "kind": "conflict"}]),
                                                encoding="utf-8")
            edits = [{"doc": "program_info/a.md", "old": "共有 3 個", "new": "共有 4 個", "why": "w"},
                     {"doc": "program_info/a.md", "old": "另一句原文", "new": "另一句新文", "why": "w"}]
            (consist / "verdicts" / "X1.json").write_text(json.dumps(
                {"conclusion": "c", "edits": edits, "confidence": "high", "outside": "",
                 "verdict": "fixed", "_reread": {"decision": "confirm", "why": "checked"},
                 "_refused": [{"id": "X1", "doc": "program_info/a.md", "why": "old occurs 0 times now"}]}),
                encoding="utf-8")
            saved = (kbrefused.REPO, kbrefused.CONSIST_ITEMS, kbrefused.CONSIST_VERDICTS, kbrefused.TRIAGE)
            kbrefused.REPO, kbrefused.CONSIST_ITEMS = root, consist / "items.json"
            kbrefused.CONSIST_VERDICTS = consist / "verdicts"
            kbrefused.TRIAGE = root / "workspace" / "kb_refused" / "triage.json"
            try:
                items, covered = kbrefused.triage()
            finally:
                (kbrefused.REPO, kbrefused.CONSIST_ITEMS, kbrefused.CONSIST_VERDICTS,
                 kbrefused.TRIAGE) = saved
        self.assertEqual([c["new"] for c in covered], ["共有 4 個"])
        self.assertEqual([(i["id"], i["new"]) for i in items], [("F1", "另一句新文")])

    def test_chapter_pages_restating_their_own_flow_are_not_duplicates(self):
        same = "把戰場上的隊員寫回名冊，掉落物隨擊殺者的背包進名冊，這一步在勝利處理的最後面才執行完畢。"
        files = {"chapters/ch12.md": "# 12\n\n%s\n" % same, "chapters/ch13.md": "# 13\n\n%s\n" % same}
        with TempRepo(files):
            self.assertEqual(kbconsist.duplicate_groups(), [])


class OutsideTest(unittest.TestCase):
    SRC = "int f(void)\n{\n    /* the old comment */\n    return 1;\n}\n"

    def test_a_comment_or_a_rename_is_allowed_in_c_source_a_code_change_is_not(self):
        import kboutside
        self.assertIsNone(kboutside.code_change(self.SRC, self.SRC.replace("old comment", "new one")))
        self.assertIsNone(kboutside.code_change(self.SRC, self.SRC.replace("f(void)", "g(void)")))
        self.assertIsNotNone(kboutside.code_change(self.SRC, self.SRC.replace("return 1", "return 2")))

    def test_fixes_naming_a_common_source_file_are_one_group(self):
        import kboutside
        fixes = [("V:a#1", "src/text.c 的註解要改", "", "program_info/dialog.md"),
                 ("C:X1", "src/text.c 與 tests/text.c 的註解", "", None),
                 ("C:X2", "tests/text.c:936 同樣", "", None),
                 ("V:b#2", "rebuild_info/pitfalls.md 那一列", "", "program_info/cd_audio.md"),
                 ("V:c#3", "同頁另一段", "", "program_info/code_pools.md")]
        groups, _ = kboutside.group(fixes)
        self.assertEqual(sorted(groups), [["C:X1", "C:X2", "V:a#1"], ["V:b#2"], ["V:c#3"]])

    def test_edits_to_decision_records_and_generated_pages_are_refused(self):
        import kboutside
        files = {"docs/adr/0004-x.md": "old\n", "assets/text/global_text.md": "old\n",
                 "src/a.c": self.SRC}
        with TempRepo(files):
            saved = kboutside.REPO
            kboutside.REPO = kbverify.REPO
            try:
                problems, texts = kboutside.check_edits([
                    {"file": "docs/adr/0004-x.md", "old": "old", "new": "new", "why": "w"},
                    {"file": "assets/text/global_text.md", "old": "old", "new": "new", "why": "w"},
                    {"file": "src/a.c", "old": "return 1", "new": "return 2", "why": "w"},
                    {"file": "src/a.c", "old": "the old comment", "new": "a better one", "why": "w"}])
            finally:
                kboutside.REPO = saved
        self.assertEqual(len(problems), 3)
        self.assertIn("a better one", texts["src/a.c"])


if __name__ == "__main__":
    unittest.main()
