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

#endif
