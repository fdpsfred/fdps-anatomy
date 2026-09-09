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

#endif
