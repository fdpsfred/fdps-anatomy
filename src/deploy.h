/* deploy.h -- putting a chapter's scripted units onto the battle map.
 *
 * The scripted deployments themselves live in the resident MAP%02d.DAT block
 * reached through data_fdps_tile_event_data_table_ptr (gamedata.h), as
 * struct fdps_char_spawn_record.  Where each one is put comes from a second,
 * separate table -- the MAP%02d.COD placement block this file owns below.
 */
#ifndef DEPLOY_H
#define DEPLOY_H

#include <stdio.h>

/* 00060140.  Base of the map's placement table, loaded out of MAP%02d.COD by
   fdps_deploy_wave and released by it again at the end of the same call; the
   chapter loader fdps_build_map_unit_array loads and releases its own.  Null
   outside those two spans, and neither of them tests it before use.

   Only two places in the image read anything through it and both read the
   same shape: a pair of signed 16-bit tile coordinates at base + 0xb +
   index * 6, index being the deployment record's own index (0002330f here and
   00022d0a in fdps_build_map_unit_array).  Nothing reads the bytes in front of
   the first pair and nothing reads a third field, so how the table's header
   and its records divide up those 0xb bytes is not something the program
   settles -- struct fdps_map_spawn_pos_record in src/fdpstype.h is ticket 17's
   layout for the file, and the arithmetic here is the original's own.

   The original types the base as a byte pointer and does that arithmetic at
   the use site, so a reader casts. */
extern unsigned char *data_fdps_map_spawn_pos_table_ptr;

/* 000232b0.  Appends one scripted deployment to the battle's unit array:
   grows the array by one 0x50-byte record, resolves the tile to stand on,
   fills the whole record out of deployment record deploy_index, derives the
   combat stats from it and makes the record live by incrementing
   data_fdps_map_unit_count.

   deploy_index selects BOTH tables at once -- the 0x1a-byte deployment record
   in data_fdps_tile_event_data_table_ptr and the 6-byte placement record in
   data_fdps_map_spawn_pos_table_ptr -- and is not range checked against
   either.  The only caller, fdps_deploy_wave, walks the deployment table with
   it.

   icon_cel_fp is an ICON.CEL stream the caller has already opened.  Nothing
   here reads it; it is handed straight to fdps_cache_cel_sprite_group, which
   seeks and reads on it unconditionally, so it may not be null.

   place_exact non-zero puts the unit on the tile the placement record names.
   Zero searches the map for the nearest unoccupied walkable tile to it
   instead, and if that search finds nothing at all the unit is placed on an
   uninitialised pair of coordinates -- the original writes the chosen tile
   only from inside the accept branch.

   The unit array moves: the call reallocs it, so any record pointer held
   across this function must be re-resolved through fdps_get_unit_record
   afterwards. */
extern void fdps_deploy_unit(int deploy_index, FILE *icon_cel_fp,
                             unsigned char place_exact);
#pragma aux fdps_deploy_unit "*" parm caller [];

#endif
