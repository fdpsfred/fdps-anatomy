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
   fdps_move_grid_flood_fill_range spreads over the terrain, so an impassable
   cell (flag bit 0x40) cuts it short; only the min_dist cut on that same call
   is Manhattan.  From 0x10 up the reach is range_code - 0x10 tiles along the
   centre's own row and column, no terrain is read, and the min_dist step is
   skipped entirely (rebuild_info/pitfalls.md).

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
