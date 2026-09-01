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

/* 0003a840.  The shared test, then two defeat tests of this chapter's own,
   the first one short circuiting the second.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a840..0003a846 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003a84c has nothing pushed in front of it and no ESP
   adjustment behind it, so the shared test takes no argument, and the very
   next instruction is PUSH 0x6: EAX is not consulted between the two calls,
   so that call's result is not used here.

   The two own tests are PUSH 0x6 / CALL 0x000109b0 / ADD ESP,0x4 at
   0003a851..0003a858 and PUSH 0x7 / CALL 0x000109b0 / ADD ESP,0x4 at
   0003a85f..0003a866, the caller clearing the one argument each time, and
   both EAX values are used.  TEST EAX,EAX / JNZ 0003a86d at 0003a85b jumps
   the slot-7 call ENTIRELY and lands on the store, and TEST EAX,EAX / JZ
   0003a877 at 0003a869 skips the store.  So the shape is a short-circuiting
   or: slot 7 is asked about only when slot 6 answered 0, and either non-zero
   answer reaches the one MOV dword ptr [0x00069da0],0x1 at 0003a86d.  Both
   arms share that single store; there is not one store per condition.

   That store is guarded by nothing but the predicate: it does not consult the
   code's current value, and it runs after the shared test rather than as an
   alternative to it.  So it overrides a 2 the shared test wrote moments
   earlier, and an action that empties the enemy side and retires either guest
   at once is a defeat and not a clear.  It also fires on the path where the
   shared test returned at its own gate because a chapter event had already
   recorded a verdict.  Gating the store on the code still being 0, or hanging
   it off an else of the victory, changes both of those outcomes.

   Unit indices 6 and 7 are positions in this map's unit array, and on this
   chapter they are the two guests 布蘭多 and 蓋亞 in that order.  map08.dat's
   header byte +1 fields eight player slots, and the roster standing at the
   start of the chapter holds six: fdps_roster_add_character is called exactly
   once from each of the chapter 1, 2, 3, 4, 7 and 8 init handlers, so the
   permanent party is 蘭迪斯, 尤利安, 亞克, 法蓮娜, 裘娜, 費塔加 at indices
   0..5.  fdps_chapter_09_init then appends two more before the battle -- PUSH
   0x8 / CALL 0x00023bc0 then PUSH 0x9 / CALL 0x00023bc0 at
   000210bc..000210cd, character ids 8 布蘭多 and 9 蓋亞 -- and the roster is
   appended to at data_fdps_roster_member_count, so they land at 6 and 7 in
   that order and fill the map's last two player slots.  All 31 of map08.dat's
   deployment records are side 0, so no enemy record can reach either index.

   The chapter's three stated lose conditions are 蘭迪斯, 布蘭多 or 蓋亞
   dying.  The first is the shared test's slot 0 -- chapter id 8 is neither
   0x10 nor 0x15, so the arm it takes is PUSH 0x0 at 0003a382 -- and the other
   two are this store.  Nothing here is carried as a map death script: every
   deployment record in map08.dat has a zero death-script opcode.

   Table slot 8: the dword at 000602ac, eight entries into the table based at
   0006028c, is 0003a840. */
void fdps_chapter_09_post_action(void)
{
    fdps_battle_check_default_end_conditions();
    if (fdps_unit_is_retired(6) != 0 || fdps_unit_is_retired(7) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003a8c0.  The only handler in this file that does not call the shared test:
   an eight-slot escape count, then a defeat test, then the clear.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003a8c0..0003a8c4 -- over SUB ESP,0xc, and all three dwords of
   that local area are live.  [EBP-0x4] is zeroed at 0003a8cc and incremented
   at 0003a91c, so it is the running count; [EBP-0x8] is zeroed at 0003a8d3,
   compared against 8 at 0003a8da and incremented at 0003a8e5, so it is the
   loop index; [EBP-0xc] takes EAX straight off the record lookup at 0003a8f6
   and is dereferenced at 0003a8f9, so it is the record pointer.

   The loop is the ordinary -od for shape with the increment block ahead of the
   test in address order: CMP dword ptr [EBP-0x8],0x8 / JL 0003a8ea / JMP
   0003a921 at 0003a8da is the guard, the body runs 0003a8ea..0003a91f, and the
   JMP 0003a8e2 at 0003a91f lands on the MOV EAX,[EBP-0x8] / INC dword ptr
   [EBP-0x8] pair that falls back into the test.  The compare is JL, signed,
   over an int index, and the bound is the literal 8 -- the eight player slots
   map09.dat fields, which is the party the guide sets out on the last two
   stair rows for this chapter's final wave.

   Body.  MOV EAX,[EBP-0x8] / PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4 at
   0003a8ea is fdps_get_unit_record(unit_index), the caller clearing its one
   argument, and its EAX IS used: it is stored to [EBP-0xc] and reloaded at
   0003a8f9.  MOV AL,byte ptr [EAX+0x1] / AND EAX,0xff / CMP EAX,0x17 at
   0003a8fc reads the record's pos_y at offset 1 as an unsigned byte and tests
   it for equality against 0x17, and JZ 0003a919 skips straight to the
   increment.  Only when that fails is PUSH EAX / CALL 0x000109b0 / ADD ESP,0x4
   at 0003a90c reached -- fdps_unit_is_retired(unit_index), its EAX used by
   TEST EAX,EAX / JZ 0003a91f at 0003a915.  So the two conditions are a
   short-circuiting or over one shared increment, not two counts.

   Tail.  PUSH 0x0 / CALL 0x000109b0 / ADD ESP,0x4 at 0003a921 is
   fdps_unit_is_retired(0), and its EAX is used: TEST EAX,EAX / JZ 0003a93b at
   0003a92b picks between MOV dword ptr [0x00069da0],0x1 at 0003a92f and the
   CMP dword ptr [EBP-0x4],0x8 / JNZ 0003a94b at 0003a93b guarding MOV dword
   ptr [0x00069da0],0x2 at 0003a941.  The JMP 0003a94b at 0003a939 takes the
   defeat arm past the count entirely, so the two are an if/else and the defeat
   wins over a completed escape decided in the same call.  The count compare is
   an equality on the JNZ, not a threshold.

   Both stores are conditional and there is no unconditional write anywhere in
   the body, so a battle that has neither ended leaves
   data_fdps_chapter_event_or_battle_end_code holding whatever the battle loop
   gave it, and a verdict a chapter event already recorded survives untouched.

   Two things here are not what the siblings above would suggest.  The escape
   count ORs the bottom-row test with fdps_unit_is_retired, so a casualty
   counts as accounted for and the chapter stays winnable after losses;
   counting only units standing on row 0x17 makes it unwinnable the moment
   anyone but 蘭迪斯 dies.  And there is no CALL 0x0003a2e0 in this function at
   all: emptying the enemy side is not a clear here, which matches the guide's
   勝利條件 戰場底部脫離（所有人到達戰場底部）against 失敗條件 蘭迪斯死亡.

   Table slot 9: the dword at 000602b0, nine entries into the table based at
   0006028c, is 0003a8c0. */
void fdps_chapter_10_post_action(void)
{
    int escaped_or_retired_count;
    int unit_index;
    struct fdps_unit_record *unit_record;

    escaped_or_retired_count = 0;
    for (unit_index = 0; unit_index < 8; unit_index++) {
        unit_record = fdps_get_unit_record(unit_index);
        if (unit_record->pos_y == 0x17 ||
            fdps_unit_is_retired(unit_index) != 0) {
            escaped_or_retired_count++;
        }
    }
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    } else if (escaped_or_retired_count == 8) {
        data_fdps_chapter_event_or_battle_end_code = 2;
    }
}

/* 0003a9a0.  The shared test, then one defeat test of this chapter's own --
   the same seventeen instructions as chapters 4 and 5 above, reached through
   a different table slot and meaning a different unit by its literal.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a9a0..0003a9a6 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003a9ac has nothing pushed in front of it and no ESP
   adjustment behind it, so the shared test takes no argument, and the very
   next instruction is PUSH 0x8: EAX is not consulted between the two calls,
   so that call's result is not used here.  PUSH 0x8 / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003a9b1..0003a9b8 is fdps_unit_is_retired(8), the caller
   clearing its one argument, and its EAX is used -- TEST EAX,EAX / JZ
   0003a9c9 at 0003a9bb is the only branch in the body, skipping the MOV
   dword ptr [0x00069da0],0x1 at 0003a9bf.

   That store is guarded by nothing but the predicate: it does not consult the
   code's current value, and it runs after the shared test rather than as an
   alternative to it.  So it overrides a 2 the shared test wrote moments
   earlier, and an action that empties the enemy side and retires unit 8 at
   once is a defeat and not a clear.  It also fires on the path where the
   shared test returned at its own gate because a chapter event had already
   recorded a verdict.  Gating the store on the code still being 0, or hanging
   it off an else of the victory, changes both of those outcomes.

   Unit index 8 is a position in this map's unit array, not a character id.
   map10.dat's header byte +1 fields nine player slots, and the roster fills
   them exactly: fdps_roster_add_character is called once from each of the
   chapter 1, 2, 3, 4, 7 and 8 init handlers and twice from chapter 9's, so
   eight members stand at 0..7 going into this chapter, and
   fdps_chapter_11_init appends character id 7 -- 琴琴, the level 15 武道家 --
   at index 8 before the battle is built.  All 49 of map10.dat's deployment
   records are side 0 with character ids 79 and above, so no enemy record can
   reach that index and no friendly one competes for it.

   The chapter's two stated lose conditions are 蘭迪斯 or 琴琴 dying.  The
   first is the shared test's slot 0 -- chapter id 10 is neither 0x10 nor
   0x15, so the arm it takes is PUSH 0x0 at 0003a382 -- and the second is this
   store.  Nothing here is carried as a map death script: the only non-255
   death-script opcodes in map10.dat are the 1s on the two enemies that drop
   2000 and 2500 gold.

   Table slot 10: the dword at 000602b4, ten entries into the table based at
   0006028c, is 0003a9a0. */
void fdps_chapter_11_post_action(void)
{
    fdps_battle_check_default_end_conditions();
    if (fdps_unit_is_retired(8) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003aa10.  One CALL and a return, with no branch in the body at all --
   instruction for instruction the chapter 2 and chapter 7 handlers above,
   reached through a third table slot.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003aa10..0003aa16 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003aa21..0003aa24 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind.

   CALL 0x0003a2e0 at 0003aa1c is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the POPs, so its result is not used
   and this handler returns nothing of its own.  The verdict the callee leaves
   in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly -- CMP dword ptr [0x00069da0],0x0 at
   00012a4e, immediately after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   Chapter 12 is 火神的宮殿, the chapter that asks which of the three guardian
   rooms to enter before the battle begins, and that choice settles which
   guardian is fought -- 修佩魯, 雷德 or 亞德尼恩 -- and the reward chain that
   follows from it.  None of it reaches this handler: the guide gives
   勝利條件 敵人全滅 and 失敗條件 蘭迪斯死亡, one win condition and one lose
   condition, and both are the shared test's already, the same pair whichever
   room was entered.  The table slot immediately before this one does follow
   the shared test with a defeat test of its own -- PUSH 0x8 / CALL 0x000109b0
   and an unguarded MOV dword ptr [0x00069da0],0x1 at 0003a9b1..0003a9bf -- and
   by this chapter the roster is long enough for such a slot to exist, so
   carrying that shape one slot further is the natural mistake and would end
   the battle on paths the original does not.

   Table slot 11: the dword at 000602b8, eleven entries into the table based at
   0006028c, is 0003aa10. */
void fdps_chapter_12_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}
