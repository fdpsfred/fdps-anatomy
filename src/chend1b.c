/* chend1b.c -- the per-chapter end handlers, chapters 12 to 15: what the game
 * does at the moment a chapter's battle has been won, before the village phase
 * that follows it.  Chapters 1 to 11 are chend1.c and chapters 16 to 30 are
 * chend2.c.
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
