/* mapai.h -- the map AI: what one computer-controlled actor does on its turn,
 * and the map queries that decision needs.
 *
 * Nothing here owns state.  The decisions are taken from the unit records, the
 * movement grid and the map layers, all of which belong to other files; this
 * one only reads them.
 */
#ifndef MAPAI_H
#define MAPAI_H

/* Walks the actor at unit_index toward the opposing unit that is nearest to it
   OVER WALKABLE TERRAIN, and answers whether it moved.

   The movement grid is flooded from the actor's tile with an allowance large
   enough to cover the map, the cheapest reachable tile carrying an opposing
   unit is found, and the actor is sent toward that unit's own tile -- which is
   occupied, so what it actually reaches is the tile nearest it that the walk
   can finish on.  The grid is reset afterwards on every path and nothing this
   function computes is left in it.  It expects the grid to be blank on entry:
   it does not reset before flooding, and it marks no zones of control.

   The result is 1 only when a walk was played.  0 comes back when no opposing
   unit stands on a tile the flood reached, when the tile that won is the one
   the actor is already on, and when the move produced no steps; the caller
   reads that 0 as "this handler declined the actor".

   side_select is the acting side and is forwarded to the move alone.  THE
   TARGET SEARCH DOES NOT CONSULT IT: it is given a hard-coded side filter that
   selects the units whose side byte is non-zero, so on the NPC phase the actor
   itself qualifies, wins on its own tile and this function moves nothing.  And
   the terrain costs the flood uses are the row BELOW the actor's class, since
   the class code is passed without the +1 every other caller applies.  Both
   are the original's behaviour and are load-bearing
   (rebuild_info/pitfalls.md).

   The cursor draw mode is left at 1, the ordinary box, whenever the move
   branch was taken -- it is stored, not restored. */
extern int fdps_map_actor_move_toward_nearest_reachable_opponent(
               int unit_index, int side_select);
#pragma aux fdps_map_actor_move_toward_nearest_reachable_opponent "*" parm caller [];

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
