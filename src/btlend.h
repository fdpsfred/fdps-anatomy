/* btlend.h -- deciding a battle is over, and the window that reports it.
 *
 * The tallies here are what the win/fail test and the result window are
 * phrased in: how many units of a given side are still standing.  Nothing in
 * btlend.c owns state -- it reads the battle unit array through unit.h and the
 * unit count through gamedata.h.
 */
#ifndef BTLEND_H
#define BTLEND_H

/* How many units of one side are still in the battle?  Walks battle unit
   indices 0..data_fdps_map_unit_count-1 (gamedata.h) and counts the ones whose
   side byte -- struct fdps_unit_record's side at record offset 6 -- equals
   side and which fdps_unit_is_retired (unit.h) reports have not left the
   battle.  Retired units are passed over, so the answer is a survivor tally
   and not a roster size; it is 0 when nothing matches, and 0 when the unit
   count is 0 or below because the bound is tested before the body runs.

   side is a side code, not a truth value, and is compared for equality against
   the record byte widened to 0..255: 0 is the enemy side, 2 is the player
   side, and 1 is the third side the result window reports separately.  A side
   code no unit carries is not an error and simply counts nothing.

   The bound is the live unit count and the index is not otherwise range
   checked; each record is resolved through fdps_get_unit_record as the walk
   reaches it, so no record pointer is held across the loop. */
extern int fdps_battle_count_remaining_units_on_side(int side);
#pragma aux fdps_battle_count_remaining_units_on_side "*" parm caller [];

#endif
