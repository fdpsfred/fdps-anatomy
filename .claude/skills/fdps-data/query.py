"""Query the FDPS game data sets that sit next to this script.

    python query.py show <table> <code>          one record in full
    python query.py list <table> [--limit N]     every record, one line each
    python query.py name <text> [--table T]      records whose name contains <text>
    python query.py where <table> <expr>...      field=value / field=min..max filters
    python query.py find <number> [--table T]    every field anywhere holding that number
    python query.py fields [table]               the field names `where` accepts
    python query.py chapter <n> [--enemies] [--treasure] [--events] [--scripts] [--text]
                                                 one chapter: header, then the sections asked
                                                 for (all but --text when none is given)
    python query.py deploy <code>                every chapter deployment record of a unit
    python query.py text <words> [--block N] [--status S] [--limit N]
                                                 full-text search over all 66 text blocks
    python query.py entry <block> <entry>        one text entry in full: who shows it, or
                                                 why it is never shown and who owns that

Codes are hexadecimal for item / spell / character / class / enemy / race /
use_effect and decimal for chapter / shop, matching how the knowledge base writes
them; `--dec` and a `0x` prefix override that.  Text entries are hexadecimal,
blocks decimal (FDETXTnn).  Add --json after the command to get the raw records.
"""
import argparse
import json
import sys
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA_FILE = "fdps_data.json"
TEXT_FILE = "fdps_text.json"
HEX_TABLES = ("item", "spell", "character", "class", "enemy", "race", "use_effect")
NAME_FIELDS = ("label", "name", "class", "class_name")
# Identifiers rather than measurements: excluded from `find`, still usable in `where`.
INDEX_FIELDS = ("code", "index", "chapter", "record_index", "row", "text_entry",
                "after_chapter", "before_chapter", "map")
# Fields the knowledge base writes as hexadecimal codes rather than as quantities.
HEX_FIELDS = ("type", "hit_effect", "use_effect", "use_target", "use_distance",
              "distance", "race", "class_code", "learn_index", "text_entry")
# Chapter records nest their deployments, treasure and events; `chapter` and
# `deploy` read those, `find` would only confuse event codes with item codes.
NESTED_TABLES = ("chapter",)
CHAPTER_SECTIONS = ("enemies", "treasure", "events", "scripts", "text")
DEFAULT_SECTIONS = CHAPTER_SECTIONS[:-1]    # a chapter without flags: all but the text


def load(data_dir, name=DATA_FILE):
    with (Path(data_dir) / name).open(encoding="utf-8") as fh:
        return json.load(fh)


def table_of(data, name):
    if name not in data["tables"]:
        raise SystemExit("unknown table %r; pick one of %s"
                         % (name, ", ".join(data["tables"])))
    return data["tables"][name]


def numeric_fields(table):
    seen = []
    for rec in table["records"]:
        for key, value in rec.items():
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                if key not in seen:
                    seen.append(key)
    return seen


def to_int(text, base, what):
    try:
        return int(text, base)
    except ValueError:
        raise SystemExit("%r is not a %s %s" % (text, "hexadecimal" if base == 16
                                                else "decimal", what))


def parse_code(text, table_name, decimal):
    if text.lower().startswith("0x"):
        return to_int(text, 16, "code")
    if decimal or table_name not in HEX_TABLES:
        return to_int(text, 10, "code")
    return to_int(text, 16, "code")


def parse_number(text):
    if text.lower().startswith("0x"):
        return to_int(text, 16, "number")
    if "." in text:
        try:
            return float(text)
        except ValueError:
            raise SystemExit("%r is not a number" % text)
    return to_int(text, 10, "number")


def parse_constraint(expr):
    """`ap=300` exact, `ap=300..400` inclusive range, `ap=300..` / `ap=..400` open ended."""
    if "=" not in expr:
        raise SystemExit("constraint %r is not field=value or field=min..max" % expr)
    field, _, spec = expr.partition("=")
    if ".." in spec:
        lo, _, hi = spec.partition("..")
        return field, (parse_number(lo) if lo else None, parse_number(hi) if hi else None)
    value = parse_number(spec)
    return field, (value, value)


def matches(rec, field, bounds):
    value = rec.get(field)
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return False
    lo, hi = bounds
    return (lo is None or value >= lo) and (hi is None or value <= hi)


def label(rec):
    for field in NAME_FIELDS:
        if rec.get(field):
            return rec[field]
    if rec.get("description"):          # use_effect codes have a description, no name
        return rec["description"].split("：")[0][:12]
    return "(unnamed)"


def code_text(rec, table_name):
    return rec["code_hex"] if table_name in HEX_TABLES else str(rec["code"])


def format_item(v):
    """One element of a list of dicts: a coded name, a route, or a learned spell."""
    if "route_name" in v:
        return "%s %02X %s MV+%d" % (v["route_name"], v["form"], v["class_name"] or "?",
                                     v["move_bonus"])
    return ("%02X %s" % (v.get("code", v.get("spell", 0)),
                         v.get("name") or v.get("spell_name") or "?")
            + (" @Lv%d" % v["level"] if "level" in v else ""))


def format_value(key, value):
    if key in HEX_FIELDS and isinstance(value, int):
        return "%02X" % value
    if key == "move_cost":
        return " ".join("%02X" % v for v in value)
    if isinstance(value, list):
        if not value:
            return "-"
        if all(isinstance(v, dict) for v in value):
            return ", ".join(format_item(v) for v in value)
        return " ".join(str(v) for v in value)
    return str(value)


def format_side(field, value):
    """A discrepancy's two sides: code lists and code fields read as hex, amounts as-is."""
    if value is None:
        return "(not listed)"
    if isinstance(value, list):
        return "[%s]" % " ".join("%02X" % v for v in value)
    if field in HEX_FIELDS or field.startswith("move_cost"):
        return "%02X" % value
    return str(value)


def print_record(data, table_name, rec):
    table = table_of(data, table_name)
    print("%s %s  %s" % (table_name, code_text(rec, table_name), label(rec)))
    print("  source: names %s, values %s (%s / %s)"
          % (table["name_source"], table["value_source"], table["canon"],
             table["member"] or "-"))
    for key, value in rec.items():
        if key in ("code", "code_hex", "name", "label") or value is None:
            continue
        if value == [] or value is False:
            continue
        print("  %-22s %s" % (key, format_value(key, value)))
    if table_name == "character" and not rec["appearance_documented"]:
        print("  ! no appearance values: nothing in the game reads this index of "
              "FRIAPRDA.DAT (assets/characters.md)")
    for d in data["discrepancies"]:
        if d["table"] == table_name and d["code"] == rec["code"]:
            print("  ! guide says %s = %s, data file has %s -- %s"
                  % (d["field"], format_side(d["field"], d["guide"]),
                     format_side(d["field"], d["dump"]), d["note"]))


def pad(text, width):
    """Pad to `width` terminal columns, counting full-width characters as two."""
    used = sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in text)
    return text + " " * max(0, width - used)


def print_rows(data, table_name, records, limit=None):
    shown = records if limit is None else records[:limit]
    for rec in shown:
        print("%-9s %-4s %s %s" % (table_name, code_text(rec, table_name),
                                   pad(label(rec), 18), summary(table_name, rec)))
    if limit is not None and len(records) > limit:
        print("... %d more (raise --limit to see them)" % (len(records) - limit))


def chapters_text(numbers):
    return ",".join(str(n) for n in numbers) or "-"


def summary(table_name, rec):
    if table_name == "item":
        return "type %02X  AP %d HIT %d DP %d EV %d  price %d" % (
            rec["type"], rec["ap"], rec["hit"], rec["dp"], rec["ev"], rec["price"])
    if table_name == "spell":
        power = ("AP x %.2f" % rec["ap_multiplier"]) if rec["ap_multiplier"] else str(rec["power"])
        return "power %s  hit %d  MP %d  radius %d" % (power, rec["hit"], rec["mp"],
                                                       rec["radius"])
    if table_name == "character":
        if rec["appearance_documented"]:
            return "class %02X  Lv %d  HP %d MP %d  AP %d DP %d DX %d" % (
                rec["class_code"], rec["level"], rec["hp_base"], rec["mp_base"],
                rec["ap_base"], rec["dp_base"], rec["dx_base"])
        return "growth AP %d-%d DP %d-%d HP %d-%d MP %d-%d" % (
            rec["ap_min"], rec["ap_max"], rec["dp_min"], rec["dp_max"],
            rec["hp_min"], rec["hp_max"], rec["mp_min"], rec["mp_max"])
    if table_name == "class":
        return "critical %d  magic resist %d%%" % (rec["critical"], rec["magic_resist"])
    if table_name == "enemy":
        return "%s/%s  HP %d MP %d AP %d DP %d DX %d MV %d EXP %d  chapters %s" % (
            rec["race_name"] or "?", rec["class_name"] or "?", rec["hp"], rec["mp"], rec["ap"],
            rec["dp"], rec["dx"], rec["mv"], rec["exp"], chapters_text(rec["deployed_chapters"]))
    if table_name == "race":
        return "%d party/guest, %d enemy/NPC units" % (len(rec["party_and_guest_units"]),
                                                       len(rec["enemy_and_npc_units"]))
    if table_name == "shop":
        return "after chapter %d  item %d / weapon %d / secret %d" % (
            rec["after_chapter"], len(rec["item_shop"]), len(rec["weapon_shop"]),
            len(rec["secret_shop"]))
    if table_name == "use_effect":
        return "%s  %d items" % (rec["consumed"] or "-", len(rec["items"]))
    if table_name == "chapter":
        return "index %d  %s / %s" % (rec["index"], rec["win"], rec["lose"])
    return ""


def emit_json(records):
    json.dump(records, sys.stdout, indent=2, ensure_ascii=False)
    sys.stdout.write("\n")


def find_record(data, table_name, code):
    hits = [r for r in table_of(data, table_name)["records"] if r["code"] == code]
    if not hits:
        raise SystemExit("%s has no record %s" % (table_name, code))
    return hits


def cmd_show(data, args):
    code = parse_code(args.code, args.table, args.dec)
    hits = find_record(data, args.table, code)
    if args.json:
        return emit_json(hits)
    for rec in hits:
        if args.table == "chapter":
            print_chapter(rec, DEFAULT_SECTIONS, args.data_dir)
        else:
            print_record(data, args.table, rec)


def cmd_list(data, args):
    table = table_of(data, args.table)
    records = table["records"] if args.blank else [r for r in table["records"]
                                                   if not r.get("blank")]
    if args.json:
        return emit_json(records)
    print_rows(data, args.table, records, args.limit)
    print("%d records" % len(records))


def cmd_name(data, args):
    found = []
    for table_name in ([args.table] if args.table else data["tables"]):
        for rec in table_of(data, table_name)["records"]:
            if any(args.text in (rec.get(f) or "") for f in NAME_FIELDS):
                found.append((table_name, rec))
    if args.json:
        return emit_json([{"table": t, **r} for t, r in found])
    for table_name, rec in found:
        print_rows(data, table_name, [rec])
    print("%d records" % len(found))


def cmd_where(data, args):
    table = table_of(data, args.table)
    constraints = [parse_constraint(e) for e in args.expr]
    known = numeric_fields(table)
    for field, _ in constraints:
        if field not in known:
            raise SystemExit("%s has no numeric field %r; try: %s"
                             % (args.table, field, " ".join(known)))
    found = [r for r in table["records"]
             if all(matches(r, f, b) for f, b in constraints) and not r.get("blank")]
    if args.json:
        return emit_json(found)
    print_rows(data, args.table, found, args.limit)
    print("%d records" % len(found))


def cmd_find(data, args):
    value = parse_number(args.number)
    found = []
    tables = [args.table] if args.table else [t for t in data["tables"] if t not in NESTED_TABLES]
    for table_name in tables:
        table = table_of(data, table_name)
        for rec in table["records"]:
            if rec.get("blank"):
                continue
            for field, held in rec.items():
                if field in INDEX_FIELDS or isinstance(held, bool):
                    continue
                if isinstance(held, (int, float)):
                    hit = held == value
                elif isinstance(held, list):
                    # Lists of codes, and lists of {code|spell, name} such as a
                    # character's starting equipment or the spells they learn.
                    hit = value in [v.get("code", v.get("spell")) if isinstance(v, dict)
                                    else v for v in held]
                else:
                    continue
                if hit:
                    found.append({"table": table_name, "code": code_text(rec, table_name),
                                  "name": label(rec), "field": field})
    if args.json:
        return emit_json(found)
    for hit in found:
        print("%-9s %-4s %s %s" % (hit["table"], hit["code"], pad(hit["name"], 18),
                                   hit["field"]))
    print("%d fields hold %d (%#x); chapter records are searched by `deploy` and `chapter`"
          % (len(found), value, value))


def cmd_fields(data, args):
    for table_name in ([args.table] if args.table else data["tables"]):
        print("%-10s %s" % (table_name, " ".join(numeric_fields(table_of(data, table_name)))))


# ---------------------------------------------------------------------------
# Chapters
# ---------------------------------------------------------------------------
def fn(ref):
    if not ref:
        return "-"
    return "%s (%s)" % (ref["name"], ref["address"] or "?")


def wave_state(wave):
    if wave["deployed"] is None:
        return "not judged"
    if wave["deployed"]:
        return "deployed: " + (wave["when"] or "")
    return "never deployed: " + (wave["why"] or "")


def print_chapter(rec, sections, data_dir):
    print("chapter %d  %s  (chapter index %d, %s)" % (rec["chapter"], rec["name"], rec["index"],
                                               rec["doc"]))
    print("  win: %s / lose: %s" % (rec["win"], rec["lose"]))
    print("  map %02d  %dx%d  party slots %d  deployment records %d  %s %d entries  disc %d"
          % (rec["map"], rec["map_size"][0], rec["map_size"][1], rec["player_slots"],
             rec["spawn_count"], rec["text_block"], rec["text_entries"], rec["disc"]))
    print("  init %s / post %s / end %s" % (fn(rec["handlers"]["init"]),
                                            fn(rec["handlers"]["post"]),
                                            fn(rec["handlers"]["end"])))
    if rec["village_shop"] is not None:
        print("  village before this chapter: SHOP%02d.DAT (show shop %d)" % (rec["village_shop"],
                                                            rec["village_shop"]))
    if not rec["judged"]:
        print("  ! no landed judgement: waves and text readers are not judged")
    if "enemies" in sections:
        print("\n[enemies]")
        for wave in rec["waves"]:
            print("  wave %s (%d records)" % ("FF" if wave["wave"] == 0xFF else wave["wave"],
                                        len(wave["records"])))
            print("    " + wave_state(wave))
            if wave["deployed"] is False:
                continue
            for d in rec["deployments"]:
                if d["wave"] == wave["wave"]:
                    anchor = "(%d, %d)" % tuple(d["anchor"]) if d["anchor"] else "-"
                    print("    #%-3d %s %s %s Lv%-3d %s %s%s"
                          % (d["index"], pad(d["side_name"] or str(d["side"]), 4), d["code_hex"],
                             pad(d["name"], 14), d["level"], anchor, d["ai_text"],
                             "  on death: " + d["death"] if d["death"] else ""))
    if "treasure" in sections:
        print("\n[treasure]")
        if not rec["treasure"]:
            print("  no chest or buried cell")
        for t in rec["treasure"]:
            cells = "、".join("(%d, %d) %s" % (c["x"], c["y"], c["kind"]) for c in t["cells"])
            print("  code %-3d %s  %s%s" % (t["code"], cells, t["content"],
                                         " (%d cells share it, taken once)" % len(t["cells"])
                                         if t["shared"] else ""))
        for u in rec["unreachable_search_records"]:
            print("  search record %d no cell refers to: kind %d, payload %X (cut_content/items.md)"
                  % (u["record"], u["kind"], u["payload"] & 0xFFFF))
        for d in rec["drops"]:
            print("  drop #%-3d %s %s wave %s%s  %s"
                  % (d["index"], d["code_hex"], pad(d["name"], 14), d["wave"],
                     "" if d["deployed"] is not False else " (never deployed, unobtainable)",
                     d["content"]))
    if "events" in sections:
        print("\n[events]")
        for t in rec["turn_events"]:
            print("  turn event  turn %-3d %s  slot %d %s"
                  % (t["turn"], t["phase"] or t["side"], t["slot"], fn(t["handler"])))
        for c in rec["cell_events"]:
            cells = "、".join("(%d, %d)" % tuple(xy) for xy in c["cells"])
            print("  cell event  code %d %s %s  slot %d %s"
                  % (c["code"], cells, c["trigger"], c["slot"], fn(c["handler"])))
        for d in rec["death_events"]:
            print("  death script #%-3d %s %s wave %s  %s"
                  % (d["index"], d["code_hex"], pad(d["name"], 14), d["wave"], d["death"]))
        if not (rec["turn_events"] or rec["cell_events"] or rec["death_events"]):
            print("  no turn event, cell event or death script")
    if "scripts" in sections:
        print("\n[scripts]")
        for s in rec["scripts"]:
            callers = "；".join("%s %s" % (c["kind"], c["function"]) for c in s["callers"])
            print("  %s  %d bytes, %d steps  map %s -> %s -> ends on %s  (%s)"
                  % (s["member"], s["size"], s["steps"], s["initial_map"],
                     "→".join(str(x) for x in s["switches"]) or "-", s["final_map"], callers))
        if not rec["scripts"]:
            print("  plays no cut-scene script")
    if "text" in sections:
        print("\n[text]")
        block = load(data_dir, TEXT_FILE)["blocks"][rec["chapter"]]
        for e in block["entries"]:
            print_entry_line(block["block"], e)


def cmd_chapter(data, args):
    n = parse_code(args.number, "chapter", True)
    (rec,) = find_record(data, "chapter", n)
    sections = [s for s in CHAPTER_SECTIONS if getattr(args, s)] or list(DEFAULT_SECTIONS)
    if args.json:
        return emit_json(rec)
    print_chapter(rec, sections, args.data_dir)


def cmd_deploy(data, args):
    code = parse_code(args.code, "character", args.dec)
    found = [(c, d) for c in table_of(data, "chapter")["records"] for d in c["deployments"]
             if d["char_id"] == code]
    if args.json:
        return emit_json([{"chapter": c["chapter"], **d} for c, d in found])
    for c, d in found:
        state = {True: "deployed", False: "never deployed", None: "not judged"}[d["deployed"]]
        print("ch%02d  #%-3d %s %s Lv%-3d %s wave %-3s %s"
              % (c["chapter"], d["index"], d["code_hex"], pad(d["name"], 14), d["level"],
                 pad(d["side_name"] or str(d["side"]), 4), d["wave"], state))
    unit = [r for t in ("character", "enemy") for r in table_of(data, t)["records"]
            if r["code"] == code]
    scenes = unit[0]["deployed_scene_maps"] if unit else []
    print("%d chapter deployment records%s" % (
        len(found), "; also on cut-scene maps %s" % " ".join(str(m) for m in scenes)
        if scenes else ""))


# ---------------------------------------------------------------------------
# Text
# ---------------------------------------------------------------------------
STATUS = {"shown": "some reader shows it", "never_shown": "nothing ever shows it",
          "empty": "empty string"}


def searchable(e):
    """The entry's lines run together, so a phrase split by a line break matches."""
    return "".join(line for line in e["lines"] if line != "▼")


def print_entry_line(block, e):
    body = " / ".join(line for line in e["lines"] if line != "▼") or "(empty)"
    owner = ""
    if e["owner"]:
        owner = " -> %s" % e["owner"]["id"]
    print("FDETXT%02d 0x%02x  [%s%s]  %s" % (block, e["entry"], e["status"], owner, body))


def cmd_text(data, args):
    text = load(args.data_dir, TEXT_FILE)
    blocks = text["blocks"]
    if args.block is not None:
        blocks = [b for b in blocks if b["block"] == args.block]
    found = [(b["block"], e) for b in blocks for e in b["entries"]
             if args.words in searchable(e) and (args.status is None or e["status"] == args.status)]
    if args.json:
        return emit_json([{"block": b, **e} for b, e in found])
    shown = found if args.limit is None else found[:args.limit]
    for block, e in shown:
        print_entry_line(block, e)
    if args.limit is not None and len(found) > args.limit:
        print("... %d more (raise --limit to see them)" % (len(found) - args.limit))
    print("%d entries" % len(found))


def cmd_entry(data, args):
    text = load(args.data_dir, TEXT_FILE)
    block = to_int(args.block, 10, "block")
    index = to_int(args.entry[2:] if args.entry.lower().startswith("0x") else args.entry, 16,
                   "entry")
    if not 0 <= block < len(text["blocks"]):
        raise SystemExit("no block FDETXT%02d" % block)
    entries = text["blocks"][block]["entries"]
    if not 0 <= index < len(entries):
        raise SystemExit("FDETXT%02d has %d entries" % (block, len(entries)))
    e = entries[index]
    if args.json:
        return emit_json(e)
    kind = text["blocks"][block]["kind"]
    print("FDETXT%02d 0x%02x  (%s block)  %s — %s"
          % (block, index, kind, e["status"], STATUS.get(e["status"], "")))
    if e.get("region"):
        print("  region: %s%s" % (e["region"], " / " + e["group"] if e.get("group") else ""))
    for reader in e["readers"]:
        print("  reader: " + reader)
    if e["owner"]:
        o = e["owner"]
        print("  owner:  %s %s（%s，%s）" % (o["id"], o["title"] or "", o["category"] or "?",
                                          o["page"] or "?"))
    print("")
    for line in e["lines"] or ["(empty)"]:
        print("  " + line)


def main(argv=None, data_dir=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass            # a redirected stream that cannot be reconfigured
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--json", action="store_true", help="print the raw records")

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("show", parents=[common])
    p.add_argument("table")
    p.add_argument("code")
    p.add_argument("--dec", action="store_true", help="read the code as decimal")
    p.set_defaults(func=cmd_show)

    p = sub.add_parser("list", parents=[common])
    p.add_argument("table")
    p.add_argument("--limit", type=int, default=None)
    p.add_argument("--blank", action="store_true", help="include the empty records too")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("name", parents=[common])
    p.add_argument("text")
    p.add_argument("--table")
    p.set_defaults(func=cmd_name)

    p = sub.add_parser("where", parents=[common])
    p.add_argument("table")
    p.add_argument("expr", nargs="+")
    p.add_argument("--limit", type=int, default=None)
    p.set_defaults(func=cmd_where)

    p = sub.add_parser("find", parents=[common])
    p.add_argument("number")
    p.add_argument("--table")
    p.set_defaults(func=cmd_find)

    p = sub.add_parser("fields", parents=[common])
    p.add_argument("table", nargs="?")
    p.set_defaults(func=cmd_fields)

    p = sub.add_parser("chapter", parents=[common])
    p.add_argument("number")
    for section in CHAPTER_SECTIONS:
        p.add_argument("--" + section, action="store_true")
    p.set_defaults(func=cmd_chapter)

    p = sub.add_parser("deploy", parents=[common])
    p.add_argument("code")
    p.add_argument("--dec", action="store_true", help="read the code as decimal")
    p.set_defaults(func=cmd_deploy)

    p = sub.add_parser("text", parents=[common])
    p.add_argument("words")
    p.add_argument("--block", type=int, default=None)
    p.add_argument("--status", choices=sorted(STATUS))
    p.add_argument("--limit", type=int, default=None)
    p.set_defaults(func=cmd_text)

    p = sub.add_parser("entry", parents=[common])
    p.add_argument("block")
    p.add_argument("entry")
    p.set_defaults(func=cmd_entry)

    args = parser.parse_args(argv)
    args.data_dir = Path(data_dir) if data_dir else HERE
    args.func(load(args.data_dir), args)


if __name__ == "__main__":
    main()
