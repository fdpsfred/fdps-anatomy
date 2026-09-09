/* chevt5b.h -- the scripted chapter-event handlers of chapters 26 and 27.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the turn-event dispatcher
 * passes 0 when the event is turn-scheduled rather than unit-attached, while
 * the cell search and the death-script runner pass a real index.
 *
 * chevt1.h holds the same family for chapters 2 to 7, chevt2.h for chapter 8,
 * chevt2b.h for 9 to 14, chevt3.h for 15 to 19, chevt4.h for 20 to 23,
 * chevt5.h for 24 and 25 and chevt6.h for 28 to 30.  Nothing here owns
 * state.
 */
#ifndef CHEVT5B_H
#define CHEVT5B_H

/* Chapter 26's turn-4 event: one line of the chapter's own FDETXT26.TXT block
   is spoken, and then the two ten-unit holding groups of the tower garrison
   come off their holding behaviour and start advancing on the player.

   MAP25.DAT's only live turn-event record is (turn 4, this slot, side 0), so
   the event fires as the enemy phase of turn 4 opens -- immediately after the
   player's fourth turn ends.  What it releases is unit indices 0x1a..0x2d,
   deployment records 14..33 of the map's wave-0 block: two identical ten-unit
   groups, every record of which the map file deploys in the holding behaviour
   mode.  The commanders below them and the sixteen units above them keep
   holding until fdps_chapter_26_event_deploy_waves_2_and_3, the next slot of
   the same table, sweeps the whole wave-0 block.

   ONLY THE BEHAVIOUR NIBBLE IS WRITTEN.  Each record's AI byte is merged, not
   stored: the low nibble -- the mode fdps_map_actor_behavior_step dispatches
   on -- goes to 0, and the high nibble is carried across because two of its
   bits are per-unit flags the target scorers read on their own.

   THE RANGE IS INCLUSIVE.  The last unit released is 0x2d and not 0x2c, and
   the record above it is where the map's already-advancing units begin.

   unit_index is the handler table's shared parameter and this handler does not
   use it: the turn-event dispatcher that reaches this slot passes 0 and the
   body overwrites the slot before anything else happens.

   Table slot 40 at 00060264. */
extern void fdps_chapter_26_event_enemies_advance(int unit_index);
#pragma aux fdps_chapter_26_event_enemies_advance "*" parm caller [];

/* Chapter 26's mid-map ambush: the first time a unit on the player's side
   walks onto the map's trigger tile, the enemy wave and the allied relief wave
   both come onto the battlefield and the lower half of the deployment block
   stops holding position.

   TWO GATES, BOTH REFUSING THE WHOLE BODY: the shared one-shot latch must
   still be clear, and the triggering unit's side byte must be 2, the player's
   own roster.  An enemy or a guest crossing the tile changes nothing and
   leaves the ambush armed for the unit that comes next.  The latch is the same
   element fdps_chapter_25_event_deploy_wave_1 (chevt5.h) uses; the two events
   live on different maps and the block is cleared between chapters, so they
   cannot collide.

   THE ORDER IS ENEMY WAVE, LINE, PAN AND HOLD, LINE, ALLIED WAVE, LINE.  The
   enemy wave is already standing when its line is spoken; the allied wave is
   announced before it arrives.  The pan hides the cursor, walks the view to
   the bottom of the map, holds it there for twelve composed frames and then
   puts the cursor back -- as a literal 1, not as whatever it found.

   THE RELEASE RANGE IS TWO LITERALS, 0x0c..0x4f INCLUSIVE, and not the unit
   count: the twelve party slots below it and every unit from 0x50 upward keep
   the behaviour mode they already carry.  On MAP25.DAT the two literals cover
   the map's wave-0 block exactly -- 68 of its 80 deployment records are tagged
   wave 0, seven wave 2 and five wave 3 -- so what is released is the garrison
   alone, and neither the enemy wave nor the allied wave this handler has just
   brought on starts moving with it.

   ONLY THE BEHAVIOUR NIBBLE IS WRITTEN.  Each released record's AI byte is
   merged, not stored: the low nibble goes to 0 and the high nibble is carried
   across because two of its bits are per-unit flags the target scorers read on
   their own.

   unit_index is the handler table's shared parameter: the index of the unit
   that tripped the tile event, not range checked.  It is read once, resolved
   through fdps_get_unit_record (unit.h) for the side byte, and nothing else in
   the body looks at it.

   Table slot 41 at 00060268. */
extern void fdps_chapter_26_event_deploy_waves_2_and_3(int unit_index);
#pragma aux fdps_chapter_26_event_deploy_waves_2_and_3 "*" parm caller [];

/* Chapter 26's wave-2 wipe line: the death script the seven enemy
   reinforcements of the chapter's mid-map ambush all carry, which speaks one
   line of the chapter's own FDETXT26.TXT block once the last of the seven is
   gone and then latches itself off.

   The seven are the units at indices 0x50..0x56 --
   fdps_chapter_26_event_deploy_waves_2_and_3 above appends MAP25.DAT's seven
   wave-2 records there, on top of the twelve party slots and the map's 68
   wave-0 records, and brings the five wave-3 allies on behind them at
   0x57..0x5b.  Every one of the seven carries the same death script, so this
   handler runs once for each of them as it dies and does nothing until the
   last run finds all seven retired.

   TWO GATES, BOTH REFUSING THE WHOLE BODY: the one-shot latch must still be 0,
   and every one of the seven must answer fdps_unit_is_retired (unit.h) with a
   non-zero.  The latch is element 0x12 of
   data_fdps_map_cell_event_triggered_flags (gamedata.h) and NOT either of the
   elements the two handlers above use; the chapter 27 handler at 00039440
   latches the same element in the chapter that follows, which is safe because
   one chapter is loaded at a time and fdps_chapter_state_reset clears the
   whole block when a chapter starts.

   THE POLL IS NOT STOPPED BY THE FIRST SURVIVOR.  All seven are asked on every
   firing and the answers are gathered into one flag; a rewrite that broke out
   of the loop asks fewer of them.  fdps_unit_is_retired only reads a record, so
   nothing observes the difference beyond the call count.

   THE TEST DEPENDS ON THE CALLER'S ORDER.  fdps_map_actor_move_and_attack
   collects the death scripts of the killed, then marks them retired, and only
   then runs the scripts, so the seventh unit already reads as retired on the
   pass that kills it.  Running the scripts before the marking leaves the line
   permanently unspoken.

   Message 0x17 opens with the -0x11 speaker token carrying character id 0x0c,
   MAP25.DAT's single wave-3 ally, so the line is the guest hero's.

   unit_index is the handler table's shared parameter.  The dispatcher that
   reaches this slot, fdps_run_death_scripts, forwards the index of the unit
   that made the killing action rather than that of the dead unit whose script
   is running, and this handler reads neither: the incoming value is stored over
   on entry.

   Table slot 42 at 0006026c. */
extern void fdps_chapter_26_event_wave_2_defeated_line(int unit_index);
#pragma aux fdps_chapter_26_event_wave_2_defeated_line "*" parm caller [];

/* Chapter 27's four-generals death script: the script all four of the Mage
   King's generals carry, which once the last of them is gone speaks his line,
   brings the map's wave-1 reinforcements onto the battlefield and takes him out
   of hold-position behaviour, and then latches itself off.

   The four are the units at indices 0x0d..0x10 -- MAP26.DAT's deployment
   records 1 to 4, landing behind the map's twelve party slots -- and the unit
   released is 0x0c, record 0, the Mage King himself.  Every one of the four
   carries the same death script, so this handler runs once for each of them as
   it dies and does nothing until the last run finds the whole group retired.

   TWO GATES, BOTH REFUSING THE WHOLE BODY: the one-shot latch must still be 0,
   and every one of the four must answer fdps_unit_is_retired (unit.h) with a
   non-zero.  The latch is element 0x12 of
   data_fdps_map_cell_event_triggered_flags (gamedata.h), the same element
   fdps_chapter_26_event_wave_2_defeated_line above uses in the chapter before
   this one, which is safe because one chapter is loaded at a time and
   fdps_chapter_state_reset clears the whole block when a chapter starts.

   THE POLL IS NOT STOPPED BY THE FIRST SURVIVOR.  All four are asked on every
   firing and the answers are gathered into one flag; a rewrite that broke out of
   the loop asks fewer of them.  fdps_unit_is_retired only reads a record, so
   nothing observes the difference beyond the call count.

   THE ORDER IS LINE, WAVE, RELEASE, LATCH.  The boss's line is spoken while the
   reinforcements are still off the board and he himself is still holding
   position, and the latch is written after all three have happened.

   ONLY THE BEHAVIOUR NIBBLE OF THE RELEASED RECORD IS WRITTEN.  The Mage King's
   AI byte is merged, not stored: the low nibble -- the mode
   fdps_map_actor_behavior_step dispatches on -- goes to 0x0b, the spell-first
   chain that ends in a movement routine, and the high nibble is carried across
   because two of its bits are per-unit flags the target scorers read on their
   own.  The mode the map file deploys him in is 2, which never leaves its tile.

   THE TEST DEPENDS ON THE CALLER'S ORDER.  fdps_run_death_scripts is reached
   only after fdps_play_death_animation_and_mark_dead has marked the dead
   retired, so the fourth general already reads as retired on the pass that kills
   him.  Running the scripts before the marking leaves the wave permanently
   undeployed.

   Message 0x14 opens with the -0x11 speaker token carrying character id 0x3f,
   the chapter's boss, so the line is the Mage King's own.

   unit_index is the handler table's shared parameter.  The dispatcher that
   reaches this slot, fdps_run_death_scripts, forwards the index of the unit that
   made the killing action rather than that of the dead unit whose script is
   running, and this handler reads neither: the incoming value is stored over
   before anything else in the guarded block happens.

   Table slot 43 at 00060270. */
extern void fdps_chapter_27_event_deploy_wave_1(int unit_index);
#pragma aux fdps_chapter_27_event_deploy_wave_1 "*" parm caller [];

#endif
