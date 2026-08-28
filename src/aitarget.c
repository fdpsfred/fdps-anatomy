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
