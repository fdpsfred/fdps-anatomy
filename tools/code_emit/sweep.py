"""sweep.py -- the file side of ticket 22's closing sweep over emit_issues.json.

The sweep workflow (sweep_ticket22.js) has no filesystem of its own, and its
agents must not be trusted to say "done" about their own files (ADR-0007 5.1).
Everything that has to be mechanically right lives here instead: which entries
are one concern and which are mirrors of it, whether a cluster file covers every
concern exactly once, whether a finding file is complete, and transcribing a
finished finding into emit_issues.json without judging anything.

Subcommands:

    items [--force]     build workspace/code_emit/sweep/items.json (every entry
                        with its id and its root), index.json (the open roots,
                        full text, for the clustering agent) and labels.json (one
                        label-review cluster per address holding resolved roots).
                        Frozen once written: the sweep's own landings change
                        statuses, and the clusters are validated against the
                        list as it stood when the sweep began
    check-clusters      validate clusters.json against items.json
    plan                print every cluster with its state, as JSON
    show CID            print one cluster's concerns in full, mirrors included
    check-finding CID   validate the cluster's finding (and its rescan, if any)
    final-path CID      print the finding file that counts: the rescan when it
                        validates, else the first pass
    findings-index      one paragraph per investigated cluster, for the rescan
    apply CID [--code-failed REASON]
                        transcribe the cluster's final finding into
                        emit_issues.json
    handoff             print every entry handed to ticket 23 or 24, as JSON
    kb-notes            print every knowledge-base note the findings raised
    --selftest          prove the mirror resolution, validation and transcription

An entry's id is "<addr>#<n>", n its position in that address's array.  The
sweep only ever rewrites fields of existing entries and never appends, so the
ids are stable for the life of the sweep.
"""
import argparse
import copy
import json
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ISSUES = Path(__file__).resolve().parent / "data" / "emit_issues.json"
ROUTING = Path(__file__).resolve().parent / "data" / "routing.json"
SWEEP = ROOT / "workspace" / "code_emit" / "sweep"

OPEN_RULINGS = ("settled", "code_change", "handoff_t23", "handoff_t24", "open")
LABEL_RULINGS = ("label_ok", "label_corrected")
EMIT_REF_RX = re.compile(r"^\s*emit#(\d+)\b")


class SweepError(Exception):
    pass


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8", newline="\n")


# ------------------------------------------------------------------ items

def resolve_mirror(entries, idx):
    """Return the index of the entry this one mirrors, or None.

    Two spellings exist.  From t22-04 on a reviewer wrote "emit#N": the Nth
    entry of the same address whose `from` is "emit".  Before that the field
    held a quotation of the other entry's `what`, so the match is by text.  A
    same_as that matches nothing is not guessed at; the entry stands as its
    own concern and the caller reports it.
    """
    ref = entries[idx].get("same_as")
    if ref is None:
        return None
    emits = [i for i, e in enumerate(entries) if e.get("from") == "emit"]
    m = EMIT_REF_RX.match(ref)
    if m:
        n = int(m.group(1))
        return emits[n] if n < len(emits) else None
    probe = ref.strip().strip("'\"`").strip()
    probe = probe.rstrip(".").rstrip("…").rstrip(".")
    for length in (len(probe), 80, 40):
        head = probe[:length]
        if len(head) < 20:
            continue
        hits = [i for i, e in enumerate(entries)
                if i != idx and e.get("what", "").startswith(head)]
        if len(hits) == 1:
            return hits[0]
    return None


def build_items(issues, names):
    """Flatten emit_issues.json into items with their roots resolved."""
    items = []
    unresolved = []
    for addr, entries in issues.items():
        for i, entry in enumerate(entries):
            target = resolve_mirror(entries, i)
            if entry.get("same_as") is not None and target is None:
                unresolved.append(f"{addr}#{i}")
            items.append({
                "id": f"{addr}#{i}",
                "addr": addr,
                "function": names.get(addr, {}).get("name", ""),
                "file": names.get(addr, {}).get("target", ""),
                "from": entry["from"],
                "status": entry["status"],
                "root": f"{addr}#{target}" if target is not None else None,
            })
    by_id = {it["id"]: it for it in items}
    # A mirror of a mirror collapses to the first entry that mirrors nothing.
    for it in items:
        seen = {it["id"]}
        while it["root"] and by_id[it["root"]]["root"]:
            nxt = by_id[it["root"]]["root"]
            if nxt in seen:
                raise SweepError(f"same_as cycle through {it['id']}")
            seen.add(nxt)
            it["root"] = nxt
    return items, unresolved


def cmd_items(args):
    # Built once per sweep and then frozen.  Landing rewrites statuses in
    # emit_issues.json, so a rebuild after the first landing would no longer
    # list those concerns as open, and every cluster holding one would fail
    # validation on resume.
    if (SWEEP / "items.json").exists() and not args.force:
        print(json.dumps({"frozen": True, "note": "items.json already exists; --force rebuilds"}))
        return 0
    issues = read_json(ISSUES)
    names = read_json(ROUTING)["functions"]
    items, unresolved = build_items(issues, names)
    by_id = {it["id"]: it for it in items}
    roots = [it for it in items if it["root"] is None]
    open_roots = [it for it in roots if it["status"] == "open"]
    index = []
    for it in open_roots:
        addr, n = it["id"].split("#")
        entry = issues[addr][int(n)]
        index.append({
            "id": it["id"], "function": it["function"], "file": it["file"],
            "from": it["from"], "what": entry["what"], "needs": entry["needs"],
            "mirrors": [m["id"] for m in items if m["root"] == it["id"]],
        })
    labels = {}
    for it in roots:
        if it["status"] == "resolved":
            labels.setdefault("L" + it["addr"], []).append(it["id"])
    status_split = [m["id"] for m in items
                    if m["root"] and m["status"] != by_id[m["root"]]["status"]]
    write_json(SWEEP / "items.json", {"items": items})
    write_json(SWEEP / "index.json", {"open_roots": index})
    write_json(SWEEP / "labels.json", {"clusters": [
        {"id": cid, "kind": "label", "items": ids} for cid, ids in sorted(labels.items())]})
    print(json.dumps({
        "entries": len(items), "roots": len(roots), "open_roots": len(open_roots),
        "resolved_roots": len(roots) - len(open_roots),
        "label_clusters": len(labels),
        "mirrors_unresolved": unresolved,
        "mirrors_with_status_unlike_root": status_split,
    }, indent=2))


# --------------------------------------------------------------- clusters

def load_items():
    return {it["id"]: it for it in read_json(SWEEP / "items.json")["items"]}


def validate_clusters(clusters, items):
    """Every open root in exactly one open cluster, and nothing else in any."""
    errors = []
    want = {i for i, it in items.items() if it["root"] is None and it["status"] == "open"}
    seen = {}
    ids = set()
    for c in clusters:
        cid = c.get("id", "")
        if not re.fullmatch(r"c\d{2,3}", cid):
            errors.append(f"cluster id {cid!r} is not c<digits>")
        if cid in ids:
            errors.append(f"cluster id {cid} used twice")
        ids.add(cid)
        if not str(c.get("root_cause", "")).strip():
            errors.append(f"{cid}: root_cause is empty")
        if not c.get("items"):
            errors.append(f"{cid}: no items")
        for i in c.get("items", []):
            if i not in want:
                errors.append(f"{cid}: {i} is not an open root concern")
            elif i in seen:
                errors.append(f"{i} is in both {seen[i]} and {cid}")
            else:
                seen[i] = cid
    missing = sorted(want - set(seen))
    if missing:
        errors.append("not in any cluster: " + ", ".join(missing))
    return errors


def all_clusters():
    out = []
    path = SWEEP / "clusters.json"
    if path.exists():
        for c in read_json(path)["clusters"]:
            out.append(dict(c, kind="open"))
    out.extend(read_json(SWEEP / "labels.json")["clusters"])
    return out


def cluster_by_id(cid):
    for c in all_clusters():
        if c["id"] == cid:
            return c
    raise SweepError(f"no cluster {cid}")


def cmd_check_clusters(_args):
    errors = validate_clusters(read_json(SWEEP / "clusters.json")["clusters"], load_items())
    print(json.dumps({"ok": not errors, "errors": errors}, indent=2))
    return 0 if not errors else 1


# --------------------------------------------------------------- findings

def finding_path(cid, rescan=False):
    return SWEEP / "findings" / f"{cid}{'.rescan' if rescan else ''}.json"


def validate_finding(finding, cluster):
    errors = []
    kind = cluster["kind"]
    allowed = OPEN_RULINGS if kind == "open" else LABEL_RULINGS
    if finding.get("cluster") != cluster["id"]:
        errors.append(f"cluster field is {finding.get('cluster')!r}, not {cluster['id']}")
    for key in ("root_cause", "investigation"):
        if not str(finding.get(key, "")).strip():
            errors.append(f"{key} is empty")
    rulings = finding.get("rulings")
    if not isinstance(rulings, list):
        return errors + ["rulings is not a list"]
    got = {}
    for r in rulings:
        rid = r.get("id")
        if rid not in cluster["items"]:
            errors.append(f"ruling for {rid}, which is not in this cluster")
            continue
        if rid in got:
            errors.append(f"{rid} ruled twice")
        got[rid] = r
        if r.get("ruling") not in allowed:
            errors.append(f"{rid}: ruling {r.get('ruling')!r} not one of {allowed}")
        if not str(r.get("answer", "")).strip():
            errors.append(f"{rid}: answer is empty")
        if kind == "open" and not isinstance(r.get("blocking"), bool):
            errors.append(f"{rid}: blocking must be true or false")
        if r.get("ruling") == "code_change" and r.get("blocking") is not True:
            errors.append(f"{rid}: code_change without a behaviour difference (blocking false)"
                          " -- a source-literal preference is never a code change (ADR-0001)")
        if r.get("ruling") == "label_corrected" and not str(r.get("label_correction", "")).strip():
            errors.append(f"{rid}: label_corrected needs label_correction")
    for i in cluster["items"]:
        if i not in got:
            errors.append(f"{i} has no ruling")
    changes = finding.get("code_changes", [])
    wants_code = {r["id"] for r in rulings if r.get("ruling") == "code_change"}
    covered = set()
    for ch in changes:
        for key in ("file", "function", "change", "behaviour_difference", "test"):
            if not str(ch.get(key, "")).strip():
                errors.append(f"code_change {ch.get('function', '?')}: {key} is empty")
        covered.update(ch.get("ids", []))
    if wants_code - covered:
        errors.append("code_change rulings with no code_changes entry: "
                      + ", ".join(sorted(wants_code - covered)))
    if covered - wants_code:
        errors.append("code_changes entries for ids not ruled code_change: "
                      + ", ".join(sorted(covered - wants_code)))
    for key in ("ghidra_fixes", "kb_notes"):
        if not isinstance(finding.get(key, []), list):
            errors.append(f"{key} is not a list")
    return errors


def final_finding(cid):
    """The rescan when it exists and validates, else the first pass.

    A rescan file that does not validate is not silently dropped: `plan`
    reports needs_rescan for it, and the workflow lists it as unfinished.
    """
    rp = finding_path(cid, True)
    if rp.exists():
        try:
            f = read_json(rp)
            if not validate_finding(f, cluster_by_id(cid)):
                return f, True
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass
    p = finding_path(cid)
    if p.exists():
        return read_json(p), False
    return None, False


def cmd_check_finding(args):
    cluster = cluster_by_id(args.cid)
    report = {"cluster": args.cid}
    for rescan in (False, True):
        p = finding_path(args.cid, rescan)
        key = "rescan" if rescan else "finding"
        if not p.exists():
            report[key] = "absent"
            continue
        try:
            errors = validate_finding(read_json(p), cluster)
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            errors = [f"unreadable: {e}"]
        report[key] = "ok" if not errors else errors
    print(json.dumps(report, indent=2, ensure_ascii=False))
    final = report["rescan"] if report["rescan"] != "absent" else report["finding"]
    return 0 if final == "ok" else 1


def cmd_final_path(args):
    cluster_by_id(args.cid)
    f, rescanned = final_finding(args.cid)
    if f is None:
        raise SweepError(f"{args.cid} has no finding")
    print(finding_path(args.cid, rescanned))


def cmd_show(args):
    cluster = cluster_by_id(args.cid)
    issues = read_json(ISSUES)
    items = load_items()
    out = {"cluster": cluster["id"], "kind": cluster["kind"],
           "root_cause_hypothesis": cluster.get("root_cause", ""), "concerns": []}
    for rid in cluster["items"]:
        addr, n = rid.split("#")
        mirrors = []
        for mid, it in items.items():
            if it["root"] == rid:
                maddr, mn = mid.split("#")
                mirrors.append(dict(issues[maddr][int(mn)], id=mid))
        out["concerns"].append({
            "id": rid, "function": items[rid]["function"], "file": items[rid]["file"],
            "entry": issues[addr][int(n)], "mirrors": mirrors})
    print(json.dumps(out, indent=2, ensure_ascii=False))


def ruling_counts(finding):
    counts = {}
    for r in finding.get("rulings", []):
        counts[r.get("ruling")] = counts.get(r.get("ruling"), 0) + 1
    return counts


def cmd_findings_index(_args):
    out = []
    for c in all_clusters():
        f, rescan = final_finding(c["id"])
        if f is None:
            continue
        out.append({"cluster": c["id"], "kind": c["kind"], "rescanned": rescan,
                    "root_cause": f.get("root_cause", ""), "rulings": ruling_counts(f),
                    "answers": {r["id"]: r.get("answer", "")[:400] for r in f.get("rulings", [])}})
    print(json.dumps(out, indent=2, ensure_ascii=False))


# ------------------------------------------------------------------ apply

def landed(issues, cluster):
    for rid in cluster["items"]:
        addr, n = rid.split("#")
        if (issues[addr][int(n)].get("sweep") or {}).get("cluster") != cluster["id"]:
            return False
    return True


def apply_finding(issues, items, cluster, finding, rescanned, code_failed=None):
    """Transcribe one finished finding.  Pure: returns a new issues dict."""
    issues = copy.deepcopy(issues)
    passname = "rescan" if rescanned else "first"
    for r in finding["rulings"]:
        rid = r["id"]
        addr, n = rid.split("#")
        entry = issues[addr][int(n)]
        ruling = r["ruling"]
        mark = {"cluster": cluster["id"], "ruling": ruling, "pass": passname}
        if cluster["kind"] == "label":
            entry["label_review"] = r["answer"]
            if ruling == "label_corrected":
                entry["label_correction"] = r["label_correction"]
        else:
            if entry.get("answer"):
                entry["earlier_answer"] = entry["answer"]
            entry["answer"] = r["answer"]
            entry["blocking"] = r["blocking"]
            if ruling in ("settled", "code_change"):
                entry["status"] = "resolved"
            elif ruling in ("handoff_t23", "handoff_t24"):
                entry["status"] = "handoff"
                entry["handoff_to"] = ruling[-2:]
            else:
                entry["status"] = "open"
            if ruling == "code_change" and code_failed:
                entry["status"] = "open"
                mark["code_failed"] = code_failed
        entry["sweep"] = mark
        for mid, it in items.items():
            if it["root"] != rid:
                continue
            maddr, mn = mid.split("#")
            mirror = issues[maddr][int(mn)]
            mirror["status"] = entry["status"]
            if "handoff_to" in entry:
                mirror["handoff_to"] = entry["handoff_to"]
            mirror["sweep"] = dict(mark, mirror_of=rid)
    return issues


def cmd_apply(args):
    cluster = cluster_by_id(args.cid)
    finding, rescanned = final_finding(args.cid)
    if finding is None:
        raise SweepError(f"{args.cid} has no finding")
    errors = validate_finding(finding, cluster)
    if errors:
        raise SweepError(f"{args.cid} finding is invalid: " + "; ".join(errors))
    issues = read_json(ISSUES)
    new = apply_finding(issues, load_items(), cluster, finding, rescanned, args.code_failed)
    write_json(ISSUES, new)
    print(json.dumps({"cluster": args.cid, "applied": len(finding["rulings"]),
                      "rulings": ruling_counts(finding), "rescanned": rescanned,
                      "code_failed": bool(args.code_failed)}))


def cmd_plan(_args):
    issues = read_json(ISSUES)
    out = []
    def valid_at(path, cluster):
        try:
            return path.exists() and not validate_finding(read_json(path), cluster)
        except (json.JSONDecodeError, UnicodeDecodeError):
            return False

    for c in all_clusters():
        f, rescanned = final_finding(c["id"])
        first_valid = valid_at(finding_path(c["id"]), c)
        rescan_valid = valid_at(finding_path(c["id"], True), c)
        valid = rescan_valid or first_valid
        out.append({
            "id": c["id"], "kind": c["kind"], "size": len(c["items"]),
            # The first pass is done only when its file validates; an agent
            # that said it finished but left a partial file has not.
            "first_valid": first_valid,
            "rescanned": rescanned,
            "rescan_valid": rescan_valid,
            "finding_valid": valid,
            "needs_rescan": bool(first_valid and not rescan_valid and c["kind"] == "open"
                                 and ruling_counts(read_json(finding_path(c["id"]))).get("open")),
            "has_code_change": bool(valid and f.get("code_changes")),
            "has_ghidra_fixes": bool(valid and f.get("ghidra_fixes")),
            "landed": landed(issues, c),
        })
    print(json.dumps({"clusters": out}, indent=2))


def cmd_handoff(_args):
    issues = read_json(ISSUES)
    items = load_items()
    out = {"23": [], "24": []}
    for rid, it in items.items():
        if it["root"]:
            continue
        addr, n = rid.split("#")
        e = issues[addr][int(n)]
        if e.get("status") == "handoff":
            out[e["handoff_to"]].append({"id": rid, "function": it["function"],
                                          "what": e["what"], "answer": e["answer"]})
    print(json.dumps(out, indent=2, ensure_ascii=False))


def cmd_kb_notes(_args):
    out = []
    for c in all_clusters():
        f, _ = final_finding(c["id"])
        for note in (f or {}).get("kb_notes", []):
            out.append(dict(note, cluster=c["id"]))
    print(json.dumps(out, indent=2, ensure_ascii=False))


# --------------------------------------------------------------- selftest

def selftest():
    issues = {
        "00010000": [
            {"from": "emit", "what": "Alpha concern about an index bound here.", "needs": "x", "status": "open"},
            {"from": "emit", "what": "Beta concern, already answered once.", "needs": "y",
             "status": "resolved", "answer": "old"},
            {"from": "review", "what": "mine", "needs": "z", "status": "open", "same_as": "emit#0"},
            {"from": "review", "what": "quoted", "needs": "z", "status": "resolved",
             "same_as": "'Beta concern, already answered once.'"},
            {"from": "review", "what": "dangling", "needs": "z", "status": "open", "same_as": "emit#7"},
            {"from": "review", "what": "chained", "needs": "z", "status": "open",
             "same_as": "emit#1 -- the second one"},
        ],
    }
    names = {"00010000": {"name": "fdps_x", "target": "x.c"}}
    items, unresolved = build_items(issues, names)
    by = {i["id"]: i for i in items}
    assert by["00010000#2"]["root"] == "00010000#0", "emit#N resolves to the Nth emit entry"
    assert by["00010000#3"]["root"] == "00010000#1", "a quoted same_as resolves by text"
    assert by["00010000#5"]["root"] == "00010000#1", "emit#N followed by prose still resolves"
    assert unresolved == ["00010000#4"] and by["00010000#4"]["root"] is None, \
        "an unmatched same_as stands alone and is reported, never guessed"

    open_roots = {"00010000#0", "00010000#4"}
    ok = [{"id": "c01", "root_cause": "r", "items": ["00010000#0", "00010000#4"]}]
    assert validate_clusters(ok, by) == []
    assert any("not in any cluster" in e for e in
               validate_clusters([{"id": "c01", "root_cause": "r", "items": ["00010000#0"]}], by))
    assert any("both" in e for e in validate_clusters(
        ok + [{"id": "c02", "root_cause": "r", "items": ["00010000#0"]}], by))
    assert any("not an open root" in e for e in validate_clusters(
        [{"id": "c01", "root_cause": "r", "items": sorted(open_roots) + ["00010000#2"]}], by)), \
        "a mirror is never clustered on its own"

    cluster = {"id": "c01", "kind": "open", "items": ["00010000#0", "00010000#4"]}
    good = {"cluster": "c01", "root_cause": "r", "investigation": "i",
            "rulings": [
                {"id": "00010000#0", "ruling": "code_change", "answer": "a", "blocking": True},
                {"id": "00010000#4", "ruling": "handoff_t24", "answer": "b", "blocking": False}],
            "code_changes": [{"ids": ["00010000#0"], "file": "src/x.c", "function": "fdps_x",
                              "change": "c", "behaviour_difference": "d", "test": "t"}],
            "ghidra_fixes": [], "kb_notes": []}
    assert validate_finding(good, cluster) == []
    bad = copy.deepcopy(good)
    bad["rulings"][0]["blocking"] = False
    assert any("ADR-0001" in e for e in validate_finding(bad, cluster)), \
        "a code change that fixes no behaviour difference is refused"
    bad = copy.deepcopy(good)
    bad["rulings"].pop()
    assert any("has no ruling" in e for e in validate_finding(bad, cluster)), \
        "a finding that skips a concern is incomplete, not done"
    bad = copy.deepcopy(good)
    bad["code_changes"] = []
    assert any("no code_changes entry" in e for e in validate_finding(bad, cluster))

    new = apply_finding(issues, by, cluster, good, False)
    e0, e2, e4 = new["00010000"][0], new["00010000"][2], new["00010000"][4]
    assert e0["status"] == "resolved" and e0["sweep"]["cluster"] == "c01"
    assert e2["status"] == "resolved" and e2["sweep"]["mirror_of"] == "00010000#0", \
        "a mirror follows its root"
    assert e4["status"] == "handoff" and e4["handoff_to"] == "24"
    assert issues["00010000"][0]["status"] == "open", "apply does not mutate its input"
    assert landed(new, cluster) and not landed(issues, cluster)
    failed = apply_finding(issues, by, cluster, good, False, code_failed="review rejected")
    assert failed["00010000"][0]["status"] == "open", "a code change that never landed is not resolved"
    assert failed["00010000"][0]["sweep"]["code_failed"] == "review rejected"

    label = {"id": "L00010000", "kind": "label", "items": ["00010000#1"]}
    lf = {"cluster": "L00010000", "root_cause": "r", "investigation": "i",
          "rulings": [{"id": "00010000#1", "ruling": "label_corrected", "answer": "x",
                       "label_correction": "not blocking"}]}
    assert validate_finding(lf, label) == []
    new = apply_finding(issues, by, label, lf, False)
    e1 = new["00010000"][1]
    assert e1["answer"] == "old" and e1["status"] == "resolved", \
        "a label review never rewrites the conclusion"
    assert e1["label_correction"] == "not blocking"

    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "x.json"
        write_json(p, issues)
        assert read_json(p) == issues
        assert b"\r\n" not in p.read_bytes()
    print("sweep.py selftest: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("items").add_argument("--force", action="store_true")
    sub.add_parser("check-clusters")
    sub.add_parser("plan")
    for name in ("show", "check-finding", "final-path"):
        sub.add_parser(name).add_argument("cid")
    sub.add_parser("findings-index")
    p = sub.add_parser("apply")
    p.add_argument("cid")
    p.add_argument("--code-failed", default=None)
    sub.add_parser("handoff")
    sub.add_parser("kb-notes")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return 0
    handlers = {
        "items": cmd_items, "check-clusters": cmd_check_clusters, "plan": cmd_plan,
        "show": cmd_show, "check-finding": cmd_check_finding, "final-path": cmd_final_path,
        "findings-index": cmd_findings_index, "apply": cmd_apply,
        "handoff": cmd_handoff, "kb-notes": cmd_kb_notes,
    }
    if args.cmd not in handlers:
        ap.print_help()
        return 2
    sys.stdout.reconfigure(encoding="utf-8")
    try:
        return handlers[args.cmd](args) or 0
    except SweepError as e:
        print(f"sweep.py: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
