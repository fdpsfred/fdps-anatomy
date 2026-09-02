/* chevt2.h -- the scripted chapter-event handlers of chapters 8 to 14.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the turn-event dispatcher
 * passes 0 when the event is turn-scheduled rather than unit-attached, while
 * the cell search and the death-script runner pass a real index.
 *
 * chevt1.h holds the same family for chapters 2 to 7.  Nothing here owns
 * state.
 */
#ifndef CHEVT2_H
#define CHEVT2_H

/* Chapter 10's stairway ambush: brings on the ten enemy reinforcements the
   chapter's map tags as wave 10, once.

   It fires only while both of two conditions hold: the shared one-shot latch,
   element 0x10 of data_fdps_map_cell_event_triggered_flags (gamedata.h), is
   still 0, and the record unit_index names is not on side 0.  The side test is
   unsigned and is only a test against 0, and side 0 is the enemy, 1 the
   guest/neutral one and 2 the player's roster -- so what it keeps out is an
   enemy unit stopping on the trigger tile, and the player's units and the
   guests spring it alike.  The
   record is resolved through fdps_get_unit_record (unit.h) and is not range
   checked, so an index outside the live unit array reads whatever lies at that
   stride.

   When it fires it puts the latch up first and then calls fdps_deploy_wave
   (deploy.h) with wave 10 and place_exact 0, so the reinforcements land on the
   nearest free walkable tile to their placement records rather than on the
   records' own coordinates.  The map number it deploys under is read from
   data_fdps_chapter_current_chapter_id at the call, not from anything the
   handler holds.

   The latch is one byte shared with a dozen other chapters' handlers and with
   the chapter 14, 15 and 26 post-action checks.  fdps_chapter_state_reset
   clears the whole array when a chapter starts, so a later chapter's handler
   begins from a clean latch; the save-state block carries it, so a chapter
   reloaded after its event fired does not fire it again.  Both halves are what
   make one shared slot safe, and neither survives rewriting this as a
   function-local static.

   Table slot 16, named by MAP09.DAT's tile-event entry 1 and by no other
   shipped map, which is what makes this chapter 10.  Chapter 10's other
   handler, slot 15, covers the same map's turn-scheduled waves. */
extern void fdps_chapter_10_event_deploy_wave_10(int unit_index);
#pragma aux fdps_chapter_10_event_deploy_wave_10 "*" parm caller [];

/* Chapter 13's death-triggered event: takes unit indices 9 through 0x2c
   inclusive off the hold-position behaviour the map deploys them in and puts
   them on the default one, which paths a unit toward the nearest opposing
   unit, so the map's enemies stop waiting to be lured out one at a time and
   start advancing.

   It rewrites the low nibble -- the behaviour code -- of the ai_behavior byte
   at record offset 0x34 to 0 across that range and leaves the high nibble
   alone, because bits 0x40 and 0x80 of it are independent AI flags other code
   reads on their own.  The two bounds are literals; nothing is range checked
   and data_fdps_map_unit_count is not consulted, so they are only correct
   against chapter 13's own deployment, which puts 9 player records at indices
   0..8 and map12.dat's 37 records at 9..0x2d.  Each record is resolved through
   fdps_get_unit_record per iteration, so the array base is re-read.

   The range stops one short of that last deployed unit: 0x2c is index 36 of
   the map's 37 records, so unit index 0x2d -- a level-16 unit at tile (15,9)
   -- keeps the mode the map gave it and goes on holding position.  That is the
   original's behaviour and not an oversight to correct.

   There is no one-shot latch: nothing in the body guards the loop and nothing
   records that it ran, so calling it again runs it again.  The merge is
   idempotent, so a second firing changes nothing, but a unit the AI has since
   moved into another behaviour mode would be pushed back to mode 0.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read, so
   any index, in range or not, behaves the same.

   Table slot 18, and chapter 13's map12.dat is the only shipped file that
   names it -- as the death script of deployment record 28, the dark mage
   standing at tile (14,9) that becomes unit index 0x25 -- so the event fires
   when that unit is killed. */
extern void fdps_chapter_13_event_enemies_advance(int unit_index);
#pragma aux fdps_chapter_13_event_enemies_advance "*" parm caller [];

#endif
