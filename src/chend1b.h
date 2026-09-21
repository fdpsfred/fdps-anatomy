/* chend1b.h -- the per-chapter end handlers, chapters 12 to 15.  Chapters 1 to
 * 11 are chend1.h, 16 to 24 are chend2.h and 25 to 30 are chend2b.h.
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
#ifndef CHEND1B_H
#define CHEND1B_H

/* Chapter 12's end handler: closes chapter 12, 火神的宮殿, out and hands the
   game to chapter 13.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit on the enemy side has its
   hit points zeroed and any that had not already left the field is played off
   it; the battle party is banked onto the persistent roster; the victory
   cut-scene Win11.dat is interpreted; every party member who fell is revived
   and billed for it; and the chapter index is advanced to 12, chapter 13.

   The body is chapter 3's through chapter 11's, instruction for instruction,
   with its own script name and its own stored index.  The sweep is the
   belt-and-braces step it is in chapters 4 to 7, 9 and 11 rather than the
   load-bearing one it is in chapters 3, 8 and 10: chapter 12's post-action
   test is the bare shared end condition with nothing added
   (fdps_chapter_12_post_action, chpost1.h), and that condition records a clear
   only once no unit on the enemy side is still standing, so the sweep normally
   finds that side already empty.

   Table slot 11. */
extern void fdps_chapter_12_end(void);
#pragma aux fdps_chapter_12_end "*" parm caller [];

/* Chapter 13's end handler: closes chapter 13, 地獄三鬥神, out and hands the
   game to chapter 14, 天空之騎士.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit on the enemy side has its
   hit points zeroed and any that had not already left the field is played off
   it; the battle party is banked onto the persistent roster; the victory
   cut-scene Win12.dat is interpreted; every party member who fell is revived
   and billed for it; and the chapter index is advanced to 13, chapter 14.

   The body is chapter 12's, instruction for instruction, with its own script
   name and its own stored index -- the two handlers differ in exactly two
   operands.  The sweep is the belt-and-braces step it is in chapter 12 rather
   than the load-bearing one it is in chapters 3, 8 and 10: chapter 13's
   post-action test is the bare shared end condition with nothing added
   (fdps_chapter_13_post_action, chpost1.h), and that condition records a clear
   only once no unit on the enemy side is still standing, so the sweep normally
   finds that side already empty.

   Table slot 12. */
extern void fdps_chapter_13_end(void);
#pragma aux fdps_chapter_13_end "*" parm caller [];

/* Chapter 14's end handler: closes chapter 14, 天空之騎士, out and hands the
   game to chapter 15, 要塞砲危機.  Takes nothing and returns nothing.

   In order, and the order is the content: every unit on the enemy side has its
   hit points zeroed and any that had not already left the field is played off
   it; the battle party is banked onto the persistent roster; the victory
   cut-scene Win13.dat is interpreted; every party member who fell is revived
   and billed for it; and the chapter index is advanced to 14, chapter 15.

   The body is chapter 13's, instruction for instruction, with its own script
   name and its own stored index -- the two handlers differ in exactly two
   operands.  The sweep is the belt-and-braces step it is in chapters 12 and 13
   rather than the load-bearing one it is in chapters 3, 8 and 10: chapter 14's
   post-action test is the bare shared end condition with nothing added
   (fdps_chapter_14_post_action, chpost1.h), and that condition records a clear
   only once no unit on the enemy side is still standing, so the sweep normally
   finds that side already empty.

   Table slot 13. */
extern void fdps_chapter_14_end(void);
#pragma aux fdps_chapter_14_end "*" parm caller [];

/* Chapter 15's end handler: closes chapter 15, 要塞砲危機, out and hands the
   game to chapter 16, 羅特帝亞突入.  Takes nothing and returns nothing.

   It is the one handler of this family that does NOT sweep the enemy side
   first, and the one that has a branch in it at all.

   In order: if chapter 15's duel was fought, the units the duel took off the
   map are put back on it -- battle records 0..8 have their flags byte set to
   0, and record 0x34 has its behaviour byte set to 0 while its flags byte is
   left alone; then the battle party is banked onto the persistent roster; the
   victory cut-scene Win14.dat is interpreted; every party member who fell is
   revived and billed for it; and the chapter index is advanced to 15,
   chapter 16.

   THE DUEL BLOCK IS GATED, and the gate is element 0x10 of
   data_fdps_map_cell_event_triggered_flags (gamedata.h), which
   fdps_chapter_15_event_boss_defeat (chevt3.h) raises only when the player
   accepted the duel.  A chapter won without the duel skips the block entirely
   and nothing is un-retired.

   THE BLOCK IS NOT THE DUEL EVENT'S MIRROR IMAGE.  The duel retired records
   0..8 except 4, and record 0x34, by writing 1 into byte +5 of each.  This
   handler clears byte +5 of 0..8 INCLUDING 4, and for record 0x34 it clears
   byte +0x34 -- the behaviour byte -- and not byte +5, so unit 0x34 is still
   retired when the writeback that follows decides which records to bank.

   Why there is no sweep: chapter 15 reaches victory through
   fdps_chapter_15_event_boss_defeat, whose first act is
   fdps_battle_destroy_remaining_enemies -- the map is already cleared before
   the duel can even be offered, so the sweep every other handler of the family
   opens with would have nothing left to do here.

   Table slot 14. */
extern void fdps_chapter_15_end(void);
#pragma aux fdps_chapter_15_end "*" parm caller [];

#endif
