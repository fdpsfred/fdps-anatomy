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

/* Chapter 16's turn-scheduled release event: it takes one block of the map's
   enemies off the hold-position behaviour the map deployed them in and puts
   them on the default one, which paths a unit toward the nearest opposing
   unit, and which block it releases depends on the battle turn counter.

   The counter is tested for equality against 5 and there are only two
   outcomes: 5 releases unit indices 0x19 through 0x22 inclusive, and anything
   else releases 0x0a through 0x19 inclusive.  The second is the fall-through,
   not a test for a second turn number, so a firing on any turn other than 5
   lands there.  The two ranges overlap at index 0x19, which is released by
   both.

   Like the chapter 15 handler above it rewrites only the low nibble -- the
   behaviour code -- of the ai_behavior byte at record offset 0x34 to 0, and
   leaves the high nibble alone, because bits 0x40 and 0x80 of it are
   independent AI flags other code reads on their own.  The bounds are
   literals; nothing is range checked and data_fdps_map_unit_count is not
   consulted.  Each record is resolved through fdps_get_unit_record per
   iteration, so the array base is re-read.

   The indices are only meaningful against chapter 16's own deployment.
   map15.dat lays 10 player records down at 0..9 and its 25 enemy deployment
   records follow in file order at 0x0a..0x22, wave by wave: 0x0a..0x16 the
   thirteen wave-0 samurai, archers and knights, 0x17..0x18 the two wave-1 dark
   mages, 0x19..0x1c the four wave-2 flying units, 0x1d..0x20 the four wave-3
   samurai and 0x21..0x22 the two wave-4 dark mages.  So the turn-5 branch
   releases the whole second half of the deployment -- waves 2, 3 and 4, ten
   units, all of them deployed holding position -- and the other branch
   releases the units that opened the battle plus the first wave-2 flyer.

   Table slot 23, and the turn-event table of chapter 16's map15.dat is the
   only shipped thing that names it, with the two records {turn 5, slot 23,
   phase 0} and {turn 15, slot 23, phase 0}; no tile trigger and no death
   script in any MAP*.DAT reaches the slot.  The turn-event dispatcher fires a
   record while the counter still holds the turn whose phase has just ended, so
   the first firing sees 5 and the second sees 15: in the shipped data the two
   branches are the turn-5 release and the turn-15 release.

   There is no one-shot latch: nothing guards either loop and nothing records
   that it ran.  The merge is idempotent, so a repeat firing on the same turn
   changes nothing, but a unit the AI has since moved into another behaviour
   mode would be pushed back to mode 0.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before the counter is even read, and
   never read back, so any index behaves the same. */
extern void fdps_chapter_16_event_enemies_advance_for_turn(int unit_index);
#pragma aux fdps_chapter_16_event_enemies_advance_for_turn "*" parm caller [];

/* Chapter 17's turn-scheduled reinforcement event: it brings on the wave of
   the current map's deployment table that the turn just played is due, by
   handing fdps_deploy_wave the map the chapter is playing, the battle turn
   counter less seven, and a placement flag of 0.

   The wave key is the raw subtraction and nothing else: there is no test, no
   table and no bound on either side of it, so the wave asked for is whatever
   the counter happens to hold minus seven.  On the two turns the map schedules
   it that is wave 1 and wave 2; on any earlier turn it is a negative number,
   which matches no deployment record because a record's wave byte is
   unsigned, so the call opens its files, loads the placement table and deploys
   nothing.

   The map number is read from data_fdps_chapter_current_chapter_id, so it is
   whatever chapter is loaded rather than anything this handler holds, and the
   placement flag of 0 makes fdps_deploy_unit put each unit on the nearest free
   walkable tile to the coordinates its MAP%02d.COD record names rather than on
   those coordinates themselves.

   There is no one-shot latch and nothing records that the handler has run.
   Calling it twice on the same turn deploys the same wave twice, because the
   wave walk appends and never checks whether those records are already on the
   map; the map's own turn table naming a slot once per turn is what keeps that
   from happening.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before the counter is read, and never
   read back, so any index behaves the same.

   Table slot 24, and the turn-event table of chapter 17's map16.dat is the
   only shipped thing that names it, with the two records {turn 8, slot 24,
   phase 0} and {turn 9, slot 24, phase 0}; no tile trigger and no death script
   in any MAP*.DAT reaches the slot.  The turn-event dispatcher fires a record
   while the counter still holds the turn whose phase has just ended, so the
   first firing asks for wave 1 -- map16.dat's six level 15 records 23..28 --
   and the second for wave 2, its single level 14 record 29.  All seven have
   spawn points in the lower-left corner of the map, tiles (2..4, 22..24). */
extern void fdps_chapter_17_event_deploy_wave_for_turn(int unit_index);
#pragma aux fdps_chapter_17_event_deploy_wave_for_turn "*" parm caller [];

#endif
