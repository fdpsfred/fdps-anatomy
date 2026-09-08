/* mapai.h -- the map AI: what one computer-controlled actor does on its turn,
 * and the map queries that decision needs.
 *
 * Nothing here owns state.  The decisions are taken from the unit records, the
 * movement grid and the map layers, all of which belong to other files; this
 * one only reads them.
 */
#ifndef MAPAI_H
#define MAPAI_H

/* Runs the actor at unit_index through one whole turn of its own behaviour,
   and shows the result.

   The behaviour is the LOW NIBBLE of unit record byte 0x34; the high nibble is
   no part of it.  Eleven of the sixteen values name an arm -- 0, 1 and 2 are
   the plain fighters, 3 and 9 chase a named character, 4 and 7 walk to the
   destination in record bytes 0x35 and 0x36, 5 opens the chest belonging to
   the actor's event slot, and 10 and 11 are the item- and spell-carrying
   fighters.  8 is the idle one and 6, 12, 13, 14 and 15 name no arm at all.

   A retired actor -- bit 0 of the flags byte -- is left alone entirely.  So is
   an actor in behaviour 8: it is the ONE live behaviour that skips the tail
   below, which is what makes it idle rather than merely inactive.

   Every other path, including the five behaviours that name no arm, ends by
   reporting the tile the actor is now standing on as a turn-end tile event,
   marking the actor for redraw and drawing one view frame.

   side_select is the acting side and is forwarded unchanged to every handler
   this dispatches to.  The two turn drivers pass 0 for the enemy phase and 1
   for the NPC phase; the title demo passes the actor's own side byte. */
extern void fdps_map_actor_behavior_step(int actor_index, int side_select);
#pragma aux fdps_map_actor_behavior_step "*" parm caller [];

/* Takes the best of the three actions src/aiscore.c has scored -- the physical
   attack, the spell and the item -- and answers 1 once one of them has been
   carried out, 0 when none of the three scored high enough to be worth taking.

   The three searches are run first, so this both decides and acts; a caller
   that reads 0 has had all three searches run for it and may go on to a
   movement fallback. */
extern int fdps_map_actor_take_best_action(int unit_index, int side_select);
#pragma aux fdps_map_actor_take_best_action "*" parm caller [];

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

/* Walks the actor at unit_index toward the opposing unit that is nearest to it
   IN A STRAIGHT LINE -- the sum of the two axis distances, terrain ignored
   entirely -- and answers whether it moved.  This is the fallback the behaviour
   dispatcher tries once the path-cost search above has declined the actor.

   Every unit index from 0 to data_fdps_map_unit_count-1 is scored.  A unit
   counts only when it has not retired and when its side byte disagrees with
   side_select AS A TRUTH VALUE: side_select 0 accepts every unit whose side
   byte is non-zero, and any non-zero side_select accepts only side byte 0.
   Side numbers are never compared, so an actor whose own side byte passes that
   filter is scored along with everybody else, wins its own tile at distance 0
   and this function moves nothing.  A tie in distance keeps the lower unit
   index.

   The actor is then sent toward the winning unit's own tile -- which is
   occupied, so what it actually reaches is the tile nearest it that the walk
   can finish on.  Unlike the path-cost sibling this one touches the movement
   grid only through that move: it neither floods nor resets.

   The result is 1 only when a walk was played.  0 comes back when no unit
   passed the filter, when the tile that won is the one the actor is already on,
   and when the move produced no steps; the caller reads that 0 as "this handler
   declined the actor".

   side_select is forwarded unchanged to the move as well as driving the filter.
   The cursor draw mode is left at 1, the ordinary box, whenever the move branch
   was taken -- it is stored, not restored -- and is not touched at all when it
   was not. */
extern int fdps_map_actor_move_toward_nearest_opponent(int unit_index,
                                                       int side_select);
#pragma aux fdps_map_actor_move_toward_nearest_opponent "*" parm caller [];

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
