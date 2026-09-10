/* chinit1b.c -- the per-chapter entry handlers, chapters 11 to 15: what the
 * game does at the moment it enters a chapter, before the battle loop runs.
 * Chapters 1 to 10 are chinit1.c and 16 to 30 are chinit2.c.
 *
 * These are slots of the handler table based at 00060074, indexed by the
 * 0-based chapter id and called only through it, so the dispatcher in
 * title.c is not a static caller of any of them.
 *
 * See chinit1b.h for what each handler sets up.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "roster.h"
#include "chapter.h"
#include "icon.h"
#include "mapcur.h"
#include "chinit1b.h"

/* --- fdps_chapter_11_init @ 00021140 ----------------------------------- */

/* The character who joins at the start of chapter 11: 琴琴 the 武道家,
   character id 7 (assets/characters.md).  PUSH 0x7 at 0002114c.  She lands at
   roster slot 8, behind the eight members chapters 1 to 4 and 7 to 9 have put
   on, because the roster is in join order and is never permuted -- chapter 10
   adds nobody, so the party is exactly eight when this handler runs. */
#define CH11_JOINING_CHARACTER 7

/* Chapter 11's opening cut-scene, the string at 0x6187c loaded into EAX at
   0002115b and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon10.dat where fdps_chapter_10_init holds Icon09.dat at the
   same position and fdps_chapter_12_init holds Icon11.dat.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON10.DAT by the
   call and cannot live in read-only storage. */
#define CH11_OPENING_SCRIPT "Icon10.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 0002116e. */
#define CH11_CURSOR_UNIT 0

/* 00021140.  Five calls, straight line, no branch, no loop and no local -- the
   same shape as chapters 2, 3 and 4, with a different character joining and a
   different script name.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021140..00021144 -- over SUB ESP,0x0 at 00021146, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 00021178..0002117b
   and the RET at 0002117c is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x7 / CALL 0x00023bc0 / ADD ESP,0x4 at 0002114c..00021153,
   MOV EAX,0x6187c / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0002115b..00021166, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   0002116e..00021175.  The two argument-less calls at 00021156 and 00021169
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set before
   the RET, so it returns nothing -- the dispatcher reaches it through the
   eleventh slot of the table at 00060074 (the dword at 0006009c is 00021140,
   and that data reference is the function's only xref) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  Nothing here reads EAX after any of
   the five; the only register written between the prologue and the RET is the
   EAX that carries the script name literal into the third call.  The
   interpreter does leave a result in EAX -- it is declared void in icon.h
   because no caller in the image looks at it -- and the ADD ESP,0x4 at
   00021166 is the stack cleanup, not a use of it.

   THE ORDER DECIDES WHETHER THE CHAPTER HAS 琴琴 IN IT.  MAP10.DAT declares
   NINE player slots -- byte +1 of the block (src/rsrc.c) -- and the party is
   eight members when the chapter opens, so slot 8 exists for exactly one
   person.  fdps_build_map_unit_array fills player slot i from roster slot i
   only while i is below the roster count and writes the zeroed, retired spare
   otherwise (src/deploy.c), so the add running BEFORE the reset is what makes
   slot 8 a live unit rather than that spare.  The shipped ICON10.DAT then
   spends most of its length on map unit 8 -- it walks her, poses her, retires
   her with the three stand-ins, revives her alone and re-places her on tile
   (11, 9) -- and the strategy guide's losing condition for the chapter is
   蘭迪斯 or 琴琴 dying.  Written the other way round the chapter opens
   without her.

   Her level is the roster add's own and not a deployment record's: the add
   builds her record from FRIAPRDA.DAT's level 15, 60 base HP and 12 base MP
   and FRILEVUP.DAT's 12 HP and 2 MP a level, for HP 228 and MP 40, which is
   the LV15 武道家琴琴（HP228,MP40）the strategy guide prints as this
   chapter's 己方 addition (assets/characters.md).

   THE CUT-SCENE DEPLOYS THE WHOLE OPPOSITION, AND SWITCHES NO MAP.  Walked
   with the opcode ladder in src/icon.c, the shipped ICON10.DAT's 173 bytes
   hold no SWITCH_MAP at all and three DEPLOY_WAVEs -- wave 1, then wave 2,
   then wave 3, each with the place-exact operand 0 -- which on MAP10.DAT is
   7, 3 and 4 of its 49 records.  The map itself tags only three records wave
   0, all of them the same level-1 character, and those are the stand-ins the
   script retires again part way through.  So the array the handler returns
   with is nine player slots, three wave-0 stand-ins and fourteen deployed
   enemies, twenty-six units, and those fourteen are the guide's opening 敵方
   group by character id and level: LV15 魔導士 x3, LV15 冰魔導士 x2, LV14
   野武士, LV13 狼人 x2, LV13 拳士 x3 and LV13 弓兵 x3.  The map's remaining
   thirty-two records are its waves 4 and 5, the reinforcement lines the guide
   lists against later turns, which no opcode here deploys.

   NOTHING IS WRITTEN ON A UNIT.  The body has no store in it at all, as
   chapters 2 to 10 have none, and the cut-scene adds no status either: walked
   with the same ladder the shipped ICON10.DAT holds no SET_UNIT_TIMER
   anywhere.  The guide lists no status on anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the third call loads and the
   title-card graphic the fourth one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and with no SWITCH_MAP in the script nothing
   disturbs it on the way.  The Icon10.dat above is the one place the chapter
   number is spelled out rather than read. */
void fdps_chapter_11_init(void)
{
    fdps_roster_add_character(CH11_JOINING_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH11_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH11_CURSOR_UNIT);
}

/* --- fdps_chapter_12_init @ 00021180 ----------------------------------- */

/* Chapter 12's opening cut-scene, the string at 0x61888 loaded into EAX at
   00021191 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon11.dat where fdps_chapter_11_init above holds Icon10.dat at
   the same position and fdps_chapter_13_init holds Icon12.dat.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON11.DAT by the
   call and cannot live in read-only storage. */
#define CH12_OPENING_SCRIPT "Icon11.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000211a4. */
#define CH12_CURSOR_UNIT 0

/* 00021180.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_10_init, with no fdps_roster_add_character in front of them and
   no store behind them.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021180..00021184 -- over SUB ESP,0x0 at 00021186, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000211ae..000211b1
   and the RET at 000211b2 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61888 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021191..0002119c, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000211a4..000211ab.  The two argument-less calls at 0002118c and 0002119f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   third call, so it returns nothing -- the dispatcher reaches it through the
   twelfth slot of the table at 00060074 (the dword at 000600a0 is 00021180,
   and that data reference is the function's only xref) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, AND NO SLOT IS LEFT OVER.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 11 finished it: the eight members chapters 1 to 4 and 7
   to 9 put on, and 琴琴, whom fdps_chapter_11_init added -- nine.  MAP11.DAT
   declares NINE player slots -- byte +1 of the block (src/rsrc.c) -- so every
   slot has a member behind it and fdps_build_map_unit_array writes no zeroed,
   retired spare anywhere in the array (src/deploy.c).

   THE CHAPTER'S ONE OPPONENT IS THE ANSWER THE PLAYER GIVES INSIDE THE
   CUT-SCENE.  Walked with the opcode ladder in src/icon.c, the shipped
   ICON11.DAT's 900 bytes hold three SWITCH_MAPs and four DEPLOY_WAVEs:
   SWITCH_MAP 0x34 at script offset 4 rebuilds on MAP52.DAT and SWITCH_MAP
   0x28 at offset 217 on MAP40.DAT -- cut-scene maps whose actors are level-2
   stand-ins -- and SWITCH_MAP 0x0b at offset 792 sets the chapter id back to
   11 and rebuilds map 11 from scratch.  The DEPLOY_WAVE at offset 802 is the
   last opcode of any weight in the script and its wave operand is 0xff, the
   form that takes the wave from the answer ASK_THREE_WAY left at offset 708
   rather than from the script (icon.h), so the wave is 1, 2 or 3.

   MAP11.DAT holds exactly three deployment records and tags them wave 1, 2 and
   3 -- character 113, 114 and 115, all level 20 -- and tags nothing wave 0, so
   the reset's own opening deploy puts nobody down and the array the handler
   returns with is nine player slots and the single opponent that answer chose,
   ten units.  Those three are the strategy guide's three rooms of 火神的宮殿:
   左邊房間 LV20 修佩魯, 中間房間 LV20 雷德 and 右邊房間 LV20 亞德尼恩, one
   of them and no more, which is why the guide prints three separate maps for
   this chapter and one 敵方 line against each.

   NOTHING IS WRITTEN ON A UNIT.  The body has no store in it at all, and the
   cut-scene adds no status either: walked with the same ladder the shipped
   ICON11.DAT holds no SET_UNIT_TIMER anywhere.  The guide lists no status on
   anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and the cut-scene's own run of switches leaves it
   as it found it.  The Icon11.dat above is the one place the chapter number is
   spelled out rather than read. */
void fdps_chapter_12_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH12_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH12_CURSOR_UNIT);
}

/* --- fdps_chapter_13_init @ 000211c0 ----------------------------------- */

/* Chapter 13's opening cut-scene, the string at 0x61894 loaded into EAX at
   000211d1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon12.dat where fdps_chapter_12_init above holds Icon11.dat at
   the same position and fdps_chapter_14_init holds Icon13.dat.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON12.DAT by the
   call and cannot live in read-only storage. */
#define CH13_OPENING_SCRIPT "Icon12.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000211e4. */
#define CH13_CURSOR_UNIT 0

/* 000211c0.  Four calls, straight line, no branch, no loop and no local -- the
   same plain form as fdps_chapter_12_init above, with no
   fdps_roster_add_character in front of the rebuild and no store behind it.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 000211c0..000211c4 -- over SUB ESP,0x0 at 000211c6, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000211ee..000211f1
   and the RET at 000211f2 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61894 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000211d1..000211dc, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000211e4..000211eb.  The two argument-less calls at 000211cc and 000211df
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   third call, so it returns nothing -- the dispatcher reaches it through the
   thirteenth slot of the table at 00060074 (the dword at 000600a4 is
   000211c0, and that data reference is the function's only xref) and ignores
   EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, AND NO SLOT IS LEFT OVER.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 12 finished it: the eight members chapters 1 to 4 and 7
   to 9 put on, and 琴琴, whom fdps_chapter_11_init added -- nine, because
   chapter 12 adds nobody either.  MAP12.DAT declares NINE player slots -- byte
   +1 of the block (src/rsrc.c) -- so every slot has a member behind it and
   fdps_build_map_unit_array writes no zeroed, retired spare anywhere in the
   array (src/deploy.c).

   THE WHOLE OPPOSITION IS DOWN BEFORE THE CUT-SCENE STARTS.  MAP12.DAT holds
   thirty-seven deployment records and tags EVERY ONE of them wave 0, so the
   reset's own opening deploy puts all thirty-seven on the map and the array is
   forty-six units before the script's first opcode.  Walked with the opcode
   ladder in src/icon.c, the shipped ICON12.DAT's 177 bytes carry no
   DEPLOY_WAVE and no SWITCH_MAP at all: it is a staging scene that poses the
   party, retires and revives three actors around a .saf clip and re-places
   them, and it deploys nobody and leaves the chapter id alone.

   ONE OF THE THIRTY-SEVEN IS AN ACTOR AND IS RETIRED BEFORE THE PLAYER SEES
   THE MAP.  Record 20 is character 13 at level 15 on the enemy side, and the
   script's RETIRE at script offset 125 names map unit 29, which is that record
   -- the nine player slots come first, so map unit 9 + n is record n.  It is
   the one RETIRE in the member with no REVIVE behind it.  That leaves thirty-six live opponents, and thirty-six is exactly
   what the strategy guide's 敵方 list for the chapter comes to.  Matching the
   guide's printed HP, DX and MV against ENEMYDAT.DAT row by row gives the
   character id behind every line of it: LV20 薩達特 is character 68, LV20 巴魯
   is 69 and LV20 席拉 is 70 -- records 0, 1 and 2, which is NOT the order the
   guide prints them in -- with LV15 暗黑騎兵 x9 (character 76), LV11 弓箭手 x7
   (94), LV16 拳士 x7 (108), LV13 騎士 x4 (89), LV13 暗魔導士 x4 (103) and LV17
   騎兵 x2 (88).

   THE CUT-SCENE MOVES THE PARTY, SO THE CURSOR CALL IS NOT WHERE THE MAP PUT
   UNIT 0.  MAP12.COD record 37 -- the first record past the map's
   thirty-seven scripted ones, and so player slot 0's start tile -- is
   (17, 16), but the script's first PLACE_UNIT at offset 10 moves unit 0 to
   (4, 19), and the cursor call is the last of the four.

   NOTHING IS WRITTEN ON A UNIT'S STATUS.  The body has no store in it at all,
   and the cut-scene adds no status either: walked with the same ladder the
   shipped ICON12.DAT holds no SET_UNIT_TIMER anywhere.  The guide lists no
   status on anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and this member carries no SWITCH_MAP to disturb
   it.  The Icon12.dat above is the one place the chapter number is spelled out
   rather than read. */
void fdps_chapter_13_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH13_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH13_CURSOR_UNIT);
}

/* --- fdps_chapter_14_init @ 00021200 ----------------------------------- */

/* Chapter 14's opening cut-scene, the string at 0x618a0 loaded into EAX at
   00021211 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon13.dat where fdps_chapter_13_init above holds Icon12.dat at
   the same position and fdps_chapter_15_init holds Icon14.dat.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON13.DAT by the
   call and cannot live in read-only storage. */
#define CH14_OPENING_SCRIPT "Icon13.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00021224. */
#define CH14_CURSOR_UNIT 0

/* 00021200.  Four calls, straight line, no branch, no loop and no local -- the
   same plain form as fdps_chapter_12_init and fdps_chapter_13_init above, with
   no fdps_roster_add_character in front of the rebuild and no store behind it.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021200..00021204 -- over SUB ESP,0x0 at 00021206, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002122e..00021231
   and the RET at 00021232 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x618a0 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021211..0002121c, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021224..0002122b.  The two argument-less calls at 0002120c and 0002121f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   fourth call, so it returns nothing -- the dispatcher reaches it through the
   fourteenth slot of the table at 00060074 (the dword at 000600a8 is
   00021200, and that data reference is the function's only xref) and ignores
   EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, AND NO SLOT IS LEFT OVER.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 13 finished it: the eight members chapters 1 to 4 and 7
   to 9 put on, and 琴琴, whom fdps_chapter_11_init added -- nine, because
   neither chapter 12's handler nor chapter 13's adds anybody either.
   MAP13.DAT declares NINE player slots -- byte +1 of the block (src/rsrc.c) --
   so every slot has a member behind it and fdps_build_map_unit_array writes no
   zeroed, retired spare anywhere in the array (src/deploy.c).

   THE WHOLE OPENING OPPOSITION COMES OUT OF THE CUT-SCENE, AND THE
   REINFORCEMENTS DO NOT.  MAP13.DAT holds thirty-seven deployment records and
   tags NONE of them wave 0 -- eighteen are wave 1, two are wave 2 and
   seventeen are wave 4 -- so the reset's own opening deploy puts nobody down
   and the nine player slots are the whole array until the script runs.  Walked
   with the opcode ladder in src/icon.c, the shipped ICON13.DAT's 689 bytes
   carry no SWITCH_MAP and exactly two DEPLOY_WAVEs: wave 2 place-nearest at
   script offset 563 and wave 1 place-nearest at offset 663, which is twenty
   opponents and an array of twenty-nine units when the handler returns.

   Those twenty are the strategy guide's opening 敵方 group to the number, and
   matching the guide's printed HP, MP, DX and MV against ENEMYDAT.DAT row by
   row gives the character id behind every line of it: LV13 武士 x5 is
   character 99 (HP364, DX26, MV4), LV12 飛兵 x6 is character 96 (HP240, DX36,
   MV7), LV11 弓箭手 x5 is character 94 (HP253, DX44, MV4) -- the same 弓箭手
   id chapter 13 above pins at the same level -- and LV13 暗魔導士 x4 is
   character 103 (HP260, MP234, DX26, MV4), likewise chapter 13's.  The map's
   remaining seventeen records are its wave 4, which is the guide's own turn-six
   event: sixteen more LV13 武士 and the LV17 狼人 that comes for the treasure
   chests, character 82 at level 17 (HP306, DX17, MV4).  Nothing in this
   handler deploys wave 4.

   THE CUT-SCENE MOVES THE PARTY, SO THE CURSOR CALL IS NOT WHERE THE MAP PUT
   UNIT 0.  MAP13.COD record 37 -- the first record past the map's thirty-seven
   scripted ones, and so player slot 0's start tile -- is (20, 4), but the
   script's PLACE_UNIT at offset 603 moves unit 0 to (7, 9) and nothing after
   it touches unit 0's position, and the cursor call is the last of the four.

   NOTHING IS WRITTEN ON A UNIT.  The body has no store in it at all, and the
   cut-scene adds no status either: walked with the same ladder the shipped
   ICON13.DAT holds no SET_UNIT_TIMER and no RETIRE anywhere.  The guide lists
   no status on anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and this member carries no SWITCH_MAP to disturb
   it.  The Icon13.dat above is the one place the chapter number is spelled out
   rather than read. */
void fdps_chapter_14_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH14_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH14_CURSOR_UNIT);
}
