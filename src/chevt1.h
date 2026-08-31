/* chevt1.h -- the scripted chapter-event handlers of chapters 2 to 7.
 *
 * Every entry point here is a slot of the chapter-event handler table at
 * 000601c4, so they all share one function-pointer type: one int argument, no
 * result.  The argument is the battle unit index the event fired for, and a
 * handler that has no unit to work on ignores it -- the dispatchers pass 0
 * when the event is turn-scheduled rather than unit-attached.
 *
 * Nothing here owns state.  The battle-end code the handlers write is
 * gamedata.h's, and it is what the caller of the battle loop reads to decide
 * whether the chapter ended in defeat, in a clear, or not at all.
 */
#ifndef CHEVT1_H
#define CHEVT1_H

/* Scripted defeat: ends the current battle as a loss.  Stores 1 -- the defeat
   code -- into data_fdps_chapter_event_or_battle_end_code (gamedata.h) and
   returns, with no test of any kind in front of the store, so calling it ends
   the battle unconditionally and overwrites whatever code was already there,
   including the 2 that means the chapter was cleared.

   The battle does not stop inside this call.  The code is only a flag; the
   phase loop reads it at the top of its next iteration and returns, and the
   caller of the loop is what runs the game-over sequence.

   unit_index is the handler table's shared parameter and is ignored: this
   handler reads no unit record, so any index, in range or not, behaves the
   same.

   Table slot 2, and no shipped map file selects it -- scripted defeat is
   written in the data as a death-script opcode instead, which the death-script
   runner handles inline.  It is emitted anyway because the table is indexed
   unchecked by a byte out of the map file and the slot has to keep its
   number. */
extern void fdps_chapter_event_set_game_over(int unit_index);
#pragma aux fdps_chapter_event_set_game_over "*" parm caller [];

/* Chapter 2's turn-10 event: takes seven of the cave's enemies off the
   hold-position behaviour the map deploys them in and puts them on the default
   one, which paths a unit toward the nearest opposing unit.

   It rewrites the low nibble -- the behaviour code -- of the ai_behavior byte
   at record offset 0x34 to 0 for unit indices 8, 9, 0x0d, 0x11, 0x13, 0x14 and
   0x15, and leaves the high nibble of that byte alone, because bits 0x40 and
   0x80 of it are independent AI flags other code reads on their own.  The
   seven indices are literals; nothing is range checked and
   data_fdps_map_unit_count is not consulted, so the indices are only correct
   against chapter 2's own deployment.  Each record is resolved through
   fdps_get_unit_record per iteration, so the array base is re-read.

   unit_index is the handler table's shared parameter and is ignored: the
   incoming slot is overwritten with 0 before anything else and never read, so
   any index, in range or not, behaves the same.

   Table slot 3, and chapter 2's map01.dat is the only shipped file that names
   it -- one turn-event record, turn 10, so the event fires once per playthrough
   of that chapter. */
extern void fdps_chapter_02_event_enemies_advance(int unit_index);
#pragma aux fdps_chapter_02_event_enemies_advance "*" parm caller [];

#endif
