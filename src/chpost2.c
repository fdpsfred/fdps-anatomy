/* chpost2.c -- the per-chapter post-action handlers, chapters 16 to 30: the
 * win/lose test the battle loop runs after a unit has acted.
 *
 * These are slots 15 to 29 of the handler table based at 0006028c, indexed by
 * the 0-based chapter id and called only through it, so none of the four
 * battle dispatchers appears as a static caller.
 *
 * See chpost2.h for what each handler decides.  Nothing here owns state: the
 * shared default test is btlend.c's and the battle-end code it settles is
 * gamedata.h's.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "btlend.h"
#include "chpost2.h"

/* 0003acb0.  One CALL and a return, with no branch in the body at all.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003acb0..0003acb6 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003acbc is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the RET at 0003acc5, so its result
   is not used and this handler returns nothing of its own.  The verdict the
   callee leaves in data_fdps_chapter_event_or_battle_end_code is the answer,
   and the dispatchers read that global directly, immediately after the
   indirect call.

   The address reaches the dispatchers only as the dword at 000602c8, fifteen
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 15 is chapter 16.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   The chapter's two stated conditions -- the enemy wiped out and 蘭迪斯's
   death -- are both the shared test's own, so a handler that adds nothing is
   the complete rule and not an omission.  The chapter's scripted business, the
   second wave that starts when the enemy knights are gone and the wandering
   smith who reforges 蘭迪斯's sword within twenty turns, is carried by
   turn-event handlers keyed on the turn counter; writing either of them here
   would fire it once per unit action instead of once per turn. */
void fdps_chapter_16_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* 0003ad10.  The shared test, then one defeat test of this chapter's own.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003ad10..0003ad16 --
   and nothing in it is ever read, so there is no local to name.  The epilogue
   is the four bare POPs at 0003ad39..0003ad3c with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003ad3d carries no immediate.

   CALL 0x0003a2e0 at 0003ad1c has nothing pushed in front of it and no ESP
   adjustment behind it, so the shared test takes no argument, and the very
   next instruction is PUSH 0x3: EAX is not consulted between the two calls,
   so that call's result is not used here.  PUSH 0x3 / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003ad21..0003ad28 is fdps_unit_is_retired(3), the caller
   clearing its one argument, and its EAX is used -- TEST EAX,EAX / JZ
   0003ad39 at 0003ad2b is the only branch in the body, skipping the MOV
   dword ptr [0x00069da0],0x1 at 0003ad2f.

   That store looks like a duplicate and is not one.  Chapter 17's id is 0x10,
   which is one of the two ids the shared test singles out at its own CMP dword
   ptr [0x00069cf4],0x10 / JZ 0003a368, so the shared test has already asked
   fdps_unit_is_retired(3) and already stored the 1 -- but only on the path
   where the code was still 0 when it was entered.  When a chapter event has
   already recorded a verdict the shared test returns at its gate having
   examined nothing, and this store, which consults neither the code's current
   value nor what the shared test found, is then the only thing that reports
   the defeat.  So deleting it as dead, folding the two tests into an if/else,
   or copying the shared test's "only while the code is 0" guard onto it all
   change behaviour on exactly that path.  What the store does not do is
   outrank a 2 written earlier in this same call: with the code 0 on entry the
   shared test runs its whole body, and its own 0x10 arm at 0003a368 has
   already replaced that 2 with the 1 at 0003a376 before control comes back
   here, so the store then writes a 1 over a 1.  The store is distinguishable
   only when the code was already non-zero when this handler was entered.

   There is no victory test of the chapter's own, and that is not an omission:
   the guide gives 第17章 人質的危機 勝利條件 敵人全滅, which is precisely the
   sweep the shared test performs, and 失敗條件 法蓮娜死亡, which is this
   store.

   Unit index 3 is a position in this map's unit array, not a character id, and
   here it is 法蓮娜.  fdps_build_map_unit_array rebuilds unit slot i from
   roster slot i, and the roster is in join order and is never permuted, so the
   roster the chapter handlers have built by chapter 17 -- 蘭迪斯, 尤利安,
   亞克, 法蓮娜, 裘娜, 費塔加, 布蘭多, 蓋亞, 琴琴, 瑪麗安 -- puts her at 3.
   fdps_chapter_17_init parks the map cursor on the same unit 3 where
   twenty-seven of the thirty entry handlers pass 0.  Writing the argument as a
   character id, or carrying the shared test's usual slot 0 here, both reach
   蘭迪斯 instead.

   Table slot 16: the dword at 000602cc, sixteen entries into the table based
   at 0006028c, is 0003ad10, and that table entry is the function's only
   xref. */
void fdps_chapter_17_post_action(void)
{
    fdps_battle_check_default_end_conditions();
    if (fdps_unit_is_retired(3) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003ad80.  One CALL and a return, with no branch in the body at all.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003ad80..0003ad86 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003ad91..0003ad94 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003ad95 carries no immediate.  Twenty-two bytes end to end, the whole body
   size.

   CALL 0x0003a2e0 at 0003ad8c is the entire body.  Nothing is pushed in front
   of it and nothing adjusts ESP behind it, so the shared test takes no
   argument; nothing reads EAX between the CALL and the RET, so its result is
   not used and this handler returns nothing of its own.  The verdict the callee
   leaves in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly after the indirect call.

   Chapter 18's id is 17, which is neither of the two ids -- 0x10 and 0x15 --
   the shared test singles out at its own CMP dword ptr [0x00069cf4],0x10 /
   CMP ...,0x15, so the slot the shared test watches for the defeat is 0,
   蘭迪斯, and not 3.  That is the whole reason this chapter needs no test of
   its own: the guide gives 第18章 咆哮的獅王 勝利條件 敵人全滅, which is the
   sweep the shared test performs, and 失敗條件 蘭迪斯死亡, which is the shared
   test's own slot-0 store.  A store here would be a second, ungated copy of a
   decision the callee has already made -- which is what chapter 17 needs and
   this chapter does not have.

   The chapter's scripted business is elsewhere and is not missing from here:
   the flyers that appear from the four upper windows on the player's fourth,
   sixth, eighth and tenth turns, the knights from the two doors on the fifth,
   seventh, tenth and eleventh, and the reinforcements from below on the
   thirteenth are all keyed on the turn counter, so they belong to turn-event
   handlers; putting any of them here would fire it once per unit action
   instead of once per turn.

   The address reaches the dispatchers only as the dword at 000602d0, seventeen
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 17 is chapter 18. */
void fdps_chapter_18_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* The three unit indices chapter 20's handler releases on each of the first
   seventeen turns are turn + one of these bases: 12, 29 and 46, the three ADD
   immediates at 0003b16c, 0003b188 and 0003b1ad.  Each base is one below the
   first index of its run, since the earliest turn that adds to it is turn 1;
   the three runs are 17 units apart and are consecutive stretches of the map's
   enemy block, which the schedule walks three abreast from index 13 to index
   63. */
#define CHAPTER_20_RELEASE_BASE_1 0x0c
#define CHAPTER_20_RELEASE_BASE_2 0x1d
#define CHAPTER_20_RELEASE_BASE_3 0x2e

/* The last turn on which chapter 20 releases anything, straight off the CMP
   dword ptr [0x00069ce8],0x11 / JG 0x0003b1b9 at 0003b15c: the branch that
   skips the three calls is taken only for a turn counter strictly greater than
   0x11, so turn 17 still releases and turn 18 does not.  JG and not JA, so the
   comparison is signed. */
#define CHAPTER_20_LAST_RELEASE_TURN 0x11

/* 0003b150.  A turn-scheduled release of three held units, then the shared
   test.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b150..0003b156 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003b1be..0003b1c1 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003b1c2 carries no immediate.

   CMP dword ptr [0x00069ce8],0x11 / JG 0x0003b1b9 at 0003b15c is the only
   branch in the body and it guards all three calls at once; the target is the
   CALL 0x0003a2e0 at 0003b1b9, so the shared test runs on every turn whichever
   way the branch goes.  The comparison is JG rather than JA, which is the
   signed ordering data_fdps_battle_turn_counter is declared with in
   gamedata.h.

   Each of the three calls is PUSH 0x0 / MOV EAX,[0x00069ce8] / ADD EAX,<base>
   / PUSH EAX / MOV EAX,[0x00069ce8] / ADD EAX,<base> / PUSH EAX / CALL
   0x00036b60 / ADD ESP,0xc -- 0003b165, 0003b181 and 0003b19d.  The counter is
   re-loaded for each push rather than kept in a register, which is what -od
   emits and carries no meaning of its own; the two pushed indices are
   therefore always equal, so each call is an INCLUSIVE range of exactly one
   unit and no loop of the callee's runs more than once.  ADD ESP,0xc after
   each is the caller clearing three dword arguments, and nothing reads EAX
   between the CALL and the next PUSH, so none of the three results is used.

   Value 0 in the low nibble of the unit record's ai_behavior byte is the
   behaviour code that walks the unit at the nearest opposing unit; map19.dat
   deploys this map's enemies in mode 2, which fights what comes into reach but
   never advances.  So the schedule releases three held enemies per turn over
   turns 1 to 17 -- indices 13..29, 30..46 and 47..63, 51 units in all -- which
   is the batch-by-batch advance the strategy guide describes for this map.
   The callee merges rather than assigns, so the high-nibble AI flags each
   released unit carries survive.

   The three bases skip index 11 deliberately.  That index is map19.dat's
   deployment record 0, the map's only unit in behaviour mode 5, the scripted
   event walker that leaves the bottom-left chest to take the treasure at the
   top of the map; writing mode 0 over it would cancel that script, so the
   first column starts at 13 -- one above it and one above index 12 as well.
   Index 64, the last deployment record, is simply past the arithmetic's reach.
   Starting the first column at 11 or 12 to make the three columns cover the
   block evenly is the mistake this constant exists to prevent.

   The highest index the schedule ever writes is 17 + 0x2e = 63, and the map
   has 65 live units, so nothing here runs off the array.  Neither this handler
   nor the callee bounds an index against data_fdps_map_unit_count, and neither
   has to.

   CALL 0x0003a2e0 at 0003b1b9 is the shared end test, unconditional and
   argument-free, and nothing reads EAX between it and the RET, so this handler
   returns nothing of its own and adds no end condition either: chapter 20 is
   chapter id 0x13, which is neither of the two ids -- 0x10 and 0x15 -- the
   shared test singles out, so the slot it watches for the defeat is 0,
   蘭迪斯.  That matches the chapter's stated rules exactly, 勝利條件 敵人全滅
   and 失敗條件 蘭迪斯死亡.

   Table slot 19: the dword at 000602d8, nineteen entries into the table based
   at 0006028c, is 0003b150, and that table entry is the function's only
   xref. */
void fdps_chapter_20_post_action(void)
{
    if (data_fdps_battle_turn_counter <= CHAPTER_20_LAST_RELEASE_TURN) {
        fdps_object_set_field34_low_nibble_range(
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_1,
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_1, 0);
        fdps_object_set_field34_low_nibble_range(
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_2,
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_2, 0);
        fdps_object_set_field34_low_nibble_range(
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_3,
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_3, 0);
    }
    fdps_battle_check_default_end_conditions();
}

/* 0003b210.  One CALL and a return, with no branch in the body at all.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b210..0003b216 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003b221..0003b224 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003b225 carries no immediate.  Twenty-two bytes end to end, the whole body
   size.

   CALL 0x0003a2e0 at 0003b21c is the entire body.  Nothing is pushed in front
   of it and nothing adjusts ESP behind it, so the shared test takes no
   argument; nothing reads EAX between the CALL and the RET, so its result is
   not used and this handler returns nothing of its own.  The verdict the callee
   leaves in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly after the indirect call.

   Chapter 21's id is 20 (0x14), which is neither of the two ids -- 0x10 and
   0x15 -- the shared test singles out at its own CMP dword ptr [0x00069cf4],
   0x10 / CMP ...,0x15 at 0003a356 and 0003a35f, so the slot the shared test
   watches for the defeat is 0, 蘭迪斯, and not 3.  That is the whole reason
   this chapter needs no test of its own: the guide gives 第21章 地底神殿
   勝利條件 敵人全滅, which is the sweep the shared test performs, and
   失敗條件 蘭迪斯死亡, which is the shared test's own slot-0 store.  A store
   here would be a second, ungated copy of a decision the callee has already
   made -- which is what chapter 17 needs and this chapter does not have.

   The chapter's scripted business is elsewhere and is not missing from here:
   both reinforcement waves are position triggers -- the first when a unit
   reaches the junction two squares past the turn, the second when a unit
   reaches any of the standing enemy groups, which also switches the map to a
   general assault -- and they are carried by fdps_chapter_21_event_deploy_wave_1
   and fdps_chapter_21_event_deploy_wave_2.  Neither is keyed on a unit having
   acted, and putting either here would fire it after every action regardless
   of where anybody stood.

   The address reaches the dispatchers only as the dword at 000602dc, twenty
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 20 is chapter 21. */
void fdps_chapter_21_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}
