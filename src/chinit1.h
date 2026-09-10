/* chinit1.h -- the per-chapter entry handlers, chapters 1 to 15.
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
 * the title card are chapter.h's, the cut-scene interpreter is icon.h's, the
 * unit records are unit.h's and the map cursor is mapcur.h's.
 */
#ifndef CHINIT1_H
#define CHINIT1_H

/* Chapter 1's entry handler: brings the game into chapter 1.  Takes nothing
   and returns nothing.

   In order, and the order is the content: character 0 -- 蘭迪斯, the
   level-1 swordsman the game opens with -- is appended to the party roster;
   the chapter state is rebuilt around him; the opening cut-scene Icon00.dat
   is interpreted; the chapter title card is shown; map unit 2, the guest
   hero 索爾 the cut-scene has just deployed, is given eleven turns of
   poison and 100 current HP; and the map cursor is parked on unit 0's tile.

   The roster add comes first because the state rebuild deploys the map's
   player slots out of the roster array, and the two writes on 索爾 come
   last because the unit they land on does not exist until the cut-scene has
   deployed it.  Neither ordering is visible in any data file.

   Table slot 0. */
extern void fdps_chapter_01_init(void);
#pragma aux fdps_chapter_01_init "*" parm caller [];

/* Chapter 2's entry handler: brings the game into chapter 2.  Takes nothing
   and returns nothing.

   In order, and the order is the content: character 6 -- 尤利安, the priest
   who joins the party at the start of the chapter -- is appended to the party
   roster; the chapter state is rebuilt around the roster that add has just
   grown; the opening cut-scene Icon01.dat is interpreted; the chapter title
   card is shown; and the map cursor is parked on unit 0's tile.

   The roster add comes first because the state rebuild fills the map's player
   slots out of the roster array and stops at the roster count, so running the
   two the other way round leaves 尤利安 off the map.  Nothing here writes on
   a unit record afterwards, which is what separates this handler from chapter
   1's.

   Table slot 1. */
extern void fdps_chapter_02_init(void);
#pragma aux fdps_chapter_02_init "*" parm caller [];

/* Chapter 3's entry handler: brings the game into chapter 3.  Takes nothing
   and returns nothing.

   In order, and the order is the content: character 4 -- 亞克, the level-7
   knight who joins the party at the start of the chapter -- is appended to the
   party roster; the chapter state is rebuilt around the roster that add has
   just grown; the opening cut-scene Icon02.dat is interpreted; the chapter
   title card is shown; and the map cursor is parked on unit 0's tile.

   The roster add comes first because the state rebuild fills the map's player
   slots out of the roster array and stops at the roster count, and MAP02.DAT
   declares three player slots against a party that is three members strong
   only once this add has run.  Nothing here writes on a unit record
   afterwards.

   Table slot 2. */
extern void fdps_chapter_03_init(void);
#pragma aux fdps_chapter_03_init "*" parm caller [];

/* Chapter 4's entry handler: brings the game into chapter 4.  Takes nothing
   and returns nothing.

   In order, and the order is the original's: character 1 -- 法蓮娜 the
   魔導士 -- is appended to the party roster; the chapter state is rebuilt;
   the opening cut-scene Icon03.dat is interpreted; the chapter title card is
   shown; and the map cursor is parked on unit 0's tile.

   The add still comes before the rebuild, but chapter 4 is where that stops
   being observable: MAP03.DAT asks for three player slots and the party is
   already three strong when the chapter opens, so 法蓮娜 lands at roster slot
   3, one past the last slot the map fills, and is not one of chapter 4's map
   units either way.  She comes onto the map from the map file instead, as the
   level-8 side-2 record MAP03.DAT tags wave 3.  Nothing here writes on a unit
   record.

   Table slot 3. */
extern void fdps_chapter_04_init(void);
#pragma aux fdps_chapter_04_init "*" parm caller [];

/* Chapter 5's entry handler: brings the game into chapter 5.  Takes nothing
   and returns nothing.

   Four calls and nothing else: the chapter state is rebuilt, the opening
   cut-scene Icon04.dat is interpreted, the chapter title card is shown, and
   the map cursor is parked on unit 0's tile.  This is the plain form of the
   family -- the first handler with no fdps_roster_add_character in front of
   the rebuild and, like chapters 2 to 4, no store on a unit record behind it.

   Nobody joins the party this chapter, and that is visible on the map rather
   than merely absent from the body: MAP04.DAT asks for five player slots
   against the four members the four handlers before this one have added, so
   the fifth slot is the zeroed, retired spare fdps_build_map_unit_array
   writes for a slot with no member behind it.

   Table slot 4. */
extern void fdps_chapter_05_init(void);
#pragma aux fdps_chapter_05_init "*" parm caller [];

/* Chapter 6's entry handler: brings the game into chapter 6.  Takes nothing
   and returns nothing.

   Four calls and nothing else, the same plain form chapter 5 has: the chapter
   state is rebuilt, the opening cut-scene Icon05.dat is interpreted, the
   chapter title card is shown, and the map cursor is parked on unit 0's tile.
   Nobody joins the party and nothing is written on a unit record.

   Where chapter 5 left a slot over, this chapter fits: MAP05.DAT asks for four
   player slots and the party the first four handlers built is four members, so
   every player slot has a member behind it and none of them is the retired
   spare.

   The rebuild and the cut-scene divide the map's 32 deployment records between
   them: the rebuild's opening deploy takes the two tagged wave 0 -- one of
   which is the chapter's 友方 LV10 英雄索爾 -- and Icon05.dat's own DEPLOY_WAVE
   brings in the twenty-three tagged wave 1, so the handler returns with 29
   units on the map and the seven tagged wave 2 still to come from elsewhere.

   Table slot 5. */
extern void fdps_chapter_06_init(void);
#pragma aux fdps_chapter_06_init "*" parm caller [];

/* Chapter 7's entry handler: brings the game into chapter 7.  Takes nothing
   and returns nothing.

   In order, and the order is the original's: character 3 -- 裘娜 the 戰士,
   the LV15 warrior the strategy guide lists as this chapter's 加入 -- is
   appended to the party roster; the chapter state is rebuilt; the opening
   cut-scene Icon06.dat is interpreted; the chapter title card is shown; and
   the map cursor is parked on unit 0's tile.

   The add comes before the rebuild as it does in every handler that has one,
   but chapter 7 is the second chapter where that is not observable: MAP06.DAT
   asks for four player slots and the party is already four members strong, so
   裘娜 lands at roster slot 4, one past the last slot the map fills, and is
   not one of chapter 7's map units either way.  MAP07.DAT asks for five, so
   chapter 8 is the first map she is deployed on.

   She is on chapter 7's map as the enemy instead, and that is a different
   record: MAP06.DAT's five scripted deployments are the guide's 敵方 line --
   one side-0 level-15 character 116 and four side-0 level-14 character 86 --
   and none of the five is tagged wave 0, so the whole opposition arrives from
   the cut-scene's own two DEPLOY_WAVEs rather than from the rebuild.  Nothing
   here writes on a unit record.

   Table slot 6. */
extern void fdps_chapter_07_init(void);
#pragma aux fdps_chapter_07_init "*" parm caller [];

/* Chapter 8's entry handler: brings the game into chapter 8.  Takes nothing
   and returns nothing.

   In order, and the order is the original's: character 2 -- 費塔加 the
   魔導士 -- is appended to the party roster; the chapter state is rebuilt;
   the opening cut-scene Icon07.dat is interpreted; the chapter title card is
   shown; and the map cursor is parked on unit 0's tile.

   The add comes before the rebuild as it does in every handler that has one,
   and chapter 8 is the third chapter where that is not observable: MAP07.DAT
   asks for five player slots and the party is already five members strong, so
   費塔加 lands at roster slot 5, one past the last slot the map fills, and is
   not one of chapter 8's map units.

   He is on chapter 8's map as a side-1 ally instead, and that is a different
   record: MAP07.DAT's deployment 19 is side 1, character 2, level 15, carrying
   光之杖 and 祭司袍, and it is tagged wave 1 -- a wave nothing this handler
   runs ever deploys, so he is not on the map when the handler returns either.
   The strategy guide's 加入 line for the chapter is that record and not the
   roster record the add builds: LV15 at HP162 and MP163 against FRIAPRDA.DAT's
   own level 13 at HP150 and MP149.

   The cut-scene leaves the chapter on its own map even though it spends most
   of itself somewhere else.  ICON07.DAT switches to map 48 -- a cut-scene map
   with no player slot whose six deployments are the five party actors and
   費塔加 -- and switches back to map 7 near its end, so the array the handler
   returns with is the one that second switch rebuilt: five player slots and
   MAP07.DAT's fourteen wave-0 records, for nineteen.  Nothing here writes on a
   unit record and neither does the cut-scene.

   Table slot 7. */
extern void fdps_chapter_08_init(void);
#pragma aux fdps_chapter_08_init "*" parm caller [];

/* Chapter 9's entry handler: brings the game into chapter 9.  Takes nothing
   and returns nothing.

   In order, and the order is the original's: character 8 -- 布蘭多 the 技師 --
   and then character 9 -- 蓋亞 the 機兵 -- are appended to the party roster;
   the chapter state is rebuilt; the opening cut-scene Icon08.dat is
   interpreted; the chapter title card is shown; and the map cursor is parked
   on unit 0's tile.  Two adds is what separates this handler from every one
   before it; the rest is chapter 8's shape unchanged.

   The add comes before the rebuild as it does in every handler that has one,
   and chapter 9 is the FIRST chapter where that is observable.  MAP08.DAT asks
   for eight player slots and the party is six members strong when the chapter
   opens, so the two newcomers land at roster slots 6 and 7 -- the last two
   slots the map fills.  A rebuild that ran before them would find the roster
   count still at six and leave both slots zeroed with the retired bit set,
   which is to say 布蘭多 and 蓋亞 would not be on the map at all, and the
   chapter is lost the moment either of them dies.

   The order of the two adds is observable too, and only here.  Roster slot
   order is player-slot order and player-slot order is start-tile order, so
   character 8 takes MAP08.COD record 37 at tile (23, 12) and character 9
   record 38 at (24, 13).  Swapping the two calls swaps the tiles the two of
   them open the chapter on.

   The record each add builds is this chapter's 己方 line in the strategy
   guide, which is what separates chapter 9 from chapter 8: LV14 技師布蘭多 at
   HP167 and MP39 and LV16 機兵蓋亞 at HP240 and MP45 are FRIAPRDA.DAT and
   FRILEVUP.DAT read at those characters' own levels, with the guide's AP and
   DP its equipment on top.  No MAP08.DAT record carries either of them.

   The cut-scene spends itself on two maps of its own and puts this one back.
   ICON08.DAT switches to map 54 and then to map 55 -- cut-scene maps with no
   player slot, whose actors are level-2 stand-ins -- deploys map 55's one
   wave-1 actor, and switches back to map 8 before it ends, so the array the
   handler returns with is the one that last switch rebuilt: eight player slots
   and MAP08.DAT's twenty-four wave-0 records, for thirty-two.  Nothing here
   writes on a unit record and neither does the cut-scene.

   Table slot 8. */
extern void fdps_chapter_09_init(void);
#pragma aux fdps_chapter_09_init "*" parm caller [];


/* Chapter 10's entry handler: brings the game into chapter 10.  Takes nothing
   and returns nothing.

   Four calls and nothing else, the plain form chapters 5 and 6 have: the
   chapter state is rebuilt, the opening cut-scene Icon09.dat is interpreted,
   the chapter title card is shown, and the map cursor is parked on unit 0's
   tile.  Nobody joins the party and nothing is written on a unit record.

   The party is eight members when the chapter opens and MAP09.DAT asks for
   eight player slots, so every slot has a member behind it and none of them is
   the retired spare -- the fit chapter 6 has and chapter 5 does not.

   The cut-scene deploys nobody at all, which is true of no other handler in
   the file: ICON09.DAT switches to the cut-scene map 37, then to 38, then back
   to map 9, and carries no DEPLOY_WAVE.  So the array the handler returns with
   is the one that last switch rebuilt -- the eight player slots and the eight
   records MAP09.DAT tags wave 0, for sixteen -- and those eight are the
   strategy guide's opening 敵方 group: four LV13 步兵, two LV13 騎兵 and two
   LV15 魔導士.  The map's other forty records are the reinforcements the guide
   lists against later turns.

   Table slot 9. */
extern void fdps_chapter_10_init(void);
#pragma aux fdps_chapter_10_init "*" parm caller [];

#endif
