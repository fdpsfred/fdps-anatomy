/* chpost2.h -- the per-chapter post-action handlers, chapters 16 to 30.
 *
 * Every entry point here is a slot of the post-action handler table based at
 * 0006028c, and the slot number is the 0-based chapter id, so this file's
 * handlers are slots 15 to 29.  The four battle dispatchers load the chapter
 * id from data_fdps_chapter_current_chapter_id (gamedata.h), scale it by four
 * and CALL through the table with nothing pushed and no stack cleanup, so
 * every handler shares one function-pointer type: no arguments, no result.
 *
 * A handler is run after a unit has finished acting and its whole job is to
 * decide whether the battle is over.  The answer is not returned; it is left
 * in data_fdps_chapter_event_or_battle_end_code (gamedata.h), where 0 means
 * the battle carries on, 1 defeat and 2 chapter cleared, and the phase loops
 * compare that global against 0 to decide whether to break out.
 *
 * A handler is not where a chapter's scripted business lives.  The turn
 * events, the reinforcement waves and the closing scene are separate handlers
 * in separate tables; only the win/lose test is here.
 *
 * Nothing here owns state.  The shared default test is btlend.h's, the unit
 * records are unit.h's and the two globals above are gamedata.h's.
 */
#ifndef CHPOST2_H
#define CHPOST2_H

/* Chapter 16's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict
   in data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 16 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test.  That matches the
   chapter's stated rules, which are the shared test's two exactly -- win when
   the enemy is wiped out, lose when 蘭迪斯 dies.

   The chapter's own scripted business is elsewhere and is not missing from
   here: the enemy groups that leave hold position when the knights fall are
   released by a turn-event handler, and the wandering smith who reforges
   蘭迪斯's sword within twenty turns is another, both keyed on the turn
   counter rather than on a unit having acted.  The victory scene and the
   advance to chapter 17 belong to chapter 16's entry in the separate
   chapter-end table at 00060304, which main dispatches only once this handler
   has left 2 in the battle-end code.

   Chapter 16 is chapter id 15, which is neither of the two ids the shared
   test singles out, so the slot it watches for the defeat is 0 and not 3.

   Table slot 15. */
extern void fdps_chapter_16_post_action(void);
#pragma aux fdps_chapter_16_post_action "*" parm caller [];

/* Chapter 17's post-action test: the game's standard end conditions, then one
   defeat condition of its own -- unit slot 3 having left the battle.  Takes
   nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h).

   The chapter's stated rules are 勝利條件 敵人全滅 and 失敗條件 法蓮娜死亡,
   and both are covered: the win is the sweep inside
   fdps_battle_check_default_end_conditions (btlend.h) and the loss is this
   handler's own test, which stores 1 with no regard for what the code already
   holds.  That is what records the defeat on the path where the shared test
   returned at its gate because a verdict was already in the code -- a clear
   put there by a chapter event loses to a retired slot 3.  Within one call the
   two tests never disagree: whenever the shared test runs its body it reaches
   its own chapter-0x10 arm and stores the same 1.

   Slot 3 is 法蓮娜: unit slot i is roster slot i and the roster is in join
   order, which by chapter 17 reads 蘭迪斯, 尤利安, 亞克, 法蓮娜, 裘娜,
   費塔加, 布蘭多, 蓋亞, 琴琴, 瑪麗安.

   Chapter 17 is chapter id 16, which is one of the two ids -- 0x10 and 0x15 --
   the shared test itself singles out, so the slot the shared test watches for
   the defeat is 3 here rather than the usual 0, and this handler's test asks
   about the same unit the shared test just asked about.  The repetition is
   load-bearing, not redundant: only the shared test's copy is gated on the
   code still being 0.

   Table slot 16. */
extern void fdps_chapter_17_post_action(void);
#pragma aux fdps_chapter_17_post_action "*" parm caller [];

/* Chapter 18's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 18 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test.  That matches the
   chapter's stated rules, which are the shared test's two exactly -- 勝利條件
   敵人全滅 and 失敗條件 蘭迪斯死亡.

   Chapter 18 is chapter id 17, which is neither of the two ids -- 0x10 and
   0x15 -- the shared test singles out, so the slot it watches for the defeat
   is 0, 蘭迪斯, and not 3.  This is the difference from the handler above it:
   chapter 17 has to name its own unit because its loss is 法蓮娜's, chapter 18
   does not because its loss is the one the shared test already watches.

   The chapter's own scripted business is elsewhere and is not missing from
   here: the flyers from the four upper windows, the knights from the two doors
   and the reinforcements from below are all turn events, keyed on the turn
   counter rather than on a unit having acted.

   Table slot 17. */
extern void fdps_chapter_18_post_action(void);
#pragma aux fdps_chapter_18_post_action "*" parm caller [];

#endif
