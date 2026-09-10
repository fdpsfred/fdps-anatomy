/* chinit2.h -- the per-chapter entry handlers, chapters 16 to 30.  Chapters 1
 * to 10 are chinit1.h and 11 to 15 are chinit1b.h.
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
 * in place: whoever joins the party this chapter, the rebuilt chapter state,
 * the opening cut-scene and title card, and any unit condition the chapter
 * opens with that no data file carries.
 *
 * Nothing here owns state.  The roster is roster.h's, the chapter frame and
 * the title card are chapter.h's, the cut-scene interpreter is icon.h's and
 * the map cursor is mapcur.h's.
 */
#ifndef CHINIT2_H
#define CHINIT2_H

/* Chapter 16's entry handler: brings the game into chapter 16.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon15.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   15 finished it, ten members: the eight chapters 1 to 4 and 7 to 9 put on,
   琴琴 and 瑪麗安.  MAP15.DAT is the first map to ask for TEN player slots, so
   every slot has a member behind it and no zeroed, retired spare is written.

   The chapter is fought with half the party, and the cut-scene is what takes
   the other half off: ICON15.DAT retires map units 3, 4, 7, 8 and 9 -- roster
   slots, so 法蓮娜, 裘娜, 蓋亞, 琴琴 and 瑪麗安 -- and carries no REVIVE, so
   the five the player commands are 蘭迪斯, 尤利安, 亞克, 費塔加 and 布蘭多,
   which is the strategy guide's 己方 line for the chapter.  Chapter 17 is the
   other half of the same split.

   The whole opposition is on the map before the player moves.  MAP15.DAT tags
   thirteen of its twenty-five deployment records wave 0, which the state reset
   puts down, and the cut-scene's four DEPLOY_WAVEs -- waves 1, 2, 3 and 4,
   each placed exactly -- put down the other twelve, for thirty-five units.
   Those twenty-five are the guide's 敵方 list to the number: LV16 暗魔導士 x4
   (character 103), LV11 騎士 x4 (89), LV14 武士 x9 (99), LV14 弓箭手 x4 (94)
   and LV12 飛兵 x4 (96).

   The cut-scene also walks unit 0 off its start tile, so the tile the cursor
   ends on is ICON15.DAT's and not MAP15.COD's.

   Table slot 15. */
extern void fdps_chapter_16_init(void);
#pragma aux fdps_chapter_16_init "*" parm caller [];

/* Chapter 17's entry handler: brings the game into chapter 17.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon16.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 3's tile.  Unit 3 is roster slot 3, 法蓮娜, and this is one of only
   three handlers that park the cursor anywhere but unit 0 -- the other two are
   chapters 22 and 23, and those three are exactly the chapters 法蓮娜 is the
   loss condition of.

   Nobody joins the party this chapter -- there is no fdps_roster_add_character
   in the body -- and the roster is left as chapter 16 finished it, the same
   ten members.  MAP16.DAT asks for ten player slots, so every slot has a
   member behind it and no zeroed, retired spare is written.

   The chapter is fought with the half of the party chapter 16 sat out, and the
   cut-scene is what takes the other half off: ICON16.DAT retires map units 0,
   1, 2, 5 and 6 -- the exact complement of ICON15.DAT's five -- and carries no
   REVIVE, so the five the player commands are 法蓮娜, 裘娜, 蓋亞, 琴琴 and
   瑪麗安, which is the strategy guide's 己方 line for the chapter.

   The cut-scene puts nobody on the map: ICON16.DAT has no DEPLOY_WAVE at all,
   so what is on the board when the handler returns is the reset's own opening
   deploy -- the ten player slots and the twenty-three records MAP16.DAT tags
   wave 0, thirty-three units.  Twenty of those twenty-three are enemy-side and
   are the guide's opening 敵方 list to the number: LV16 暗魔導士 x2 (character
   103), LV11 騎士 x6 (89), LV15 武士 x10 (99) and LV14 弓箭手 x2 (94).  The
   other three carry side 1, the guest side: the hostages the chapter is named
   for.  The map's remaining seven records -- six wave-1 武士 and one wave-2
   狼人 -- belong to the chapter's own turn events and are not down yet.

   The cut-scene also walks unit 3 off its start tile onto (2, 7), so the tile
   the cursor ends on is ICON16.DAT's and not MAP16.COD's.

   Table slot 16. */
extern void fdps_chapter_17_init(void);
#pragma aux fdps_chapter_17_init "*" parm caller [];

#endif
