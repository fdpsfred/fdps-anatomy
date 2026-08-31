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
