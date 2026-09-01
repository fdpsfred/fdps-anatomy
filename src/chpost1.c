/* chpost1.c -- the per-chapter post-action handlers, chapters 1 to 15: the
 * win/lose test the battle loop runs after a unit has acted.
 *
 * These are slots of the handler table based at 0006028c, indexed by the
 * 0-based chapter id and called only through it, so none of the four battle
 * dispatchers appears as a static caller.
 *
 * See chpost1.h for what each handler decides.  Nothing here owns state: the
 * shared default test is btlend.c's and the battle-end code it settles is
 * gamedata.h's.
 */
#include "btlend.h"
#include "chpost1.h"

/* 0003a450.  One CALL and a return, with no branch in the body at all.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a450..0003a456 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003a45c is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the RET at 0003a465, so its result
   is not used and this handler returns nothing of its own.  The verdict the
   callee leaves in data_fdps_chapter_event_or_battle_end_code is the answer,
   and the dispatchers read that global directly -- CMP dword ptr
   [0x00069da0],0x0 at 00012a4e, immediately after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   Chapter 2's second lose condition, Sol's death, is not missing from this
   function -- it is carried in map01.dat as a death script on Sol's
   deployment record and fired by the death-script runner.  Adding the test
   that chapter 1's handler performs would end the battle on paths the
   original does not. */
void fdps_chapter_02_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}
