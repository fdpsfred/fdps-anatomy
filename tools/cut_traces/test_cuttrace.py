"""Unit tests for cuttrace.py (python -m unittest tools/cut_traces/test_cuttrace.py)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cuttrace  # noqa: E402

SUMMARY = {"new_traces": [
    {"from": "C1", "ticket": "25.10", "trace": "trace one"},
    {"from": "U01", "ticket": "25.11", "trace": "someone else's"},
    {"from": "C3", "ticket": "25.10", "trace": "trace two"},
]}


def traces():
    return cuttrace.traces_for(SUMMARY, "25.10")


def judgement(**over):
    t = traces()[0]
    j = {
        "id": t["id"], "trace_sha1": t["trace_sha1"], "from": "C1",
        "verdict": "holds", "disposition": "route", "entry": "", "topic": "",
        "category": "none", "route": "known_bugs", "title": "", "kb_text": "",
        "route_note": "這是 bug 的機制，不是刪減", "confidence": "high",
        "conclusion": "驗證後的結論寫在這裡，至少二十個字元長度才算數。",
        "evidence": [{"source": "src", "location": "src/village.c:12", "observation": "x"}],
        "corrected_trace": "", "pitfall_candidate": "", "open_question": "",
    }
    j.update(over)
    return j


KNOWN = {"C1", "C3", "C12"}


class TracesTest(unittest.TestCase):
    def test_only_the_tickets_traces_numbered_in_order(self):
        ts = traces()
        self.assertEqual([t["id"] for t in ts], ["T10-01", "T10-02"])
        self.assertEqual([t["from"] for t in ts], ["C1", "C3"])
        self.assertEqual(len(ts[0]["trace_sha1"]), 40)


class CheckTest(unittest.TestCase):
    def check(self, **over):
        return cuttrace.check_judgement(judgement(**over), traces()[0], "25.10", KNOWN)

    def test_a_complete_route_judgement_passes(self):
        self.assertEqual(self.check(), [])

    def test_stale_wording_fails(self):
        self.assertTrue(any("stale" in p for p in self.check(trace_sha1="0" * 40)))

    def test_first_hand_evidence_is_required(self):
        problems = self.check(evidence=[{"source": "guide", "location": "x", "observation": "y"}])
        self.assertTrue(any("first-hand" in p for p in problems))

    def test_refuted_must_be_dropped(self):
        self.assertTrue(self.check(verdict="refuted", disposition="route"))
        self.assertEqual(self.check(verdict="refuted", disposition="drop", route="none",
                                    route_note=""), [])

    def test_needs_correction_needs_the_corrected_trace(self):
        self.assertTrue(self.check(verdict="needs_correction"))
        self.assertEqual(self.check(verdict="needs_correction", corrected_trace="改寫後"), [])

    def test_addendum_needs_an_existing_entry_and_text(self):
        base = dict(disposition="addendum", route="none", route_note="")
        self.assertTrue(self.check(entry="C99", kb_text="補充", **base))
        self.assertTrue(self.check(entry="C3", kb_text="", **base))
        self.assertEqual(self.check(entry="C3", kb_text="補充一句。", **base), [])

    def test_new_entry_must_land_on_this_tickets_topic_with_a_class(self):
        base = dict(disposition="new_entry", route="none", route_note="", title="標題",
                    kb_text="條目本文。")
        self.assertEqual(self.check(topic="code", category="residual", **base), [])
        self.assertTrue(self.check(topic="story", category="residual", **base))
        self.assertTrue(self.check(topic="code", category="none", **base))
        self.assertTrue(self.check(topic="code", category="excluded", **base))

    def test_exclude_needs_title_reason_and_the_excluded_class(self):
        base = dict(disposition="exclude", route="none", route_note="", topic="code")
        self.assertEqual(self.check(category="excluded", title="內容", kb_text="理由", **base), [])
        self.assertTrue(self.check(category="residual", title="內容", kb_text="理由", **base))
        self.assertTrue(self.check(category="excluded", title="", kb_text="理由", **base))

    def test_a_null_field_fails_the_gate_instead_of_crashing_it(self):
        self.assertTrue(any("corrected_trace" in p for p in self.check(corrected_trace=None)))
        self.assertTrue(cuttrace.check_judgement([], traces()[0], "25.10", KNOWN))

    def test_an_english_word_is_not_a_ghidra_address(self):
        problems = self.check(evidence=[{"source": "ghidra", "location": "facade",
                                         "observation": "x"}])
        self.assertTrue(any("address" in p for p in problems))
        self.assertEqual(self.check(evidence=[{"source": "ghidra", "location": "0x357a0",
                                               "observation": "x"}]), [])

    def test_route_needs_a_target_and_a_note(self):
        self.assertTrue(self.check(route="none"))
        self.assertTrue(self.check(route_note=""))
        self.assertTrue(self.check(route="elsewhere"))


class AnyTopicTicketTest(unittest.TestCase):
    """Ticket 25.8's chapter traces may land on any topic page."""

    def test_new_entry_may_land_on_any_topic_but_a_real_one(self):
        summary = {"new_traces": [{"from": "ch03", "ticket": "25.8", "trace": "t"}]}
        t = cuttrace.traces_for(summary, "25.8")[0]
        self.assertEqual(t["id"], "T8-01")
        j = judgement(id=t["id"], trace_sha1=t["trace_sha1"], disposition="new_entry",
                      route="none", route_note="", title="標題", kb_text="本文。",
                      category="sealed")
        self.assertEqual(cuttrace.check_judgement(dict(j, topic="items"), t, "25.8", KNOWN), [])
        self.assertEqual(cuttrace.check_judgement(dict(j, topic="code"), t, "25.8", KNOWN), [])
        self.assertTrue(cuttrace.check_judgement(dict(j, topic="nowhere"), t, "25.8", KNOWN))


class FlowTest(unittest.TestCase):
    def test_states_rescan_and_summary(self):
        with tempfile.TemporaryDirectory() as d:
            jdir = Path(d)
            t1, t2 = traces()
            (jdir / "T10-01.json").write_text(json.dumps(judgement()), encoding="utf-8")
            lands = judgement(id=t2["id"], trace_sha1=t2["trace_sha1"], **{"from": "C3"},
                              disposition="addendum", entry="C3", kb_text="補充。",
                              route="none", route_note="")
            (jdir / "T10-02.json").write_text(json.dumps(lands), encoding="utf-8")
            rows = cuttrace.states(traces(), "25.10", KNOWN, jdir)
            self.assertEqual([r["state"] for r in rows], ["done", "done"])
            # Everything that lands in the knowledge base gets a second reader.
            self.assertEqual([t["id"] for t in cuttrace.rescan_todo(traces(), "25.10", KNOWN, jdir)],
                             ["T10-02"])
            lands["_reread"] = "確認"
            (jdir / "T10-02.json").write_text(json.dumps(lands), encoding="utf-8")
            self.assertEqual(cuttrace.rescan_todo(traces(), "25.10", KNOWN, jdir), [])
            s = cuttrace.summarize(traces(), "25.10", KNOWN, jdir)
            self.assertEqual(s["complete"], 2)
            self.assertEqual(s["by_disposition"]["addendum"], ["T10-02"])
            self.assertEqual(s["by_route"]["known_bugs"], ["T10-01"])

    def test_missing_file_is_unfinished(self):
        with tempfile.TemporaryDirectory() as d:
            rows = cuttrace.states(traces(), "25.10", KNOWN, Path(d))
            self.assertEqual([r["state"] for r in rows], ["missing", "missing"])
            self.assertEqual(cuttrace.summarize(traces(), "25.10", KNOWN, Path(d))["unfinished"],
                             ["T10-01", "T10-02"])


if __name__ == "__main__":
    unittest.main()
