"""Bookkeeping for ticket 25.9: the per-finding verification of the cut-content survey.

The workflow (verify_ticket25_9.js) sends one finding to one agent.  Everything
that is not a judgement lives here, so that it is the same code every time:

  show      print ONE finding -- the only way a judging agent sees its claim
  pending   every finding with its state: missing / failing / done
  check     the gate over some or all verdict files
  rescan    findings whose verdict needs a second reading and has not had one
  report    the closing report and the archive of every verdict, for devlog/runs/

The findings are read from the ticket file itself, not copied, so a correction to
the ticket's wording makes the old verdict stale instead of silently keeping it:
every verdict records the SHA-1 of the claim text it judged.

Usage:
    python tools/cut_verify/cutverify.py show C1
    python tools/cut_verify/cutverify.py pending [--all]
    python tools/cut_verify/cutverify.py check [--ids C1 C2 ...] [--json]
    python tools/cut_verify/cutverify.py rescan
    python tools/cut_verify/cutverify.py report --date 2026-09-25 [--stopped REASON]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
TICKET = REPO / ".scratch" / "fdps-rebuild" / "issues" / "25.9-cut-findings-verification.md"
WORK = REPO / "workspace" / "cut_verify"
VERDICTS = WORK / "verdicts"
RUNS = REPO / "devlog" / "runs"

VERDICT_VALUES = ("holds", "refuted", "needs_correction")
# The five classes of CONTEXT.md "刪減與未用", plus the negative conclusions
# of the survey ("there is no debug key"), which assert that nothing exists and
# so belong to no class.
CATEGORY_VALUES = {
    "residual": "殘留內容",
    "stub": "空殼",
    "sealed": "被封住的內容",
    "predecessor_leftover": "前作遺留",
    "excluded": "排除",
    "negative": "否定性結論",
}
CONFIDENCE_VALUES = ("high", "medium", "low")
SOURCE_VALUES = ("src", "ghidra", "data", "guide", "fd2", "investigation")
FIRST_HAND = ("src", "ghidra", "data")
REQUIRED = ("id", "claim_sha1", "target_ticket", "verdict", "category",
            "suggested_category", "category_uncertain", "confidence", "conclusion",
            "evidence", "differences_from_claim", "corrected_claim", "new_traces",
            "pitfall_candidate", "open_question")
MIN_CONCLUSION = 20

_SECTION = re.compile(r"^###\s+(.*)$")
_TARGET = re.compile(r"轉錄到\s*(25\.\d+)")
_ITEM = re.compile(r"^- \*\*([A-Z]\d+)\*\*\s*(.*)$")


# ------------------------------------------------------------------ findings

def claim_hash(claim: str) -> str:
    return hashlib.sha1(claim.strip().encode("utf-8")).hexdigest()


def parse_findings(text: str) -> list[dict]:
    """Split the ticket's 發現清單 into findings.

    Structure only: a finding is a top-level bullet whose text starts with a bold
    ID, plus every indented or wrapped line under it.  Its target ticket is the
    one its ### section says it is transcribed to.  Nothing in the claim text is
    interpreted.
    """
    findings: list[dict] = []
    in_list = False
    target = None
    current = None
    for line in text.splitlines():
        if line.startswith("## "):
            in_list = line.strip() == "## 發現清單"
            current = None
            continue
        if not in_list:
            continue
        m = _SECTION.match(line)
        if m:
            t = _TARGET.search(m.group(1))
            target = t.group(1) if t else None
            current = None
            continue
        m = _ITEM.match(line)
        if m:
            if target is None:
                raise ValueError("finding %s is outside a section naming its ticket" % m.group(1))
            current = {"id": m.group(1), "target_ticket": target,
                       "section": "", "lines": [m.group(2)]}
            findings.append(current)
            continue
        if current is not None and line.strip():
            current["lines"].append(line.rstrip())
        elif current is not None and not line.strip():
            current = None

    seen = set()
    out = []
    for f in findings:
        if f["id"] in seen:
            raise ValueError("duplicate finding id %s" % f["id"])
        seen.add(f["id"])
        claim = "\n".join(f["lines"]).strip()
        out.append({"id": f["id"], "target_ticket": f["target_ticket"],
                    "claim": claim, "claim_sha1": claim_hash(claim)})
    return out


def load_findings(path: Path = TICKET) -> list[dict]:
    return parse_findings(path.read_text(encoding="utf-8"))


# ---------------------------------------------------------------------- gate

def check_verdict(v: dict, finding: dict) -> list[str]:
    problems = [("missing field %s" % k) for k in REQUIRED if k not in v]
    if problems:
        return problems

    if v["id"] != finding["id"]:
        problems.append("id %r does not match the finding %s" % (v["id"], finding["id"]))
    if v["target_ticket"] != finding["target_ticket"]:
        problems.append("target_ticket %r, the ticket says %s"
                        % (v["target_ticket"], finding["target_ticket"]))
    if v["claim_sha1"] != finding["claim_sha1"]:
        problems.append("stale: judged a different wording of the claim")
    if v["verdict"] not in VERDICT_VALUES:
        problems.append("verdict %r not one of %s" % (v["verdict"], VERDICT_VALUES))
    for field in ("category", "suggested_category"):
        if v[field] not in CATEGORY_VALUES:
            problems.append("%s %r not one of %s" % (field, v[field], sorted(CATEGORY_VALUES)))
    if v["confidence"] not in CONFIDENCE_VALUES:
        problems.append("confidence %r not one of %s" % (v["confidence"], CONFIDENCE_VALUES))
    if not isinstance(v["category_uncertain"], bool):
        problems.append("category_uncertain must be true or false")
    if not isinstance(v["new_traces"], list):
        problems.append("new_traces must be a list")
    if len((v["conclusion"] or "").strip()) < MIN_CONCLUSION:
        problems.append("conclusion is shorter than %d characters" % MIN_CONCLUSION)

    evidence = v["evidence"] if isinstance(v["evidence"], list) else []
    if not evidence:
        problems.append("no evidence")
    first_hand = 0
    for i, e in enumerate(evidence):
        if not isinstance(e, dict):
            problems.append("evidence[%d] is not an object" % i)
            continue
        src, loc, obs = e.get("source"), e.get("location") or "", e.get("observation") or ""
        if src not in SOURCE_VALUES:
            problems.append("evidence[%d].source %r not one of %s" % (i, src, SOURCE_VALUES))
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
    if evidence and first_hand == 0:
        problems.append("no first-hand evidence (src, ghidra or data); the survey's own "
                        "outputs, the guide and FD2 only corroborate")

    if v["verdict"] in ("refuted", "needs_correction") and not v["differences_from_claim"].strip():
        problems.append("%s without differences_from_claim" % v["verdict"])
    if v["verdict"] == "needs_correction" and not v["corrected_claim"].strip():
        problems.append("needs_correction without corrected_claim")
    return problems


def _load(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8")), None
    except Exception as exc:                                       # noqa: BLE001
        return None, "unreadable: %s" % exc


def states(findings: list[dict], vdir: Path = VERDICTS) -> list[dict]:
    rows = []
    for f in findings:
        path = vdir / ("%s.json" % f["id"])
        if not path.is_file():
            rows.append({"id": f["id"], "state": "missing"})
            continue
        v, err = _load(path)
        problems = [err] if err else check_verdict(v, f)
        if problems:
            rows.append({"id": f["id"], "state": "failing", "problems": problems})
        else:
            rows.append({"id": f["id"], "state": "done", "verdict": v["verdict"]})
    return rows


# -------------------------------------------------------------------- rescan

def rescan_reasons(v: dict) -> list[str]:
    out = []
    if v.get("verdict") == "needs_correction":
        out.append("verdict is needs_correction")
    if v.get("category") != v.get("suggested_category"):
        out.append("category %s differs from the list's suggestion %s"
                   % (v.get("category"), v.get("suggested_category")))
    if v.get("category_uncertain"):
        out.append("category marked uncertain")
    if v.get("confidence") != "high":
        out.append("confidence is %s" % v.get("confidence"))
    if (v.get("open_question") or "").strip():
        out.append("open question recorded")
    return out


def rescan_todo(findings: list[dict], vdir: Path = VERDICTS) -> list[dict]:
    todo = []
    for row in states(findings, vdir):
        if row["state"] != "done":
            continue
        v, _ = _load(vdir / ("%s.json" % row["id"]))
        why = rescan_reasons(v)
        if why and not (v.get("_supersedes") or v.get("_reread")):
            todo.append({"id": row["id"], "why": "; ".join(why)})
    return todo


# -------------------------------------------------------------------- report

def summarize(findings: list[dict], vdir: Path = VERDICTS) -> dict:
    rows = states(findings, vdir)
    done = [r["id"] for r in rows if r["state"] == "done"]
    by_verdict = {k: [] for k in VERDICT_VALUES}
    by_category: dict[str, list[str]] = {k: [] for k in CATEGORY_VALUES}
    by_ticket: dict[str, dict] = {}
    new_traces, pitfalls, still_open, reread, category_changed = [], [], [], [], []
    for f in findings:
        if f["id"] not in done:
            continue
        v, _ = _load(vdir / ("%s.json" % f["id"]))
        by_verdict[v["verdict"]].append(f["id"])
        by_category[v["category"]].append(f["id"])
        t = by_ticket.setdefault(f["target_ticket"], {k: [] for k in VERDICT_VALUES})
        t[v["verdict"]].append(f["id"])
        for trace in v["new_traces"]:
            new_traces.append({"from": f["id"], "ticket": f["target_ticket"], "trace": trace})
        if v["pitfall_candidate"].strip():
            pitfalls.append({"from": f["id"], "pitfall": v["pitfall_candidate"]})
        if v["open_question"].strip() or v["confidence"] != "high":
            still_open.append({"id": f["id"], "confidence": v["confidence"],
                               "open_question": v["open_question"]})
        if v.get("_supersedes") or v.get("_reread"):
            reread.append({"id": f["id"], "changed": bool(v.get("_supersedes"))})
        if v["category"] != v["suggested_category"]:
            category_changed.append({"id": f["id"], "suggested": v["suggested_category"],
                                     "judged": v["category"]})
    return {
        "total": len(findings),
        "complete": len(done),
        "unfinished": [r["id"] for r in rows if r["state"] != "done"],
        "unfinished_detail": [r for r in rows if r["state"] != "done"],
        "by_verdict": by_verdict,
        "by_category": by_category,
        "by_ticket": by_ticket,
        "category_changed": category_changed,
        "reread": reread,
        "still_open": still_open,
        "new_traces": new_traces,
        "pitfall_candidates": pitfalls,
    }


def archive(findings: list[dict], vdir: Path = VERDICTS) -> list[dict]:
    out = []
    for row in states(findings, vdir):
        if row["state"] != "done":
            continue
        v, _ = _load(vdir / ("%s.json" % row["id"]))
        out.append(v)
    return out


# ---------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verdicts", type=Path, default=VERDICTS)
    ap.add_argument("--ticket", type=Path, default=TICKET)
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

    findings = load_findings(args.ticket)
    by_id = {f["id"]: f for f in findings}

    if args.cmd == "show":
        f = by_id.get(args.id)
        if f is None:
            print("no finding %s" % args.id, file=sys.stderr)
            return 1
        print(json.dumps(f, ensure_ascii=False, indent=1))
        return 0

    if args.cmd == "pending":
        rows = states(findings, args.verdicts)
        if args.all:
            print(json.dumps(rows, ensure_ascii=False, indent=1))
        else:
            print(json.dumps([r["id"] for r in rows if r["state"] != "done"]))
        return 0

    if args.cmd == "check":
        wanted = args.ids or [f["id"] for f in findings]
        unknown = [i for i in wanted if i not in by_id]
        rows = states([by_id[i] for i in wanted if i in by_id], args.verdicts)
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
            print("checked=%d ok=%d missing=%d failing=%d"
                  % (result["checked"], result["ok"], len(result["missing"]),
                     len(result["failures"])))
            for i in result["missing"]:
                print("  MISSING  %s" % i)
            for i, problems in result["failures"].items():
                print("  FAIL     %s" % i)
                for pr in problems:
                    print("             %s" % pr)
            print("GATE %s" % ("PASSED" if result["gate_passed"] else "FAILED"))
        return 0 if result["gate_passed"] else 1

    if args.cmd == "rescan":
        print(json.dumps(rescan_todo(findings, args.verdicts), ensure_ascii=False, indent=1))
        return 0

    if args.cmd == "report":
        summary = summarize(findings, args.verdicts)
        summary["stopped"] = args.stopped
        summary["category_labels"] = CATEGORY_VALUES
        RUNS.mkdir(parents=True, exist_ok=True)
        spath = RUNS / ("%s-cut-verify-summary.json" % args.date)
        spath.write_text(json.dumps(summary, ensure_ascii=False, indent=1) + "\n",
                         encoding="utf-8")
        written = [str(spath)]
        # A stopped run leaves a partial set; the archive would read exactly like
        # a complete one, so it is only written when the run finished (ADR-0007 5.6).
        if not args.stopped:
            apath = RUNS / ("%s-cut-verify-verdicts.json" % args.date)
            apath.write_text(json.dumps(archive(findings, args.verdicts), ensure_ascii=False,
                                        indent=1) + "\n", encoding="utf-8")
            written.append(str(apath))
        print(json.dumps({"written": written, "complete": summary["complete"],
                          "total": summary["total"], "unfinished": summary["unfinished"],
                          "by_verdict": {k: len(x) for k, x in summary["by_verdict"].items()}},
                         ensure_ascii=False))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
