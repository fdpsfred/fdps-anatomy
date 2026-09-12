/* chend2.h -- the per-chapter end handlers, chapters 16 to 30.  Chapters 1 to
 * 11 are chend1.h and chapters 12 to 15 are chend1b.h.
 *
 * Every entry point here is a slot of the handler table based at 00060304, and
 * the slot number is the 0-based chapter id.  The table is reached indirectly:
 * the main loop loads data_fdps_chapter_current_chapter_id (gamedata.h),
 * scales it by four and CALLs through the slot with nothing pushed and no
 * stack cleanup (MOV EAX,[0x00069cf4] / LEA EAX,[EAX*0x4] / CALL dword ptr
 * [EAX + 0x60304] at 00029395..000293a1), so every handler shares one
 * function-pointer type -- no arguments, no result -- and none of them has a
 * static caller.
 *
 * A handler runs once, at the moment a chapter's battle has been won and
 * before the village phase that follows it.  Its job is to close the chapter
 * out: the writeback that banks the battle party onto the persistent roster,
 * the victory cut-scene, the revive of whoever fell, and the chapter index the
 * next phase is driven by.
 *
 * Nothing here owns state.  The roster and the revive are roster.h's, the
 * cut-scene interpreter is icon.h's, the map sweep is btlend.h's and the
 * chapter index is a gamedata.h global.
 */
#ifndef CHEND2_H
#define CHEND2_H

/* Chapter 16's end handler: closes chapter 16, 羅特帝亞突入, out and hands the
   game to chapter 17, 人質的危機.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit on the enemy side has its
   hit points zeroed and any that had not already left the field is played off
   it; the battle party is banked onto the persistent roster; the victory
   cut-scene Win15.dat is interpreted; every party member who fell is revived
   and billed for it; and the chapter index is advanced to 16, chapter 17.

   The body is chapters 12 to 14's, instruction for instruction, with its own
   script name and its own stored index -- the handlers differ in exactly two
   operands.  The sweep is the belt-and-braces step it is in chapters 12 to 14
   rather than the load-bearing one it is in chapters 3, 8 and 10: chapter 16's
   post-action test is the bare shared end condition with nothing added
   (fdps_chapter_16_post_action, chpost2.h), and that condition records a clear
   only once no unit on the enemy side is still standing, so the sweep normally
   finds that side already empty.

   Table slot 15. */
extern void fdps_chapter_16_end(void);
#pragma aux fdps_chapter_16_end "*" parm caller [];

/* Chapter 17's end handler: closes chapter 17, 人質的危機, out and hands the
   game to chapter 18, 咆哮的獅王.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit on the enemy side has its
   hit points zeroed and any that had not already left the field is played off
   it; the battle party is banked onto the persistent roster; the victory
   cut-scene Win16.dat is interpreted; every party member who fell is revived
   and billed for it; and the chapter index is advanced to 17, chapter 18.

   The body is chapter 16's, instruction for instruction, with its own script
   name and its own stored index -- the handlers differ in exactly two
   operands.  The sweep is the belt-and-braces step it is in chapters 12 to 16
   rather than the load-bearing one it is in chapters 3, 8 and 10: chapter 17
   wins on 敵人全滅, the shared end condition's own test, and
   fdps_chapter_17_post_action (chpost2.h) adds only a defeat condition on unit
   slot 3, so by the time this handler runs the enemy side is normally empty
   already.

   Table slot 16. */
extern void fdps_chapter_17_end(void);
#pragma aux fdps_chapter_17_end "*" parm caller [];

#endif
