"""Unit tests for cutverify.py.

Run with:  python -m unittest tools/cut_verify/test_cutverify.py
"""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cutverify  # noqa: E402

TICKET_SAMPLE = """# 25.9 sample

intro text

## 發現清單

每條的格式：編號｜主張。

### 程式痕跡（轉錄到 25.10）

- **C1** first claim ｜`src/village.c`
- **C2** second claim
  that wraps

### 道具（轉錄到 25.12）

- **I04** parent claim
  - sub one
  - sub two
- **I05** after the subs

## Another heading

- **X9** must not be picked up
"""


def good_verdict(fid: str = "C1", claim: str = "first claim ｜`src/village.c`") -> dict:
    return {
        "id": fid,
        "claim_sha1": cutverify.claim_hash(claim),
        "target_ticket": "25.10",
        "verdict": "holds",
        "category": "sealed",
        "suggested_category": "sealed",
        "category_uncertain": False,
        "confidence": "high",
        "conclusion": "暗號表只有 24 列，章節索引 25 減 1 越界讀到堆疊殘值。",
        "evidence": [
            {"source": "src", "location": "src/village.c:120",
             "observation": "fdps_check_secret_code_key indexes the 24-row table with chapter-1"},
            {"source": "data", "location": "SHOP25.DAT@0x40",
             "observation": "secret row B7 B8 B9 D2 D3 D4 D6 D7 D8 D9"},
        ],
        "differences_from_claim": "",
        "corrected_claim": "",
        "new_traces": [],
        "pitfall_candidate": "",
        "open_question": "",
    }


class FindingsTest(unittest.TestCase):
    def setUp(self):
        self.findings = cutverify.parse_findings(TICKET_SAMPLE)

    def test_ids_in_order_and_heading_boundary(self):
        self.assertEqual([f["id"] for f in self.findings], ["C1", "C2", "I04", "I05"])

    def test_target_ticket_comes_from_section(self):
        self.assertEqual([f["target_ticket"] for f in self.findings],
                         ["25.10", "25.10", "25.12", "25.12"])

    def test_continuation_and_sub_bullets_belong_to_parent(self):
        c2 = self.findings[1]
        self.assertIn("that wraps", c2["claim"])
        i04 = self.findings[2]
        self.assertIn("sub one", i04["claim"])
        self.assertIn("sub two", i04["claim"])
        self.assertNotIn("after the subs", i04["claim"])

    def test_duplicate_id_is_an_error(self):
        with self.assertRaises(ValueError):
            cutverify.parse_findings(TICKET_SAMPLE.replace("**C2**", "**C1**"))

    def test_real_ticket_has_the_53_findings(self):
        findings = cutverify.load_findings()
        self.assertEqual(len(findings), 53)
        by_ticket = {}
        for f in findings:
            by_ticket[f["target_ticket"]] = by_ticket.get(f["target_ticket"], 0) + 1
        self.assertEqual(by_ticket, {"25.10": 16, "25.11": 10, "25.12": 8,
                                     "25.13": 6, "25.14": 13})


class GateTest(unittest.TestCase):
    def setUp(self):
        self.finding = cutverify.parse_findings(TICKET_SAMPLE)[0]

    def check(self, v):
        return cutverify.check_verdict(v, self.finding)

    def test_good_verdict_passes(self):
        self.assertEqual(self.check(good_verdict()), [])

    def test_unknown_enum_values_fail(self):
        for field, value in (("verdict", "true"), ("category", "cut"), ("confidence", "sure")):
            v = good_verdict()
            v[field] = value
            self.assertTrue(self.check(v), field)

    def test_id_and_ticket_must_match_the_finding(self):
        v = good_verdict()
        v["id"] = "C2"
        self.assertTrue(self.check(v))
        v = good_verdict()
        v["target_ticket"] = "25.12"
        self.assertTrue(self.check(v))

    def test_investigation_outputs_alone_are_not_evidence(self):
        v = good_verdict()
        v["evidence"] = [{"source": "investigation", "location": "cut_code/report 1",
                          "observation": "the survey said so"}]
        self.assertTrue(any("first-hand" in p for p in self.check(v)))

    def test_first_hand_evidence_needs_a_line_address_or_offset(self):
        v = good_verdict()
        v["evidence"][0]["location"] = "src/village.c"
        v["evidence"][1]["location"] = "SHOP25.DAT"
        self.assertTrue(self.check(v))

    def test_correction_needs_both_the_difference_and_the_corrected_claim(self):
        v = good_verdict()
        v["verdict"] = "needs_correction"
        self.assertTrue(self.check(v))
        v["differences_from_claim"] = "表是 25 列不是 24 列"
        v["corrected_claim"] = "修正後的主張"
        self.assertEqual(self.check(v), [])

    def test_refuted_needs_the_difference(self):
        v = good_verdict()
        v["verdict"] = "refuted"
        self.assertTrue(self.check(v))
        v["differences_from_claim"] = "有寫入端，在 src/x.c:10"
        self.assertEqual(self.check(v), [])

    def test_stale_claim_hash_is_reported(self):
        v = good_verdict()
        v["claim_sha1"] = "0" * 40
        self.assertTrue(any("stale" in p for p in self.check(v)))


class StateTest(unittest.TestCase):
    def test_pending_rescan_and_report(self):
        findings = cutverify.parse_findings(TICKET_SAMPLE)
        with tempfile.TemporaryDirectory() as tmp:
            vdir = Path(tmp)
            (vdir / "C1.json").write_text(json.dumps(good_verdict()), encoding="utf-8")
            bad = good_verdict("C2", findings[1]["claim"])
            bad["evidence"] = []
            (vdir / "C2.json").write_text(json.dumps(bad), encoding="utf-8")
            doubt = good_verdict("I04", findings[2]["claim"])
            doubt["target_ticket"] = "25.12"
            doubt["category"] = "residual"
            (vdir / "I04.json").write_text(json.dumps(doubt), encoding="utf-8")

            states = {s["id"]: s["state"] for s in cutverify.states(findings, vdir)}
            self.assertEqual(states, {"C1": "done", "C2": "failing", "I04": "done",
                                      "I05": "missing"})

            todo = cutverify.rescan_todo(findings, vdir)
            self.assertEqual([t["id"] for t in todo], ["I04"])
            self.assertIn("category", todo[0]["why"])

            doubt["_reread"] = "confirmed"
            (vdir / "I04.json").write_text(json.dumps(doubt), encoding="utf-8")
            self.assertEqual(cutverify.rescan_todo(findings, vdir), [])

            summary = cutverify.summarize(findings, vdir)
            self.assertEqual(summary["complete"], 2)
            self.assertEqual(sorted(summary["unfinished"]), ["C2", "I05"])
            self.assertEqual(summary["by_verdict"]["holds"], ["C1", "I04"])


if __name__ == "__main__":
    unittest.main()
