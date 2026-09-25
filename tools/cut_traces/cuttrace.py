"""The non-judging half of the new-trace workflow (ticket 25.10, reusable per ticket).

Ticket 25.9's closing report lists "new traces": things its agents noticed while
verifying a finding, recorded in one sentence and deliberately not judged.  A
ticket that wants to put any of them into cut_content/ has to judge each one on
its own.  This script is everything around that judgement that needs no
judgement: numbering the traces, showing one, the gate over a judgement file,
the rescan list and the closing report.

    python tools/cut_traces/cuttrace.py show T10-05
    python tools/cut_traces/cuttrace.py pending [--all]
    python tools/cut_traces/cuttrace.py check [--ids ...] [--json]
    python tools/cut_traces/cuttrace.py rescan
    python tools/cut_traces/cuttrace.py report --date YYYY-MM-DD [--stopped REASON]

--ticket (default 25.10) picks the traces; ids are T<minor>-NN in the order the
closing report lists them, so they stay stable as long as that archived report
does not change.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SUMMARY = REPO / "devlog" / "runs" / "2026-09-25-cut-verify-summary.json"
WORK = REPO / "workspace" / "cut_traces"
RUNS = REPO / "devlog" / "runs"

sys.path.insert(0, str(REPO / "tools" / "cut_content"))
import cut_content  # noqa: E402  (owner of the cut_content/ page structure)

TOPIC_OF_TICKET = {"25.10": "code", "25.11": "units", "25.12": "items",
                   "25.13": "battle_assets", "25.14": "story"}
VERDICTS = ("holds", "refuted", "needs_correction")
DISPOSITIONS = ("absorbed", "addendum", "new_entry", "exclude", "route", "drop")
CLASSES = ("residual", "stub", "sealed", "predecessor_leftover", "negative")
CATEGORIES = CLASSES + ("excluded", "none")
ROUTES = ("none", "known_bugs", "pitfalls", "kb_fix", "25.11", "25.12", "25.13", "25.14")
CONFIDENCE = ("high", "medium", "low")
SOURCES = ("src", "ghidra", "data", "guide", "fd2", "investigation")
FIRST_HAND = ("src", "ghidra", "data")
REQUIRED = ("id", "trace_sha1", "from", "verdict", "disposition", "entry", "topic",
            "category", "route", "title", "kb_text", "route_note", "confidence",
            "conclusion", "evidence", "corrected_trace", "pitfall_candidate", "open_question")
MIN_CONCLUSION = 20


def sha1(text):
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def traces_for(summary, ticket):
    minor = ticket.split(".")[1]
    out = []
    for t in summary["new_traces"]:
        if t["ticket"] != ticket:
            continue
        out.append({"id": "T%s-%02d" % (minor, len(out) + 1), "ticket": ticket,
                    "from": t["from"], "trace": t["trace"], "trace_sha1": sha1(t["trace"])})
    return out


def known_entries(base=cut_content.CUT_DIR):
    """Every id currently on a cut_content/ page or in its exclusion list."""
    entries, _ = cut_content.collect(base)
    index = (base / "_index.md").read_text(encoding="utf-8")
    return {e.id for e in entries} | set(cut_content.exclusion_ids(index))


# ---------------------------------------------------------------------- gate

def check_evidence(evidence):
    problems, first_hand = [], 0
    if not isinstance(evidence, list) or not evidence:
        return ["no evidence"]
    for i, e in enumerate(evidence):
        if not isinstance(e, dict):
            problems.append("evidence[%d] is not an object" % i)
            continue
        src, loc, obs = e.get("source"), e.get("location") or "", e.get("observation") or ""
        if src not in SOURCES:
            problems.append("evidence[%d].source %r not one of %s" % (i, src, SOURCES))
            continue
        if not obs.strip():
            problems.append("evidence[%d] has no observation" % i)
        if src == "src" and not re.search(r":\d+", loc):
            problems.append("evidence[%d] src location needs file:line, got %r" % (i, loc))
            continue
        if src == "ghidra" and not re.search(r"[0-9a-fA-F]{5,8}", loc):
            problems.append("evidence[%d] ghidra location needs an address, got %r" % (i, loc))
            continue
        if src == "data" and "@" not in loc:
            problems.append("evidence[%d] data location needs FILE@offset, got %r" % (i, loc))
            continue
        if src in FIRST_HAND:
            first_hand += 1
    if first_hand == 0:
        problems.append("no first-hand evidence (src, ghidra or data); the guide, FD2 and "
                        "the survey only corroborate")
    return problems


def check_judgement(j, trace, ticket, known):
    problems = ["missing field %s" % k for k in REQUIRED if k not in j]
    if problems:
        return problems
    if j["id"] != trace["id"]:
        problems.append("id %r does not match the trace %s" % (j["id"], trace["id"]))
    if j["trace_sha1"] != trace["trace_sha1"]:
        problems.append("stale: judged a different wording of the trace")
    for field, allowed in (("verdict", VERDICTS), ("disposition", DISPOSITIONS),
                           ("category", CATEGORIES), ("route", ROUTES),
                           ("confidence", CONFIDENCE)):
        if j[field] not in allowed:
            problems.append("%s %r not one of %s" % (field, j[field], allowed))
    if len((j["conclusion"] or "").strip()) < MIN_CONCLUSION:
        problems.append("conclusion is shorter than %d characters" % MIN_CONCLUSION)
    problems += check_evidence(j["evidence"])

    if j["verdict"] == "refuted" and j["disposition"] != "drop":
        problems.append("a refuted trace has nothing to land: disposition must be drop")
    if j["verdict"] == "needs_correction" and not j["corrected_trace"].strip():
        problems.append("needs_correction without corrected_trace")

    d, text = j["disposition"], (j["kb_text"] or "").strip()
    if d in ("absorbed", "addendum") and j["entry"] not in known:
        problems.append("%s names entry %r, which is not on a cut_content/ page"
                        % (d, j["entry"]))
    if d in ("addendum", "new_entry", "exclude") and not text:
        problems.append("%s without kb_text" % d)
    if d in ("new_entry", "exclude"):
        if j["topic"] != TOPIC_OF_TICKET[ticket]:
            problems.append("%s must land on this ticket's topic %s, not %r; a trace for "
                            "another topic is a route" % (d, TOPIC_OF_TICKET[ticket], j["topic"]))
        if not (j["title"] or "").strip():
            problems.append("%s without title" % d)
    if d == "new_entry" and j["category"] not in CLASSES:
        problems.append("new_entry needs one of %s, got %r" % (CLASSES, j["category"]))
    if d == "exclude" and j["category"] != "excluded":
        problems.append("exclude needs category excluded")
    if d == "route":
        if j["route"] == "none":
            problems.append("route without a target")
        if not (j["route_note"] or "").strip():
            problems.append("route without route_note")
    return problems


def _load(path):
    try:
        return json.loads(path.read_text(encoding="utf-8")), None
    except Exception as exc:                                       # noqa: BLE001
        return None, "unreadable: %s" % exc


def states(traces, ticket, known, jdir):
    rows = []
    for t in traces:
        path = jdir / ("%s.json" % t["id"])
        if not path.is_file():
            rows.append({"id": t["id"], "state": "missing"})
            continue
        j, err = _load(path)
        problems = [err] if err else check_judgement(j, t, ticket, known)
        if problems:
            rows.append({"id": t["id"], "state": "failing", "problems": problems})
        else:
            rows.append({"id": t["id"], "state": "done"})
    return rows


# -------------------------------------------------------------------- rescan

def rescan_reasons(j):
    out = []
    if j["disposition"] in ("addendum", "new_entry", "exclude"):
        out.append("it lands in the knowledge base (%s)" % j["disposition"])
    if j["verdict"] != "holds":
        out.append("verdict is %s" % j["verdict"])
    if j["confidence"] != "high":
        out.append("confidence is %s" % j["confidence"])
    if (j["open_question"] or "").strip():
        out.append("open question recorded")
    return out


def rescan_todo(traces, ticket, known, jdir):
    todo = []
    for row in states(traces, ticket, known, jdir):
        if row["state"] != "done":
            continue
        j, _ = _load(jdir / ("%s.json" % row["id"]))
        why = rescan_reasons(j)
        if why and not (j.get("_supersedes") or j.get("_reread")):
            todo.append({"id": row["id"], "why": "; ".join(why)})
    return todo


# -------------------------------------------------------------------- report

def summarize(traces, ticket, known, jdir):
    rows = states(traces, ticket, known, jdir)
    done = {r["id"] for r in rows if r["state"] == "done"}
    by_verdict = {k: [] for k in VERDICTS}
    by_disposition = {k: [] for k in DISPOSITIONS}
    by_route = {k: [] for k in ROUTES if k != "none"}
    pitfalls, still_open, reread = [], [], []
    for t in traces:
        if t["id"] not in done:
            continue
        j, _ = _load(jdir / ("%s.json" % t["id"]))
        by_verdict[j["verdict"]].append(t["id"])
        by_disposition[j["disposition"]].append(t["id"])
        if j["route"] != "none":
            by_route[j["route"]].append(t["id"])
        if j["pitfall_candidate"].strip():
            pitfalls.append({"id": t["id"], "pitfall": j["pitfall_candidate"]})
        if j["open_question"].strip() or j["confidence"] != "high":
            still_open.append({"id": t["id"], "confidence": j["confidence"],
                               "open_question": j["open_question"]})
        if j.get("_supersedes") or j.get("_reread"):
            reread.append({"id": t["id"], "changed": bool(j.get("_supersedes"))})
    return {
        "ticket": ticket,
        "total": len(traces),
        "complete": len(done),
        "unfinished": [r["id"] for r in rows if r["state"] != "done"],
        "unfinished_detail": [r for r in rows if r["state"] != "done"],
        "by_verdict": by_verdict,
        "by_disposition": by_disposition,
        "by_route": by_route,
        "reread": reread,
        "still_open": still_open,
        "pitfall_candidates": pitfalls,
    }


def archive(traces, ticket, known, jdir):
    out = []
    for t, row in zip(traces, states(traces, ticket, known, jdir)):
        if row["state"] == "done":
            j, _ = _load(jdir / ("%s.json" % t["id"]))
            out.append(dict(j, trace=t["trace"]))
    return out


# ---------------------------------------------------------------------- main

def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ticket", default="25.10")
    ap.add_argument("--summary", type=Path, default=SUMMARY)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("show")
    p.add_argument("id")
    p = sub.add_parser("pending")
    p.add_argument("--all", action="store_true")
    p = sub.add_parser("check")
    p.add_argument("--ids", nargs="*", default=None)
    p.add_argument("--json", action="store_true")
    sub.add_parser("rescan")
    p = sub.add_parser("report")
    p.add_argument("--date", required=True)
    p.add_argument("--stopped", default="")
    args = ap.parse_args()

    ticket = args.ticket
    traces = traces_for(json.loads(args.summary.read_text(encoding="utf-8")), ticket)
    by_id = {t["id"]: t for t in traces}
    jdir = WORK / ticket / "judgements"
    known = known_entries()

    if args.cmd == "show":
        t = by_id.get(args.id)
        if t is None:
            print("no trace %s" % args.id, file=sys.stderr)
            return 1
        print(json.dumps(dict(t, judgement_file=str(jdir / ("%s.json" % t["id"])),
                              target_topic=TOPIC_OF_TICKET[ticket]),
                         ensure_ascii=False, indent=1))
        return 0

    if args.cmd == "pending":
        rows = states(traces, ticket, known, jdir)
        print(json.dumps(rows if args.all else [r["id"] for r in rows if r["state"] != "done"],
                         ensure_ascii=False, indent=1))
        return 0

    if args.cmd == "check":
        wanted = args.ids or [t["id"] for t in traces]
        unknown = [i for i in wanted if i not in by_id]
        rows = states([by_id[i] for i in wanted if i in by_id], ticket, known, jdir)
        result = {
            "checked": len(wanted),
            "ok": sum(1 for r in rows if r["state"] == "done"),
            "missing": [r["id"] for r in rows if r["state"] == "missing"] + unknown,
            "failures": {r["id"]: r["problems"] for r in rows if r["state"] == "failing"},
        }
        result["gate_passed"] = not result["missing"] and not result["failures"]
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=1))
        else:
            print("checked=%d ok=%d missing=%d failing=%d" % (
                result["checked"], result["ok"], len(result["missing"]), len(result["failures"])))
            for i in result["missing"]:
                print("  MISSING  %s" % i)
            for i, problems in result["failures"].items():
                print("  FAIL     %s" % i)
                for pr in problems:
                    print("             %s" % pr)
            print("GATE %s" % ("PASSED" if result["gate_passed"] else "FAILED"))
        return 0 if result["gate_passed"] else 1

    if args.cmd == "rescan":
        print(json.dumps(rescan_todo(traces, ticket, known, jdir), ensure_ascii=False, indent=1))
        return 0

    if args.cmd == "report":
        summary = summarize(traces, ticket, known, jdir)
        summary["stopped"] = args.stopped
        RUNS.mkdir(parents=True, exist_ok=True)
        stem = "%s-cut-traces-%s" % (args.date, ticket)
        spath = RUNS / ("%s-summary.json" % stem)
        spath.write_text(json.dumps(summary, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        written = [str(spath)]
        # A stopped run leaves a partial set that would read like a complete one,
        # so the judgement archive is only written when the run finished (ADR-0007 5.6).
        if not args.stopped:
            apath = RUNS / ("%s-judgements.json" % stem)
            apath.write_text(json.dumps(archive(traces, ticket, known, jdir), ensure_ascii=False,
                                        indent=1) + "\n", encoding="utf-8")
            written.append(str(apath))
        print(json.dumps({"written": written, "complete": summary["complete"],
                          "total": summary["total"], "unfinished": summary["unfinished"],
                          "by_disposition": {k: len(v) for k, v in
                                             summary["by_disposition"].items()}},
                         ensure_ascii=False))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
