/* chend2b.h -- the per-chapter end handlers, chapters 25 to 30.  Chapters 1 to
 * 11 are chend1.h, 12 to 15 are chend1b.h and 16 to 24 are chend2.h.
 * Chapters 27 and 30 here are the two that can end the game instead of only
 * handing it on to the next chapter.
 *
 * Every entry point here is a slot of the handler table based at 00060304, and
 * the slot number is the 0-based chapter id.  The table is reached indirectly:
 * the main loop loads data_fdps_chapter_current_chapter_id (gamedata.h),
 * scales it by four and CALLs through the slot with nothing pushed and no
 * stack cleanup, so every handler shares one function-pointer type -- no
 * arguments, no result -- and none of them has a static caller.
 *
 * A handler runs once, at the moment a chapter's battle has been won and
 * before the village phase that follows it.
 *
 * Nothing here owns state.  The roster and the revive are roster.h's, the
 * cut-scene interpreter is icon.h's, the map sweep is btlend.h's, the bag
 * edits are unititem.h's, the message draw is text.h's and the chapter index
 * is a gamedata.h global.
 */
#ifndef CHEND2B_H
#define CHEND2B_H

/* Chapter 25's end handler: closes chapter 25 out and hands the game to
   chapter 26.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit on the enemy side has its
   hit points zeroed and any that had not already left the field is played off
   it; the battle party is banked onto the persistent roster; the victory
   cut-scene Win24.dat is interpreted; every party member who fell is revived
   and billed for it; and the chapter index is advanced to 25, chapter 26.

   The body is chapter 16's, instruction for instruction, with its own script
   name and its own stored index.  THE SWEEP IS LOAD-BEARING HERE: chapter 25
   is won by the three 魔戰將軍 in unit slots 12 to 14 leaving the field, not by
   敵人全滅 (fdps_chapter_25_post_action, chpost3.h), so the handler is
   entered with the rest of the enemy side still standing.

   Table slot 24. */
extern void fdps_chapter_25_end(void);
#pragma aux fdps_chapter_25_end "*" parm caller [];

/* Chapter 26's end handler: closes chapter 26 out and hands the game to
   chapter 27.  Takes nothing and returns nothing.

   First the gift: when 蘭迪斯 (battle unit 0) does not carry 真炎龍劍 (item
   0xa2) and his bag is not full, text entry 0x18 of the loaded chapter's
   block is drawn in the standard message colours and 炎龍劍 (item 0x62) goes
   into his first empty entry.  Either condition failing skips the draw and
   the add together.  Then, unconditionally and in this order: the enemy side
   is swept, the battle party is banked (carrying the gift onto the roster),
   the victory cut-scene Win25.dat is interpreted, the fallen are revived and
   billed, and the chapter index is set to 26, chapter 27.

   Table slot 25. */
extern void fdps_chapter_26_end(void);
#pragma aux fdps_chapter_26_end "*" parm caller [];

#endif
