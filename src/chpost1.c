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
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
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

/* 0003a560.  The shared test, then one defeat test of this chapter's own.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a560..0003a566 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003a56c has nothing pushed in front of it and no ESP
   adjustment behind it, so the shared test takes no argument, and the very
   next instruction is PUSH 0x3: EAX is not consulted between the two calls,
   so that call's result is not used here.  PUSH 0x3 / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003a571..0003a578 is fdps_unit_is_retired(3), the caller
   clearing its one argument, and its EAX is used -- TEST EAX,EAX / JZ
   0003a589 at 0003a57b is the only branch in the body, skipping the MOV
   dword ptr [0x00069da0],0x1 at 0003a57f.

   That store is guarded by nothing but the predicate: it does not consult the
   code's current value, and it runs after the shared test rather than as an
   alternative to it.  So it overrides a 2 the shared test wrote moments
   earlier, and an action that empties the enemy side and retires unit 3 at
   once is a defeat and not a clear.  Gating the store on the code still being
   0, or hanging it off an else of the victory, inverts exactly that case.

   Unit index 3 is a position in this map's unit array, not a character id.
   map03.dat fields three player slots, so the roster fills indices 0..2 and
   the deploy that ends the array build appends the map's one wave-0 record at
   index 3: the guest 索爾.  The same index 3 on the maps of chapters 5 and 6
   is 法蓮娜, so writing this argument as a per-character constant would be
   wrong in both directions.

   Table slot 3. */
void fdps_chapter_04_post_action(void)
{
    fdps_battle_check_default_end_conditions();
    if (fdps_unit_is_retired(3) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003a5d0.  The shared test, then one defeat test of this chapter's own --
   the same seventeen instructions as chapter 4's handler above, reached
   through a different table slot and meaning a different unit by the 3.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a5d0..0003a5d6 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003a5dc has nothing pushed in front of it and no ESP
   adjustment behind it, so the shared test takes no argument, and the very
   next instruction is PUSH 0x3: EAX is not consulted between the two calls,
   so that call's result is not used here.  PUSH 0x3 / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003a5e1..0003a5e8 is fdps_unit_is_retired(3), the caller
   clearing its one argument, and its EAX is used -- TEST EAX,EAX / JZ
   0003a5f9 at 0003a5eb is the only branch in the body, skipping the MOV
   dword ptr [0x00069da0],0x1 at 0003a5ef.

   That store is guarded by nothing but the predicate: it does not consult the
   code's current value, and it runs after the shared test rather than as an
   alternative to it.  So it overrides a 2 the shared test wrote moments
   earlier, and an action that empties the enemy side and retires unit 3 at
   once is a defeat and not a clear.  It also fires on the path where the
   shared test returned at its own gate because a chapter event had already
   recorded a verdict.  Gating the store on the code still being 0, or hanging
   it off an else of the victory, changes both of those outcomes.

   Unit index 3 is a position in this map's unit array, not a character id.
   map04.dat fields five player slots, so the roster fills indices 0..4 in the
   order it was appended -- 蘭迪斯, 尤利安, 亞克, 法蓮娜 -- and index 3 is
   法蓮娜.  The map's one wave-0 deployment record, the guest 索爾, is
   appended after those five and stands at index 5.  The identical body of
   chapter 4's handler reaches 索爾 with the same 3, so writing this argument
   as a per-character constant would be wrong in both directions.

   The chapter's three stated lose conditions are 蘭迪斯, 法蓮娜 or 索爾
   dying.  The first is the shared test's slot 0, the second is this store,
   and the third is not this handler's business at all: 索爾's deployment
   record carries a death script, which the script runner fires.  Adding a
   third test here would end the battle on paths the original does not.

   Table slot 4: the dword at 0006029c, four entries into the table based at
   0006028c, is 0003a5d0. */
void fdps_chapter_05_post_action(void)
{
    fdps_battle_check_default_end_conditions();
    if (fdps_unit_is_retired(3) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003a640.  The shared test, then one defeat test of this chapter's own --
   the same seventeen instructions as the two handlers above, reached through
   a third table slot.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a640..0003a646 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003a64c has nothing pushed in front of it and no ESP
   adjustment behind it, so the shared test takes no argument, and the very
   next instruction is PUSH 0x3: EAX is not consulted between the two calls,
   so that call's result is not used here.  PUSH 0x3 / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003a651..0003a658 is fdps_unit_is_retired(3), the caller
   clearing its one argument, and its EAX is used -- TEST EAX,EAX / JZ
   0003a669 at 0003a65b is the only branch in the body, skipping the MOV
   dword ptr [0x00069da0],0x1 at 0003a65f.

   That store is guarded by nothing but the predicate: it does not consult the
   code's current value, and it runs after the shared test rather than as an
   alternative to it.  So it overrides a 2 the shared test wrote moments
   earlier, and an action that empties the enemy side and retires unit 3 at
   once is a defeat and not a clear.  It also fires on the path where the
   shared test returned at its own gate because a chapter event had already
   recorded a verdict.  Gating the store on the code still being 0, or hanging
   it off an else of the victory, changes both of those outcomes.

   Unit index 3 is a position in this map's unit array, not a character id.
   map05.dat's header byte +1 fields four player slots, so the roster fills
   indices 0..3 in the order it was appended -- 蘭迪斯, 尤利安, 亞克,
   法蓮娜 -- and index 3 is 法蓮娜.  The roster is the same four it was in
   chapter 5: fdps_chapter_05_init and fdps_chapter_06_init have no roster
   append in them at all, where fdps_chapter_04_init still carries PUSH 0x1 /
   CALL 0x00023bc0 at 00020f7c.  Chapter 4's identically shaped handler
   reaches the guest 索爾 with the same 3, so writing this argument as a
   per-character constant would be wrong in both directions.

   The chapter's three stated lose conditions are 蘭迪斯, 法蓮娜 or 索爾
   dying.  The first is the shared test's slot 0, the second is this store,
   and the third is not this handler's business: 索爾 is one of the map's
   wave-0 deployment records, appended after the four roster slots, and his
   record carries a death script that the script runner fires.  Adding a third
   test here would end the battle on paths the original does not.

   Table slot 5: the dword at 000602a0, five entries into the table based at
   0006028c, is 0003a640. */
void fdps_chapter_06_post_action(void)
{
    fdps_battle_check_default_end_conditions();
    if (fdps_unit_is_retired(3) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003a6b0.  One CALL and a return, with no branch in the body at all --
   instruction for instruction chapter 2's handler above, reached through a
   different table slot.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a6b0..0003a6b6 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003a6bc is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the four POPs at
   0003a6c1..0003a6c4, so its result is not used and this handler returns
   nothing of its own.  The verdict the callee leaves in
   data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly -- CMP dword ptr [0x00069da0],0x0 at
   00012a4e, immediately after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   The three handlers immediately before this one in the table all follow the
   shared test with fdps_unit_is_retired(3) and a store of 1, and this
   chapter's roster still holds 法蓮娜, so that shape is the natural thing to
   carry over and it is not here.  Chapter 7 is the arena: the guide gives
   勝利條件 敵人全滅 and 失敗條件 蘭迪斯死亡, one lose condition and it is
   slot 0, which is exactly what the shared test already watches for every
   chapter id but 0x10 and 0x15.  Adding a second test would end the battle on
   paths the original does not.

   Table slot 6: the dword at 000602a4, six entries into the table based at
   0006028c, is 0003a6b0. */
void fdps_chapter_07_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}
