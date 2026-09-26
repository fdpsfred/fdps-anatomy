/* aitarget.c -- target collection (area, line, range) and counter-attack
 * feasibility.
 *
 * See aitarget.h for what a collector is asked and what it answers.  Nothing
 * here owns state: every function reads the map unit array through
 * data_fdps_map_unit_array_ptr and reports on it.
 */
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "movegrid.h"
#include "table.h"
#include "unit.h"
#include "unititem.h"
#include "aitarget.h"

/* 000109f0.  Walks all data_fdps_map_unit_count records with stride 0x50 and
   counts the ones the targeting mode accepts.

   The distance test is CMP / JL against the caller's limit, so it is strictly
   less-than: the caller has already decremented the area-of-effect global, and
   writing dist <= max_dist here widens every area of effect by one tile.

   The mode table is this function's own.  fdps_collect_targets_in_range is
   called with the same ITEM.DAT byte 0x15 and MAGICDAT.DAT byte 0x06 in the
   same statement pairs but reads mode 2 as "side 1, no state test", where this
   one reads it as "side 2 that has already acted" (00010abb, 00010ac8).
   Folding the two tests into one shared helper changes which units the confirm
   key accepts.

   The match counter advances even when out_indices is NULL: 00010af3 skips the
   store at 00010afe, not the increment at 00010b03.  Every caller there is
   passes NULL, so folding the increment into the append would make the
   function always return 0.

   abs is the CRT call the original makes (CALL 0x0003d364, twice per record,
   before the retired bit is even looked at).  The flag set carries no -oi, so
   __INLINE_FUNCTIONS__ is not defined and stdlib.h leaves abs a call here
   too. */
int fdps_collect_targets_in_area(int tile_x, int tile_y, int max_dist,
                                 unsigned char *out_indices, int select_mode)
{
    struct fdps_unit_record *unit;
    int count;
    int index;
    int dist;

    count = 0;
    for (index = 0; index < data_fdps_map_unit_count; index++) {
        unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr + index;
        dist = abs((int) unit->pos_x - tile_x)
             + abs((int) unit->pos_y - tile_y);
        if ((unit->flags & 1) != 0 || dist >= max_dist) {
            continue;
        }
        if (((select_mode == 0) && (unit->side == 0)) ||
            ((select_mode == 1) && (unit->side != 0)) ||
            ((select_mode == 2) && (unit->side == 2)
                                && ((unit->flags & 0x80) != 0)) ||
            ((select_mode == 3) && (unit->side == 2))) {
            if (out_indices != NULL) {
                out_indices[count] = (unsigned char) index;
            }
            count++;
        }
    }
    return count;
}

#define AITARGET_MAP_TILE_SIZE 0x18

/* 00013670.  Walks the straight line a line-shaped item or spell sweeps out
   from the acting unit's tile toward the aimed tile and collects the units
   standing on it.

   The side test is this function's own and runs the OPPOSITE way round from
   the select_mode of the two collectors above, which the same callers feed
   from the same ITEM.DAT and MAGICDAT.DAT bytes: CMP [EBP+0x2c],0x0 / JNZ at
   00013788 keeps a NON-zero side byte when select_enemy_side is zero, and the
   second pair at 00013797 keeps side 0 when it is non-zero.  Writing the
   family's "0 means side 0" test here makes every line-shaped attack sweep the
   caster's own side (rebuild_info/pitfalls.md).

   The step is one orthogonal direction and never a diagonal: CMP at 000136b8
   compares origin_x with aim_x and only when they are equal is the vertical
   step taken; otherwise the horizontal step is taken and the y difference is
   discarded entirely.  Deriving the step from the sign of both deltas draws a
   line the original never draws, and that is reachable in play because the
   player's two call sites pass the free-moving map cursor as the aim tile.

   The two cursor globals are the whole channel into
   fdps_battle_find_unit_at_cursor (unit.h), which takes no arguments and reads
   them itself, so this routine points them at each tile in turn and puts the
   caller's values back at 000137be before returning.

   The step is added before the bounds test at 0001372d, so the origin tile is
   never examined; the four bounds tests are signed (JGE/JL at 00013747,
   00013750, 0001375c, 00013767) against the grid header's tile extents times
   0x18, and a tile outside them is skipped rather than ending the loop.

   The retired-unit filter the two collectors above apply for themselves is not
   repeated here: fdps_battle_find_unit_at_cursor already calls
   fdps_unit_is_retired and never reports a retired unit to this body.

   out_indices is written unconditionally -- there is no NULL branch, unlike
   the two collectors above -- and neither it nor line_length is bounds
   checked. */
int fdps_collect_targets_in_line(int aim_x, int aim_y,
                                 unsigned char *out_indices, int origin_x,
                                 int origin_y, int line_length,
                                 int select_enemy_side)
{
    struct fdps_unit_record *unit;
    int step_x;
    int step_y;
    int step_index;
    int found_count;
    int map_width_px;
    int map_height_px;
    int saved_cursor_x;
    int saved_cursor_y;
    int unit_index;

    /* All four are cleared up front at 0001367c-00013691 and step_index is
       cleared again at the loop head. */
    step_x = 0;
    step_y = 0;
    step_index = 0;
    found_count = 0;

    map_width_px = (int) *(short *) data_fdps_battle_move_grid_ptr
                 * AITARGET_MAP_TILE_SIZE;
    map_height_px = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2)
                  * AITARGET_MAP_TILE_SIZE;

    if (origin_x == aim_x) {
        if (aim_y < origin_y) {
            step_y = -AITARGET_MAP_TILE_SIZE;
        } else {
            step_y = AITARGET_MAP_TILE_SIZE;
        }
    } else if (aim_x < origin_x) {
        step_x = -AITARGET_MAP_TILE_SIZE;
    } else {
        step_x = AITARGET_MAP_TILE_SIZE;
    }

    saved_cursor_x = data_fdps_map_cursor_world_x;
    saved_cursor_y = data_fdps_map_cursor_world_y;
    data_fdps_map_cursor_world_x = origin_x * AITARGET_MAP_TILE_SIZE;
    data_fdps_map_cursor_world_y = origin_y * AITARGET_MAP_TILE_SIZE;

    for (step_index = 0; step_index < line_length; step_index++) {
        data_fdps_map_cursor_world_x = data_fdps_map_cursor_world_x + step_x;
        data_fdps_map_cursor_world_y = data_fdps_map_cursor_world_y + step_y;
        if (data_fdps_map_cursor_world_x >= map_width_px ||
            data_fdps_map_cursor_world_x < 0 ||
            data_fdps_map_cursor_world_y >= map_height_px ||
            data_fdps_map_cursor_world_y < 0) {
            continue;
        }
        unit_index = fdps_battle_find_unit_at_cursor();
        if (unit_index == -1) {
            continue;
        }
        unit = fdps_get_unit_record(unit_index);
        if (((select_enemy_side == 0) && (unit->side != 0)) ||
            ((select_enemy_side != 0) && (unit->side == 0))) {
            out_indices[found_count] = (unsigned char) unit_index;
            found_count++;
        }
    }

    data_fdps_map_cursor_world_x = saved_cursor_x;
    data_fdps_map_cursor_world_y = saved_cursor_y;
    return found_count;
}

/* 00011e50.  Paints the reachable set into the movement grid's marker bytes and
   then reports the units standing on a marked cell that select_mode accepts.

   It never resets the grid.  It reads the header out of
   data_fdps_battle_move_grid_ptr and starts writing straight away (00011e63),
   so it needs fdps_map_grid_reset to have left every marker at the 0xff
   sentinel; every call site in the image calls fdps_map_grid_reset again right
   after the return -- CALL 0x00010b20 at 000123fe, 00015dd0, 00025335 -- to
   wipe what this wrote.  Adding a reset at either end here would be a second
   one on top of the caller's.

   CMP [EBP+0x20],0x10 / JGE at 00011e7a splits the two shapes and they are not
   two spellings of the same disc.  Below 0x10 the reach is whatever
   fdps_move_grid_flood_fill_range spreads, and in practice that is the
   Manhattan diamond of radius range_code clipped to the map: the fill refuses
   only a cell carrying bit 0x40, which fdps_move_grid_mark_zone_of_control
   stamps on the tile a unit stands on, and all fifteen call sites reach here
   on a grid fdps_map_grid_reset has cleared of both zone bits -- every routine
   that marks zones resets again before it returns -- while the class record
   handed to the fill (below) costs one for every terrain type.  Neither units
   nor terrain stop it.  From 0x10 up the reach is range_code - 0x10 tiles
   along the centre's own row and column, no terrain is read, and the min_dist
   step is skipped entirely (rebuild_info/pitfalls.md).

   The flood fill is handed fdps_get_class_record(0) -- PUSH 0x0 at 00011e90 --
   which is PROMAP.DAT's leading default row and not any unit's class, so every
   terrain costs one movement point and range_code is a tile count.

   The min_dist test is CMP EAX,[EBP+0x24] / JGE at 00011f0f, so it is strictly
   less-than and min_dist is exclusive: 1 drops the centre tile alone.  The two
   arm tests are CMP EAX,[EBP+0x20] / JG at 00011f56 and 00011f9d, so the reach
   is inclusive.  Writing either the other way moves the ring by one tile.

   The straight-line branch subtracts 0x10 from range_code in place (00011f2a)
   and both arm loops compare against that same reduced value.

   select_mode's table is this function's own and is NOT the one
   fdps_collect_targets_in_area above reads from the same ITEM.DAT byte: mode 2
   here is side 1 with no state test (00012066), where that one wants side 2
   that has already acted.

   abs is the CRT call the original makes (CALL 0x0003d364).  The flag set
   carries no -oi, so __INLINE_FUNCTIONS__ is not defined and stdlib.h leaves it
   a call here too. */
int fdps_collect_targets_in_range(int tile_x, int tile_y,
                                  unsigned char *out_indices, int range_code,
                                  int min_dist, int select_mode)
{
    struct fdps_class_record *default_class_move_cost;
    struct fdps_move_grid_cell *sweep_cell;
    struct fdps_move_grid_cell *unit_cell;
    struct fdps_unit_record *unit;
    int grid_width;
    int grid_height;
    int match_count;
    int column;
    int row;
    int dist;
    int unit_index;
    int unit_tile_x;
    int unit_tile_y;

    match_count = 0;
    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

    if (range_code < 0x10) {
        default_class_move_cost = fdps_get_class_record(0);
        fdps_move_grid_flood_fill_range(default_class_move_cost,
                                        tile_x, tile_y, range_code);
        if (min_dist != 0) {
            sweep_cell = (struct fdps_move_grid_cell *)
                         (data_fdps_battle_move_grid_ptr + 4);
            for (row = 0; row < grid_height; row++) {
                for (column = 0; column < grid_width; column++) {
                    dist = abs(column - tile_x) + abs(row - tile_y);
                    if (dist < min_dist) {
                        sweep_cell->marker = (unsigned char) 0xff;
                    }
                    sweep_cell++;
                }
            }
        }
    } else {
        range_code = range_code - 0x10;
        for (column = 0; column < grid_width; column++) {
            if (abs(column - tile_x) <= range_code) {
                ((struct fdps_move_grid_cell *)
                 (data_fdps_battle_move_grid_ptr + 4))
                    [tile_y * grid_width + column].marker = 0;
            }
        }
        for (row = 0; row < grid_height; row++) {
            if (abs(row - tile_y) <= range_code) {
                ((struct fdps_move_grid_cell *)
                 (data_fdps_battle_move_grid_ptr + 4))
                    [row * grid_width + tile_x].marker = 0;
            }
        }
    }

    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        unit_tile_x = (int) unit->pos_x;
        unit_tile_y = (int) unit->pos_y;
        unit_cell = (struct fdps_move_grid_cell *)
                    (data_fdps_battle_move_grid_ptr + 4) +
                    (unit_tile_y * grid_width + unit_tile_x);
        if ((unit->flags & 1) != 0 || unit_cell->marker == 0xff) {
            continue;
        }
        if (((select_mode == 0) && (unit->side == 0)) ||
            ((select_mode == 1) && (unit->side != 0)) ||
            ((select_mode == 2) && (unit->side == 1)) ||
            ((select_mode == 3) && (unit->side == 2))) {
            if (out_indices != NULL) {
                out_indices[match_count] = (unsigned char) unit_index;
            }
            match_count++;
        }
    }
    return match_count;
}

/* 000125c0.  The counterattack question asked about a TILE rather than about a
   second unit, which is the form the map AI needs while it is still deciding
   where to stand.

   Four tests in this order, each failure storing -1 into the one result slot
   at [EBP-0x4] and jumping to the single epilogue at 000126a1: the paralysis
   counter, orthogonal adjacency, an equipped weapon, and that weapon's minimum
   range.  Only the fall-through stores 1.  The result is therefore 1 or -1 and
   never 0, and the caller at 000124a2 compares it with CMP EAX,1
   (rebuild_info/pitfalls.md); returning 0 for a refusal, which is what a
   rewrite into a bool-shaped predicate produces, is accepted by every `if` in
   sight and changes nothing until the AI starts counting counterattacks it was
   told would not happen.

   CMP byte ptr [EAX+0x26],0x0 / JZ at 000125de is a plain zero test on
   status_timers[4], the paralysis counter, and not a compare against any
   particular count.

   The adjacency test is CMP EAX,0x1 / JZ at 00012631 on the sum of the two
   CRT abs calls -- EQUAL to one, not at most one -- so the defender's own tile
   (sum 0) is refused along with every diagonal (sum 2).  Each delta is formed
   as the passed coordinate minus the record byte, the byte zero-extended by
   AND EAX,0xff, and handed to abs, so the two orders of subtraction are the
   same answer and the byte is never signed.

   CMP EAX,0x1 / JLE at 0001268c is the range test, so range_min 0 and 1 both
   pass.  The unit-index twin fdps_check_can_counter_attack at 000137e0 tests
   the same byte for equality with 1 instead, and the pair disagree about a
   unit carrying item 0x63 光束砲, whose range is 0-0 (assets/items.md).  The
   two are not one function with two argument shapes and must not be folded.
   range_max at item record +0x0c is not read here at all.

   The equipped-slot call is fdps_unit_find_equipped_slot(defender_unit, 0) --
   PUSH 0x0 first at 0001263f -- so it is the weapon and not the armour, and
   -1 from it is a refusal.  The slot then goes through fdps_unit_get_item_id
   and fdps_get_item_record; the original reuses the one stack slot at
   [EBP-0x10] for the slot number and then the item id, which is two values and
   is spelt as two here.

   abs is the CRT call the original makes (CALL 0x0003d364, twice).  The flag
   set carries no -oi, so __INLINE_FUNCTIONS__ is not defined and stdlib.h
   leaves abs a call here as it does in the two collectors above. */
int fdps_check_can_counter_attack_from_tile(int defender_unit, int attacker_x,
                                            int attacker_y)
{
    struct fdps_unit_record *defender;
    struct fdps_item_effect *weapon;
    int dist_x;
    int dist_y;
    int weapon_slot;
    int weapon_item_id;
    int can_counter;

    defender = fdps_get_unit_record(defender_unit);
    if (defender->status_timers[4] != 0) {
        can_counter = -1;
    } else {
        dist_x = abs(attacker_x - (int) defender->pos_x);
        dist_y = abs(attacker_y - (int) defender->pos_y);
        if (dist_x + dist_y != 1) {
            can_counter = -1;
        } else {
            weapon_slot = fdps_unit_find_equipped_slot(defender_unit, 0);
            if (weapon_slot == -1) {
                can_counter = -1;
            } else {
                weapon_item_id = fdps_unit_get_item_id(defender_unit,
                                                       weapon_slot);
                weapon = fdps_get_item_record(weapon_item_id);
                if (weapon->range_min < 2) {
                    can_counter = 1;
                } else {
                    can_counter = -1;
                }
            }
        }
    }
    return can_counter;
}

/* 000137e0.  The counterattack question asked about a second UNIT rather than
   about a tile, which is the form the fight itself and the three display
   routines need once both parties are on the map.

   Both records are resolved before anything is tested: CALL 0x0002d210 at
   000137f0 for the attacker and again at 000137ff for the defender, the two
   pointers parked at [EBP-0x1c] and [EBP-0x18], and only then does the first
   test run.  The attacker's record is fetched even on the path where the
   defender is paralysed and the tile bytes are never looked at.

   Four tests in the same order as the tile twin above, each failure storing -1
   into the one result slot at [EBP-0x4] and jumping to the single epilogue at
   000138d7; only the fall-through stores 1.  The result is 1 or -1 and never 0,
   and all six call sites -- 00012f04, 00012f6c, 00018f7b, 000190f5, 0001cdee,
   0001ef6c -- follow the CALL with CMP EAX,0x1.  A rewrite into a bool-shaped
   predicate returning 0 for a refusal passes every one of those compares in the
   same direction and is still wrong the moment somebody writes the natural
   `if (fdps_check_can_counter_attack(a, b))`.

   CMP byte ptr [EAX+0x26],0x0 / JZ at 0001380d is a plain zero test on the
   DEFENDER's status_timers[4], the paralysis counter.  Nothing about the
   attacker beyond its two tile bytes is examined anywhere in the body.

   Each adjacency delta is formed as the attacker's byte minus the defender's,
   both zero-extended -- XOR EDX,EDX / MOV DL,byte ptr [EAX] for the attacker
   and MOV AL,byte ptr [EAX] / AND EAX,0xff for the defender at 00013822 --
   so neither coordinate is ever signed and the order of subtraction does not
   matter once abs has run.  CMP EAX,0x1 / JZ at 00013867 is on the sum: EQUAL
   to one, so the defender's own tile at sum 0 and every diagonal at sum 2 are
   refused alike.

   CMP EAX,0x1 / JZ at 000138c2 is the range test and it is EQUALITY, where the
   tile twin fdps_check_can_counter_attack_from_tile at 000125c0 writes the same
   byte as CMP EAX,0x1 / JLE.  The pair therefore disagree about a defender
   holding item 0x63 光束砲, whose range is 0-0 (assets/items.md): the map AI
   predicts a counterattack that this function, the one the exchange actually
   consults, refuses.  That divergence is the behaviour; the two must not be
   folded into a shared helper.  range_max at item record +0x0c is not read.

   The equipped-slot call is fdps_unit_find_equipped_slot(defender_unit, 0) --
   PUSH 0x0 first at 00013875 -- so it is the weapon and not the armour, and -1
   from it is a refusal.  The original reuses the one stack slot at [EBP-0x8]
   for the slot number and then the item id, which is two values and is spelt as
   two here.

   abs is the CRT call the original makes (CALL 0x0003d364, twice).  The flag
   set carries no -oi, so __INLINE_FUNCTIONS__ is not defined and stdlib.h
   leaves abs a call here as it does in the three functions above. */
int fdps_check_can_counter_attack(int attacker_unit, int defender_unit)
{
    struct fdps_unit_record *attacker;
    struct fdps_unit_record *defender;
    struct fdps_item_effect *weapon;
    int dist_x;
    int dist_y;
    int weapon_slot;
    int weapon_item_id;
    int can_counter;

    attacker = fdps_get_unit_record(attacker_unit);
    defender = fdps_get_unit_record(defender_unit);
    if (defender->status_timers[4] != 0) {
        can_counter = -1;
    } else {
        dist_x = abs((int) attacker->pos_x - (int) defender->pos_x);
        dist_y = abs((int) attacker->pos_y - (int) defender->pos_y);
        if (dist_x + dist_y != 1) {
            can_counter = -1;
        } else {
            weapon_slot = fdps_unit_find_equipped_slot(defender_unit, 0);
            if (weapon_slot == -1) {
                can_counter = -1;
            } else {
                weapon_item_id = fdps_unit_get_item_id(defender_unit,
                                                       weapon_slot);
                weapon = fdps_get_item_record(weapon_item_id);
                if (weapon->range_min == 1) {
                    can_counter = 1;
                } else {
                    can_counter = -1;
                }
            }
        }
    }
    return can_counter;
}
