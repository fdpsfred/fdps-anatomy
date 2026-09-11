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

/* Fires the chapter script's turn-scheduled events for one side's phase of the
   current battle turn.  All sixteen entries of the turn-event table in the
   resident MAP%02d.DAT block are walked; an entry whose scheduled turn equals
   data_fdps_battle_turn_counter and whose side equals `side` calls its handler
   out of data_fdps_chapter_event_handler_table (chapter.h) with 0 as the unit
   index.  The walk does not stop at the first match, writes no global and
   returns nothing.

   `side` is in the encoding of unit record byte +6: 0 enemy, 1 friendly NPC,
   2 player.  fdps_battle_advance_turn is the only caller and passes 1 before
   the NPC pass, 0 before the enemy pass and 2 as it opens the next player
   phase. */
extern void fdps_battle_run_turn_events(int side);
#pragma aux fdps_battle_run_turn_events "*" parm caller [];

#endif
