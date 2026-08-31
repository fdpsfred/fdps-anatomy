/* chevt3.h -- the scripted chapter-event handlers of chapters 15 to 19.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the turn-event dispatcher
 * passes 0 when the event is turn-scheduled rather than unit-attached, while
 * the cell search and the death-script runner pass a real index.
 *
 * chevt1.h holds the same family for chapters 2 to 7 and chevt2.h for chapters
 * 8 to 14.  Nothing here owns state.
 */
#ifndef CHEVT3_H
#define CHEVT3_H

/* Chapter 15's death-triggered event: takes unit indices 0x1d through 0x25
   inclusive off the hold-position behaviour the map deploys them in and puts
   them on the default one, which paths a unit toward the nearest opposing
   unit, so the enemy group holding the bridgehead in the top-right corner of
   the map stops standing its ground and starts advancing on the party.

   It rewrites the low nibble -- the behaviour code -- of the ai_behavior byte
   at record offset 0x34 to 0 across that range and leaves the high nibble
   alone, because bits 0x40 and 0x80 of it are independent AI flags other code
   reads on their own.  The two bounds are literals; nothing is range checked
   and data_fdps_map_unit_count is not consulted, so they are only correct
   against chapter 15's own deployment, which puts 9 player records at indices
   0..8 and map14.dat's wave-0 records 1..43 at 9..0x33.  Each record is
   resolved through fdps_get_unit_record per iteration, so the array base is
   re-read.

   The nine indices are deployment records 21..29: three level 14 barbarian
   warriors at (23,3), (24,3) and (25,3), two level 19 ice mages at (23,4) and
   (25,4), three level 15 archers at (23,5), (24,5) and (25,5), and record 27,
   the level 16 beam turret at (16,10) that is this chapter's victory
   condition.  The turret has no movement allowance and both behaviour chains
   open with fdps_map_actor_take_best_action and close with fdps_unit_rest, so
   the mode change does not alter anything it does; it is inside the range
   because the range is contiguous, not because the handler is about it.

   There is no one-shot latch: nothing in the body guards the loop and nothing
   records that it ran, so calling it again runs it again.  The merge is
   idempotent, so a second firing changes nothing, but a unit the AI has since
   moved into another behaviour mode would be pushed back to mode 0.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read, so
   any index, in range or not, behaves the same.

   Table slot 20, and chapter 15's map14.dat is the only shipped file that
   names it -- as the death script of deployment record 8, the level 19 ice
   mage standing at tile (18,1) inside the stockade at the top of the map -- so
   the event fires when that unit is killed.  That file carries no turn events
   and no tile triggers at all, so the death script is the only route to this
   slot. */
extern void fdps_chapter_15_event_activate_enemy_group(int unit_index);
#pragma aux fdps_chapter_15_event_activate_enemy_group "*" parm caller [];

#endif
