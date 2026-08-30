/* aitarget.h -- collecting the units a targeting mode accepts, and deciding
 * whether a counter-attack is possible.
 *
 * Every collector here walks the map unit array and answers "which units does
 * this targeting mode accept", either as a count alone or as a list of unit
 * indices written into a caller-supplied byte array.  The select_mode
 * numbering is NOT shared between the collectors even though the same ITEM.DAT
 * and MAGICDAT.DAT bytes are passed to more than one of them; each function
 * documents its own table.
 */
#ifndef AITARGET_H
#define AITARGET_H

/* Counts the units within an exclusive Manhattan distance of one tile that
   select_mode accepts, appending each match's unit index to out_indices as one
   byte when that pointer is non-NULL.  Pass NULL to count only; the count
   advances either way.  Returns the number of matches.

   select_mode: 0 keeps side 0, 1 keeps every non-zero side, 2 keeps side 2
   that has already acted this turn, 3 keeps side 2, anything else keeps
   nothing. */
extern int fdps_collect_targets_in_area(int tile_x, int tile_y, int max_dist,
                                        unsigned char *out_indices,
                                        int select_mode);
#pragma aux fdps_collect_targets_in_area "*" parm caller [];

/* Marks the tiles an action used from (tile_x, tile_y) can reach into the
   marker byte of every movement grid cell, then counts the units standing on a
   marked tile that select_mode accepts, appending each match's unit index to
   out_indices as one byte when that pointer is non-NULL.  Pass NULL to count
   only; the count advances either way.  Returns the number of matches.

   The grid has to arrive as fdps_map_grid_reset (movegrid.h) leaves it -- every
   marker at the 0xff sentinel -- and is left dirty on return; the caller resets
   it again.

   range_code carries the reach and the shape together.  Below 0x10 it is that
   many movement points spread by fdps_move_grid_flood_fill_range over the
   PROMAP.DAT default class row, whose eight terrain costs are all 1, so it is a
   tile count that walls still cut short; min_dist then removes every tile whose
   Manhattan distance from the centre is below it, exclusively.  From 0x10 up it
   is a straight-line cross with arms of range_code - 0x10 tiles along the
   centre's row and column, inclusive, and min_dist is ignored.

   select_mode: 0 keeps side 0, 1 keeps every non-zero side, 2 keeps side 1, 3
   keeps side 2, anything else keeps nothing.  Mode 2 is NOT what
   fdps_collect_targets_in_area reads from the same ITEM.DAT byte.

   Nothing is bounds checked: neither the unit coordinates against the grid
   header, nor out_indices against the number of matches, nor the grid pointer
   against null. */
extern int fdps_collect_targets_in_range(int tile_x, int tile_y,
                                         unsigned char *out_indices,
                                         int range_code, int min_dist,
                                         int select_mode);
#pragma aux fdps_collect_targets_in_range "*" parm caller [];

#endif
