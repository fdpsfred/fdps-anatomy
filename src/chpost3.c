/* chpost3.c -- the per-chapter post-action handlers, chapters 25 to 30: the
 * win/lose test the battle loop runs after a unit has acted.
 *
 * These are slots 24 to 29 of the handler table based at 0006028c, indexed by
 * the 0-based chapter id and called only through it, so none of the four
 * battle dispatchers appears as a static caller.
 *
 * These six chapters are played on a map whose player-slot count has settled
 * at twelve -- 珊 joins in chapter 24 and no handler adds anybody after her --
 * which is what puts the 魔戰將軍 and the 魔導王 at unit slots 0x0c onwards,
 * the base every hard-coded index below is measured from.
 *
 * See chpost3.h for what each handler decides.  Nothing here owns state: the
 * shared default test is btlend.c's and the battle-end code it settles is
 * gamedata.h's.
 */
#include "gamedata.h"
#include "unit.h"
#include "btlend.h"
#include "chpost3.h"

/* The three unit slots chapter 25's victory test asks about, the PUSH 0xc,
   PUSH 0xd and PUSH 0xe immediates at 0003b6bc, 0003b6ca and 0003b6da.  They
   are the map's first three enemy slots and they hold the chapter's three
   魔戰將軍.

   MAP24.DAT is a 131-byte header followed by 59 deployment records of 26 bytes,
   which is the guide's enemy list for this chapter to the unit: 3 + 1 黑暗祭司
   + 11 神箭手 + 18 鎧甲武士 + 8 地獄騎士 + 18 天空騎士.  Its header byte +1
   fields twelve player slots -- the same field the chapter 4, 5, 6, 9 and 11
   handlers in chpost1.c are read for, and the field that gives map19.dat
   eleven -- and byte +2 is the record count, 59.  fdps_build_map_unit_array lays those twelve player
   slots down first and fdps_deploy_wave appends the records whose wave byte
   (+0x15) matches the wave being deployed, so the first record of wave 0 is
   unit slot 12.  Records 0, 1 and 2 are wave 0, are the file's only level-30
   units -- character ids 64, 65 and 66 -- and the guide gives exactly three
   level-30 enemies, 塞克斯, 布魯森 and 汎拉沛.  The roster is twelve deep for
   the same count: the ten of chapter 17 plus 蘭斯洛特, who arrives on chapter
   19's sixth turn, plus 珊, who joins in chapter 24.

   The map is not deployed in one go, but the split does not move these three:
   41 of the 59 records are wave 0 and land at slots 12..52 when the map opens,
   and the other 18 are wave 1 -- all character id 97, the LV16 天空騎士x18 the
   chapter's reinforcement event brings on -- appended at slots 53..70 when that
   event fires.  The chapter's eighteen 鎧甲武士 are a different group, character
   id 100, and every one of them is wave 0.  Which of the two 18-strong groups is
   which is settled by chapter 24, where their counts differ: MAP23.DAT holds id
   95 x4, id 100 x6 and id 97 x15 against that chapter's guide list 神箭手x4 /
   鎧甲武士x6 / 天空騎士x15.  The warlords are wave-0 records 0, 1 and 2 either
   way. */
#define CHAPTER_25_WARLORD_SLOT_1 0x0c
#define CHAPTER_25_WARLORD_SLOT_2 0x0d
#define CHAPTER_25_WARLORD_SLOT_3 0x0e

/* 0003b6b0.  Two end conditions of the chapter's own and no forward to the
   shared test, like chapter 22's handler in chpost2.c and unlike chapters
   28's and 29's below -- but this one declares a victory as well as a
   defeat.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b6b0..0003b6b6 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003b70c..0003b70f with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003b710 carries no immediate.

   The victory test is three calls chained by their zero tests.  PUSH 0xc /
   CALL 0x000109b0 / ADD ESP,0x4 at 0003b6bc..0003b6c3 is
   fdps_unit_is_retired(0x0c) with the caller clearing its one argument, and its
   EAX is used at once: TEST EAX,EAX / JZ 0003b6d8 at 0003b6c6 leaves for the
   JMP 0x0003b6e8 that skips the store the moment a warlord is still standing.
   The 0xd call at 0003b6ca and the 0xe call at 0003b6dc repeat the shape with
   JNZ into the next test and a fall-through onto the same skip, so the three
   calls are a short-circuiting && chain in source order and no call after a
   live warlord is made at all.  Only when all three report retired does control
   reach MOV dword ptr [0x00069da0],0x2 at 0003b6ea.

   The defeat test then runs unconditionally: the store's own successor and the
   skip path's target are both 0003b6f4, where PUSH 0x0 / CALL 0x000109b0 / ADD
   ESP,0x4 asks about unit slot 0 and TEST EAX,EAX / JZ 0003b70c at 0003b6fe
   guards MOV dword ptr [0x00069da0],0x1 at 0003b702.  Neither store consults
   the code's current value and neither is the other's else branch, so the last
   write wins and the defeat is last: 蘭迪斯 falling on the same action that
   retires the final warlord is a Game Over in the original, where an if/else,
   an else-if, or the shared test's "only while the code is still 0" guard
   copied onto either store would clear the chapter instead.  In the other
   direction the same absence of a guard is what lets the victory overwrite a
   defeat a chapter event recorded earlier in the action.

   There is no CALL 0x0003a2e0 here, and that is the chapter's rules rather
   than a missing line: the guide gives 第25章 魔戰將軍 勝利條件 魔戰將軍死亡,
   three named bosses and not 敵人全滅, so the shared test's sweep would clear
   the chapter as soon as the last minion fell with all three warlords still
   alive.  Because that test never runs, this handler has to carry the defeat
   itself, which is the slot-0 store -- 失敗條件 蘭迪斯死亡, and slot 0 is
   蘭迪斯 because unit slot i is roster slot i and the roster is in join order.
   Chapter 25's 己方 is 法蓮娜以外的所有人, so it is 法蓮娜 at slot 3 and not
   蘭迪斯 at slot 0 who goes undeployed here, the mirror image of chapter 22.
   Her slot is still reserved -- the map fields twelve player slots, not eleven
   -- which is what keeps the warlords at 12, 13 and 14.

   Chapter 25 is chapter id 24 and the dword at 000602ec, twenty-four entries
   into the table based at 0006028c, is 0003b6b0; that table entry is the
   function's only xref, which is why it has no static caller. */
void fdps_chapter_25_post_action(void)
{
    if (fdps_unit_is_retired(CHAPTER_25_WARLORD_SLOT_1) != 0 &&
        fdps_unit_is_retired(CHAPTER_25_WARLORD_SLOT_2) != 0 &&
        fdps_unit_is_retired(CHAPTER_25_WARLORD_SLOT_3) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 2;
    }
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* The four unit slots chapter 26's victory test asks about, the PUSH 0xc, PUSH
   0xd, PUSH 0xe and PUSH 0xf immediates at 0003b76c, 0003b77a, 0003b78a and
   0003b79a.  They are the map's first four enemy slots and they hold the
   chapter's four 魔戰將軍.

   MAP25.DAT is a 131-byte header followed by 80 deployment records of 26 bytes
   -- 0x83 + 80 * 0x1a is 2211, the whole file.  Its header byte +1 fields
   twelve player slots, the same field chapter 25's map sets to twelve, and byte
   +2 is the record count, 80.  fdps_build_map_unit_array lays the twelve player
   slots down first and fdps_deploy_wave appends, in file order, the records
   whose wave byte (+0x15) equals the wave being deployed, so the 68 wave-0
   records become unit slots 0x0c..0x4f the moment the map opens.  Records 0, 1,
   2 and 3 are wave 0, are the file's only level-30 units -- their level byte
   (+4) is 0x1e where every other record on the map is 0x12 or 0x28 -- and carry
   character ids 0x40, 0x41, 0x42 and 0x43.  The guide's enemy list for this
   chapter gives exactly four level-30 enemies, 凱因巴, 塞克斯, 布魯森 and
   汎拉沛, against LV18 for the whole garrison behind them.

   The roster is twelve deep for the same count as chapter 25's: the ten of
   chapter 17 plus 蘭斯洛特, who arrives on chapter 19's sixth turn, plus 珊, who
   joins in chapter 24.  Chapter 26's 己方 is 法蓮娜以外的所有人, so slot 3 is
   reserved and empty here, which is what keeps the four warlords at 12..15
   rather than 11..14. */
#define CHAPTER_26_WARLORD_SLOT_1 0x0c
#define CHAPTER_26_WARLORD_SLOT_2 0x0d
#define CHAPTER_26_WARLORD_SLOT_3 0x0e
#define CHAPTER_26_WARLORD_SLOT_4 0x0f

/* The slot of data_fdps_map_cell_event_triggered_flags (gamedata.h) that the
   one-shot chapter-event handlers latch: byte ptr [0x000640e8], element 0x10 of
   the 32-entry array based at 0x000640d8.

   The array's own indexer is a cell's raw event code and the shipped M%02d.DTL
   event planes only ever use codes 0 to 15, so element 0x10 is the first slot no
   map cell can reach and the event handlers use it as private storage.  It is
   one slot shared by all of them, which is safe only because one chapter is
   loaded at a time and fdps_chapter_state_reset memsets the whole array when a
   chapter starts.  For this chapter the writer is
   fdps_chapter_26_event_deploy_waves_2_and_3 at 00039230, which sets it to 1 at
   0003939c as the last thing it does.

   Element 0x10 is inside the declared 32 and not past it, so this is not a
   folded base the decompiler has attributed to the wrong symbol; the array is
   the owner of that byte. */
#define CHAPTER_EVENT_ONE_SHOT_SLOT 0x10

/* The unit slot chapter 26's second defeat test asks about, the PUSH 0x5b at
   0003b7d8.  It is the LAST unit slot the map ever holds and it is the fourth
   and last of 索爾's 侍衛 -- NOT 索爾 himself, who is at 0x57.

   MAP25.DAT's 80 deployment records split 68 / 7 / 5 across waves 0, 2 and 3;
   there are no wave-1 records.  fdps_chapter_26_event_deploy_waves_2_and_3
   deploys wave 2 first (its FUN_00023830(map, 2, 0) at the top of the body) and
   wave 3 afterwards, so the seven wave-2 records -- file records 73..79, all
   side 0, character id 0x64 -- land at slots 0x50..0x56, and the five wave-3
   records -- file records 62..66, all side 1 -- land at 0x57..0x5b.  Record 62
   is character id 0x0c at level 40 and records 63..66 are four copies of
   character id 0x3b at level 40, which is the guide's 友方 LV40英雄索爾 plus
   LV40侍衛x4.  Character id 0x0c is 索爾: FRIAPRDA.DAT's row 0x0c is the
   HP960 / MP480 / AP300 / DP100 / DX160 / MV6 base line and FRILEVUP.DAT's row
   0x0c grows every field by 1, so at level 40 the unit builder's
   HP = hp_base + (LV-1) * hp_min gives 999 and MP gives 519, while
   AP = ap_base + LV * ap_min gives 340 and DP 140 -- 740 and 310 once 炎龍劍
   and 大地鎧甲 are counted, and DX 200.  That is the guide's 索爾 in all six
   fields.

   So the chapter's stated 失敗條件 索爾死亡 is NOT what the code tests: it
   watches the last of his four escorts.  Writing the guide's rule as a test on
   索爾's own slot reaches 0x57 and ends the battle on a different unit's death.
   Nothing here corrects that; the original's rule is the rule.

   0x5b is also the highest slot the map ever reaches -- twelve party slots plus
   all 80 deployment records is 92 units, 0x00..0x5b -- which is why the read is
   in bounds only once the relief force has landed, and why the latch above
   guards it. */
#define CHAPTER_26_ALLIED_GUARD_LAST_SLOT 0x5b

/* 0003b760.  Three end conditions of the chapter's own and no forward to the
   shared test: a victory, a defeat, and a second defeat that only becomes
   reachable part-way through the map.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b760..0003b766 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is the
   four bare POPs at 0003b7f2..0003b7f5 with no MOV ESP,EBP in front of them,
   which is what an empty local area leaves behind, and the RET at 0003b7f6
   carries no immediate.

   The victory test is four calls chained by their zero tests.  PUSH 0xc / CALL
   0x000109b0 / ADD ESP,0x4 at 0003b76c..0003b773 is fdps_unit_is_retired(0x0c)
   with the caller clearing its one argument, and its EAX is used at once: TEST
   EAX,EAX / JZ 0003b788 at 0003b776 leaves for the JMP 0x0003b798 that skips the
   store the moment a warlord is still standing.  The 0xd call at 0003b77a, the
   0xe call at 0003b78a and the 0xf call at 0003b79a repeat the shape with JNZ
   into the next test and a fall-through onto the same skip chain, so the four
   calls are a short-circuiting && chain in source order and no call after a live
   warlord is made at all.  Only when all four report retired does control reach
   MOV dword ptr [0x00069da0],0x2 at 0003b7aa.

   The first defeat test then runs unconditionally: the store's own successor and
   the skip chain's target are both 0003b7b4, where PUSH 0x0 / CALL 0x000109b0 /
   ADD ESP,0x4 asks about unit slot 0 and TEST EAX,EAX / JZ 0003b7cc at 0003b7be
   guards MOV dword ptr [0x00069da0],0x1 at 0003b7c2.  Slot 0 is 蘭迪斯, the
   first half of the chapter's 失敗條件.

   The second defeat test is gated on the chapter's one-shot event latch: XOR
   EAX,EAX / MOV AL,byte ptr [0x000640e8] / CMP EAX,0x1 / JNZ 0003b7e6 at
   0003b7cc..0003b7d6 zero-extends the byte and compares it for EQUALITY with 1,
   so any other value -- including a non-zero one -- skips the test rather than
   admitting it.  Only on the equal path does PUSH 0x5b / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003b7d8 run, and a retired slot 0x5b reaches MOV dword ptr
   [0x00069da0],0x1 at 0003b7e8 through the JNZ at 0003b7e4.

   That gate is load-bearing rather than defensive.  Slot 0x5b does not exist
   until fdps_chapter_26_event_deploy_waves_2_and_3 has run, and that handler
   sets the latch as the last thing it does, so the latch reading 1 is exactly
   the condition "the relief force is on the map".  fdps_unit_is_retired does not
   bound its index, so an ungated test would read the retirement bit of a record
   the map has not built yet and could end the battle before the ally ever
   appears.

   None of the three stores consults the code's current value and none is
   another's else branch, so the last write wins and the order is victory,
   蘭迪斯, escort.  An action that retires the final warlord and 蘭迪斯 together
   is a Game Over in the original, and so is one that retires the final warlord
   and slot 0x5b together once the latch is set; an if/else, an else-if, or the
   shared test's "only while the code is still 0" guard copied onto any of the
   three would clear the chapter instead.  In the other direction the same
   absence of a guard is what lets the victory overwrite a defeat a chapter event
   recorded earlier in the action.

   There is no CALL 0x0003a2e0 here, and that is the chapter's rules rather than
   a missing line: the guide gives 第26章 狂信人之塔 勝利條件 擊倒魔戰將軍, four
   named bosses and not 敵人全滅, so the shared test's sweep would clear the
   chapter as soon as the last of the seventy-odd garrison fell with all four
   warlords alive.  Because that test never runs, this handler has to carry both
   defeats itself.

   Chapter 26 is chapter id 25 and the dword at 000602f0, twenty-five entries
   into the table based at 0006028c, is 0003b760; that table entry is the
   function's only xref, which is why it has no static caller. */
void fdps_chapter_26_post_action(void)
{
    if (fdps_unit_is_retired(CHAPTER_26_WARLORD_SLOT_1) != 0 &&
        fdps_unit_is_retired(CHAPTER_26_WARLORD_SLOT_2) != 0 &&
        fdps_unit_is_retired(CHAPTER_26_WARLORD_SLOT_3) != 0 &&
        fdps_unit_is_retired(CHAPTER_26_WARLORD_SLOT_4) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 2;
    }
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
    if (data_fdps_map_cell_event_triggered_flags[CHAPTER_EVENT_ONE_SHOT_SLOT] ==
            1 &&
        fdps_unit_is_retired(CHAPTER_26_ALLIED_GUARD_LAST_SLOT) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* The one unit slot chapter 27's victory test asks about, the PUSH 0xc at
   0003b8ac.  It is the map's first enemy slot and it holds LV40 魔導王吉歐,
   the boss whose death is the chapter's 勝利條件.

   MAP26.DAT is a 131-byte header followed by 55 deployment records of 26 bytes
   -- 0x83 + 55 * 0x1a is 1561, the whole file.  Its header byte +1 fields twelve
   player slots, the same count chapters 25 and 26 field, and byte +2 is the
   record count, 55.  fdps_build_map_unit_array lays the twelve player slots down
   first and fdps_deploy_wave appends, in file order, the records whose wave byte
   (+0x15) equals the wave being deployed, so record 0 becomes unit slot 12.

   Record 0 is wave 0 -- so the slot exists from the moment the map opens and
   needs no latch of the kind chapter 26's second defeat test carries -- and it
   is the file's ONLY level-40 record: the level byte (+4) reads 0x28 once, 0x1e
   on four records and 0x12 on the other fifty.  That histogram is the guide's
   enemy list for this chapter to the unit -- LV40魔導王吉歐, four LV30 魔戰將軍,
   and LV18 神箭手x8 + 鎧甲武士x12 + 地獄騎士x10 + 天空騎士x20, fifty of them --
   so the single level-40 record is 吉歐 and it is the first record in the file.

   Its character id is 0x3f, which is over 60 and so indexes ENEMYDAT.DAT rather
   than FRIAPRDA.DAT, at row 0x3f - 60.  That table's field layout is not decoded
   (resource_info/data_tables.md), so this is a numeric match and not a field
   read: taking the little-endian word at +2 and the byte at +4 of a row as the
   HP and MP bases and multiplying each by the unit's level reproduces the
   guide's numbers for all five of this map's named enemies at once -- 300 and
   250 at level 40 give 吉歐's HP12000 and MP10000, and rows 0x40..0x43 at level
   30 give 5400/0, 4500/3000, 3900/6000 and 6300/4500, which are 塞克斯,
   布魯森, 汎拉沛 and 凱因巴.

   The four 魔戰將軍 stand at slots 13, 14, 15 and 16 here and none of them is
   tested: this chapter is won by killing 吉歐 alone.  Their defeat is what
   triggers the reinforcement event instead -- the thirty wave-1 records, which
   land at slots 0x25..0x42 when it fires -- and that event is a separate
   handler.  Carrying chapter 26's four-warlord && chain over to this chapter
   would clear it while the boss still stands. */
#define CHAPTER_27_MAGE_KING_SLOT 0x0c

/* 0003b8a0.  A victory condition and a defeat condition of the chapter's own
   and no forward to the shared test, the same shape as chapter 25's handler
   with a one-slot victory test in place of its three-slot chain.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b8a0..0003b8a6 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is the
   four bare POPs at 0003b8dc..0003b8df with no MOV ESP,EBP in front of them,
   which is what an empty local area leaves behind, and the RET at 0003b8e0
   carries no immediate.

   The victory test is one call.  PUSH 0xc / CALL 0x000109b0 / ADD ESP,0x4 at
   0003b8ac..0003b8b3 is fdps_unit_is_retired(0x0c) with the caller clearing its
   one argument, and its EAX is used at once: TEST EAX,EAX / JZ 0003b8c4 at
   0003b8b6 guards MOV dword ptr [0x00069da0],0x2 at 0003b8ba.

   The defeat test then runs unconditionally: the store's own successor and the
   skip path's target are both 0003b8c4, where PUSH 0x0 / CALL 0x000109b0 / ADD
   ESP,0x4 asks about unit slot 0 and TEST EAX,EAX / JZ 0003b8dc at 0003b8ce
   guards MOV dword ptr [0x00069da0],0x1 at 0003b8d2.  Slot 0 is 蘭迪斯 -- unit
   slot i is roster slot i and the roster is in join order -- and his death is
   the chapter's 失敗條件.  Chapter 27's 己方 is 法蓮娜以外的所有人, so it is
   slot 3 that is reserved and empty here, which is what keeps 吉歐 at 12.

   Neither store consults the code's current value and neither is the other's
   else branch, so the last write wins and the defeat is last: 蘭迪斯 falling on
   the same action that kills 吉歐 is a Game Over in the original, where an
   if/else, an else-if, or the shared test's "only while the code is still 0"
   guard copied onto either store would clear the chapter instead.  In the other
   direction the same absence of a guard is what lets the victory overwrite a
   defeat a chapter event recorded earlier in the action.

   There is no CALL 0x0003a2e0 here, and that is the chapter's rules rather than
   a missing line: the guide gives 第27章 魔導士的野望 勝利條件 魔導王死亡, one
   named boss and not 敵人全滅, so the shared test's sweep would clear the
   chapter as soon as the last of the fifty-odd garrison fell with 吉歐 still
   alive.  Because that test never runs, this handler has to carry the defeat
   itself.

   Chapter 27 is chapter id 26 (0x1a) and the dword at 000602f4, twenty-six
   entries into the table based at 0006028c, is 0003b8a0; that table entry is the
   function's only xref, which is why it has no static caller. */
void fdps_chapter_27_post_action(void)
{
    if (fdps_unit_is_retired(CHAPTER_27_MAGE_KING_SLOT) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 2;
    }
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003b990.  One CALL and a return, with no branch in the body at all -- the
   same bare forward chapters 16, 18 and 21 have.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP at 0003b990..0003b993, MOV EBP,ESP at 0003b994, SUB
   ESP,0x0 at 0003b996 -- and nothing in it is ever read, so there is no local
   to name.  The epilogue is the four bare POPs at 0003b9a1..0003b9a4 with no
   MOV ESP,EBP in front of them, which is what an empty local area leaves
   behind, and the RET at 0003b9a5 carries no immediate: the caller cleans, and
   there is nothing to clean.

   CALL 0x0003a2e0 at 0003b99c is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the RET, so its result is not used
   and this handler returns nothing of its own.  The verdict the callee leaves
   in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   The chapter's two stated conditions -- 勝利條件 敵人全滅 and 失敗條件
   蘭迪斯死亡 -- are both the shared test's own, so a handler that adds nothing
   is the complete rule and not an omission.  Chapter 28 is chapter id 27
   (0x1b), which is neither of the ids -- 0x10 and 0x15 -- the shared test
   singles out, so the slot it watches for the defeat is 0, 蘭迪斯.

   The chapter's scripted business, the three reinforcements that appear along
   the top edge at the end of the player phase on each of the nine turns the
   guide lists -- 2, 4, 6, 7, 10, 12, 14, 16, 18, which is not every even turn:
   it includes 7 and skips 8 -- is carried by a turn-event handler keyed on the
   turn counter; releasing a wave here would fire it once per unit action
   instead of once per turn.

   The dword at 000602f8, twenty-seven entries into the table based at
   0006028c, is 0003b990; that table entry is the function's only xref, which
   is why it has no static caller. */
void fdps_chapter_28_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* 0003b9f0.  One CALL and a return, with no branch in the body at all -- the
   same bare forward chapters 16, 18, 21 and 28 have.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP at 0003b9f0..0003b9f3 (53 56 57 55), MOV EBP,ESP at
   0003b9f4, SUB ESP,0x0 at 0003b9f6 in the six-byte imm32 form 81 EC 00 00 00
   00 -- and nothing in it is ever read, so there is no local to name.  The
   epilogue is the four bare POPs at 0003ba01..0003ba04 with no MOV ESP,EBP in
   front of them, which is what an empty local area leaves behind, and the RET
   at 0003ba05 is the one-byte C3: the caller cleans, and there is nothing to
   clean.

   CALL 0x0003a2e0 at 0003b9fc is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the RET, so its result is not used
   and this handler returns nothing of its own.  The verdict the callee leaves
   in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   The chapter's two stated conditions -- 勝利條件 敵人全滅 and 失敗條件
   蘭迪斯死亡 -- are both the shared test's own, so a handler that adds nothing
   is the complete rule and not an omission.  Chapter 29 is chapter id 28
   (0x1c), which is neither of the ids -- 0x10 and 0x15 -- the shared test
   singles out, so the slot it watches for the defeat is 0, 蘭迪斯.

   The chapter's scripted business is elsewhere and is not missing from here:
   the right, lower-right and upper-middle enemy groups that break cover once a
   player unit crosses the vertical line before the central junction, and the
   general attack that starts once one reaches the upper room's entrance, are
   fdps_chapter_29_event_activate_enemy_groups and
   fdps_chapter_29_event_activate_all_enemies (chevt6.h), the dwords 000395d0
   and 00039670 at 00060278 and 0006027c -- slots 45 and 46 of the
   chapter-script event vector based at 000601c4.  Both are keyed on where a
   unit stands and are armed by map28.dat's tile triggers, so neither can be
   reached from a postlude that runs after every action.

   The dword at 000602fc, twenty-eight entries into the table based at
   0006028c, is 0003b9f0; that table entry is the function's only xref, which
   is why it has no static caller. */
void fdps_chapter_29_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* 0003ba50.  One CALL, one branch, one store and no forward to the shared end
   test.  Nine of the thirty post-action handlers never call 0x0003a2e0 --
   chapters 03, 08, 10, 22, 23, 25, 26, 27 and 30, the twenty-one that do being
   the complete caller list of 0003a2e0 -- and of those nine, chapters 22
   (0003b270), 23 (0003b2e0) and 30 are the three that also never store a 2, so
   defeat is the only verdict any of the three can produce.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP at 0003ba50..0003ba53 (53 56 57 55), MOV EBP,ESP at
   0003ba54, SUB ESP,0x0 at 0003ba56 in the six-byte imm32 form 81 EC 00 00 00
   00 -- and nothing in it is ever read, so there is no local to name.  The
   epilogue is the four bare POPs at 0003ba74..0003ba77 with no MOV ESP,EBP in
   front of them, which is what an empty local area leaves behind, and the RET
   at 0003ba78 is the one-byte C3: the caller cleans, and there is nothing to
   clean.

   PUSH 0x0 / CALL 0x000109b0 / ADD ESP,0x4 at 0003ba5c..0003ba65 is
   fdps_unit_is_retired(0) with the caller clearing its one argument, and its
   EAX is used at once: TEST EAX,EAX / JZ 0003ba74 at 0003ba66 is the only
   branch in the body, skipping the MOV dword ptr [0x00069da0],0x1 at 0003ba6a.
   That store is the whole of the rest of the function -- the code is never read
   before it is written and no other value is ever stored -- so this handler can
   turn the code into a 1 and can do nothing else with it.  EAX is left holding
   the callee's answer at the RET, but the table's slots are called as void
   f(void) and no dispatch site reads a result, so the handler returns nothing.

   Unit index 0 is 蘭迪斯: unit slot i is roster slot i, the roster is in join
   order and is never permuted, so slot 0 is his on every map that deploys him,
   and chapter 30 names no exclusion from its 己方.

   There is no CALL 0x0003a2e0 here, and that is the chapter's rules rather than
   a missing line.  The shared test declares its victory by sweeping the unit
   list for a live enemy, and the guide gives 第30章 最終聖戰 勝利條件 擊倒平衡
   之神 -- three named bosses, the third of which has to be beaten to end the
   chapter -- against reinforcements it describes as 敵方援軍是永遠清不完.  Two
   ghosts and two 白骨戰士 are put back on the map as fast as they are killed, so
   the sweep for a live enemy can never come up empty and forwarding to the
   shared test would be dead weight on every action rather than a second way to
   win.  The clear is written elsewhere, and the sweep of every instruction
   naming 0x00069da0 -- 59 in the image, none of them a write in any form but
   MOV with an immediate -- says where it can come from.  The fifteen stores of
   2 are the shared test's own at 0003a2f9; one in each of nine other
   post-action handlers, chapters 03 at 0003a507, 08 at 0003a7f0, 10 at
   0003a941, 15 at 0003ac06, 19 at 0003af4a, 24 at 0003b49a, 25 at 0003b6ea, 26
   at 0003b7aa and 27 at 0003b8ba; the three named boss-defeat events, chapter
   15's at 00037caf and 00037cbb, 22's at 00038936 and 23's at 00038a12; and
   the opcode-4 arm of fdps_run_death_scripts at 0001dcfb.  Chapter 30 owns
   none of the first fourteen -- a handler and the shared test are reached only
   through the table slot of the chapter being played, and the boss-defeat
   events are chapters 15's, 22's and 23's -- which leaves 0001dcfb as the only
   store of 2 a chapter-30 battle can execute.  So the third 平衡之神's death
   reaches the code as a death-script record, and this handler must not be able
   to raise a 2 at all.

   The store carries no "only while the code is still 0" guard, unlike the two
   the shared test puts around its own writes, and copying that guard here by
   analogy changes behaviour: a defeat that lands on the same action as the
   scripted clear overwrites the 2 with a 1 in the original, and the player gets
   a Game Over where the guarded version would clear the last chapter.  The
   value read back is the value this handler last wrote.

   The address reaches the dispatchers only as the dword at 00060300, twenty-nine
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 29 is chapter 30, and it is the table's last slot -- the
   dword at 00060304 is 0003a410, fdps_chapter_01_end, the first entry of the
   separate chapter-end table. */
void fdps_chapter_30_post_action(void)
{
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}
