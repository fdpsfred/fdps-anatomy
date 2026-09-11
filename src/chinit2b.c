/* chinit2b.c -- the per-chapter entry handlers, chapters 25 to 30: what the
 * game does at the moment it enters a chapter, before the battle loop runs.
 * Chapters 1 to 10 are chinit1.c, 11 to 15 are chinit1b.c and 16 to 24 are
 * chinit2.c.  This file starts at chapter 25 because chapter 24's handler is
 * the last in the game that adds anybody to the roster: the six here are the
 * run-in to the ending, and every one of them is the plain four-call form of
 * the family with only the cut-scene member's name differing.
 *
 * These are slots of the handler table based at 00060074, indexed by the
 * 0-based chapter id and called only through it, so the dispatcher in
 * title.c is not a static caller of any of them.
 *
 * See chinit2b.h for what each handler sets up.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "chapter.h"
#include "icon.h"
#include "mapcur.h"
#include "chinit2b.h"

/* --- fdps_chapter_25_init @ 000214d0 ----------------------------------- */

/* Chapter 25's opening cut-scene, the string at 0x61924 loaded into EAX at
   000214e1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon24.dat where the slot-23 handler at 00021490 holds Icon23.dat at
   0x61918 and the slot-25 handler at 00021510 holds Icon25.dat at 0x61930 --
   the three sit next to each other in the image, twelve bytes apart, each an
   eleven-byte member name and a filler byte.  Spelling the member out of the
   handler's own chapter number gives Icon25.dat, which loads, runs, and plays
   chapter 26's opening scene under chapter 25's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON24.DAT by the
   call and cannot live in read-only storage. */
#define CH25_OPENING_SCRIPT "Icon24.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000214f4, roster slot 0 and so
   蘭迪斯, which is what twenty-seven of the thirty handlers pass. */
#define CH25_CURSOR_UNIT 0

/* 000214d0.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_23_init, with no fdps_roster_add_character in front of them and
   no store behind them.  At 0x33 bytes it is the shortest shape the thirty
   handlers come in.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 000214d0..000214d4 -- over SUB ESP,0x0 at 000214d6, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000214fe..00021501
   and the RET at 00021502 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61924 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000214e1..000214ec, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000214f4..000214fb.  The two argument-less calls at 000214dc and 000214ef
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never read after
   any of the four calls, so it returns nothing -- the dispatcher reaches it
   through the twenty-fifth slot of the table at 00060074 (the dword at
   000600d4 is 000214d0, and that data reference is the function's only xref)
   and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER.  The first instruction after the
   prologue is the state rebuild, so the roster is left exactly as chapter 24
   finished it -- the eleven chapters 1 to 19 assembled plus 珊, whom chapter
   24's handler appended at slot 11 -- and MAP24.DAT asks for TWELVE player
   slots, the first map in the game to do so, so every slot has a member behind
   it and fdps_build_map_unit_array writes no zeroed, retired spare anywhere in
   the array (src/deploy.c).

   THE CUT-SCENE REBUILDS THE BOARD TWICE MORE BEHIND THIS HANDLER'S OWN
   REBUILD, which is what makes this member unlike its neighbours'.  Walked
   with the opcode ladder in src/icon.c, ICON24.DAT's 507 bytes hold
   eighty-eight opcodes and two of them are SWITCH_MAP: the third opcode, at
   script offset 4, sets the chapter id to 56 and resets the state onto that
   map, and the one at offset 183 sets it back to 24 and resets again.  Map 56
   is a cut-scene stage -- eleven player slots and two deployment records, both
   wave 0, so the scene plays there on a thirteen-unit board -- and the second
   reset throws that board away and rebuilds map 24's, so when the handler
   returns the chapter id reads 24 again, put back by the script rather than
   left untouched.  The handler neither reads nor writes that global itself;
   the dispatcher that reached this slot is what put 24 there.

   WHAT THE CUT-SCENE LEAVES BEHIND.  Its single RETIRE_UNIT, at script offset
   185 and the first opcode after the return to map 24, takes map unit 3 off
   the board with no REVIVE behind it.  A player slot's map unit index is its
   roster slot and the roster is in join order, so the member taken off is
   法蓮娜 and the eleven left are the guide's 己方 line for the chapter,
   法蓮娜以外的所有人.  The member has no DEPLOY_WAVE at all, so the
   opposition on the board is the map's own opening wave and nothing else.

   THE WHOLE OPENING BOARD IS THE MAP'S WAVE 0.  MAP24.DAT holds fifty-nine
   deployment records and tags forty-one of them wave 0, which the rebuild puts
   down behind the twelve player slots for fifty-three units.  Those forty-one
   are the guide's 敵方 list for the chapter less one group: LV30 塞克斯,
   布魯森 and 汎拉沛 one each (characters 64, 65 and 66), LV16 黑暗祭司 x1
   (104), LV16 地獄騎士 x8 (78), LV16 鎧甲武士 x18 (100) and LV16 神箭手 x11
   (95).  So the three 魔戰將軍 the chapter is named for are on the board from
   the first turn.  The other eighteen records are wave 1, eighteen LV16
   天空騎士 (97), which is the guide's 事件 reinforcement and no part of what
   this handler does.

   THE CURSOR ENDS ON A TILE THE CUT-SCENE CHOSE.  ICON24.DAT places map unit 0
   at (4, 0) at script offset 273 and then walks it down two tiles, one in each
   of the group walks at offsets 348 and 364 (facing operand 0 is down and each
   walk is one tile), so unit 0 is on (4, 2) when the cursor call reads it and
   the cursor lands on (96, 48).  Nothing of MAP24.COD's own start tiles
   survives into that number. */
void fdps_chapter_25_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH25_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH25_CURSOR_UNIT);
}
