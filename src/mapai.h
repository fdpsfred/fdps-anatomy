/* mapai.h -- the map AI: what one computer-controlled actor does on its turn,
 * and the map queries that decision needs.
 *
 * Nothing here owns state.  The decisions are taken from the unit records, the
 * movement grid and the map layers, all of which belong to other files; this
 * one only reads them.
 */
#ifndef MAPAI_H
#define MAPAI_H

/* Finds the treasure-chest cell carrying cell_code and reports where it is, so
   the AI can walk a unit to it.

   Scans the whole map in row-major order -- y outer, x inner -- over the
   dimensions in the movement grid's header, and takes the first cell whose
   terrain attribute names the treasure-chest kind and whose event code equals
   cell_code.  On a hit out_xy[0] receives the column and out_xy[1] the row,
   both as single bytes, and the result is 0; with no cell matching the result
   is -1 and out_xy is left untouched.  Note the sense: 0 is the success value.

   Only the chest kind matches.  Buried treasure -- the other openable kind --
   is deliberately passed over, so an AI actor never walks to one. */
extern int fdps_map_find_chest_cell(int cell_code, unsigned char *out_xy);
#pragma aux fdps_map_find_chest_cell "*" parm caller [];

#endif
