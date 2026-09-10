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

/* Chapter 18's entry handler: brings the game into chapter 18.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon17.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.

   Nobody joins the party this chapter -- there is no fdps_roster_add_character
   in the body -- and the roster is left as chapter 17 finished it, the same
   ten members.  MAP17.DAT asks for ten player slots, so every slot has a
   member behind it and no zeroed, retired spare is written.

   The whole party fights.  ICON17.DAT retires map units 3, 4, 7, 8 and 9 for
   the length of the scene and REVIVEs all five before it ends, which no other
   chapter-init cut-scene does, so all ten player slots carry a clear flags
   byte when the handler returns.

   The board the chapter opens on is the cut-scene's doing and not the map's.
   MAP17.DAT tags only two of its sixty-five deployment records wave 0 -- a
   guest-side character 12 and a character 117, map units 10 and 11, the scene's
   own actors -- and the cut-scene deploys wave 2 for a third, map unit 12, then
   retires all three before it ends.  Its closing DEPLOY_WAVE brings in wave 1,
   the fifteen records that are the strategy guide's opening 敵方 list to the
   number: LV14 衛兵 x5 (character 90), LV15 弓箭手 x2 (94), LV13 騎士 x5 (89),
   LV14 暗黑騎士 x2 (77) and LV16 暗魔導士 x1 (103).  The map's remaining
   forty-eight records are the chapter's turn events -- flights of 飛兵 and
   squads of 騎士 on waves 4 to 11, and the wave-13 reinforcement the guide
   gives as 第十三回合 -- and are not down yet.

   The cut-scene also walks unit 0 off its start tile onto (17, 19), so the
   tile the cursor ends on is ICON17.DAT's and not MAP17.COD's.

   Table slot 17. */
extern void fdps_chapter_18_init(void);
#pragma aux fdps_chapter_18_init "*" parm caller [];

/* Chapter 19's entry handler: hands the 聖騎士 蘭斯洛特 to the party roster
   and brings the game into chapter 19.  Takes nothing and returns nothing.

   In order: character 11 is appended to the roster, the chapter state is
   rebuilt, the opening cut-scene Icon18.dat is interpreted, the chapter title
   card is shown, and the map cursor is parked on unit 0's tile.

   蘭斯洛特 JOINS HERE, NOT AT THE ARRIVAL EVENT.  The add runs on the way into
   the chapter, so he is on the roster before the battle starts even though the
   unit the player sees walk on belongs to the turn-6 event
   fdps_chapter_19_event_lancelot_joins.  The two are different records: the add
   builds his roster line out of FRIAPRDA.DAT row 11 at level 15 and 616 HP,
   while MAP18.DAT's wave-1 record puts a level-2 unit on the board.  The
   strategy guide records both -- its 己方 line is the level-2 unit, and its
   備註 says that finishing the chapter before the event fires still leaves him
   in the party, at level 15 with 修羅之矛 and 重鎧甲, which is this add's
   record exactly.

   HE IS NOT ONE OF THIS CHAPTER'S MAP UNITS.  He lands at roster slot 10 and
   MAP18.DAT asks for ten player slots, so the ten the chapter is fought with
   are the same ten chapters 16 to 18 opened with and no slot is left over.

   The board the chapter opens on is half the map's and half the cut-scene's.
   MAP18.DAT tags five of its sixty-eight deployment records wave 0 -- LV15
   暗黑騎士 (character 77) -- and ICON18.DAT's single DEPLOY_WAVE brings in the
   thirty-nine records tagged wave 5, for fifty-four units.  Those thirty-nine
   plus the five are the strategy guide's opening 敵方 list to the number: LV17
   弓箭手 x14 (character 94), LV18 暗魔導士 x7 (103), LV17 飛兵 x6 (96), LV17
   野蠻戰士 x5 (80) and LV15 暗黑騎士 x12 (77).  The map's remaining
   twenty-four records are the chapter's own events -- wave 1 蘭斯洛特, the
   wave-2 LV18 challenger the guide leaves unnamed, and the wave-6
   reinforcement of fourteen LV17 飛兵 and eight LV15 武鬥家 -- and none of them
   is down yet.

   The cut-scene walks unit 0 off its start tile, so the tile the cursor ends on
   is ICON18.DAT's and not MAP18.COD's.

   Table slot 18. */
extern void fdps_chapter_19_init(void);
#pragma aux fdps_chapter_19_init "*" parm caller [];

/* Chapter 20's entry handler: brings the game into chapter 20.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon19.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.

   Nobody joins the party this chapter -- there is no fdps_roster_add_character
   in the body -- and the roster is left as chapter 19 finished it, eleven
   members: the ten chapters 1 to 15 assembled and 蘭斯洛特, whom chapter 19's
   handler put on at roster slot 10.  MAP19.DAT is the first map to ask for
   ELEVEN player slots, so every slot has a member behind it, no zeroed,
   retired spare is written, and 蘭斯洛特 is a map unit for the first time.

   THE CUT-SCENE IS STAGED ON ANOTHER MAP.  ICON19.DAT stages itself on
   MAP58.DAT -- a cut-scene map with no player slots, whose three actors are
   蘭迪斯, 法蓮娜 and 費塔加 -- and switches back to chapter 19 before it
   ends, rebuilding the chapter state a second time.  Every RETIRE, REVIVE and
   DEPLOY_WAVE it carries applies to the actors on that stage and is discarded
   with them, so the board the handler returns on is the closing rebuild's: the
   eleven player slots and the fifty-four records MAP19.DAT tags wave 0,
   sixty-five units.  Nine of the thirty opening scripts are built this way --
   ICON00, ICON06 to ICON09, ICON11, ICON19, ICON24 and ICON29 -- and this is
   the first of them in this file; chapters 16 to 19 above are all of the other
   kind.

   Those fifty-four are the strategy guide's 敵方 list for the chapter to the
   number: LV17 蛇魔使 x23 (character 75), LV18 野蠻戰士 x13 (80), LV17
   狼人戰士 x10 (83) and LV28 冰魔導士 x8 (101).  The map's one remaining
   record, a level-17 character 67 on wave 1, belongs to the chapter's own turn
   events and is not down yet.

   Because the closing rebuild comes after every walk in the scene, the tile
   the cursor ends on is MAP19.COD's own player-slot-0 start tile, (5, 4), and
   not a tile the cut-scene walked to.  Chapters 16 to 19 above are the other
   way round: their scripts switch no map, so their cursor tile is the one
   their walks reached.

   Table slot 19. */
extern void fdps_chapter_20_init(void);
#pragma aux fdps_chapter_20_init "*" parm caller [];

/* Chapter 21's entry handler: brings the game into chapter 21.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon20.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.

   Nobody joins the party this chapter -- there is no fdps_roster_add_character
   in the body -- and the roster is left as chapter 20 finished it, the same
   eleven members chapter 20 was fought with.  MAP20.DAT asks for the same
   ELEVEN player slots MAP19.DAT did, so every slot has a member behind it and
   no zeroed, retired spare is written.

   THE CUT-SCENE CHANGES NO UNIT STATE.  ICON20.DAT has no SWITCH_MAP, no
   RETIRE_UNIT, no REVIVE_UNIT, no DEPLOY_WAVE and no SET_UNIT_TIMER: it only
   stands the eleven party units where the chapter opens and says one page of
   chapter text.  So the board the handler returns on is the state rebuild's
   alone -- the eleven player slots and the thirty-eight records MAP20.DAT tags
   wave 0, forty-nine units.

   Those thirty-eight are the first five lines of the strategy guide's 敵方
   list for the chapter: LV17 黑暗祭司 (character 104), LV28 冰魔導士 x5 (101),
   LV16 狂戰士 x3 (81), LV18 狼人戰士 x7 (83) and LV18 蛇魔使 x22 (75), all on
   the enemy side.  The guide prints the 狂戰士 as LV18 and the map says level
   16; the guide's own HP figure of 720 is the record's coefficient times
   sixteen, so the file is the one the game plays.

   The map's other thirty-two records are the two sixteen-strong reinforcement
   waves the guide's last ten lines list, and neither is this handler's: both
   are tile triggers, fdps_chapter_21_event_deploy_wave_1 and
   fdps_chapter_21_event_deploy_wave_2 (chevt4.h).

   Because the script switches no map, the tile the cursor ends on is the one
   the cut-scene's four walks of unit 0 reached, (16, 20), and not MAP20.COD's
   own player-slot-0 start tile (17, 20) one square to its right.  Chapter 20
   above is the other way round.

   Table slot 20. */
extern void fdps_chapter_21_init(void);
#pragma aux fdps_chapter_21_init "*" parm caller [];

#endif
