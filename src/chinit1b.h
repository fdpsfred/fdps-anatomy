/* chinit1b.h -- the per-chapter entry handlers, chapters 11 to 15.  Chapters 1
 * to 10 are chinit1.h and 16 to 30 are chinit2.h.
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
#ifndef CHINIT1B_H
#define CHINIT1B_H

/* Chapter 11's entry handler: brings the game into chapter 11.  Takes nothing
   and returns nothing.

   In order, and the order is the content: character 7 -- 琴琴 the 武道家, who
   joins the party at the start of the chapter -- is appended to the party
   roster; the chapter state is rebuilt around the roster that add has just
   grown; the opening cut-scene Icon10.dat is interpreted; the chapter title
   card is shown; and the map cursor is parked on unit 0's tile.

   The roster add comes first and here the ordering really does decide the
   chapter.  MAP10.DAT asks for NINE player slots and the party is eight
   members when the chapter opens, so the ninth slot is hers and only hers;
   fdps_build_map_unit_array fills a player slot from the roster only while
   the slot index is below the roster count and writes a zeroed, retired spare
   otherwise (src/deploy.c).  Run the reset first and slot 8 is that spare --
   and the cut-scene then walks, poses, retires, revives and re-places map
   unit 8 all the way through, and the strategy guide's own losing condition
   for the chapter is 蘭迪斯 or 琴琴 dying.

   The cut-scene is what puts the opposition down: ICON10.DAT carries no
   SWITCH_MAP and three DEPLOY_WAVEs, waves 1, 2 and 3 in that order, which on
   MAP10.DAT is 7, 3 and 4 records.  With the three the map tags wave 0 -- the
   level-1 stand-ins the cut-scene retires again -- and the nine player slots
   that is a map of twenty-six units when the handler returns, and those
   fourteen are the guide's opening 敵方 group to the number: LV15 魔導士 x3,
   LV15 冰魔導士 x2, LV14 野武士, LV13 狼人 x2, LV13 拳士 x3 and LV13 弓兵 x3.
   The map's other thirty-two records are its waves 4 and 5, the
   reinforcements the guide lists against later turns.

   Table slot 10. */
extern void fdps_chapter_11_init(void);
#pragma aux fdps_chapter_11_init "*" parm caller [];

/* Chapter 12's entry handler: brings the game into chapter 12.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon11.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   11 finished it, nine members: the eight chapters 1 to 4 and 7 to 9 put on
   and 琴琴.  MAP11.DAT asks for exactly nine player slots, so every slot has a
   member behind it and no zeroed, retired spare is written.

   The chapter's one opponent is chosen inside the cut-scene and not here.
   ICON11.DAT switches to two cut-scene maps and back to map 11, and its last
   DEPLOY_WAVE carries the wave operand 0xff, the form that takes the wave from
   the answer the ASK_THREE_WAY question left behind (icon.h).  MAP11.DAT holds
   three deployment records, tagged wave 1, 2 and 3 and tagged nothing wave 0
   -- LV20 characters 113, 114 and 115, which are the strategy guide's
   修佩魯, 雷德 and 亞德尼恩, one per room of 火神的宮殿 -- so the handler
   returns with nine player slots and one opponent, ten units.

   Table slot 11. */
extern void fdps_chapter_12_init(void);
#pragma aux fdps_chapter_12_init "*" parm caller [];

/* Chapter 13's entry handler: brings the game into chapter 13.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon12.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   12 finished it, nine members: the eight chapters 1 to 4 and 7 to 9 put on
   and 琴琴.  MAP12.DAT asks for exactly nine player slots, so every slot has a
   member behind it and no zeroed, retired spare is written.

   The whole opposition is on the map before the cut-scene starts.  MAP12.DAT
   tags every one of its thirty-seven deployment records wave 0, so the state
   reset's own opening deploy puts all of them down and the array is forty-six
   units by the time the script's first opcode runs; ICON12.DAT carries no
   DEPLOY_WAVE and no SWITCH_MAP at all.  What the script does change is that
   it retires one of the thirty-seven and never revives it -- map unit 29,
   which is record 20, character 13 at level 15 -- and that is why the array
   holds thirty-seven opponents where the strategy guide's 敵方 list for the
   chapter counts thirty-six.  It also walks the party off its start tiles, so
   the tile the cursor ends on is the script's and not MAP12.COD's.

   Table slot 12. */
extern void fdps_chapter_13_init(void);
#pragma aux fdps_chapter_13_init "*" parm caller [];

/* Chapter 14's entry handler: brings the game into chapter 14.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon13.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   13 finished it, nine members: the eight chapters 1 to 4 and 7 to 9 put on
   and 琴琴.  MAP13.DAT asks for exactly nine player slots, so every slot has a
   member behind it and no zeroed, retired spare is written.

   The whole opening opposition comes out of the cut-scene.  MAP13.DAT tags
   none of its thirty-seven deployment records wave 0 -- eighteen are wave 1,
   two are wave 2 and seventeen are wave 4 -- so the state reset deploys nobody
   and the nine player slots stand alone until ICON13.DAT's two DEPLOY_WAVEs,
   wave 2 and then wave 1, put twenty opponents down for an array of
   twenty-nine.  Those twenty are the strategy guide's opening 敵方 group:
   five LV13 武士 (character 99), six LV12 飛兵 (96), five LV11 弓箭手 (94)
   and four LV13 暗魔導士 (103).  The map's wave 4 is the guide's turn-six
   event -- sixteen more 武士 and the LV17 狼人 that opens the treasure chests
   -- and no part of this handler deploys it.

   The script also walks the party off its start tiles, so the tile the cursor
   ends on is ICON13.DAT's and not MAP13.COD's.

   Table slot 13. */
extern void fdps_chapter_14_init(void);
#pragma aux fdps_chapter_14_init "*" parm caller [];

/* Chapter 15's entry handler: brings the game into chapter 15.  Takes nothing
   and returns nothing.

   In order: character 5 -- 瑪麗安 the 弓兵 -- is appended to the party roster;
   the chapter state is rebuilt; the opening cut-scene Icon14.dat is
   interpreted; the chapter title card is shown; the acted-this-turn bit is
   cleared off every unit on the map; and the map cursor is parked on unit 0's
   tile.  It is the only one of the thirty handlers that makes that fifth call.

   THE ROSTER ADD IS FOR THE CHAPTERS AFTER THIS ONE, NOT FOR THIS ONE.
   MAP14.DAT asks for NINE player slots and the party is already nine members
   when the chapter opens -- the eight chapters 1 to 4 and 7 to 9 put on, and
   琴琴 -- so the nine slots are filled from roster slots 0 to 8 and 瑪麗安,
   who lands at roster slot 9, gets no player slot here at all.  She reaches
   the chapter's map as MAP14.DAT's own record 0 instead: side 2, character 5,
   level 20, keyed wave 1, carrying 狙擊弓, 銀鱗甲 and 再生藥, which is the
   LV20 弓兵瑪麗安（HP250,MP38,AP250,DP165,DX70,MV4）the strategy guide prints
   as this chapter's 己方 addition.  Her roster record is the level-10 line out
   of FRIAPRDA.DAT, and it is what MAP15.DAT and every later map deploy: from
   chapter 16 on the maps ask for TEN player slots.

   The opening opposition is on the map before the cut-scene starts.  MAP14.DAT
   keys forty-three of its forty-five deployments wave 0, so the state reset's
   own opening deploy puts all of them down behind the nine player slots:
   forty-two enemy-side units -- twenty-six LV14 野蠻戰士 (character 80), nine
   LV15 弓箭手 (94), five LV19 冰魔導士 (101), one LV18 暗魔導士 (103) and the
   LV16 光束砲座 (128) the chapter is won by destroying -- and one ally-side
   LV15 actor (98).  ICON14.DAT then deploys wave 1, which is 瑪麗安 alone, for
   fifty-three units when the handler returns.  The remaining record is wave 2,
   the LV17 character 35 the guide lists against the twenty-five-turn event,
   and nothing here deploys it.

   THE CLEAR IS FOR WHAT THE CUT-SCENE DID.  ICON14.DAT is the only
   Icon%02d.dat in the game carrying opcode 0x62, the scripted actor step: it
   runs one behaviour step for map unit 35 -- MAP14.DAT's record 27, the
   光束砲座 -- and that step ends in fdps_battle_mark_unit_done, which raises
   the acted-this-turn bit.  fdps_icon_script_run clears that bit on entry, not
   on exit, so without this handler's own call the cannon would sit out the
   first player phase.

   Table slot 14. */
extern void fdps_chapter_15_init(void);
#pragma aux fdps_chapter_15_init "*" parm caller [];

#endif
