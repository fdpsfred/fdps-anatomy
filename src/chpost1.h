/* chpost1.h -- the per-chapter post-action handlers, chapters 1 to 15.
 *
 * Every entry point here is a slot of the post-action handler table based at
 * 0006028c, and the slot number is the 0-based chapter id: the four battle
 * dispatchers load the chapter id from
 * data_fdps_chapter_current_chapter_id (gamedata.h), scale it by four and
 * CALL through the table with nothing pushed and no stack cleanup.  So they
 * all share one function-pointer type: no arguments, no result.
 *
 * A handler is run after a unit has finished acting and its whole job is to
 * decide whether the battle is over.  The answer is not returned; it is left
 * in data_fdps_chapter_event_or_battle_end_code (gamedata.h), where 0 means
 * the battle carries on, 1 defeat and 2 chapter cleared, and the phase loops
 * compare that global against 0 to decide whether to break out.
 *
 * Nothing here owns state.  The shared default test is btlend.h's, the unit
 * records are unit.h's and the two globals above are gamedata.h's.
 */
#ifndef CHPOST1_H
#define CHPOST1_H

/* Chapter 2's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict
   in data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 2 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test.  The strategy guide lists
   two lose conditions for the chapter, Randis's death and Sol's death, and
   only the first is the shared test's; the second is carried in map01.dat as
   a death script on Sol's deployment record and is fired by the death-script
   runner, not from here.  Chapter 1's sibling handler does enforce its Sol
   condition in code, so writing that shape here would be the natural mistake
   and would end the battle on paths the original does not.

   Table slot 1. */
extern void fdps_chapter_02_post_action(void);
#pragma aux fdps_chapter_02_post_action "*" parm caller [];

#endif
