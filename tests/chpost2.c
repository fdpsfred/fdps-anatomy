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
}
