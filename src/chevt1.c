/* chevt1.c -- the scripted chapter-event handlers of chapters 2 to 7.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the three
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt1.h for what each handler does.  Nothing here owns state; the
 * battle-end code is gamedata.h's.
 */
#include "gamedata.h"
#include "chevt1.h"

/* 00036cd0.  Two stores and a return, with no branch in the body at all.

   The store that matters is MOV dword ptr [0x00069da0],0x1 at 00036ce3: an
   unconditional write of the literal 1, the defeat code, into the battle-end
   global.  It is not a compare-and-set and not an or -- whatever the global
   held is gone, so a handler that fires after the chapter was already marked
   cleared turns that clear into a defeat.

   The first store, MOV dword ptr [EBP + 0x14],0x0 at 00036cdc, writes zero
   over the incoming argument slot and nothing ever reads it back.  That is the
   shape every handler in this family is built from: the frame is empty, SUB
   ESP,0x0 at 00036cd6, and the handlers that need a counter run their loops on
   the argument slot itself -- 0003795d and 00037985 in the chapter 8 handler
   initialise it to 0 the same way and then compare and INC it.  This handler
   has no loop, so the initialisation is all that is left of it.  The write is
   kept because it is what the original does; it has no observable effect
   either way, since the slot belongs to the caller's outgoing argument area
   and the caller discards it with ADD ESP,0x4 the moment the call returns.

   Nothing sets EAX before the RET at 00036cf1, and the dispatcher at 0002e140
   ignores what comes back, so the result really is void and not an int the
   callers happen not to read. */
void fdps_chapter_event_set_game_over(int unit_index)
{
    unit_index = 0;
    data_fdps_chapter_event_or_battle_end_code = 1;
}
