/* tests/chpost1.c -- cover for src/chpost1.c.
 *
 * Chapter 2's post-action handler is one CALL: PUSH EBX/ESI/EDI/EBP, MOV
 * EBP,ESP, SUB ESP,0x0, CALL 0x0003a2e0, then the four POPs and RET at
 * 0003a461..0003a465.  There is no store, no compare and no second call in
 * the body, so what is worth pinning is exactly two things: that the shared
 * default end test really runs, and that nothing else does.
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
 * The chapter id staged throughout is 1, because the handler table based at
 * 0006028c is indexed by the 0-based chapter id and this handler is slot 1 --
 * the dispatchers do MOV EAX,[0x00069cf4] / LEA EAX,[EAX*0x4 + 0x0] / CALL
 * dword ptr [EAX + 0x6028c] at 00012a3c..00012a48.
 *
 * That the handler adds no lose condition of its own is asserted by retiring
 * each slot other than 0 in turn and finding the battle still open: chapter
 * 1's sibling handler tests fdps_unit_is_retired(2) for Sol, and the whole
 * risk in this function is that shape being copied here.  Chapter 2 keeps its
 * Sol condition in map01.dat as a death script instead, so no unit slot but
 * the shared test's slot 0 may end this battle from code.
 *
 * The unit array is staged here rather than read from a game file: the
 * handler takes no arguments at all, so the array global, the unit count and
 * the chapter id are its entire input.  Nothing below asserts what any of
 * those globals holds on its own -- ticket 23 owns that.
 */
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "chpost1.h"

/* Eight slots so every index the "no condition of its own" sweep touches has
   a record of its own. */
#define STAGE_UNITS 8

/* Side codes, from the record's side byte at offset 6. */
#define SIDE_ENEMY  0
#define SIDE_PLAYER 2

/* Bit 0 of the flags byte at offset 5 is the retirement flag. */
#define FLAG_RETIRED 0x01

/* Chapter 2 is chapter id 1: the table slot number is the 0-based id. */
#define CHAPTER_02_ID 1

/* Chapter 4 is chapter id 3, table slot 3: the dword at 00060298, three
   entries into the table based at 0006028c, is 0003a560. */
#define CHAPTER_04_ID 3

/* The guest side, from the deployment record chapter 4's slot 3 comes from. */
#define SIDE_GUEST 1

/* The slot chapter 4's own defeat test asks about -- PUSH 0x3 at 0003a571. */
#define GUEST_SLOT 3

/* Chapter 5 is chapter id 4, table slot 4: the dword at 0006029c, four
   entries into the table based at 0006028c, is 0003a5d0. */
#define CHAPTER_05_ID 4

/* The slot chapter 5's own defeat test asks about -- PUSH 0x3 at 0003a5e1.
   On map04.dat that slot is 法蓮娜: the map fields five player slots, so the
   roster fills 0..4 and she is the fourth member appended. */
#define FARLENA_SLOT 3

/* 索爾 on this chapter's map: the one wave-0 deployment record, appended
   after the five player slots. */
#define CH05_SOL_SLOT 5

/* Chapter 6 is chapter id 5, table slot 5: the dword at 000602a0, five
   entries into the table based at 0006028c, is 0003a640. */
#define CHAPTER_06_ID 5

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
    data_fdps_chapter_current_chapter_id = CHAPTER_02_ID;
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
   was given. */
static void the_shared_end_test_runs(void)
{
    stage(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_02_post_action();
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
    fdps_chapter_02_post_action();
    CHECK_EQ(end_code(), 0);
}

/* Chapter 2 is not 0x10 or 0x15, so the shared test watches unit slot 0 --
   Randis -- and its store carries no guard: the defeat stands even in the
   same call that emptied the enemy side. */
static void a_retired_randis_is_a_defeat(void)
{
    stage(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    fdps_chapter_02_post_action();
    CHECK_EQ(end_code(), 1);

    stage(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_02_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  Chapter 1's handler tests
   slot 2 for Sol and stores 1; chapter 2 carries that condition in map01.dat
   instead, so every slot from 1 up may retire with the battle still open.
   The sweep covers slot 2 specifically and the rest of the player side
   besides.  Slot 7 is the live enemy that holds the battle open throughout,
   so the only thing that could turn any of these into a non-zero code is a
   defeat test the handler does not have. */
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
        fdps_chapter_02_post_action();
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
    fdps_chapter_02_post_action();
    CHECK_EQ(end_code(), 1);

    stage(2, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_02_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open, rather than
   being reset or cleared by an initialisation the frame does not have. */
static void an_open_battle_stays_open(void)
{
    stage(2, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_02_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_02_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------ *
 * Chapter 4's handler, 0003a560.  It is the shared test followed by one
 * defeat test of its own: PUSH 0x3 / CALL 0x000109b0 / ADD ESP,0x4 /
 * TEST EAX,EAX / JZ, and the MOV dword ptr [0x00069da0],0x1 at 0003a57f
 * the JZ skips.  What that shape makes worth pinning is the ORDER and the
 * absence of a guard, because both are easy to write away: the store runs
 * after the shared test and consults nothing, so it beats a clear the
 * shared test recorded in the same call and it fires even on the path
 * where the shared test returned at its gate without examining anything.
 *
 * Chapter id 3 is neither 0x10 nor 0x15, so inside the shared test the
 * arm taken is PUSH 0x0 at 0003a382 -- slot 0, not slot 3.  Slots 0 and 3
 * are therefore the only two that can end this battle from code, and the
 * sweep below holds every other slot to that.
 * ------------------------------------------------------------------ */

/* Same staging as above with the chapter id moved to chapter 4's. */
static void stage_ch04(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_04_ID;
}

/* The whole point of the store being unguarded and last: the same action
   empties the enemy side and retires the guest.  The shared test writes 2 at
   0003a2f9 and nothing puts it back to 0, then this handler overwrites it
   with 1.  An else, or a store gated on the code still being 0, would answer
   2 here. */
static void a_retired_guest_outranks_a_cleared_field(void)
{
    stage_ch04(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(GUEST_SLOT, SIDE_GUEST, FLAG_RETIRED);
    fdps_chapter_04_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The ordinary defeat: the battle is still going -- a live enemy settles the
   shared test at 0 -- and the retired guest turns that into 1. */
static void a_retired_guest_is_a_defeat(void)
{
    stage_ch04(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(GUEST_SLOT, SIDE_GUEST, FLAG_RETIRED);
    fdps_chapter_04_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With the guest still in play the handler adds nothing at all, so both of
   the shared test's own answers come through unchanged. */
static void a_live_guest_leaves_the_shared_verdict_alone(void)
{
    stage_ch04(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(GUEST_SLOT, SIDE_GUEST, 0);
    fdps_chapter_04_post_action();
    CHECK_EQ(end_code(), 2);

    stage_ch04(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(GUEST_SLOT, SIDE_GUEST, 0);
    fdps_chapter_04_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The CALL to the shared test is really made and chapter 4 takes its ordinary
   arm: chapter id 3 is not 0x10 or 0x15, so the watched slot in there is 0,
   and a retired slot 0 is a defeat with the guest untouched. */
static void the_shared_slot_zero_test_still_runs(void)
{
    stage_ch04(4, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(GUEST_SLOT, SIDE_GUEST, 0);
    fdps_chapter_04_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The guest test sits outside the shared test's gate.  A verdict a chapter
   event already recorded makes the shared test return at 0003a2ec without
   examining anything, and the retired guest still overwrites it with 1;
   with the guest in play the recorded verdict survives. */
static void the_guest_test_survives_a_recorded_verdict(void)
{
    stage_ch04(4, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(GUEST_SLOT, SIDE_GUEST, FLAG_RETIRED);
    fdps_chapter_04_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch04(4, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_ENEMY, 0);
    stage_unit(GUEST_SLOT, SIDE_GUEST, 0);
    fdps_chapter_04_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Only slots 0 and 3 end this battle from code.  Every other slot retires in
   turn with a live enemy at slot 7 holding the shared test's answer at 0, so
   anything but 0 would be a defeat test the handler does not have -- the
   literal in the PUSH having drifted, or a second one having been invented
   for 法蓮娜, whose death this chapter carries as a map death script. */
static void no_slot_but_zero_and_three_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        if (retired_slot == GUEST_SLOT) {
            continue;
        }
        stage_ch04(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_04_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* ------------------------------------------------------------------ *
 * Chapter 5's handler, 0003a5d0.  Instruction for instruction chapter 4's:
 * PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0, CALL 0x0003a2e0, then
 * PUSH 0x3 / CALL 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ 0003a5f9
 * and the MOV dword ptr [0x00069da0],0x1 at 0003a5ef that the JZ skips.
 *
 * The cases below are the same risk set as chapter 4's, restaged on this
 * chapter's map: the order of the two tests, the absence of a guard on the
 * store, and the literal 3 in the PUSH.  What differs is who slot 3 is.
 * map04.dat fields five player slots, so the roster fills 0..4 and slot 3
 * is 法蓮娜, while the guest 索爾 -- appended after them as the map's one
 * wave-0 deployment -- is slot 5.  The strategy guide's chapter 5 entry
 * lists three lose conditions, 蘭迪斯, 法蓮娜 or 索爾 dying; only the
 * first two can come from this code, so slot 5 retiring must leave the
 * battle open here and the sweep at the end holds it to that.
 *
 * Chapter id 4 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 * ------------------------------------------------------------------ */

/* Same staging as above with the chapter id moved to chapter 5's. */
static void stage_ch05(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_05_ID;
}

/* The whole point of the store being unguarded and last: the same action
   empties the enemy side and kills 法蓮娜.  The shared test writes 2 at
   0003a2f9 and nothing puts it back to 0, then this handler overwrites it
   with 1.  An else, or a store gated on the code still being 0, would answer
   2 here. */
static void ch05_a_retired_farlena_outranks_a_cleared_field(void)
{
    stage_ch05(6, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(CH05_SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_05_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The ordinary defeat: the battle is still going -- a live enemy settles the
   shared test at 0 -- and the retired 法蓮娜 turns that into 1. */
static void ch05_a_retired_farlena_is_a_defeat(void)
{
    stage_ch05(6, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, 0);
    stage_unit(CH05_SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_05_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With 法蓮娜 still in play the handler adds nothing at all, so both of the
   shared test's own answers come through unchanged. */
static void ch05_a_live_farlena_leaves_the_shared_verdict_alone(void)
{
    stage_ch05(6, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(CH05_SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_05_post_action();
    CHECK_EQ(end_code(), 2);

    stage_ch05(6, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    stage_unit(CH05_SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_05_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The CALL to the shared test is really made and chapter 5 takes its ordinary
   arm: chapter id 4 is not 0x10 or 0x15, so the watched slot in there is 0,
   蘭迪斯, and a retired slot 0 is a defeat with 法蓮娜 untouched. */
static void ch05_the_shared_slot_zero_test_still_runs(void)
{
    stage_ch05(6, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    stage_unit(CH05_SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_05_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The 法蓮娜 test sits outside the shared test's gate.  A verdict a chapter
   event already recorded makes the shared test return at 0003a2ec without
   examining anything, and the retired 法蓮娜 still overwrites it with 1;
   with her in play the recorded verdict survives. */
static void ch05_the_farlena_test_survives_a_recorded_verdict(void)
{
    stage_ch05(6, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_05_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch05(6, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_05_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Only slots 0 and 3 end this battle from code.  Every other slot retires in
   turn with a live enemy at slot 7 holding the shared test's answer at 0, so
   anything but 0 would be a defeat test the handler does not have -- the
   literal in the PUSH having drifted, or a second one having been invented
   for 索爾, whose death this chapter carries as a death script on his
   deployment record.  Slot 5 in the sweep is that 索爾. */
static void ch05_no_slot_but_zero_and_three_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        if (retired_slot == FARLENA_SLOT) {
            continue;
        }
        stage_ch05(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_05_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* ------------------------------------------------------------------ *
 * Chapter 6's handler, 0003a640.  Instruction for instruction chapters 4
 * and 5's: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0, CALL 0x0003a2e0,
 * then PUSH 0x3 / CALL 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ 0003a669
 * and the MOV dword ptr [0x00069da0],0x1 at 0003a65f that the JZ skips.
 *
 * The cases below are the same risk set as chapter 5's, restaged on this
 * chapter's map: the order of the two tests, the absence of a guard on the
 * store, and the literal 3 in the PUSH.  map05.dat's header byte +1 fields
 * four player slots, so the roster -- 蘭迪斯, 尤利安, 亞克, 法蓮娜, the same
 * four as in chapter 5 because neither chapter's init appends anyone -- fills
 * slots 0..3 exactly and slot 3 is 法蓮娜.  The strategy guide's chapter 6
 * entry gives 勝利條件 敵人全滅 and 失敗條件 蘭迪斯、法蓮娜、索爾任一人死亡:
 * the first two lose conditions are slot 0's and this store's, and 索爾 --
 * a wave-0 deployment record carrying a death script, appended after the four
 * roster slots -- is the script runner's, so whichever slot past 3 he lands
 * on must leave the battle open here.  The sweep at the end holds every slot
 * but 0 and 3 to that.
 *
 * Chapter id 5 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 * ------------------------------------------------------------------ */

/* Same staging as above with the chapter id moved to chapter 6's. */
static void stage_ch06(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_06_ID;
}

/* The whole point of the store being unguarded and last: the same action
   empties the enemy side and kills 法蓮娜.  The shared test writes 2 at
   0003a2f9 and nothing puts it back to 0, then this handler overwrites it
   with 1.  An else, or a store gated on the code still being 0, would answer
   2 here. */
static void ch06_a_retired_farlena_outranks_a_cleared_field(void)
{
    stage_ch06(5, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_06_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The ordinary defeat: the battle is still going -- a live enemy settles the
   shared test at 0 -- and the retired 法蓮娜 turns that into 1. */
static void ch06_a_retired_farlena_is_a_defeat(void)
{
    stage_ch06(5, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_06_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With 法蓮娜 still in play the handler adds nothing at all, so both of the
   shared test's own answers come through unchanged. */
static void ch06_a_live_farlena_leaves_the_shared_verdict_alone(void)
{
    stage_ch06(5, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_06_post_action();
    CHECK_EQ(end_code(), 2);

    stage_ch06(5, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_06_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The CALL to the shared test is really made and chapter 6 takes its ordinary
   arm: chapter id 5 is not 0x10 or 0x15, so the watched slot in there is 0,
   蘭迪斯, and a retired slot 0 is a defeat with 法蓮娜 untouched. */
static void ch06_the_shared_slot_zero_test_still_runs(void)
{
    stage_ch06(5, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_06_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The 法蓮娜 test sits outside the shared test's gate.  A verdict a chapter
   event already recorded makes the shared test return at 0003a2ec without
   examining anything, and the retired 法蓮娜 still overwrites it with 1;
   with her in play the recorded verdict survives. */
static void ch06_the_farlena_test_survives_a_recorded_verdict(void)
{
    stage_ch06(5, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_06_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch06(5, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(FARLENA_SLOT, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_06_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Only slots 0 and 3 end this battle from code.  Every other slot retires in
   turn with a live enemy at slot 7 holding the shared test's answer at 0, so
   anything but 0 would be a defeat test the handler does not have -- the
   literal in the PUSH having drifted, or a second one having been invented
   for 索爾.  He stands at one of the slots past the roster that this sweep
   retires, and his death is the death script runner's business, not this
   handler's. */
static void ch06_no_slot_but_zero_and_three_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        if (retired_slot == FARLENA_SLOT) {
            continue;
        }
        stage_ch06(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_06_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

void run_chpost1_tests(void)
{
    RUN_TEST(the_shared_end_test_runs);
    RUN_TEST(a_live_enemy_keeps_the_battle_going);
    RUN_TEST(a_retired_randis_is_a_defeat);
    RUN_TEST(no_other_slot_ends_the_battle);
    RUN_TEST(a_recorded_verdict_is_left_alone);
    RUN_TEST(an_open_battle_stays_open);
    RUN_TEST(a_retired_guest_outranks_a_cleared_field);
    RUN_TEST(a_retired_guest_is_a_defeat);
    RUN_TEST(a_live_guest_leaves_the_shared_verdict_alone);
    RUN_TEST(the_shared_slot_zero_test_still_runs);
    RUN_TEST(the_guest_test_survives_a_recorded_verdict);
    RUN_TEST(no_slot_but_zero_and_three_ends_the_battle);
    RUN_TEST(ch05_a_retired_farlena_outranks_a_cleared_field);
    RUN_TEST(ch05_a_retired_farlena_is_a_defeat);
    RUN_TEST(ch05_a_live_farlena_leaves_the_shared_verdict_alone);
    RUN_TEST(ch05_the_shared_slot_zero_test_still_runs);
    RUN_TEST(ch05_the_farlena_test_survives_a_recorded_verdict);
    RUN_TEST(ch05_no_slot_but_zero_and_three_ends_the_battle);
    RUN_TEST(ch06_a_retired_farlena_outranks_a_cleared_field);
    RUN_TEST(ch06_a_retired_farlena_is_a_defeat);
    RUN_TEST(ch06_a_live_farlena_leaves_the_shared_verdict_alone);
    RUN_TEST(ch06_the_shared_slot_zero_test_still_runs);
    RUN_TEST(ch06_the_farlena_test_survives_a_recorded_verdict);
    RUN_TEST(ch06_no_slot_but_zero_and_three_ends_the_battle);
}
