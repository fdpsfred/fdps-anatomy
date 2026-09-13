/* chpost2.h -- the per-chapter post-action handlers, chapters 16 to 24.
 *
 * Every entry point here is a slot of the post-action handler table based at
 * 0006028c, and the slot number is the 0-based chapter id, so this file's
 * handlers are slots 15 to 23.  The four battle dispatchers load the chapter
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

/* Chapter 19's post-action test: an ordinary battle whose exit is a duel.
   Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) -- but it is the one
   handler in this file that can also put that verdict BACK to 0, and the one
   that speaks to the player and moves an item.

   Element 0x11 of data_fdps_map_cell_event_triggered_flags (gamedata.h) is the
   switch between its two halves and is down for most of the chapter.  While it
   is down the handler is a bare forward to
   fdps_battle_check_default_end_conditions (btlend.h): every enemy retired
   clears the chapter, a retired unit slot 0 is a defeat, and chapter 19's id,
   0x12, is neither of the two the shared test singles out, so the slot it
   watches for that defeat is 0, 蘭迪斯.  That is the chapter's stated rule set,
   勝利條件 敵人全滅 and 失敗條件 蘭迪斯死亡.

   The offer is the second half and it is an exit rite rather than an event: it
   fires on the action that has just cleared the chapter and on no other.  All
   five of the battle turn counter 20 or less, the battle-end code standing at
   2, the latch still down, unit slot 4 -- 裘娜 -- not retired, and the
   妖刀村雨 (item 0xa5) in her bag have to hold.  When they do, the map's wave 2
   is deployed as the challenger, the challenge and the question are spoken
   under FACE.CEL portrait 0x23, and fdps_prompt_two_choice (msgwin.h) takes the
   answer.

   Accepting retires every unit index 0 through 0x4c except 裘娜 -- the whole
   party, 蘭迪斯 included -- and puts the battle-end code back to 0, so the
   phase loop resumes with only the two duellists standing.  Declining says so
   and leaves the cleared code alone.  Either answer raises the latch, which is
   what makes the offer one-shot and what fdps_chapter_19_end reads to decide
   whether to un-retire and restore the roster.

   With the latch up the first half settles the duel instead of forwarding: it
   watches unit 4 and unit 0x4d, writes nothing at all while both are standing,
   and once one is down speaks the won or lost line and clears the chapter.
   裘娜's win trades the 妖刀村雨 for the 妖刀村正 (item 0xa6); her loss moves
   nothing.

   IT MUST NOT FORWARD TO THE SHARED TEST WHILE THE LATCH IS UP.  The accepted
   branch has just retired 蘭迪斯, so the shared test would force the defeat
   code 1 the moment the duel began and the duel would be an instant Game Over.

   0x4d is a hard-coded unit index and not a handle on the unit the deployment
   appended: it names the challenger only while the array already holds exactly
   0x4d units.  Clearing the chapter before the turn-6 wave has arrived appends
   him at 0x4c instead, where the retire sweep kills him and the settle test
   never fires.  That is shipped behaviour and not something the rebuild
   corrects.

   Table slot 18. */
extern void fdps_chapter_19_post_action(void);
#pragma aux fdps_chapter_19_post_action "*" parm caller [];

/* Chapter 20's post-action test: releases three of the map's held enemies on
   each of the first seventeen turns, then applies the game's standard end
   conditions.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it.

   The release is this handler's whole content beyond the shared test.  While
   data_fdps_battle_turn_counter (gamedata.h) is 17 or less -- a SIGNED
   ordering -- it clears the behaviour code of unit slots turn+12, turn+29 and
   turn+46 through fdps_object_set_field34_low_nibble_range (unit.h), each as a
   one-slot inclusive range with a value of 0.  Behaviour code 0 walks a unit
   at the nearest enemy; the map deploys these units in code 2, which fights
   what reaches them but never advances.  So turns 1 to 17 send slots 13..29,
   30..46 and 47..63 at the party, three per turn and 51 in all, and from turn
   18 the whole block is loose and the calls are skipped.  The merge keeps each
   unit's high-nibble AI flags.

   Slots 11 and 12 are never released and that is the point of the bases: slot
   11 is the map's scripted event walker, deployed in behaviour code 5 to leave
   its chest and fetch a treasure, and code 0 written over it would cancel that
   walk.  Slot 64 is past the arithmetic's reach.  63 is the highest slot the
   schedule reaches, inside the map's 65 live units.

   Chapter 20 adds no end condition of its own.  Its id is 0x13, neither of the
   two ids the shared test singles out, so the slot that test watches for the
   defeat is 0, 蘭迪斯 -- which is the chapter's stated 失敗條件, its 勝利條件
   being the 敵人全滅 the shared test sweeps for.

   The chapter's other scripted business is elsewhere and is not missing from
   here: the sword upgrade is a tile trigger and the wave-1 arrival is the
   opening IconAni script's, neither of them keyed on a unit having acted.

   Table slot 19. */
extern void fdps_chapter_20_post_action(void);
#pragma aux fdps_chapter_20_post_action "*" parm caller [];

/* Chapter 21's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 21 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test.  That matches the
   chapter's stated rules, which are the shared test's two exactly -- 勝利條件
   敵人全滅 and 失敗條件 蘭迪斯死亡.

   Chapter 21 is chapter id 20 (0x14), which is neither of the two ids -- 0x10
   and 0x15 -- the shared test singles out, so the slot it watches for the
   defeat is 0, 蘭迪斯, and not 3.  It sits directly below the second of those
   two ids, chapter 22's 0x15, which is the neighbour an off-by-one in the
   shared test's second comparison would hand this chapter's defeat to.

   The chapter's own scripted business is elsewhere and is not missing from
   here: both reinforcement waves are position triggers -- the junction two
   squares past the turn for the first, reaching any standing enemy group for
   the second, which also starts the general assault -- and both are chapter
   event handlers, keyed on where a unit stands rather than on a unit having
   acted.

   Table slot 20. */
extern void fdps_chapter_21_post_action(void);
#pragma aux fdps_chapter_21_post_action "*" parm caller [];

/* Chapter 22's post-action test: one defeat test of its own and no shared test
   at all.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) -- a 1 when unit
   slot 3 has retired, and whatever the code already held when it has not.

   This is the only handler in this file that does not forward to
   fdps_battle_check_default_end_conditions (btlend.h), so it declares no
   victory: chapter 22's 勝利條件 is 擊倒巫湯婆婆, one named boss rather than
   敵人全滅, and the clear is the scripted boss-defeat event's to write.  A
   forward added here would clear the chapter as soon as the last minion fell.

   The defeat store is unguarded, so it outranks a clear the boss event
   recorded earlier in the same action: the code is written, never read.

   Unit slot 3 is 法蓮娜, the chapter's 失敗條件 法蓮娜死亡.  Chapter 22 deploys
   蘭迪斯以外的所有人, so the slot 0 the shared test would have watched is not
   on the map at all.

   Chapter 22's id is 0x15, the second of the two the shared test singles out,
   but because nothing calls that test during this chapter its 0x15 arm never
   runs; the whole defeat rule is this handler's own store.

   Table slot 21. */
extern void fdps_chapter_22_post_action(void);
#pragma aux fdps_chapter_22_post_action "*" parm caller [];

/* Chapter 23's post-action test: two defeat conditions of its own, tested one
   inside the other, and no shared test at all.  Takes nothing, returns nothing,
   and leaves the verdict in data_fdps_chapter_event_or_battle_end_code
   (gamedata.h) -- a 1 when unit slot 3 has retired, a 1 preceded by a message
   when slot 3 is standing and unit slot 0x1f has retired, and whatever the code
   already held otherwise.

   Like chapter 22's handler it does not forward to
   fdps_battle_check_default_end_conditions (btlend.h), so it declares no
   victory: chapter 23's 勝利條件 is 擊倒死神, one named boss rather than
   敵人全滅, and the clear is the scripted boss-defeat event's to write.  A
   forward added here would clear the chapter as soon as the last minion fell.

   THE SECOND TEST IS THE FIRST ONE'S ELSE.  With slot 3 already retired the
   handler declares the defeat in silence and never looks at slot 0x1f, so
   flattening the two into "lose if either" -- two ifs, or one || -- paints
   entry 0x14 of the chapter text block on a turn the original leaves quiet.

   Both stores are unguarded, so either defeat outranks a clear the boss event
   recorded earlier in the same action: the code is written, never read.

   Unit slot 3 is 法蓮娜, the chapter's 失敗條件 法蓮娜死亡.  Chapter 23 deploys
   蘭迪斯以外的所有人, so the slot 0 the shared test would have watched is not on
   the map at all, and fdps_chapter_23_init opens the map cursor on slot 3 for
   the same reason.

   Unit slot 0x1f is one of the map's own deployed units rather than a roster
   member -- fdps_chapter_23_end treats indices 11 and up as the map's and 0
   through 10 as this chapter's eleven player units.  Which unit it is has not
   been established; the chapter's second stated 失敗條件 is 蘭迪斯從戰場上方
   消失（二十回合）, the only other loss the guide gives, but nothing here ties
   that clause to this slot.

   The message drawn on that second path is entry 0x14 of
   data_fdps_current_chapter_text_ptr (gamedata.h), painted straight onto the
   mode 13h aperture at 0xa0000 with pitch 0x140 in the standard message colours
   0xd0 / 0 / 0x6d, and the cursor fdps_draw_text hands back is discarded.

   Table slot 22. */
extern void fdps_chapter_23_post_action(void);
#pragma aux fdps_chapter_23_post_action "*" parm caller [];

/* Chapter 24's post-action test: an ordinary battle whose exit is a second
   duel, the same shape as chapter 19's above.  Takes nothing, returns nothing,
   and leaves the verdict in data_fdps_chapter_event_or_battle_end_code
   (gamedata.h) -- and like chapter 19's it is one of the two handlers in this
   file that can also put that verdict BACK to 0, speak to the player and move
   an item.

   Element 0x11 of data_fdps_map_cell_event_triggered_flags (gamedata.h) is the
   switch between its two halves and is down for most of the chapter.  While it
   is down the handler is a bare forward to
   fdps_battle_check_default_end_conditions (btlend.h): every enemy retired
   clears the chapter, a retired unit slot 0 is a defeat, and chapter 24's id,
   0x17, is neither of the two the shared test singles out, so the slot it
   watches for that defeat is 0, 蘭迪斯.

   The offer is the second half and it is an exit rite rather than an event: it
   fires on the action that has just cleared the chapter and on no other.  All
   five of the battle turn counter 25 or less, the battle-end code standing at
   2, the latch still down, unit slot 4 -- 裘娜 -- not retired, and the 妖刀村正
   (item 0xa6) in her bag have to hold.  When they do, the map's wave 7 is
   deployed as the challenger, the challenge and the question are spoken under
   FACE.CEL portrait 0x23, and fdps_prompt_two_choice (msgwin.h) takes the
   answer.  The guide states the same window in words -- 本章務必在25回合內結束,
   如此該名大刀老漢又會來單挑裘娜.

   Accepting retires every unit index up to data_fdps_map_unit_count - 2 except
   裘娜 -- the whole party, 蘭迪斯 included -- and puts the battle-end code back
   to 0, so the phase loop resumes with only the two duellists standing.
   Declining says so and leaves the cleared code alone.  Either answer raises the
   latch, which is what makes the offer one-shot and what fdps_chapter_24_end
   reads.

   With the latch up the first half settles the duel instead of forwarding: it
   watches unit 4 and unit 0x52, writes nothing at all while both are standing,
   and once one is down speaks the won or lost line and clears the chapter.
   裘娜's win trades the 妖刀村正 for the 妖刀正宗 (item 0xa7), her strongest
   weapon; her loss moves nothing.

   IT MUST NOT FORWARD TO THE SHARED TEST WHILE THE LATCH IS UP.  The accepted
   branch has just retired 蘭迪斯, so the shared test would force the defeat code
   1 the moment the duel began and the duel would be an instant Game Over.

   0x52 is a hard-coded unit index and not a handle on the unit the deployment
   appended: it names the challenger only while the array already holds exactly
   0x52 units.  The sweep is not what an early clear breaks -- its bound is the
   live count less one, so the record the deployment just appended is spared
   wherever it lands -- but the settle test is: clear the chapter before all five
   turn waves have arrived and fdps_unit_is_retired(0x52) asks about a slot past
   the array's last record instead of about him.  That is shipped behaviour and
   not something the rebuild corrects: the guide gives the result as
   若第七回合以前便結束，則兩人對戰的第一回合己方結束時，狂戰士便自動認輸,
   the challenger still on the map and forfeiting, where chapter 19's
   literal-bounded sweep makes its challenger disappear instead.

   Table slot 23. */
extern void fdps_chapter_24_post_action(void);
#pragma aux fdps_chapter_24_post_action "*" parm caller [];

#endif
