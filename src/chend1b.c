/* chend1b.c -- the per-chapter end handlers, chapters 12 to 15: what the game
 * does at the moment a chapter's battle has been won, before the village phase
 * that follows it.  Chapters 1 to 11 are chend1.c, 16 to 24 are chend2.c and
 * 25 to 30 are chend2b.c.
 *
 * These are slots of the handler table based at 00060304, indexed by the
 * 0-based chapter id and called only through it, so the dispatcher in main.c
 * is not a static caller of any of them.
 *
 * See chend1b.h for what each handler closes out.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "btlend.h"
#include "icon.h"
#include "roster.h"
#include "unit.h"
#include "chend1b.h"

/* Chapter 12's victory cut-scene, the string at 0x62134 loaded into EAX at
   0003aa46 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 12 is the
   0-based id 11, so this is Win11.dat.

   The literal is the bare member name with no path and no container: the
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so a
   lower-case spelling here is what the original has and is not a defect to
   tidy, and the literal cannot live in read-only storage. */
#define CH12_VICTORY_SCRIPT "Win11.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0xc at 0003aa59.  The index is 0-based, so 12 is chapter 13
   -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 11 of a table indexed by this same global,
   and every handler in the family stores its own literal.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 11, the index of the chapter that has just been won, and this store is
   12, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH12_NEXT_CHAPTER_ID 12

/* 0003aa30.  Four calls and one store, straight line, no branch and no loop --
   instruction for instruction the same body as fdps_chapter_03_end through
   fdps_chapter_11_end (chend1.c), with a different script name and a different
   stored index.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003aa30..0003aa34 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form at 0003aa36..0003aa3b.  Nothing is
   addressed off EBP anywhere in the body, so there is no local here to name
   and none is declared; the four registers come back off the stack at
   0003aa63..0003aa66 and the RET at 0003aa67 is bare.

   CALLING CONVENTION.  The one argument goes on the stack and the caller takes
   it back: MOV EAX,0x62134 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003aa46..0003aa51.  The three argument-less calls at 0003aa3c, 0003aa41 and
   0003aa54 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set for a result before the RET -- the only write to it is the one that
   carries the script name into the third call -- so it returns nothing.  The
   one caller agrees: the dispatcher at 00029395 loads the chapter index,
   scales it by four and CALLs through [EAX + 0x60304] with nothing pushed, no
   stack cleanup afterwards and no read of EAX (the next instruction is another
   CALL, 000293a7).  Slot 11 of that table, at 00060330, holds 0003aa30 and is
   the only reference to this function in the image.

   VALUES USED AFTER A CALL: none at all.  All four callees return void as far
   as this body is concerned and nothing here reads EAX after any of them;
   fdps_icon_script_run does leave a result in EAX and this call site discards
   it -- the ADD ESP,0x4 and the CALL that follows overwrite it.

   THE MAP IS SWEPT FIRST, and here the sweep is the belt-and-braces step it is
   in chapters 4 to 7, 9 and 11 rather than the load-bearing one it is in
   chapters 3, 8 and 10.  Chapter 12's verdict comes from
   fdps_chapter_12_post_action (chpost1.h), which is the bare shared end test
   with no condition of its own, and that test records a clear only when no
   unit on side 0 is still standing (btlend.h); the dispatcher at 000293a1
   reaches this table only on that verdict.  So on the shipped data the sweep
   normally finds the enemy side already empty and its hit-point stores land on
   units that have retired.  What it does is not conditional on that:
   fdps_battle_destroy_remaining_enemies zeroes the hit-point word of every
   unit on side 0 whether or not it has left the field, and plays the ones that
   are not already retired off the map (btlend.h).

   THE ORDER OF THE REST IS THE ALGORITHM.  The cut-scene runs AFTER the
   writeback and the revive AFTER the cut-scene.  The script is interpreted
   with the battle's unit array still standing, so a unit-record edit it makes
   lands on a party that has already been banked and reaches the roster only if
   the script itself asks for another writeback (opcode 0x61, src/icon.c).  The
   revive then reads the roster the writeback has just filled, which is what
   makes it see the battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell, and it does nothing about
   which of chapter 12's three guardian rooms the player fought through -- that
   choice is the chapter's own and leaves no mark here.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003aa59 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_12_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH12_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH12_NEXT_CHAPTER_ID;
}

/* Chapter 13's victory cut-scene, the string at 0x62140 loaded into EAX at
   0003aaa6 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 13 is the
   0-based id 12, so this is Win12.dat.

   The literal is the bare member name with no path and no container, for the
   same reason chapter 12's is: the interpreter names IconAni.vfs itself and
   upper-cases the member name in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so a lower-case spelling here is what the
   original has and is not a defect to tidy, and the literal cannot live in
   read-only storage. */
#define CH13_VICTORY_SCRIPT "Win12.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0xd at 0003aab9.  The index is 0-based, so 13 is chapter
   14, 天空之騎士 -- both the village phase that runs next and the chapter
   loaded after it read this global, so this one store is what advances the
   game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 12 of a table indexed by this same global,
   and every handler in the family stores its own literal.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 12, the index of the chapter that has just been won, and this store is
   13, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH13_NEXT_CHAPTER_ID 13

/* 0003aa90.  Four calls and one store, straight line, no branch and no loop --
   instruction for instruction the same body as fdps_chapter_12_end above and
   as fdps_chapter_03_end through fdps_chapter_11_end (chend1.c), with a
   different script name and a different stored index.  The two bodies differ
   in exactly two operands: the string address at 0003aaa6 and the immediate at
   0003aab9.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003aa90..0003aa94 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form at 0003aa96..0003aa9b.  Nothing is
   addressed off EBP anywhere in the body, so there is no local here to name
   and none is declared; the four registers come back off the stack at
   0003aac3..0003aac6 and the RET at 0003aac7 is bare.

   CALLING CONVENTION.  The one argument goes on the stack and the caller takes
   it back: MOV EAX,0x62140 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003aaa6..0003aab1.  The three argument-less calls at 0003aa9c, 0003aaa1 and
   0003aab4 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set for a result before the RET -- the only write to it is the one that
   carries the script name into the third call -- so it returns nothing.  The
   one caller agrees: the dispatcher at 00029395 loads the chapter index,
   scales it by four and CALLs through [EAX + 0x60304] with nothing pushed, no
   stack cleanup afterwards and no read of EAX.  Slot 12 of that table, at
   00060334, holds 0003aa90 and is the only reference to this function in the
   image.

   VALUES USED AFTER A CALL: none at all.  All four callees return void as far
   as this body is concerned and nothing here reads EAX after any of them;
   fdps_icon_script_run does leave a result in EAX and this call site discards
   it -- the ADD ESP,0x4 and the CALL that follows overwrite it.

   THE MAP IS SWEPT FIRST, and here the sweep is the belt-and-braces step it is
   in chapter 12 rather than the load-bearing one it is in chapters 3, 8 and
   10.  Chapter 13's verdict comes from fdps_chapter_13_post_action (chpost1.h,
   0003aa70), whose whole body is one CALL to
   fdps_battle_check_default_end_conditions -- the bare shared end test with no
   condition of its own -- and that test records a clear only when no unit on
   side 0 is still standing (btlend.h); the dispatcher at 000293a1 reaches this
   table only on that verdict.  So on the shipped data the sweep normally finds
   the enemy side already empty and its hit-point stores land on units that
   have retired.  What it does is not conditional on that:
   fdps_battle_destroy_remaining_enemies zeroes the hit-point word of every
   unit on side 0 whether or not it has left the field, and plays the ones that
   are not already retired off the map (btlend.h).

   THE ORDER OF THE REST IS THE ALGORITHM, and it is chapter 12's order for
   chapter 12's reasons.  The cut-scene runs AFTER the writeback and the revive
   AFTER the cut-scene.  The script is interpreted with the battle's unit array
   still standing, so a unit-record edit it makes lands on a party that has
   already been banked and reaches the roster only if the script itself asks
   for another writeback (opcode 0x61, src/icon.c).  The revive then reads the
   roster the writeback has just filled, which is what makes it see the
   battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell, and it hands nothing else to
   the phase that follows: the store below is the only global it writes
   directly.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003aab9 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_13_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH13_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH13_NEXT_CHAPTER_ID;
}

/* Chapter 14's victory cut-scene, the string at 0x6214c loaded into EAX at
   0003ab06 and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 14 is the
   0-based id 13, so this is Win13.dat.

   The literal is the bare member name with no path and no container, for the
   same reason chapters 12's and 13's are: the interpreter names IconAni.vfs
   itself and upper-cases the member name in place before the container compare
   (vfs.h, rebuild_info/pitfalls.md), so a lower-case spelling here is what the
   original has and is not a defect to tidy, and the literal cannot live in
   read-only storage. */
#define CH14_VICTORY_SCRIPT "Win13.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0xe at 0003ab19.  The index is 0-based, so 14 is chapter
   15, 要塞砲危機 -- both the village phase that runs next and the chapter
   loaded after it read this global, so this one store is what advances the
   game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 13 of a table indexed by this same global,
   and every handler in the family stores its own literal.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 13, the index of the chapter that has just been won, and this store is
   14, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH14_NEXT_CHAPTER_ID 14

/* 0003aaf0.  Four calls and one store, straight line, no branch and no loop --
   instruction for instruction the same body as fdps_chapter_12_end and
   fdps_chapter_13_end above and as fdps_chapter_03_end through
   fdps_chapter_11_end (chend1.c), with a different script name and a different
   stored index.  It differs from chapter 13's body in exactly two operands:
   the string address at 0003ab06 and the immediate at 0003ab19.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003aaf0..0003aaf4 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form at 0003aaf6..0003aafb.  Nothing is
   addressed off EBP anywhere in the body, so there is no local here to name
   and none is declared; the four registers come back off the stack at
   0003ab23..0003ab26 and the RET at 0003ab27 is bare.

   CALLING CONVENTION.  The one argument goes on the stack and the caller takes
   it back: MOV EAX,0x6214c / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003ab06..0003ab11.  The three argument-less calls at 0003aafc, 0003ab01 and
   0003ab14 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set for a result before the RET -- the only write to it is the one that
   carries the script name into the third call -- so it returns nothing.  The
   one caller agrees: the dispatcher at 00029395 loads the chapter index,
   scales it by four and CALLs through [EAX + 0x60304] with nothing pushed, no
   stack cleanup afterwards and no read of EAX.  Slot 13 of that table, at
   00060338, holds 0003aaf0 and is the only reference to this function in the
   image.

   VALUES USED AFTER A CALL: none at all.  All four callees return void as far
   as this body is concerned and nothing here reads EAX after any of them;
   fdps_icon_script_run does leave a result in EAX and this call site discards
   it -- the ADD ESP,0x4 and the CALL that follows overwrite it.

   THE MAP IS SWEPT FIRST, and here the sweep is the belt-and-braces step it is
   in chapters 12 and 13 rather than the load-bearing one it is in chapters 3,
   8 and 10.  Chapter 14's verdict comes from fdps_chapter_14_post_action
   (chpost1.h), whose whole body is one CALL to
   fdps_battle_check_default_end_conditions -- the bare shared end test with no
   condition of its own -- and that test records a clear only when no unit on
   side 0 is still standing (btlend.h); the dispatcher at 000293a1 reaches this
   table only on that verdict.  So on the shipped data the sweep normally finds
   the enemy side already empty and its hit-point stores land on units that
   have retired.  What it does is not conditional on that:
   fdps_battle_destroy_remaining_enemies zeroes the hit-point word of every
   unit on side 0 whether or not it has left the field, and plays the ones that
   are not already retired off the map (btlend.h).

   THE ORDER OF THE REST IS THE ALGORITHM, and it is chapter 12's order for
   chapter 12's reasons.  The cut-scene runs AFTER the writeback and the revive
   AFTER the cut-scene.  The script is interpreted with the battle's unit array
   still standing, so a unit-record edit it makes lands on a party that has
   already been banked and reaches the roster only if the script itself asks
   for another writeback (opcode 0x61, src/icon.c).  The revive then reads the
   roster the writeback has just filled, which is what makes it see the
   battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell, and it hands nothing else to
   the phase that follows: the store below is the only global it writes
   directly.  In particular 天空之騎士's stated lose condition -- 蘭迪斯 or
   法蓮娜 falling -- leaves no mark here either; it is not in chapter 14's
   post-action test and it is not in this handler.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003ab19 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_14_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH14_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH14_NEXT_CHAPTER_ID;
}


/* Chapter 15's victory cut-scene, the string at 0x62158 loaded into EAX at
   0003ac7d and pushed as fdps_icon_script_run's only argument.  The member
   name is based on the chapter that has just been WON: chapter 15 is the
   0-based id 14, so this is Win14.dat.

   (Ghidra's automatic string here started two bytes early, at 0x62156, over
   the 2e 63 filler that pads the preceding "Win13.dat" literal to a four-byte
   boundary; the symbol has since been placed at 0x62158.  What the callee is
   handed is 0x62158 and the characters it sees are "Win14.dat".)

   The literal is the bare member name with no path and no container: the
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so a
   lower-case spelling here is what the original has and is not a defect to
   tidy, and the literal cannot live in read-only storage. */
#define CH15_VICTORY_SCRIPT "Win14.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0xf at 0003ac90.  The index is 0-based, so 15 is chapter
   16, 羅特帝亞突入 -- both the village phase that runs next and the chapter
   loaded after it read this global, so this one store is what advances the
   game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 14 of a table indexed by this same global,
   and every handler in the family stores its own literal.

   THE TWO NUMBERS IN THIS HANDLER DIFFER BY ONE ON PURPOSE: the script above
   is 14, the index of the chapter that has just been won, and this store is
   15, the index of the one that comes next.  Writing the same number in both
   places is wrong in one of them. */
#define CH15_NEXT_CHAPTER_ID 0xf

/* The gate on the whole un-retire block, CMP byte ptr [0x000640e8],0x0 / JZ at
   0003ac2c: element 0x10 of the 32-entry array
   data_fdps_map_cell_event_triggered_flags based at 0x000640d8 (gamedata.h),
   the slot this chapter family's one-shot handlers share.
   fdps_chapter_15_event_boss_defeat raises it at 00037c83 and only on the
   branch where the player accepted the duel, and fdps_chapter_state_reset
   clears the whole block when a chapter starts, so a chapter 15 won without a
   duel arrives here with it at 0. */
#define CH15_DUEL_LATCH_SLOT 0x10

/* How many records the un-retire loop walks, CMP dword ptr [EBP-0x8],0x9 / JL
   at 0003ac3c: unit indices 0..8, the roster block map14.dat's header sizes at
   9.

   IT SKIPS NOBODY.  The duel event's own retire loop steps over 裘娜 at index
   4 because she is one of the duellists; this loop does not, because by the
   time the chapter is over she has to be back on the field with the rest of
   the party.  Copying the event's guard across leaves her retired. */
#define CH15_DUEL_ROSTER_UNIT_COUNT 9

/* The one record outside that block the block also touches, PUSH 0x34 at
   0003ac64: map14.dat's wave-1 record, the level 20 archer 瑪麗安 that
   Icon14.dat deploys during the opening cutscene, sitting behind the 9 roster
   units and the 43 wave-0 records. */
#define CH15_DUEL_ARCHER_UNIT_INDEX 0x34

/* What each of records 0..8 is left holding in its flags byte, MOV byte ptr
   [EAX+0x5],0x0 at 0003ac5e.  IT IS A WHOLE-BYTE STORE AND NOT AN AND-NOT: it
   drops the retired bit 0x01 that fdps_unit_is_retired reads and everything
   else the byte was holding with it, the has-acted bit 0x80 included.  The
   duel event's matching store is the same shape in the other direction. */
#define CH15_UNIT_FLAGS_CLEARED 0

/* What record 0x34 is left holding in its behaviour byte at +0x34, MOV byte
   ptr [EAX+0x34],0x0 at 0003ac74: the AI behaviour mode reset to its default.

   THE OFFSET IS THE WHOLE POINT.  The duel event retired this record by
   writing 1 into byte +5; what is cleared here is byte +0x34, a different byte
   of the same record.  Record 0x34 therefore STAYS retired across the chapter
   end, and mirroring the duel event by clearing its flags byte as well would
   both un-retire a unit the original leaves retired and leave the behaviour
   byte the original resets. */
#define CH15_AI_BEHAVIOR_DEFAULT 0

/* 0003ac20.  The family's odd one out twice over: it is the only end handler
   with a branch in it, and the only one that does not open with
   fdps_battle_destroy_remaining_enemies.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003ac20..0003ac24 -- over SUB ESP,0x8, two local dwords: the
   loop counter at [EBP-0x8] and the record pointer at [EBP-0x4].  The four
   registers come back off the stack at 0003ac9a..0003ac9f and the RET at
   0003aca0 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,[EBP-0x8] / PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4 at
   0003ac4c..0003ac55, PUSH 0x34 / CALL / ADD ESP,0x4 at 0003ac64..0003ac6b,
   and MOV EAX,0x62158 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003ac7d..0003ac88.  The two argument-less calls at 0003ac78 and 0003ac8b
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set for a
   result before the RET, so it returns nothing.  The one caller agrees: the
   dispatcher at 00029395 loads the chapter index, scales it by four and CALLs
   through [EAX + 0x60304] with nothing pushed, no stack cleanup afterwards and
   no read of EAX.  Slot 14 of that table, at 0006033c, holds 0003ac20 and is
   the only reference to this function in the image.

   VALUES USED AFTER A CALL: the two fdps_get_unit_record calls only.  Each
   CALL is followed by MOV [EBP-0x4],EAX -- at 0003ac58 and 0003ac6e -- and the
   next instruction reloads that slot into EAX for the byte store, so EAX is
   the record pointer both times and it is the only thing either call hands
   back.  fdps_icon_script_run does leave a result in EAX and this call site
   discards it: the ADD ESP,0x4 and the CALL that follows overwrite it, and
   nothing in between reads it.  The two void calls set nothing this body
   looks at.

   THE ORDER OF THE FOUR STEPS IS THE ALGORITHM.  The un-retire runs BEFORE the
   writeback, and that is what it is for: the writeback banks a battle record
   onto its roster slot and skips 蘭迪斯 outright while his unit is retired
   (roster.h), so which records are standing when it runs decides which of them
   reach the roster the rest of the game reads.  The cut-scene then runs after
   the writeback, so a unit-record edit the script makes lands on a party that
   has already been banked; and the revive runs after the cut-scene, reading
   the roster the writeback has just filled, which is what makes it see the
   battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell; the 妖刀村雨 the duel is
   fought for is awarded by fdps_chapter_15_post_action (chpost1.h) and not
   here.  The chapter index store below is the only global it writes directly.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003ac90 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_15_end(void)
{
    /* The record each of the two stores is made through, [EBP-0x4].  The
       original reloads the slot into EAX between the call and the store
       rather than keeping the pointer in a register. */
    struct fdps_unit_record *unit;
    /* The un-retire loop's counter, [EBP-0x8]: a battle unit index. */
    int unit_index;

    if (data_fdps_map_cell_event_triggered_flags[CH15_DUEL_LATCH_SLOT] != 0) {
        for (unit_index = 0;
             unit_index < CH15_DUEL_ROSTER_UNIT_COUNT;
             unit_index++) {
            unit = fdps_get_unit_record(unit_index);
            unit->flags = CH15_UNIT_FLAGS_CLEARED;
        }
        unit = fdps_get_unit_record(CH15_DUEL_ARCHER_UNIT_INDEX);
        unit->ai_behavior = CH15_AI_BEHAVIOR_DEFAULT;
    }

    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH15_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH15_NEXT_CHAPTER_ID;
}
