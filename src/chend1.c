/* chend1.c -- the per-chapter end handlers, chapters 1 to 15: what the game
 * does at the moment a chapter's battle has been won, before the village
 * phase that follows it.  Chapters 16 to 30 are chend2.c.
 *
 * These are slots of the handler table based at 00060304, indexed by the
 * 0-based chapter id and called only through it, so the dispatcher in main.c
 * is not a static caller of any of them.
 *
 * See chend1.h for what each handler closes out.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "btlend.h"
#include "icon.h"
#include "roster.h"
#include "unit.h"
#include "chend1.h"

/* Which battle unit the spell below is granted to: PUSH 0x0 at 0003a41e, the
   unit_index argument of fdps_set_flag_bit.  Chapter 1 deploys one party
   member and he is battle unit 0 -- 蘭迪斯, roster character 0.

   It is the BATTLE unit index and not a roster slot: fdps_set_flag_bit
   resolves the record through fdps_get_unit_record, which walks
   data_fdps_map_unit_array_ptr (src/unit.c). */
#define CH01_SPELL_RECIPIENT_UNIT 0

/* The spell he learns: PUSH 0x0 at 0003a41c, the spell_id argument, which
   fdps_set_flag_bit turns into bit 0 of the five-byte spells_known_bitmap at
   record +0x1a.  Spell 00 is 業火 (assets/spells.md).

   蘭迪斯's own line in FRIAPRDA.DAT carries an empty spell mask, so this call
   is the only place in the game he acquires it; none of the other fourteen
   handlers in this file has an equivalent step.  Dropping it as a stray
   one-off leaves the protagonist with no spells for the rest of the game
   (rebuild_info/pitfalls.md). */
#define CH01_STARTING_SPELL 0

/* Chapter 1's victory cut-scene, the string at 0x620b0 loaded into EAX at
   0003a42d and pushed as fdps_icon_script_run's only argument.  The
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so
   this literal is folded to WIN00.DAT by the call and cannot live in
   read-only storage.

   The number in the name is the 0-based chapter id and not a scene id of its
   own, the same way the Icon00.dat the chapter opened with is numbered. */
#define CH01_VICTORY_SCRIPT "Win00.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x1 at 0003a440.  The index is 0-based, so 1 is chapter 2
   -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 0 of a table indexed by this same global,
   and every handler in the family stores its own literal. */
#define CH01_NEXT_CHAPTER_ID 1

/* 0003a410.  Four calls and one store, straight line, no branch and no loop.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003a410..0003a414 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0003a44a..0003a44d
   and the RET at 0003a44e is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x0 / PUSH 0x0 / CALL 0x000282b0 / ADD ESP,0x8 at
   0003a41c..0003a425, and MOV EAX,0x620b0 / PUSH EAX / CALL 0x00021650 / ADD
   ESP,0x4 at 0003a42d..0003a438.  The two argument-less calls at 0003a428 and
   0003a43b are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set for a result before the RET -- the only write to it is the one that
   carries the script name into the third call -- so it returns nothing.  The
   one caller agrees: the dispatcher at 00029395 loads the chapter index,
   scales it by four and CALLs through [EAX + 0x60304] with nothing pushed, no
   stack cleanup afterwards and no read of EAX (the next instruction is
   another CALL).

   VALUES USED AFTER A CALL: none at all.  All four callees return void as far
   as this body is concerned and nothing here reads EAX after any of them.
   fdps_set_flag_bit does leave the resolved unit record in EAX, but this call
   site is one of the five that discard it: ADD ESP,0x8 and then the next CALL
   overwrite it.

   THE ORDER IS THE ALGORITHM, and its first two steps are the reason this
   handler is not interchangeable with the rest of the family.

   The spell runs BEFORE the writeback.  fdps_roster_write_back_battle_units
   memmoves the whole 0x50-byte battle record over the roster record and then
   clears only the six status timers at +0x22 (src/roster.c); +0x1a, the spell
   bitmap, is inside the copied block and outside the cleared range, so the
   bit set on the live battle record is what reaches the roster.  Setting it
   after the writeback, or on the roster record instead, leaves it on a copy
   the next chapter's deployment overwrites and loses the spell with no
   symptom at the point of the mistake.

   The cut-scene runs AFTER the writeback and the revive AFTER the cut-scene.
   The script is interpreted with the battle's unit array still standing, so a
   unit-record edit it makes lands on a party that has already been banked and
   reaches the roster only if the script itself asks for another writeback
   (opcode 0x61, src/icon.c).  The revive then reads the roster the writeback
   has just filled, which is what makes it see the battle's casualties at
   all.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003a440 is the handler's last act and the only thing it leaves for the
   phase that follows.  Like fdps_chapter_02_end it does not call
   fdps_battle_destroy_remaining_enemies first. */
void fdps_chapter_01_end(void)
{
    fdps_set_flag_bit(CH01_SPELL_RECIPIENT_UNIT, CH01_STARTING_SPELL);
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH01_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH01_NEXT_CHAPTER_ID;
}

/* Chapter 2's victory cut-scene, the string at 0x620bc loaded into EAX at
   0003a481 and pushed as fdps_icon_script_run's only argument.  The
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so
   this literal is folded to WIN01.DAT by the call and cannot live in
   read-only storage.

   The number in the name is the 0-based chapter id, so chapter 2's scene is
   Win01.dat and not Win02.dat. */
#define CH02_VICTORY_SCRIPT "Win01.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x2 at 0003a494.  The index is 0-based, so 2 is chapter 3
   -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 1 of a table indexed by this same global,
   and every handler in the family stores its own literal. */
#define CH02_NEXT_CHAPTER_ID 2

/* 0003a470.  Three calls and one store, straight line, no branch and no loop.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003a470..0003a474 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0003a49e..0003a4a1
   and the RET at 0003a4a2 is bare.

   CALLING CONVENTION.  The one argument goes on the stack and the caller takes
   it back: MOV EAX,0x620bc / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003a481..0003a48c.  The two argument-less calls at 0003a47c and 0003a48f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set for a
   result before the RET -- the only write to it is the one that carries the
   script name into the second call -- so it returns nothing.  The one caller
   agrees: the dispatcher at 00029395 loads the chapter index, scales it by
   four and CALLs through [EAX + 0x60304] with nothing pushed, no stack
   cleanup afterwards and no read of EAX (the next instruction is another
   CALL).  Slot 1 of that table, at 00060308, holds 0003a470 and is the only
   reference to this function in the image.

   VALUES USED AFTER A CALL: none at all.  All three callees return void as far
   as this body is concerned and nothing here reads EAX after any of them; the
   ADD ESP,0x4 and the CALL that follows it overwrite whatever the middle call
   left there.

   THE ORDER IS THE ALGORITHM.  The cut-scene runs AFTER the writeback and the
   revive AFTER the cut-scene.  The script is interpreted with the battle's
   unit array still standing, so a unit-record edit it makes lands on a party
   that has already been banked and reaches the roster only if the script
   itself asks for another writeback (opcode 0x61, src/icon.c).  The revive
   then reads the roster the writeback has just filled, which is what makes it
   see the battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it is the plain three-step shape, and it does not
   call fdps_battle_destroy_remaining_enemies first: chapter 2 is won only by
   retiring every enemy, so there is never one left standing when this runs.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003a494 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_02_end(void)
{
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH02_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH02_NEXT_CHAPTER_ID;
}

/* Chapter 3's victory cut-scene, the string at 0x620c8 loaded into EAX at
   0003a536 and pushed as fdps_icon_script_run's only argument.  The
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so
   this literal is folded to WIN02.DAT by the call and cannot live in
   read-only storage.

   The number in the name is the 0-based chapter id of the chapter that has
   just ENDED, so chapter 3's scene is Win02.dat -- the same slot number the
   handler itself was dispatched through, and one less than the index it
   leaves behind. */
#define CH03_VICTORY_SCRIPT "Win02.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x3 at 0003a549.  The index is 0-based, so 3 is chapter 4
   -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 2 of a table indexed by this same global,
   and every handler in the family stores its own literal. */
#define CH03_NEXT_CHAPTER_ID 3

/* 0003a520.  Four calls and one store, straight line, no branch and no loop.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003a520..0003a524 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0003a553..0003a556
   and the RET at 0003a557 is bare.

   CALLING CONVENTION.  The one argument goes on the stack and the caller takes
   it back: MOV EAX,0x620c8 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003a536..0003a541.  The three argument-less calls at 0003a52c, 0003a531 and
   0003a544 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set for a result before the RET -- the only write to it is the one that
   carries the script name into the third call -- so it returns nothing.  The
   one caller agrees: the dispatcher at 00029395 loads the chapter index,
   scales it by four and CALLs through [EAX + 0x60304] with nothing pushed, no
   stack cleanup afterwards and no read of EAX (the next instruction is another
   CALL).  Slot 2 of that table, at 0006030c, holds 0003a520 and is the only
   reference to this function in the image.

   VALUES USED AFTER A CALL: none at all.  All four callees return void as far
   as this body is concerned and nothing here reads EAX after any of them;
   fdps_icon_script_run does leave a result in EAX and this call site discards
   it -- the ADD ESP,0x4 and the CALL that follows overwrite it.

   THE MAP IS CLEARED FIRST, and that is what separates this handler from
   chapters 1 and 2.  Chapter 3 is cleared by retiring unit slot 4, the map's
   魔導士, with no test of the enemy side at all
   (fdps_chapter_03_post_action, chpost1.h), so enemies are normally still
   standing when this runs.  fdps_battle_destroy_remaining_enemies zeroes the
   hit-point word of every unit on side 0 and then plays the ones that are not
   already retired off the map (btlend.h), so the cut-scene below is
   interpreted over a cleared field.  Dropping the call leaves the survivors
   standing through the victory scene.

   It runs BEFORE the writeback, and the two cannot be swapped blind: the
   destroy pass ends in the death animation, which sets the flags byte of every
   unit it plays off to 1, and the writeback both reads that byte and masks the
   roster copy down to it.  Enemy units carry character ids the roster does not
   hold, so on the shipped data the banked party is the same either way, but
   the order here is the original's and nothing in this handler re-establishes
   it.

   THE ORDER OF THE REST IS THE ALGORITHM.  The cut-scene runs AFTER the
   writeback and the revive AFTER the cut-scene.  The script is interpreted
   with the battle's unit array still standing, so a unit-record edit it makes
   lands on a party that has already been banked and reaches the roster only if
   the script itself asks for another writeback (opcode 0x61, src/icon.c).  The
   revive then reads the roster the writeback has just filled, which is what
   makes it see the battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003a549 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_03_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH03_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH03_NEXT_CHAPTER_ID;
}

/* Chapter 4's victory cut-scene, the string at 0x620d4 loaded into EAX at
   0003a5a6 and pushed as fdps_icon_script_run's only argument.  The
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so
   this literal is folded to WIN03.DAT by the call and cannot live in
   read-only storage.

   The number in the name is the 0-based chapter id of the chapter that has
   just ENDED, so chapter 4's scene is Win03.dat -- the same slot number the
   handler itself was dispatched through, and one less than the index it
   leaves behind. */
#define CH04_VICTORY_SCRIPT "Win03.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x4 at 0003a5b9.  The index is 0-based, so 4 is chapter 5
   -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 3 of a table indexed by this same global,
   and every handler in the family stores its own literal. */
#define CH04_NEXT_CHAPTER_ID 4

/* 0003a590.  Four calls and one store, straight line, no branch and no loop --
   instruction for instruction the same body as fdps_chapter_03_end above, with
   a different script name and a different stored index.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003a590..0003a594 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0003a5c3..0003a5c6
   and the RET at 0003a5c7 is bare.

   CALLING CONVENTION.  The one argument goes on the stack and the caller takes
   it back: MOV EAX,0x620d4 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003a5a6..0003a5b1.  The three argument-less calls at 0003a59c, 0003a5a1 and
   0003a5b4 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set for a result before the RET -- the only write to it is the one that
   carries the script name into the third call -- so it returns nothing.  The
   one caller agrees: the dispatcher at 00029395 loads the chapter index,
   scales it by four and CALLs through [EAX + 0x60304] with nothing pushed, no
   stack cleanup afterwards and no read of EAX (the next instruction is another
   CALL).  Slot 3 of that table, at 00060310, holds 0003a590 and is the only
   reference to this function in the image.

   VALUES USED AFTER A CALL: none at all.  All four callees return void as far
   as this body is concerned and nothing here reads EAX after any of them;
   fdps_icon_script_run does leave a result in EAX and this call site discards
   it -- the ADD ESP,0x4 and the CALL that follows overwrite it.

   THE MAP IS SWEPT FIRST, as it is in chapter 3's handler, and here the sweep
   is a belt-and-braces step rather than the load-bearing one it is there.
   Chapter 4's verdict comes from fdps_chapter_04_post_action (chpost1.h),
   which is the shared end test plus one defeat test of its own, and the shared
   test only records a clear when no unit on side 0 is still standing
   (btlend.h); the dispatcher at 000293a1 reaches this table only on that
   verdict.  So on the shipped data the sweep normally finds the enemy side
   already empty and its hit-point stores land on units that have retired.
   What it does is not conditional on that: fdps_battle_destroy_remaining_enemies
   zeroes the hit-point word of every unit on side 0 whether or not it has left
   the field, and plays the ones that are not already retired off the map
   (btlend.h).

   THE ORDER OF THE REST IS THE ALGORITHM.  The cut-scene runs AFTER the
   writeback and the revive AFTER the cut-scene.  The script is interpreted
   with the battle's unit array still standing, so a unit-record edit it makes
   lands on a party that has already been banked and reaches the roster only if
   the script itself asks for another writeback (opcode 0x61, src/icon.c).  The
   revive then reads the roster the writeback has just filled, which is what
   makes it see the battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003a5b9 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_04_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH04_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH04_NEXT_CHAPTER_ID;
}

/* Chapter 5's victory cut-scene, the string at 0x620e0 loaded into EAX at
   0003a616 and pushed as fdps_icon_script_run's only argument.  The
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so
   this literal is folded to WIN04.DAT by the call and cannot live in
   read-only storage.

   The number in the name is the 0-based chapter id of the chapter that has
   just ENDED, so chapter 5's scene is Win04.dat -- the same slot number the
   handler itself was dispatched through, and one less than the index it
   leaves behind. */
#define CH05_VICTORY_SCRIPT "Win04.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x5 at 0003a629.  The index is 0-based, so 5 is chapter 6
   -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 4 of a table indexed by this same global,
   and every handler in the family stores its own literal. */
#define CH05_NEXT_CHAPTER_ID 5

/* 0003a600.  Four calls and one store, straight line, no branch and no loop --
   instruction for instruction the same body as fdps_chapter_03_end and
   fdps_chapter_04_end above, with a different script name and a different
   stored index.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003a600..0003a604 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0003a633..0003a636
   and the RET at 0003a637 is bare.

   CALLING CONVENTION.  The one argument goes on the stack and the caller takes
   it back: MOV EAX,0x620e0 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003a616..0003a621.  The three argument-less calls at 0003a60c, 0003a611 and
   0003a624 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set for a result before the RET -- the only write to it is the one that
   carries the script name into the third call -- so it returns nothing.  The
   one caller agrees: the dispatcher at 00029395 loads the chapter index,
   scales it by four and CALLs through [EAX + 0x60304] with nothing pushed, no
   stack cleanup afterwards and no read of EAX (the next instruction is another
   CALL).  Slot 4 of that table, at 00060314, holds 0003a600 and is the only
   reference to this function in the image.

   VALUES USED AFTER A CALL: none at all.  All four callees return void as far
   as this body is concerned and nothing here reads EAX after any of them;
   fdps_icon_script_run does leave a result in EAX and this call site discards
   it -- the ADD ESP,0x4 and the CALL that follows overwrite it.

   THE MAP IS SWEPT FIRST, as it is in chapters 3 and 4, and here the sweep is
   the belt-and-braces step it is in chapter 4 rather than the load-bearing one
   it is in chapter 3.  Chapter 5's verdict comes from
   fdps_chapter_05_post_action (chpost1.h), which is the shared end test plus
   one defeat test of its own on unit slot 3, and the shared test only records
   a clear when no unit on side 0 is still standing (btlend.h); the dispatcher
   at 000293a1 reaches this table only on that verdict.  So on the shipped data
   the sweep normally finds the enemy side already empty and its hit-point
   stores land on units that have retired.  What it does is not conditional on
   that: fdps_battle_destroy_remaining_enemies zeroes the hit-point word of
   every unit on side 0 whether or not it has left the field, and plays the
   ones that are not already retired off the map (btlend.h).

   THE ORDER OF THE REST IS THE ALGORITHM.  The cut-scene runs AFTER the
   writeback and the revive AFTER the cut-scene.  The script is interpreted
   with the battle's unit array still standing, so a unit-record edit it makes
   lands on a party that has already been banked and reaches the roster only if
   the script itself asks for another writeback (opcode 0x61, src/icon.c).  The
   revive then reads the roster the writeback has just filled, which is what
   makes it see the battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003a629 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_05_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH05_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH05_NEXT_CHAPTER_ID;
}

/* Chapter 6's victory cut-scene, the string at 0x620ec loaded into EAX at
   0003a686 and pushed as fdps_icon_script_run's only argument.  The
   interpreter upper-cases the member name in place before the container
   compare (vfs.h, rebuild_info/pitfalls.md), so this literal is folded to
   WIN05.DAT by the call and cannot live in read-only storage.

   The number in the name is the 0-based index of the chapter that has just
   ENDED, so chapter 6's scene is Win05.dat -- the same slot number the handler
   itself was dispatched through, and one less than the index it leaves
   behind. */
#define CH06_VICTORY_SCRIPT "Win05.dat"

/* What the handler leaves in data_fdps_chapter_current_chapter_id: MOV dword
   ptr [0x00069cf4],0x6 at 0003a699.  The index is 0-based, so 6 is chapter 7
   -- both the village phase that runs next and the chapter loaded after it
   read this global, so this one store is what advances the game.

   It is an assignment of the chapter's successor and not an increment: the
   handler was reached through slot 5 of a table indexed by this same global,
   and every handler in the family stores its own literal. */
#define CH06_NEXT_CHAPTER_ID 6

/* 0003a670.  Four calls and one store, straight line, no branch and no loop --
   instruction for instruction the same body as fdps_chapter_03_end,
   fdps_chapter_04_end and fdps_chapter_05_end above, with a different script
   name and a different stored index.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 0003a670..0003a674 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form at 0003a676..0003a67b.  Nothing is
   addressed off EBP anywhere in the body, so there is no local here to name
   and none is declared; the four registers come back off the stack at
   0003a6a3..0003a6a6 and the RET at 0003a6a7 is bare.

   CALLING CONVENTION.  The one argument goes on the stack and the caller takes
   it back: MOV EAX,0x620ec / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0003a686..0003a691.  The three argument-less calls at 0003a67c, 0003a681 and
   0003a694 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set for a result before the RET -- the only write to it is the one that
   carries the script name into the third call -- so it returns nothing.  The
   one caller agrees: the dispatcher at 00029395 loads the chapter index,
   scales it by four and CALLs through [EAX + 0x60304] with nothing pushed, no
   stack cleanup afterwards and no read of EAX (the next instruction is another
   CALL).  Slot 5 of that table, at 00060318, holds 0003a670 and is the only
   reference to this function in the image.

   VALUES USED AFTER A CALL: none at all.  All four callees return void as far
   as this body is concerned and nothing here reads EAX after any of them;
   fdps_icon_script_run does leave a result in EAX and this call site discards
   it -- the ADD ESP,0x4 and the CALL that follows overwrite it.

   THE MAP IS SWEPT FIRST, as it is in chapters 3, 4 and 5, and here the sweep
   is the belt-and-braces step it is in chapters 4 and 5 rather than the
   load-bearing one it is in chapter 3.  Chapter 6's verdict comes from
   fdps_chapter_06_post_action (chpost1.h), which is the shared end test plus
   one defeat test of its own on unit slot 3 -- 法蓮娜 on this chapter's map --
   and the shared test only records a clear when no unit on side 0 is still
   standing (btlend.h); the dispatcher at 000293a1 reaches this table only on
   that verdict.  So on the shipped data the sweep normally finds the enemy
   side already empty and its hit-point stores land on units that have retired.
   What it does is not conditional on that: fdps_battle_destroy_remaining_enemies
   zeroes the hit-point word of every unit on side 0 whether or not it has left
   the field, and plays the ones that are not already retired off the map
   (btlend.h).

   THE ORDER OF THE REST IS THE ALGORITHM.  The cut-scene runs AFTER the
   writeback and the revive AFTER the cut-scene.  The script is interpreted
   with the battle's unit array still standing, so a unit-record edit it makes
   lands on a party that has already been banked and reaches the roster only if
   the script itself asks for another writeback (opcode 0x61, src/icon.c).  The
   revive then reads the roster the writeback has just filled, which is what
   makes it see the battle's casualties at all.

   WHAT THIS HANDLER DOES NOT DO.  It grants nothing before the writeback, so
   unlike fdps_chapter_01_end it awards no spell.

   THE CHAPTER IS ADVANCED HERE and nowhere else on this path: the store at
   0003a699 is the handler's last act and the only thing it leaves for the
   phase that follows. */
void fdps_chapter_06_end(void)
{
    fdps_battle_destroy_remaining_enemies();
    fdps_roster_write_back_battle_units();
    fdps_icon_script_run(CH06_VICTORY_SCRIPT);
    fdps_roster_revive_fallen_members();
    data_fdps_chapter_current_chapter_id = CH06_NEXT_CHAPTER_ID;
}
