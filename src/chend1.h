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

/* Chapter 2's end handler: closes chapter 2 out and hands the game to chapter
   3.  Takes nothing and returns nothing.

   In order, and the order is the content: the battle party is banked onto the
   persistent roster; the victory cut-scene Win01.dat is interpreted; every
   party member who fell is revived and billed for it; and the chapter index
   is advanced to 2, chapter 3.

   This is the family's plain three-step shape -- nothing is awarded before the
   writeback the way chapter 1 awards 業火 -- and, like chapter 1's, it does
   not sweep the map for surviving enemies first: chapter 2 is won only by
   retiring every one of them.

   Table slot 1. */
extern void fdps_chapter_02_end(void);
#pragma aux fdps_chapter_02_end "*" parm caller [];

/* Chapter 3's end handler: closes chapter 3 out and hands the game to chapter
   4.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit still on the enemy side
   is destroyed and played off the map; the battle party is banked onto the
   persistent roster; the victory cut-scene Win02.dat is interpreted; every
   party member who fell is revived and billed for it; and the chapter index is
   advanced to 3, chapter 4.

   It is the first handler of the family to sweep the map first, and it needs
   to: chapter 3 is cleared by retiring unit slot 4, its 魔導士, without the
   enemy side being looked at (fdps_chapter_03_post_action, chpost1.h), so the
   chapter normally ends with enemies still standing.  Chapters 1 and 2 are
   cleared only by emptying the enemy side and have no such call.

   Table slot 2. */
extern void fdps_chapter_03_end(void);
#pragma aux fdps_chapter_03_end "*" parm caller [];

/* Chapter 4's end handler: closes chapter 4 out and hands the game to chapter
   5.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit on the enemy side has its
   hit points zeroed and any that had not already left the field is played off
   it; the battle party is banked onto the persistent roster; the victory
   cut-scene Win03.dat is interpreted; every party member who fell is revived
   and billed for it; and the chapter index is advanced to 4, chapter 5.

   The body is chapter 3's, instruction for instruction, with its own script
   name and stored index.  The sweep means something different here, though:
   chapter 4 is cleared through the shared end test, which records a clear only
   when the enemy side is already empty (fdps_chapter_04_post_action,
   chpost1.h; btlend.h), so on the shipped data the sweep normally has nothing
   left to retire -- where chapter 3, which is cleared by retiring one named
   unit, normally does.

   Table slot 3. */
extern void fdps_chapter_04_end(void);
#pragma aux fdps_chapter_04_end "*" parm caller [];

/* Chapter 5's end handler: closes chapter 5 out and hands the game to chapter
   6.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit on the enemy side has its
   hit points zeroed and any that had not already left the field is played off
   it; the battle party is banked onto the persistent roster; the victory
   cut-scene Win04.dat is interpreted; every party member who fell is revived
   and billed for it; and the chapter index is advanced to 5, chapter 6.

   The body is chapter 3's and chapter 4's, instruction for instruction, with
   its own script name and stored index.  As in chapter 4 the sweep is a
   belt-and-braces step: chapter 5 is cleared through the shared end test,
   which records a clear only when the enemy side is already empty
   (fdps_chapter_05_post_action, chpost1.h; btlend.h), so on the shipped data
   it normally has nothing left to retire.

   Table slot 4. */
extern void fdps_chapter_05_end(void);
#pragma aux fdps_chapter_05_end "*" parm caller [];

#endif
