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

/* Chapter 18's end handler: closes chapter 18, 咆哮的獅王, out, hands the game
   to chapter 19, 蛇口之道, and on the way decides whether 蘭迪斯 is given the
   勇者徽章.  Takes nothing and returns nothing.

   THE GATE IS WHAT MAKES THIS ONE DIFFERENT, and it is the only chapter end in
   the game that hands out the 英雄 class-change route.  If 蘭迪斯 is still
   carrying the 神的聖印 and not one of the first nine party members has
   changed class, the seal is taken off him, the 勇者徽章 is put in his first
   free bag entry and the chapter plays the alternative victory scene
   Win17-1.dat; otherwise nothing is traded and the ordinary Win17.dat plays.
   The guide states the same rule for this chapter.  Nine is a literal in the
   sweep and not the live unit count, so 瑪麗安, who joins at slot 9, cannot
   deny the trade (see the note in chend2.c).

   Whichever way the gate went, the rest is the family's: every unit on the
   enemy side has its hit points zeroed and any that had not already left the
   field is played off it; the battle party is banked onto the persistent
   roster; the victory cut-scene is interpreted; every party member who fell is
   revived and billed for it; and the chapter index is advanced to 18, chapter
   19.  The trade runs BEFORE the writeback, so the bag banked onto the roster
   is the one the trade left.

   The sweep of the map is the belt-and-braces step it is in chapters 12 to 17
   rather than the load-bearing one it is in chapters 3, 8 and 10: chapter 18
   wins on 敵人全滅, the shared end condition's own test, and
   fdps_chapter_18_post_action (chpost2.h) adds nothing to it, so by the time
   this handler runs the enemy side is normally empty already.

   Table slot 17. */
extern void fdps_chapter_18_end(void);
#pragma aux fdps_chapter_18_end "*" parm caller [];

/* Chapter 19's end handler: closes chapter 19, 蛇口之道, out and hands the game
   to chapter 20.  Takes nothing and returns nothing.

   THE CHAPTER'S DUEL IS UNDONE HERE, and that is what makes this one
   different.  If 裘娜's duel was PUT to the player -- whether it was accepted
   or refused -- every battle unit carrying one of the twelve permanent roster
   character ids has its flags byte cleared and its hit points and magic points
   put back on their maxima, which takes the retired marks the duel staging
   stamped on the field off again and revives, free, anyone who genuinely fell
   during the chapter.  Nothing about that gate records which answer was given,
   so refusing the duel buys the same free recovery: see the note in chend2.c.
   The recovery runs BEFORE the party is banked, because the writeback reads
   the same flags byte to decide whether to heal.

   The rest is the family's, less one step: the battle party is banked onto the
   persistent roster; the victory cut-scene Win18.dat is interpreted; every
   party member still at 0 hit points is revived and billed for it; and the
   chapter index is advanced to 19, chapter 20.

   THE MAP IS NEVER SWEPT.  Unlike chapters 16 to 18 this handler does not call
   fdps_battle_destroy_remaining_enemies at all, so an enemy still standing
   when the chapter ends is still standing when the cut-scene plays -- chapters
   1, 2, 15 and 19 are the only handlers that leave that step out.

   Table slot 18. */
extern void fdps_chapter_19_end(void);
#pragma aux fdps_chapter_19_end "*" parm caller [];

/* Chapter 20's end handler: closes chapter 20, 迷走之隧道, out and hands the
   game to chapter 21.  Takes nothing and returns nothing.

   The family's plain shape, the same five steps chapters 16 and 17 run and in
   the same order: every enemy still standing on the battle map is swept; the
   battle party is banked onto the persistent roster; the victory cut-scene
   Win19.dat is interpreted; every party member still at 0 hit points is
   revived and billed for it; and the chapter index is advanced to 20,
   chapter 21, 地底神殿.

   NOTHING STANDS IN FRONT OF THE FIVE.  Unlike chapter 19 just before it this
   handler has no gate on a one-shot latch and no recovery of the field: the
   duel that made chapter 19's handler put the party back together is that
   chapter's business and chapter 20 stages nothing of its own on the party.

   THE SWEEP IS BELT AND BRACES.  Chapter 20's 勝利條件 is the shared end
   condition's own 敵人全滅, so the enemy side is normally empty already by the
   time this handler runs.

   Table slot 19. */
extern void fdps_chapter_20_end(void);
#pragma aux fdps_chapter_20_end "*" parm caller [];

/* Chapter 21's end handler: closes chapter 21, 地底神殿, out and hands the
   game to chapter 22.  Takes nothing and returns nothing.

   The family's plain shape, the same five steps chapters 16, 17 and 20 run and
   in the same order: every enemy still standing on the battle map is swept;
   the battle party is banked onto the persistent roster; the victory cut-scene
   Win20.dat is interpreted; every party member still at 0 hit points is
   revived and billed for it; and the chapter index is advanced to 21,
   chapter 22, 巫湯婆婆.

   NOTHING STANDS IN FRONT OF THE FIVE: no latch is read, no unit record is
   recovered and nothing is granted before the sweep.  It is
   fdps_chapter_20_end one slot back instruction for instruction, differing in
   the script name and the stored index alone.

   THE SWEEP IS BELT AND BRACES.  Chapter 21's 勝利條件 is the shared end
   condition's own 敵人全滅, so the enemy side is normally empty already by the
   time this handler runs.

   THE INDEX IT LEAVES IS ALSO AN END-CONDITION INPUT: 21 is one of the two
   chapter ids the shared end test singles out, so the chapter this handler
   selects is one whose defeat condition watches unit slot 3 rather than
   slot 0.

   Table slot 20. */
extern void fdps_chapter_21_end(void);
#pragma aux fdps_chapter_21_end "*" parm caller [];

/* Chapter 22's end handler: closes chapter 22, 巫湯婆婆, out and hands the
   game to chapter 23, 死神冥河.  Takes nothing and returns nothing.

   The family's plain shape, the same five steps chapters 16, 17, 20 and 21 run
   and in the same order: every enemy still standing on the battle map is
   swept; the battle party is banked onto the persistent roster; the victory
   cut-scene Win21.dat is interpreted; every party member still at 0 hit points
   is revived and billed for it; and the chapter index is advanced to 22,
   chapter 23.

   NOTHING STANDS IN FRONT OF THE FIVE: no latch is read, no unit record is
   recovered and nothing is granted before the sweep.  It is
   fdps_chapter_21_end one slot back instruction for instruction, differing in
   the script name and the stored index alone.

   THE SWEEP IS NOT BELT AND BRACES HERE, which is the one behavioural
   difference from the handlers it copies.  Chapter 22's 勝利條件 is 擊倒巫湯婆婆
   rather than 敵人全滅 and its post-action test declares no victory of its own,
   so the chapter clears while the boss's minions are still on the map and this
   sweep is what removes them.

   Table slot 21. */
extern void fdps_chapter_22_end(void);
#pragma aux fdps_chapter_22_end "*" parm caller [];

/* Chapter 23's end handler: closes chapter 23, 死神冥河, out and hands the
   game to chapter 24, 魔精石之秘密.  Takes nothing and returns nothing.

   THE MAP'S THIRD SIDE IS CLEARED HERE, and that is what makes this one
   different.  After the enemy side is swept, every battle unit from index 11
   up whose side byte is 1 has its flags byte set to exactly 1, which raises
   the retired bit and drops everything else the byte held.  Chapter 23's map
   carries twenty-six such units of its own plus the keepsake-ring spirit, and
   nothing else in the handler family removes them -- the enemy sweep only
   looks at side 0 -- so this loop is what empties the map before the
   cut-scene plays.  Index 11 is the map's player-slot count, so the party's
   own slots are stepped over rather than tested.

   The rest is the family's, in the family's order: every unit on the enemy
   side has its hit points zeroed and any that had not already left the field
   is played off it; the battle party is banked onto the persistent roster; the
   victory cut-scene Win22.dat is interpreted; every party member still at 0
   hit points is revived and billed for it; and the chapter index is advanced
   to 23, chapter 24.  The third-side sweep runs BEFORE the party is banked,
   because the writeback reads the same flags byte to decide whether to heal.

   THE SWEEP OF THE ENEMY SIDE IS NOT BELT AND BRACES HERE, for the same reason
   it is not in chapter 22: chapter 23 clears on one named boss rather than on
   敵人全滅, so the handler is entered with the boss's escort still standing.

   Table slot 22. */
extern void fdps_chapter_23_end(void);
#pragma aux fdps_chapter_23_end "*" parm caller [];

#endif
