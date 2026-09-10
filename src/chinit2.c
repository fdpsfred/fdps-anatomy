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

/* --- fdps_chapter_20_init @ 00021390 ----------------------------------- */

/* Chapter 20's opening cut-scene, the string at 0x618e8 loaded into EAX at
   000213a1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon19.dat where fdps_chapter_19_init holds Icon18.dat at 0x618dc and
   fdps_chapter_21_init holds Icon20.dat at 0x618f4 -- the three sit next to
   each other in the image.  Spelling the member out of the handler's own
   chapter number gives Icon20.dat, which loads, runs, and plays chapter 21's
   opening scene under chapter 20's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON19.DAT by the
   call and cannot live in read-only storage. */
#define CH20_OPENING_SCRIPT "Icon19.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000213b4. */
#define CH20_CURSOR_UNIT 0

/* 00021390.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_16_init above, with no fdps_roster_add_character in front of
   them and no store behind them.  At 0x33 bytes it is the shortest shape the
   thirty handlers come in.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021390..00021394 -- over SUB ESP,0x0 at 00021396, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000213be..000213c1
   and the RET at 000213c2 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x618e8 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000213a1..000213ac, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000213b4..000213bb.  The two argument-less calls at 0002139c and 000213af
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   second call, so it returns nothing -- the dispatcher reaches it through the
   twentieth slot of the table at 00060074 (the dword at 000600c0 is 00021390,
   and that data reference is the function's only xref) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, AND EVERY SLOT IS FILLED.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 19 finished it: the ten chapters 1 to 15 assembled plus
   蘭斯洛特, whom fdps_chapter_19_init put on at roster slot 10 -- eleven.
   MAP19.DAT declares ELEVEN player slots -- byte +1 of the block (src/rsrc.c)
   -- and it is the first map in the game to ask for eleven, so every slot has
   a member behind it and fdps_build_map_unit_array writes no zeroed, retired
   spare anywhere in the array (src/deploy.c).  蘭斯洛特 is a map unit for the
   first time here: chapter 19 added him to a roster its own map asked only ten
   slots of.

   THE CUT-SCENE IS STAGED ON ANOTHER MAP AND ENDS BY REBUILDING THIS ONE.
   ICON19.DAT carries two SCRIPT_OP_SWITCH_MAPs.  The opcode at script offset 4
   writes chapter id 58 and rebuilds the state on MAP58.DAT, a 0-player-slot
   stage whose three wave-0 records are 蘭迪斯, 法蓮娜 and 費塔加 at level 2;
   the opcode at script offset 191 writes the id back to 19 and rebuilds the
   state on MAP19.DAT.  Everything the scene does to units in between -- its
   four RETIREs, its one REVIVE and the DEPLOY_WAVE at offset 167 that brings
   on MAP58.DAT's wave-1 actor -- happens to map-58 units and is thrown away by
   the second rebuild along with the map they stood on.

   Staging a chapter's opening scene this way is not rare: nine of the thirty
   scripts carry the opcode -- ICON00, ICON06 to ICON09, ICON11, ICON19,
   ICON24 and ICON29 -- but ICON19.DAT is the first of them since ICON11.DAT,
   so it is the first handler in this file whose script does it, and chapters
   16 to 19 above are all of the other kind.

   So the board this handler returns on is the second rebuild's and not the
   first's, and the two are identical: eleven player slots plus the
   fifty-four records MAP19.DAT tags wave 0, sixty-five units.  Those
   fifty-four are the strategy guide's 敵方 list for the chapter to the number
   -- LV17 蛇魔使 x23 (character 75), LV18 野蠻戰士 x13 (80), LV17 狼人戰士
   x10 (83) and LV28 冰魔導士 x8 (101).  The map's one remaining record, a
   level-17 character 67 on wave 1, belongs to the chapter's own turn events
   and is not down yet.

   NOBODY IS OFF THE BOARD AND NOTHING IS WRITTEN ON A UNIT'S STATUS.  The
   body has no store in it at all, and the closing rebuild rewrites all eleven
   player slots out of the roster with a clear flags byte and clear status
   timers, so the scene's RETIREs cannot reach the party the player commands.
   The guide lists no status on anybody at the chapter's start.

   THE CURSOR ENDS WHERE THE MAP PUT UNIT 0, NOT WHERE THE SCENE LEFT IT.
   Every WALK_UNITS and PLACE_UNIT in ICON19.DAT is before the second
   SWITCH_MAP -- the last walk is at script offset 179 and the opcode is at 191
   -- so unit 0 is rebuilt on MAP19.COD's player-slot-0 start tile, record 55,
   the first record past the map's fifty-five scripted ones, which is (5, 4).
   The four handlers above all end on a tile their cut-scene walked to, because
   none of their scripts switches map; this one is the first in the file that
   does not.

   THE CHAPTER IS SET, BUT NOT BY THIS FUNCTION.  Both the script the second
   call loads and the title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put 19 there.  The script takes it to 58 and back to 19 on its own, so the
   value the title card is drawn from is the one the handler was entered with,
   restored by the script rather than preserved by it.  The Icon19.dat above is
   the one place the chapter number is spelled out rather than read. */
void fdps_chapter_20_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH20_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH20_CURSOR_UNIT);
}

/* --- fdps_chapter_21_init @ 000213d0 ----------------------------------- */

/* Chapter 21's opening cut-scene, the string at 0x618f4 loaded into EAX at
   000213e1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon20.dat where fdps_chapter_20_init above holds Icon19.dat at 0x618e8
   and the slot-21 handler at 00021410 holds Icon21.dat at 0x61900 -- the three
   sit next to each other in the image, twelve bytes apart, each an eleven-byte
   member name and a filler byte.  Spelling the member
   out of the handler's own chapter number gives Icon21.dat, which loads, runs,
   and plays chapter 22's opening scene under chapter 21's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON20.DAT by the
   call and cannot live in read-only storage. */
#define CH21_OPENING_SCRIPT "Icon20.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000213f4. */
#define CH21_CURSOR_UNIT 0

/* 000213d0.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_20_init above, with no fdps_roster_add_character in front of
   them and no store behind them.  At 0x33 bytes it is the shortest shape the
   thirty handlers come in, and it is byte-for-byte the shape of its neighbour
   with two operands changed.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 000213d0..000213d4 -- over SUB ESP,0x0 at 000213d6, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000213fe..00021401
   and the RET at 00021402 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x618f4 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000213e1..000213ec, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000213f4..000213fb.  The two argument-less calls at 000213dc and 000213ef
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   second call, so it returns nothing -- the dispatcher reaches it through the
   twenty-first slot of the table at 00060074 (the dword at 000600c4 is
   000213d0, and that data reference is the function's only xref) and ignores
   EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER AND NOBODY LEAVES IT.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 20 finished it -- the eleven chapters 1 to 19 assembled,
   蘭斯洛特 last -- and MAP20.DAT asks for the same ELEVEN player slots
   MAP19.DAT did, so every slot has a member behind it and
   fdps_build_map_unit_array writes no zeroed, retired spare anywhere in the
   array (src/deploy.c).  法蓮娜 is still one of the eleven here: the guide
   notes she leaves the party, but that is after this chapter's three-battle
   run and no part of what this handler does.

   THE CUT-SCENE IS STAGED ON THE CHAPTER'S OWN MAP AND CHANGES NO UNIT STATE.
   ICON20.DAT carries no SWITCH_MAP, no RETIRE_UNIT, no REVIVE_UNIT, no
   DEPLOY_WAVE and no SET_UNIT_TIMER: thirty-nine of its forty-four opcodes are
   PLACE_UNIT, WALK_UNITS and FACE_UNITS over the eleven party units, and the
   other five are two view scrolls, two music changes and one page of chapter
   text.  So the board it returns on is the state rebuild's alone, and the only
   thing the script leaves behind is where the party is standing.

   The eleven player slots plus the thirty-eight records MAP20.DAT tags wave 0
   make forty-nine units.  Those thirty-eight are the first five lines of the
   strategy guide's 敵方 list for the chapter to the number: LV17 黑暗祭司
   (character 104), LV28 冰魔導士 x5 (101), LV16 狂戰士 x3 (81), LV18 狼人戰士
   x7 (83) and LV18 蛇魔使 x22 (75), all of them on side 0, so the guest side
   is empty this chapter.  The guide prints the 狂戰士 as LV18 and the map
   records say level 16; the guide's own HP figure for them, 720, is the
   record's HP coefficient of 45 times SIXTEEN, so the level in the file is the
   one the game plays and the guide's label is its own slip
   (assets/characters.md gives the coefficient form).

   THE TWO REINFORCEMENT WAVES ARE NOT THIS HANDLER'S.  MAP20.DAT's other
   thirty-two records are two sixteen-strong waves, 1 and 2, and the guide's
   last ten lines: an 暗魔導士 (character 103), four 騎士 (89), three 弓箭手
   (94), four 衛兵 (90) and four 武鬥家 (109) each time, the wave-2 暗魔導士 a
   level below the wave-1 one.  Both come in on a tile trigger --
   fdps_chapter_21_event_deploy_wave_1 and fdps_chapter_21_event_deploy_wave_2
   (chevt4.h) -- so none of the five ids is on the board when this returns.

   THE CURSOR ENDS ON THE TILE THE CUT-SCENE WALKED UNIT 0 TO.  ICON20.DAT
   places unit 0 at (19, 24) at script offset 63 and then walks it four times:
   two tiles up, two left, two up and one left, which is (16, 20).  The fourth
   walk is a left one, so the walks leave it facing left, not up: every pass of
   fdps_icon_script_walk_units writes the list entry's second byte into the
   facing field (src/icon.c), and that byte is 1 on the fourth walk.  Unit 0
   only ends the member facing up because of the FACE_UNITS at script offset
   249, the script's last opcode over it, after four earlier FACE_UNITS have
   turned it through 2, 1, 3 and 0.  The tile is what the cursor call reads,
   and it is one tile left of MAP20.COD's own player-slot-0 start tile, record
   70, which is (17, 20) -- the tile the third walk left it on -- so a run whose
   script never opened lands one tile away rather than somewhere obviously
   wrong.  Chapter 20 above is the other way round: its script switches map at
   the end, so its cursor tile is the map's.

   THE CHAPTER IS SET, BUT NOT BY THIS FUNCTION.  Both the script the second
   call loads and the title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put 20 there, and this member carries no SWITCH_MAP to disturb it.  The
   Icon20.dat above is the one place the chapter number is spelled out rather
   than read. */
void fdps_chapter_21_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH21_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH21_CURSOR_UNIT);
}

/* --- fdps_chapter_22_init @ 00021410 ----------------------------------- */

/* Chapter 22's opening cut-scene, the string at 0x61900 loaded into EAX at
   00021421 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon21.dat where fdps_chapter_21_init above holds Icon20.dat at 0x618f4
   and the slot-22 handler at 00021450 holds Icon22.dat at 0x6190c -- the three
   sit next to each other in the image, twelve bytes apart, each an eleven-byte
   member name and a filler byte.  Spelling the member out of the handler's own
   chapter number gives Icon22.dat, which loads, runs, and plays chapter 23's
   opening scene under chapter 22's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON21.DAT by the
   call and cannot live in read-only storage. */
#define CH22_OPENING_SCRIPT "Icon21.dat"

/* Which unit the cursor is left on: PUSH 0x3 at 00021434, not the PUSH 0x0
   twenty-seven of the thirty handlers make.  Unit slot 3 is roster slot 3, and
   the roster is in join order, so it is 法蓮娜 -- the one member chapter 22 is
   lost without, the guide's 失敗條件 for the chapter being 法蓮娜死亡.  Only
   fdps_chapter_17_init, this handler and fdps_chapter_23_init pass 3, and
   those are the three chapters that turn on her. */
#define CH22_CURSOR_UNIT 3

/* 00021410.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_21_init above, with no fdps_roster_add_character in front of
   them and no store behind them.  At 0x33 bytes it is the shortest shape the
   thirty handlers come in, and it is byte-for-byte the shape of its neighbour
   with two operands changed.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021410..00021414 -- over SUB ESP,0x0 at 00021416, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002143e..00021441
   and the RET at 00021442 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61900 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021421..0002142c, and PUSH 0x3 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021434..0002143b.  The two argument-less calls at 0002141c and 0002142f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   second call, so it returns nothing -- the dispatcher reaches it through the
   twenty-second slot of the table at 00060074 (the dword at 000600c8 is
   00021410, and that data reference is the function's only xref) and ignores
   EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER AND NOBODY LEAVES IT.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 21 finished it -- the eleven chapters 1 to 19 assembled,
   蘭斯洛特 last -- and MAP21.DAT asks for the same ELEVEN player slots
   MAP19.DAT and MAP20.DAT did, so every slot has a member behind it and
   fdps_build_map_unit_array writes no zeroed, retired spare anywhere in the
   array (src/deploy.c).

   THE CUT-SCENE TAKES 蘭迪斯 OFF THE BOARD, AND THAT IS THE ONLY UNIT STATE IT
   TOUCHES.  Walked with the opcode ladder in src/icon.c, ICON21.DAT's 283
   bytes hold fifty opcodes, and exactly one of them is a unit-state opcode:
   the RETIRE_UNIT at script offset 278, naming map unit 0, with no REVIVE
   anywhere behind it.  Thirty-five of the other forty-nine are PLACE_UNIT,
   WALK_UNITS and FACE_UNITS over the party, and the remaining fourteen are
   four view shakes, four view-tile resets, one scroll, two fades, two music
   changes and one page of chapter text.  A player slot's map unit index is its
   roster slot and the roster is in join order, so the ten the player commands
   this chapter are everybody but 蘭迪斯 -- the guide's 己方 line for the
   chapter, 蘭迪斯以外的所有人, which chapter 21's own 說明 announces one
   chapter early as 後面兩章蘭迪斯不會出場.

   THE OPENING BOARD IS THE ELEVEN PLAYER SLOTS AND ONE ENEMY.  MAP21.DAT tags
   exactly one of its fifty-five records wave 0 -- a level-20 character 73,
   巫湯婆婆, the boss the chapter's 勝利條件 names -- so the state rebuild's
   opening deploy is that single unit and the array is twelve units long when
   the handler returns.  The cut-scene adds nothing to it: ICON21.DAT carries
   no DEPLOY_WAVE and no SWITCH_MAP.

   THE FIVE REINFORCEMENT WAVES ARE NOT THIS HANDLER'S.  MAP21.DAT's other
   fifty-four records are waves 1 to 5, and the guide has them arriving on the
   first, third, fifth and eighth player turns: wave 1 is eight LV19 狼人戰士
   (character 83) with eight LV18 蛇魔使 (75), wave 2 four of each, wave 3 four
   LV17 幽魂 (105), wave 5 four LV17 骷髏兵 (84), and wave 4 the last
   twenty-two, seven of each of the first two and four of each of the second
   two.  Laid end to end that is the guide's 敵方 list for the chapter to the
   number, and its HP figures settle which id is which: ENEMYDAT.DAT is indexed
   by the character id less 60 and holds HP as a per-level coefficient
   (assets/characters.md), so row 45 at 25 a level is the 幽魂's HP425 and row
   24 at 38 a level is the 骷髏兵's HP646.  All fifty-five records are on side
   0, so the guest side is empty this chapter and none of the four
   reinforcement ids is on the board when this returns.

   THE CURSOR ENDS ON THE TILE THE CUT-SCENE WALKED UNIT 3 TO.  ICON21.DAT
   stands unit 3 at (23, 23) at script offset 52 with the rest of the party,
   moves it to (3, 24) at offset 125 once the view has been walked across the
   map, and then walks it four times: four tiles up, two right, one up and one
   right, which is (6, 19).  The fourth walk is a right one, so the walks leave
   it facing right; the FACE_UNITS at script offsets 170, 190 and 230 are what
   leave it facing up, the last of them the script's final opcode over it.  The
   tile is what the cursor call reads, and MAP21.COD's own player-slot-3 start
   tile, record 58, is (2, 24), so a run whose script never opened lands four
   tiles away.

   THE CHAPTER IS SET, BUT NOT BY THIS FUNCTION.  Both the script the second
   call loads and the title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put 21 there, and this member carries no SWITCH_MAP to disturb it.  The
   Icon21.dat above is the one place the chapter number is spelled out rather
   than read. */
void fdps_chapter_22_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH22_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH22_CURSOR_UNIT);
}

/* --- fdps_chapter_23_init @ 00021450 ----------------------------------- */

/* Chapter 23's opening cut-scene, the string at 0x6190c loaded into EAX at
   00021461 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, so this
   is Icon22.dat where fdps_chapter_22_init above holds Icon21.dat at 0x61900
   and the slot-23 handler at 00021490 holds Icon23.dat at 0x61918 -- the three
   sit next to each other in the image, twelve bytes apart, each an eleven-byte
   member name and a filler byte.  Spelling the member out of the handler's own
   chapter number gives Icon23.dat, which loads, runs, and plays chapter 24's
   opening scene under chapter 23's title card.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON22.DAT by the
   call and cannot live in read-only storage. */
#define CH23_OPENING_SCRIPT "Icon22.dat"

/* Which unit the cursor is left on: PUSH 0x3 at 00021474, not the PUSH 0x0
   twenty-seven of the thirty handlers make.  Unit slot 3 is roster slot 3, and
   the roster is in join order, so it is 法蓮娜 -- the guide's 失敗條件 for the
   chapter is 法蓮娜死亡, and its 說明 has the chapter turning on her landing
   the killing blow on the 死神.  Only fdps_chapter_17_init,
   fdps_chapter_22_init and this handler pass 3, and those are the three
   chapters that turn on her. */
#define CH23_CURSOR_UNIT 3

/* 00021450.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_22_init above, with no fdps_roster_add_character in front of
   them and no store behind them.  At 0x33 bytes it is the shortest shape the
   thirty handlers come in, and it is byte-for-byte the shape of its neighbour
   with one operand changed: chapter 22 passes the same 3 to the cursor call,
   so only the script literal separates the two bodies.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021450..00021454 -- over SUB ESP,0x0 at 00021456, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002147e..00021481
   and the RET at 00021482 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x6190c / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021461..0002146c, and PUSH 0x3 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021474..0002147b.  The two argument-less calls at 0002145c and 0002146f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   second call, so it returns nothing -- the dispatcher reaches it through the
   twenty-third slot of the table at 00060074 (the dword at 000600cc is
   00021450, and that data reference is the function's only xref) and ignores
   EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER AND NOBODY LEAVES IT.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 22 finished it -- the eleven chapters 1 to 19 assembled,
   蘭斯洛特 last -- and MAP22.DAT asks for the same ELEVEN player slots
   MAP19.DAT to MAP21.DAT did, so every slot has a member behind it and
   fdps_build_map_unit_array writes no zeroed, retired spare anywhere in the
   array (src/deploy.c).  法蓮娜 leaves the party after this chapter, which is
   the guide's 說明 and no part of what this handler does.

   THE CUT-SCENE TAKES 蘭迪斯 OFF THE BOARD BEFORE IT DOES ANYTHING ELSE.
   Walked with the opcode ladder in src/icon.c, ICON22.DAT's 626 bytes hold a
   hundred and six opcodes, and the RETIRE_UNIT at script offset 4 -- naming
   map unit 0, the third opcode of the member -- is the only one of them that
   touches a player slot's flags byte.  There is no REVIVE anywhere behind it,
   and the ten PLACE_UNITs at offsets 15 to 60 stand up units 1 to 10 and skip
   0.  A player slot's map unit index is its roster slot and the roster is in
   join order, so the ten the player commands are everybody but 蘭迪斯 -- the
   guide's 己方 line for the chapter, 蘭迪斯以外的所有人, the same line chapter
   22 carries.

   THE MAP'S OWN OPENING DEPLOY IS ALL CAST AND NO OPPOSITION.  MAP22.DAT holds
   eighty deployment records and tags twenty-one of them wave 0, and every one
   of those twenty-one is on side 1, the guest side that
   fdps_collect_targets_in_area selects with mode 2 and that the ambush gates
   in src/chevt4.c refuse (src/aitarget.c).  They are characters 36 to 39,
   FRIAPRDA.DAT template rows rather than named party members
   (assets/characters.md), so the state rebuild appends twenty-one guests as
   map units 11 to 31 and not a single enemy.

   THE OPPOSITION IS THE CUT-SCENE'S, IN TWO DEPLOYS THAT MUST STAY IN ORDER.
   ICON22.DAT's DEPLOY_WAVE at script offset 469 asks for wave 2, MAP22.DAT's
   single LV23 死神 (character 72), which lands as map unit 32; its DEPLOY_WAVE
   at offset 604 asks for wave 1, ten records, which land as map units 33 to
   42.  Both carry the place operand 0, the nearest-free-tile search rather
   than the exact anchor (src/deploy.c), and forty-three units is what the
   handler returns on.  Map unit 32 is 0x20, the index
   fdps_chapter_23_event_boss_defeat and
   fdps_chapter_23_event_deploy_wave_for_turn both hold as a literal
   (chevt4.h), and the twenty-six FACE_UNITS the member runs over unit 32
   between the two deploys are the 死神 turning as it is revealed.  Deploying
   wave 1 first would put it at map unit 42 and leave both of those events
   pointing at a 地獄犬.

   THOSE ELEVEN ARE THE GUIDE'S OPENING 敵方 GROUP, and the guide's own numbers
   are what pin the four character ids: ENEMYDAT.DAT is indexed by the id less
   60 and holds HP as a per-level coefficient with MV absolute
   (assets/characters.md), so row 12 at 180 a level is the 死神's HP4140 at
   LV23 and MV3, row 24 at 38 is the 骷髏兵's HP684 at LV18 and MV4, row 45 at
   25 is the 幽魂's HP450 and MV4, and row 47 at 35 is the 地獄犬's HP630 and
   MV7.  Wave 1 is three 骷髏兵, two 幽魂 and five 地獄犬, which is the guide's
   second, third and fourth 敵方 lines to the number.  The map's remaining
   forty-eight records are not down yet: waves 3 and 4 are the fifteen and
   twenty-seven the guide gives as the 第三回合 and 第七回合 reinforcements,
   both fdps_chapter_23_event_deploy_wave_for_turn's, and waves 5 to 10 are six
   single guest-side records the chapter's own events bring on one at a time.

   THE CURSOR ENDS ON THE TILE THE CUT-SCENE WALKED UNIT 3 TO.  ICON22.DAT
   stands unit 3 at (11, 16) at script offset 35 and walks it exactly once, in
   the seven-unit group walk at offset 128, one tile with the facing byte 2 --
   up -- which is (11, 15).  Six later FACE_UNITS turn it, the last of them at
   offset 615 leaving it facing up.  The tile is what the cursor call reads,
   and MAP22.COD gives all eleven player slots the same start tile, record 83
   at (30, 23), so a run whose script never opened lands nineteen tiles right
   and eight down of where this one leaves the cursor.

   NOTHING IS WRITTEN ON A UNIT'S STATUS.  The body has no store in it at all,
   and the cut-scene adds none either: walked with the same ladder the shipped
   ICON22.DAT holds no SET_UNIT_TIMER anywhere.  The guide lists no status on
   anybody at the chapter's start.

   THE CHAPTER IS SET, BUT NOT BY THIS FUNCTION.  Both the script the second
   call loads and the title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put 22 there, and this member carries no SWITCH_MAP to disturb it.  The
   Icon22.dat above is the one place the chapter number is spelled out rather
   than read. */
void fdps_chapter_23_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH23_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH23_CURSOR_UNIT);
}
