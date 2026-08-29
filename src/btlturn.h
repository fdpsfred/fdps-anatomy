/* btlturn.h -- the battle's turn and phase engine: the enemy, NPC and player
 * phases, the per-unit turn, and the advance from one turn to the next.
 *
 * The unit records these work on live in the one 0x50-byte array reached
 * through data_fdps_map_unit_array_ptr (src/gamedata.h), bounded by
 * data_fdps_map_unit_count; the record layout is struct fdps_unit_record in
 * src/fdpstype.h.  Nothing in this file owns state of its own.
 *
 * Byte +5 of a record, struct field `flags`, is the turn engine's own status
 * byte: bit 0 is the retired flag and bit 7 is the acted-this-turn flag.  The
 * phase engine tests both at once as flags & 0x81 and passes over any unit for
 * which that is non-zero, so a unit that is retired and a unit that has already
 * acted stop the phase waiting on them in exactly the same way.
 */
#ifndef BTLTURN_H
#define BTLTURN_H

/* Raises the acted-this-turn flag, bit 7 of the status byte, on the unit at
   unit_index, so the player phase stops waiting on it.  No other field is
   touched and nothing is returned.  unit_index is NOT range-checked against
   data_fdps_map_unit_count: the record address is formed and written whatever
   it holds.  The bit is cleared again for every unit when the next turn
   opens. */
extern void fdps_battle_mark_unit_done(int unit_index);
#pragma aux fdps_battle_mark_unit_done "*" parm caller [];

#endif
