"""Unit tests for story_verify.py (python -m unittest tools/cut_content/test_story_verify.py)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import story_verify  # noqa: E402

ITEM = {"id": "B30-0d", "kind": "judged", "claim": "FDETXT30 0x0d 屬於 S4。"}


def verdict(**over):
    v = {"id": "B30-0d", "claim_sha1": story_verify.sha1(ITEM["claim"]), "verdict": "holds",
         "category": "residual", "owner": "S4", "confidence": "high",
         "conclusion": "第二型態平衡之神的叫陣沒有任何讀取端，也沒有顯示中的另一版。",
         "evidence": [{"source": "data", "location": "FDETXT30.TXT@0x3c4", "observation": "x"}],
         "corrected_claim": "", "open_question": "", "pitfall_candidate": ""}
    v.update(over)
    return v


class CheckVerdictTest(unittest.TestCase):
    def test_a_complete_verdict_passes(self):
        self.assertEqual(story_verify.check_verdict(verdict(), ITEM), [])

    def test_a_verdict_for_another_wording_is_stale(self):
        problems = story_verify.check_verdict(verdict(claim_sha1="0" * 40), ITEM)
        self.assertTrue(any("stale" in p for p in problems))

    def test_anything_but_holds_needs_the_corrected_claim(self):
        problems = story_verify.check_verdict(verdict(verdict="needs_correction"), ITEM)
        self.assertTrue(any("corrected_claim" in p for p in problems))

    def test_judged_items_need_first_hand_evidence_mechanical_ones_do_not(self):
        second_hand = [{"source": "guide", "location": "x", "observation": "y"}]
        self.assertTrue(story_verify.check_verdict(verdict(evidence=second_hand), ITEM))
        self.assertEqual(story_verify.check_verdict(verdict(evidence=second_hand),
                                                    dict(ITEM, kind="mechanical")), [])

    def test_the_owner_may_not_be_empty_and_fields_must_be_strings(self):
        self.assertTrue(story_verify.check_verdict(verdict(owner=""), ITEM))
        self.assertTrue(story_verify.check_verdict(verdict(open_question=None), ITEM))
        self.assertTrue(story_verify.check_verdict([], ITEM))


if __name__ == "__main__":
    unittest.main()
