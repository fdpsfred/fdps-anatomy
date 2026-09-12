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

/* --- fdps_chapter_26_init @ 00021510 ----------------------------------- */

/* Chapter 26's opening cut-scene, the string at 0x61930 loaded into EAX at
   00021521 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon25.dat where the slot-24 handler at 000214d0 holds Icon24.dat at
   0x61924 and the slot-26 handler at 00021550 holds Icon26.dat at 0x6193c --
   the four literals sit end to end twelve bytes apart, each an eleven-byte
   member name and one filler byte, so each is its own symbol and none of them
   is a folded index off the one before it.  Spelling the member out of the
   handler's own chapter number gives Icon26.dat, which loads, runs, and plays
   chapter 27's opening scene under chapter 26's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON25.DAT by the
   call and cannot live in read-only storage. */
#define CH26_OPENING_SCRIPT "Icon25.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00021534, roster slot 0 and so
   蘭迪斯, which is what twenty-seven of the thirty handlers pass. */
#define CH26_CURSOR_UNIT 0

/* 00021510.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_25_init, with no fdps_roster_add_character in front of them and
   no store behind them.  At 0x33 bytes it is the shortest shape the thirty
   handlers come in, and every byte but the literal's address is the same as
   the handler on either side.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021510..00021514 -- over SUB ESP,0x0 at 00021516, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002153e..00021541
   and the RET at 00021542 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61930 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021521..0002152c, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021534..0002153b.  The two argument-less calls at 0002151c and 0002152f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never read after
   any of the four calls, so it returns nothing -- the dispatcher reaches it
   through the twenty-sixth slot of the table at 00060074 (the dword at
   000600d8 is 00021510, and that data reference is the function's only xref)
   and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER either.  The first instruction after the
   prologue is the state rebuild, so the roster is left exactly as chapter 24
   finished it -- the eleven chapters 1 to 19 assembled plus 珊 -- and
   MAP25.DAT asks for twelve player slots, so every slot has a member behind it
   and fdps_build_map_unit_array writes no zeroed, retired spare anywhere in
   the array (src/deploy.c).

   THE CUT-SCENE SWITCHES NO MAP, which is what makes this member unlike
   chapter 25's.  Walked with the opcode ladder in src/icon.c, ICON25.DAT's
   1,564 bytes hold 142 opcodes and not one of them is SWITCH_MAP or
   DEPLOY_WAVE: the whole scene plays on the board this handler's own reset
   built, and the chapter id is never written by anything but the dispatcher
   that reached this slot.

   WHAT THE CUT-SCENE LEAVES BEHIND is one player slot off the board.  It
   retires map units 1 to 11 at script offsets 7 to 27 so that only 蘭迪斯 is
   drawn through the opening, and then revives 1, 2 and 4 to 11 at offsets 696
   to 714 -- map unit 3 is the one index the revive run skips, and there is no
   later REVIVE for it.  A player slot's map unit index is its roster slot and
   the roster is in join order, so the member left off is 法蓮娜 and the eleven
   remaining are the guide's 己方 line for the chapter,
   法蓮娜以外的所有人.  Chapter 25 arrives at the same eleven by retiring that
   one slot outright; this member arrives there by retiring everybody and
   bringing all but that one back.

   THE OPENING BOARD IS THE MAP'S WAVE 0 AND NOTHING ELSE, and on this chapter
   that is nearly the whole file.  MAP25.DAT holds eighty deployment records
   and tags sixty-eight of them wave 0 -- the wave tag is byte 21 of the
   26-byte struct fdps_char_spawn_record -- which the rebuild puts down behind
   the twelve player slots for eighty map units in all.  Those sixty-eight are
   the guide's 敵方 list bar one group: the four LV30 named generals 凱因巴,
   塞克斯, 布魯森 and 汎拉沛 one each (characters 67, 64, 65 and 66), LV18
   黑暗祭司 x4 (104), LV18 地獄騎士 x10 (78), LV18 神箭手 x10 (95), LV18
   鎧甲武士 x24 (100) and LV18 天空騎士 x16 (97).  So all four 魔戰將軍 are
   on the board from the first turn.  Only twelve records are held back: seven
   more LV18 鎧甲武士 in wave 2, the guide's second 鎧甲武士 line, and five in
   wave 3 that are its 友方 -- the LV40 英雄索爾 (12) and four LV40 侍衛 (59),
   both on the guest side.  Neither wave is any part of what this handler does.

   THE CURSOR ENDS ON A TILE THE CUT-SCENE WALKED TO.  MAP25.COD starts player
   slot 0 on (6, 36) and ICON25.DAT walks map unit 0 four tiles with facing 2,
   which src/icon.c's ladder makes a step up, at script offset 67; nothing
   places unit 0 outright anywhere in the member.  So unit 0 is on (6, 32) when
   the cursor call reads it and the cursor lands on (144, 768). */
void fdps_chapter_26_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH26_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH26_CURSOR_UNIT);
}

/* --- fdps_chapter_27_init @ 00021550 ----------------------------------- */

/* Chapter 27's opening cut-scene, the string at 0x6193c loaded into EAX at
   00021561 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon26.dat where the slot-25 handler at 00021510 holds Icon25.dat at
   0x61930 and the slot-27 handler at 00021590 holds Icon27.dat at 0x61948 --
   the literals sit end to end twelve bytes apart, eleven bytes of member name
   and one filler byte each, so each is its own symbol and none is a folded
   index off the one before it.  The byte at 0x6193c is the 'I' itself, so the
   literal takes no offset (rebuild_info/pitfalls.md).  Spelling the member out
   of the handler's own chapter number gives Icon27.dat, which loads, runs, and
   plays chapter 28's opening scene under chapter 27's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON26.DAT by the
   call and cannot live in read-only storage. */
#define CH27_OPENING_SCRIPT "Icon26.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00021574, roster slot 0 and so
   蘭迪斯, which is what twenty-seven of the thirty handlers pass. */
#define CH27_CURSOR_UNIT 0

/* 00021550.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_26_init, with no fdps_roster_add_character in front of them and
   no store behind them.  At 0x33 bytes it is the shortest shape the thirty
   handlers come in, and every byte but the literal's address is the same as
   the handler on either side.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021550..00021554 -- over SUB ESP,0x0 at 00021556, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002157e..00021581
   and the RET at 00021582 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x6193c / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021561..0002156c, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021574..0002157b.  The two argument-less calls at 0002155c and 0002156f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never read after
   any of the four calls, so it returns nothing -- the dispatcher reaches it
   through the twenty-seventh slot of the table at 00060074 (the dword at
   000600dc is 00021550, and that data reference is the function's only xref)
   and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER either.  The first instruction after the
   prologue is the state rebuild, so the roster is left exactly as chapter 24
   finished it -- the eleven chapters 1 to 19 assembled plus 珊 -- and
   MAP26.DAT asks for twelve player slots, so every slot has a member behind it
   and fdps_build_map_unit_array writes no zeroed, retired spare anywhere in
   the array (src/deploy.c).

   THE CUT-SCENE SWITCHES NO MAP AND DEPLOYS NO WAVE, the same as chapter 26's
   and unlike chapter 25's.  Walked with the opcode ladder in src/icon.c,
   ICON26.DAT's 719 bytes hold seventy-five opcodes and not one of them is
   SWITCH_MAP or DEPLOY_WAVE: the whole scene plays on the board this handler's
   own reset built, and the chapter id is never written by anything but the
   dispatcher that reached this slot.

   WHAT THE CUT-SCENE LEAVES BEHIND is one player slot off the board.  Its
   third opcode, the RETIRE_UNIT at script offset 4, names map unit 3, and the
   member carries no REVIVE_UNIT at all.  A player slot's map unit index is its
   roster slot and the roster is in join order, so the member left off is
   法蓮娜 and the eleven remaining are the guide's 己方 line for the chapter,
   法蓮娜以外的所有人.  The FACE_UNITS at script offset 298 lists eleven
   units and skips 3, which is the same eleven read a second way.

   THE OPENING BOARD IS THE MAP'S WAVE 0 AND NOTHING ELSE.  MAP26.DAT is a
   131-byte header and fifty-five 26-byte deployment records -- the wave tag is
   byte 21 of struct fdps_char_spawn_record -- and twenty-five of them are wave
   0, which the rebuild puts down behind the twelve player slots for
   thirty-seven map units in all.  Those twenty-five are the guide's 敌方 list
   bar one group: LV40 魔導王吉歐 once (character 63), the four LV30
   魔戰將軍 塞克斯, 布魯森, 汎拉沫 and 凱因巴 once each (64, 65, 66 and
   67), LV18 神箭手 x8 (95) and LV18 鑺甲武士 x12 (100).  吉歐 is record 0
   of the file and so becomes map unit 12, the slot chapter 27's victory test
   asks about (src/chpost3.c), and the four 魔戰將軍 follow him at 13 to 16 --
   so the boss and all four generals stand on the field from the first turn.
   The thirty records held back are the whole of wave 1, LV18 地獄騎士 x10
   (78) and LV18 天空騎士 x20 (97), which is the guide's 事件 -- the
   reinforcement that arrives once the four 魔戰將軍 are down, landing at map
   units 37 to 66 -- and no part of what this handler does.

   THE CURSOR ENDS ON A TILE THE CUT-SCENE WALKED TO.  MAP26.COD starts player
   slot 0 on (8, 27) -- the record at 0xb + (55 + 0) * 6, read as two signed
   words (src/deploy.c) -- and ICON26.DAT walks map unit 0 one tile with facing
   2 four separate times, at script offsets 326, 332, 351 and 367, which
   src/icon.c's ladder makes a step up each; nothing places unit 0 outright
   anywhere in the member.  So unit 0 is on (8, 23) when the cursor call reads
   it and the cursor lands on (192, 552).  The only PLACE_UNIT in the member,
   at script offset 6, is 吉歐's: map unit 12 is put on (8, 10) and then walked
   two tiles up and one back down, which is nothing the cursor call reads. */
void fdps_chapter_27_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH27_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH27_CURSOR_UNIT);
}

/* --- fdps_chapter_28_init @ 00021590 ----------------------------------- */

/* Chapter 28's opening cut-scene, the string at 0x61948 loaded into EAX at
   000215a1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon27.dat where the slot-26 handler at 00021550 holds Icon26.dat at
   0x6193c and the slot-28 handler at 000215d0 holds Icon28.dat at 0x61954 --
   the literals sit end to end twelve bytes apart, eleven bytes of member name
   and one filler byte each, so each is its own symbol and none is a folded
   index off the one before it.  The byte at 0x61948 is the 'I' itself, so the
   literal takes no offset (rebuild_info/pitfalls.md).  Spelling the member out
   of the handler's own chapter number gives Icon28.dat, which loads, runs, and
   plays chapter 29's opening scene under chapter 28's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON27.DAT by the
   call and cannot live in read-only storage. */
#define CH28_OPENING_SCRIPT "Icon27.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000215b4, roster slot 0 and so
   蘭迪斯, which is what twenty-seven of the thirty handlers pass. */
#define CH28_CURSOR_UNIT 0

/* 00021590.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_27_init, with no fdps_roster_add_character in front of them and
   no store behind them.  At 0x33 bytes it is the shortest shape the thirty
   handlers come in, and every byte but the literal's address is the same as
   the handler on either side.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021590..00021594 -- over SUB ESP,0x0 at 00021596, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000215be..000215c1
   and the RET at 000215c2 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61948 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000215a1..000215ac, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000215b4..000215bb.  The two argument-less calls at 0002159c and 000215af
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never read after
   any of the four calls, so it returns nothing -- the dispatcher reaches it
   through the twenty-eighth slot of the table at 00060074 (the dword at
   000600e0 is 00021590, and that data reference is the function's only xref)
   and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER either.  The first instruction after the
   prologue is the state rebuild, so the roster is left exactly as chapter 24
   finished it -- the eleven chapters 1 to 19 assembled plus 珊 -- and
   MAP27.DAT asks for twelve player slots, so every slot has a member behind it
   and fdps_build_map_unit_array writes no zeroed, retired spare anywhere in
   the array (src/deploy.c).

   THE CUT-SCENE SWITCHES NO MAP AND DEPLOYS NO WAVE, the same as chapters 26's
   and 27's and unlike chapter 25's.  Walked with the opcode ladder in
   src/icon.c, ICON27.DAT's 267 bytes hold thirty-one opcodes and not one of
   them is SWITCH_MAP or DEPLOY_WAVE: the whole scene plays on the board this
   handler's own reset built, and the chapter id is never written by anything
   but the dispatcher that reached this slot.

   THE CHAPTER IS FOUGHT WITH THE WHOLE PARTY, which is what makes this member
   unlike the three before it.  ICON27.DAT carries no RETIRE_UNIT, no
   REVIVE_UNIT and no BLINK_UNITS_OUT at all, so every one of the twelve player
   slots the reset filled is still on the board when the handler returns and
   each carries a clear flags byte.  Chapters 25, 26 and 27 each left 法蓮娜
   off; nothing here does, and the strategy guide's entry for this chapter
   accordingly has no 己方 line of its own.

   WHAT THE CUT-SCENE DOES INSTEAD IS PLACE THE WHOLE PARTY.  Its twelve
   PLACE_UNITs at script offsets 7 to 62 put map units 0 to 11 on tiles of the
   script's own choosing -- (11, 24) for unit 0 -- so MAP27.COD's start records
   for the player slots are overwritten before a frame is drawn, and the nine
   group WALKs at offsets 87 to 229 march the party up the map from there.

   THE OPENING BOARD IS THE MAP'S WAVE 0 AND NOTHING ELSE.  MAP27.DAT is a
   131-byte header and sixty-five 26-byte deployment records -- the wave tag is
   byte 21 of struct fdps_char_spawn_record -- and thirty-seven of them are
   wave 0, which the rebuild puts down behind the twelve player slots for
   forty-nine map units in all.  Those thirty-seven are exactly the guide's
   敵方 list for the chapter: LV25 黑暗祭司 x4 (character 104), LV25 幽魂 x9
   (105), LV25 骷髏兵 x10 (84) and LV25 地獄犬 x14 (107).  Record 0 of the file
   is wave 0 and a 骷髏兵, so it becomes map unit 12, the first unit behind the
   party.  The twenty-eight records held back carry wave tags 1 to 9 and one
   record tagged 0xff; the nine waves are three units each and are the guide's
   事件, the reinforcement that arrives at the end of the player's 2nd, 4th,
   6th, 7th, 10th, 12th, 14th, 16th and 18th turns, and no part of what this
   handler does.

   THE CURSOR ENDS ON A TILE THE CUT-SCENE CHOSE OUTRIGHT.  Unit 0 is placed on
   (11, 24) at script offset 7 and then walked eight tiles: five up in the four
   walks at offsets 87, 103, 119 and 135, one right at offset 155 (facing 3,
   which src/icon.c's ladder makes a step right), and two more up at offsets
   173 and 195.  So unit 0 is on (12, 17) when the cursor call reads it and the
   cursor lands on (288, 408).  Nothing of MAP27.COD's own start tile for
   player slot 0, (8, 18), survives into that number. */
void fdps_chapter_28_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH28_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH28_CURSOR_UNIT);
}

/* --- fdps_chapter_29_init @ 000215d0 ----------------------------------- */

/* Chapter 29's opening cut-scene, the string at 0x61954 loaded into EAX at
   000215e1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon28.dat where the slot-27 handler at 00021590 holds Icon27.dat at
   0x61948 and the slot-29 handler at 00021610 holds Icon29.dat at 0x61960 --
   the three sit next to each other in the image, twelve bytes apart, each an
   eleven-byte member name and a filler byte.  Spelling the member out of the
   handler's own chapter number gives Icon29.dat, which loads, runs, and plays
   chapter 30's opening scene under chapter 29's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON28.DAT by the
   call and cannot live in read-only storage. */
#define CH29_OPENING_SCRIPT "Icon28.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000215f4, roster slot 0 and so
   蘭迪斯, which is what twenty-seven of the thirty handlers pass. */
#define CH29_CURSOR_UNIT 0

/* 000215d0.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, instruction for instruction the same body as
   fdps_chapter_28_init at 00021590 and fdps_chapter_30_init at 00021610 apart
   from the script string.  At 0x33 bytes it is the shortest shape the thirty
   handlers come in.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 000215d0..000215d4 -- over SUB ESP,0x0 at 000215d6, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000215fe..00021601
   and the RET at 00021602 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61954 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000215e1..000215ec, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000215f4..000215fb.  The two argument-less calls at 000215dc and 000215ef
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never read after
   any of the four calls, so it returns nothing -- the dispatcher reaches it
   through the twenty-ninth slot of the table at 00060074 (the dword at
   000600e4 is 000215d0, and that data reference is the function's only xref)
   and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER either.  The first instruction after the
   prologue is the state rebuild, so the roster is left exactly as chapter 24
   finished it -- the eleven chapters 1 to 19 assembled plus 珊 -- and
   MAP28.DAT asks for twelve player slots, so every slot has a member behind it
   and fdps_build_map_unit_array writes no zeroed, retired spare anywhere in
   the array (src/deploy.c).

   THE CUT-SCENE SWITCHES NO MAP AND DEPLOYS NO WAVE, the same as chapters
   26's, 27's and 28's and unlike chapter 25's.  Walked with the opcode ladder
   in src/icon.c, ICON28.DAT's 721 bytes hold eighty-nine opcodes and not one
   of them is SWITCH_MAP or DEPLOY_WAVE: the whole scene plays on the board
   this handler's own reset built, and the chapter id is never written by
   anything but the dispatcher that reached this slot.

   THE CHAPTER IS FOUGHT WITH THE WHOLE PARTY, as chapter 28 is, but this
   member reaches that end by a route chapter 28's has not got.  ICON28.DAT
   carries no RETIRE_UNIT at all; what it does carry is two BLINK_UNITS_OUT, at
   script offsets 4 and 668, and both name the same pair of units, 90 and 91,
   which are the map's last two deployment records and so the chapter's two
   守護魔龍.  A blink leaves its units with the retired bit set -- the phase
   counter's last value is 7 and the handler stores its low bit outright
   (src/icon.c) -- and each blink here is undone by a pair of REVIVE_UNITs, at
   offsets 664 and 666 and again at 672 and 674.  So every one of the
   ninety-two units on the board, the twelve player slots included, carries a
   clear flags byte when the handler returns, and the strategy guide's entry
   for this chapter accordingly has no 己方 line of its own.

   WHAT THE CUT-SCENE DOES WITH THE PARTY IS PLACE IT AND MARCH IT.  Its twelve
   PLACE_UNITs at script offsets 8 to 63 put map units 0 to 11 on one tile,
   (5, 24), so MAP28.COD's start records for the player slots are overwritten
   before a frame is drawn, and five group WALKs at offsets 73 to 145 carry a
   shrinking list of them up the map, each walk one tile, followed by a sixth
   at offset 158 that steps six of the party one tile left.

   THE OPENING BOARD IS THE WHOLE MAP FILE.  MAP28.DAT is a 131-byte header and
   eighty 26-byte deployment records -- the wave tag is byte 21 of struct
   fdps_char_spawn_record -- and every one of the eighty is tagged wave 0, so
   the reset puts all of them down behind the twelve player slots for
   ninety-two map units in all.  Those eighty are exactly the guide's 敵方 list
   for the chapter: LV27 骷髏兵 x34 (character 84), LV27 地獄犬 x25 (107), LV27
   幽魂 x19 (105) and LV30 守護魔龍 x2 (112).  Record 0 of the file is a
   骷髏兵, so it becomes map unit 12, the first unit behind the party, and the
   two 守護魔龍 are the file's last two records and so map units 90 and 91 --
   which is what the cut-scene's blinks name.  The map holds nothing back at
   all: this chapter's 事件 line in the guide is the moment the standing enemy
   groups start moving, not a reinforcement that arrives, so there is no later
   wave for this handler to have deployed or left alone.

   THE CURSOR ENDS ON A TILE THE CUT-SCENE CHOSE OUTRIGHT.  Unit 0 is placed on
   (5, 24) at script offset 8 and then walked five tiles up, one in each of the
   group walks at offsets 73, 97, 117, 133 and 145 (facing 2, which src/icon.c's
   ladder makes a step up); the walk at offset 158 does not list it.  So unit 0
   is on (5, 19) when the cursor call reads it and the cursor lands on
   (120, 456).  Nothing of MAP28.COD's own start tile for player slot 0,
   (4, 19), survives into that number. */
void fdps_chapter_29_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH29_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH29_CURSOR_UNIT);
}

/* --- fdps_chapter_30_init @ 00021610 ----------------------------------- */

/* Chapter 30's opening cut-scene, the string at 0x61960 loaded into EAX at
   00021621 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon29.dat where the slot-28 handler at 000215d0 holds Icon28.dat at
   0x61954 -- the two sit twelve bytes apart in the image, each an eleven-byte
   member name and a filler byte, and there is no member above this one.
   Spelling the member out of the handler's own chapter number gives
   Icon30.dat, which ICONANI.VFS does not hold at all: the container runs
   ICON00.DAT through ICON29.DAT and stops (resource_info/vfs.md).  A member
   the interpreter cannot find is not a quiet miss either -- it stops in
   fdps_wait_any_key and the last chapter of the game opens on a keypress the
   player was never asked for, with no cut-scene, no wave-1 boss on the board
   and the cell event never triggered (src/icon.c).

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON29.DAT by the
   call and cannot live in read-only storage. */
#define CH30_OPENING_SCRIPT "Icon29.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00021634, roster slot 0 and so
   蘭迪斯, which is what twenty-seven of the thirty handlers pass. */
#define CH30_CURSOR_UNIT 0

/* 00021610.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, instruction for instruction the same body as
   fdps_chapter_28_init at 00021590 and fdps_chapter_29_init at 000215d0 apart
   from the script string.  At 0x33 bytes it is the shortest shape the thirty
   handlers come in, and it is the last of them: there is no chapter 31.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021610..00021614 -- over SUB ESP,0x0 at 00021616, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002163e..00021641
   and the RET at 00021642 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61960 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021621..0002162c, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021634..0002163b.  The two argument-less calls at 0002161c and 0002162f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never read after
   any of the four calls, so it returns nothing -- the dispatcher reaches it
   through the thirtieth and last slot of the table at 00060074 (the dword at
   000600e8 is 00021610, and that data reference is the function's only xref)
   and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER either.  The first instruction after the
   prologue is the state rebuild, so the roster is left exactly as chapter 24
   finished it -- the eleven chapters 1 to 19 assembled plus 珊 -- and
   MAP29.DAT asks for twelve player slots, so every slot has a member behind it
   and fdps_build_map_unit_array writes no zeroed, retired spare anywhere in
   the array (src/deploy.c).

   THE HANDLER'S OWN RESET PUTS NOTHING ON THE BOARD BUT THE PARTY.  MAP29.DAT
   is a 131-byte header and seven 26-byte deployment records -- the wave tag is
   byte 21 of struct fdps_char_spawn_record -- and not one of the seven is
   tagged wave 0.  Six other maps open empty as well -- MAP00, MAP01, MAP06,
   MAP11, MAP13 and the cut-scene stage MAP34 -- but this is the only one of
   the six this file's handlers load, so where chapters 25 to 29 return on a
   board the reset filled, here the twelve player slots are the whole of it and
   everything the player sees opposite them is put there by a script.

   THE CUT-SCENE IS WHAT DEPLOYS THE BOSS.  ICON29.DAT's 2,977 bytes hold 478
   opcodes when walked with the opcode ladder in src/icon.c, and two of them
   are DEPLOY_WAVE, at script offsets 435 and 2938, both reading `04 01 01` --
   wave 1, placed on the tile its placement record names rather than on a
   searched-for one.  MAP29.DAT's wave 1 is a single record, record 0: the LV40
   平衡之神, character 60, whose ENEMYDAT.DAT row 0 carries 150 HP a level and
   so the guide's HP6000 (assets/characters.md).  It lands at map unit 12, the
   first index behind the twelve player slots, on MAP29.COD's record 0, (10, 3).
   The three records the map tags waves 2 and 3 are the god's second and third
   forms, characters 61 and 62, and the four it tags wave 4 are the endless
   reinforcement pair the guide's 事件 line describes, LV20 死靈 x2 (character
   106) and LV20 白骨戰士 x2 (85); none of the seven but record 0 is on the
   board when this handler returns.

   THE SECOND DEPLOY IS BEHIND A MAP SWITCH THAT CHANGES NOTHING.  The
   SWITCH_MAP at script offset 2936 writes chapter id 0x1d -- 29, the id the
   dispatcher already put there -- and resets the state again, so it rebuilds
   the very board the handler's own reset built and throws away everything the
   scene did to it: the four RETIRE_UNITs and four REVIVE_UNITs that flash the
   god in and out, the four PLACE_UNITs that move it, and the first DEPLOY_WAVE
   with it.  The deploy at 2938 puts wave 1 back.  So the board at the return
   is thirteen units however the scene is read, and the chapter id still reads
   29 because the only thing that ever wrote it wrote what was already there.

   WHAT THE SCENE LEAVES BEHIND IT IS A CELL EVENT AND A FACING.  The
   TRIGGER_CELL_EVENT at offset 2941 sets triggered flag 0 to 1 and applies the
   map cell changes that follow from it, and that flag survives because the
   reset that clears the flag table is now behind it; the FACE_UNITS at 2947
   turns all twelve party members to facing 2, over the 0 the array build wrote
   (src/deploy.c).  The 398 SET_MAP_CELLs, twelve view shakes and four text
   draws in between are the scene itself and leave no state this handler's
   caller can read.

   THE CURSOR ENDS ON THE MAP'S OWN TILE, unlike chapters 25's and 29's.  All
   four of ICON29.DAT's PLACE_UNITs name map unit 12, the god, and no walk in
   the member lists a party member at all, so player slot 0 keeps the start
   tile MAP29.COD gives it -- the record at 0xb + (7 + 0) * 6, read as two
   signed words (src/deploy.c), which is (7, 15) -- and the cursor lands on
   (168, 360). */
void fdps_chapter_30_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH30_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH30_CURSOR_UNIT);
}
