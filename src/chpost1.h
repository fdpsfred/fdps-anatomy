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

/* Chapter 9's post-action test: the game's standard end conditions, and then
   two defeat conditions of its own -- unit slot 6 or unit slot 7 having left
   the battle.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h).

   The two own tests share one store and short circuit: slot 7 is asked about
   only when slot 6 answered 0, and either non-zero answer writes the same 1.
   They run after fdps_battle_check_default_end_conditions (btlend.h) and not
   as an alternative to it, and the store is unguarded, so a defeat found here
   outranks a clear the shared test settled in the same call and fires even
   when the shared test returned early because a chapter event had already
   recorded a verdict.

   Slots 6 and 7 on this chapter's map are the two guests 布蘭多 and 蓋亞, in
   that order.  map08.dat fields eight player slots; the permanent party at
   the start of the chapter is six -- 蘭迪斯, 尤利安, 亞克, 法蓮娜, 裘娜,
   費塔加 -- and chapter 9's init handler appends character ids 8 and 9 to the
   roster before the battle, so they occupy the last two slots.  Slot 0,
   蘭迪斯, is the shared test's business, which completes the guide's three
   lose conditions with nothing left over for a map death script.

   Table slot 8. */
extern void fdps_chapter_09_post_action(void);
#pragma aux fdps_chapter_09_post_action "*" parm caller [];

/* Chapter 10's post-action test: the escape chapter.  Takes nothing, returns
   nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h).

   Alone among the handlers in this file it does NOT call
   fdps_battle_check_default_end_conditions (btlend.h), so emptying the enemy
   side is not a clear on this chapter and a defeat is not inherited from the
   shared test either.  Both conditions are its own:

     - a defeat, code 1, when unit slot 0 -- 蘭迪斯 -- has left the battle;
     - a clear, code 2, when all eight of the map's player slots, 0 through 7,
       are accounted for and slot 0 is still in play.

   The two are an if/else with the defeat first, so a call that completes the
   escape and retires 蘭迪斯 at once is a defeat.  A slot counts as accounted
   for when its record's pos_y (struct fdps_unit_record, unit.h) is 0x17, the
   map's bottom row, OR when fdps_unit_is_retired says it is gone: a casualty
   counts as having escaped, so the chapter stays winnable after losses.  The
   count must reach exactly eight; nothing else is examined and
   data_fdps_map_unit_count is not consulted, so enemies and any slot past 7
   have no bearing on the answer.

   Neither store is unconditional: with neither end condition met the code
   keeps the value the battle loop gave it, so a verdict a chapter event
   already recorded survives the call.

   These are the guide's chapter 10 (宗教法庭) rules, 勝利條件 戰場底部脫離
   （所有人到達戰場底部）and 失敗條件 蘭迪斯死亡.

   Table slot 9. */
extern void fdps_chapter_10_post_action(void);
#pragma aux fdps_chapter_10_post_action "*" parm caller [];

/* Chapter 11's post-action test: the game's standard end conditions, and then
   one defeat condition of its own -- unit slot 8 having left the battle.
   Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h).

   The two tests are sequential and not alternatives, and the slot-8 store is
   unguarded: it overwrites whatever
   fdps_battle_check_default_end_conditions (btlend.h) just recorded, so a
   defeat outranks a clear settled in the same call, and it fires even when
   the shared test returned early because a chapter event had already recorded
   a verdict.

   Slot 8 on this chapter's map is 琴琴, the guest the chapter brings in.
   map10.dat fields nine player slots; eight roster members stand at 0..7
   going into the chapter, and fdps_chapter_11_init appends character id 7,
   琴琴, at index 8 before the battle is built.  Slot 0, 蘭迪斯, is the shared
   test's business, and those two are the whole of the chapter's stated lose
   conditions -- every one of map10.dat's 49 deployment records is an enemy,
   so nothing is left for a map death script.  The index is a position in the
   map's unit array and nothing more: the identically shaped handlers of
   chapters 4, 5 and 6 test slot 3 and mean three different people by it.

   Table slot 10. */
extern void fdps_chapter_11_post_action(void);
#pragma aux fdps_chapter_11_post_action "*" parm caller [];

/* Chapter 12's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 12 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test, the same shape as chapters
   2's and 7's.  火神的宮殿 asks the player which of the three guardian rooms to
   enter before the battle, and the answer decides which guardian is fought and
   what the party can obtain later; it has no bearing here.  The guide gives the
   chapter one win condition and one lose condition, 敵人全滅 and 蘭迪斯死亡,
   and the shared test is both of them whichever room was chosen.  The handler
   in the table slot just before this one does add a slot-8 defeat test, and by
   this chapter the roster is long enough to have a slot at that index, so
   writing that shape here is the natural mistake and would end the battle on
   paths the original does not.

   Table slot 11. */
extern void fdps_chapter_12_post_action(void);
#pragma aux fdps_chapter_12_post_action "*" parm caller [];

/* Chapter 13's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 13 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test, the same shape as chapters
   2's, 7's and 12's.  地獄三鬥神 is given one win condition and one lose
   condition, 敵人全滅 and 蘭迪斯死亡, and the shared test is both of them.
   The three 鬥神 薩達特, 席拉 and 巴魯 are enemy deployments covered by
   敵人全滅, and the chapter fields no guest, so nothing is left over for a
   slot test here or for a map death script.  Chapter 11's handler, two table
   slots before this one, does add a slot-8 defeat test, and by this chapter
   the roster is long enough to have a slot at that index, so writing that
   shape here is the natural mistake and would end the battle on paths the
   original does not.

   Table slot 12. */
extern void fdps_chapter_13_post_action(void);
#pragma aux fdps_chapter_13_post_action "*" parm caller [];

/* Chapter 14's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 14 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test, the same shape as chapters
   2's, 7's, 12's and 13's.  天空之騎士 is given
   勝利條件 敵人全滅 and 失敗條件
   蘭迪斯或法蓮娜死亡, so unlike those four
   it has a stated lose condition the shared test does not cover -- and it is
   still not here.  The handlers of chapters 4, 5 and 6 do add a defeat test on
   unit slot 3, which on chapters 5 and 6 is 法蓮娜, and chapter
   15's, the very next table slot, adds one on slot 4; by chapter 14 the roster
   is long enough for a slot at either index to exist, so writing that shape
   here is the natural mistake and would end the battle on paths the original
   does not.

   Table slot 13. */
extern void fdps_chapter_14_post_action(void);
#pragma aux fdps_chapter_14_post_action "*" parm caller [];

#endif
