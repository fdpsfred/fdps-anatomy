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
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
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
}
