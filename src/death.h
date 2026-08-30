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

#endif
