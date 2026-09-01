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

#endif
