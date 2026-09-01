/* chpost2.c -- the per-chapter post-action handlers, chapters 16 to 30: the
 * win/lose test the battle loop runs after a unit has acted.
 *
 * These are slots 15 to 29 of the handler table based at 0006028c, indexed by
 * the 0-based chapter id and called only through it, so none of the four
 * battle dispatchers appears as a static caller.
 *
 * See chpost2.h for what each handler decides.  Nothing here owns state: the
 * shared default test is btlend.c's and the battle-end code it settles is
 * gamedata.h's.
 */
#include "btlend.h"
#include "chpost2.h"

/* 0003acb0.  One CALL and a return, with no branch in the body at all.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003acb0..0003acb6 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003acbc is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the RET at 0003acc5, so its result
   is not used and this handler returns nothing of its own.  The verdict the
   callee leaves in data_fdps_chapter_event_or_battle_end_code is the answer,
   and the dispatchers read that global directly, immediately after the
   indirect call.

   The address reaches the dispatchers only as the dword at 000602c8, fifteen
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 15 is chapter 16.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   The chapter's two stated conditions -- the enemy wiped out and 蘭迪斯's
   death -- are both the shared test's own, so a handler that adds nothing is
   the complete rule and not an omission.  The chapter's scripted business, the
   second wave that starts when the enemy knights are gone and the wandering
   smith who reforges 蘭迪斯's sword within twenty turns, is carried by
   turn-event handlers keyed on the turn counter; writing either of them here
   would fire it once per unit action instead of once per turn. */
void fdps_chapter_16_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}
