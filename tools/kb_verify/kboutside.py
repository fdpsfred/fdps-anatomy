"""The fixes the verification passes could not make in the knowledge base.

A verifier (or a consistency agent) that found a problem whose fix lives
outside the page it was judging wrote it into its verdict's `outside` field
instead of an edit: a src/ or tests/ comment, a Ghidra plate or parameter
name, a generator or its hand data in tools/, another page, a ticket, an ADR.
The closing reports list them (devlog/runs/*-kb-verify-summary.json,
*-kb-consist-summary.json, *-kb-refused-summary.json).  This script turns
them into judged, transcribable work:

    python tools/kb_verify/kboutside.py freeze [--refresh]   group the fixes into items (items.json)
    python tools/kb_verify/kboutside.py show <ID>            ONE group (the agent's only view of it)
    python tools/kb_verify/kboutside.py pending [--all]
    python tools/kb_verify/kboutside.py check [--ids ...] [--json]
    python tools/kb_verify/kboutside.py rescan
    python tools/kb_verify/kboutside.py report --date YYYY-MM-DD [--stopped REASON]
    python tools/kb_verify/kboutside.py apply [--dry-run]
    python tools/kb_verify/kboutside.py ghidra               the Ghidra changes to transcribe

Several fixes often touch the same comment (four verifiers saw the same wrong
sentence in src/text.c), so the unit of work is a GROUP: fixes that name a
common non-knowledge-base file are joined (union-find over the files they
name); a fix that names only knowledge-base pages is grouped by its first
page.  One agent settles one group and writes one coherent set of edits, so
two agents never race on the same comment.

What the landing enforces, deterministically:
  - an edit's old text occurs exactly once in its file, and never inside a
    generated region of a knowledge-base page or in a generated page;
  - an edit to src/ or tests/ C source changes comments only, or renames
    identifiers and nothing else (the whole file before and after, with
    comments and literals blanked by code_emit's strip_c, must be the same
    token sequence up to identifier names) -- the full build gate then proves
    the image unchanged;
  - no edit goes into docs/adr/ (a decision record says what was decided
    then), devlog/, workspace/, legacy/ or README.md;
  - regeneration is picked from a fixed list (REGENERATE), never a free command.
Ghidra changes are not landed by this script: `ghidra` prints them for the
ticket session, which makes them one at a time and checks bookmarks, calling
conventions, saves and re-exports the snapshot.
"""

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "code_emit"))
import kbverify  # noqa: E402

REPO = kbverify.REPO
WORK = REPO / "workspace" / "kb_outside"
ITEMS = WORK / "items.json"
VERDICTS = WORK / "verdicts"
RUNS = kbverify.RUNS

STATUSES = ("done_already", "fixed", "declined", "developer")
FORBIDDEN_ROOTS = ("docs/adr/", "devlog/", "workspace/", "legacy/", "fdps_game_files/")
FORBIDDEN_FILES = ("README.md",)
C_SOURCE = re.compile(r"^(?:src|tests)/.+\.(?:c|h)$")
PATH = re.compile(r"(?:src|tests|tools|docs|\.scratch|\.claude|program_info|resource_info|rebuild_info"
                  r"|assets|chapters|cut_content|libs)/[\w./\-]+\.(?:json|java|asm|py|js|md|c|h)\b")
KB_ROOTS = ("program_info/", "resource_info/", "rebuild_info/", "assets/", "chapters/",
            "cut_content/", "libs/", "CONTEXT.md")

# The regenerations a group may ask for after its edits, by key.
REGENERATE = {
    "global_text": ["python", "tools/global_text/global_text.py", "build"],
    "story": ["python", "tools/cut_content/story.py", "build"],
    "cut_content_index": ["python", "tools/cut_content/cut_content.py", "index"],
    "chapters_index": ["python", "tools/chapter_docs/index.py", "build"],
    "data_skill": ["python", "tools/data_skill/build.py"],
    "chapter_refill": ["python", "tools/chapter_docs/check_chapter.py", "--refill"],   # + chapter numbers
}


# ------------------------------------------------------------------- items

def outside_fixes():
    """[(ref, outside, context, home_doc)] from every closing report."""
    out = []
    for path in sorted(RUNS.glob("*-kb-verify-summary.json")):
        for o in json.loads(path.read_text(encoding="utf-8")).get("outside_fixes", []):
            u = o["unit"]
            doc = next((x["doc"] for x in kbverify.units() if x["id"] == u), None)
            out.append(("V:%s#%s" % (u, o["n"]), o["outside"], o.get("problem", ""), doc))
    for stem, prefix in (("kb-consist", "C"), ("kb-refused", "F")):
        for path in sorted(RUNS.glob("*-%s-summary.json" % stem)):
            for o in json.loads(path.read_text(encoding="utf-8")).get("outside_fixes", []):
                out.append(("%s:%s" % (prefix, o["id"]), o["outside"], "", None))
    return out


def files_named(text):
    return sorted({m.rstrip(".") for m in PATH.findall(text)})


def group(fixes):
    """Union-find over the non-KB files each fix names."""
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    keys = {}
    for ref, text, _ctx, home in fixes:
        named = files_named(text)
        code = [p for p in named if not p.startswith(KB_ROOTS)]
        k = code or named[:1] or ([home] if home else ["?" + ref])
        keys[ref] = named or ([home] if home else [])
        for f in k:
            parent[find(ref)] = find("F:" + f)
    groups = {}
    for ref, *_ in fixes:
        groups.setdefault(find(ref), []).append(ref)
    return [sorted(v) for v in groups.values()], keys


def build_items():
    fixes = outside_fixes()
    by_ref = {f[0]: f for f in fixes}
    groups, keys = group(fixes)
    groups.sort(key=lambda g: (-len(g), g[0]))
    items = []
    for k, refs in enumerate(groups, 1):
        members = [{"ref": r, "outside": by_ref[r][1], "context": by_ref[r][2],
                    "home_doc": by_ref[r][3], "files": keys[r]} for r in refs]
        it = {"id": "O%d" % k, "members": members,
              "files": sorted({f for m in members for f in m["files"]})}
        it["sha1"] = kbverify.sha1(json.dumps(it, ensure_ascii=False, sort_keys=True))
        items.append(it)
    return items


def load_items():
    return json.loads(ITEMS.read_text(encoding="utf-8")) if ITEMS.is_file() else []


def by_id():
    return {i["id"]: i for i in load_items()}


def render(item):
    out = ["Group %s: %d fix(es) the verification passes could not make in the page they judged."
           % (item["id"], len(item["members"])),
           "Files named: %s" % (", ".join(item["files"]) or "(none; see each fix)"), ""]
    for m in item["members"]:
        out.append("[%s]%s" % (m["ref"], " (found while verifying %s)" % m["home_doc"] if m["home_doc"] else ""))
        if m["context"]:
            out.append("  the problem: %s" % m["context"])
        out.append("  the fix asked for: %s" % m["outside"])
        out.append("")
    out.append("The verdict that raised a fix is in workspace\\kb_verify\\verdicts\\<unit>.json "
               "(V:<unit>#<n>), workspace\\kb_consist\\verdicts\\<id>.json (C:<id>) or "
               "workspace\\kb_refused\\verdicts\\<id>.json (F:<id>).")
    return "\n".join(out)


# -------------------------------------------------------------------- gate

REQUIRED = ("id", "item_sha1", "members", "edits", "ghidra", "regenerate", "conclusion", "evidence",
            "confidence", "open_question", "developer_question")


def _allowed_file(rel):
    if rel in FORBIDDEN_FILES or rel.startswith(FORBIDDEN_ROOTS):
        return "edits to %s are not made by this pass" % rel
    if rel in kbverify.GENERATED_PAGES:
        return "%s is generated; edit its generator and ask for the regeneration" % rel
    if not (REPO / rel).is_file():
        return "%s does not exist" % rel
    return None


def _tokens(text):
    from build_emit import strip_c
    return re.findall(r"[A-Za-z_]\w*|\S", strip_c(text))


def code_change(before, after):
    """None when after differs from before only in comments (and whitespace)
    or only by renamed identifiers; else what changed."""
    a, b = _tokens(before), _tokens(after)
    if a == b:
        return None
    if len(a) == len(b) and all(x == y or (re.fullmatch(r"[A-Za-z_]\w*", x) and re.fullmatch(r"[A-Za-z_]\w*", y))
                                for x, y in zip(a, b)):
        return None
    return "the edit changes code, not only comments or identifier names"


def check_edits(edits):
    problems, texts = [], {}
    if not isinstance(edits, list):
        return ["edits must be a list"], texts
    for k, e in enumerate(edits):
        w = "edits[%d]" % k
        if not isinstance(e, dict) or not all(isinstance(e.get(x), str) for x in ("file", "old", "new", "why")):
            problems.append("%s needs file, old, new, why strings" % w)
            continue
        bad = _allowed_file(e["file"])
        if bad:
            problems.append("%s: %s" % (w, bad))
            continue
        if not e["old"] or e["old"] == e["new"]:
            problems.append("%s: empty or no-op edit" % w)
            continue
        text = texts.setdefault(e["file"], (REPO / e["file"]).read_text(encoding="utf-8"))
        if text.count(e["old"]) != 1:
            problems.append("%s: old occurs %d times in %s" % (w, text.count(e["old"]), e["file"]))
            continue
        if e["file"].endswith(".md") and e["file"] in kbverify.all_kb_docs():
            lines = text.split("\n")
            hidden = kbverify.masked_set(kbverify.masks(lines)[0])
            first = text.count("\n", 0, text.index(e["old"]))
            if any(i in hidden for i in range(first, first + e["old"].count("\n") + 1)):
                problems.append("%s: old lies inside a generated region of %s" % (w, e["file"]))
                continue
        new_text = text.replace(e["old"], e["new"], 1)
        if C_SOURCE.match(e["file"]):
            why = code_change(text, new_text)
            if why:
                problems.append("%s: %s (%s)" % (w, why, e["file"]))
                continue
        if e["file"].endswith(".json"):
            try:
                json.loads(new_text)
            except ValueError as exc:
                problems.append("%s: %s would no longer parse: %s" % (w, e["file"], exc))
                continue
        texts[e["file"]] = new_text
    return problems, texts


def check_regenerate(regen):
    problems = []
    if not isinstance(regen, list):
        return ["regenerate must be a list"]
    for r in regen:
        if not isinstance(r, str):
            problems.append("regenerate entries are strings")
            continue
        key = r.split(":")[0]
        if key not in REGENERATE:
            problems.append("regenerate %r is not one of %s" % (r, sorted(REGENERATE)))
        elif key == "chapter_refill" and not re.fullmatch(r"chapter_refill:\d+(,\d+)*", r):
            problems.append("write chapter_refill:N[,N...]")
    return problems


def check_verdict(v, item):
    if not isinstance(v, dict):
        return ["not a JSON object"]
    problems = ["missing field %s" % k for k in REQUIRED if k not in v]
    if problems:
        return problems
    if v["id"] != item["id"]:
        problems.append("id mismatch")
    if v["item_sha1"] != item["sha1"]:
        problems.append("stale: judged a different item")
    refs = [m["ref"] for m in item["members"]]
    got = {m.get("ref"): m for m in v["members"] if isinstance(m, dict)}
    for r in refs:
        m = got.get(r)
        if m is None:
            problems.append("no status for member %s" % r)
        elif m.get("status") not in STATUSES or not str(m.get("note", "")).strip():
            problems.append("member %s needs a status in %s and a note" % (r, STATUSES))
    if v["confidence"] not in kbverify.CONFIDENCE:
        problems.append("confidence %r" % v["confidence"])
    if not isinstance(v["conclusion"], str) or len(v["conclusion"].strip()) < 15:
        problems.append("conclusion must say what was settled")
    problems += kbverify.check_evidence(v["evidence"], True)
    if not isinstance(v["ghidra"], list) or not all(
            isinstance(g, dict) and kbverify.GHIDRA_ADDRESS.search(str(g.get("address", "")))
            and str(g.get("change", "")).strip() for g in v["ghidra"]):
        problems.append("ghidra entries need an address and a change")
    problems += check_regenerate(v["regenerate"])
    if v.get("_landed"):
        return problems
    edit_problems, _ = check_edits(v["edits"])
    problems += edit_problems
    rr = v.get("_reread")
    if rr is not None:
        if not isinstance(rr, dict) or rr.get("decision") not in ("confirm", "amend", "reject"):
            problems.append("_reread.decision must be confirm, amend or reject")
        elif not isinstance(rr.get("why"), str) or len(rr["why"].strip()) < 10:
            problems.append("_reread.why must say what was checked")
        elif rr["decision"] == "amend":
            p, _ = check_edits(rr.get("edits", []))
            problems += ["_reread: " + x for x in p]
            problems += ["_reread: " + x for x in check_regenerate(rr.get("regenerate", []))]
    return problems


def needs_reread(v):
    return bool(v["edits"]) or bool(v["ghidra"]) or v["confidence"] != "high"


def final(v):
    """(edits, ghidra, regenerate) that may land."""
    rr = v.get("_reread")
    if rr is None:
        return ([], [], []) if needs_reread(v) else (v["edits"], v["ghidra"], v["regenerate"])
    if rr["decision"] == "reject":
        return [], [], []
    if rr["decision"] == "confirm":
        return v["edits"], v["ghidra"], v["regenerate"]
    return rr.get("edits", []), rr.get("ghidra", v["ghidra"]), rr.get("regenerate", [])


def verdict_path(iid):
    return VERDICTS / ("%s.json" % iid)


def states(items):
    rows = []
    for it in items:
        p = verdict_path(it["id"])
        if not p.is_file():
            rows.append({"id": it["id"], "state": "missing"})
            continue
        v, err = kbverify._load(p)
        problems = [err] if err else check_verdict(v, it)
        rows.append({"id": it["id"], "state": "failing" if problems else "done",
                     **({"problems": problems} if problems else {})})
    return rows


# ----------------------------------------------------------------- landing

def apply(dry_run=False):
    import subprocess
    applied, refused, regen = [], [], []
    landed = []
    for it in load_items():
        p = verdict_path(it["id"])
        if not p.is_file():
            refused.append({"id": it["id"], "why": "no verdict"})
            continue
        v, err = kbverify._load(p)
        if v is None:
            refused.append({"id": it["id"], "why": err})
            continue
        if v.get("_landed"):
            continue
        problems = check_verdict(v, it)
        if problems:
            refused.append({"id": it["id"], "why": "verdict fails the gate: " + "; ".join(problems[:3])})
            continue
        if needs_reread(v) and "_reread" not in v:
            refused.append({"id": it["id"], "why": "no second reading yet"})
            continue
        edits, _ghidra, regenerate = final(v)
        errs, texts = check_edits(edits)
        if errs:
            refused.append({"id": it["id"], "why": "; ".join(errs[:3])})
            continue
        if not dry_run:
            for rel, text in texts.items():
                (REPO / rel).write_bytes(text.encode("utf-8"))
        applied += [{"id": it["id"], "file": e["file"]} for e in edits]
        regen += [r for r in regenerate if r not in regen]
        landed.append(it["id"])
    ran = []
    if not dry_run:
        chapters = sorted({int(n) for r in regen if r.startswith("chapter_refill:")
                           for n in r.split(":")[1].split(",")})
        order = ["global_text", "story", "chapter_refill", "chapters_index", "cut_content_index",
                 "data_skill"]
        for key in order:
            if key == "chapter_refill":
                if chapters:
                    cmd = REGENERATE[key] + [str(n) for n in chapters]
                else:
                    continue
            elif key in regen:
                cmd = REGENERATE[key]
            else:
                continue
            r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, encoding="utf-8")
            ran.append({"command": " ".join(cmd), "exit": r.returncode,
                        "tail": (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]})
        for iid in landed:
            v, _ = kbverify._load(verdict_path(iid))
            v["_landed"] = True
            verdict_path(iid).write_text(json.dumps(v, ensure_ascii=False, indent=1) + "\n",
                                         encoding="utf-8")
    return applied, refused, ran


def ghidra_changes():
    out = []
    for it in load_items():
        p = verdict_path(it["id"])
        v, _ = kbverify._load(p) if p.is_file() else (None, None)
        if not v or (needs_reread(v) and "_reread" not in v):
            continue
        for g in final(v)[1]:
            out.append(dict(g, id=it["id"]))
    return out


def summarize(stopped):
    items = load_items()
    rows = states(items)
    out = {"items": len(items), "fixes": sum(len(i["members"]) for i in items), "complete": 0,
           "unfinished": [r for r in rows if r["state"] != "done"], "by_status": {},
           "edits_landing": 0, "ghidra": [], "declined": [], "developer_questions": [],
           "rejected": [], "not_reread": [], "still_open": [], "stopped": stopped}
    for r in rows:
        if r["state"] != "done":
            continue
        out["complete"] += 1
        v, _ = kbverify._load(verdict_path(r["id"]))
        for m in v["members"]:
            out["by_status"][m["status"]] = out["by_status"].get(m["status"], 0) + 1
            if m["status"] in ("declined", "developer"):
                out["declined"].append({"id": r["id"], "ref": m["ref"], "status": m["status"],
                                        "note": m["note"]})
        edits, ghidra, _ = final(v)
        out["edits_landing"] += len(edits)
        out["ghidra"] += [dict(g, id=r["id"]) for g in ghidra]
        if str(v["developer_question"]).strip():
            out["developer_questions"].append({"id": r["id"], "question": v["developer_question"]})
        if (v.get("_reread") or {}).get("decision") == "reject":
            out["rejected"].append(r["id"])
        if needs_reread(v) and "_reread" not in v:
            out["not_reread"].append(r["id"])
        if v["confidence"] != "high" or str(v["open_question"]).strip():
            out["still_open"].append({"id": r["id"], "open_question": v["open_question"]})
    return out


# --------------------------------------------------------------------- CLI

def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("freeze").add_argument("--refresh", action="store_true")
    sub.add_parser("show").add_argument("id")
    sub.add_parser("pending").add_argument("--all", action="store_true")
    p = sub.add_parser("check")
    p.add_argument("--ids", nargs="*", default=None)
    p.add_argument("--json", action="store_true")
    sub.add_parser("rescan")
    p = sub.add_parser("report")
    p.add_argument("--date", required=True)
    p.add_argument("--stopped", default="")
    sub.add_parser("apply").add_argument("--dry-run", action="store_true")
    sub.add_parser("ghidra")
    a = ap.parse_args()
    if a.cmd == "freeze":
        if ITEMS.is_file() and not a.refresh:
            print(json.dumps({"frozen": True, "items": len(load_items())}))
            return 0
        items = build_items()
        WORK.mkdir(parents=True, exist_ok=True)
        ITEMS.write_text(json.dumps(items, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        print(json.dumps({"frozen": True, "items": len(items),
                          "fixes": sum(len(i["members"]) for i in items)}))
        return 0
    if a.cmd == "show":
        it = by_id().get(a.id)
        if it is None:
            print("no item %s" % a.id, file=sys.stderr)
            return 1
        print("item: %s\nitem_sha1: %s\nverdict_file: %s\n" % (it["id"], it["sha1"], verdict_path(it["id"])))
        print(render(it))
        return 0
    if a.cmd == "pending":
        rows = states(load_items())
        print(json.dumps([{"id": r["id"], "state": r["state"]} for r in rows] if a.all
                         else [r["id"] for r in rows if r["state"] != "done"], ensure_ascii=False))
        return 0
    if a.cmd == "check":
        table = by_id()
        wanted = a.ids or list(table)
        rows = states([table[i] for i in wanted if i in table])
        result = {"checked": len(wanted), "ok": sum(r["state"] == "done" for r in rows),
                  "missing": [r["id"] for r in rows if r["state"] == "missing"]
                  + [i for i in wanted if i not in table],
                  "failures": {r["id"]: r["problems"] for r in rows if r["state"] == "failing"}}
        result["gate_passed"] = not result["missing"] and not result["failures"]
        if a.json:
            print(json.dumps(result, ensure_ascii=False, indent=1))
        else:
            print("checked=%d ok=%d missing=%d failing=%d" % (
                result["checked"], result["ok"], len(result["missing"]), len(result["failures"])))
            for i, pr in result["failures"].items():
                print("  FAIL %s: %s" % (i, "; ".join(pr)))
            print("GATE %s" % ("PASSED" if result["gate_passed"] else "FAILED"))
        return 0 if result["gate_passed"] else 1
    if a.cmd == "rescan":
        todo = []
        for r in states(load_items()):
            if r["state"] != "done":
                continue
            v, _ = kbverify._load(verdict_path(r["id"]))
            if needs_reread(v) and "_reread" not in v:
                todo.append({"id": r["id"], "why": "%d edit(s), %d Ghidra change(s), confidence %s"
                             % (len(v["edits"]), len(v["ghidra"]), v["confidence"])})
        print(json.dumps(todo, ensure_ascii=False, indent=1))
        return 0
    if a.cmd == "report":
        s = summarize(a.stopped)
        RUNS.mkdir(parents=True, exist_ok=True)
        written = [RUNS / ("%s-kb-outside-summary.json" % a.date)]
        written[0].write_text(json.dumps(s, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        if not a.stopped:
            archive = []
            for it in load_items():
                p = verdict_path(it["id"])
                if p.is_file():
                    v, _ = kbverify._load(p)
                    if v is not None:
                        archive.append(dict(v, item=render(it)))
            written.append(RUNS / ("%s-kb-outside-verdicts.json" % a.date))
            written[1].write_text(json.dumps(archive, ensure_ascii=False, indent=1) + "\n",
                                  encoding="utf-8")
        print(json.dumps({"written": [str(p) for p in written], "complete": s["complete"],
                          "items": s["items"], "unfinished": [r["id"] for r in s["unfinished"]]},
                         ensure_ascii=False))
        return 0
    if a.cmd == "apply":
        applied, refused, ran = apply(a.dry_run)
        files = sorted({x["file"] for x in applied})
        # The unit tests of every tool folder an edit touched, for the gate run.
        tests = sorted({"python -m unittest " + t.relative_to(REPO).as_posix()
                        for f in files if f.startswith("tools/")
                        for t in (REPO / f).parent.glob("test_*.py")})
        print(json.dumps({"applied": len(applied), "refused": refused, "regenerated": ran,
                          "files": files, "tests_to_run": tests,
                          "src_changed": any(C_SOURCE.match(f) for f in files)},
                         ensure_ascii=False, indent=1))
        return 1 if refused or any(r["exit"] for r in ran) else 0
    if a.cmd == "ghidra":
        print(json.dumps(ghidra_changes(), ensure_ascii=False, indent=1))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
