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

#endif
