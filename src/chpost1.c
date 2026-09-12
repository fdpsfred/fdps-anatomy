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
#include "text.h"
#include "chpost1.h"

/* The mode 13h aperture and its row stride, PUSH 0xa0000 at 0003a3dd and PUSH
   0x140 at 0003a3d8 in front of chapter 1's one draw.  0xa0000 stays a literal
   because it is where the display adapter answers and not the address of
   anything the linker places (rebuild_info/pitfalls.md, contract E). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 at 0003a3d3, PUSH 0x0 at 0003a3d1
   and PUSH 0x6d at 0003a3cf: glyph fill, no cell background, and the shadow
   the outline colour becomes while the font's outline flag is clear.  Every
   ordinary line of spoken game text is drawn with these three. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The unit slot chapter 1's own defeat test asks about, PUSH 0x2 at 0003a3c1.
   It is a position in this map's unit array and not a character id: map00.dat
   declares one player slot in its header byte at +1, so the roster pass fills
   only slot 0 with 蘭迪斯, and the opening ICON00.DAT script then deploys the
   map's lone enemy record into slot 1 and its wave-1 record -- side 1,
   character id 0x0c, level 10, the guest 索爾 -- into slot 2, which is the
   slot the same script immediately retires, places at (14, 14) and unretires
   again. */
#define CH01_SOL_SLOT 2

/* The entry of the chapter's own text block the death line is spoken from,
   PUSH 0xf at 0003a3e2.  It is the last of FDETXT01.TXT's sixteen entries and
   opens with the token -0x11 carrying character id 0x0c, so fdps_draw_text
   raises the portrait of the very unit that has just been lost. */
#define CH01_SOL_DEATH_TEXT_ID 15

/* The unit slot chapter 3's victory test asks about, PUSH 0x4 at 0003a4d6.
   It is a position in this map's unit array and not a character id:
   map02.dat declares three player slots in its header byte at +1, so the
   roster pass fills 0, 1 and 2 with 蘭迪斯, 尤利安 and 亞克, and the wave-0
   deploy that ends the array build appends the map's two wave-0 records in
   table order -- side 1, character id 0x0c, level 10, the guest 索爾 at slot
   3, then side 0, character id 0x66, level 8, the 魔導士 at slot 4.  He is
   the only enemy standing when the map opens: of the other 78 deployment
   records, 5 are the level-5 狼人 of wave 1 and the remaining 73 are level-6
   石巨神 spread over waves 2 to 15. */
#define CH03_MAGE_SLOT 4

/* The entry of the chapter's own text block the victory line is spoken from,
   PUSH 0xd at 0003a4f7.  FDETXT03.TXT holds twenty-four entries and entry 13
   opens with the token pair -0x11, 102 -- the portrait code carrying
   character id 0x66 -- so the line is the 魔導士's own. */
#define CH03_MAGE_DEATH_TEXT_ID 13

/* 0003a3b0.  The shared test, then one defeat test of this chapter's own that
   speaks a line before it records its verdict.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a3b0..0003a3b6 -- and
   nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003a3bc has nothing pushed in front of it and no ESP
   adjustment behind it, so the shared test takes no argument, and the very
   next instruction is PUSH 0x2: EAX is not consulted between the two calls, so
   that call's result is not used here.  PUSH 0x2 / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003a3c1..0003a3c8 is fdps_unit_is_retired(2), the caller
   clearing its one argument, and its EAX is used -- TEST EAX,EAX / JZ 0003a3fc
   at 0003a3cb is the only branch in the body, skipping both the draw and the
   store.

   The seven pushes at 0003a3cf..0003a3e4 go in reverse argument order --
   0x6d, 0x0, 0xd0, 0x140, 0xa0000, 0xf, then the dword at 0x00060124 -- so the
   draw is fdps_draw_text(data_fdps_current_chapter_text_ptr, 15, 0xa0000,
   0x140, 0xd0, 0, 0x6d), and ADD ESP,0x1c at 0003a3ef is this function
   clearing all seven itself.  fdps_draw_text hands back the cursor it stopped
   at; nothing here reads it, EAX being untouched between the CALL at 0003a3ea
   and the MOV at 0003a3f2, so the result is discarded.

   THE DRAW IS INSIDE THE BRANCH AND CARRIES NO GUARD OF ITS OWN.  It is not
   conditioned on the battle-end code, so the line is spoken on the path where
   the shared test had already settled a clear and on the path where it
   returned at its gate because a chapter event had recorded a verdict.

   MOV dword ptr [0x00069da0],0x1 at 0003a3f2 stores without reading the code
   first.  So it overrides a 2 the shared test wrote moments earlier, and an
   action that empties the enemy side and retires slot 2 at once is a defeat
   and not a clear.  Gating the store on the code still being 0, or hanging it
   off an else of the victory, inverts exactly that case.

   The chapter's two stated lose conditions are 蘭迪斯 dying and 索爾 dying:
   the first is the shared test's slot 0 and the second is this store.  Unlike
   chapters 2 and 5, which carry their 索爾 condition as a death script on his
   deployment record, chapter 1 enforces it here in code -- and it is the only
   post-action handler in the file that speaks before it decides.

   Table slot 0: the dword at 0006028c, the base of the table itself, is
   0003a3b0. */
void fdps_chapter_01_post_action(void)
{
    fdps_battle_check_default_end_conditions();
    if (fdps_unit_is_retired(CH01_SOL_SLOT) != 0) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH01_SOL_DEATH_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

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

/* 0003a4b0.  No shared test at all: two tests of this chapter's own, written
   as a strict else-if chain, and nothing else in the body.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a4b0..0003a4b6 --
   and nothing in it is ever read, so there is no local to name.

   PUSH 0x0 / CALL 0x000109b0 / ADD ESP,0x4 at 0003a4bc..0003a4c3 is
   fdps_unit_is_retired(0), the caller clearing its one argument, and its EAX
   is used: TEST EAX,EAX / JZ 0003a4d6 at 0003a4c6 picks between MOV dword ptr
   [0x00069da0],0x1 at 0003a4ca and the second test.  PUSH 0x4 / CALL
   0x000109b0 / ADD ESP,0x4 at 0003a4d6..0003a4dd is fdps_unit_is_retired(4)
   and its EAX is used the same way -- TEST EAX,EAX / JZ 0003a511 at 0003a4e0
   skips the whole victory arm.

   THE TWO TESTS ARE ONE ELSE-IF CHAIN AND NOT TWO INDEPENDENT IFS.  JMP
   0003a511 at 0003a4d4 takes the defeat arm straight to the epilogue, so the
   unit-4 test never runs once slot 0 is found retired.  The siblings above
   and below do write their second test as an unguarded if, but there the
   override runs in the safe direction: it stamps a defeat over a clear.  Here
   the arms are the other way round, so an action that retires 蘭迪斯 and the
   魔導士 together would have the 2 overwrite the 1, speak the victory line and
   clear a chapter the original loses.

   The victory arm speaks before it records: PUSH 0x6d / 0x0 / 0xd0 / 0x140 /
   0xa0000 / 0xd / dword ptr [0x00060124] then CALL 0x0001ff60 and ADD
   ESP,0x1c at 0003a4e4..0003a504, the caller clearing all seven arguments.
   Nothing reads EAX after that CALL, so the cursor fdps_draw_text returns is
   discarded.  Then MOV dword ptr [0x00069da0],0x2 at 0003a507.

   There is no CALL 0x0003a2e0 anywhere in this function, so emptying the
   enemy side is not a clear here -- the reinforcement waves keep arriving and
   only the 魔導士's death ends it, which is the guide's 勝利條件 廿二回合內打
   倒魔導士.  The turn half of that condition is not enforced here either:
   map02.dat's turn-event table ends its live records with (22, 5, 0) and
   fdps_battle_run_turn_events calls chapter-event slot 5 when the counter
   reaches it.  And neither store is gated on the code's current value, so a
   verdict a chapter event already recorded is overwritten by either arm --
   this handler has none of the shared test's early-return protection because
   it does not call it.

   Table slot 2: the dword at 00060294, two entries into the table based at
   0006028c, is 0003a4b0. */
void fdps_chapter_03_post_action(void)
{
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    } else if (fdps_unit_is_retired(CH03_MAGE_SLOT) != 0) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH03_MAGE_DEATH_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        data_fdps_chapter_event_or_battle_end_code = 2;
    }
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

/* The unit slot chapter 8's first defeat test asks about, PUSH 0x0 at
   0003a71c.  This handler never calls fdps_battle_check_default_end_conditions
   (btlend.h), so unlike every other 蘭迪斯 test in this file it is the
   chapter's own instruction and not the shared test's. */
#define CH08_RANDIS_UNIT_INDEX 0

/* The guest mage 費塔加 on this map, PUSH 0x13 at 0003a73d.  He is not in the
   unit array when the battle opens:
   fdps_chapter_08_event_send_guest_mage_to_cells (chevt2.h) is what deploys
   him. */
#define CH08_GUEST_MAGE_UNIT_INDEX 0x13

/* The turn the 費塔加 test is armed after, CMP dword ptr [0x00069ce8],0x3 /
   JLE 0003a74b at 0003a734.  The compare is signed and the jump is JLE, so the
   test runs only while the counter is strictly greater than 3 -- the chapter
   script deploys him at the end of the player's third turn, and asking about a
   slot that is not filled yet would report a defeat on turns 1 to 3. */
#define CH08_GUEST_MAGE_ARMED_AFTER_TURN 3

/* The four captives this chapter escorts off the map, PUSH 0xf, 0x10, 0x11 and
   0x12 at 0003a757, 0003a765, 0003a775 and 0003a785.  They are positions in
   this map's unit array; the same four indices are the range
   fdps_chapter_08_event_villager_escapes (chevt2.h) accepts. */
#define CH08_VILLAGER_1_UNIT_INDEX 0x0f
#define CH08_VILLAGER_2_UNIT_INDEX 0x10
#define CH08_VILLAGER_3_UNIT_INDEX 0x11
#define CH08_VILLAGER_4_UNIT_INDEX 0x12

/* Where the escape tally lives, byte ptr [0x000640e9] at 0003a795: element
   0x11 of data_fdps_map_cell_event_triggered_flags (gamedata.h), the 32-entry
   array based at 0x000640d8 that fdps_chapter_state_reset clears on entry.
   fdps_chapter_08_event_villager_escapes bumps it once per villager that walks
   out alive, so it is a count and not a flag. */
#define CH08_ESCAPED_VILLAGER_COUNT_SLOT 0x11

/* The two closing lines, PUSH 0x1b at 0003a7bb and PUSH 0x23 at 0003a7e0:
   entries of the chapter's own text block for the total failure and for the
   escape. */
#define CH08_NO_VILLAGER_RESCUED_TEXT_ID 0x1b
#define CH08_VILLAGERS_ESCAPED_TEXT_ID 0x23

/* 0003a710.  Three rules of this chapter's own and no shared test at all: the
   two guests that may not be lost, and the captives-are-all-off-the-map ending
   that is a defeat or a clear depending on a count.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003a710..0003a716 -- and
   nothing in it is ever read, so there is no local to name.

   THERE IS NO CALL 0x0003a2e0 ANYWHERE IN THE BODY.  Every other handler in
   this file either forwards to fdps_battle_check_default_end_conditions or
   runs it first, and chapter 8 does not: emptying the enemy side is not a
   clear here, and a retired slot 0 is a defeat only because this function
   tests it itself.  Opening with the shared call by analogy with the siblings
   adds a victory condition the chapter does not have.

   Rule 1.  PUSH 0x0 / CALL 0x000109b0 / ADD ESP,0x4 at 0003a71c..0003a723 is
   fdps_unit_is_retired(0), the caller clearing its one argument, and its EAX
   is used -- TEST EAX,EAX / JZ 0003a734 at 0003a726 skips the MOV dword ptr
   [0x00069da0],0x1 at 0003a72a.  The store is unguarded and the rules that
   follow are not alternatives to it, so a call that both loses 蘭迪斯 and
   empties the cells writes 1 here and 2 below, and the last write wins.

   Rule 2.  CMP dword ptr [0x00069ce8],0x3 / JLE 0003a74b at 0003a734 gates the
   second call entirely: PUSH 0x13 / CALL 0x000109b0 / ADD ESP,0x4 at
   0003a73d..0003a744 is reached only when the turn counter is strictly above
   3, and its EAX is used by TEST EAX,EAX / JNZ 0003a74d at 0003a747.  The two
   JMPs at 0003a74b and the store at 0003a74d are the -od spelling of a
   short-circuiting and over one store, not two tests with a store each.

   Rule 3.  Four calls in a row -- PUSH 0xf, 0x10, 0x11, 0x12, each CALL
   0x000109b0 / ADD ESP,0x4, at 0003a757, 0003a765, 0003a775 and 0003a785 --
   each EAX used at once by TEST EAX,EAX and a jump, and every failing arm
   funnels through the JMP chain at 0003a773, 0003a783 and 0003a793 to the
   epilogue.  So it is a four-term short-circuiting and: the ending is reached
   only when all four captives are off the battlefield, whether they walked out
   or were killed.

   The ending itself is CMP byte ptr [0x000640e9],0x0 / JNZ 0003a7cd at
   0003a795 over the escape tally, and the two arms differ in more than the
   text id.  A tally of 0 -- none of the four got out alive -- stores 1 and
   then draws; any other tally draws and then stores 2.  THE CLEAR DOES NOT
   REQUIRE ALL FOUR TO HAVE ESCAPED: one survivor is enough, and the guide's
   失敗條件 村民全滅 is exactly the == 0 case.  Writing the obvious "all four
   escaped is the clear" turns a partial rescue into a defeat.

   fdps_draw_text's return is discarded on both arms -- ADD ESP,0x1c at
   0003a7c8 and 0003a7ed with nothing reading EAX behind either -- and its
   seven pushed arguments are the family's fixed tail: the chapter text block,
   the entry id, the mode 13h aperture, the row pitch, and the three message
   colours.

   Nothing here writes data_fdps_chapter_event_or_battle_end_code
   unconditionally, so a battle that has met none of the three rules leaves
   whatever the loop or a chapter event put there untouched.

   Table slot 7: the dword at 000602a8, seven entries into the table based at
   0006028c, is 0003a710, and that table entry is the function's only xref. */
void fdps_chapter_08_post_action(void)
{
    if (fdps_unit_is_retired(CH08_RANDIS_UNIT_INDEX) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }

    if (data_fdps_battle_turn_counter > CH08_GUEST_MAGE_ARMED_AFTER_TURN &&
        fdps_unit_is_retired(CH08_GUEST_MAGE_UNIT_INDEX) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }

    if (fdps_unit_is_retired(CH08_VILLAGER_1_UNIT_INDEX) != 0 &&
        fdps_unit_is_retired(CH08_VILLAGER_2_UNIT_INDEX) != 0 &&
        fdps_unit_is_retired(CH08_VILLAGER_3_UNIT_INDEX) != 0 &&
        fdps_unit_is_retired(CH08_VILLAGER_4_UNIT_INDEX) != 0) {

        if (data_fdps_map_cell_event_triggered_flags
                [CH08_ESCAPED_VILLAGER_COUNT_SLOT] == 0) {
            data_fdps_chapter_event_or_battle_end_code = 1;
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH08_NO_VILLAGER_RESCUED_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        } else {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH08_VILLAGERS_ESCAPED_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            data_fdps_chapter_event_or_battle_end_code = 2;
        }
    }
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

/* 0003aa70.  One CALL and a return, with no branch in the body at all --
   instruction for instruction the chapter 2, 7 and 12 handlers above, reached
   through a fourth table slot.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003aa70..0003aa76 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003aa81..0003aa84 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind.

   CALL 0x0003a2e0 at 0003aa7c is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the POPs, so its result is not used
   and this handler returns nothing of its own.  The verdict the callee leaves
   in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly -- CMP dword ptr [0x00069da0],0x0 at
   00012a4e, immediately after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   Chapter 13 is 地獄三鬥神, and the guide gives it 勝利條件 敵人全滅 and
   失敗條件 蘭迪斯死亡 -- one win condition and one lose condition, both of them
   the shared test's already.  The three 鬥神 the chapter is named for,
   薩達特, 席拉 and 巴魯, are enemy deployments and so are covered by
   敵人全滅; the guide lists no guest on the player side for this chapter, so
   there is no one for an extra defeat test to be about either.  The table slot
   two before this one, chapter 11's, does follow the shared test with a defeat
   test of its own -- PUSH 0x8 / CALL 0x000109b0 at 0003a9b1, then a MOV dword
   ptr [0x00069da0],0x1 at 0003a9bf that is gated on that test alone and not on
   the code's current value -- and by this chapter the roster is long enough for
   a slot at that index to exist, so carrying that shape further along the table
   is the natural mistake and would end the battle on paths the original does
   not.  It is not carried here: a search of the whole image for CALL
   0x000109b0 lists 62 sites, and none of them falls between 0003aa70 and the
   RET at 0003aa85.

   Table slot 12: the dword at 000602bc, twelve entries into the table based at
   0006028c, is 0003aa70, and that table entry is the function's only xref. */
void fdps_chapter_13_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* 0003aad0.  One CALL and a return, with no branch in the body at all --
   instruction for instruction the chapter 2, 7, 12 and 13 handlers above,
   reached through a fifth table slot.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003aad0..0003aad6 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003aae1..0003aae4 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind.

   CALL 0x0003a2e0 at 0003aadc is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the POPs, so its result is not used
   and this handler returns nothing of its own.  The verdict the callee leaves
   in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly -- CMP dword ptr [0x00069da0],0x0 at
   00012a4e, immediately after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   Chapter 14 is 天空之騎士, and the guide gives it
   勝利條件 敵人全滅 and 失敗條件
   蘭迪斯或法蓮娜死亡 -- two lose
   conditions, one more than the four handlers of the same shape above have.
   The first of them is the shared test's slot 0; the second is not in this
   function, and not by omission that could be read as an oversight in the
   reading: no instruction between 0003aad0 and the RET at 0003aae5 examines a
   unit at all.  A search of the whole image for CALL 0x000109b0,
   fdps_unit_is_retired, lists 62 sites, and the two nearest this function are
   0003a9b3 in chapter 11's handler and 0003ab51 in chapter 15's, one on
   either side of it.  Chapters 4, 5 and 6 do follow the shared test with
   exactly that test on unit slot 3, and on chapters 5 and 6 slot 3 is
   法蓮娜 herself, so carrying their shape here is the natural
   mistake and would end the battle on paths the original does not.  Where
   法蓮娜's condition is enforced instead is not readable from
   here: the per-unit death_script_opcode the record layout carries (struct
   fdps_unit_record, fdpstype.h) and fdps_run_death_scripts are the route the
   other chapters' casualty conditions take when no handler tests them, but
   which record carries it on this chapter's map is a question for the map
   data and not for this function.

   Table slot 13: the dword at 000602c0, thirteen entries into the table based
   at 0006028c, is 0003aad0, and that table entry is the function's only
   xref. */
void fdps_chapter_14_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}
