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
#include "roster.h"
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

/* --- fdps_chapter_17_init @ 000212d0 ----------------------------------- */

/* Chapter 17's opening cut-scene, the string at 0x618c4 loaded into EAX at
   000212e1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id, so chapter 17's member is Icon16.dat,
   the one after this file's own Icon15.dat at 0x618b8 -- the two literals sit
   next to each other in the image with a single 0xff between them.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON16.DAT by the
   call and cannot live in read-only storage. */
#define CH17_OPENING_SCRIPT "Icon16.dat"

/* Which unit the cursor is left on: PUSH 0x3 at 000212f4, not the PUSH 0x0
   twenty-seven of the thirty handlers make.  Unit slot 3 is roster slot 3, and
   the roster is in join order, so it is 法蓮娜 -- the one member chapter 17 is
   lost without.  Only this handler, fdps_chapter_22_init and
   fdps_chapter_23_init pass 3, and those are the three chapters that turn on
   her. */
#define CH17_CURSOR_UNIT 3

/* 000212d0.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_16_init above it, with no fdps_roster_add_character in front of
   them and no store behind them.  At 0x33 bytes it is the shortest length the
   family has, the same as chapter 16's.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 000212d0..000212d4 -- over SUB ESP,0x0 at 000212d6, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000212fe..00021301
   and the RET at 00021302 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x618c4 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000212e1..000212ec, and PUSH 0x3 / CALL 0x0002da50 / ADD ESP,0x4 at
   000212f4..000212fb.  The two argument-less calls at 000212dc and 000212ef
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   last call, so it returns nothing -- the dispatcher reaches it through the
   seventeenth slot of the table at 00060074 (the dword at 000600b4 is
   000212d0, and that data reference is the function's only xref) and ignores
   EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, AND EVERY SLOT IS FILLED.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 16 finished it -- the same ten members chapter 16 opened
   with, 瑪麗安 from chapter 15 being the last to join.  MAP16.DAT declares TEN
   player slots -- byte +1 of the block (src/rsrc.c) -- so every slot has a
   member behind it and fdps_build_map_unit_array writes no zeroed, retired
   spare anywhere in the array (src/deploy.c).

   THE HALF OF THE PARTY CHAPTER 16 FOUGHT WITH IS THE HALF THIS CHAPTER SITS
   OUT, AND AGAIN IT IS THE CUT-SCENE THAT DECIDES IT.  Walked with the opcode
   ladder in src/icon.c, the shipped ICON16.DAT's 154 bytes hold five RETIREs
   in a row at script offsets 32 to 41 -- map units 0, 1, 2, 5 and 6 -- and no
   REVIVE anywhere, which is the exact complement of the five ICON15.DAT
   retires.  Map unit i is roster slot i and the roster is in join order, so
   the five taken off are 蘭迪斯, 尤利安, 亞克, 費塔加 and 布蘭多 and the five
   left standing are 法蓮娜, 裘娜, 蓋亞, 琴琴 and 瑪麗安, which is the strategy
   guide's 己方 line for the chapter exactly.

   THE CUT-SCENE PUTS NOBODY ON THE MAP.  ICON16.DAT carries no DEPLOY_WAVE at
   all, where chapter 16's member carries four, so the units on the board when
   the handler returns are the reset's own opening deploy and nothing else: MAP16.DAT holds thirty deployment records and tags
   twenty-three of them wave 0, which behind the ten player slots makes
   thirty-three.  Twenty of those twenty-three are enemy-side and they are the
   strategy guide's opening 敵方 list to the number, by character id and level:
   LV16 暗魔導士 x2 is character 103, LV11 騎士 x6 is 89, LV15 武士 x10 is 99
   and LV14 弓箭手 x2 is 94.  The map's remaining seven records are the two
   the chapter's own events bring in later -- six more LV15 武士 tagged wave 1,
   the guide's 第八回合 reinforcement, and the LV14 狼人 tagged wave 2 that
   comes for the 強化套件 on the ninth -- and neither is down yet.

   THE OTHER THREE ARE GUESTS, AND THEY ARE THE HOSTAGES THE CHAPTER IS NAMED
   FOR.  MAP16.DAT's records 18, 19 and 20 carry side 1, the guest side that
   fdps_collect_targets_in_area selects with mode 2 and that the ambush gates
   in src/chevt4.c refuse (src/aitarget.c), and they are characters 12 and 14
   at level 10 and character 91 at level 20.  The first two are FRIAPRDA.DAT
   template rows rather than named party members (assets/characters.md), so
   they are built through the roster-side branch of the deploy with a template
   stat line.

   THE CUT-SCENE WALKS UNIT 3, SO THE CURSOR CALL IS NOT WHERE THE MAP PUT IT.
   The member places map units 3, 4, 7, 8 and 9 on (18, 18) at script offsets 7
   to 31, moves the whole group to (1, 6) at offsets 84 to 108, and then walks
   unit 3 one tile right at offset 122 and one tile down at offset 134, which
   leaves it on (2, 7) facing down.  The cursor call is the last of the four.

   NOTHING IS WRITTEN ON A UNIT'S STATUS.  The body has no store in it at all,
   and the cut-scene adds no status either: walked with the same ladder the
   shipped ICON16.DAT holds no SET_UNIT_TIMER anywhere.  The guide lists no
   status on anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and this member carries no SWITCH_MAP to disturb
   it.  The Icon16.dat above is the one place the chapter number is spelled out
   rather than read. */
void fdps_chapter_17_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH17_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH17_CURSOR_UNIT);
}

/* --- fdps_chapter_18_init @ 00021310 ----------------------------------- */

/* Chapter 18's opening cut-scene, the string at 0x618d0 loaded into EAX at
   00021321 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id, so chapter 18's member is Icon17.dat,
   the one after chapter 17's Icon16.dat at 0x618c4 -- the literals sit end to
   end in the image, "Icon17.dat" at 0x618d0 and "Icon18.dat" at 0x618dc.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON17.DAT by the
   call and cannot live in read-only storage. */
#define CH18_OPENING_SCRIPT "Icon17.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00021334, the same unit
   twenty-seven of the thirty handlers name. */
#define CH18_CURSOR_UNIT 0

/* 00021310.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_16_init and fdps_chapter_17_init above it, with no
   fdps_roster_add_character in front of them and no store behind them.  At
   0x33 bytes it is the shortest length the family has, the same as those two.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021310..00021314 -- over SUB ESP,0x0 at 00021316, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002133e..00021341
   and the RET at 00021342 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x618d0 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021321..0002132c, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021334..0002133b.  The two argument-less calls at 0002131c and 0002132f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   last call, so it returns nothing -- the dispatcher reaches it through the
   eighteenth slot of the table at 00060074 (the dword at 000600b8 is
   00021310, and that data reference is the function's only xref) and ignores
   EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, AND EVERY SLOT IS FILLED.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 17 finished it -- the same ten members chapters 16 and 17
   opened with, 瑪麗安 from chapter 15 being the last to join.  MAP17.DAT
   declares TEN player slots -- byte +1 of the block (src/rsrc.c) -- so every
   slot has a member behind it and fdps_build_map_unit_array writes no zeroed,
   retired spare anywhere in the array (src/deploy.c).

   THE WHOLE PARTY FIGHTS THIS CHAPTER, THOUGH THE CUT-SCENE TAKES HALF OF IT
   OFF AND PUTS IT BACK.  Walked with the opcode ladder in src/icon.c, the
   shipped ICON17.DAT's 774 bytes retire map units 3, 4, 7, 8 and 9 at script
   offsets 37 to 45 -- the same five chapter 16's member retires -- and then
   REVIVE all five at offsets 429 to 437, which is what separates this member
   from chapters 16's and 17's: those two carry no REVIVE at all.  So all ten
   player slots carry a clear flags byte when the handler returns, and the
   guide's 己方 line for the chapter names nobody sitting out.

   THE THREE UNITS THE CUT-SCENE ACTS WITH ARE OFF THE BOARD AGAIN BY THE
   RETURN.  MAP17.DAT tags exactly two of its sixty-five deployment records
   wave 0 -- record 31, character 12 at level 10 on side 1, the guest side
   (src/aitarget.c), and record 33, character 117 at level 5 -- so the reset's
   own opening deploy makes them map units 10 and 11.  The member then retires
   both at offsets 386 and 388, revives unit 10 at 392, deploys wave 2 exactly
   at 394 -- MAP17.DAT's single wave-2 record 32, character 67 at level 5,
   which lands as map unit 12 -- and retires unit 12 at 676 and unit 10 again
   at 735.  All three are retired when the member ends, so the units in play
   are the ten player slots and the enemy group below.

   THE OPPOSITION IS THE CUT-SCENE'S OWN WAVE, NOT THE MAP'S OPENING DEPLOY.
   MAP17.DAT tags nothing but those two records wave 0, so the reset puts no
   enemy down at all; it is the member's closing DEPLOY_WAVE at offset 753 --
   wave 1, with the place operand 0, the nearest-free-tile search rather than
   the exact anchor (src/deploy.c) -- that brings the fifteen wave-1 records
   in.  Those fifteen are the strategy guide's opening 敵方 list for the
   chapter to the number, by character id and level: LV14 衛兵 x5 is character
   90, LV15 弓箭手 x2 is 94, LV13 騎士 x5 is 89, LV14 暗黑騎士 x2 is 77 and
   LV16 暗魔導士 x1 is 103.  The map's other forty-eight records belong to the
   chapter's own turn events -- the four flights of four LV15 飛兵 (character
   96) on waves 4, 6, 8 and 10, the four squads of four LV13 騎士 on waves 5,
   7, 9 and 11, and the fifteen-record wave 13 the guide's 第十三回合 gives as
   the reinforcement that repeats the opening group -- and none of them is down
   yet.

   THE CUT-SCENE WALKS UNIT 0, SO THE CURSOR CALL IS NOT WHERE THE MAP PUT IT.
   MAP17.COD gives every one of the ten player slots the same start tile,
   (3, 18) -- records 65 to 74, the first records past the map's sixty-five
   scripted ones -- and the member places map unit 0 on (17, 24) at offset 108
   and then walks it five tiles up, three at offset 126 and one each at 140 and
   154, which leaves it on (17, 19) facing up.  The cursor call is the last of
   the four.

   NOTHING IS WRITTEN ON A UNIT'S STATUS.  The body has no store in it at all,
   and the cut-scene adds no status either: walked with the same ladder the
   shipped ICON17.DAT holds no SET_UNIT_TIMER anywhere.  The guide lists no
   status on anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and this member carries no SWITCH_MAP to disturb
   it.  The Icon17.dat above is the one place the chapter number is spelled out
   rather than read. */
void fdps_chapter_18_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH18_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH18_CURSOR_UNIT);
}

/* --- fdps_chapter_19_init @ 00021350 ----------------------------------- */

/* The character who joins the party at the start of chapter 19: 蘭斯洛特 the
   聖騎士, character id 11 (assets/characters.md).  PUSH 0xb at 0002135c.  He
   lands at roster slot 10, behind the ten members chapters 16 to 18 were
   fought with, because the roster is in join order and is never permuted.

   THE RECORD THE ADD BUILDS IS NOT THE UNIT THE PLAYER SEES ARRIVE, and this
   is the widest the two have been apart in the family.
   fdps_roster_add_character reads FRIAPRDA.DAT row 11 and FRILEVUP.DAT row 11,
   which is level 15 on 420 base HP and 0 base MP with 14 HP and 0 MP a level,
   so the roster record is LV15 at 616 HP and 0 MP carrying 修羅之矛 and 重鎧甲
   -- item ids 0x2d and 0x69 (assets/items.md).  MAP18.DAT's own wave-1 record
   is a different line: side 2, character 11, level 2, items 0x29 and 0x6a,
   which is the strategy guide's 己方 line for the chapter, LV2 聖騎士 蘭斯洛特
   with 破陣之矛 and 精鋼鎧甲, and it is fdps_chapter_19_event_lancelot_joins
   that deploys it on turn 6.

   THE GUIDE RECORDS THE DIFFERENCE FROM THE PLAYER'S SIDE.  Its 備註 for the
   chapter says that finishing before the arrival event fires still leaves
   蘭斯洛特 in the party, but at LV15 with 616 HP, 0 MP, 修羅之矛 and 重鎧甲 --
   which is this add's record read back, number for number.  So the add is
   observable on its own, and moving it into the arrival event would lose the
   behaviour the guide is describing. */
#define CH19_JOINING_CHARACTER 11

/* Chapter 19's opening cut-scene, the string at 0x618dc loaded into EAX at
   0002136b and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id, so chapter 19's member is Icon18.dat,
   the one after chapter 18's Icon17.dat at 0x618d0 -- the literals sit end to
   end in the image.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON18.DAT by the
   call and cannot live in read-only storage. */
#define CH19_OPENING_SCRIPT "Icon18.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 0002137e, the same unit
   twenty-seven of the thirty handlers name. */
#define CH19_CURSOR_UNIT 0

/* 00021350.  Five calls, straight line, no branch, no loop and no local -- the
   plain four-call form of fdps_chapter_18_init above it with an
   fdps_roster_add_character put back in front of it, which is the shape
   chapters 2 to 4, 7, 8 and 11 have.  At 0x3d bytes it is the plain form's
   0x33 plus the ten bytes of the add.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021350..00021354 -- over SUB ESP,0x0 at 00021356, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 00021388..0002138b
   and the RET at 0002138c is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0xb / CALL 0x00023bc0 / ADD ESP,0x4 at 0002135c..00021363,
   MOV EAX,0x618dc / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0002136b..00021376, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   0002137e..00021385.  The two argument-less calls at 00021366 and 00021379
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   last call, so it returns nothing -- the dispatcher reaches it through the
   nineteenth slot of the table at 00060074 (the dword at 000600bc is 00021350,
   and that data reference is the function's only xref) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All five callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the third call.

   THE ADD RUNS BEFORE THE RESET, AND HERE THAT ORDER COSTS NOTHING.
   fdps_chapter_state_reset fills player slot i from roster slot i only while i
   is below the roster count (src/deploy.c), and MAP18.DAT declares TEN player
   slots -- byte +1 of the block (src/rsrc.c) -- against a roster this add has
   just made eleven.  蘭斯洛特 is roster slot 10, one past the last slot the map
   asks for, so he is not one of chapter 19's map units whichever way round the
   first two calls run; the ten that are behind those slots are the same ten
   chapters 16 to 18 were fought with, every slot filled and no zeroed, retired
   spare written anywhere in the array.  The order is kept because it is the
   original's and not because this chapter shows it.

   HALF THE OPENING BOARD IS THE MAP'S AND HALF IS THE CUT-SCENE'S.  MAP18.DAT
   holds sixty-eight deployment records and tags five of them wave 0 -- LV15
   暗黑騎士, character 77 -- so the reset's own opening deploy appends those
   five behind the ten player slots as map units 10 to 14.  Walked with the
   opcode ladder in src/icon.c, the shipped ICON18.DAT's 231 bytes hold one
   DEPLOY_WAVE, at script offset 196: wave 5 with the place operand 0, the
   nearest-free-tile search rather than the exact anchor (src/deploy.c), which
   is thirty-nine more records.  The handler therefore returns with fifty-four
   units on the map.

   THOSE FORTY-FOUR ARE THE GUIDE'S OPENING 敵方 LIST TO THE NUMBER, by
   character id and level: LV17 弓箭手 x14 is character 94, LV18 暗魔導士 x7 is
   103, LV17 飛兵 x6 is 96, LV17 野蠻戰士 x5 is 80, and LV15 暗黑騎士 x12 is 77
   -- five of those twelve on wave 0 and seven on wave 5.  The map's remaining
   twenty-four records are the chapter's own events and none of them is down
   yet: wave 1 is the single 蘭斯洛特 unit the turn-6 arrival deploys, wave 2 is
   the LV18 character 35 the guide prints as ？？？？, the challenger who comes
   for 裘娜, and wave 6 is the reinforcement of fourteen LV17 飛兵 and eight
   LV15 武鬥家 the guide describes as arriving on the left, right and top.

   NOBODY IS TAKEN OFF THE BOARD, AND NOTHING IS WRITTEN ON A UNIT'S STATUS.
   The body has no store in it at all, and ICON18.DAT carries no RETIRE_UNIT,
   no REVIVE_UNIT and no SET_UNIT_TIMER anywhere in its 231 bytes, so all ten
   player slots carry a clear flags byte and clear status timers when the
   handler returns.  The guide lists no status on anybody this chapter.

   THE CUT-SCENE WALKS UNIT 0, SO THE CURSOR CALL IS NOT WHERE THE MAP PUT IT.
   MAP18.COD gives the ten player slots ten different start tiles -- records 68
   to 77, the first records past the map's sixty-eight scripted ones -- and
   slot 0's is (12, 2).  Eight of the member's nine WALK_UNITS opcodes list map
   unit 0, and their facings come to one tile left and four tiles down net, which
   leaves it on (11, 6).  The cursor call is the last of the five.

   THE CHAPTER IS NOT SET HERE.  Both the script the third call loads and the
   title-card graphic the fourth one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and this member carries no SWITCH_MAP to disturb
   it.  The Icon18.dat above is the one place the chapter number is spelled out
   rather than read. */
void fdps_chapter_19_init(void)
{
    fdps_roster_add_character(CH19_JOINING_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH19_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH19_CURSOR_UNIT);
}
