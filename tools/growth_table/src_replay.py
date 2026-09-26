"""Independent check of gen_growth: replay the game's own code, level by level,
and compare every figure with the generator's closed-form rows.

Nothing here reuses gen_growth's rules.  Each function below is a line-by-line
transcription of the src/ routine it names, working on a unit record with
16-bit stat fields, and the rand() of a level-up is replaced by a chooser that
answers either 0 (the floor) or range - 1 (the ceiling) -- the two ends of
`rand() % range`.  The level cap, who may promote, which badge sends a
character down which route, and what a promotion adds are all left to the
transcribed code to decide; the generator's answers are only read to compare.

What the replay takes from outside src/: the table records (tools/data_tables)
and each character's join level (gen_growth.JOINS, from assets/characters.md).

Usage:
    python tools/growth_table/src_replay.py [--dump DIR]
"""
import argparse
import itertools
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools" / "data_tables"))
import data_tables  # noqa: E402
import gen_growth  # noqa: E402  (only its model and closed-form output are compared)

# src/unitstat.c
MACHINE_SOLDIER_PORTRAIT_ID = 9
MACHINE_SOLDIER_LEVEL_CAP = 0x63
ORDINARY_LEVEL_CAP = 0x28
# src/church.c
PROMOTE_MIN_LEVEL = 0x14
PROMOTABLE_PORTRAIT_COUNT = 9
ITEM_HERO_BADGE, ITEM_LIGHT_BADGE, ITEM_DARK_BADGE = 0xDB, 0xE0, 0xE1
HERO_BADGE_PORTRAIT_ID = 0
PROMOTED_LEVEL = 1


def s16(v):
    """A C short: what a 16-bit field holds after the assignment."""
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def c_mod(a, b):
    """C's %, which truncates toward zero."""
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return a - b * q


class Record:
    """The fields of struct fdps_unit_record the routines below touch."""

    def __init__(self, char_id):
        self.char_id = char_id
        self.portrait_id = char_id
        self.level = 0
        self.move = 0
        self.ap_base = self.dp_base = self.dx_base = 0
        self.hp_current = self.hp_max = self.mp_current = self.mp_max = 0
        self.bag = set()

    def figures(self):
        return {"hp": self.hp_max, "mp": self.mp_max, "ap": self.ap_base,
                "dp": self.dp_base, "dx": self.dx_base}


def growth_record(game, portrait_id):
    g = game.growth[portrait_id]
    return {"ap_min": g["ap"][0], "ap_max": g["ap"][1], "dp_min": g["dp"][0], "dp_max": g["dp"][1],
            "dx_min": g["dx"][0], "dx_max": g["dx"][1], "hp_min": g["hp"][0], "hp_max": g["hp"][1],
            "mp_min": g["mp"][0], "mp_max": g["mp"][1]}


def roster_add_character(game, char_id, level):
    """src/roster.c fdps_roster_add_character, with the level a deployment
    supplies (src/deploy.c fdps_deploy_unit, char id below 0x3C, computes the
    five figures and the move the same way)."""
    base_record = game.appearance[char_id]
    growth = growth_record(game, char_id)
    member = Record(char_id)
    hp_start = base_record["hp"] + growth["hp_min"] * (level - 1)
    mp_start = base_record["mp"] + growth["mp_min"] * (level - 1)
    member.level = level
    member.ap_base = s16(base_record["ap"] + level * growth["ap_min"])
    member.dp_base = s16(base_record["dp"] + level * growth["dp_min"])
    member.move = base_record["move"]
    member.dx_base = s16(base_record["dx"] + growth["dx_min"] * level)
    member.hp_current = member.hp_max = s16(hp_start)
    member.mp_current = member.mp_max = s16(mp_start)
    return member


def level_up_apply_stat_gain(stat, growth_pair, choose):
    """src/unitstat.c fdps_level_up_apply_stat_gain; `choose(range)` stands in
    for rand() and is only asked when the range is not zero."""
    min_gain = growth_pair[0]
    gain_range = growth_pair[1] - min_gain
    gain_offset = 0
    if gain_range != 0:
        gain_offset = c_mod(choose(gain_range), gain_range)
    return s16(stat + s16(min_gain + gain_offset))


def award_level(game, rec, choose):
    """The part of src/unitstat.c fdps_unit_award_exp_and_level_up that decides
    whether a level can be bought and what it pays.  Returns False at the cap."""
    unit_level = rec.level
    unit_portrait_id = rec.portrait_id
    if unit_portrait_id == MACHINE_SOLDIER_PORTRAIT_ID:
        if unit_level == MACHINE_SOLDIER_LEVEL_CAP:
            return False
    elif unit_level == ORDINARY_LEVEL_CAP:
        return False
    g = growth_record(game, rec.portrait_id)
    rec.level += 1
    rec.ap_base = level_up_apply_stat_gain(rec.ap_base, (g["ap_min"], g["ap_max"]), choose)
    rec.dp_base = level_up_apply_stat_gain(rec.dp_base, (g["dp_min"], g["dp_max"]), choose)
    rec.dx_base = level_up_apply_stat_gain(rec.dx_base, (g["dx_min"], g["dx_max"]), choose)
    rec.hp_max = level_up_apply_stat_gain(rec.hp_max, (g["hp_min"], g["hp_max"]), choose)
    rec.mp_max = level_up_apply_stat_gain(rec.mp_max, (g["mp_min"], g["mp_max"]), choose)
    return True


def church_candidate_route(rec):
    """src/church.c fdps_church_promote_loop's sweep: None when the member is
    not a candidate, else the route the bag picks."""
    if not (rec.level >= PROMOTE_MIN_LEVEL and rec.portrait_id < PROMOTABLE_PORTRAIT_COUNT):
        return None
    if rec.portrait_id == HERO_BADGE_PORTRAIT_ID and ITEM_HERO_BADGE in rec.bag:
        return 3
    if ITEM_LIGHT_BADGE in rec.bag:
        return 1
    if ITEM_DARK_BADGE in rec.bag:
        return 2
    return 0


def church_promote(game, rec, route):
    """src/church.c fdps_church_promote_unit and the stat payout of
    fdps_church_promote_loop."""
    promotion_record = game.promotions[rec.char_id]
    form, clazz, move = promotion_record[route]
    rec.portrait_id = form
    rec.move = (rec.move + move) & 0xFF
    g = growth_record(game, form)
    rec.ap_base = s16(rec.ap_base + s16(g["ap_max"]))
    rec.dp_base = s16(rec.dp_base + s16(g["dp_max"]))
    rec.dx_base = s16(rec.dx_base + s16(g["dx_max"]))
    rec.hp_current = s16(rec.hp_current + s16(g["hp_max"]))
    rec.hp_max = s16(rec.hp_max + s16(g["hp_max"]))
    rec.mp_current = s16(rec.mp_current + s16(g["mp_max"]))
    rec.mp_max = s16(rec.mp_max + s16(g["mp_max"]))
    rec.level = PROMOTED_LEVEL
    return form, clazz, move


FLOOR = ("min", lambda gain_range: 0)
CEILING = ("max", lambda gain_range: gain_range - 1)
BADGES = (ITEM_LIGHT_BADGE, ITEM_DARK_BADGE, ITEM_HERO_BADGE)


def replay_to_cap(game, rec, choose):
    """Figures at the current level, then after every level the award pays."""
    rows = {rec.level: rec.figures()}
    while award_level(game, rec, choose):
        rows[rec.level] = rec.figures()
    return rows


def compare(game, model):
    """Every figure of gen_growth's rows against the replay.  Returns the
    number of values compared and a list of mismatch descriptions."""
    full = {c["id"]: c for c in gen_growth.expand(model)["chars"]}
    mismatches = []
    values = 0

    def check(where, variant, want_rows, got_rows):
        nonlocal values
        if [r["lv"] for r in want_rows] != sorted(got_rows):
            mismatches.append(f"{where}: levels {want_rows[0]['lv']}..{want_rows[-1]['lv']} "
                              f"vs replay {min(got_rows)}..{max(got_rows)}")
            return
        for r in want_rows:
            for s in gen_growth.STATS:
                values += 1
                if r[variant][s] != got_rows[r["lv"]][s]:
                    mismatches.append(f"{where} LV{r['lv']} {s}.{variant}: generator "
                                      f"{r[variant][s]}, replay {got_rows[r['lv']][s]}")

    for c in model["chars"]:
        char_id = c["id"]
        for j_index, j in enumerate(c["joins"]):
            want = full[char_id]["joins"][j_index]
            for variant, choose in (FLOOR, CEILING):
                where = f"{c['name']} join LV{j['level']} base"
                check(where, variant, want["base_rows"], replay_to_cap(game, roster_add_character(game, char_id, j["level"]), choose))

                # Level to every possible promotion point and try every bag.
                reached = {}
                for level in range(j["level"], ORDINARY_LEVEL_CAP + 1):
                    for n in range(len(BADGES) + 1):
                        for bag in itertools.combinations(BADGES, n):
                            rec = roster_add_character(game, char_id, j["level"])
                            while rec.level < level and award_level(game, rec, choose):
                                pass
                            if rec.level != level:
                                continue
                            rec.bag = set(bag)
                            route = church_candidate_route(rec)
                            if route is None:
                                continue
                            form, clazz, move = church_promote(game, rec, route)
                            rows = replay_to_cap(game, rec, choose)
                            reached.setdefault(form, {})[level] = (rows, game.class_name(clazz), move)
                gen_forms = [r["form"] for r in c["routes"]]
                if sorted(reached) != sorted(gen_forms):
                    mismatches.append(f"{c['name']}: replay reaches forms {sorted(reached)}, "
                                      f"generator lists {sorted(gen_forms)}")
                    continue
                for r_index, r in enumerate(c["routes"]):
                    by_level = reached[r["form"]]
                    gen_levels = sorted(int(p) for p in want["routes"][r_index]["by_promote_level"])
                    if sorted(by_level) != gen_levels:
                        mismatches.append(f"{c['name']} {r['cls']}: replay promotes at "
                                          f"{sorted(by_level)}, generator at {gen_levels}")
                        continue
                    for level in gen_levels:
                        rows, cls_name, move = by_level[level]
                        if (cls_name, move) != (r["cls"], r["move_bonus"]):
                            mismatches.append(f"{c['name']} {r['cls']}: replay class/move {cls_name}/{move}")
                        check(f"{c['name']} join LV{j['level']} {r['cls']} promoted at LV{level}", variant,
                              want["routes"][r_index]["by_promote_level"][str(level)], rows)
    return {"values": values, "mismatches": mismatches}


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", default=data_tables.DEFAULT_DUMP)
    args = parser.parse_args(argv)
    game = data_tables.load(args.dump)
    report = compare(game, gen_growth.model(game))
    for m in report["mismatches"][:40]:
        print("MISMATCH " + m)
    if report["mismatches"]:
        print(f"FAIL: {len(report['mismatches'])} of {report['values']} values differ")
        return 1
    print(f"OK: {report['values']} values agree with the replay of src/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
