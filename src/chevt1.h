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

#endif
