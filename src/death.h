/* death.h -- the death-script pipeline: what a unit owes when it is destroyed,
 * and the three steps that pay it out.
 *
 * A unit's death script is the 3-byte (opcode, signed 16-bit operand) pair at
 * struct fdps_unit_record's death_script_opcode / death_script_operand, record
 * offsets 0x31 and 0x32 (src/fdpstype.h).  fdps_deploy_unit fills it from
 * bytes 0x16 and 0x17 of the unit's MAP%02d.DAT deployment record, so only the
 * map's scripted units carry one; fdps_build_map_unit_array stamps 0xff, the
 * "no script" sentinel, into the opcode of every player-roster slot, which is
 * why the party is never paid for its own losses.
 *
 * Nothing in death.c owns state: the battle units are reached through unit.h
 * and their count through gamedata.h.
 */
#ifndef DEATH_H
#define DEATH_H

/* Which death scripts has the action just carried out become owed?  Walks
   battle unit indices 0..data_fdps_map_unit_count-1 (gamedata.h), resolving
   each record through fdps_get_unit_record (unit.h), and copies the 3-byte
   death script of every unit that passes all three tests into out_scripts,
   packed from the front with no gaps, three bytes per record.  Returns how
   many records were written, 0 when nothing qualifies.

   The three tests are: bit 0 of the record's flags byte at offset 5 is clear,
   i.e. the unit has not already been taken off the map; the opcode byte at
   offset 0x31 is not 0xff, the "no script" sentinel; and the signed hit-point
   word at offset 0x40 is at most 0, so a unit resting exactly at zero HP does
   qualify.

   The order against fdps_play_death_animation_and_mark_dead is load-bearing
   and not a matter of taste.  That routine sets the very flag bit the first
   test above rejects, so this collect has to run BEFORE it; running it after
   -- play the destruction sequence, then hand out the rewards, which reads as
   the natural order -- returns 0 every time and silently loses every item,
   gold payout and scripted chapter event owed for those kills.  All four call
   sites already spell the three steps in the right order: this function into a
   stack buffer, then fdps_play_death_animation_and_mark_dead, then
   fdps_run_death_scripts over what was collected.

   out_scripts is written with no bound of any kind: the walk can emit one
   record per unit on the map and the buffer's capacity is never communicated
   to this function.  The caller buffers are sized by nothing but the
   original's frame layout -- fdps_map_actor_move_and_attack leaves it 0x0c
   bytes, four records -- so a caller must keep its buffer at least as large as
   the original's.  A capacity check does not belong here; the original has
   none and adding one would drop records the callers do read.

   The sibling collector fdps_collect_death_script_events reads the same field
   under the same two flag and hit-point tests but accepts only opcodes 2..5,
   so the map-AI path that reaches it pays out no item and no gold. */
extern int fdps_collect_death_scripts(unsigned char *out_scripts);
#pragma aux fdps_collect_death_scripts "*" parm caller [];

/* Which death scripts does the map AI's spell kill owe the CHAPTER?  The same
   walk as fdps_collect_death_scripts over battle unit indices
   0..data_fdps_map_unit_count-1 (gamedata.h), under the same two tests -- bit
   0 of the record's flags byte at offset 5 clear, and the signed hit-point
   word at offset 0x40 at most 0 -- but accepting only death script opcodes 2
   to 5 inclusive.  Every accepted unit's 3-byte script is copied into
   out_events, packed from the front with no gaps; the count written is
   returned, 0 when nothing qualifies.

   Opcodes 2 to 5 are the half of the script vocabulary fdps_run_death_scripts
   carries out unconditionally: the chapter-event handler call, the scripted
   text line, and the two battle-end verdicts.  Opcodes 0 and 1, the item drop
   and the gold, are dropped here on purpose, which is why the sole caller
   fdps_map_actor_cast_chosen_spell also zeroes the pending-experience
   accumulator right after this call -- an AI spell kill pays the player
   nothing.  The suppression has to happen at the collect and not be left to
   the executor: the executor's own guard on a reward record returns out of the
   whole array rather than skipping the one record, so letting a reward opcode
   through would abandon every record behind it, the battle-end verdicts
   included.  For the same reason this and fdps_collect_death_scripts are two
   functions and not one shared helper with a flag; they differ only in this
   test, and the difference is the policy.

   The window also subsumes the sentinel: 0xff, the "no script" opcode
   fdps_build_map_unit_array stamps into every roster slot, is outside it, and
   the byte is widened without sign before the compare, so no opcode above the
   window is mistaken for a negative one.

   The order against fdps_play_death_animation_and_mark_dead is load-bearing in
   exactly the way it is for fdps_collect_death_scripts above: that routine
   sets the flag bit this walk rejects on, so a collect that runs after it
   returns 0 and silently loses every chapter event and battle-end verdict the
   kills owed.  The caller spells the steps in the right order.

   out_events is written with no bound of any kind; the only caller leaves it
   0x0c bytes, four records.  A capacity check does not belong here -- the
   original has none, and adding one would drop records the caller does read --
   so a caller must keep its buffer at least as large as the original's. */
extern int fdps_collect_death_script_events(unsigned char *out_events);
#pragma aux fdps_collect_death_script_events "*" parm caller [];

/* Plays the destruction sequence for every battle unit standing at exactly
   zero hit points, and takes those units off the map.  Reads its whole input
   from data_fdps_map_unit_count and the records fdps_get_unit_record resolves
   (gamedata.h, unit.h); takes nothing and returns nothing.

   WHICH UNITS IT TAKES IS NOT THE TEST THE TWO COLLECTORS ABOVE APPLY.  A unit
   qualifies when bit 0 of its flags byte is clear AND its hit-point word is
   EQUAL to zero -- the collectors accept anything at zero or below, so a unit
   the fight drove past zero owes its death script but is not destroyed by this
   routine.  Nothing bounds the 80-slot list the qualifying indices are
   collected into.

   Three phases, and nothing happens at all when the list comes out empty.
   First the dying units spin on the spot: thirteen frames, each storing the
   step number modulo 4 into every dying unit's facing byte and then presenting
   one frame through fdps_render_view_frame (mapdraw.h), so the last frame
   leaves every one of them facing 0, down.  Then every dying unit's flags byte
   is set to 1 -- a whole-byte store, not a bit set, so bit 7 and everything
   else in that byte is wiped along with it.  Then Explo.Saf, out of the
   resident BaseAni.vfs image at data_fdps_animation_baseani_archive_ptr, is
   played once over every dying unit's tile at the same time, one frame of the
   sheet per presented frame, with the frame's sound effect asked for on the
   first unit of the list only so it fires once a frame rather than once a
   dying unit.

   IT MARKS THE FLAG THE TWO COLLECTORS ABOVE REJECT ON, so both of them have
   to run before it; see fdps_collect_death_scripts for what running them
   afterwards costs.

   The explosion is presented straight to the mode 13h aperture, so the caller
   must already be in a graphics mode, and every frame ends waiting for
   data_fdps_timer_tick_counter to change: with the timer interrupt not
   installed the first frame of the spin never ends. */
extern void fdps_play_death_animation_and_mark_dead(void);
#pragma aux fdps_play_death_animation_and_mark_dead "*" parm caller [];

/* 0001d990.  Runs the death scripts one of the two collectors above gathered:
   pays the item and gold rewards of the units the action killed to the acting
   unit, fires their scripted chapter events and records the battle-end
   verdicts they carry.  Not emitted yet.

   actor_unit_index is the unit whose action caused the deaths -- it receives
   the rewards and supplies the message window's portrait -- script_count is
   what the collector returned, and scripts is the packed array of 3-byte
   records it filled.  A script_count of 0 does nothing at all. */
extern void fdps_run_death_scripts(int actor_unit_index, int script_count,
                                   unsigned char *scripts);
#pragma aux fdps_run_death_scripts "*" parm caller [];

#endif
