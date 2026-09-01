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

/* Chapter 25's post-action test: a victory condition and a defeat condition of
   its own, and no shared test at all.  Takes nothing, returns nothing, and
   leaves the verdict in data_fdps_chapter_event_or_battle_end_code
   (gamedata.h).

   The victory is unit slots 12, 13 and 14 all having left the battle, tested in
   that order and short-circuiting on the first one still standing; those three
   are the chapter's three 魔戰將軍, its 勝利條件 魔戰將軍死亡, and they are the
   map's first three enemy slots because the roster is twelve deep by this
   chapter.  The defeat is unit slot 0, 蘭迪斯, its 失敗條件 蘭迪斯死亡; slot 3
   is the one absent from this map, since 己方 is 法蓮娜以外的所有人.

   Neither store is guarded on what the code already holds and the defeat test
   is not the victory's else branch: it runs on every call and its store is the
   later of the two, so an action that retires the last warlord and 蘭迪斯 at
   once ends in a Game Over, while an action that retires the last warlord after
   a chapter event has recorded a defeat clears the chapter.

   Like chapter 22's handler and unlike the rest of this file, it never calls
   fdps_battle_check_default_end_conditions (btlend.h).  It cannot: that test
   declares its victory by sweeping for a live enemy, and this chapter is won by
   killing three named units while the rest of the map still stands.

   Table slot 24. */
extern void fdps_chapter_25_post_action(void);
#pragma aux fdps_chapter_25_post_action "*" parm caller [];

/* Chapter 26's post-action test: a victory condition and two defeat conditions
   of its own, and no shared test at all.  Takes nothing, returns nothing, and
   leaves the verdict in data_fdps_chapter_event_or_battle_end_code
   (gamedata.h).

   The victory is unit slots 12, 13, 14 and 15 all having left the battle, tested
   in that order and short-circuiting on the first one still standing; those four
   are the chapter's four 魔戰將軍 -- its 勝利條件 擊倒魔戰將軍 -- and they are
   the map's first four enemy slots because the roster is twelve deep by this
   chapter, 己方 being 法蓮娜以外的所有人.

   The first defeat is unit slot 0, 蘭迪斯.  The second is unit slot 0x5b, and it
   is asked only while data_fdps_map_cell_event_triggered_flags[0x10]
   (gamedata.h) holds exactly 1 -- the one-shot latch
   fdps_chapter_26_event_deploy_waves_2_and_3 (chevt5.h) sets when it brings the
   map's reinforcements on.  Slot 0x5b does not exist before that event runs, so
   the gate is what keeps the test inside the unit array rather than a redundant
   guard, and it is an equality against 1 and not a non-zero test.

   Slot 0x5b is the LAST of 索爾's four 侍衛 and not 索爾, who stands at 0x57:
   the event deploys the map's wave 2 before its wave 3, so the seven wave-2
   enemies take 0x50..0x56 and the five wave-3 allies take 0x57..0x5b in file
   order, 索爾 first.  The chapter's stated 失敗條件 索爾死亡 is therefore not
   what the code watches, and writing it as a test on 索爾's own slot ends the
   battle on a different unit's death.

   None of the three stores is guarded on what the code already holds and none is
   another's else branch: they run in the order victory, 蘭迪斯, escort, and the
   last write wins.  So an action that retires the final warlord together with
   蘭迪斯 -- or, once the latch is set, together with slot 0x5b -- ends in a Game
   Over, while an action that retires the final warlord after a chapter event has
   recorded a defeat clears the chapter.

   Like chapters 22's and 25's handlers and unlike the rest of this file, it never
   calls fdps_battle_check_default_end_conditions (btlend.h).  It cannot: that
   test declares its victory by sweeping for a live enemy, and this chapter is won
   by killing four named units while the tower garrison still stands.

   Table slot 25. */
extern void fdps_chapter_26_post_action(void);
#pragma aux fdps_chapter_26_post_action "*" parm caller [];

/* Chapter 27's post-action test: one victory condition and one defeat condition
   of its own, and no shared test at all.  Takes nothing, returns nothing, and
   leaves the verdict in data_fdps_chapter_event_or_battle_end_code
   (gamedata.h).

   The victory is unit slot 12 having left the battle -- LV40魔導王吉歐, the
   chapter's 勝利條件 魔導王死亡 -- and the defeat is unit slot 0, 蘭迪斯, its
   失敗條件 蘭迪斯死亡.  Slot 12 is the map's first enemy slot because the
   roster is twelve deep by this chapter and 己方 is 法蓮娜以外的所有人, so slot
   3 is reserved and empty; 吉歐 is MAP26.DAT's first deployment record and its
   only level-40 one, and he is deployed in wave 0, so the slot is occupied from
   the moment the map opens.

   The four 魔戰將軍 stand at slots 13 to 16 and none of them is tested.  Killing
   them is what brings the chapter's reinforcements on, which is a separate
   event handler's business; this chapter is not the four-warlord victory
   chapter 26 is, and testing them here would clear it with the boss alive.

   Neither store is guarded on what the code already holds and the defeat test is
   not the victory's else branch: it runs on every call and its store is the later
   of the two, so an action that kills 吉歐 and 蘭迪斯 at once ends in a Game
   Over, while an action that kills 吉歐 after a chapter event has recorded a
   defeat clears the chapter.

   Like chapters 22's, 25's and 26's handlers and unlike the rest of this file,
   it never calls fdps_battle_check_default_end_conditions (btlend.h).  It
   cannot: that test declares its victory by sweeping for a live enemy, and this
   chapter is won by killing one named unit while the rest of the map still
   stands.

   Table slot 26. */
extern void fdps_chapter_27_post_action(void);
#pragma aux fdps_chapter_27_post_action "*" parm caller [];

/* Chapter 28's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 28 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test, the same shape chapters
   16, 18 and 21 have.  That matches the chapter's stated rules, which are the
   shared test's two exactly -- 勝利條件 敵人全滅 and 失敗條件 蘭迪斯死亡.

   Chapter 28 is chapter id 27 (0x1b), which is neither of the two ids -- 0x10
   and 0x15 -- the shared test singles out, so the slot it watches for the
   defeat is 0, 蘭迪斯, and not 3.

   The chapter's own scripted business is elsewhere and is not missing from
   here: the three reinforcements that arrive along the top edge at the end of
   the player phase on nine scheduled turns are keyed on the turn counter, so
   they belong to a turn event; releasing them from here would put a wave on
   the map after every single unit action instead of once a turn.  The guide
   enumerates those turns as 2, 4, 6, 7, 10, 12, 14, 16 and 18 -- an enumeration,
   not the even turns: 7 is in it and 8 is not.

   Table slot 27. */
extern void fdps_chapter_28_post_action(void);
#pragma aux fdps_chapter_28_post_action "*" parm caller [];

#endif
