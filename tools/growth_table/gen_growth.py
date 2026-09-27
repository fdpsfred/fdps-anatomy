"""Per-level stat ranges of every character who can join the party, along
every class-change route, for the character stat comparison page.

Every number comes from the game's own tables through tools/data_tables
(FRIAPRDA.DAT base figures, FRILEVUP.DAT growth pairs, RANKUP.DAT routes,
GETMGTAB.DAT spells); the rules that turn them into levels are transcribed
from src/ and documented in the knowledge base:

    entry      AP/DP/DX = base + LV x min,  HP/MP = base + (LV - 1) x min
               (src/roster.c fdps_roster_add_character, src/deploy.c
               fdps_deploy_unit; assets/characters.md)
    level-up   each stat gains min + rand() % (max - min); an equal pair gains
               min and draws no number (src/unitstat.c
               fdps_level_up_apply_stat_gain; program_info/battle.md)
    level cap  99 for portrait id 9 (蓋亞), 40 for everyone else
               (src/unitstat.c fdps_unit_award_exp_and_level_up)
    promotion  at the church, level >= 20 and portrait id < 9; the new form's
               RAW max bytes are added, move gains the route's bonus, level
               goes back to 1 (src/church.c fdps_church_promote_loop;
               program_info/village.md)

Two variants bound every figure: "min" rolls the minimum on every level-up,
"max" the largest gain a roll can give (max - 1 for a pair whose bytes
differ).  A promoted route depends on the level the character promotes at, so
the full output carries every promotion level from 20 (or the join level) to 40.

Usage:
    python tools/growth_table/gen_growth.py [--dump DIR]

Writes workspace/growth_table/growth_compact.json (the parameters the page
embeds; its JavaScript recomputes every level) and growth_data.json (every
level of every join x route x promotion level, which the two checks compare
against: src_replay.py and verify_js.py).
"""
import argparse
import json
import sys
from pathlib import Path
from typing import NamedTuple, Optional

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "data_tables"))
import data_tables  # noqa: E402  (owner of the table decoders and the game text)

OUT_DIR = ROOT / "workspace" / "growth_table"
STATS = ("hp", "mp", "ap", "dp", "dx")
ENTRY_SCALES_BY_LEVEL = ("ap", "dp", "dx")   # the other two scale by LV - 1

# src/unitstat.c: MACHINE_SOLDIER_PORTRAIT_ID, MACHINE_SOLDIER_LEVEL_CAP,
# ORDINARY_LEVEL_CAP.
MACHINE_SOLDIER_PORTRAIT_ID = 9
MACHINE_SOLDIER_LEVEL_CAP = 99
ORDINARY_LEVEL_CAP = 40
# src/church.c: PROMOTE_MIN_LEVEL, PROMOTABLE_PORTRAIT_COUNT, PROMOTED_LEVEL.
PROMOTE_MIN_LEVEL = 20
PROMOTABLE_PORTRAIT_COUNT = 9
PROMOTED_LEVEL = 1
# src/church.c: the badge each route needs (route 0 needs none).
BADGE_ITEMS = {1: 0xE0, 2: 0xE1, 3: 0xDB}

class Deploy(NamedTuple):
    """The unit of the same character id the join chapter's map deploys.  Its
    record replaces the roster record at the victory write-back, so when it
    is certain to be deployed before victory it is the only way the character
    arrives; a turn-event arrival the player can pre-empt by winning first
    (`arrival_turn`) is one of two ways."""
    map_no: int
    level: int
    arrival_turn: Optional[int] = None


class Join(NamedTuple):
    char_id: int
    chapter: int
    deploy: Optional[Deploy] = None


# How each character reaches the player, in the order they join
# (assets/characters.md 「加入」; 費塔加's LV15 unit: chapters/ch08.md).
JOINS = (
    Join(0x00, 1),
    Join(0x06, 2),
    Join(0x04, 3),
    Join(0x01, 4, Deploy(3, 8)),
    Join(0x03, 7),
    Join(0x02, 8, Deploy(7, 15)),
    Join(0x08, 9),
    Join(0x09, 9),
    Join(0x07, 11),
    Join(0x05, 15, Deploy(14, 20)),
    Join(0x0B, 19, Deploy(18, 2, arrival_turn=6)),
    Join(0x0A, 24, Deploy(23, 15, arrival_turn=7)),
)


class GrowthError(Exception):
    """The game data breaks an assumption the model rests on."""


# ---- the rules ---------------------------------------------------------------

def gain_range(pair):
    """(smallest, largest) gain a level-up can roll from a FRILEVUP.DAT pair."""
    lo, hi = pair
    if hi == lo:
        return lo, lo
    if hi < lo:
        raise GrowthError(f"inverted growth pair {pair}")
    return lo, hi - 1


def entry_stats(base, growth, level):
    """The five figures a unit is created with at `level`."""
    out = {}
    for s in STATS:
        steps = level if s in ENTRY_SCALES_BY_LEVEL else level - 1
        out[s] = base[s] + growth[s][0] * steps
    return out


def level_cap(portrait_id):
    return (MACHINE_SOLDIER_LEVEL_CAP if portrait_id == MACHINE_SOLDIER_PORTRAIT_ID
            else ORDINARY_LEVEL_CAP)


def base_rows(base, growth, join, cap):
    """Levels join..cap of the form the character joins in."""
    start = entry_stats(base, growth, join)
    ranges = {s: gain_range(growth[s]) for s in STATS}
    rows = []
    for lv in range(join, cap + 1):
        n = lv - join
        rows.append({"lv": lv,
                     "min": {s: start[s] + ranges[s][0] * n for s in STATS},
                     "max": {s: start[s] + ranges[s][1] * n for s in STATS}})
    return rows


def promo_rows(carry, growth, cap=ORDINARY_LEVEL_CAP):
    """Levels 1..cap after a promotion, from the row the character promoted at.
    The promotion adds the new form's raw max bytes; each later level rolls."""
    ranges = {s: gain_range(growth[s]) for s in STATS}
    rows = []
    for lv in range(PROMOTED_LEVEL, cap + 1):
        n = lv - PROMOTED_LEVEL
        rows.append({"lv": lv,
                     "min": {s: carry["min"][s] + growth[s][1] + ranges[s][0] * n for s in STATS},
                     "max": {s: carry["max"][s] + growth[s][1] + ranges[s][1] * n for s in STATS}})
    return rows


def promote_levels(join):
    return list(range(max(PROMOTE_MIN_LEVEL, join), ORDINARY_LEVEL_CAP + 1))


# ---- the characters ----------------------------------------------------------

def _pairs(growth_rec):
    return {s: list(growth_rec[s]) for s in STATS}


def _learned(game, form):
    learn = game.growth[form]["learn"]
    if learn == 0xFF:
        return []
    return [[lv, game.spell_name(spell)] for lv, spell in game.learning[learn]]


def routes(game, char_id):
    """The distinct forms the church can turn a character into, each with the
    routes that reach it, in route order."""
    if char_id >= PROMOTABLE_PORTRAIT_COUNT:
        return []
    by_form = {}
    for number, (form, clazz, move) in data_tables.offered_routes(char_id, game.promotions[char_id]):
        entry = by_form.setdefault(form, {"form": form, "cls": game.class_name(clazz),
                                          "move_bonus": move, "routes": []})
        if (entry["cls"], entry["move_bonus"]) != (game.class_name(clazz), move):
            raise GrowthError(f"char {char_id:02X}: form {form:02X} reached with two class/move pairs")
        entry["routes"].append(number)
    out = []
    for entry in by_form.values():
        numbers = entry.pop("routes")
        # A form route 0 also reaches needs no badge.  Any other form needs the
        # badge of a route that reaches it: the hero badge reaches its form
        # alone, and a form both the light and the dark badge reach is named by
        # the light one, which the church tests first (src/church.c).
        entry["badge"] = None if 0 in numbers else game.item_name(BADGE_ITEMS[min(numbers)])
        entry["routes"] = numbers
        entry["growth"] = _pairs(game.growth[entry["form"]])
        entry["learn"] = _learned(game, entry["form"])
        out.append(entry)
    return out


def joins(game, join):
    """The join levels of one character, each with how it comes about -- the
    page words them: `roster` (the roster record is the character), `stays`
    (the certain deployment replaces it), `arrives` (the deployment, if it
    arrives before victory) and `early` (the roster record, when the player
    wins first)."""
    roster_level = game.appearance[join.char_id]["level"]
    d = join.deploy
    if d is None:
        return [{"level": roster_level, "how": "roster"}]
    if not any(s["map"] == d.map_no and s["level"] == d.level and s["side"] in (1, 2)
               for s in game.deployments(join.char_id)):
        raise GrowthError(f"char {join.char_id:02X}: no side-1/2 LV{d.level} deployment on MAP{d.map_no:02d}")
    if d.map_no != join.chapter - 1:
        raise GrowthError(f"char {join.char_id:02X}: MAP{d.map_no:02d} is not chapter {join.chapter}'s map")
    if d.arrival_turn is None:
        return [{"level": d.level, "how": "stays", "roster_level": roster_level}]
    return [{"level": d.level, "how": "arrives", "turn": d.arrival_turn},
            {"level": roster_level, "how": "early", "turn": d.arrival_turn}]


def check_pairs(growth):
    """Refuse a growth record whose pairs the level-up would read inverted."""
    for s in STATS:
        gain_range(growth[s])


def promoted_classes(game):
    """Every class the church can turn someone into.  A character who joins in
    one of them (蘭斯洛特's 聖騎士, 珊's 法師) is plotted on the promoted half of
    the page's cumulative level axis."""
    return {clazz for owner, rts in enumerate(game.promotions)
            for _n, (_form, clazz, _move) in data_tables.offered_routes(owner, rts)}


def model(game):
    """The parameters of every joining character: what the page embeds."""
    promoted = promoted_classes(game)
    chars = []
    for join in JOINS:
        char_id = join.char_id
        app = game.appearance[char_id]
        check_pairs(game.growth[char_id])
        rts = routes(game, char_id)
        for r in rts:
            check_pairs(r["growth"])
        chars.append({
            "id": char_id, "name": game.char_name(char_id),
            "cls": game.class_name(app["class"]), "race": game.race_name(app["race"]),
            "chapter": join.chapter, "move": app["move"], "cap": level_cap(char_id),
            "joins_promoted": app["class"] in promoted,
            "base": {s: app[s] for s in STATS},
            "growth": _pairs(game.growth[char_id]),
            "learn": _learned(game, char_id),
            "joins": joins(game, join),
            "routes": rts,
        })
    return {"stats": list(STATS), "promote_min": PROMOTE_MIN_LEVEL,
            "cap_normal": ORDINARY_LEVEL_CAP, "chars": chars}


def expand(m):
    """Every level of every join x route x promotion level."""
    out = []
    for c in m["chars"]:
        jo = []
        for j in c["joins"]:
            brows = base_rows(c["base"], c["growth"], j["level"], c["cap"])
            by_lv = {r["lv"]: r for r in brows}
            rts = []
            for r in c["routes"]:
                rts.append({"form": r["form"],
                            "by_promote_level": {str(p): promo_rows(by_lv[p], r["growth"])
                                                 for p in promote_levels(j["level"])}})
            jo.append({"level": j["level"], "base_rows": brows, "routes": rts})
        out.append({"id": c["id"], "name": c["name"], "joins": jo})
    return {"stats": m["stats"], "chars": out}


def write(m, out_dir=OUT_DIR):
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "growth_compact.json").write_text(
        json.dumps(m, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    (out_dir / "growth_data.json").write_text(
        json.dumps(expand(m), ensure_ascii=False, separators=(",", ":")), encoding="utf-8")


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", default=data_tables.DEFAULT_DUMP)
    args = parser.parse_args(argv)
    try:
        m = model(data_tables.load(args.dump))
    except (GrowthError, data_tables.TableError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    write(m)
    for c in m["chars"]:
        rts = ", ".join(f"{r['form']:02X} {r['cls']} ({r['badge'] or 'no badge'})" for r in c["routes"]) or "-"
        jl = "/".join(f"LV{j['level']} {j['how']}" for j in c["joins"])
        promoted = " (joins promoted)" if c["joins_promoted"] else ""
        print(f"{c['id']:02X} {c['name']}: join {jl}; cap {c['cap']}{promoted}; routes {rts}")
    print(f"wrote {OUT_DIR / 'growth_compact.json'} and growth_data.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
