"""land.py -- write settled data verdicts into src/, and nothing else.

This is the transcription stage of ticket 23 (ADR-0007, rule 2).  A judge
agent has already decided, for one global, what its C definition is and why;
the verdict sits in workspace/data_emit/verdicts/<symbol>.json.  This script
puts that definition where routing.json says it goes, byte for byte as the
verdict spells it, and refuses -- loudly, per symbol -- anything it cannot place
without deciding something itself (ADR-0007 5.3).

Where a definition goes inside its .c: one block right after the file's
#include lines, opened and closed by the two marker comments below.  Inside the
block the entries are kept in the original image's address order, initialised
ones first.  That order is load-bearing, not tidiness: Watcom lays explicitly
initialised globals of one translation unit out in _DATA in source order, so
address order in the source is what reproduces the original's adjacency where a
reader depends on it (tools/data_emit/layout_probe.py measured this;
rebuild_info/data_emit.md owns the conclusion).  Tentative (zero) definitions
go to _BSS in an order the toolchain chooses, so where they sit in the block
does not matter and they come after.

Everything a landing touches:
  src/<target>.c     the definition, plus the #include lines it needs
  src/<stem>.h       an extern the verdict says must change type or qualifier
  tools/data_emit/data/manifest.json   one entry per landed symbol

Subcommands:
  validate [SYM ...]   check verdict files are complete and consistent
  plan                 which verdicts are ready, grouped by target, as JSON
  apply --target X.c   land every ready verdict routed to X.c
  --selftest

Usage: python tools/data_emit/land.py <subcommand> [...]
"""
import argparse
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
VERDICTS = ROOT / "workspace" / "data_emit" / "verdicts"
MANIFEST = ROOT / "tools" / "data_emit" / "data" / "manifest.json"
ROUTING = ROOT / "tools" / "code_emit" / "data" / "routing.json"

BEGIN = "/* Global data owned by this file, in the original image's address order."
BEGIN_TAIL = (" * Initialised definitions come first and their order is the layout\n"
              " * (rebuild_info/data_emit.md); zero-filled ones follow. */")
END = "/* End of global data. */"

KINDS = ("zero", "initialized")
CONFIDENCE = ("high", "medium", "low")
ENTRY_RX = re.compile(r"^/\* ([0-9a-f]{8})[.:]", re.M)


class LandError(Exception):
    pass


# ------------------------------------------------------------------ verdicts

def load_routing():
    return json.loads(ROUTING.read_text(encoding="utf-8"))


def routing_by_symbol(routing):
    return {v["symbol"]: dict(v, addr=a) for a, v in routing["globals"].items()}


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", " ", text, flags=re.S)


def validate(v, routing_rows):
    """Problems with one verdict, as a list of strings.  Empty means landable."""
    p = []
    sym = v.get("symbol")
    if not sym:
        return ["no symbol"]
    row = routing_rows.get(sym)
    if row is None:
        return ["routing.json has no global named %s" % sym]
    if v.get("addr") != row["addr"]:
        p.append("addr %r, routing says %s" % (v.get("addr"), row["addr"]))
    if v.get("reroute"):
        p.append("asks to reroute to %s; the reroute has to land first"
                 % v["reroute"].get("to"))
    elif v.get("target") != row["target"]:
        p.append("target %r, routing says %s" % (v.get("target"), row["target"]))
    if v.get("kind") not in KINDS:
        p.append("kind %r is not one of %s" % (v.get("kind"), KINDS))
    if v.get("confidence") not in CONFIDENCE:
        p.append("confidence %r" % v.get("confidence"))
    if not isinstance(v.get("size"), int) or v["size"] <= 0:
        p.append("size %r" % v.get("size"))
    d = v.get("definition") or ""
    body = strip_comments(d)
    if not body.strip().endswith(";"):
        p.append("definition does not end with ';'")
    if re.search(r"\bextern\b", body):
        p.append("definition contains extern")
    if "#" in body:
        p.append("definition contains a preprocessor line")
    # The symbol is defined exactly once, and as a definition.
    # A plain declarator ("int x[6] =", "int x;") or a function-pointer
    # array's ("void (*x[30])(void) =").
    defs = re.findall(r"\b%s\b\s*(\[[^\]]*\])?\s*(=|;|\))" % re.escape(sym), body)
    if len(defs) != 1:
        p.append("definition names %s %d times as a declarator, expected 1"
                 % (sym, len(defs)))
    if v.get("kind") == "zero" and re.search(r"\b%s\b[^;]*=" % re.escape(sym), body):
        p.append("kind zero but the definition has an initialiser")
    if v.get("kind") == "initialized" and not re.search(
            r"\b%s\b[^;]*=" % re.escape(sym), body):
        p.append("kind initialized but the definition has no initialiser")
    if not (v.get("comment") or "").strip():
        p.append("no comment")
    if "*/" in (v.get("comment") or ""):
        p.append("comment contains */")
    hdr = v.get("header") or {}
    if hdr.get("replace_with") and not hdr.get("current"):
        p.append("header.replace_with without header.current")
    lay = v.get("layout") or {}
    pad = lay.get("zero_pad_after")
    if pad not in (None, False, 0) and (not isinstance(pad, int) or pad < 0):
        p.append("layout.zero_pad_after %r is not a byte count" % pad)
    elif pad and not re.search(r"\bstatic\b[^;]*=[^;]*;\s*$", body.strip()):
        p.append("layout.zero_pad_after needs the definition to END with its own "
                 "static initialised zero pad")
    if lay.get("follows") or lay.get("zero_guard_before") or pad:
        if v.get("kind") != "initialized":
            p.append("a layout constraint needs kind initialized (only _DATA keeps "
                     "source order)")
    for inc in v.get("includes") or []:
        if not re.fullmatch(r"[a-z0-9_]{1,8}\.h", inc):
            p.append("include %r is not an 8.3 project header name" % inc)
    if not (v.get("basis") or "").strip():
        p.append("no basis")
    return p


def read_verdict(sym):
    path = VERDICTS / ("%s.json" % sym)
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except ValueError:
        return {"symbol": sym, "_unreadable": True}


def all_verdicts():
    if not VERDICTS.is_dir():
        return {}
    out = {}
    for path in sorted(VERDICTS.glob("*.json")):
        try:
            v = json.loads(path.read_text(encoding="utf-8"))
        except ValueError:
            v = {"symbol": path.stem, "_unreadable": True}
        out[path.stem] = v
    return out


# ---------------------------------------------------------------- the block

def entry_text(v):
    """One entry: the address-led comment, then the definition verbatim."""
    comment = " ".join(v["comment"].split())
    lines = wrap("/* %s. %s */" % (v["addr"], comment), 79)
    return "\n".join(lines) + "\n" + v["definition"].rstrip() + "\n"


def wrap(text, width):
    words = text.split(" ")
    lines, cur = [], ""
    for w in words:
        if cur and len(cur) + 1 + len(w) > width:
            lines.append(cur)
            cur = "   " + w
        else:
            cur = (cur + " " + w) if cur else w
    if cur:
        lines.append(cur)
    return lines


def split_block(text):
    """(before, entries {addr: text}, after) or None when there is no block."""
    i = text.find(BEGIN)
    if i < 0:
        return None
    j = text.find(END, i)
    if j < 0:
        raise LandError("block opens but never closes")
    head_end = text.find("*/", i) + 2
    body = text[head_end:j]
    entries = {}
    starts = [m.start() for m in ENTRY_RX.finditer(body)]
    for k, s in enumerate(starts):
        e = starts[k + 1] if k + 1 < len(starts) else len(body)
        chunk = body[s:e].strip("\n") + "\n"
        entries[body[s + 3:s + 11]] = chunk
    return text[:i], entries, text[j + len(END):]


def render_block(entries, kinds):
    init = sorted(a for a in entries if kinds.get(a) == "initialized")
    zero = sorted(a for a in entries if kinds.get(a) != "initialized")
    parts = [BEGIN + "\n" + BEGIN_TAIL + "\n"]
    for a in init + zero:
        parts.append("\n" + entries[a])
    parts.append("\n" + END)
    return "".join(parts)


def kinds_in_block(entries):
    """Kind of each existing entry, read back from the text itself."""
    out = {}
    for a, chunk in entries.items():
        body = strip_comments(chunk)
        out[a] = "initialized" if "=" in body else "zero"
    return out


def insert_after_includes(text, block):
    lines = text.split("\n")
    last = -1
    for i, l in enumerate(lines):
        if l.startswith("#include"):
            last = i
    if last < 0:
        raise LandError("no #include line to place the block after")
    return "\n".join(lines[:last + 1] + ["", block] + lines[last + 1:])


def ensure_includes(text, names):
    have = set(re.findall(r'^#include\s+"([^"]+)"', text, re.M))
    missing = [n for n in names if n not in have]
    if not missing:
        return text
    lines = text.split("\n")
    last = max(i for i, l in enumerate(lines) if l.startswith("#include"))
    return "\n".join(lines[:last + 1] + ['#include "%s"' % n for n in missing]
                     + lines[last + 1:])


GAMEDATA_HEAD = """/* gamedata.c -- the game-state globals more than one file reads.
 *
 * A global lands here because it is shared, not because it is miscellaneous:
 * rebuild_info/code_layout.md routes a global read by two or more files here
 * and one read by a single file to that file.  The declarations are in
 * gamedata.h, which this file includes so that every definition is checked
 * against its declaration by the compiler.
 */
#include "gamedata.h"
"""


def new_file_text(target):
    stem = Path(target).stem
    if stem == "gamedata":
        return GAMEDATA_HEAD
    raise LandError("src/%s does not exist and only gamedata.c is created here"
                    % target)


def land_into(text, verdicts):
    """Return text with every verdict's entry placed in the block."""
    split = split_block(text)
    if split is None:
        text = insert_after_includes(text, BEGIN + "\n" + BEGIN_TAIL + "\n\n" + END)
        split = split_block(text)
    before, entries, after = split
    kinds = kinds_in_block(entries)
    for v in verdicts:
        entries[v["addr"]] = entry_text(v)
        kinds[v["addr"]] = v["kind"]
    return before + render_block(entries, kinds) + after


def check_layout_order(verdicts_by_sym, target_entries_kinds):
    """A `follows` constraint needs its predecessor to be the entry right
    before it among the file's initialised definitions."""
    problems = []
    init = sorted(a for a, k in target_entries_kinds.items() if k == "initialized")
    addr_to_sym = {v["addr"]: s for s, v in verdicts_by_sym.items()}
    for s, v in verdicts_by_sym.items():
        prev = (v.get("layout") or {}).get("follows")
        if not prev:
            continue
        pv = verdicts_by_sym.get(prev)
        if pv is None:
            problems.append("%s follows %s, which is not landing in the same file"
                            % (s, prev))
            continue
        k = init.index(v["addr"])
        if k == 0 or addr_to_sym.get(init[k - 1]) != prev:
            problems.append("%s follows %s, but the initialised definition before it "
                            "in the file is %s" % (s, prev,
                                                   addr_to_sym.get(init[k - 1], init[k - 1]) if k else "nothing"))
    return problems


# -------------------------------------------------------------------- apply

def load_manifest():
    if MANIFEST.is_file():
        return json.loads(MANIFEST.read_text(encoding="utf-8"))
    return {"_doc": ("Every global ticket 23 has landed in src/: where it came from, "
                     "how it is defined, the layout it has to keep and the basis of "
                     "the decision.  Written by tools/data_emit/land.py; read by "
                     "tools/data_emit/check_data.py, which compares each entry's "
                     "linked bytes with the shipped image."),
            "symbols": []}


def manifest_entry(v):
    return {"symbol": v["symbol"], "addr": v["addr"], "size": v["size"],
            "target": v["target"], "kind": v["kind"],
            "layout": {k: val for k, val in (v.get("layout") or {}).items()
                       if k in ("follows", "zero_guard_before", "zero_pad_after") and val},
            "confidence": v["confidence"], "basis": " ".join(v["basis"].split()),
            "handoff": [h.get("id") for h in (v.get("handoff") or []) if h.get("id")]}


def apply_target(target, src_dir=SRC, manifest_path=MANIFEST, verdicts=None,
                 routing=None):
    """Land every valid verdict routed to `target`.  Returns a report dict."""
    routing = routing or load_routing()
    rows = routing_by_symbol(routing)
    verdicts = verdicts if verdicts is not None else all_verdicts()
    mine, refused = [], []
    for sym, v in sorted(verdicts.items()):
        row = rows.get(sym)
        if row is None or row["target"] != target:
            continue
        if v.get("_unreadable"):
            refused.append({"symbol": sym, "why": ["verdict file is not valid JSON"]})
            continue
        probs = validate(v, rows)
        if probs:
            refused.append({"symbol": sym, "why": probs})
        else:
            mine.append(v)
    if not mine:
        return {"target": target, "landed": [], "refused": refused}

    path = src_dir / target
    text = path.read_text(encoding="utf-8") if path.is_file() else new_file_text(target)
    stem = Path(target).stem
    includes = ["%s.h" % stem]
    for v in mine:
        for inc in v.get("includes") or []:
            if inc not in includes:
                includes.append(inc)
    text = ensure_includes(text, includes)
    for inc in includes:
        if not (src_dir / inc).is_file():
            raise LandError("%s needs %s, which is not in src/" % (target, inc))
    text = land_into(text, mine)

    _, entries, _ = split_block(text)
    by_sym = {v["symbol"]: v for v in mine}
    order_problems = check_layout_order(by_sym, kinds_in_block(entries))
    if order_problems:
        raise LandError("; ".join(order_problems))

    header_edits = []
    for v in mine:
        h = v.get("header") or {}
        if not h.get("replace_with"):
            continue
        hpath = src_dir / (h.get("file") or "%s.h" % stem)
        htext = hpath.read_text(encoding="utf-8")
        if h["current"] not in htext and htext.count(h["replace_with"]) == 1:
            continue                        # landed before; nothing left to do
        n = htext.count(h["current"])
        if n != 1:
            raise LandError("%s: header line %r occurs %d times in %s"
                            % (v["symbol"], h["current"], n, hpath.name))
        header_edits.append((hpath, htext.replace(h["current"], h["replace_with"])))

    path.write_text(text, encoding="utf-8", newline="\n")
    for hpath, htext in header_edits:
        hpath.write_text(htext, encoding="utf-8", newline="\n")
    man = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else load_manifest()
    keep = [e for e in man["symbols"] if e["symbol"] not in by_sym]
    man["symbols"] = sorted(keep + [manifest_entry(v) for v in mine],
                            key=lambda e: e["addr"])
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(man, indent=2, ensure_ascii=False) + "\n",
                             encoding="utf-8", newline="\n")
    return {"target": target, "landed": sorted(by_sym), "refused": refused,
            "headers_changed": [str(h.relative_to(src_dir)) for h, _ in header_edits]}


def rescan_list():
    """Verdicts the rescan pass has to read again (ADR-0007 rule 4): low
    confidence, or an open question left, and not re-read yet."""
    landed = {e["symbol"] for e in load_manifest()["symbols"]}
    out = []
    for sym, v in all_verdicts().items():
        if v.get("_unreadable") or sym in landed or v.get("rescanned"):
            continue
        if v.get("confidence") == "low" or (v.get("open_questions") or []):
            out.append({"symbol": sym, "addr": v.get("addr"),
                        "confidence": v.get("confidence"),
                        "open_questions": len(v.get("open_questions") or [])})
    return sorted(out, key=lambda r: r["addr"] or "")


def ghidra_fix_list():
    """Ghidra corrections proposed by landed verdicts and not applied yet."""
    landed = {e["symbol"] for e in load_manifest()["symbols"]}
    out = []
    for sym, v in all_verdicts().items():
        if sym not in landed or v.get("ghidra_applied"):
            continue
        fixes = v.get("ghidra_fixes") or []
        if fixes:
            out.append({"symbol": sym, "addr": v.get("addr"), "fixes": fixes})
    return out


def summary():
    """The compact index the knowledge-base stage writes from (ADR-0007 rule 1):
    counts and the few rows that carry a decision, never the verdicts."""
    man = load_manifest()["symbols"]
    verdicts = all_verdicts()
    by_kind, by_target, by_conf = {}, {}, {}
    layout, handoff, headers, open_q = [], {}, [], []
    for e in man:
        by_kind[e["kind"]] = by_kind.get(e["kind"], 0) + 1
        by_target[e["target"]] = by_target.get(e["target"], 0) + 1
        by_conf[e["confidence"]] = by_conf.get(e["confidence"], 0) + 1
        if e.get("layout"):
            layout.append({"symbol": e["symbol"], "addr": e["addr"], **e["layout"],
                           "basis": e["basis"]})
        for h in e.get("handoff") or []:
            handoff.setdefault(h, []).append(e["symbol"])
        v = verdicts.get(e["symbol"]) or {}
        if (v.get("header") or {}).get("replace_with"):
            headers.append({"symbol": e["symbol"], "was": v["header"]["current"],
                            "now": v["header"]["replace_with"]})
        if v.get("open_questions"):
            open_q.append({"symbol": e["symbol"], "questions": v["open_questions"]})
    initialised = [{"symbol": e["symbol"], "addr": e["addr"], "basis": e["basis"]}
                   for e in man if e["kind"] == "initialized" and not e.get("layout")]
    return {"landed": len(man), "by_kind": by_kind, "by_target": by_target,
            "by_confidence": by_conf, "layout_constraints": layout,
            "initialised_without_layout": initialised, "handoff": handoff,
            "header_changes": headers, "open_questions": open_q}


def plan():
    routing = load_routing()
    rows = routing_by_symbol(routing)
    landed = {e["symbol"] for e in load_manifest()["symbols"]}
    out = {"ready": {}, "invalid": [], "reroute": [], "landed": len(landed)}
    for sym, v in all_verdicts().items():
        if sym in landed:
            continue
        if v.get("_unreadable"):
            out["invalid"].append({"symbol": sym, "why": ["not valid JSON"]})
            continue
        if v.get("reroute"):
            out["reroute"].append({"symbol": sym, "to": v["reroute"].get("to"),
                                   "why": v["reroute"].get("why", "")})
            continue
        probs = validate(v, rows)
        if probs:
            out["invalid"].append({"symbol": sym, "why": probs})
        else:
            out["ready"].setdefault(rows[sym]["target"], []).append(sym)
    return out


# ----------------------------------------------------------------- selftest

def _selftest():
    rows = []
    routing = {"globals": {
        "00060040": {"symbol": "data_ap", "target": "gd.c", "type": "/int[6]", "size": 24},
        "00060058": {"symbol": "data_def", "target": "gd.c", "type": "/int[6]", "size": 24},
        "00060070": {"symbol": "data_flag", "target": "gd.c", "type": "/byte", "size": 1},
        "00069000": {"symbol": "data_z", "target": "gd.c", "type": "/int", "size": 4},
        "00069100": {"symbol": "data_other", "target": "x.c", "type": "/int", "size": 4},
    }}
    r = routing_by_symbol(routing)

    def V(sym, addr, kind, defi, **kw):
        v = {"symbol": sym, "addr": addr, "target": "gd.c", "kind": kind,
             "definition": defi, "comment": "Test entry.", "size": 4,
             "confidence": "high", "basis": "test", "layout": {}}
        v.update(kw)
        return v

    good = V("data_z", "00069000", "zero", "int data_z;")
    rows.append(("valid zero verdict", validate(good, r) == [], str(validate(good, r))))
    bad = V("data_z", "00069000", "zero", "int data_z = 1;")
    rows.append(("zero with initialiser refused", bool(validate(bad, r)), str(validate(bad, r))))
    bad = V("data_z", "00069000", "zero", "extern int data_z;")
    rows.append(("extern refused", bool(validate(bad, r)), str(validate(bad, r))))
    bad = V("data_z", "00069004", "zero", "int data_z;")
    rows.append(("wrong address refused", bool(validate(bad, r)), str(validate(bad, r))))
    bad = V("data_z", "00069000", "zero", "int data_z;", target="x.c")
    rows.append(("wrong target refused", bool(validate(bad, r)), str(validate(bad, r))))
    bad = V("data_z", "00069000", "zero", "int data_z;", layout={"follows": "data_def"})
    rows.append(("layout on a zero definition refused", bool(validate(bad, r)),
                 str(validate(bad, r))))
    fp = V("data_z", "00069000", "initialized",
           "void (*data_z[2])(void) = { f0, f1 };", size=8)
    rows.append(("function-pointer array declarator accepted", validate(fp, r) == [],
                 str(validate(fp, r))))
    padded = V("data_z", "00069000", "initialized",
               "int data_z = 0;\nstatic unsigned char data_z_tail[3] = { 0 };",
               layout={"zero_pad_after": 3})
    rows.append(("zero pad after accepted", validate(padded, r) == [], str(validate(padded, r))))
    bad = V("data_z", "00069000", "initialized", "int data_z = 0;",
            layout={"zero_pad_after": 3})
    rows.append(("zero pad without its static refused", bool(validate(bad, r)),
                 str(validate(bad, r))))

    tmp = Path(tempfile.mkdtemp(prefix="fdps_land_selftest_"))
    try:
        (tmp / "src").mkdir()
        (tmp / "src" / "gd.c").write_text('#include "gd.h"\n\nint f(void)\n{\n    return 0;\n}\n',
                                          encoding="utf-8")
        (tmp / "src" / "gd.h").write_text("extern unsigned int data_ap[6];\nextern int data_def[6];\n",
                                          encoding="utf-8")
        vs = {
            "data_z": good,
            "data_flag": V("data_flag", "00060070", "initialized",
                           "unsigned char data_flag = 0;", size=1,
                           layout={"follows": "data_def"}),
            "data_ap": V("data_ap", "00060040", "initialized",
                         "int data_ap[6] = { 5, 0, -5, -5, -5, 0 };", size=24,
                         header={"file": "gd.h", "current": "extern unsigned int data_ap[6];",
                                 "replace_with": "extern int data_ap[6];"}),
            "data_def": V("data_def", "00060058", "initialized",
                          "int data_def[6] = { 0, 0, 10, 10, -5, 0 };", size=24,
                          layout={"follows": "data_ap"}),
        }
        man = tmp / "manifest.json"
        rep = apply_target("gd.c", src_dir=tmp / "src", manifest_path=man,
                           verdicts=vs, routing=routing)
        text = (tmp / "src" / "gd.c").read_text(encoding="utf-8")
        order = [text.find(s) for s in ("int data_ap[6]", "int data_def[6]",
                                         "unsigned char data_flag", "int data_z;")]
        rows.append(("all four landed", rep["landed"] == sorted(vs), str(rep)))
        rows.append(("initialised in address order, zero after",
                     all(o > 0 for o in order) and order == sorted(order), str(order)))
        rows.append(("block sits before the code",
                     text.find(END) < text.find("int f(void)"), "yes"))
        rows.append(("header line replaced",
                     "extern unsigned int" not in (tmp / "src" / "gd.h").read_text(encoding="utf-8"),
                     "yes"))
        m = json.loads(man.read_text(encoding="utf-8"))
        rows.append(("manifest records layout",
                     [e["layout"] for e in m["symbols"] if e["symbol"] == "data_def"]
                     == [{"follows": "data_ap"}], str(m["symbols"][:1])))
        # Idempotent: landing the same verdicts again changes nothing.
        apply_target("gd.c", src_dir=tmp / "src", manifest_path=man,
                     verdicts=vs, routing=routing)
        again = (tmp / "src" / "gd.c").read_text(encoding="utf-8")
        rows.append(("re-landing is idempotent", again == text, "yes" if again == text else "differs"))
        # A later landing adds to the block without disturbing it.
        routing["globals"]["00060060"] = {"symbol": "data_mid", "target": "gd.c",
                                          "type": "/int", "size": 4}
        mid = V("data_mid", "00060060", "initialized", "int data_mid = 3;")
        apply_target("gd.c", src_dir=tmp / "src", manifest_path=man,
                     verdicts={"data_mid": mid}, routing=routing)
        t3 = (tmp / "src" / "gd.c").read_text(encoding="utf-8")
        rows.append(("later landing keeps earlier entries",
                     all(s in t3 for s in ("data_ap[6] =", "data_z;", "data_mid = 3")), "yes"))
        # A follows constraint that the address order cannot satisfy is refused
        # before anything is written.
        broken = dict(vs["data_flag"], layout={"follows": "data_ap"})
        try:
            apply_target("gd.c", src_dir=tmp / "src", manifest_path=man,
                         verdicts={"data_flag": broken, "data_ap": vs["data_ap"]},
                         routing=routing)
            rows.append(("unsatisfiable follows refused", False, "landed"))
        except LandError as e:
            rows.append(("unsatisfiable follows refused",
                         (tmp / "src" / "gd.c").read_text(encoding="utf-8") == t3, str(e)))
    finally:
        shutil.rmtree(str(tmp), ignore_errors=True)
    ok = True
    for name, passed, detail in rows:
        print("[selftest] %-44s %s (%s)" % (name, "ok" if passed else "FAIL", detail[:160]))
        ok = ok and passed
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", nargs="?", choices=("validate", "plan", "apply", "rescan-list",
                                               "ghidra-fixes", "summary"))
    ap.add_argument("symbols", nargs="*")
    ap.add_argument("--target")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        ok = _selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    if a.cmd == "validate":
        rows = routing_by_symbol(load_routing())
        syms = a.symbols or sorted(all_verdicts())
        bad = 0
        for s in syms:
            v = read_verdict(s)
            probs = ["no verdict file"] if v is None else (
                ["not valid JSON"] if v.get("_unreadable") else validate(v, rows))
            print("%-60s %s" % (s, "ok" if not probs else "; ".join(probs)))
            bad += bool(probs)
        return 1 if bad else 0
    if a.cmd == "plan":
        print(json.dumps(plan(), indent=2, ensure_ascii=False))
        return 0
    if a.cmd == "rescan-list":
        print(json.dumps(rescan_list(), indent=2, ensure_ascii=False))
        return 0
    if a.cmd == "ghidra-fixes":
        print(json.dumps(ghidra_fix_list(), indent=2, ensure_ascii=False))
        return 0
    if a.cmd == "summary":
        print(json.dumps(summary(), indent=2, ensure_ascii=False))
        return 0
    if a.cmd == "apply":
        if not a.target:
            ap.error("apply needs --target")
        try:
            rep = apply_target(a.target)
        except LandError as e:
            print(json.dumps({"target": a.target, "error": str(e)}, ensure_ascii=False))
            return 1
        print(json.dumps(rep, indent=2, ensure_ascii=False))
        return 0 if not rep["refused"] else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
