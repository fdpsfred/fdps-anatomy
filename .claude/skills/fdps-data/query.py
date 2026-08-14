"""Query the FDPS game data set that sits next to this script.

    python query.py show <table> <code>          one record in full
    python query.py list <table> [--limit N]     every record, one line each
    python query.py name <text> [--table T]      records whose name contains <text>
    python query.py where <table> <expr>...      field=value / field=min..max filters
    python query.py find <number> [--table T]    every field anywhere holding that number
    python query.py fields [table]               the field names `where` accepts

Codes are hexadecimal for item / spell / character / class and decimal for chapter,
matching how the knowledge base writes them; `--dec` and a `0x` prefix override that.
Add --json after the command to get the raw records instead of the formatted report.
"""
import argparse
import json
import sys
import unicodedata
from pathlib import Path

DATA = Path(__file__).resolve().with_name("fdps_data.json")
HEX_TABLES = ("item", "spell", "character", "class")
NAME_FIELDS = ("label", "name", "class", "class_name")
# Identifiers rather than measurements: excluded from `find`, still usable in `where`.
INDEX_FIELDS = ("code", "index", "chapter", "record_index")
# Fields the knowledge base writes as hexadecimal codes rather than as quantities.
HEX_FIELDS = ("type", "hit_effect", "use_effect", "use_target", "use_distance",
              "distance", "race", "class_code", "learn_index")


def load():
    with DATA.open(encoding="utf-8") as fh:
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
    return "(未命名)"


def code_text(rec, table_name):
    return rec["code_hex"] if table_name in HEX_TABLES else str(rec["code"])


def format_value(key, value):
    if key in HEX_FIELDS and isinstance(value, int):
        return "%02X" % value
    if key == "move_cost":
        return " ".join("%02X" % v for v in value)
    if isinstance(value, list):
        if not value:
            return "-"
        if all(isinstance(v, dict) for v in value):
            return ", ".join("%02X %s" % (v.get("code", v.get("spell", 0)),
                                          v.get("name") or v.get("spell_name") or "?")
                             + (" @Lv%d" % v["level"] if "level" in v else "")
                             for v in value)
        return " ".join(str(v) for v in value)
    return str(value)


def format_side(field, value):
    """A discrepancy's two sides: code lists and code fields read as hex, amounts as-is."""
    if value is None:
        return "(未列)"
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
        print("  ! no appearance values: this index is outside the twelve rows of "
              "FRIAPRDA.DAT that assets/characters.md documents")
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
    if table_name == "chapter":
        return "index %d" % rec["index"]
    return ""


def emit_json(records):
    json.dump(records, sys.stdout, indent=2, ensure_ascii=False)
    sys.stdout.write("\n")


def cmd_show(data, args):
    table = table_of(data, args.table)
    code = parse_code(args.code, args.table, args.dec)
    hits = [r for r in table["records"] if r["code"] == code]
    if not hits:
        raise SystemExit("%s has no record %s" % (args.table, args.code))
    if args.json:
        return emit_json(hits)
    for rec in hits:
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
    for table_name in ([args.table] if args.table else data["tables"]):
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
    print("%d fields hold %d (%#x)" % (len(found), value, value))


def cmd_fields(data, args):
    for table_name in ([args.table] if args.table else data["tables"]):
        print("%-10s %s" % (table_name, " ".join(numeric_fields(table_of(data, table_name)))))


def main():
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

    args = parser.parse_args()
    args.func(load(), args)


if __name__ == "__main__":
    main()
