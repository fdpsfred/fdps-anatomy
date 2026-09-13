/* tests/chpost2.c -- cover for src/chpost2.c.  One section per handler, in the
 * order the handlers appear in the source, each introduced by what its
 * chapter's rules are and which instructions the cases below it pin.
 *
 * Chapter 16's post-action handler is one CALL: PUSH EBX/ESI/EDI/EBP, MOV
 * EBP,ESP, SUB ESP,0x0 at 0003acb0..0003acb6, CALL 0x0003a2e0 at 0003acbc,
 * then the four POPs and RET at 0003acc1..0003acc5.  There is no store, no
 * compare and no second call in the body, so what is worth pinning is exactly
 * two things: that the shared default end test really runs, and that nothing
 * else does.
 *
 * The expected verdicts are the shared test's, read off its assembly at
 * 0003a2e0 rather than off the emitted C: the CMP dword ptr [0x00069da0],0x0
 * / JNZ at 0003a2ec that abandons the body for a code that is already
 * non-zero, the MOV dword ptr [0x00069da0],0x2 at 0003a2f9 that writes the
 * cleared verdict up front, the CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331
 * and MOV AL,byte ptr [EAX+0x5] / AND AL,0x1 at 0003a33a that put it back to
 * 0 for a live enemy, and the PUSH 0x0 / unguarded MOV dword ptr
 * [0x00069da0],0x1 at 0003a382..0003a390 that makes a retired unit slot 0 a
 * defeat in every chapter but 0x10 and 0x15.  The 0/1/2 meanings of the code
 * are program_info/architecture.md.
 *
 * The chapter id staged throughout is 15, because the handler table based at
 * 0006028c is indexed by the 0-based chapter id and this handler is slot 15:
 * the dword at 000602c8, fifteen entries into the table, is 0003acb0.
 *
 * That id is one below the first of the two the shared test singles out, so
 * the sweep that retires every slot but 0 in turn is not only checking that
 * chapter 16 adds no condition of its own -- it is also the case that catches
 * an off-by-one in the shared test's chapter comparison, which would put slot
 * 3 in charge of the defeat instead of slot 0.
 *
 * The chapter's own conditions really are the shared test's two and nothing
 * more: the strategy guide's chapter 16 entry, 羅特帝亞突入, states 勝利條件：
 * 敵人全滅 and 失敗條件：蘭迪斯死亡.  Its two scripted events -- the second
 * wave that starts once the enemy knights are gone, and the wandering smith
 * 蘭迪斯 can reach within twenty turns -- are turn events and not postludes,
 * so neither may show up as a verdict from this handler.
 *
 * The unit array is staged here rather than read from a game file: the
 * handler takes no arguments at all, so the array global, the unit count and
 * the chapter id are its entire input.  Nothing below asserts what any of
 * those globals holds on its own -- ticket 23 owns that.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "deploy.h"
#include "chpost2.h"

/* Eight slots so every index the "no condition of its own" sweep touches has
   a record of its own. */
#define STAGE_UNITS 8

/* Side codes, from the record's side byte at offset 6. */
#define SIDE_ENEMY  0
#define SIDE_PLAYER 2

/* Bit 0 of the flags byte at offset 5 is the retirement flag. */
#define FLAG_RETIRED 0x01

/* Chapter 16 is chapter id 15: the table slot number is the 0-based id. */
#define CHAPTER_16_ID 15

/* Chapter 17 is chapter id 16, the 0x10 the shared test compares against at
   0003a356, so under this id the shared test's own defeat test asks about unit
   slot 3 and not slot 0. */
#define CHAPTER_17_ID 16

/* Slot 3 is 法蓮娜: unit slot i is roster slot i and the roster is in join
   order, so by chapter 17 it reads 蘭迪斯, 尤利安, 亞克, 法蓮娜, ... */
#define FARLENA_SLOT 3

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero every slot and publish the block, then set the chapter id and the
   incoming battle-end code.  Every unit starts as a non-retired member of
   side 0, and each case edits only the fields it is about. */
static void stage(int live_unit_count, int battle_end_code)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = live_unit_count;
    data_fdps_chapter_current_chapter_id = CHAPTER_16_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void stage_unit(int unit_index, int side, int flags)
{
    stage_units[unit_index].side = (unsigned char) side;
    stage_units[unit_index].flags = (unsigned char) flags;
}

static int end_code(void)
{
    return (int) data_fdps_chapter_event_or_battle_end_code;
}

/* The CALL is really taken: with every enemy retired the shared test's up
   front 2 survives, and a handler whose body did nothing would leave the 0 it
   was given.  This is the chapter's stated win condition, 敵人全滅. */
static void the_shared_end_test_runs(void)
{
    stage(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_16_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0, so the walk inside the shared test
   is reached through this handler and not short circuited by anything in
   front of the CALL. */
static void a_live_enemy_keeps_the_battle_going(void)
{
    stage(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_16_post_action();
    CHECK_EQ(end_code(), 0);
}

/* Chapter id 15 is neither 0x10 nor 0x15, so the shared test watches unit
   slot 0 -- 蘭迪斯, the chapter's stated 失敗條件 -- and its store carries no
   guard: the defeat stands even in the same call that emptied the enemy
   side. */
static void a_retired_randis_is_a_defeat(void)
{
    stage(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_16_post_action();
    CHECK_EQ(end_code(), 1);

    stage(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_16_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  The sweep retires each of
   slots 1 to 6 in turn with slot 7 left as a live enemy holding the battle
   open, so the only thing that could turn any of these into a non-zero code
   is a defeat test the handler does not have -- or the shared test asking
   about slot 3, which it does only for chapter ids 0x10 and 0x15 and this
   chapter's id of 15 sits directly below the first of them. */
static void no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        stage(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_16_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded by a chapter event survives the handler
   untouched: the shared test's gate returns before anything is examined, and
   this handler adds no store of its own on either side of the CALL.  Each
   value below would be overwritten by a body that ran -- the array holds a
   live enemy, which would settle the code at 0. */
static void a_recorded_verdict_is_left_alone(void)
{
    stage(2, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_16_post_action();
    CHECK_EQ(end_code(), 1);

    stage(2, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_16_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open, rather than
   being reset or cleared by an initialisation the frame does not have.  It is
   also called twice, because the dispatchers run it after every unit action
   and a handler that only behaved on its first call would still pass every
   case above. */
static void an_open_battle_stays_open(void)
{
    stage(2, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_16_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_16_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------------
 * Chapter 17's handler, 0003ad10: the shared test, then PUSH 0x3 / CALL
 * 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ 0003ad39 and the unguarded MOV
 * dword ptr [0x00069da0],0x1 at 0003ad2f.
 *
 * The chapter's stated rules are the guide's chapter 17 entry, 人質的危機:
 * 勝利條件：敵人全滅 and 失敗條件：法蓮娜死亡.  The first is the shared test's
 * sweep and the second is the store above, so what is worth pinning is the
 * boundary between the two copies of the same unit-3 test: the shared test's,
 * gated on the code still being 0 by the CMP/JNZ at 0003a2ec, and this one,
 * gated on nothing.  The cases that separate them are the ones that enter with
 * a verdict already recorded.
 *
 * Everything is staged with chapter id 16, because the table slot number is
 * the 0-based chapter id and the dword at 000602cc, sixteen entries into the
 * table based at 0006028c, is 0003ad10.
 * ------------------------------------------------------------------------ */

/* Chapter 17 is one of the two ids the shared test singles out, so its own
   staging differs from chapter 16's only in the id. */
static void stage17(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_17_ID;
}

/* 失敗條件：法蓮娜死亡.  With the battle otherwise still open -- a live enemy
   holds the sweep's verdict at 0 -- a retired slot 3 is a defeat. */
static void a_retired_farlena_is_a_defeat(void)
{
    stage17(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 1);
}

/* 勝利條件：敵人全滅.  With slot 3 still standing the shared test's up front 2
   survives to the RET: this handler has no store on the victory path. */
static void every_enemy_retired_clears_the_chapter(void)
{
    stage17(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 2);
}

/* A live enemy and a standing slot 3 leave the battle open, so neither call in
   the body writes anything on the ordinary path. */
static void a_live_enemy_and_a_live_farlena_keep_the_battle_going(void)
{
    stage17(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The action that empties the enemy side and retires slot 3 at once is a
   defeat, not a clear.  This one does not discriminate this handler's store:
   with the code 0 on entry the shared test runs its whole body, and its own
   chapter-0x10 arm has already turned its 2 into the 1 at 0003a376 before
   0003ad2f is reached, so a guarded store, an if/else and an else of the
   victory all answer 1 here too.  It is a regression case over the whole
   chain -- sweep, chapter arm and handler agreeing on 1 -- and the case that
   does discriminate the store is the next one. */
static void the_last_enemy_and_farlena_falling_together_is_a_defeat(void)
{
    stage17(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The case that separates this handler's test from the shared test's copy of
   it.  A verdict already in the code sends the shared test back at its gate
   without examining a record, so the 1 below can only come from the store at
   0003ad2f: gating that store on the code still being 0, the way the shared
   test gates its own, would leave the 2 standing. */
static void a_recorded_clear_still_loses_to_a_retired_farlena(void)
{
    stage17(4, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The other side of the same boundary: with slot 3 standing the handler stores
   nothing at all, so a verdict a chapter event recorded comes back untouched
   even though the staged array would have settled the code at 0 had the sweep
   run. */
static void a_recorded_verdict_survives_a_standing_farlena(void)
{
    stage17(4, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 2);

    stage17(4, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 1);
}

/* Slot 3 and no other slot loses this chapter -- slot 0 included, which is the
   slot every chapter but 0x10 and 0x15 watches and the one an argument of 0
   here would reach.  Each of slots 0 to 6 except 3 is retired in turn with
   slot 7 left as a live enemy holding the battle open. */
static void no_slot_but_farlena_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 0; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        if (retired_slot == FARLENA_SLOT) {
            continue;
        }
        stage17(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_17_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* The dispatchers run this after every unit action, so it has to answer the
   same way twice: neither call leaves state behind that changes the second
   verdict. */
static void the_verdict_is_stable_across_calls(void)
{
    stage17(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 0);

    stage17(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 1);
    fdps_chapter_17_post_action();
    CHECK_EQ(end_code(), 1);
}

/* ------------------------------------------------------------------------
 * Chapter 18's handler, 0003ad80: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB
 * ESP,0x0 at 0003ad80..0003ad86, CALL 0x0003a2e0 at 0003ad8c, then the four
 * POPs and RET at 0003ad91..0003ad95.  Like chapter 16's it has no store, no
 * compare and no second call, so the two things worth pinning are that the
 * shared default end test really runs and that nothing else does.
 *
 * The expected verdicts are the shared test's, read off its assembly at
 * 0003a2e0: the CMP dword ptr [0x00069da0],0x0 / JNZ at 0003a2ec that abandons
 * the body for a code that is already non-zero, the MOV dword ptr
 * [0x00069da0],0x2 at 0003a2f9 that writes the cleared verdict up front, the
 * CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 and MOV AL,byte ptr [EAX+0x5] /
 * AND AL,0x1 at 0003a33a that put it back to 0 for a live enemy, and the PUSH
 * 0x0 / unguarded MOV dword ptr [0x00069da0],0x1 at 0003a382..0003a390 that
 * makes a retired unit slot 0 a defeat in every chapter but 0x10 and 0x15.
 *
 * The chapter id staged throughout is 17, because the table is indexed by the
 * 0-based chapter id and this handler is slot 17: the dword at 000602d0,
 * seventeen entries into the table based at 0006028c, is 0003ad80, and that
 * table entry is the function's only xref.
 *
 * That id sits directly ABOVE the first of the two ids the shared test singles
 * out, which is the other side of the boundary chapter 16's sweep tests: an
 * off-by-one in the shared test's CMP ...,0x10 would hand the defeat to slot 3
 * here as well, and the sweep below is what catches it.
 *
 * The chapter's own conditions really are the shared test's two and nothing
 * more: the guide's chapter 18 entry, 咆哮的獅王, states 勝利條件：敵人全滅 and
 * 失敗條件：蘭迪斯死亡.  Its scripted business -- flyers from the four upper
 * windows on the fourth, sixth, eighth and tenth turns, knights from the two
 * doors on the fifth, seventh, tenth and eleventh, reinforcements from below on
 * the thirteenth -- is all turn events, so none of it may show up as a verdict
 * from this handler.
 * ------------------------------------------------------------------------ */

/* Chapter 18 is chapter id 17. */
#define CHAPTER_18_ID 17

static void stage18(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_18_ID;
}

/* 勝利條件：敵人全滅.  The CALL is really taken: with every enemy retired the
   shared test's up front 2 survives, and a handler whose body did nothing would
   leave the 0 it was given. */
static void chapter_18_clears_when_every_enemy_is_retired(void)
{
    stage18(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_18_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0, so the walk inside the shared test is
   reached through this handler and not short circuited by anything in front of
   the CALL. */
static void chapter_18_a_live_enemy_keeps_the_battle_going(void)
{
    stage18(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_18_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件：蘭迪斯死亡.  Chapter id 17 is neither 0x10 nor 0x15, so the shared
   test watches unit slot 0, and its store carries no guard: the defeat stands
   even in the same call that emptied the enemy side. */
static void chapter_18_a_retired_randis_is_a_defeat(void)
{
    stage18(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_18_post_action();
    CHECK_EQ(end_code(), 1);

    stage18(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_18_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  Slots 1 to 6 are retired in
   turn with slot 7 left as a live enemy holding the battle open, so the only
   thing that could turn any of these into a non-zero code is a defeat test the
   handler does not have -- or the shared test asking about slot 3, which it
   does only for chapter ids 0x10 and 0x15 and this chapter's id of 17 sits
   directly above the first of them. */
static void chapter_18_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        stage18(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_18_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded by a chapter event survives the handler untouched:
   the shared test's gate returns before anything is examined, and this handler
   adds no store of its own on either side of the CALL.  Each value below would
   be overwritten by a body that ran -- the array holds a live enemy, which
   would settle the code at 0. */
static void chapter_18_a_recorded_verdict_is_left_alone(void)
{
    stage18(2, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_18_post_action();
    CHECK_EQ(end_code(), 1);

    stage18(2, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_18_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open.  It is called
   twice because the dispatchers run it after every unit action, and a handler
   that only behaved on its first call would still pass every case above. */
static void chapter_18_an_open_battle_stays_open(void)
{
    stage18(2, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_18_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_18_post_action();
    CHECK_EQ(end_code(), 0);
}

/* --------------------------------------------------------------------------
 * Chapter 19's handler, 0003ae80.  The only one in this file that speaks to
 * the player, that moves an item, and that can put the battle-end code BACK to
 * 0 after the shared test has cleared the chapter.
 *
 * IT IS TWO HALVES WITH ONE SWITCH BETWEEN THEM, element 0x11 of
 * data_fdps_map_cell_event_triggered_flags, read at 0003ae8c and again at
 * 0003af68.  Latch down: the top half is a bare forward to
 * fdps_battle_check_default_end_conditions and the bottom half may offer the
 * duel.  Latch up: the top half settles the duel instead and the bottom half
 * is shut.  Every case below says which half it is about.
 *
 * WHAT MAKES "THE SHARED TEST DID NOT RUN" ASSERTABLE: with the latch up and
 * unit slot 0 retired, a body that forwarded would leave the defeat code 1 in
 * data_fdps_chapter_event_or_battle_end_code, because chapter 19's id is 0x12
 * and that is neither of the two ids -- 0x10 and 0x15 -- the shared test
 * singles out.  The handler must leave the sentinel instead.  That is the
 * rebuild note's claim, and it is the difference between a working duel and an
 * instant Game Over.
 *
 * THE STAGED MAP HAS NO ENEMY UNLESS A CASE PLANTS ONE.  The shared test
 * writes the cleared code up front and only a live enemy-side unit puts it
 * back, so the default staging is "the chapter has just been cleared" -- which
 * is what the offer's second gate wants and what makes the gate cases cheap.
 *
 * THE OFFER'S FIVE GATES ARE SWEPT ONE AT A TIME.  Each case closes exactly one
 * of them with the other four open, and reads the answer off the latch: the
 * offer raises it at 0003b099 on both answers, so a latch still down after the
 * call is a gate that held.  That is the only reading available, and it is
 * enough: nothing else in the handler writes that byte.
 *
 * THREE CASES RUN THE OFFER FOR REAL and they are the expensive ones.  They
 * need FIELD.VFS for fdps_deploy_wave, ICON.CEL for the sheet it opens across
 * the walk, and FACE.CEL for the portrait the panel reveal carries -- a
 * missing FIELD.VFS sends fdps_deploy_wave into fdps_wait_any_key and a missing
 * FACE.CEL ends the process at exit(1), neither of which an assertion can
 * catch, so those three skip themselves when the files are not next to the
 * executable.  Each slides the panel in and out over twelve retrace-paced
 * frames and runs a modal prompt, so each costs seconds and carries every
 * claim its path can settle.
 *
 * HOW THE PROMPT IS ANSWERED is tests/chevt3.c's feeder: a timer handler that
 * advances the game's clock and appends the case's next make code only while
 * the ring is empty, stepping through the list on the read index moving rather
 * than on ticks.  A run here contains exactly one prompt, and past the end of
 * the list the last code is held, so a run that somehow asked twice answers the
 * second the same way and fails an assertion instead of spinning forever.
 *
 * WHY THE OFFER RUNS IN VILLAGE MODE: data_fdps_village_mode_flag decides what
 * fdps_prompt_two_choice draws its frames over, and with it raised the prompt
 * lifts its backdrop off the visible page instead of composing a map scene per
 * frame.  It is a harness choice and not the chapter's: what is being pinned
 * here is which answer leads where, and the compositor is tests/mapdraw.c's.
 *
 * WHY EVERY STAGED UNIT CARRIES PORTRAIT 0x80: fdps_message_window_close
 * recomposes the scene behind the retracting panel, which walks the whole unit
 * array through fdps_draw_map_unit, and 0x80 is the id that routine drops
 * before it needs a sprite cache.
 *
 * WHICH TEXT ENTRY EACH DRAW ASKS FOR IS NOT ASSERTED, for the reason the
 * chapter 1 section of tests/chpost1.c gives: fdps_draw_text takes its whole
 * effect through pixels at the VGA aperture, keeps no state, and returns a
 * cursor this handler discards.  The six ids are literals in the instruction
 * stream -- 0x11 at 0003aee0, 0x12 at 0003af3a, 0xd at 0003afc2, 0xe at
 * 0003afef, 0x10 at 0003b025 and 0xf at 0003b089 -- and the text block is
 * staged as entries that are a lone terminator, so a draw walks one, paints
 * nothing and returns at once.  What the cases do pin about the draws is that
 * none of them stops the handler: every case runs to the end of the function.
 *
 * WHICH WAVE THE DEPLOYMENT ASKS FOR IS NOT ASSERTED EITHER.  The staged
 * deployment table holds no records at all, so fdps_deploy_wave matches nothing
 * whatever wave it is given and the unit array keeps the 0x4e records these
 * cases index; the alternative is the whole chapter 17 deployment fixture, and
 * what it would buy is a literal -- PUSH 0x2 at 0003af9f -- that is already
 * plain in the disassembly.  What IS asserted is that the deployment ran at
 * all: fdps_deploy_wave frees the placement table it loaded and leaves
 * data_fdps_map_spawn_pos_table_ptr NULL, so a pointer parked non-NULL before
 * the call and found NULL after it is the call having happened.
 * -------------------------------------------------------------------------- */

/* Chapter 19 is chapter id 0x12: the table slot number is the 0-based id, and
   the dword at 000602d4, eighteen entries into the table based at 0006028c, is
   0003ae80.  It is neither of the two ids the shared test singles out, so the
   slot that test watches for the defeat is 0, 蘭迪斯.  It is also the map
   number fdps_deploy_wave formats into "map%02d.cod", and MAP18.COD is a
   member of FIELD.VFS. */
#define CH19_CHAPTER_ID 0x12

/* The latch element, byte ptr [0x000640e9] -- element 0x11 of the 32-entry
   array based at 0x000640d8. */
#define CH19_LATCH_SLOT 0x11

/* The two duellists, PUSH 0x4 and PUSH 0x4d, and how many records the array is
   staged with: one more than the highest index the handler touches. */
#define CH19_JUNA_SLOT 4
#define CH19_CHALLENGER_SLOT 0x4d
#define CH19_UNITS 0x4e

/* The bound of the accepted branch's retire sweep, CMP dword ptr
   [EBP + -0x8],0x4d / JL at 0003b03c: indices 0 through 0x4c. */
#define CH19_SWEEP_COUNT 0x4d

/* The two swords, and two other item ids that are neither of them so that
   "only the 妖刀村雨 was taken" is readable. */
#define CH19_MURASAME 0xa5
#define CH19_MURAMASA 0xa6
#define CH19_OTHER_ITEM_1 0x50
#define CH19_OTHER_ITEM_2 0x51

/* The last turn the offer is still made on and the first it is not, either
   side of CMP dword ptr [0x00069ce8],0x14 / JG at 0003af54. */
#define CH19_LAST_TURN 0x14
#define CH19_FIRST_LATE_TURN 0x15

/* An inventory entry nobody is carrying -- flag bit 0x80 is what
   fdps_unit_item_count reads as empty and the stale id beside it is the 0xff a
   deployment leaves -- and one that is carried but not equipped. */
#define CH19_INVENTORY_ENTRIES 8
#define CH19_EMPTY_FLAG 0x80
#define CH19_EMPTY_ID 0xff
#define CH19_CARRIED_FLAG 0x00

/* The flags byte every staged unit starts with: the has-acted bit alone, which
   leaves the retired bit clear.  It is what makes the retire sweep's store
   readable as a store -- a byte that came out 0x81 would be an OR and a byte
   that came out 1 is the whole-byte MOV at 0003b064. */
#define CH19_STAGED_FLAGS 0x80
#define CH19_RETIRED_FLAGS 1

/* The portrait id fdps_draw_map_unit returns on before it needs a sprite
   cache, which is what lets fdps_message_window_close recompose the scene over
   a staged array. */
#define CH19_PORTRAIT_NO_MAP_SPRITE 0x80

/* Side codes, from the record's side byte at offset 6: the shared test's sweep
   only looks at side 0. */
#define CH19_SIDE_ENEMY 0
#define CH19_SIDE_PLAYER 2

/* Any non-zero hit points; what they buy is that nothing the compositor
   touches thinks a staged unit is mid-death. */
#define CH19_LIVE_HP 100

/* The chapter text block: 0x13 entries, one past the highest id the handler
   asks for, every one of them pointing at the same lone terminator. */
#define CH19_TEXT_IDS 0x13
#define CH19_TEXT_EMPTY_AT 0x40
#define CH19_TEXT_BLOCK_BYTES (CH19_TEXT_EMPTY_AT + 2)
#define CH19_TEXT_END (-1)

/* The battle-end code parked before each run.  0 is not a value either half
   writes on the paths that are supposed to leave the code alone, so "left
   alone" and "written" are told apart. */
#define CH19_END_OPEN 0
#define CH19_END_DEFEAT 1
#define CH19_END_CLEARED 2

/* Where the deployment table's record count sits in the MAP%02d.DAT block,
   MOV AL,byte ptr [EAX+0x2] at 000238bb, and how many bytes in front of the
   records it is.  The count is staged at 0, so fdps_deploy_wave's walk matches
   nothing and the unit array is left exactly as these cases built it. */
#define CH19_SPAWN_TABLE_COUNT_OFFSET 2
#define CH19_SPAWN_TABLE_RECORD_BASE 0x83

/* The mode the offer is run in, the timer vector the feeder takes over, and
   the make codes the prompt answers to: Enter confirms the highlighted cell,
   which is the left one on entry, the right arrow moves to the other cell, and
   Esc cancels with -1 (msgwin.h). */
#define CH19_MODE_TEXT 0x03
#define CH19_MODE_320X200X256 0x13
#define CH19_TIMER_VECTOR 8
#define CH19_KEY_ESC 0x01
#define CH19_KEY_ENTER 0x1c
#define CH19_KEY_RIGHT 0x4d
#define CH19_KEYS_MAX 2

/* The Message.cel stand-in: one 302 x 73 sprite encoded as five fill runs per
   row, because a fill run cannot be longer than 64 pixels. */
#define CH19_PANEL_W 302
#define CH19_PANEL_H 73
#define CH19_PANEL_FILL_MAX 64
#define CH19_PANEL_SEGMENTS 5
#define CH19_PANEL_LAST_SEGMENT_W 46
#define CH19_PANEL_ROW_BYTES (CH19_PANEL_SEGMENTS * 2)
#define CH19_PANEL_STREAM_AT 0x40
#define CH19_PANEL_SHEET_BYTES (CH19_PANEL_STREAM_AT \
                                + CH19_PANEL_H * CH19_PANEL_ROW_BYTES)
#define CH19_PANEL_COLOR 0x40

/* The Shadow.cel stand-in the prompt draws its two option cells out of:
   fourteen 24 x 24 sprites, one fill run per row, sprite i filled with i. */
#define CH19_SHADOW_SPRITES 14
#define CH19_SHADOW_SPRITE_W 24
#define CH19_SHADOW_SPRITE_H 24
#define CH19_SHADOW_STREAM_BYTES (CH19_SHADOW_SPRITE_H * 2)
#define CH19_SHADOW_STREAM_AT 0x50
#define CH19_SHADOW_SHEET_BYTES (CH19_SHADOW_STREAM_AT \
                                 + CH19_SHADOW_SPRITES \
                                   * CH19_SHADOW_STREAM_BYTES)

/* The .CEL header fields both fixtures carry, and a table position neither
   reader may consult: both hardwire the table at 0x0f. */
#define CH19_CEL_TABLE_AT 0x0f
#define CH19_CEL_DECOY_TABLE_AT 0x100
#define CH19_CEL_VERSION 1
#define CH19_CEL_PIXEL_FORMAT 2

/* The three files the offer cases cannot be run without, and the smallest
   ICON.CEL that fdps_cache_cel_sprite_group's fixed 0x2970-byte bite out of
   the offset table stays inside. */
#define CH19_ICON_SHEET "ICON.CEL"
#define CH19_FIELD_ARCHIVE "FIELD.VFS"
#define CH19_FACE_SHEET "FACE.CEL"
#define CH19_ICON_LEAST_BYTES (15L + 0x2970L)

static struct fdps_unit_record ch19_units[CH19_UNITS];
static unsigned char ch19_text_block[CH19_TEXT_BLOCK_BYTES];
static unsigned char ch19_spawn_table[CH19_SPAWN_TABLE_RECORD_BASE];
static unsigned char ch19_panel_sheet[CH19_PANEL_SHEET_BYTES];
static unsigned char ch19_shadow_sheet[CH19_SHADOW_SHEET_BYTES];
static unsigned char ch19_spawn_pos_decoy;
static unsigned char ch19_keys[CH19_KEYS_MAX];
static volatile int ch19_key_count;
static volatile int ch19_keys_read;
static volatile int ch19_last_head;
static void (__interrupt __far *ch19_saved_timer)();
static int ch19_sheets_built = 0;
static int ch19_files_checked = 0;
static int ch19_files_ready = 0;

static struct fdps_unit_record *ch19_unit(int unit_index)
{
    return &ch19_units[unit_index];
}

/* Everything both halves read that is not a file: the unit array with nobody
   retired, nobody on the enemy side and nobody carrying anything, the text
   block, the empty deployment table, the latch and the two globals the gates
   compare. */
static void ch19_stage(int battle_turn, int latch, int battle_end_code)
{
    int i;
    int entry;
    int text_id;

    memset(ch19_units, 0, sizeof(ch19_units));
    for (i = 0; i < CH19_UNITS; i++) {
        ch19_units[i].side = (unsigned char) CH19_SIDE_PLAYER;
        ch19_units[i].flags = (unsigned char) CH19_STAGED_FLAGS;
        ch19_units[i].portrait_id = (unsigned char) CH19_PORTRAIT_NO_MAP_SPRITE;
        ch19_units[i].hp_current = (short) CH19_LIVE_HP;
        for (entry = 0; entry < CH19_INVENTORY_ENTRIES; entry++) {
            ch19_units[i].inventory_slots[entry * 2] = CH19_EMPTY_FLAG;
            ch19_units[i].inventory_slots[entry * 2 + 1] = CH19_EMPTY_ID;
        }
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch19_units;
    data_fdps_map_unit_count = CH19_UNITS;

    memset(ch19_text_block, 0, (size_t) CH19_TEXT_BLOCK_BYTES);
    *(short *) (ch19_text_block + CH19_TEXT_EMPTY_AT) = (short) CH19_TEXT_END;
    for (text_id = 0; text_id < CH19_TEXT_IDS; text_id++) {
        *(short *) (ch19_text_block + text_id * 2) = (short) CH19_TEXT_EMPTY_AT;
    }
    data_fdps_current_chapter_text_ptr = ch19_text_block;

    memset(ch19_spawn_table, 0, sizeof(ch19_spawn_table));
    ch19_spawn_table[CH19_SPAWN_TABLE_COUNT_OFFSET] = 0;
    data_fdps_tile_event_data_table_ptr = ch19_spawn_table;

    for (i = 0; i < 32; i++) {
        data_fdps_map_cell_event_triggered_flags[i] = 0;
    }
    data_fdps_map_cell_event_triggered_flags[CH19_LATCH_SLOT] =
        (unsigned char) latch;

    data_fdps_chapter_current_chapter_id = CH19_CHAPTER_ID;
    data_fdps_battle_turn_counter = battle_turn;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void ch19_retire(int unit_index)
{
    ch19_units[unit_index].flags = (unsigned char) CH19_RETIRED_FLAGS;
}

/* One live or retired enemy-side unit, which is what decides whether the shared
   test leaves the cleared code standing. */
static void ch19_plant_enemy(int unit_index, int retired)
{
    ch19_units[unit_index].side = (unsigned char) CH19_SIDE_ENEMY;
    if (retired) {
        ch19_units[unit_index].flags = (unsigned char) CH19_RETIRED_FLAGS;
    }
}

/* Put an item in one of a unit's eight entries, packed from the front the way
   fdps_unit_find_item_slot's 0..count-1 sweep needs. */
static void ch19_give(int unit_index, int entry, int item_id)
{
    ch19_units[unit_index].inventory_slots[entry * 2] = CH19_CARRIED_FLAG;
    ch19_units[unit_index].inventory_slots[entry * 2 + 1] =
        (unsigned char) item_id;
}

static int ch19_entry_flag(int unit_index, int entry)
{
    return (int) ch19_units[unit_index].inventory_slots[entry * 2];
}

static int ch19_entry_id(int unit_index, int entry)
{
    return (int) ch19_units[unit_index].inventory_slots[entry * 2 + 1];
}

/* How many of the eight occupied entries hold that id, which is how a case says
   an item was handed over, taken away, or handed over twice. */
static int ch19_count_item(int unit_index, int item_id)
{
    int entry;
    int found;

    found = 0;
    for (entry = 0; entry < CH19_INVENTORY_ENTRIES; entry++) {
        if (ch19_entry_flag(unit_index, entry) != CH19_EMPTY_FLAG
                && ch19_entry_id(unit_index, entry) == item_id) {
            found++;
        }
    }
    return found;
}

static int ch19_latch(void)
{
    return (int) data_fdps_map_cell_event_triggered_flags[CH19_LATCH_SLOT];
}

static void ch19_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void ch19_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static void ch19_cel_header(unsigned char *sheet, int width, int height,
                            int sprites)
{
    sheet[0] = 'C';
    sheet[1] = 'E';
    sheet[2] = 'L';
    ch19_u16(sheet, 0x03, CH19_CEL_VERSION);
    ch19_u16(sheet, 0x05, CH19_CEL_DECOY_TABLE_AT);
    ch19_u16(sheet, 0x07, (unsigned int) width);
    ch19_u16(sheet, 0x09, (unsigned int) height);
    ch19_u16(sheet, 0x0b, (unsigned int) sprites);
    ch19_u16(sheet, 0x0d, CH19_CEL_PIXEL_FORMAT);
}

/* The panel and the option sheets.  Neither changes between cases, so they are
   built once. */
static void ch19_build_sheets(void)
{
    int row;
    int segment;
    int cursor;
    int run;
    int sprite;
    int stream_at;

    if (ch19_sheets_built) {
        return;
    }
    ch19_sheets_built = 1;

    memset(ch19_panel_sheet, 0, (size_t) CH19_PANEL_SHEET_BYTES);
    ch19_cel_header(ch19_panel_sheet, CH19_PANEL_W, CH19_PANEL_H, 1);
    ch19_u32(ch19_panel_sheet, CH19_CEL_TABLE_AT,
             (unsigned long) CH19_PANEL_STREAM_AT);
    ch19_u32(ch19_panel_sheet, CH19_CEL_TABLE_AT + 4,
             (unsigned long) CH19_PANEL_SHEET_BYTES);
    for (row = 0; row < CH19_PANEL_H; row++) {
        cursor = CH19_PANEL_STREAM_AT + row * CH19_PANEL_ROW_BYTES;
        for (segment = 0; segment < CH19_PANEL_SEGMENTS; segment++) {
            if (segment == CH19_PANEL_SEGMENTS - 1) {
                run = CH19_PANEL_LAST_SEGMENT_W;
            } else {
                run = CH19_PANEL_FILL_MAX;
            }
            ch19_panel_sheet[cursor] = (unsigned char) (run - 1);
            ch19_panel_sheet[cursor + 1] = CH19_PANEL_COLOR;
            cursor += 2;
        }
    }

    memset(ch19_shadow_sheet, 0, (size_t) CH19_SHADOW_SHEET_BYTES);
    ch19_cel_header(ch19_shadow_sheet, CH19_SHADOW_SPRITE_W,
                    CH19_SHADOW_SPRITE_H, CH19_SHADOW_SPRITES);
    for (sprite = 0; sprite < CH19_SHADOW_SPRITES; sprite++) {
        stream_at = CH19_SHADOW_STREAM_AT + sprite * CH19_SHADOW_STREAM_BYTES;
        ch19_u32(ch19_shadow_sheet, CH19_CEL_TABLE_AT + sprite * 4,
                 (unsigned long) stream_at);
        for (row = 0; row < CH19_SHADOW_SPRITE_H; row++) {
            ch19_shadow_sheet[stream_at + row * 2] =
                (unsigned char) (CH19_SHADOW_SPRITE_W - 1);
            ch19_shadow_sheet[stream_at + row * 2 + 1] = (unsigned char) sprite;
        }
    }
    ch19_u32(ch19_shadow_sheet, CH19_CEL_TABLE_AT + CH19_SHADOW_SPRITES * 4,
             (unsigned long) CH19_SHADOW_SHEET_BYTES);
}

static void ch19_check_files(void)
{
    FILE *fp;
    long size;

    if (ch19_files_checked) {
        return;
    }
    ch19_files_checked = 1;

    fp = fopen(CH19_ICON_SHEET, "rb");
    if (fp == NULL) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size < CH19_ICON_LEAST_BYTES) {
        return;
    }

    fp = fopen(CH19_FIELD_ARCHIVE, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);

    fp = fopen(CH19_FACE_SHEET, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);

    ch19_files_ready = 1;
}

/* The player.  It advances the game's clock like the real timer handler and
   appends the case's next make code the way fdps_keyboard_isr does, but only
   while the ring is empty, and it steps through the list on the read index
   moving rather than on ticks -- only fdps_read_keyboard_queue moves that. */
static void __interrupt __far ch19_timer_isr(void)
{
    int slot;
    int next_key;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_scancode_queue_head != ch19_last_head) {
        ch19_last_head = data_fdps_input_scancode_queue_head;
        ch19_keys_read++;
    }

    if (data_fdps_input_scancode_queue_head
            == data_fdps_input_scancode_queue_write_index) {
        next_key = ch19_keys_read;
        if (next_key >= ch19_key_count) {
            next_key = ch19_key_count - 1;
        }
        slot = data_fdps_input_scancode_queue_write_index;
        data_fdps_input_scancode_queue[slot] = ch19_keys[next_key];
        slot++;
        if (slot == SCANCODE_QUEUE_LEN) {
            slot = 0;
        }
        data_fdps_input_scancode_queue_write_index = slot;
    }

    _chain_intr(ch19_saved_timer);
}

static void ch19_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole firing of the offer: the sheets the panel and the prompt read, an
   empty scene so the close's recomposition draws nothing but the staged units,
   the placement pointer parked non-NULL so the deployment is readable off it,
   the adapter in mode 13h, the feeder installed, and text mode back before
   anything is asserted so a failure prints on a readable screen. */
static void ch19_run_offer(int first_key, int second_key)
{
    ch19_build_sheets();

    data_fdps_message_window_sheet_ptr = ch19_panel_sheet;
    data_fdps_shadow_sprite_sheet_ptr = ch19_shadow_sheet;
    data_fdps_scene_layer_count = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_map_spawn_pos_table_ptr = &ch19_spawn_pos_decoy;

    ch19_keys[0] = (unsigned char) first_key;
    ch19_keys[1] = (unsigned char) second_key;
    ch19_key_count = CH19_KEYS_MAX;
    ch19_keys_read = 0;
    ch19_last_head = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;

    data_fdps_village_mode_flag = 1;
    ch19_set_mode(CH19_MODE_320X200X256);
    ch19_saved_timer = _dos_getvect(CH19_TIMER_VECTOR);
    _dos_setvect(CH19_TIMER_VECTOR, ch19_timer_isr);

    fdps_chapter_19_post_action();

    _dos_setvect(CH19_TIMER_VECTOR, ch19_saved_timer);
    ch19_set_mode(CH19_MODE_TEXT);
    data_fdps_village_mode_flag = 0;

    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
}

/* The record fields these cases read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if
   the layout were wrong. */
static void chapter_19_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 7);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
}

/* Latch down, and the CALL at 0003ae95 is really taken: with the enemy side
   wiped out the shared test's up front 2 survives, and a body that did nothing
   would leave the 0 it was given.  The offer does not follow, because 裘娜 is
   carrying nothing -- the fifth gate. */
static void chapter_19_the_shared_end_test_runs(void)
{
    ch19_stage(1, 0, CH19_END_OPEN);
    ch19_plant_enemy(10, 1);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_latch(), 0);
}

/* One live enemy puts the code back to 0 inside the shared test, so the walk in
   there is reached through this handler and not short circuited in front of the
   CALL. */
static void chapter_19_a_live_enemy_keeps_the_battle_going(void)
{
    ch19_stage(1, 0, CH19_END_OPEN);
    ch19_plant_enemy(10, 0);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_OPEN);
    CHECK_EQ(ch19_latch(), 0);
}

/* Chapter 19's id is 0x12, neither of the two the shared test singles out, so
   the slot that test watches for the defeat is 0 -- 蘭迪斯, the chapter's
   stated 失敗條件 -- and its store carries no guard. */
static void chapter_19_a_retired_randis_is_a_defeat(void)
{
    ch19_stage(1, 0, CH19_END_OPEN);
    ch19_plant_enemy(10, 0);
    ch19_retire(0);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_DEFEAT);
    CHECK_EQ(ch19_latch(), 0);
}

/* No slot but 0 ends the battle through the latch-down half.  Every other index
   the array holds is retired on its own with TWO live enemies standing -- two,
   so that the index being retired is never also the map's last live enemy and
   the shared test's own clear cannot be mistaken for a condition of this
   handler's -- and the answer has to stay 0 every time -- which is also what says this handler adds
   no end condition of its own while the latch is down. */
static void chapter_19_no_other_slot_ends_the_battle(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < CH19_UNITS; retired_slot++) {
        ch19_stage(1, 0, CH19_END_OPEN);
        ch19_plant_enemy(10, 0);
        ch19_plant_enemy(11, 0);
        ch19_retire(retired_slot);
        fdps_chapter_19_post_action();
        CHECK_EQ(end_code(), CH19_END_OPEN);
        CHECK_EQ(ch19_latch(), 0);
    }
}

/* A verdict already recorded is the shared test's own gate and is not
   recomputed, and it closes the offer's second gate as well: a 1 in the code is
   not the 2 the CMP at 0003af5d asks for. */
static void chapter_19_a_recorded_defeat_is_left_alone(void)
{
    ch19_stage(1, 0, CH19_END_DEFEAT);
    ch19_plant_enemy(10, 0);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_DEFEAT);
    CHECK_EQ(ch19_latch(), 0);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 1);
}

/* The first gate: the turn counter 0x14 or less.  Three late turns are put
   through -- the first one outside the window and two well past it -- because a
   rebuild that had the compare as an equality would pass on 21 alone.  The
   chapter is cleared each time and the offer is not made. */
static void chapter_19_a_late_turn_withholds_the_offer(void)
{
    static int late_turns[3] = {CH19_FIRST_LATE_TURN, 0x1b, 100};
    int i;

    for (i = 0; i < 3; i++) {
        ch19_stage(late_turns[i], 0, CH19_END_OPEN);
        ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
        fdps_chapter_19_post_action();
        CHECK_EQ(end_code(), CH19_END_CLEARED);
        CHECK_EQ(ch19_latch(), 0);
        CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 1);
        CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 0);
    }
}

/* The second gate: the code standing at 2.  With the battle still open the
   offer is not made even on turn 1 with everything else in place, which is what
   makes this an exit rite -- the challenge comes with the chapter's last
   action and with no other. */
static void chapter_19_an_open_battle_withholds_the_offer(void)
{
    ch19_stage(1, 0, CH19_END_OPEN);
    ch19_plant_enemy(10, 0);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_OPEN);
    CHECK_EQ(ch19_latch(), 0);
}

/* The fourth gate: 裘娜 standing.  The gate reads bit 0 of unit 4's flags byte
   and nothing else, so the retired bit is put up on its own and then under the
   has-acted bit as well, and both refuse; the has-acted bit on its own is what
   every other case here runs with and it does not refuse. */
static void chapter_19_a_retired_juna_withholds_the_offer(void)
{
    static int retired_flags[2] = {CH19_RETIRED_FLAGS, 0x81};
    int i;

    for (i = 0; i < 2; i++) {
        ch19_stage(CH19_LAST_TURN, 0, CH19_END_OPEN);
        ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
        ch19_units[CH19_JUNA_SLOT].flags = (unsigned char) retired_flags[i];
        fdps_chapter_19_post_action();
        CHECK_EQ(end_code(), CH19_END_CLEARED);
        CHECK_EQ(ch19_latch(), 0);
    }
}

/* The fifth gate: the 妖刀村雨 in her bag.  An empty bag refuses, and so does a
   bag holding the 妖刀村正 the duel would have given her -- which is what stops
   the offer being made twice over a reload, and is the only reason the item id
   in the gate matters. */
static void chapter_19_without_the_murasame_no_offer(void)
{
    ch19_stage(CH19_LAST_TURN, 0, CH19_END_OPEN);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_latch(), 0);

    ch19_stage(CH19_LAST_TURN, 0, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURAMASA);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_latch(), 0);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 1);
}

/* The gate reads one element of the latch array and no other.  Every other
   element is set to 1 with element 0x11 left at 0, and the offer still has to
   be withheld only by the gate this case closes -- here the item -- so neither
   a neighbouring index nor a folded base is being read.  Element 0x10, the
   chapter's other latch, is the one that would be reached by an off-by-one. */
static void chapter_19_only_latch_slot_17_switches_the_halves(void)
{
    int flag_slot;

    ch19_stage(CH19_LAST_TURN, 0, CH19_END_OPEN);
    for (flag_slot = 0; flag_slot < 32; flag_slot++) {
        if (flag_slot != CH19_LATCH_SLOT) {
            data_fdps_map_cell_event_triggered_flags[flag_slot] = 1;
        }
    }
    ch19_plant_enemy(10, 0);
    ch19_retire(CH19_CHALLENGER_SLOT);
    fdps_chapter_19_post_action();
    /* The latch-down half ran, so the live enemy kept the battle open; had a
       neighbouring element been read as the latch, the duel half would have
       seen the retired challenger and written the cleared code. */
    CHECK_EQ(end_code(), CH19_END_OPEN);
    CHECK_EQ(ch19_latch(), 0);
}

/* Latch up, and the rebuild note's claim: the duel half does NOT forward to the
   shared test.  蘭迪斯 is retired, which is exactly what the accepted branch
   leaves behind, and a forward would put the defeat code 1 in the global and
   end chapter 19's duel as a Game Over on its first action.  Both duellists are
   standing, so the handler must write nothing at all. */
static void chapter_19_the_duel_does_not_run_the_shared_test(void)
{
    ch19_stage(1, 1, CH19_END_OPEN);
    ch19_retire(0);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_OPEN);
    CHECK_EQ(ch19_latch(), 1);
}

/* While both duellists are standing the duel half writes nothing, which is what
   keeps the battle loop running the duel.  The enemy side is empty, so a
   forward to the shared test would have cleared the chapter, and the offer half
   is shut by the latch even with 裘娜 carrying the 妖刀村雨 on turn 1. */
static void chapter_19_both_duellists_standing_settles_nothing(void)
{
    ch19_stage(1, 1, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_OPEN);
    CHECK_EQ(ch19_latch(), 1);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 1);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 0);
}

/* The challenger down is 裘娜's win: the chapter is cleared and the 妖刀村雨 is
   traded for the 妖刀村正.  The trade is two calls and not one -- the remove is
   gated on the find answering something other than -1 and the add is not -- so
   this case is what says both ran. */
static void chapter_19_a_retired_challenger_is_junas_win(void)
{
    ch19_stage(1, 1, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
    ch19_retire(CH19_CHALLENGER_SLOT);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 0);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 1);
    CHECK_EQ(ch19_latch(), 1);
}

/* 裘娜 down is her loss: the chapter is cleared just the same and NOTHING moves.
   The two arms share the store at 0003af4a and differ only in the item calls,
   so a rebuild that hung the trade off the wrong side of the JZ at 0003aecb
   would pass every other case here and fail this one. */
static void chapter_19_a_retired_juna_is_her_loss(void)
{
    ch19_stage(1, 1, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
    ch19_retire(CH19_JUNA_SLOT);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 1);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 0);
}

/* Both down counts as her win, because the arm is picked by the THIRD call --
   fdps_unit_is_retired(0x4d) again at 0003aebf -- and not by which of the first
   two answered.  A rebuild that reused the || chain's answer would have to pick
   one arm for this case and would pick the wrong one half the time. */
static void chapter_19_both_falling_counts_as_junas_win(void)
{
    ch19_stage(1, 1, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
    ch19_retire(CH19_JUNA_SLOT);
    ch19_retire(CH19_CHALLENGER_SLOT);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 0);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 1);
}

/* The add is outside the remove's if and not its else: a 裘娜 who won without
   the 妖刀村雨 still gets the 妖刀村正, and the item she does hold is not
   touched.  This is the -1 arm of the CMP at 0003af02. */
static void chapter_19_the_win_gives_the_muramasa_without_the_murasame(void)
{
    ch19_stage(1, 1, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_OTHER_ITEM_1);
    ch19_retire(CH19_CHALLENGER_SLOT);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 1);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_OTHER_ITEM_1), 1);
    CHECK_EQ(ch19_entry_id(CH19_JUNA_SLOT, 0), CH19_OTHER_ITEM_1);
    CHECK_EQ(ch19_entry_id(CH19_JUNA_SLOT, 1), CH19_MURAMASA);
}

/* Exactly the 妖刀村雨 is taken and nothing else, and the entries stay packed:
   she goes in holding the 妖刀村雨 first and two other things behind it, and
   comes out with those two shifted to the front and the 妖刀村正 in the entry
   the shift freed.  The slot the remove is given is the find's answer and not a
   literal, which is what this ordering reads back. */
static void chapter_19_the_win_removes_only_the_murasame(void)
{
    ch19_stage(1, 1, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
    ch19_give(CH19_JUNA_SLOT, 1, CH19_OTHER_ITEM_1);
    ch19_give(CH19_JUNA_SLOT, 2, CH19_OTHER_ITEM_2);
    ch19_retire(CH19_CHALLENGER_SLOT);
    fdps_chapter_19_post_action();
    CHECK_EQ(ch19_entry_id(CH19_JUNA_SLOT, 0), CH19_OTHER_ITEM_1);
    CHECK_EQ(ch19_entry_id(CH19_JUNA_SLOT, 1), CH19_OTHER_ITEM_2);
    CHECK_EQ(ch19_entry_id(CH19_JUNA_SLOT, 2), CH19_MURAMASA);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 0);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 1);
}

/* The dispatchers run this handler after EVERY unit action, so it is called
   twice here.  The verdict is stable, and the second call shows the add is
   ungated: with the 妖刀村雨 already gone the find answers -1, nothing is
   removed, and a second 妖刀村正 is handed over.  That is the original's
   behaviour and not a defect to guard against -- the chapter ends on the first
   of the two calls, so the second never happens in play. */
static void chapter_19_the_duel_verdict_is_stable_across_calls(void)
{
    ch19_stage(1, 1, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);
    ch19_retire(CH19_CHALLENGER_SLOT);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    fdps_chapter_19_post_action();
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 2);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 0);
    CHECK_EQ(ch19_latch(), 1);
}

/* The offer accepted, on the last turn the gate lets through.  Everything this
   path settles comes out of the one firing: the deployment ran, the whole map
   bar 裘娜 is retired with the whole-byte 1 rather than the 0x81 an OR would
   leave, 裘娜 keeps the byte she was staged with, the challenger at 0x4d is
   outside the sweep's bound and keeps his, the battle-end code goes BACK to 0
   so the phase loop resumes, and the latch is up so the offer cannot be made
   again.  Her 妖刀村雨 is untouched -- the trade belongs to the other half. */
static void chapter_19_the_offer_accepted_retires_everyone_but_juna(void)
{
    int unit_index;
    int retired_elsewhere;

    /* Staged before the guard so that a machine without the game files still
       leaves the latch array as the cases below this one expect to find it. */
    ch19_stage(CH19_LAST_TURN, 0, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);

    ch19_check_files();
    if (!ch19_files_ready) {
        return;
    }

    ch19_run_offer(CH19_KEY_ENTER, CH19_KEY_ENTER);

    CHECK_EQ(data_fdps_map_spawn_pos_table_ptr == NULL, 1);
    CHECK_EQ(data_fdps_map_unit_count, CH19_UNITS);
    CHECK_EQ(end_code(), CH19_END_OPEN);
    CHECK_EQ(ch19_latch(), 1);

    retired_elsewhere = 0;
    for (unit_index = 0; unit_index < CH19_SWEEP_COUNT; unit_index++) {
        if (unit_index == CH19_JUNA_SLOT) {
            continue;
        }
        if ((int) ch19_unit(unit_index)->flags == CH19_RETIRED_FLAGS) {
            retired_elsewhere++;
        }
    }
    CHECK_EQ(retired_elsewhere, CH19_SWEEP_COUNT - 1);
    CHECK_EQ((int) ch19_unit(0)->flags, CH19_RETIRED_FLAGS);
    CHECK_EQ((int) ch19_unit(CH19_SWEEP_COUNT - 1)->flags, CH19_RETIRED_FLAGS);
    CHECK_EQ((int) ch19_unit(CH19_JUNA_SLOT)->flags, CH19_STAGED_FLAGS);
    CHECK_EQ((int) ch19_unit(CH19_CHALLENGER_SLOT)->flags, CH19_STAGED_FLAGS);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 1);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURAMASA), 0);
}

/* The offer declined with the right cell.  The deployment still ran -- it is in
   front of the question and not behind the answer -- the latch is still raised,
   the cleared code is left standing so the chapter ends, and not one flags byte
   moved. */
static void chapter_19_the_offer_declined_ends_the_chapter(void)
{
    int unit_index;
    int untouched;

    /* Staged before the guard so that a machine without the game files still
       leaves the latch array as the cases below this one expect to find it. */
    ch19_stage(CH19_LAST_TURN, 0, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);

    ch19_check_files();
    if (!ch19_files_ready) {
        return;
    }

    ch19_run_offer(CH19_KEY_RIGHT, CH19_KEY_ENTER);

    CHECK_EQ(data_fdps_map_spawn_pos_table_ptr == NULL, 1);
    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_latch(), 1);

    untouched = 0;
    for (unit_index = 0; unit_index < CH19_UNITS; unit_index++) {
        if ((int) ch19_unit(unit_index)->flags == CH19_STAGED_FLAGS) {
            untouched++;
        }
    }
    CHECK_EQ(untouched, CH19_UNITS);
    CHECK_EQ(ch19_count_item(CH19_JUNA_SLOT, CH19_MURASAME), 1);
}

/* A cancel is not the right cell, but here it lands in the same place: the test
   at 0003b00c is an equality against 0 and fdps_prompt_two_choice answers -1 on
   Esc, so the refusal branch is taken.  A rebuild that wrote the test as
   "answer != 1" would accept the duel on a cancel and retire the whole party. */
static void chapter_19_a_cancel_declines_like_the_right_cell(void)
{
    /* Staged before the guard so that a machine without the game files still
       leaves the latch array as the cases below this one expect to find it. */
    ch19_stage(CH19_LAST_TURN, 0, CH19_END_OPEN);
    ch19_give(CH19_JUNA_SLOT, 0, CH19_MURASAME);

    ch19_check_files();
    if (!ch19_files_ready) {
        return;
    }

    ch19_run_offer(CH19_KEY_ESC, CH19_KEY_ESC);

    CHECK_EQ(end_code(), CH19_END_CLEARED);
    CHECK_EQ(ch19_latch(), 1);
    CHECK_EQ((int) ch19_unit(0)->flags, CH19_STAGED_FLAGS);
    CHECK_EQ((int) ch19_unit(CH19_JUNA_SLOT)->flags, CH19_STAGED_FLAGS);
}

/* ------------------------------------------------------------------------
 * Chapter 20's handler, 0003b150: CMP dword ptr [0x00069ce8],0x11 / JG
 * 0x0003b1b9 at 0003b15c guarding three copies of PUSH 0x0 / MOV
 * EAX,[0x00069ce8] / ADD EAX,<base> / PUSH EAX / MOV EAX,[0x00069ce8] / ADD
 * EAX,<base> / PUSH EAX / CALL 0x00036b60 / ADD ESP,0xc at 0003b165, 0003b181
 * and 0003b19d with bases 0xc, 0x1d and 0x2e, then the unconditional CALL
 * 0x0003a2e0 at 0003b1b9 the branch jumps to.
 *
 * So there are four things worth pinning: which slots each turn releases, that
 * the guard's boundary is 0x11 inclusive and its ordering signed, that the two
 * pushed indices being equal makes each call a one-slot range rather than a
 * run, and that the shared end test runs on every turn either way.
 *
 * The expected slot numbers are the assembly's three bases added to the turn
 * counter, and the expected value written is the PUSH 0x0 -- behaviour code 0
 * merged into the low nibble by fdps_object_set_field34_low_nibble_range
 * (00036b60), whose AND 0xf0 / OR keeps the high nibble.  The staged records
 * therefore start at 0x42: low nibble 2 is the hold-position code map19.dat
 * deploys this map's enemies in, and 0x40 is one of the AI flag bits the
 * scorers read, so a release shows up as 0x42 becoming 0x40 and an assignment
 * that clobbered the flags would show up as 0x00.
 *
 * The chapter id staged throughout is 19, because the table slot number is the
 * 0-based chapter id and the dword at 000602d8, nineteen entries into the
 * table based at 0006028c, is 0003b150, and that table entry is the function's
 * only xref.  That id is neither 0x10 nor 0x15, so the slot the shared test
 * watches for the defeat is 0.
 *
 * The chapter's own conditions really are the shared test's two and nothing
 * more: the guide's chapter 20 entry states 勝利條件：敵人全滅 and 失敗條件：
 * 蘭迪斯死亡.  Its scripted business -- the sword upgrade and the wave-1
 * arrival -- is a tile trigger and an opening script, so neither may show up
 * as a verdict from this handler.
 *
 * The unit array is staged rather than read from map19.dat: the handler takes
 * no arguments, so the array, the unit count, the turn counter and the chapter
 * id are its entire input.  Nothing below asserts what any of those globals
 * holds on its own -- ticket 23 owns that.
 * ------------------------------------------------------------------------ */

/* Chapter 20 is chapter id 19 (0x13). */
#define CHAPTER_20_ID 19

/* 65 is the live unit count map19.dat produces -- 11 party slots plus the 54
   wave-0 deployment records -- and one slot past it is staged as well so a
   schedule that overran the array by one would be visible rather than
   corrupting the harness. */
#define MAP19_UNITS 65
#define STAGE20_UNITS (MAP19_UNITS + 1)

/* The held enemy block: slot 11 is the scripted event walker the schedule must
   not touch, 12 is below the first base, and 13..63 is what the seventeen
   turns release. */
#define WALKER_SLOT    11
#define FIRST_RELEASED 13
#define LAST_RELEASED  63

/* The behaviour byte a held mode-2 enemy carries here: low nibble 2 is the
   hold code, 0x40 an AI flag bit the release must preserve. */
#define HELD_BEHAVIOR     0x42
#define RELEASED_BEHAVIOR 0x40

/* The last turn that releases anything, from the CMP ...,0x11 / JG. */
#define LAST_RELEASE_TURN 0x11

static struct fdps_unit_record stage20_units[STAGE20_UNITS];

/* The array laid out the way fdps_build_map_unit_array and fdps_deploy_wave
   leave it for this map: the 11 party slots at 0..10 and the 54 wave-0 enemies
   at 11..64, every one of the enemies a held mode-2 unit carrying the AI flag
   bit.  Nothing is retired, so the enemy block holds the shared test's verdict
   at 0 unless a case says otherwise. */
#define MAP19_PARTY_SLOTS 11

static void stage20(int turn)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage20_units;
    for (i = 0; i < (int) sizeof(stage20_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < STAGE20_UNITS; i++) {
        stage20_units[i].side =
            (unsigned char) (i < MAP19_PARTY_SLOTS ? SIDE_PLAYER : SIDE_ENEMY);
        stage20_units[i].ai_behavior = HELD_BEHAVIOR;
    }

    data_fdps_map_unit_array_ptr = (unsigned char *) stage20_units;
    data_fdps_map_unit_count = MAP19_UNITS;
    data_fdps_chapter_current_chapter_id = CHAPTER_20_ID;
    data_fdps_chapter_event_or_battle_end_code = 0;
    data_fdps_battle_turn_counter = turn;
}

/* How many staged slots are no longer holding.  Every release writes the same
   value, so a count and a per-slot check together say both how many moved and
   which. */
static int released_count(void)
{
    int slot;
    int count;

    count = 0;
    for (slot = 0; slot < STAGE20_UNITS; slot++) {
        if (stage20_units[slot].ai_behavior != HELD_BEHAVIOR) {
            count++;
        }
    }
    return count;
}

/* Turn 1 releases the three bases themselves: 1+0xc, 1+0x1d and 1+0x2e.  The
   released value is 0x40 and not 0x00, which is the callee's merge keeping the
   high nibble. */
static void chapter_20_turn_1_releases_slots_13_30_47(void)
{
    stage20(1);
    fdps_chapter_20_post_action();
    CHECK_EQ(stage20_units[13].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[30].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[47].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(released_count(), 3);
}

/* Turn 17 is the last turn the guard admits -- CMP ...,0x11 / JG jumps only
   above 0x11 -- and it releases the top of each column: 17+0xc, 17+0x1d and
   17+0x2e, the highest of them slot 63, two below the map's 65 units. */
static void chapter_20_turn_17_still_releases(void)
{
    stage20(LAST_RELEASE_TURN);
    fdps_chapter_20_post_action();
    CHECK_EQ(stage20_units[29].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[46].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[LAST_RELEASED].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(released_count(), 3);
}

/* Turn 18 releases nothing: the whole block is loose by then and the branch is
   taken.  This is the case that separates JG from JGE -- a JGE would still be
   releasing slots 30, 47 and 64 here, and slot 64 is a unit the original never
   writes at all. */
static void chapter_20_turn_18_releases_nothing(void)
{
    stage20(LAST_RELEASE_TURN + 1);
    fdps_chapter_20_post_action();
    CHECK_EQ(released_count(), 0);
    CHECK_EQ(stage20_units[MAP19_UNITS - 1].ai_behavior, HELD_BEHAVIOR);
}

/* Later turns keep releasing nothing, so the guard is an upper bound and not a
   window that reopens. */
static void chapter_20_late_turns_release_nothing(void)
{
    int turn;

    for (turn = LAST_RELEASE_TURN + 1; turn <= 40; turn++) {
        stage20(turn);
        fdps_chapter_20_post_action();
        CHECK_EQ(released_count(), 0);
    }
}

/* Three slots a turn and never a run.  Both indices pushed at each call site
   are the same value, so each call is a one-slot inclusive range; had the
   second been the column's other end the first turn alone would have released
   the whole block. */
static void chapter_20_releases_exactly_three_a_turn(void)
{
    int turn;

    for (turn = 1; turn <= LAST_RELEASE_TURN; turn++) {
        stage20(turn);
        fdps_chapter_20_post_action();
        CHECK_EQ(released_count(), 3);
        CHECK_EQ(stage20_units[turn + 0x0c].ai_behavior, RELEASED_BEHAVIOR);
        CHECK_EQ(stage20_units[turn + 0x1d].ai_behavior, RELEASED_BEHAVIOR);
        CHECK_EQ(stage20_units[turn + 0x2e].ai_behavior, RELEASED_BEHAVIOR);
    }
}

/* The seventeen turns run end to end cover slots 13 to 63 exactly once each,
   which is what the three bases being 17 apart means, and they leave slots 11
   and 12 and slot 64 holding.  Slot 11 is the load-bearing one: it is the
   map's only behaviour-code-5 unit, the event walker that leaves the
   bottom-left chest for the treasure at the top, and code 0 written over it
   would cancel that walk -- which is exactly what a first base of 0xa or 0xb,
   chosen to make the three columns cover the block evenly, would do. */
static void chapter_20_seventeen_turns_cover_13_to_63(void)
{
    int turn;
    int slot;

    stage20(1);
    for (turn = 1; turn <= LAST_RELEASE_TURN; turn++) {
        data_fdps_battle_turn_counter = turn;
        fdps_chapter_20_post_action();
    }
    CHECK_EQ(released_count(), LAST_RELEASED - FIRST_RELEASED + 1);
    for (slot = FIRST_RELEASED; slot <= LAST_RELEASED; slot++) {
        CHECK_EQ(stage20_units[slot].ai_behavior, RELEASED_BEHAVIOR);
    }
    CHECK_EQ(stage20_units[WALKER_SLOT].ai_behavior, HELD_BEHAVIOR);
    CHECK_EQ(stage20_units[12].ai_behavior, HELD_BEHAVIOR);
    CHECK_EQ(stage20_units[MAP19_UNITS - 1].ai_behavior, HELD_BEHAVIOR);
    CHECK_EQ(stage20_units[MAP19_UNITS].ai_behavior, HELD_BEHAVIOR);
}

/* The turn counter is ordered signed: JG at 0003b163, not JA.  A negative
   counter is not a state a battle reaches -- fdps_chapter_state_reset installs
   1 and only fdps_battle_advance_turn raises it -- so this pins the emitted
   comparison rather than a game behaviour, and the slots it reaches with a
   counter of -1 are 11, 28 and 45, all inside the staged array.  Comparing the
   counter as unsigned would jump instead and release nothing. */
static void chapter_20_the_turn_test_is_signed(void)
{
    stage20(-1);
    fdps_chapter_20_post_action();
    CHECK_EQ(released_count(), 3);
    CHECK_EQ(stage20_units[11].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[28].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[45].ai_behavior, RELEASED_BEHAVIOR);
}

/* Only the low nibble moves.  A released slot keeps the 0x40 flag bit, and
   every neighbour of a released slot keeps its whole byte, so the callee's
   inclusive range really did end where it started. */
static void chapter_20_a_release_keeps_the_ai_flags(void)
{
    stage20(5);
    fdps_chapter_20_post_action();
    CHECK_EQ(stage20_units[17].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[16].ai_behavior, HELD_BEHAVIOR);
    CHECK_EQ(stage20_units[18].ai_behavior, HELD_BEHAVIOR);
    CHECK_EQ(stage20_units[34].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[33].ai_behavior, HELD_BEHAVIOR);
    CHECK_EQ(stage20_units[35].ai_behavior, HELD_BEHAVIOR);
    CHECK_EQ(stage20_units[51].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[50].ai_behavior, HELD_BEHAVIOR);
    CHECK_EQ(stage20_units[52].ai_behavior, HELD_BEHAVIOR);
}

/* 勝利條件：敵人全滅, on a turn that also releases.  The CALL at 0003b1b9 is
   the branch's target as well as its fall-through, so the shared test runs
   whichever way the guard goes; with every enemy retired its up-front 2
   survives. */
static void chapter_20_clears_when_every_enemy_is_retired(void)
{
    int slot;

    stage20(1);
    for (slot = MAP19_PARTY_SLOTS; slot < MAP19_UNITS; slot++) {
        stage20_units[slot].flags = FLAG_RETIRED;
    }
    fdps_chapter_20_post_action();
    CHECK_EQ(end_code(), 2);
    CHECK_EQ(released_count(), 3);
}

/* The same clear on a turn past the schedule, which is the path that reaches
   the CALL through the JG rather than by falling into it. */
static void chapter_20_clears_after_the_schedule_is_over(void)
{
    int slot;

    stage20(LAST_RELEASE_TURN + 1);
    for (slot = MAP19_PARTY_SLOTS; slot < MAP19_UNITS; slot++) {
        stage20_units[slot].flags = FLAG_RETIRED;
    }
    fdps_chapter_20_post_action();
    CHECK_EQ(end_code(), 2);
    CHECK_EQ(released_count(), 0);
}

/* A live enemy puts the code back to 0, so the sweep inside the shared test is
   reached and not short circuited by the release in front of it. */
static void chapter_20_a_live_enemy_keeps_the_battle_going(void)
{
    stage20(3);
    fdps_chapter_20_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件：蘭迪斯死亡.  Chapter id 19 is neither 0x10 nor 0x15, so the shared
   test watches unit slot 0 and its store carries no guard: the defeat stands
   even in the call that emptied the enemy side.  A handler that had carried
   chapter 17's slot 3 here would answer 2. */
static void chapter_20_a_retired_randis_is_a_defeat(void)
{
    int slot;

    stage20(3);
    stage20_units[0].flags = FLAG_RETIRED;
    fdps_chapter_20_post_action();
    CHECK_EQ(end_code(), 1);

    stage20(3);
    for (slot = 0; slot < MAP19_UNITS; slot++) {
        stage20_units[slot].flags = FLAG_RETIRED;
    }
    fdps_chapter_20_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No slot but 0 ends this battle, slot 3 included -- the slot the shared test
   would watch had chapter 20's id been one of the two it singles out.  Each of
   the party's other slots, 1 to 10, is retired in turn with the enemy block
   left standing to hold the battle open. */
static void chapter_20_no_other_slot_ends_the_battle(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < MAP19_PARTY_SLOTS; retired_slot++) {
        stage20(3);
        stage20_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_20_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded by a chapter event survives untouched -- the
   shared test returns at its gate -- but the release does NOT: it sits in
   front of the CALL and is guarded only by the turn counter, so it runs on a
   turn whose battle has already been decided. */
static void chapter_20_a_recorded_verdict_is_left_alone(void)
{
    stage20(1);
    data_fdps_chapter_event_or_battle_end_code = 2;
    fdps_chapter_20_post_action();
    CHECK_EQ(end_code(), 2);
    CHECK_EQ(released_count(), 3);

    stage20(1);
    data_fdps_chapter_event_or_battle_end_code = 1;
    fdps_chapter_20_post_action();
    CHECK_EQ(end_code(), 1);
    CHECK_EQ(released_count(), 3);
}

/* The dispatchers run this after every unit action, so a turn on which several
   units act calls it several times -- and it releases the same three slots each
   time rather than advancing through the block.  Nothing in the body reads or
   writes the turn counter; only fdps_battle_advance_turn moves it. */
static void chapter_20_repeats_within_one_turn(void)
{
    stage20(4);
    fdps_chapter_20_post_action();
    fdps_chapter_20_post_action();
    fdps_chapter_20_post_action();
    CHECK_EQ(released_count(), 3);
    CHECK_EQ(data_fdps_battle_turn_counter, 4);
    CHECK_EQ(stage20_units[16].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[33].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(stage20_units[50].ai_behavior, RELEASED_BEHAVIOR);
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------------
 * Chapter 21's handler, 0003b210: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB
 * ESP,0x0 at 0003b210..0003b216, CALL 0x0003a2e0 at 0003b21c, then the four
 * POPs and RET at 0003b221..0003b225.  Like chapters 16's and 18's it has no
 * store, no compare and no second call, so the two things worth pinning are
 * that the shared default end test really runs and that nothing else does.
 *
 * The expected verdicts are the shared test's, read off its assembly at
 * 0003a2e0: the CMP dword ptr [0x00069da0],0x0 / JNZ at 0003a2ec that abandons
 * the body for a code that is already non-zero, the MOV dword ptr
 * [0x00069da0],0x2 at 0003a2f9 that writes the cleared verdict up front, the
 * CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 and MOV AL,byte ptr [EAX+0x5] /
 * AND AL,0x1 at 0003a33a that put it back to 0 for a live enemy, and the PUSH
 * 0x0 / unguarded MOV dword ptr [0x00069da0],0x1 at 0003a382..0003a390 that
 * makes a retired unit slot 0 a defeat in every chapter but 0x10 and 0x15.
 *
 * The chapter id staged throughout is 20, because the table is indexed by the
 * 0-based chapter id and this handler is slot 20: the dword at 000602dc,
 * twenty entries into the table based at 0006028c, is 0003b210, and that table
 * entry is the function's only xref.
 *
 * That id sits directly BELOW the second of the two ids the shared test singles
 * out, 0x15, which is chapter 22's.  So the sweep below is this chapter's half
 * of the same boundary chapters 16 and 18 test around 0x10: an off-by-one in
 * the shared test's CMP ...,0x15 would hand this chapter's defeat to slot 3,
 * and the sweep is what catches it.
 *
 * The chapter's own conditions really are the shared test's two and nothing
 * more: the guide's chapter 21 entry, 地底神殿, states 勝利條件：敵人全滅 and
 * 失敗條件：蘭迪斯死亡.  Its scripted business -- the first reinforcement wave
 * when a unit reaches the junction past the turn, and the second wave plus the
 * general assault when a unit reaches any standing enemy group -- is all
 * position-triggered chapter events, so none of it may show up as a verdict
 * from this handler.
 *
 * The unit array is staged rather than read from the map file: the handler
 * takes no arguments, so the array, the unit count and the chapter id are its
 * entire input.  Nothing below asserts what any of those globals holds on its
 * own -- ticket 23 owns that.
 * ------------------------------------------------------------------------ */

/* Chapter 21 is chapter id 20 (0x14). */
#define CHAPTER_21_ID 20

static void stage21(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_21_ID;
}

/* 勝利條件：敵人全滅.  The CALL is really taken: with every enemy retired the
   shared test's up front 2 survives, and a handler whose body did nothing would
   leave the 0 it was given. */
static void chapter_21_clears_when_every_enemy_is_retired(void)
{
    stage21(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_21_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0, so the walk inside the shared test is
   reached through this handler and not short circuited by anything in front of
   the CALL. */
static void chapter_21_a_live_enemy_keeps_the_battle_going(void)
{
    stage21(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_21_post_action();
    CHECK_EQ(end_code(), 0);
}

/* 失敗條件：蘭迪斯死亡.  Chapter id 20 is neither 0x10 nor 0x15, so the shared
   test watches unit slot 0, and its store carries no guard: the defeat stands
   even in the same call that emptied the enemy side. */
static void chapter_21_a_retired_randis_is_a_defeat(void)
{
    stage21(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_21_post_action();
    CHECK_EQ(end_code(), 1);

    stage21(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_21_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  Slots 1 to 6 are retired in
   turn with slot 7 left as a live enemy holding the battle open, so the only
   thing that could turn any of these into a non-zero code is a defeat test the
   handler does not have -- or the shared test asking about slot 3, which it
   does only for chapter ids 0x10 and 0x15 and this chapter's id of 20 sits
   directly below the second of them. */
static void chapter_21_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        stage21(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_21_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded by a chapter event survives the handler untouched:
   the shared test's gate returns before anything is examined, and this handler
   adds no store of its own on either side of the CALL.  Each value below would
   be overwritten by a body that ran -- the array holds a live enemy, which would
   settle the code at 0. */
static void chapter_21_a_recorded_verdict_is_left_alone(void)
{
    stage21(2, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_21_post_action();
    CHECK_EQ(end_code(), 1);

    stage21(2, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_21_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open.  It is called
   twice because the dispatchers run it after every unit action, and a handler
   that only behaved on its first call would still pass every case above. */
static void chapter_21_an_open_battle_stays_open(void)
{
    stage21(2, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_21_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_21_post_action();
    CHECK_EQ(end_code(), 0);
}

/* --------------------------------------------------------------------------
 * Chapter 22, 巫湯婆婆 -- fdps_chapter_22_post_action at 0003b270.
 *
 * The odd one out in this file: PUSH 0x3 / CALL 0x000109b0 / ADD ESP,0x4 at
 * 0003b27c..0003b283, TEST EAX,EAX / JZ 0003b294 at 0003b286 and MOV dword
 * ptr [0x00069da0],0x1 at 0003b28a are the entire body.  There is no CALL
 * 0x0003a2e0, so the shared default end test never runs and the sweep for a
 * live enemy that every other handler here inherits is simply absent.
 *
 * That absence is what most of the cases below pin, because it is the one
 * thing a plausible wrong emit would restore: the guide gives 第22章 巫湯婆婆
 * 勝利條件 擊倒巫湯婆婆 and 失敗條件 法蓮娜死亡, so the clear belongs to the
 * scripted boss-defeat event at 000388b0 and never to this handler, while the
 * defeat is the store at 0003b28a and nothing else.
 *
 * Unit slot 3 is 法蓮娜 for the same reason as in chapter 17: unit slot i is
 * roster slot i and the roster is in join order.  Chapter 22 deploys
 * 蘭迪斯以外的所有人, so slot 0 is not on this map, which is why the handler
 * names 3 rather than inheriting the shared test's usual 0.
 *
 * The chapter id staged below is 21, the 0-based id whose table slot -- the
 * dword at 000602e0, twenty-one entries into the table based at 0006028c --
 * holds 0003b270.  Nothing in the body reads it, and the last case here
 * asserts exactly that.
 * ------------------------------------------------------------------------ */

/* Chapter 22 is chapter id 21 (0x15). */
#define CHAPTER_22_ID 21

static void stage22(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_22_ID;
}

/* 失敗條件：法蓮娜死亡.  A retired slot 3 puts a 1 in the code: the TEST/JZ at
   0003b286 falls through and 0003b28a stores it. */
static void chapter_22_a_retired_farlena_is_a_defeat(void)
{
    stage22(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The case that separates this handler from every other one in the file: with
   every enemy retired and slot 3 standing, the code stays 0.  A body that
   forwarded to the shared test at 0003a2e0 would answer 2 here, because that
   test's sweep is precisely 敵人全滅 -- and chapter 22 is not won that way. */
static void chapter_22_wiping_the_enemy_out_does_not_clear_the_chapter(void)
{
    stage22(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The ordinary path: a live enemy, a standing slot 3, nothing decided.  The JZ
   at 0003b286 is taken and the body writes nothing at all.  It is called twice
   because the dispatchers run it after every unit action, and a handler that
   only behaved on its first call would still pass every other case here. */
static void chapter_22_an_open_battle_stays_open(void)
{
    stage22(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 0);
}

/* A retired slot 0 is not this chapter's defeat.  蘭迪斯 is not even deployed
   here -- 己方：蘭迪斯以外的所有人 -- and the PUSH is 0x3, not 0x0, so a body
   that carried the shared test's usual index would answer 1 below. */
static void chapter_22_a_retired_slot_0_is_not_a_defeat(void)
{
    stage22(4, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 0);
}

/* No slot but 3 ends this battle, and the sweep also catches an argument that
   drifted by one either way.  Every other slot is retired in turn with slot 3
   left standing, and the code has to stay 0 each time. */
static void chapter_22_no_slot_but_farlena_ends_the_battle(void)
{
    int retired_slot;
    int other_slot;

    for (retired_slot = 0; retired_slot < STAGE_UNITS; retired_slot++) {
        if (retired_slot == FARLENA_SLOT) {
            continue;
        }
        stage22(STAGE_UNITS, 0);
        for (other_slot = 0; other_slot < STAGE_UNITS; other_slot++) {
            stage_unit(other_slot, SIDE_PLAYER, 0);
        }
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_22_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* The store consults nothing, so a clear the boss-defeat event already
   recorded loses to a defeat detected on the same action.  Gating the store on
   the code still being 0 -- the guard the shared test puts on its own writes --
   would leave the 2 standing here, and the player would clear a chapter the
   original ends with a Game Over. */
static void chapter_22_a_recorded_clear_still_loses_to_a_retired_farlena(void)
{
    stage22(4, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With slot 3 standing there is no store on any path, so a verdict already in
   the code survives whatever else the map looks like -- including the wiped
   out enemy side that would have made the shared test recompute a 2. */
static void chapter_22_a_recorded_verdict_survives_a_standing_farlena(void)
{
    stage22(4, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 2);

    stage22(4, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The body contains no CMP against data_fdps_chapter_current_chapter_id, so
   the answer cannot depend on it.  Staging chapter 17's id and then chapter
   21's -- the two ids either side of the shared test's chapter comparison --
   changes nothing, which is the observable difference between this handler and
   one that reached the shared test. */
static void chapter_22_the_chapter_id_is_never_consulted(void)
{
    stage22(4, 0);
    data_fdps_chapter_current_chapter_id = CHAPTER_17_ID;
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 1);

    stage22(4, 0);
    data_fdps_chapter_current_chapter_id = CHAPTER_21_ID;
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_22_post_action();
    CHECK_EQ(end_code(), 0);
}

/* Chapter 23's post-action handler, 0003b2e0, is two calls to
   fdps_unit_is_retired nested one inside the other, a message and two stores:
   PUSH 0x3 / CALL 0x000109b0 / ADD ESP,0x4 at 0003b2ec, TEST EAX,EAX / JZ
   0003b306 at 0003b2f6, MOV dword ptr [0x00069da0],0x1 at 0003b2fa and JMP
   0003b341 at 0003b304 for the first arm; PUSH 0x1f / CALL 0x000109b0 at
   0003b306, TEST EAX,EAX / JZ 0003b341 at 0003b310, the seven pushes and CALL
   0x0001ff60 at 0003b314..0003b32f and MOV dword ptr [0x00069da0],0x1 at
   0003b337 for the second.  There is no CALL 0x0003a2e0 anywhere in the body.

   So the cases below pin four things: that a retired slot 3 is a defeat, that a
   retired slot 0x1f is a defeat too, that nothing else in the unit array moves
   the code at all -- in particular that wiping the enemy side out does not
   clear this chapter, which is what a body that forwarded to the shared test
   would do -- and that both stores are unguarded, so either defeat overwrites a
   clear already in the code.

   The expected verdicts are those instructions' and the 0/1/2 meanings of the
   code are program_info/architecture.md.  The chapter's rules agree: the
   strategy guide's 第23章 死神冥河 gives 勝利條件：擊倒死神, a named boss and
   not 敵人全滅, and 失敗條件：法蓮娜死亡，蘭迪斯從戰場上方消失（二十回合）.

   What is NOT asserted here is the message.  fdps_draw_text (text.c) is real
   code and it is reached on the second defeat path, but its destination is the
   hard-coded VGA aperture at 0xa0000 rather than a surface a case could hand
   it, so the else-arm ordering -- silence when slot 3 is the one that fell --
   leaves no trace a test running in a text-mode console can read back.  What
   the cases below can do is make that call harmless and deterministic, which
   the staged text block does.

   THE TEXT BLOCK HAS TO BE STAGED.  fdps_draw_text's first act is
   text_base += *(short *)(text_base + text_id * 2) and then a walk from there
   until the -1 terminator, with no null check anywhere: left at the zero
   ticket 23 gives it, data_fdps_current_chapter_text_ptr sends that walk
   through whatever the low linear addresses happen to hold, and a token that
   lands on the page break stands a modal wait on the keyboard.  So stage23
   publishes a block whose every entry points straight at a terminator, and the
   message path draws nothing and returns at once.

   The unit array is staged here rather than read from a game file: the handler
   takes no arguments, so the array global, the unit count, the chapter id and
   that text block are its entire input.  It is staged one slot longer than slot
   0x1f because fdps_unit_is_retired range checks nothing at either end. */

/* Chapter 23 is chapter id 22 (0x16). */
#define CHAPTER_23_ID 22

/* Thirty-three slots, so that index 0x1f has a record of its own and the sweep
   has one slot past it to retire. */
#define CH23_STAGE_UNITS 0x21

/* The second defeat's unit, PUSH 0x1f at 0003b306.  It is one of map22.dat's
   own deployed units and not a roster member -- fdps_chapter_23_end treats
   indices 11 and up as the map's -- and which unit it is has not been
   established. */
#define CH23_SECOND_LOSS_SLOT 0x1f

/* The staged text block: 0x15 entries, 0 through the 0x14 the handler asks
   for, and one terminator token after them for every entry to point at.  The
   offset table is 16-bit and so is the token stream, the offset is a byte
   count added to the base of the block, and -1 ends an entry (src/text.c). */
#define CH23_TEXT_ENTRIES 0x15
#define CH23_TEXT_TERMINATOR_AT (CH23_TEXT_ENTRIES * 2)
#define CH23_TEXT_END (-1)

static struct fdps_unit_record ch23_units[CH23_STAGE_UNITS];
static short ch23_text_block[CH23_TEXT_ENTRIES + 1];

static void stage23(int live_unit_count, int battle_end_code)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch23_units;
    for (i = 0; i < (int) sizeof(ch23_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH23_TEXT_ENTRIES; i++) {
        ch23_text_block[i] = (short) CH23_TEXT_TERMINATOR_AT;
    }
    ch23_text_block[CH23_TEXT_ENTRIES] = (short) CH23_TEXT_END;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch23_text_block;
    data_fdps_map_unit_array_ptr = (unsigned char *) ch23_units;
    data_fdps_map_unit_count = live_unit_count;
    data_fdps_chapter_current_chapter_id = CHAPTER_23_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void stage23_unit(int unit_index, int side, int flags)
{
    ch23_units[unit_index].side = (unsigned char) side;
    ch23_units[unit_index].flags = (unsigned char) flags;
}

/* 失敗條件：法蓮娜死亡.  A retired slot 3 puts a 1 in the code: the TEST/JZ at
   0003b2f6 falls through to the store at 0003b2fa. */
static void chapter_23_a_retired_farlena_is_a_defeat(void)
{
    stage23(CH23_STAGE_UNITS, 0);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The second condition: slot 3 standing sends the body to PUSH 0x1f at
   0003b306, and a retired slot 0x1f falls through the TEST/JZ at 0003b310 to
   the message and the store at 0003b337.  A handler that carried only chapter
   22's single test would leave the 0 here. */
static void chapter_23_a_retired_second_loss_unit_is_a_defeat(void)
{
    stage23(CH23_STAGE_UNITS, 0);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
}

/* Both watched slots standing and the body writes nothing on any path.  It is
   called twice because the dispatchers run it after every unit action, and a
   handler that only behaved on its first call would still pass every other case
   here. */
static void chapter_23_an_open_battle_stays_open(void)
{
    stage23(CH23_STAGE_UNITS, 0);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The case that separates this handler from the ones that forward: with every
   enemy retired and both watched slots standing the code stays 0.  A body
   carrying CALL 0x0003a2e0 would answer 2, because that test's sweep is
   precisely 敵人全滅 -- and chapter 23 is won by 擊倒死神 instead, which the
   scripted boss-defeat event writes. */
static void chapter_23_wiping_the_enemy_out_does_not_clear_the_chapter(void)
{
    int slot;

    stage23(CH23_STAGE_UNITS, 0);
    for (slot = 0; slot < CH23_STAGE_UNITS; slot++) {
        stage23_unit(slot, SIDE_ENEMY, FLAG_RETIRED);
    }
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_PLAYER, 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 0);
}

/* A retired slot 0 is not this chapter's defeat.  蘭迪斯 is not deployed here --
   己方：蘭迪斯以外的所有人 -- and the pushes are 0x3 and 0x1f, never 0x0, so a
   body that carried the shared test's usual index would answer 1 below. */
static void chapter_23_a_retired_slot_0_is_not_a_defeat(void)
{
    stage23(CH23_STAGE_UNITS, 0);
    stage23_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 0);
}

/* No slot but 3 and 0x1f ends this battle, and the sweep also catches either
   argument having drifted by one: 2, 4, 0x1e and 0x20 are all retired in turn
   with the two watched slots left standing, and the code has to stay 0 each
   time. */
static void chapter_23_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int other_slot;

    for (retired_slot = 0; retired_slot < CH23_STAGE_UNITS; retired_slot++) {
        if (retired_slot == FARLENA_SLOT
            || retired_slot == CH23_SECOND_LOSS_SLOT) {
            continue;
        }
        stage23(CH23_STAGE_UNITS, 0);
        for (other_slot = 0; other_slot < CH23_STAGE_UNITS; other_slot++) {
            stage23_unit(other_slot, SIDE_PLAYER, 0);
        }
        ch23_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_23_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* The first store consults nothing, so a clear the boss-defeat event already
   recorded loses to 法蓮娜 falling on the same action.  Gating the store on the
   code still being 0 -- the guard the shared test puts on its own writes --
   would leave the 2 standing here. */
static void chapter_23_a_recorded_clear_still_loses_to_a_retired_farlena(void)
{
    stage23(CH23_STAGE_UNITS, 2);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The same for the second store at 0003b337, which is equally unguarded: the
   message path overwrites a recorded clear as well. */
static void chapter_23_a_recorded_clear_still_loses_to_the_second_unit(void)
{
    stage23(CH23_STAGE_UNITS, 2);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With both watched slots standing there is no store on any path, so a verdict
   already in the code survives whatever else the map looks like -- including
   the wiped out enemy side that would have made the shared test recompute a
   2. */
static void chapter_23_a_recorded_verdict_survives_both_standing(void)
{
    stage23(CH23_STAGE_UNITS, 2);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 2);

    stage23(CH23_STAGE_UNITS, 1);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
}

/* Both watched slots gone is still one defeat and the same 1: the JMP at
   0003b304 leaves over the second test entirely, so the two arms cannot both
   run and the verdict is the first arm's. */
static void chapter_23_both_watched_slots_gone_is_still_a_defeat(void)
{
    stage23(CH23_STAGE_UNITS, 0);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The body contains no CMP against data_fdps_chapter_current_chapter_id, so the
   answer cannot depend on it.  Staging chapter 17's id and then chapter 21's --
   the two ids either side of the shared test's chapter comparison -- changes
   neither verdict, which is the observable difference between this handler and
   one that reached the shared test. */
static void chapter_23_the_chapter_id_is_never_consulted(void)
{
    stage23(CH23_STAGE_UNITS, 0);
    data_fdps_chapter_current_chapter_id = CHAPTER_17_ID;
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);

    stage23(CH23_STAGE_UNITS, 0);
    data_fdps_chapter_current_chapter_id = CHAPTER_21_ID;
    stage23_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage23_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The handler holds no state of its own, so the same map answers the same way
   however many unit actions it is run after. */
static void chapter_23_the_verdict_is_stable_across_calls(void)
{
    stage23(CH23_STAGE_UNITS, 0);
    stage23_unit(0, SIDE_PLAYER, 0);
    stage23_unit(1, SIDE_ENEMY, 0);
    stage23_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage23_unit(CH23_SECOND_LOSS_SLOT, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
    fdps_chapter_23_post_action();
    CHECK_EQ(end_code(), 1);
}

void run_chpost2_tests(void)
{
    RUN_TEST(the_shared_end_test_runs);
    RUN_TEST(a_live_enemy_keeps_the_battle_going);
    RUN_TEST(a_retired_randis_is_a_defeat);
    RUN_TEST(no_other_slot_ends_the_battle);
    RUN_TEST(a_recorded_verdict_is_left_alone);
    RUN_TEST(an_open_battle_stays_open);
    RUN_TEST(a_retired_farlena_is_a_defeat);
    RUN_TEST(every_enemy_retired_clears_the_chapter);
    RUN_TEST(a_live_enemy_and_a_live_farlena_keep_the_battle_going);
    RUN_TEST(the_last_enemy_and_farlena_falling_together_is_a_defeat);
    RUN_TEST(a_recorded_clear_still_loses_to_a_retired_farlena);
    RUN_TEST(a_recorded_verdict_survives_a_standing_farlena);
    RUN_TEST(no_slot_but_farlena_ends_the_battle);
    RUN_TEST(the_verdict_is_stable_across_calls);
    RUN_TEST(chapter_18_clears_when_every_enemy_is_retired);
    RUN_TEST(chapter_18_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(chapter_18_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_18_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_18_a_recorded_verdict_is_left_alone);
    RUN_TEST(chapter_18_an_open_battle_stays_open);
    RUN_TEST(chapter_19_record_shape_matches_the_offsets);
    RUN_TEST(chapter_19_the_shared_end_test_runs);
    RUN_TEST(chapter_19_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(chapter_19_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_19_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_19_a_recorded_defeat_is_left_alone);
    RUN_TEST(chapter_19_a_late_turn_withholds_the_offer);
    RUN_TEST(chapter_19_an_open_battle_withholds_the_offer);
    RUN_TEST(chapter_19_a_retired_juna_withholds_the_offer);
    RUN_TEST(chapter_19_without_the_murasame_no_offer);
    RUN_TEST(chapter_19_only_latch_slot_17_switches_the_halves);
    RUN_TEST(chapter_19_the_duel_does_not_run_the_shared_test);
    RUN_TEST(chapter_19_both_duellists_standing_settles_nothing);
    RUN_TEST(chapter_19_a_retired_challenger_is_junas_win);
    RUN_TEST(chapter_19_a_retired_juna_is_her_loss);
    RUN_TEST(chapter_19_both_falling_counts_as_junas_win);
    RUN_TEST(chapter_19_the_win_gives_the_muramasa_without_the_murasame);
    RUN_TEST(chapter_19_the_win_removes_only_the_murasame);
    RUN_TEST(chapter_19_the_duel_verdict_is_stable_across_calls);
    RUN_TEST(chapter_19_the_offer_accepted_retires_everyone_but_juna);
    RUN_TEST(chapter_19_the_offer_declined_ends_the_chapter);
    RUN_TEST(chapter_19_a_cancel_declines_like_the_right_cell);
    RUN_TEST(chapter_20_turn_1_releases_slots_13_30_47);
    RUN_TEST(chapter_20_turn_17_still_releases);
    RUN_TEST(chapter_20_turn_18_releases_nothing);
    RUN_TEST(chapter_20_late_turns_release_nothing);
    RUN_TEST(chapter_20_releases_exactly_three_a_turn);
    RUN_TEST(chapter_20_seventeen_turns_cover_13_to_63);
    RUN_TEST(chapter_20_the_turn_test_is_signed);
    RUN_TEST(chapter_20_a_release_keeps_the_ai_flags);
    RUN_TEST(chapter_20_clears_when_every_enemy_is_retired);
    RUN_TEST(chapter_20_clears_after_the_schedule_is_over);
    RUN_TEST(chapter_20_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(chapter_20_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_20_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_20_a_recorded_verdict_is_left_alone);
    RUN_TEST(chapter_20_repeats_within_one_turn);
    RUN_TEST(chapter_21_clears_when_every_enemy_is_retired);
    RUN_TEST(chapter_21_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(chapter_21_a_retired_randis_is_a_defeat);
    RUN_TEST(chapter_21_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_21_a_recorded_verdict_is_left_alone);
    RUN_TEST(chapter_21_an_open_battle_stays_open);
    RUN_TEST(chapter_22_a_retired_farlena_is_a_defeat);
    RUN_TEST(chapter_22_wiping_the_enemy_out_does_not_clear_the_chapter);
    RUN_TEST(chapter_22_an_open_battle_stays_open);
    RUN_TEST(chapter_22_a_retired_slot_0_is_not_a_defeat);
    RUN_TEST(chapter_22_no_slot_but_farlena_ends_the_battle);
    RUN_TEST(chapter_22_a_recorded_clear_still_loses_to_a_retired_farlena);
    RUN_TEST(chapter_22_a_recorded_verdict_survives_a_standing_farlena);
    RUN_TEST(chapter_22_the_chapter_id_is_never_consulted);
    RUN_TEST(chapter_23_a_retired_farlena_is_a_defeat);
    RUN_TEST(chapter_23_a_retired_second_loss_unit_is_a_defeat);
    RUN_TEST(chapter_23_an_open_battle_stays_open);
    RUN_TEST(chapter_23_wiping_the_enemy_out_does_not_clear_the_chapter);
    RUN_TEST(chapter_23_a_retired_slot_0_is_not_a_defeat);
    RUN_TEST(chapter_23_no_other_slot_ends_the_battle);
    RUN_TEST(chapter_23_a_recorded_clear_still_loses_to_a_retired_farlena);
    RUN_TEST(chapter_23_a_recorded_clear_still_loses_to_the_second_unit);
    RUN_TEST(chapter_23_a_recorded_verdict_survives_both_standing);
    RUN_TEST(chapter_23_both_watched_slots_gone_is_still_a_defeat);
    RUN_TEST(chapter_23_the_chapter_id_is_never_consulted);
    RUN_TEST(chapter_23_the_verdict_is_stable_across_calls);
}
