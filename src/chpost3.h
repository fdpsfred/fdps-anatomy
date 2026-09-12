/* chpost3.h -- the per-chapter post-action handlers, chapters 25 to 30.
 *
 * Every entry point here is a slot of the post-action handler table based at
 * 0006028c, and the slot number is the 0-based chapter id, so this file's
 * handlers are slots 24 to 29, the last six of the thirty.  The four battle
 * dispatchers load the chapter id from data_fdps_chapter_current_chapter_id
 * (gamedata.h), scale it by four and CALL through the table with nothing
 * pushed and no stack cleanup, so every handler shares one function-pointer
 * type: no arguments, no result.
 *
 * A handler is run after a unit has finished acting and its whole job is to
 * decide whether the battle is over.  The answer is not returned; it is left
 * in data_fdps_chapter_event_or_battle_end_code (gamedata.h), where 0 means
 * the battle carries on, 1 defeat and 2 chapter cleared, and the phase loops
 * compare that global against 0 to decide whether to break out.
 *
 * What these six chapters have in common, and what separates them from
 * chpost2.h's, is that their maps all field twelve player slots: 珊 joins the
 * roster in chapter 24 and no handler of any family adds anybody after her, so
 * the unit array's enemy block starts at slot 0x0c from chapter 25 on.  That
 * is the base the 魔戰將軍 and 魔導王 indices below are read off.
 *
 * A handler is not where a chapter's scripted business lives.  The turn
 * events, the reinforcement waves and the closing scene are separate handlers
 * in separate tables; only the win/lose test is here.
 *
 * Nothing here owns state.  The shared default test is btlend.h's, the unit
 * records are unit.h's and the two globals above are gamedata.h's.
 */
#ifndef CHPOST3_H
#define CHPOST3_H

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

   Like chapter 22's handler in chpost2.h, and unlike chapters 28's and 29's
   below, it never calls fdps_battle_check_default_end_conditions (btlend.h).
   It cannot: that test declares its victory by sweeping for a live enemy, and
   this chapter is won by killing three named units while the rest of the map
   still stands.

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
   fdps_chapter_26_event_deploy_waves_2_and_3 (chevt5b.h) sets when it brings the
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

   Like chapters 22's (chpost2.h) and 25's handlers, and unlike chapters 28's
   and 29's below, it never calls fdps_battle_check_default_end_conditions
   (btlend.h).  It cannot: that test declares its victory by sweeping for a live
   enemy, and this chapter is won by killing four named units while the tower
   garrison still stands.

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

   Like chapters 22's (chpost2.h), 25's and 26's handlers, and unlike chapters
   28's and 29's below, it never calls fdps_battle_check_default_end_conditions
   (btlend.h).  It cannot: that test declares its victory by sweeping for a live
   enemy, and this chapter is won by killing one named unit while the rest of
   the map still stands.

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

/* Chapter 29's post-action test: applies the game's standard end conditions
   and nothing else.  Takes nothing, returns nothing, and leaves the verdict in
   data_fdps_chapter_event_or_battle_end_code (gamedata.h) exactly as
   fdps_battle_check_default_end_conditions (btlend.h) left it -- every enemy
   retired clears the chapter, a retired unit slot 0 is a defeat that outranks
   that clear, and a verdict already recorded is not recomputed.

   Chapter 29 adds no condition of its own, which is the whole content of this
   handler: it is a bare forward to the shared test, the same shape chapters
   16, 18, 21 and 28 have.  That matches the chapter's stated rules, which are
   the shared test's two exactly -- 勝利條件 敵人全滅 and 失敗條件 蘭迪斯死亡.

   Chapter 29 is chapter id 28 (0x1c), which is neither of the two ids -- 0x10
   and 0x15 -- the shared test singles out, so the slot it watches for the
   defeat is 0, 蘭迪斯, and not 3.

   The two 守護魔龍 are not tested and neither is any other named unit: killing
   the pair is not the chapter's victory, wiping the map is, and the dragons are
   simply the last two of the deployment because they hold position until the
   final trigger releases them.

   The chapter's own scripted business is elsewhere and is not missing from
   here: the right, lower-right and upper-middle enemy groups break cover when a
   player unit crosses the vertical line before the central junction, and the
   whole map attacks once one reaches the upper room's entrance.  Both are
   tile-triggered position events -- fdps_chapter_29_event_activate_enemy_groups
   and fdps_chapter_29_event_activate_all_enemies (chevt6.h) -- so putting
   either here would release a wave after every unit action instead of when a
   unit steps on the tile.

   Table slot 28. */
extern void fdps_chapter_29_post_action(void);
#pragma aux fdps_chapter_29_post_action "*" parm caller [];

/* Chapter 30's post-action test: one defeat condition and nothing else -- unit
   slot 0 having left the battle.  Takes nothing, returns nothing, and leaves
   the verdict in data_fdps_chapter_event_or_battle_end_code (gamedata.h).

   One of the nine handlers that do not forward to
   fdps_battle_check_default_end_conditions (btlend.h) -- chapters 03, 08, 10,
   22, 23, 25, 26 and 27 are the others -- and the last slot of the table.  It
   can put a 1 in the code and can do nothing else with it: it never writes 2
   and never clears the code back to 0, which among those nine it shares only
   with chapters 22 and 23.

   The guide gives 第30章 最終聖戰 勝利條件 擊倒平衡之神 and 失敗條件 蘭迪斯死亡.
   The defeat is this handler's whole content; the clear is not, and cannot be,
   because the chapter's reinforcements are 永遠清不完 -- two ghosts and two
   白骨戰士 reappear as fast as they are killed -- so the shared test's 敵人全滅
   sweep would never come up empty even if it were called.  Beating the third
   平衡之神 records the clear from the death-script side instead, and
   fdps_chapter_30_end then plays the ending.

   The store carries no guard on the code still being 0, so a defeat on the same
   action as a recorded clear overwrites the 2 with a 1.

   Unit slot 0 is 蘭迪斯 and chapter 30 excludes nobody from its 己方, so unlike
   chapters 22 and 25 this handler watches the slot the shared test would have
   watched anyway -- the difference between it and a bare forward is the absent
   victory, not the index.

   The chapter's own scripted business is elsewhere and is not missing from
   here: the two reinforcement pairs that appear at lower right and lower left
   the moment a unit enters a 平衡之神's attack range, and the wave that keeps
   being restocked, are position- and death-triggered chapter-script events, so
   putting either here would fire it after every unit action instead of on the
   trigger.

   Table slot 29, the last of the thirty. */
extern void fdps_chapter_30_post_action(void);
#pragma aux fdps_chapter_30_post_action "*" parm caller [];

#endif
