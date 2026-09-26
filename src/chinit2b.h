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
   apart from chapter 25's: its 142 opcodes before the closing END play
   entirely on the board this handler's own reset built, and nothing but the
   dispatcher ever writes the chapter id.

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
   and unlike chapter 25's: its seventy-four opcodes before the closing END
   play entirely on the board this handler's own reset built, and nothing but
   the dispatcher ever writes the chapter id.

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
   victory test asks about (src/chpost3.c); the four 魔戰將軍 follow him at 13 to
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

/* Chapter 28's entry handler: brings the game into chapter 28.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon27.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   24 finished it, twelve members: the eleven chapters 1 to 19 assembled and
   珊.  MAP27.DAT asks for twelve player slots, so every slot has a member
   behind it and no zeroed, retired spare is written.

   The cut-scene switches no map and deploys no wave, the same as chapters 26's
   and 27's and unlike chapter 25's: its thirty-one opcodes before the closing
   END play entirely on the board this handler's own reset built, and nothing
   but the dispatcher ever writes the chapter id.

   The chapter is fought with the whole party, which is what tells this handler
   apart from the three before it.  ICON27.DAT carries no RETIRE_UNIT, no
   REVIVE_UNIT and no BLINK_UNITS_OUT anywhere in its 267 bytes, so all twelve
   player slots are on the board with a clear flags byte when the handler
   returns; the strategy guide's entry for the chapter has no 己方 line of its
   own to name anybody left off.  What the cut-scene does with the party
   instead is place it: twelve PLACE_UNITs put map units 0 to 11 on tiles of
   the script's own choosing, overwriting MAP27.COD's start records, and nine
   group walks march them up the map.

   The opening opposition is the map's own wave 0 and nothing else.  MAP27.DAT
   tags thirty-seven of its sixty-five deployment records wave 0, so the board
   is forty-nine units, and those thirty-seven are exactly the guide's 敵方 list
   for the chapter: LV25 黑暗祭司 x4 (character 104), LV25 幽魂 x9 (105), LV25
   骷髏兵 x10 (84) and LV25 地獄犬 x14 (107).  Record 0 of the file is wave 0
   and a 骷髏兵, so it lands at map unit 12, the first unit behind the party.
   The twenty-eight records held back are nine waves of three -- the guide's
   事件, reinforcement arriving at the end of the player's 2nd, 4th, 6th, 7th,
   10th, 12th, 14th, 16th and 18th turns -- and one record tagged 0xff, and
   this handler deploys none of them.

   The cursor ends on the tile the cut-scene put unit 0 on and walked it from:
   placed on (11, 24), then five tiles up, one right and two more up, so the
   answer is (12, 17) and nothing of MAP27.COD's own (8, 18) survives into it.

   Table slot 27. */
extern void fdps_chapter_28_init(void);
#pragma aux fdps_chapter_28_init "*" parm caller [];

/* Chapter 29's entry handler: brings the game into chapter 29.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon28.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   24 finished it, twelve members: the eleven chapters 1 to 19 assembled and
   珊.  MAP28.DAT asks for twelve player slots, so every slot has a member
   behind it and no zeroed, retired spare is written.

   The cut-scene switches no map and deploys no wave, the same as chapters
   26's, 27's and 28's and unlike chapter 25's: its eighty-nine opcodes before
   the closing END play entirely on the board this handler's own reset built,
   and nothing but the dispatcher ever writes the chapter id.

   The chapter is fought with the whole party, as chapter 28 is, but by a route
   chapter 28's member has not got: ICON28.DAT carries no RETIRE_UNIT, and the
   two BLINK_UNITS_OUT it does carry both name map units 90 and 91 -- the two
   守護魔龍, which are the map's last two deployment records.  A blink leaves
   its units retired, and each of these two is undone by a pair of
   REVIVE_UNITs, so all ninety-two units on the board carry a clear flags byte
   when the handler returns.  The strategy guide's entry for the chapter has no
   己方 line of its own.

   The opening opposition is the whole map file.  MAP28.DAT tags all eighty of
   its deployment records wave 0, so the board is ninety-two units, and those
   eighty are the guide's 敵方 list exactly: LV27 骷髏兵 x34 (character 84),
   LV27 地獄犬 x25 (107), LV27 幽魂 x19 (105) and LV30 守護魔龍 x2 (112).
   Record 0 is a 骷髏兵 and so lands at map unit 12; the two 守護魔龍 are
   the file's last two records and so map units 90 and 91.  The map holds
   nothing back: this chapter's 事件 line in the guide is the moment the
   standing enemy groups start moving, not a reinforcement that arrives.

   The cursor ends on the tile the cut-scene put unit 0 on and walked it from:
   placed on (5, 24), then five tiles up, so the answer is (5, 19) and nothing
   of MAP28.COD's own (4, 19) survives into it.

   Table slot 28. */
extern void fdps_chapter_29_init(void);
#pragma aux fdps_chapter_29_init "*" parm caller [];

/* Chapter 30's entry handler: brings the game into chapter 30, the last
   chapter.  Takes nothing and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon29.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   24 finished it, twelve members: the eleven chapters 1 to 19 assembled and
   珊.  MAP29.DAT asks for twelve player slots, so every slot has a member
   behind it and no zeroed, retired spare is written.

   The reset leaves nothing on the board but the party.  MAP29.DAT holds seven
   deployment records and tags not one of them wave 0 -- the only one of the
   six maps this file's handlers load that opens empty -- so everything the
   player faces this chapter is put there by a script.

   The cut-scene deploys the boss.  ICON29.DAT carries two DEPLOY_WAVEs, both
   `04 01 01`, and MAP29.DAT's wave 1 is a single record: the LV40
   平衡之神, character 60, the guide's HP6000 first form.  It
   lands at map unit 12 on MAP29.COD's record 0, (10, 3), so the board is
   thirteen units when the handler returns.  The god's second and third forms
   (characters 61 and 62, waves 2 and 3) and the endless reinforcement pair the
   guide's 事件 line describes (LV20 死靈 x2, character 106,
   and LV20 白骨戰士 x2, 85, wave 4) are no part of it.

   The cut-scene switches map once, at script offset 2936, and the switch
   changes nothing: it writes chapter id 29, the id that was already there, and
   the reset it makes rebuilds the same board -- discarding the first
   DEPLOY_WAVE and the four RETIRE_UNIT / four REVIVE_UNIT / four PLACE_UNIT
   opcodes that flash the god in and out of the scene -- before the second
   DEPLOY_WAVE puts wave 1 back.

   What the scene leaves behind it is a triggered cell event, flag 0 set to 1
   past the last reset, and all twelve party members turned to facing 2 over
   the 0 the array build wrote.

   The cursor ends on the map's own tile, unlike chapters 25's and 29's: every
   PLACE_UNIT in the member names map unit 12 and no walk lists a party member,
   so player slot 0 keeps MAP29.COD's start tile (7, 15) and the cursor lands
   on (168, 360).

   Table slot 29, the last slot of the table: there is no chapter 31. */
extern void fdps_chapter_30_init(void);
#pragma aux fdps_chapter_30_init "*" parm caller [];

#endif
