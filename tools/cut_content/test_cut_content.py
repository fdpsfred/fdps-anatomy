"""Unit tests for cut_content.py (python -m unittest tools/cut_content/test_cut_content.py)."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cut_content  # noqa: E402

CODE_PAGE = """# 刪減與未用：程式痕跡

## 殘留內容

### C7 判負處理函式沒有地圖引用

分類：殘留內容｜`src/chevt1.c`

body text

### C2 AI 行為模式 9 沒有來源

分類：殘留內容

body

## 否定性結論

### C13 沒有除錯鍵

分類：否定性結論

body
"""

INDEX_PAGE = """# 刪減與未用

## 總表

| 編號 | 分類 | 內容 | 主題檔 |
| --- | --- | --- | --- |
| C1 | 殘留內容 | stale | [程式痕跡](code.md) |

## 排除清單

### 程式痕跡

| 編號 | 內容 | 理由 |
| --- | --- | --- |
| C12 | `fdps_spell_heal_unit` | 編譯器產物 |
"""


class ParseTopicTest(unittest.TestCase):
    def test_entries_come_out_with_id_title_and_category(self):
        entries, problems = cut_content.parse_topic(CODE_PAGE, "code")
        self.assertEqual(problems, [])
        self.assertEqual([(e.id, e.title, e.category) for e in entries], [
            ("C7", "判負處理函式沒有地圖引用", "殘留內容"),
            ("C2", "AI 行為模式 9 沒有來源", "殘留內容"),
            ("C13", "沒有除錯鍵", "否定性結論"),
        ])

    def test_an_entry_without_a_category_line_is_a_problem(self):
        _, problems = cut_content.parse_topic("### C3 x\n\nno category here\n", "code")
        self.assertEqual(len(problems), 1)
        self.assertIn("C3", problems[0])

    def test_an_unknown_category_is_a_problem(self):
        _, problems = cut_content.parse_topic("### C3 x\n\n分類：死資料\n", "code")
        self.assertIn("死資料", problems[0])

    def test_excluded_is_not_a_topic_category(self):
        # Exclusions live only in the _index.md exclusion list.
        _, problems = cut_content.parse_topic("### C3 x\n\n分類：排除\n", "code")
        self.assertEqual(len(problems), 1)

    def test_an_id_with_another_topics_prefix_is_a_problem(self):
        _, problems = cut_content.parse_topic("### U01 x\n\n分類：殘留內容\n", "code")
        self.assertIn("U01", problems[0])


class IndexTableTest(unittest.TestCase):
    def test_table_is_sorted_by_topic_then_number(self):
        entries, _ = cut_content.parse_topic(CODE_PAGE, "code")
        table = cut_content.index_table(entries)
        rows = table.strip().split("\n")
        self.assertEqual(rows[0], "| 編號 | 分類 | 內容 | 主題檔 |")
        self.assertEqual([r.split("|")[1].strip() for r in rows[2:]], ["C2", "C7", "C13"])
        self.assertIn("[程式痕跡](code.md)", rows[2])

    def test_apply_replaces_the_existing_table_only(self):
        entries, _ = cut_content.parse_topic(CODE_PAGE, "code")
        new = cut_content.apply_index(INDEX_PAGE, cut_content.index_table(entries))
        self.assertNotIn("stale", new)
        self.assertIn("| C13 | 否定性結論 | 沒有除錯鍵 |", new)
        self.assertIn("| C12 | `fdps_spell_heal_unit` | 編譯器產物 |", new)

    def test_exclusion_ids_are_read_from_the_exclusion_section(self):
        self.assertEqual(cut_content.exclusion_ids(INDEX_PAGE), ["C12"])


def make_tree(root, code=CODE_PAGE, index=INDEX_PAGE):
    base = Path(root) / "cut_content"
    base.mkdir(parents=True)
    (base / "code.md").write_text(code, encoding="utf-8")
    entries, _ = cut_content.parse_topic(code, "code")
    index = cut_content.apply_index(index, cut_content.index_table(entries))
    (base / "_index.md").write_text(index, encoding="utf-8")
    return base


class CheckTreeTest(unittest.TestCase):
    def test_a_consistent_tree_passes(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root)
            self.assertEqual(cut_content.check_tree(base), [])

    def test_a_stale_index_table_fails(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root)
            (base / "_index.md").write_text(INDEX_PAGE, encoding="utf-8")
            problems = cut_content.check_tree(base)
            self.assertTrue(any("index" in p for p in problems), problems)

    def test_an_id_both_listed_and_excluded_fails(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root, code=CODE_PAGE + "\n### C12 x\n\n分類：殘留內容\n")
            problems = cut_content.check_tree(base)
            self.assertTrue(any("C12" in p for p in problems), problems)

    def test_a_duplicate_id_fails(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root, code=CODE_PAGE + "\n### C7 again\n\n分類：殘留內容\n")
            self.assertTrue(any("C7" in p for p in cut_content.check_tree(base)))

    def test_a_broken_relative_link_fails(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root, code=CODE_PAGE + "\n見 [x](../nowhere.md#a)\n")
            self.assertTrue(any("nowhere.md" in p for p in cut_content.check_tree(base)))

    def test_citing_workspace_fails(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root, code=CODE_PAGE + "\n出處 workspace/x.json\n")
            self.assertTrue(any("workspace/" in p for p in cut_content.check_tree(base)))

    def test_media_must_follow_the_naming_rule_and_be_referenced(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root, code=CODE_PAGE + "\n![](media/code/c7-table.png)\n")
            media = base / "media" / "code"
            media.mkdir(parents=True)
            (media / "c7-table.png").write_bytes(b"png")
            self.assertEqual(cut_content.check_tree(base), [])
            (media / "C7_Table.PNG").write_bytes(b"png")      # bad name
            (media / "c2-orphan.png").write_bytes(b"png")     # never referenced
            (media / "c99-x.png").write_bytes(b"png")         # no such entry
            problems = cut_content.check_tree(base)
            self.assertTrue(any("C7_Table.PNG" in p for p in problems), problems)
            self.assertTrue(any("c2-orphan.png" in p for p in problems), problems)
            self.assertTrue(any("c99-x.png" in p for p in problems), problems)

    def test_the_cd_audio_folder_is_exempt(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root)
            cdda = base / "media" / "cdda"
            cdda.mkdir(parents=True)
            (cdda / "disc1-track12.wav").write_bytes(b"wav")
            self.assertEqual(cut_content.check_tree(base), [])


class MediaTest(unittest.TestCase):
    def test_regenerating_into_a_clean_folder_matches_what_is_committed(self):
        calls = []

        def generator(out_dir, game_dir):
            calls.append(game_dir)
            (out_dir / "c7-table.png").write_bytes(b"\x89PNG-fake")

        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root)
            generators = {"code": generator}
            cut_content.build_media("code", base, Path(root) / "game", generators)
            self.assertEqual((base / "media" / "code" / "c7-table.png").read_bytes(), b"\x89PNG-fake")
            self.assertEqual(cut_content.verify_media("code", base, Path(root) / "game", generators), [])
            (base / "media" / "code" / "c7-table.png").write_bytes(b"edited")
            self.assertTrue(cut_content.verify_media("code", base, Path(root) / "game", generators))

    def test_build_removes_files_the_generator_no_longer_makes(self):
        def generator(out_dir, game_dir):
            (out_dir / "c7-new.png").write_bytes(b"x")

        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root)
            stale = base / "media" / "code" / "c7-old.png"
            stale.parent.mkdir(parents=True)
            stale.write_bytes(b"old")
            cut_content.build_media("code", base, Path(root), {"code": generator})
            self.assertFalse(stale.exists())
            self.assertTrue((base / "media" / "code" / "c7-new.png").exists())

    def test_a_topic_without_a_generator_has_nothing_to_build(self):
        with tempfile.TemporaryDirectory() as root:
            base = make_tree(root)
            self.assertEqual(cut_content.verify_media("code", base, Path(root), {}), [])


if __name__ == "__main__":
    unittest.main()
