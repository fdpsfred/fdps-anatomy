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

/* Chapter 4's post-action test: the game's standard end conditions, and then
   one defeat condition of its own -- unit slot 3 having left the battle.
   Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h).

   The two tests are sequential and not alternatives, and the slot-3 store is
   unguarded: it overwrites whatever
   fdps_battle_check_default_end_conditions (btlend.h) just recorded, so a
   defeat outranks a clear settled in the same call, and it fires even when
   the shared test returned early because a chapter event had already recorded
   a verdict.

   Slot 3 on this chapter's map is the guest 索爾, appended after the three
   roster members that fill slots 0..2; slot 0, 蘭迪斯, is the shared test's
   business.  The index is a position in the map's unit array and nothing
   more -- the identically shaped handlers of chapters 5 and 6 test slot 3 as
   well and there it is 法蓮娜.  The chapter's third stated lose condition,
   法蓮娜's death, is not here: on this map she is a wave-3 deployment with a
   death script, and the script runner fires it.

   Table slot 3. */
extern void fdps_chapter_04_post_action(void);
#pragma aux fdps_chapter_04_post_action "*" parm caller [];

/* Chapter 5's post-action test: the game's standard end conditions, and then
   one defeat condition of its own -- unit slot 3 having left the battle.
   Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h).

   The two tests are sequential and not alternatives, and the slot-3 store is
   unguarded: it overwrites whatever
   fdps_battle_check_default_end_conditions (btlend.h) just recorded, so a
   defeat outranks a clear settled in the same call, and it fires even when
   the shared test returned early because a chapter event had already recorded
   a verdict.

   Slot 3 on this chapter's map is 法蓮娜.  The map fields five player slots,
   so the roster fills slots 0..4 -- 蘭迪斯, 尤利安, 亞克, 法蓮娜 -- and the
   guest 索爾, the map's one wave-0 deployment, is appended after them at slot
   5.  Slot 0, 蘭迪斯, is the shared test's business.  The index is a position
   in the map's unit array and nothing more: chapter 4's identically shaped
   handler tests slot 3 as well and there it is 索爾.

   The chapter's third stated lose condition, 索爾's death, is not here: he
   carries a death script on his deployment record and the script runner fires
   it.

   Table slot 4. */
extern void fdps_chapter_05_post_action(void);
#pragma aux fdps_chapter_05_post_action "*" parm caller [];

/* Chapter 6's post-action test: the game's standard end conditions, and then
   one defeat condition of its own -- unit slot 3 having left the battle.
   Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h).

   The two tests are sequential and not alternatives, and the slot-3 store is
   unguarded: it overwrites whatever
   fdps_battle_check_default_end_conditions (btlend.h) just recorded, so a
   defeat outranks a clear settled in the same call, and it fires even when
   the shared test returned early because a chapter event had already recorded
   a verdict.

   Slot 3 on this chapter's map is 法蓮娜.  map05.dat fields four player
   slots, so the roster -- 蘭迪斯, 尤利安, 亞克, 法蓮娜, unchanged since
   chapter 4 appended her -- fills slots 0..3 exactly, and the map's wave-0
   deployments, the guest 索爾 among them, follow after it.  Slot 0, 蘭迪斯,
   is the shared test's business.  The index is a position in the map's unit
   array and nothing more: chapter 4's identically shaped handler tests slot 3
   as well and there it is 索爾.

   The chapter's third stated lose condition, 索爾's death, is not here: he
   carries a death script on his deployment record and the script runner fires
   it.

   Table slot 5. */
extern void fdps_chapter_06_post_action(void);
#pragma aux fdps_chapter_06_post_action "*" parm caller [];

/* Chapter 7's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict
   in data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 7 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test, the same shape as chapter
   2's.  The arena chapter has one win condition and one lose condition,
   敵人全滅 and 蘭迪斯死亡, and both are the shared test's already.  The three
   handlers in the table slots just before this one do add a slot-3 defeat
   test, and this chapter's roster still holds 法蓮娜 at a slot the sweep
   would reach, so writing that shape here is the natural mistake and would
   end the battle on paths the original does not.

   Table slot 6. */
extern void fdps_chapter_07_post_action(void);
#pragma aux fdps_chapter_07_post_action "*" parm caller [];

#endif
