/* chinit2.c -- the per-chapter entry handlers, chapters 16 to 30: what the
 * game does at the moment it enters a chapter, before the battle loop runs.
 * Chapters 1 to 10 are chinit1.c and 11 to 15 are chinit1b.c.
 *
 * These are slots of the handler table based at 00060074, indexed by the
 * 0-based chapter id and called only through it, so the dispatcher in
 * title.c is not a static caller of any of them.
 *
 * See chinit2.h for what each handler sets up.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "chapter.h"
#include "icon.h"
#include "mapcur.h"
#include "chinit2.h"

/* --- fdps_chapter_16_init @ 00021290 ----------------------------------- */

/* Chapter 16's opening cut-scene, the string at 0x618b8 loaded into EAX at
   000212a1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon15.dat where fdps_chapter_15_init holds Icon14.dat at the
   same position and fdps_chapter_17_init holds Icon16.dat -- the two names sit
   next to each other in the image, "Icon15.dat" at 0x618b8 and "Icon16.dat" at
   0x618c4.  Spelling the member out of the handler's own chapter number gives
   Icon16.dat, which loads, runs, and plays chapter 17's opening scene under
   chapter 16's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON15.DAT by the
   call and cannot live in read-only storage. */
#define CH16_OPENING_SCRIPT "Icon15.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000212b4. */
#define CH16_CURSOR_UNIT 0

/* 00021290.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_12_init, fdps_chapter_13_init and fdps_chapter_14_init, with no
   fdps_roster_add_character in front of them and no store behind them.  It is
   the shortest of the thirty at 0x33 bytes.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021290..00021294 -- over SUB ESP,0x0 at 00021296, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000212be..000212c1
   and the RET at 000212c2 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x618b8 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000212a1..000212ac, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000212b4..000212bb.  The two argument-less calls at 0002129c and 000212af
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   second call, so it returns nothing -- the dispatcher reaches it through the
   sixteenth slot of the table at 00060074 (the dword at 000600b0 is 00021290,
   and that data reference is the function's only xref) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, AND EVERY SLOT IS FILLED.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 15 finished it: the eight members chapters 1 to 4 and 7
   to 9 put on, 琴琴 from chapter 11 and 瑪麗安 from chapter 15 -- ten.
   MAP15.DAT declares TEN player slots -- byte +1 of the block (src/rsrc.c) --
   and this is the first map that asks for ten, so every slot has a member
   behind it and fdps_build_map_unit_array writes no zeroed, retired spare
   anywhere in the array (src/deploy.c).

   HALF THE PARTY IS TAKEN OFF THE BOARD BY THE CUT-SCENE, NOT BY THIS
   HANDLER.  Walked with the opcode ladder in src/icon.c, the shipped
   ICON15.DAT's 649 bytes hold five RETIREs in a row at script offsets 438 to
   446 -- map units 3, 4, 7, 8 and 9 -- and no REVIVE anywhere, so those five
   player slots are retired when the handler returns and stay that way.  Map
   unit i is roster slot i for the ten player slots, and the roster is in join
   order, so those five are 法蓮娜, 裘娜, 蓋亞, 琴琴 and 瑪麗安; the five left
   standing are 蘭迪斯, 尤利安, 亞克, 費塔加 and 布蘭多, which is the strategy
   guide's 己方 line for the chapter exactly.  Chapter 17's own handler is the
   other half of the same split -- the guide gives it the five this one retires.

   THE WHOLE OPPOSITION IS DOWN BEFORE THE PLAYER MOVES.  MAP15.DAT holds
   twenty-five deployment records and tags thirteen of them wave 0, so the
   reset's own opening deploy puts those thirteen behind the ten player slots,
   and the cut-scene's four DEPLOY_WAVEs at script offsets 598, 609, 628 and
   636 -- waves 1, 2, 3 and 4, each with the place-exact operand 1 -- put down
   the other twelve, for thirty-five units when the handler returns.  Those
   twenty-five are the strategy guide's 敵方 list for the chapter to the
   number, by character id and level: LV16 暗魔導士 x4 is character 103, LV11
   騎士 x4 is 89, LV14 武士 x9 is 99, LV14 弓箭手 x4 is 94 and LV12 飛兵 x4 is
   96 -- the same five ids chapters 13 and 14 pin at their own levels.  The
   guide's one event for the chapter, the second attack that starts when every
   騎士 is dead, deploys nobody: the map has no wave the cut-scene leaves
   behind.

   THE CUT-SCENE WALKS UNIT 0, SO THE CURSOR CALL IS NOT WHERE THE MAP PUT IT.
   MAP15.COD record 25 -- the first record past the map's twenty-five scripted
   ones, and so player slot 0's start tile -- is (19, 38), and the nine group
   walks that list map unit 0 move it four tiles up, two right, one down and
   then five more single steps, for a net (+2, -5) and a finish on (21, 33).
   The cursor call is the last of the four.

   NOTHING IS WRITTEN ON A UNIT'S STATUS.  The body has no store in it at all,
   and the cut-scene adds no status either: walked with the same ladder the
   shipped ICON15.DAT holds no SET_UNIT_TIMER anywhere.  The guide lists no
   status on anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and this member carries no SWITCH_MAP to disturb
   it.  The Icon15.dat above is the one place the chapter number is spelled out
   rather than read. */
void fdps_chapter_16_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH16_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH16_CURSOR_UNIT);
}
