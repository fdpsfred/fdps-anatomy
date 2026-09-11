/* chinit2b.h -- the per-chapter entry handlers, chapters 25 to 30.  Chapters 1
 * to 10 are chinit1.h, 11 to 15 are chinit1b.h and 16 to 24 are chinit2.h.
 * This file starts at chapter 25 because chapter 24's handler is the last in
 * the game that adds anybody to the roster.
 *
 * Every entry point here is a slot of the handler table based at 00060074,
 * and the slot number is the 0-based chapter id.  The table is reached
 * indirectly: fdps_title_screen loads data_fdps_chapter_current_chapter_id
 * (gamedata.h), scales it by four and CALLs through the slot with nothing
 * pushed and no stack cleanup, so every handler shares one function-pointer
 * type -- no arguments, no result -- and none of them has a static caller.
 *
 * A handler runs once, at the moment the game enters its chapter and before
 * the battle loop starts.  Its job is to put the chapter's opening situation
 * in place: the rebuilt chapter state, the opening cut-scene and title card,
 * and the tile the map cursor starts on.
 *
 * Nothing here owns state.  The chapter frame and the title card are
 * chapter.h's, the cut-scene interpreter is icon.h's and the map cursor is
 * mapcur.h's.
 */
#ifndef CHINIT2B_H
#define CHINIT2B_H

/* Chapter 25's entry handler: brings the game into chapter 25.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon24.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   24 finished it, twelve members: the eleven chapters 1 to 19 assembled and
   珊, whom chapter 24's handler appended.  MAP24.DAT is the first map to ask
   for TWELVE player slots, so every slot has a member behind it and no zeroed,
   retired spare is written.

   The cut-scene switches map twice on its own account, which no other member
   in this file does: it resets the state onto map 56, a cut-scene stage with
   nothing of its own on the board, plays the scene there, and resets back onto
   map 24 before it retires anybody.  The chapter id reads 24 again when the
   handler returns because the script put it back, not because nothing touched
   it.

   The chapter is fought without 法蓮娜, and the cut-scene is what takes her
   off: ICON24.DAT retires map unit 3 -- a roster slot, so 法蓮娜 -- with no
   REVIVE behind it, leaving the eleven the strategy guide's 己方 line names,
   法蓮娜以外的所有人.

   The opening opposition is the map's own wave 0 and nothing else; the
   cut-scene carries no DEPLOY_WAVE.  MAP24.DAT tags forty-one of its
   fifty-nine deployment records wave 0, so the board is fifty-three units, and
   those forty-one are the guide's 敵方 list for the chapter less one group:
   LV30 塞克斯, 布魯森 and 汎拉沛 one each (characters 64, 65 and 66), LV16
   黑暗祭司 x1 (104), LV16 地獄騎士 x8 (78), LV16 鎧甲武士 x18 (100) and LV16
   神箭手 x11 (95) -- the three 魔戰將軍 included, so the chapter opens with
   them already on the board.  The remaining eighteen records are wave 1,
   eighteen LV16 天空騎士 (97), the guide's 事件 reinforcement, and this
   handler does not deploy them.

   The cut-scene also places unit 0 and walks it, so the tile the cursor ends
   on is ICON24.DAT's (4, 2) and not MAP24.COD's start tile for player slot 0.

   Table slot 24. */
extern void fdps_chapter_25_init(void);
#pragma aux fdps_chapter_25_init "*" parm caller [];

/* Chapter 26's entry handler: brings the game into chapter 26.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon25.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   24 finished it, twelve members: the eleven chapters 1 to 19 assembled and
   珊.  MAP25.DAT asks for twelve player slots, so every slot has a member
   behind it and no zeroed, retired spare is written.

   The cut-scene switches no map and deploys no wave, which is what tells it
   apart from chapter 25's: its 142 opcodes play entirely on the board this
   handler's own reset built, and nothing but the dispatcher ever writes the
   chapter id.

   The chapter is fought without 法蓮娜, and the cut-scene is what leaves her
   off: it retires map units 1 to 11 so that only 蘭迪斯 is drawn through the
   opening, then revives 1, 2 and 4 to 11.  Map unit 3 is the one index the
   revive run skips and nothing revives it later; a player slot's map unit
   index is its roster slot, so the eleven left are the strategy guide's
   己方 line, 法蓮娜以外的所有人.

   The opening opposition is the map's own wave 0 and nothing else, and on this
   chapter that is nearly the whole file.  MAP25.DAT tags sixty-eight of its
   eighty deployment records wave 0, so the board is eighty units: the four
   LV30 named generals 凱因巴, 塞克斯, 布魯森 and 汎拉沛 one each (characters
   67, 64, 65 and 66), LV18 黑暗祭司 x4 (104), LV18 地獄騎士 x10 (78), LV18
   神箭手 x10 (95), LV18 鎧甲武士 x24 (100) and LV18 天空騎士 x16 (97) --
   the guide's 敵方 list bar one group, so all four 魔戰將軍 stand on the field
   from the first turn.  The twelve records held back are seven more LV18
   鎧甲武士 in wave 2 and, in wave 3, the guide's 友方: the LV40 英雄索爾 (12)
   and four LV40 侍衛 (59).  Both waves belong to the guide's 事件 line and
   this handler deploys neither.

   The cursor ends on the tile the cut-scene walked unit 0 to: MAP25.COD starts
   player slot 0 on (6, 36) and the member walks it four tiles up, so the
   answer is (6, 32) and not the map's own start tile.

   Table slot 25. */
extern void fdps_chapter_26_init(void);
#pragma aux fdps_chapter_26_init "*" parm caller [];

/* Chapter 27's entry handler: brings the game into chapter 27.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon26.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   24 finished it, twelve members: the eleven chapters 1 to 19 assembled and
   珊.  MAP26.DAT asks for twelve player slots, so every slot has a member
   behind it and no zeroed, retired spare is written.

   The cut-scene switches no map and deploys no wave, the same as chapter 26's
   and unlike chapter 25's: its seventy-five opcodes play entirely on the board
   this handler's own reset built, and nothing but the dispatcher ever writes
   the chapter id.

   The chapter is fought without 法蓮娜, and the cut-scene is what takes her
   off: its one RETIRE_UNIT, the third opcode in the file, names map unit 3 and
   nothing revives it -- ICON26.DAT carries no REVIVE_UNIT at all.  A player
   slot's map unit index is its roster slot and the roster is in join order, so
   the eleven left are the strategy guide's 己方 line for the chapter,
   法蓮娜以外的所有人.  Chapter 25 arrives at the same eleven the same way and
   chapter 26 by retiring everybody and bringing all but that one back.

   The opening opposition is the map's own wave 0 and nothing else.  MAP26.DAT
   tags twenty-five of its fifty-five deployment records wave 0, so the board is
   thirty-seven units: LV40 魔導王吉歐 (character 63) once, the four LV30
   魔戰將軍 塞克斯, 布魯森, 汎拉沫 and 凱因巴 (64, 65, 66 and 67) once each,
   LV18 神箭手 x8 (95) and LV18 鑺甲武士 x12 (100).  吉歐 is the file's
   first record and so lands at map unit 12, which is the slot chapter 27's
   victory test asks about (src/chpost2.c); the four 魔戰將軍 follow him at 13 to
   16.  The thirty records held back are all of wave 1 -- LV18 地獄騎士 x10
   (78) and LV18 天空騎士 x20 (97) -- which is the guide's 事件, the
   reinforcement that arrives once the four 魔戰將軍 are down, and this handler
   deploys none of it.

   The cursor ends on the tile the cut-scene walked unit 0 to: MAP26.COD starts
   player slot 0 on (8, 27) and the member walks it four tiles up in four
   separate group walks, so the answer is (8, 23) and not the map's own start
   tile.

   Table slot 26. */
extern void fdps_chapter_27_init(void);
#pragma aux fdps_chapter_27_init "*" parm caller [];

#endif
