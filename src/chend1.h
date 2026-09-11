/* chend1.h -- the per-chapter end handlers, chapters 1 to 15.  Chapters 16 to
 * 30 are chend2.h.
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
 * out: whatever the chapter awards that no data file carries, the writeback
 * that banks the battle party onto the persistent roster, the victory
 * cut-scene, the revive of whoever fell, and the chapter index the next phase
 * is driven by.
 *
 * Nothing here owns state.  The roster and the revive are roster.h's, the
 * cut-scene interpreter is icon.h's, the unit records are unit.h's and the
 * chapter index is a gamedata.h global.
 */
#ifndef CHEND1_H
#define CHEND1_H

/* Chapter 1's end handler: closes chapter 1 out and hands the game to chapter
   2.  Takes nothing and returns nothing.

   In order, and the order is the content: battle unit 0 -- 蘭迪斯, the only
   party member chapter 1 has -- learns spell 0, 業火; the battle party is
   banked onto the persistent roster; the victory cut-scene Win00.dat is
   interpreted; every party member who fell is revived and billed for it; and
   the chapter index is advanced to 1, chapter 2.

   The spell comes first because the writeback memmoves the whole battle
   record over the roster record, so a bit set afterwards lands on a copy
   nothing reads.  蘭迪斯's FRIAPRDA.DAT record carries an empty spell mask,
   which makes this handler the one place in the game he acquires 業火.

   Table slot 0. */
extern void fdps_chapter_01_end(void);
#pragma aux fdps_chapter_01_end "*" parm caller [];

#endif
