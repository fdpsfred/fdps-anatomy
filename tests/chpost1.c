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

/* Chapter 7 is chapter id 6, table slot 6: the dword at 000602a4, six entries
   into the table based at 0006028c, is 0003a6b0. */
#define CHAPTER_07_ID 6

/* Chapter 9 is chapter id 8, table slot 8: the dword at 000602ac, eight
   entries into the table based at 0006028c, is 0003a840. */
#define CHAPTER_09_ID 8

/* The two slots chapter 9's own defeat tests ask about -- PUSH 0x6 at
   0003a851 and PUSH 0x7 at 0003a85f.  map08.dat fields eight player slots and
   the party standing at the start of the chapter is six, so the two character
   ids fdps_chapter_09_init appends to the roster before the battle, 8 布蘭多
   then 9 蓋亞, fill the last two in that order. */
#define BRANDO_SLOT 6
#define GAIA_SLOT 7

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

/* ------------------------------------------------------------------ *
 * Chapter 7's handler, 0003a6b0.  Instruction for instruction chapter 2's:
 * PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0, CALL 0x0003a2e0, then the
 * four POPs and RET at 0003a6c1..0003a6c5.  No store, no compare, no second
 * call, so the two things worth pinning are that the shared default end test
 * really runs and that nothing else does.
 *
 * The second half is the whole risk here, and it is larger than it was for
 * chapter 2: the three table slots immediately before this one -- chapters
 * 4, 5 and 6 -- each follow the shared test with PUSH 0x3 / CALL 0x000109b0
 * and an unguarded MOV dword ptr [0x00069da0],0x1, and chapter 7's roster
 * still holds 法蓮娜, so carrying that shape one slot further is the natural
 * mistake.  The guide's chapter 7 entry, 競技場戰士, gives 勝利條件 敵人全滅
 * and 失敗條件 蘭迪斯死亡 -- one lose condition, and it is the shared test's
 * slot 0.  So no slot but 0 may end this battle from code, and the sweep
 * below holds every one of them to that, slot 3 included.
 *
 * Chapter id 6 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 * ------------------------------------------------------------------ */

/* Same staging as above with the chapter id moved to chapter 7's. */
static void stage_ch07(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_07_ID;
}

/* The CALL is really taken: with every enemy retired the shared test's up
   front 2 at 0003a2f9 survives, and a handler whose body did nothing would
   leave the 0 it was given.  This is the chapter's stated win condition,
   敵人全滅. */
static void ch07_the_shared_end_test_runs(void)
{
    stage_ch07(5, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_07_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0 at 0003a34a, so the walk inside the
   shared test is reached through this handler and not short circuited by
   anything in front of the CALL. */
static void ch07_a_live_enemy_keeps_the_battle_going(void)
{
    stage_ch07(6, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(5, SIDE_ENEMY, 0);
    fdps_chapter_07_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The chapter's one stated lose condition, 蘭迪斯死亡, and it comes entirely
   from the shared test: chapter id 6 is not 0x10 or 0x15, so the watched slot
   in there is 0, and its store carries no guard -- the defeat stands even in
   the call that emptied the enemy side. */
static void ch07_a_retired_randis_is_a_defeat(void)
{
    stage_ch07(5, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_07_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch07(5, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_07_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  Every other slot retires in
   turn with a live enemy at slot 7 holding the shared test's answer at 0, so
   anything but 0 would be a defeat test the handler does not have.  Slot 3 is
   in the sweep on purpose: it is the slot the three neighbouring handlers
   test, and on this chapter's roster it is 法蓮娜, whose death the guide does
   not list as a lose condition here. */
static void ch07_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        stage_ch07(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_07_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded survives the handler untouched: the shared
   test's gate at 0003a2ec returns before anything is examined, and this
   handler adds no store of its own on either side of the CALL.  Each value
   below would be overwritten by a body that ran -- the array holds a live
   enemy, which would settle the code at 0. */
static void ch07_a_recorded_verdict_is_left_alone(void)
{
    stage_ch07(2, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_07_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch07(2, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_07_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open, rather than
   being reset or cleared by an initialisation the frame does not have. */
static void ch07_an_open_battle_stays_open(void)
{
    stage_ch07(2, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_07_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_07_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------ *
 * Chapter 9's handler, 0003a840.  The shared test and then TWO defeat
 * tests sharing one store: CALL 0x0003a2e0, then PUSH 0x6 / CALL
 * 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JNZ 0003a86d, then PUSH 0x7 /
 * CALL 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ 0003a877, with the one
 * MOV dword ptr [0x00069da0],0x1 at 0003a86d that both arms reach.
 *
 * The JNZ jumps the slot-7 call entirely, so the two conditions are a
 * short-circuiting or and not two independent tests, and there is one store
 * and not one per condition.  What that makes worth pinning is the same risk
 * set as the single-guest handlers above -- the order of the tests against
 * the shared one, the absence of a guard on the store -- plus the second
 * literal: a handler written with only the 6, or with the 7 unreachable,
 * passes every case that touches slot 6 alone.
 *
 * map08.dat's header byte +1 fields eight player slots.  The permanent party
 * at the start of the chapter is six: fdps_roster_add_character is called
 * exactly once from each of the chapter 1, 2, 3, 4, 7 and 8 init handlers,
 * giving 蘭迪斯, 尤利安, 亞克, 法蓮娜, 裘娜, 費塔加 at 0..5.
 * fdps_chapter_09_init then appends character ids 8 and 9 -- PUSH 0x8 / CALL
 * 0x00023bc0 then PUSH 0x9 / CALL 0x00023bc0 at 000210bc..000210cd -- and the
 * roster appends at its member count, so 布蘭多 is slot 6 and 蓋亞 slot 7.
 * The strategy guide's chapter 9 entry gives 勝利條件 敵人全滅 and
 * 失敗條件 蘭迪斯、布蘭多或蓋亞其中一人死亡, and those three are
 * exactly slot 0's shared test and these two stores: all 31 of the map's
 * deployment records carry a zero death-script opcode, so nothing is left for
 * the script runner and no other slot may end this battle from code.
 *
 * Chapter id 8 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 * ------------------------------------------------------------------ */

/* Same staging as above with the chapter id moved to chapter 9's. */
static void stage_ch09(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_09_ID;
}

/* Both guests are ordinary player-side roster members here, not the side-1
   deployment guest of chapter 4: the chapter's init handler appended them to
   the roster, so they fill player slots like the rest of the party.

   Slot 5 is deliberately left as the zeroed record stage() wrote, which is a
   live unit on side 0, so a case that wants the battle still open has an
   enemy without needing a ninth staged slot.  A case that wants the field
   cleared overwrites slot 5 itself. */
static void stage_ch09_party(void)
{
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, 0);
    stage_unit(4, SIDE_PLAYER, 0);
    stage_unit(BRANDO_SLOT, SIDE_PLAYER, 0);
    stage_unit(GAIA_SLOT, SIDE_PLAYER, 0);
}

/* The whole point of the store being unguarded and last: the same action
   empties the enemy side and retires 布蘭多.  The shared test writes 2 at
   0003a2f9 and nothing puts it back to 0, then this handler overwrites it
   with 1.  An else, or a store gated on the code still being 0, would answer
   2 here. */
static void ch09_a_retired_brando_outranks_a_cleared_field(void)
{
    stage_ch09(8, 0);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, FLAG_RETIRED);
    stage_units[BRANDO_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The same for 蓋亞, reached only through the fall-through of the JNZ at
   0003a85d: slot 6 answers 0, so the second call is made and its non-zero
   answer takes the same store.  A handler that stopped at the first test
   would answer 2 here. */
static void ch09_a_retired_gaia_outranks_a_cleared_field(void)
{
    stage_ch09(8, 0);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, FLAG_RETIRED);
    stage_units[GAIA_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The ordinary defeats: the battle is still going -- a live enemy settles the
   shared test at 0 -- and either retired guest on its own turns that into 1,
   as does both of them at once. */
static void ch09_either_retired_guest_is_a_defeat(void)
{
    stage_ch09(8, 0);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, 0);
    stage_units[BRANDO_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch09(8, 0);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, 0);
    stage_units[GAIA_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch09(8, 0);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, 0);
    stage_units[BRANDO_SLOT].flags = FLAG_RETIRED;
    stage_units[GAIA_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With both guests in play the handler adds nothing at all, so both of the
   shared test's own answers come through unchanged. */
static void ch09_two_live_guests_leave_the_shared_verdict_alone(void)
{
    stage_ch09(8, 0);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 2);

    stage_ch09(8, 0);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, 0);
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The CALL to the shared test is really made and chapter 9 takes its ordinary
   arm: chapter id 8 is not 0x10 or 0x15, so the watched slot in there is 0,
   and a retired slot 0 is a defeat with both guests untouched. */
static void ch09_the_shared_slot_zero_test_still_runs(void)
{
    stage_ch09(8, 0);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, 0);
    stage_units[0].flags = FLAG_RETIRED;
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The guest tests sit outside the shared test's gate.  A verdict a chapter
   event already recorded makes the shared test return at 0003a2ec without
   examining anything, and either retired guest still overwrites it with 1;
   with both in play the recorded verdict survives. */
static void ch09_the_guest_tests_survive_a_recorded_verdict(void)
{
    stage_ch09(8, 2);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, 0);
    stage_units[BRANDO_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch09(8, 2);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, 0);
    stage_units[GAIA_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch09(8, 2);
    stage_ch09_party();
    stage_unit(5, SIDE_ENEMY, 0);
    fdps_chapter_09_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Only slots 0, 6 and 7 end this battle from code.  All eight slots are the
   party here -- which is the real shape of this map, whose 31 deployment
   records all begin at index 8 -- so the shared test finds no live enemy and
   its verdict is 2, and each of slots 1..5 retires in turn against that.  A
   defeat test the handler does not have would answer 1: either literal in the
   two PUSHes having drifted, or a third test having been invented for one of
   the party members the guide does not list. */
static void ch09_no_slot_but_zero_six_and_seven_ends_the_battle(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < BRANDO_SLOT; retired_slot++) {
        stage_ch09(8, 0);
        stage_ch09_party();
        stage_unit(5, SIDE_PLAYER, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_09_post_action();
        CHECK_EQ(end_code(), 2);
    }
}

/* ------------------------------------------------------------------ *
 * Chapter 10's handler, 0003a8c0.  The one handler in this file with no
 * CALL 0x0003a2e0 in it: an eight-slot escape count, then a defeat test,
 * then the clear, and nothing else.
 *
 * The loop is CMP dword ptr [EBP-0x8],0x8 / JL over indices 0..7, and its
 * body is CALL 0x0002d210 / MOV AL,byte ptr [EAX+0x1] / AND EAX,0xff / CMP
 * EAX,0x17 / JZ 0003a919 with PUSH EAX / CALL 0x000109b0 / TEST EAX,EAX /
 * JZ 0003a91f reached only when that compare fails, both arms landing on
 * the one INC dword ptr [EBP-0x4] at 0003a91c.  The tail is PUSH 0x0 /
 * CALL 0x000109b0 / TEST EAX,EAX / JZ 0003a93b, choosing between MOV dword
 * ptr [0x00069da0],0x1 at 0003a92f and the CMP dword ptr [EBP-0x4],0x8 /
 * JNZ 0003a94b guarding MOV dword ptr [0x00069da0],0x2 at 0003a941.
 *
 * So four things carry the whole behaviour and each is easy to write away:
 * the count reaching all eight rather than a threshold, 0x17 being an
 * equality on the record's pos_y rather than a floor, the retired test
 * being ORed INTO the count so a casualty counts as escaped, and the
 * absence of the shared end test that every other handler in this file
 * calls.  The expected verdicts below come from that assembly, and the
 * 0/1/2 meanings of the code are program_info/architecture.md.
 *
 * The guide's chapter 10 entry, 宗教法庭, gives 勝利條件 戰場底部脫離
 * （所有人到達戰場底部）and 失敗條件 蘭迪斯死亡 -- no 敵人全滅 win at
 * all, which is exactly the missing shared test -- and its closing advice
 * sets 己方八位人員 out on the last two stair rows, the eight player slots
 * the loop bound counts.
 *
 * Chapter 10 is chapter id 9, table slot 9: the dword at 000602b0, nine
 * entries into the table based at 0006028c, is 0003a8c0.
 * ------------------------------------------------------------------ */

#define CHAPTER_10_ID 9

/* The map's bottom row, from the CMP EAX,0x17 at 0003a904. */
#define BOTTOM_ROW 0x17

/* Same staging as above with the chapter id moved to chapter 10's. */
static void stage_ch10(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_10_ID;
}

/* All eight player slots, live, standing on one row.  The handler reads only
   pos_y at record offset 1 and the flags byte at offset 5, so those two and
   the array base are its entire input. */
static void stage_ch10_party_at_row(int row)
{
    int slot;

    for (slot = 0; slot < STAGE_UNITS; slot++) {
        stage_unit(slot, SIDE_PLAYER, 0);
        stage_units[slot].pos_y = (unsigned char) row;
    }
}

/* The chapter's stated win condition, 所有人到達戰場底部: every one of the
   eight slots on row 0x17 with 蘭迪斯 in play settles the code at 2. */
static void ch10_all_eight_on_the_bottom_row_is_a_clear(void)
{
    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(BOTTOM_ROW);
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The count is compared for equality with 8 and nothing less will do: each
   slot in turn is held one row short while the other seven have escaped, and
   the battle is still open every time.  A threshold, or a bound that stopped
   at 7, would clear the chapter on one of these. */
static void ch10_one_slot_short_leaves_the_battle_open(void)
{
    int held_back_slot;

    for (held_back_slot = 0; held_back_slot < STAGE_UNITS; held_back_slot++) {
        stage_ch10(STAGE_UNITS, 0);
        stage_ch10_party_at_row(BOTTOM_ROW);
        stage_units[held_back_slot].pos_y = (unsigned char) (BOTTOM_ROW - 1);
        fdps_chapter_10_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* The row test is an equality, not a floor: a slot sitting past 0x17 is no
   more escaped than one short of it.  Writing pos_y >= 0x17 passes the case
   above and fails this one. */
static void ch10_the_bottom_row_test_is_an_equality(void)
{
    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(BOTTOM_ROW);
    stage_units[4].pos_y = (unsigned char) (BOTTOM_ROW + 1);
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 0);

    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(BOTTOM_ROW);
    stage_units[4].pos_y = 0;
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The retired test is ORed into the count, so a casualty is accounted for and
   the chapter stays winnable after losses.  First: nobody but 蘭迪斯 has
   reached the bottom row and the other seven are all gone, which still clears
   the chapter.  Then the mixed case, four escaped and four dead.  Counting
   only units standing on row 0x17 answers 0 to both. */
static void ch10_a_retired_unit_counts_as_escaped(void)
{
    int slot;

    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(0);
    stage_units[0].pos_y = (unsigned char) BOTTOM_ROW;
    for (slot = 1; slot < STAGE_UNITS; slot++) {
        stage_units[slot].flags = FLAG_RETIRED;
    }
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 2);

    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(0);
    for (slot = 0; slot < 4; slot++) {
        stage_units[slot].pos_y = (unsigned char) BOTTOM_ROW;
    }
    for (slot = 4; slot < STAGE_UNITS; slot++) {
        stage_units[slot].flags = FLAG_RETIRED;
    }
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The chapter's one stated lose condition, 蘭迪斯死亡, and the defeat arm is
   taken before the count is looked at: the second case has all eight slots
   accounted for -- 蘭迪斯 counts himself, being retired -- and the answer is
   still 1.  Testing the count first, or hanging the defeat off an else of the
   clear, answers 2 there. */
static void ch10_a_retired_randis_is_a_defeat(void)
{
    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(0);
    stage_units[0].flags = FLAG_RETIRED;
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(BOTTOM_ROW);
    stage_units[0].flags = FLAG_RETIRED;
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 1);
}

/* Only slot 0 can lose the chapter.  The tail asks fdps_unit_is_retired about
   the literal 0 at 0003a921 and about nothing else, so each of the other seven
   slots retires in turn on an escaped field and the chapter still clears, and
   a lone casualty on an unescaped field leaves the battle open rather than
   ending it.  A defeat test written as "any unit retired" fails all eight. */
static void ch10_only_slot_zero_can_lose_the_chapter(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS; retired_slot++) {
        stage_ch10(STAGE_UNITS, 0);
        stage_ch10_party_at_row(BOTTOM_ROW);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_10_post_action();
        CHECK_EQ(end_code(), 2);
    }

    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(0);
    stage_units[3].flags = FLAG_RETIRED;
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The shared default end test is not called, on either side of the body.
   First: eight live units and not one of them on side 0, so the walk inside
   fdps_battle_check_default_end_conditions would find no live enemy and leave
   its own up front 2 at 0003a2f9 standing -- and nobody has escaped, so the
   answer here must be 0.  Then the other direction: eight live side-0 units
   that have all reached the bottom row, where that test would put the code
   back to 0 at 0003a34a and this handler must still answer 2.  Copying the
   sibling handlers' CALL 0x0003a2e0 fails one or the other whichever end of
   the body it is written at. */
static void ch10_the_shared_end_test_is_not_run(void)
{
    int slot;

    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(0);
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 0);

    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(BOTTOM_ROW);
    for (slot = 0; slot < STAGE_UNITS; slot++) {
        stage_units[slot].side = (unsigned char) SIDE_ENEMY;
    }
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Both stores are conditional and there is no unconditional write anywhere in
   the body, so with neither end condition met the code keeps whatever the
   battle loop gave it: a verdict a chapter event already recorded survives,
   and an open battle stays open. */
static void ch10_a_verdict_the_handler_does_not_settle_is_left_alone(void)
{
    stage_ch10(STAGE_UNITS, 1);
    stage_ch10_party_at_row(BOTTOM_ROW);
    stage_units[6].pos_y = 0;
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch10(STAGE_UNITS, 2);
    stage_ch10_party_at_row(BOTTOM_ROW);
    stage_units[6].pos_y = 0;
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 2);

    stage_ch10(STAGE_UNITS, 0);
    stage_ch10_party_at_row(BOTTOM_ROW);
    stage_units[6].pos_y = 0;
    fdps_chapter_10_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------ *
 * Chapter 11's handler, 0003a9a0.  The shared test and then one defeat
 * test of its own: CALL 0x0003a2e0 at 0003a9ac, then PUSH 0x8 / CALL
 * 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ 0003a9c9, with the
 * unguarded MOV dword ptr [0x00069da0],0x1 at 0003a9bf the JZ skips.
 *
 * The risk set is the one the chapter 4, 5 and 6 handlers share -- the
 * order of the two tests, the absence of a guard on the store, and the
 * literal in the PUSH -- so the cases below are the same shape, moved to
 * slot 8.
 *
 * map10.dat's header byte +1 fields nine player slots.
 * fdps_roster_add_character is called once from each of the chapter 1, 2,
 * 3, 4, 7 and 8 init handlers and twice from chapter 9's, so eight members
 * stand at 0..7 going into this chapter; fdps_chapter_11_init then appends
 * character id 7 -- 琴琴, the level 15 武道家 -- and the roster appends at
 * its member count, so she is slot 8.  The strategy guide's chapter 11
 * entry gives 勝利條件 敵人全滅 and 失敗條件 蘭迪斯或琴琴死亡, and those
 * two are exactly slot 0's shared test and this store: all 49 of
 * map10.dat's deployment records are side 0, and the only death-script
 * opcodes among them are the gold drops on two enemies, so nothing is left
 * for the script runner and no other slot may end this battle from code.
 *
 * Chapter id 10 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 *
 * The nine-slot array below is this chapter's own: the shared stage_units
 * has eight slots and slot 8 is past its end.
 * ------------------------------------------------------------------ */

/* Chapter 11 is chapter id 10, table slot 10: the dword at 000602b4, ten
   entries into the table based at 0006028c, is 0003a9a0. */
#define CHAPTER_11_ID 10

/* The slot chapter 11's own defeat test asks about -- PUSH 0x8 at 0003a9b1. */
#define CHINCHIN_SLOT 8

/* One more slot than the shared array holds, because the map fields nine
   player slots and the watched one is the last of them. */
#define CH11_STAGE_UNITS 9

static struct fdps_unit_record ch11_units[CH11_STAGE_UNITS];

/* The same staging as stage(), over the nine-slot array and with the chapter
   id moved to chapter 11's. */
static void stage_ch11(int live_unit_count, int battle_end_code)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch11_units;
    for (i = 0; i < (int) sizeof(ch11_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch11_units;
    data_fdps_map_unit_count = live_unit_count;
    data_fdps_chapter_current_chapter_id = CHAPTER_11_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void stage_ch11_unit(int unit_index, int side, int flags)
{
    ch11_units[unit_index].side = (unsigned char) side;
    ch11_units[unit_index].flags = (unsigned char) flags;
}

/* 琴琴 is an ordinary player-side roster member here, not the side-1
   deployment guest of chapter 4: the chapter's init handler appended her to
   the roster, so she fills a player slot like the rest of the party.

   Slot 7 is deliberately left as the zeroed record stage_ch11() wrote, which
   is a live unit on side 0, so a case that wants the battle still open has an
   enemy without needing a tenth staged slot.  A case that wants the field
   cleared overwrites slot 7 itself. */
static void stage_ch11_party(void)
{
    stage_ch11_unit(0, SIDE_PLAYER, 0);
    stage_ch11_unit(1, SIDE_PLAYER, 0);
    stage_ch11_unit(2, SIDE_PLAYER, 0);
    stage_ch11_unit(3, SIDE_PLAYER, 0);
    stage_ch11_unit(4, SIDE_PLAYER, 0);
    stage_ch11_unit(5, SIDE_PLAYER, 0);
    stage_ch11_unit(6, SIDE_PLAYER, 0);
    stage_ch11_unit(CHINCHIN_SLOT, SIDE_PLAYER, 0);
}

/* The whole point of the store being unguarded and last: the same action
   empties the enemy side and retires 琴琴.  The shared test writes 2 at
   0003a2f9 and nothing puts it back to 0, then this handler overwrites it
   with 1.  An else, or a store gated on the code still being 0, would answer
   2 here. */
static void ch11_a_retired_chinchin_outranks_a_cleared_field(void)
{
    stage_ch11(CH11_STAGE_UNITS, 0);
    stage_ch11_party();
    stage_ch11_unit(7, SIDE_ENEMY, FLAG_RETIRED);
    ch11_units[CHINCHIN_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_11_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The ordinary defeat: the battle is still going -- a live enemy settles the
   shared test at 0 -- and a retired 琴琴 turns that into 1. */
static void ch11_a_retired_chinchin_is_a_defeat(void)
{
    stage_ch11(CH11_STAGE_UNITS, 0);
    stage_ch11_party();
    stage_ch11_unit(7, SIDE_ENEMY, 0);
    ch11_units[CHINCHIN_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_11_post_action();
    CHECK_EQ(end_code(), 1);
}

/* With 琴琴 still in play the handler adds nothing at all, so both of the
   shared test's own answers come through unchanged. */
static void ch11_a_live_chinchin_leaves_the_shared_verdict_alone(void)
{
    stage_ch11(CH11_STAGE_UNITS, 0);
    stage_ch11_party();
    stage_ch11_unit(7, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_11_post_action();
    CHECK_EQ(end_code(), 2);

    stage_ch11(CH11_STAGE_UNITS, 0);
    stage_ch11_party();
    stage_ch11_unit(7, SIDE_ENEMY, 0);
    fdps_chapter_11_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The CALL to the shared test is really made and chapter 11 takes its
   ordinary arm: chapter id 10 is not 0x10 or 0x15, so the watched slot in
   there is 0, and a retired slot 0 is a defeat with 琴琴 untouched. */
static void ch11_the_shared_slot_zero_test_still_runs(void)
{
    stage_ch11(CH11_STAGE_UNITS, 0);
    stage_ch11_party();
    stage_ch11_unit(7, SIDE_ENEMY, 0);
    ch11_units[0].flags = FLAG_RETIRED;
    fdps_chapter_11_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The 琴琴 test sits outside the shared test's gate.  A verdict a chapter
   event already recorded makes the shared test return at 0003a2ec without
   examining anything, and a retired 琴琴 still overwrites it with 1; with her
   in play the recorded verdict survives. */
static void ch11_the_chinchin_test_survives_a_recorded_verdict(void)
{
    stage_ch11(CH11_STAGE_UNITS, 2);
    stage_ch11_party();
    stage_ch11_unit(7, SIDE_ENEMY, 0);
    ch11_units[CHINCHIN_SLOT].flags = FLAG_RETIRED;
    fdps_chapter_11_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch11(CH11_STAGE_UNITS, 2);
    stage_ch11_party();
    stage_ch11_unit(7, SIDE_ENEMY, 0);
    fdps_chapter_11_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Only slots 0 and 8 end this battle from code.  All nine slots are the party
   here -- which is the real shape of this map, whose 49 deployment records
   all begin at index 9 -- so the shared test finds no live enemy and its
   verdict is 2, and each of slots 1..7 retires in turn against that.  A
   defeat test the handler does not have would answer 1: the literal in the
   PUSH having drifted to a neighbouring slot, or a second test having been
   invented by analogy with chapter 9's pair. */
static void ch11_no_slot_but_zero_and_eight_ends_the_battle(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < CHINCHIN_SLOT; retired_slot++) {
        stage_ch11(CH11_STAGE_UNITS, 0);
        stage_ch11_party();
        stage_ch11_unit(7, SIDE_PLAYER, 0);
        ch11_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_11_post_action();
        CHECK_EQ(end_code(), 2);
    }
}

/* ------------------------------------------------------------------ *
 * Chapter 12's handler, 0003aa10.  Instruction for instruction chapters 2
 * and 7's: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0, CALL 0x0003a2e0
 * at 0003aa1c, then the four bare POPs and RET at 0003aa21..0003aa25.  No
 * store, no compare, no second call, so the two things worth pinning are
 * that the shared default end test really runs and that nothing else does.
 *
 * The second half is the whole risk.  The table slot just before this one --
 * chapter 11's -- follows the shared test with PUSH 0x8 / CALL 0x000109b0 and
 * an unguarded MOV dword ptr [0x00069da0],0x1, and by chapter 12 the roster is
 * long enough for a slot at that index to exist, so carrying that shape one
 * slot further is the natural mistake.  The guide's chapter 12 entry,
 * 火神的宮殿（眾神的兵器）, gives 勝利條件 敵人全滅 and 失敗條件 蘭迪斯死亡 --
 * one lose condition, and it is the shared test's slot 0.  So no slot but 0
 * may end this battle from code, and the sweep below holds every one of them
 * to that.
 *
 * The chapter's own peculiarity, the choice of guardian room the game puts to
 * the player once at the start, is not in this function either: it settles
 * which guardian is fought and what can be obtained later, not how the battle
 * ends, and the win/lose pair is the same whichever room was entered.  The
 * handler examines nothing at all, so no case below varies it.
 *
 * Chapter id 11 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 * ------------------------------------------------------------------ */

/* Chapter 12 is chapter id 11, table slot 11: the dword at 000602b8, eleven
   entries into the table based at 0006028c, is 0003aa10. */
#define CHAPTER_12_ID 11

/* Same staging as above with the chapter id moved to chapter 12's. */
static void stage_ch12(int live_unit_count, int battle_end_code)
{
    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_12_ID;
}

/* The CALL is really taken: with every enemy retired the shared test's up
   front 2 at 0003a2f9 survives, and a handler whose body did nothing would
   leave the 0 it was given.  This is the chapter's stated win condition,
   敵人全滅. */
static void ch12_the_shared_end_test_runs(void)
{
    stage_ch12(5, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_12_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0 at 0003a34a, so the walk inside the
   shared test is reached through this handler and not short circuited by
   anything in front of the CALL. */
static void ch12_a_live_enemy_keeps_the_battle_going(void)
{
    stage_ch12(5, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_12_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The chapter's one stated lose condition, 蘭迪斯死亡, and it comes entirely
   from the shared test: chapter id 11 is not 0x10 or 0x15, so the watched slot
   in there is 0, and its store carries no guard -- the defeat stands even in
   the call that emptied the enemy side. */
static void ch12_a_retired_randis_is_a_defeat(void)
{
    stage_ch12(5, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_ENEMY, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_12_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch12(5, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_12_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  Every other slot retires in
   turn with a live enemy at slot 7 holding the shared test's answer at 0, so
   anything but 0 would be a defeat test the handler does not have -- chapter
   11's slot-8 test having been carried one slot further, or one invented for
   whichever guardian-room guest the chapter is thought to owe a condition. */
static void ch12_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        stage_ch12(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_12_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded survives the handler untouched: the shared
   test's gate at 0003a2ec returns before anything is examined, and this
   handler adds no store of its own on either side of the CALL.  Each value
   below would be overwritten by a body that ran -- the array holds a live
   enemy, which would settle the code at 0. */
static void ch12_a_recorded_verdict_is_left_alone(void)
{
    stage_ch12(2, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_12_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch12(2, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_12_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open, rather than
   being reset or cleared by an initialisation the frame does not have. */
static void ch12_an_open_battle_stays_open(void)
{
    stage_ch12(2, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_12_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_12_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------ *
 * Chapter 13's handler, 0003aa70.  Instruction for instruction chapters 2, 7
 * and 12's: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0, CALL 0x0003a2e0
 * at 0003aa7c, then the four bare POPs and RET at 0003aa81..0003aa85.  No
 * store, no compare, no second call, so the two things worth pinning are that
 * the shared default end test really runs and that nothing else does.
 *
 * The second half is the whole risk.  Chapter 9's handler asks about slots 6
 * and 7 -- PUSH 0x6 at 0003a851, PUSH 0x7 at 0003a85f -- and chapter 11's,
 * two table slots before this one, asks about slot 8 at 0003a9b1, each
 * followed by a store of 1 that is gated on nothing but its own test.  By
 * chapter 13 the roster is long enough for a slot at those indices to exist,
 * so carrying one of those shapes further along the table is the natural
 * mistake.  The guide's chapter 13 entry, 地獄三鬥神, gives 勝利條件 敵人全滅
 * and 失敗條件 蘭迪斯死亡 -- one lose condition, and it is the shared test's
 * slot 0.  So no slot but 0 may end this battle from code; the sweep below
 * holds slots 1 through 8 to that, which is every literal those neighbouring
 * handlers carry.
 *
 * That there is no such test here at all is also settled directly: a search
 * of the whole image for CALL 0x000109b0, fdps_unit_is_retired, returns 62
 * sites and not one of them lies between 0003aa70 and the RET at 0003aa85 --
 * the two nearest are 0003a9b3 in chapter 11's handler and 0003ab51 in
 * chapter 15's.
 *
 * The chapter's named trio, 薩達特, 席拉 and 巴魯, are enemy deployments
 * and the guide lists no guest on the player side, so there is no unit the
 * chapter owes a condition to.  The handler examines nothing at all, so no
 * case below varies its input beyond the array the shared test walks.
 *
 * Chapter id 12 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 * ------------------------------------------------------------------ */

/* Chapter 13 is chapter id 12, table slot 12: the dword at 000602bc, twelve
   entries into the table based at 0006028c, is 0003aa70. */
#define CHAPTER_13_ID 12

/* Ten slots, two more than the shared array holds.  The sweep below has to
   retire every index a table neighbour's own defeat test names -- 6 and 7 from
   chapter 9's pair, 8 from chapter 11's -- and still leave a live enemy
   standing somewhere, and an fdps_unit_is_retired(8) staged over the shared
   eight-slot array would read one record past its end and constrain nothing.
   Chapter 11's section above makes its array nine slots for the same reason. */
#define CH13_STAGE_UNITS 10

/* The last slot, kept out of the sweep as the live enemy that holds the shared
   test's answer at 0 while each of the others retires in turn. */
#define CH13_ENEMY_SLOT 9

static struct fdps_unit_record ch13_units[CH13_STAGE_UNITS];

/* The same staging as stage(), over the ten-slot array and with the chapter id
   moved to chapter 13's. */
static void stage_ch13(int live_unit_count, int battle_end_code)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch13_units;
    for (i = 0; i < (int) sizeof(ch13_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch13_units;
    data_fdps_map_unit_count = live_unit_count;
    data_fdps_chapter_current_chapter_id = CHAPTER_13_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void stage_ch13_unit(int unit_index, int side, int flags)
{
    ch13_units[unit_index].side = (unsigned char) side;
    ch13_units[unit_index].flags = (unsigned char) flags;
}

/* The CALL is really taken: with every enemy retired the shared test's up
   front 2 at 0003a2f9 survives, and a handler whose body did nothing would
   leave the 0 it was given.  This is the chapter's stated win condition,
   敵人全滅. */
static void ch13_the_shared_end_test_runs(void)
{
    stage_ch13(5, 0);
    stage_ch13_unit(0, SIDE_PLAYER, 0);
    stage_ch13_unit(1, SIDE_PLAYER, 0);
    stage_ch13_unit(2, SIDE_PLAYER, 0);
    stage_ch13_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_ch13_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_13_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0 at 0003a34a, so the walk inside the
   shared test is reached through this handler and not short circuited by
   anything in front of the CALL. */
static void ch13_a_live_enemy_keeps_the_battle_going(void)
{
    stage_ch13(5, 0);
    stage_ch13_unit(0, SIDE_PLAYER, 0);
    stage_ch13_unit(1, SIDE_PLAYER, 0);
    stage_ch13_unit(2, SIDE_PLAYER, 0);
    stage_ch13_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_ch13_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_13_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The chapter's one stated lose condition, 蘭迪斯死亡, and it comes entirely
   from the shared test: chapter id 12 is not 0x10 or 0x15, so the watched slot
   in there is 0, and its store at 0003a390 is gated on the retirement alone and
   not on the code's current value -- the defeat stands even in the call that
   emptied the enemy side and wrote 2 moments earlier. */
static void ch13_a_retired_randis_is_a_defeat(void)
{
    stage_ch13(5, 0);
    stage_ch13_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_ch13_unit(1, SIDE_PLAYER, 0);
    stage_ch13_unit(2, SIDE_PLAYER, 0);
    stage_ch13_unit(3, SIDE_ENEMY, 0);
    stage_ch13_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_13_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch13(5, 0);
    stage_ch13_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_ch13_unit(1, SIDE_PLAYER, 0);
    stage_ch13_unit(2, SIDE_PLAYER, 0);
    stage_ch13_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_ch13_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_13_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code.  Slots 1 through 8 retire in
   turn, each against a live enemy at slot 9 that holds the shared test's
   answer at 0, so a defeat test this handler does not have would show up as a
   1.  The range is chosen rather than convenient: 6 and 7 are the literals
   chapter 9's pair pushes, 8 is chapter 11's, and those three are what a
   handler copied further along the table would carry.  A guest of its own is
   the other way this could go wrong, and the chapter fields none. */
static void ch13_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < CH13_ENEMY_SLOT; retired_slot++) {
        stage_ch13(CH13_STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < CH13_ENEMY_SLOT; player_slot++) {
            stage_ch13_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_ch13_unit(CH13_ENEMY_SLOT, SIDE_ENEMY, 0);
        ch13_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_13_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded survives the handler untouched: the shared
   test's gate at 0003a2ec returns before anything is examined, and this
   handler adds no store of its own on either side of the CALL.  Each value
   below would be overwritten by a body that ran -- the array holds a live
   enemy, which would settle the code at 0. */
static void ch13_a_recorded_verdict_is_left_alone(void)
{
    stage_ch13(2, 1);
    stage_ch13_unit(0, SIDE_PLAYER, 0);
    stage_ch13_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_13_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch13(2, 2);
    stage_ch13_unit(0, SIDE_PLAYER, 0);
    stage_ch13_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_13_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open, rather than
   being reset or cleared by an initialisation the frame does not have. */
static void ch13_an_open_battle_stays_open(void)
{
    stage_ch13(2, 0);
    stage_ch13_unit(0, SIDE_PLAYER, 0);
    stage_ch13_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_13_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_13_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------ *
 * Chapter 14's handler, 0003aad0.  Instruction for instruction chapters 2, 7,
 * 12 and 13's: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0, CALL
 * 0x0003a2e0 at 0003aadc, then the four bare POPs and RET at
 * 0003aae1..0003aae5.  No store, no compare, no second call, so the two things
 * worth pinning are that the shared default end test really runs and that
 * nothing else does.
 *
 * The second half carries more weight here than it does for the four handlers
 * of the same shape above.  The guide's chapter 14 entry,
 * 天空之騎士, gives 勝利條件
 * 敵人全滅 and 失敗條件
 * 蘭迪斯或法蓮娜死亡: a second lose
 * condition, and this handler does not implement it.  The neighbouring
 * handlers show exactly what implementing it would look like -- PUSH 0x3 at
 * 0003a571, 0003a5e1 and 0003a653 for chapters 4, 5 and 6, slot 3 being
 * 法蓮娜 herself on the last two of those, and PUSH 0x4 at
 * 0003ab4f with PUSH 0x35 at 0003ab5d for chapter 15 in the very next table
 * slot -- so a copied slot test is the concrete way this function goes wrong.
 *
 * That there is no such test is settled directly from the image: a search for
 * CALL 0x000109b0, fdps_unit_is_retired, returns 62 sites and not one of them
 * lies between 0003aad0 and the RET at 0003aae5; the two nearest are 0003a9b3
 * in chapter 11's handler and 0003ab51 in chapter 15's.  The sweep below
 * states the same thing behaviourally, and over every slot rather than a
 * chosen few: 55 slots are staged and each of 1 through 53 retires in turn,
 * which covers every literal any post-action handler in the table pushes,
 * chapter 15's 0x35 included.
 *
 * Chapter id 13 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 * ------------------------------------------------------------------ */

/* Chapter 14 is chapter id 13, table slot 13: the dword at 000602c0, thirteen
   entries into the table based at 0006028c, is 0003aad0. */
#define CHAPTER_14_ID 13

/* Fifty-five slots, so that the sweep can retire every index a post-action
   handler anywhere in the table names -- 3 from chapters 4, 5 and 6, 4 and
   0x35 from chapter 15, 6 and 7 from chapter 9, 8 from chapter 11 -- and still
   leave a live enemy standing past the highest of them.  An
   fdps_unit_is_retired staged over a shorter array would read past its end and
   constrain nothing. */
#define CH14_STAGE_UNITS 55

/* The last slot, kept out of the sweep as the live enemy that holds the shared
   test's answer at 0 while each of the others retires in turn. */
#define CH14_ENEMY_SLOT 54

static struct fdps_unit_record ch14_units[CH14_STAGE_UNITS];

/* The same staging as stage(), over the fifty-five-slot array and with the
   chapter id moved to chapter 14's. */
static void stage_ch14(int live_unit_count, int battle_end_code)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch14_units;
    for (i = 0; i < (int) sizeof(ch14_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch14_units;
    data_fdps_map_unit_count = live_unit_count;
    data_fdps_chapter_current_chapter_id = CHAPTER_14_ID;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static void stage_ch14_unit(int unit_index, int side, int flags)
{
    ch14_units[unit_index].side = (unsigned char) side;
    ch14_units[unit_index].flags = (unsigned char) flags;
}

/* The CALL is really taken: with every enemy retired the shared test's up
   front 2 at 0003a2f9 survives, and a handler whose body did nothing would
   leave the 0 it was given.  This is the chapter's stated win condition,
   敵人全滅. */
static void ch14_the_shared_end_test_runs(void)
{
    stage_ch14(5, 0);
    stage_ch14_unit(0, SIDE_PLAYER, 0);
    stage_ch14_unit(1, SIDE_PLAYER, 0);
    stage_ch14_unit(2, SIDE_PLAYER, 0);
    stage_ch14_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_ch14_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_14_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0 at 0003a34a, so the walk inside the
   shared test is reached through this handler and not short circuited by
   anything in front of the CALL. */
static void ch14_a_live_enemy_keeps_the_battle_going(void)
{
    stage_ch14(5, 0);
    stage_ch14_unit(0, SIDE_PLAYER, 0);
    stage_ch14_unit(1, SIDE_PLAYER, 0);
    stage_ch14_unit(2, SIDE_PLAYER, 0);
    stage_ch14_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_ch14_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_14_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The first of the chapter's two stated lose conditions,
   蘭迪斯死亡, and it comes entirely from the shared test:
   chapter id 13 is not 0x10 or 0x15, so the watched slot in there is 0, and its
   store at 0003a390 is gated on the retirement alone and not on the code's
   current value -- the defeat stands even in the call that emptied the enemy
   side and wrote 2 moments earlier. */
static void ch14_a_retired_randis_is_a_defeat(void)
{
    stage_ch14(5, 0);
    stage_ch14_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_ch14_unit(1, SIDE_PLAYER, 0);
    stage_ch14_unit(2, SIDE_PLAYER, 0);
    stage_ch14_unit(3, SIDE_ENEMY, 0);
    stage_ch14_unit(4, SIDE_ENEMY, 0);
    fdps_chapter_14_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch14(5, 0);
    stage_ch14_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_ch14_unit(1, SIDE_PLAYER, 0);
    stage_ch14_unit(2, SIDE_PLAYER, 0);
    stage_ch14_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_ch14_unit(4, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_14_post_action();
    CHECK_EQ(end_code(), 1);
}

/* No unit slot but 0 ends this battle from code, and here that has to hold for
   the chapter's second stated lose condition too: 法蓮娜 is on the
   player side throughout chapter 14 and the handler still asks about no slot at
   all, so whichever index she stands at, retiring her leaves the battle open.
   Slots 1 through 53 retire in turn against a live enemy at slot 54 that holds
   the shared test's answer at 0, so a defeat test this handler does not have
   would show up as a 1.  The range is every slot up to and including the
   highest literal any handler in the table pushes, chapter 15's 0x35. */
static void ch14_no_other_slot_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < CH14_ENEMY_SLOT; retired_slot++) {
        stage_ch14(CH14_STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < CH14_ENEMY_SLOT; player_slot++) {
            stage_ch14_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_ch14_unit(CH14_ENEMY_SLOT, SIDE_ENEMY, 0);
        ch14_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_14_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* A verdict already recorded survives the handler untouched: the shared test's
   gate at 0003a2ec returns before anything is examined, and this handler adds
   no store of its own on either side of the CALL.  Each value below would be
   overwritten by a body that ran -- the array holds a live enemy, which would
   settle the code at 0. */
static void ch14_a_recorded_verdict_is_left_alone(void)
{
    stage_ch14(2, 1);
    stage_ch14_unit(0, SIDE_PLAYER, 0);
    stage_ch14_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_14_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch14(2, 2);
    stage_ch14_unit(0, SIDE_PLAYER, 0);
    stage_ch14_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_14_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The handler stores nothing of its own before the CALL either: a battle that
   is still open and has nothing to decide comes back still open, rather than
   being reset or cleared by an initialisation the frame does not have. */
static void ch14_an_open_battle_stays_open(void)
{
    stage_ch14(2, 0);
    stage_ch14_unit(0, SIDE_PLAYER, 0);
    stage_ch14_unit(1, SIDE_ENEMY, 0);
    fdps_chapter_14_post_action();
    CHECK_EQ(end_code(), 0);
    fdps_chapter_14_post_action();
    CHECK_EQ(end_code(), 0);
}

/* ------------------------------------------------------------------ *
 * Chapter 1's handler, 0003a3b0.  The shared test, then one defeat test of
 * its own with a spoken line in front of the store: CALL 0x0003a2e0, then
 * PUSH 0x2 / CALL 0x000109b0 / ADD ESP,0x4 / TEST EAX,EAX / JZ 0003a3fc,
 * and behind that JZ the seven-push fdps_draw_text call at
 * 0003a3cf..0003a3ef and the MOV dword ptr [0x00069da0],0x1 at 0003a3f2.
 *
 * The risk set is chapter 4's -- the order of the two tests, the absence of
 * any guard on the store, and the literal 2 in the PUSH -- restaged on this
 * chapter's map.  map00.dat declares one player slot, so the roster fills
 * slot 0 with 蘭迪斯 alone and the opening ICON00.DAT script deploys the
 * map's lone enemy into slot 1 and the guest 索爾 into slot 2.  The
 * strategy guide's chapter 1 entry lists exactly two lose conditions,
 * 蘭迪斯 dying and 索爾 dying, and unlike chapters 2 and 5 -- which carry
 * their 索爾 condition as a map death script -- both of chapter 1's come
 * from code: slot 0 from the shared test and slot 2 from here.
 *
 * Chapter id 0 is neither 0x10 nor 0x15, so inside the shared test the arm
 * taken is PUSH 0x0 at 0003a382 -- slot 0, 蘭迪斯.
 *
 * WHETHER THE LINE IS DRAWN IS NOT ASSERTED BELOW.  fdps_draw_text takes
 * its whole effect through pixels at the VGA aperture, keeps no state and
 * returns a cursor this handler discards, so a unit test has nothing to
 * read back; the entry id is a literal in the instruction stream (PUSH 0xf
 * at 0003a3e2) and the reviewer's reading of it is what stands behind the
 * emitted C.  What the staging below does do is keep the call harmless: the
 * chapter text block is a fixture whose every entry names one lone
 * terminator, so fdps_draw_text walks the entry, paints nothing and returns
 * at once.  The real entry 15 opens with a speaker token and would stand a
 * modal wait on a keyboard nothing is typing at.
 * ------------------------------------------------------------------ */

/* Chapter 1 is chapter id 0, table slot 0: the dword at 0006028c, the base of
   the table itself, is 0003a3b0. */
#define CHAPTER_01_ID 0

/* The slot chapter 1's own defeat test asks about -- PUSH 0x2 at 0003a3c1.
   On map00.dat that slot is the guest 索爾, deployed there by the opening
   script rather than filled by the roster pass. */
#define SOL_SLOT 2

/* The synthetic chapter text block.  fdps_draw_text takes a table of signed
   16-bit byte offsets measured from the block's own base and walks the stream
   it points at until the token -1 (text.h), so a table whose every entry names
   one lone terminator draws nothing at all and needs no font staged.  Sixteen
   entries is what FDETXT01.TXT holds and covers the id 15 this handler asks
   for. */
#define CH01_TEXT_ENTRIES 16
#define CH01_TEXT_TERMINATOR (-1)

static short ch01_text[CH01_TEXT_ENTRIES + 1];

/* Same staging as chapter 2's with the chapter id moved to chapter 1's, plus
   the text block the draw inside the defeat arm reads. */
static void stage_ch01(int live_unit_count, int battle_end_code)
{
    int entry;

    for (entry = 0; entry < CH01_TEXT_ENTRIES; entry++) {
        ch01_text[entry] = (short) (CH01_TEXT_ENTRIES * 2);
    }
    ch01_text[CH01_TEXT_ENTRIES] = CH01_TEXT_TERMINATOR;

    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_01_ID;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch01_text;
}

/* The CALL at 0003a3bc is really taken: with every enemy retired and 索爾
   standing, the shared test's up front 2 survives the handler, and a body that
   did nothing would leave the 0 it was given. */
static void ch01_the_shared_end_test_runs(void)
{
    stage_ch01(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(SOL_SLOT, SIDE_GUEST, 0);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 2);
}

/* One live enemy puts the code back to 0 inside the shared test, and the
   defeat arm is not entered, so the battle carries on. */
static void ch01_a_live_enemy_keeps_the_battle_going(void)
{
    stage_ch01(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(SOL_SLOT, SIDE_GUEST, 0);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 0);
}

/* Chapter 1 is not 0x10 or 0x15, so the shared test watches unit slot 0 --
   蘭迪斯 -- and that half of the chapter's rule set is reached through this
   handler unchanged, whether the field is cleared or not. */
static void ch01_a_retired_randis_is_a_defeat(void)
{
    stage_ch01(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch01(3, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The whole point of the store being unguarded and last: the same action
   empties the enemy side and retires 索爾.  The shared test writes 2 at
   0003a2f9 and nothing puts it back to 0, then this handler overwrites it with
   1.  An else, or a store gated on the code still being 0, would answer 2
   here. */
static void ch01_a_retired_sol_outranks_a_cleared_field(void)
{
    stage_ch01(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(SOL_SLOT, SIDE_GUEST, FLAG_RETIRED);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The ordinary defeat: the battle is still going -- a live enemy settles the
   shared test at 0 -- and the retired 索爾 turns that into 1. */
static void ch01_a_retired_sol_is_a_defeat(void)
{
    stage_ch01(4, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(SOL_SLOT, SIDE_GUEST, FLAG_RETIRED);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The other side of the JZ at 0003a3cd: with 索爾 standing the branch is not
   taken and the shared test's verdict is what comes out, whichever it was. */
static void ch01_a_live_sol_leaves_the_shared_verdict_alone(void)
{
    stage_ch01(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 0);

    stage_ch01(3, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The shared test abandons its body the moment it finds the code non-zero --
   CMP dword ptr [0x00069da0],0x0 / JNZ at 0003a2ec -- but this handler's own
   test runs on that path too, because it sits after the CALL and reads nothing
   before it stores.  So a verdict a chapter event recorded is overwritten by a
   retired 索爾, and left alone when he is standing. */
static void ch01_the_sol_test_survives_a_recorded_verdict(void)
{
    stage_ch01(3, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(SOL_SLOT, SIDE_GUEST, FLAG_RETIRED);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch01(3, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(SOL_SLOT, SIDE_GUEST, 0);
    fdps_chapter_01_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Only slots 0 and 2 end this battle from code.  Every other slot retires in
   turn with a live enemy at slot 7 holding the shared test's answer at 0, so
   anything but 0 would be a defeat test the handler does not have -- the
   literal in the PUSH having drifted, or a second one having been invented. */
static void ch01_no_slot_but_zero_and_two_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        if (retired_slot == SOL_SLOT) {
            continue;
        }
        stage_ch01(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_01_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* ------------------------------------------------------------------ *
 * Chapter 3's handler, 0003a4b0.  The only handler in this file whose two
 * tests are ALTERNATIVES: PUSH 0x0 / CALL 0x000109b0 / ADD ESP,0x4 / TEST
 * EAX,EAX / JZ 0003a4d6, the MOV dword ptr [0x00069da0],0x1 at 0003a4ca and
 * then JMP 0003a511 straight to the epilogue, so the PUSH 0x4 / CALL
 * 0x000109b0 at 0003a4d6 is only reached while slot 0 is standing.  Behind
 * the second JZ sit the seven-push fdps_draw_text call at 0003a4e4..0003a4ff
 * and MOV dword ptr [0x00069da0],0x2 at 0003a507.
 *
 * There is no CALL 0x0003a2e0 in the body at all, so the shared default end
 * test is not part of this chapter: the guide gives it as 勝利條件 廿二回合
 * 內打倒魔導士 against 失敗條件 蘭迪斯死亡, and emptying the enemy side is
 * not one of them.  map02.dat fields three player slots, so the roster fills
 * 0..2 with 蘭迪斯, 尤利安 and 亞克 and the wave-0 deploy appends the guest
 * 索爾 at slot 3 and the 魔導士 at slot 4.
 *
 * The risk this file exists to catch is the else: chapters 1, 4, 5, 6, 9 and
 * 11 all write their second test as an unguarded if, and there the override
 * runs in the safe direction because it stamps a defeat over a clear.  Here
 * the arms are reversed, so copying that shape would let an action that
 * retires 蘭迪斯 and the 魔導士 together answer 2 instead of 1.
 *
 * The twenty-two turn half of the victory condition is not asserted here
 * because it is not in this function: map02.dat's turn-event table fires
 * chapter-event slot 5 when the counter reaches 22.
 *
 * WHETHER THE LINE IS DRAWN IS NOT ASSERTED BELOW, for chapter 1's reason --
 * fdps_draw_text takes its whole effect through pixels at the VGA aperture
 * and returns a cursor this handler discards, so a unit test has nothing to
 * read back; the entry id is the literal PUSH 0xd at 0003a4f7.  The fixture
 * below only keeps the call harmless.
 * ------------------------------------------------------------------ */

/* Chapter 3 is chapter id 2, table slot 2: the dword at 00060294, two entries
   into the table based at 0006028c, is 0003a4b0. */
#define CHAPTER_03_ID 2

/* The slot chapter 3's victory test asks about -- PUSH 0x4 at 0003a4d6.  On
   map02.dat that slot is the 魔導士, the second of the map's two wave-0
   deployment records and the only enemy standing when it opens. */
#define MAGE_SLOT 4

/* The synthetic chapter text block, built like chapter 1's: every entry names
   one lone terminator, so fdps_draw_text walks it, paints nothing and returns
   at once with no font staged.  Twenty-four entries is what FDETXT03.TXT
   holds and covers the id 13 this handler asks for; the real entry 13 opens
   with the portrait token for character id 0x66 and would stand a modal wait
   on a keyboard nothing is typing at. */
#define CH03_TEXT_ENTRIES 24
#define CH03_TEXT_TERMINATOR (-1)

static short ch03_text[CH03_TEXT_ENTRIES + 1];

/* Same staging as chapter 2's with the chapter id moved to chapter 3's, plus
   the text block the draw inside the victory arm reads. */
static void stage_ch03(int live_unit_count, int battle_end_code)
{
    int entry;

    for (entry = 0; entry < CH03_TEXT_ENTRIES; entry++) {
        ch03_text[entry] = (short) (CH03_TEXT_ENTRIES * 2);
    }
    ch03_text[CH03_TEXT_ENTRIES] = CH03_TEXT_TERMINATOR;

    stage(live_unit_count, battle_end_code);
    data_fdps_chapter_current_chapter_id = CHAPTER_03_ID;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch03_text;
}

/* The first arm: a retired slot 0 is the chapter's one lose condition, and it
   is decided here rather than in the shared test.  The 魔導士 is standing and
   the field is not cleared, so nothing else could have written the 1. */
static void ch03_a_retired_randis_is_a_defeat(void)
{
    stage_ch03(5, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_GUEST, 0);
    stage_unit(MAGE_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The whole reason this handler is an else-if: the same action retires
   蘭迪斯 and the 魔導士.  The JMP at 0003a4d4 takes the defeat arm past the
   unit-4 test entirely, so the answer is 1.  Written as two unguarded ifs --
   which is the shape of every other handler in this file -- the victory arm
   would run second and answer 2, clearing a chapter the original loses. */
static void ch03_a_retired_randis_outranks_a_retired_mage(void)
{
    stage_ch03(5, 0);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_GUEST, 0);
    stage_unit(MAGE_SLOT, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The second arm: with 蘭迪斯 standing the unit-4 test is reached, and a
   retired 魔導士 clears the chapter even though the rest of the enemy side is
   still on the field -- which is the guide's condition and not the shared
   test's. */
static void ch03_a_retired_mage_is_a_clear(void)
{
    stage_ch03(6, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_GUEST, 0);
    stage_unit(MAGE_SLOT, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(5, SIDE_ENEMY, 0);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Neither arm taken: the handler has no unconditional store anywhere in the
   body, so whatever the battle loop or a chapter event left in the code comes
   back untouched. */
static void ch03_neither_test_firing_leaves_the_code_alone(void)
{
    stage_ch03(6, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(MAGE_SLOT, SIDE_ENEMY, 0);
    stage_unit(5, SIDE_ENEMY, 0);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 0);

    stage_ch03(6, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(MAGE_SLOT, SIDE_ENEMY, 0);
    stage_unit(5, SIDE_ENEMY, 0);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch03(6, 2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(MAGE_SLOT, SIDE_ENEMY, 0);
    stage_unit(5, SIDE_ENEMY, 0);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Neither store consults the code before writing, because there is no gate in
   front of either: this handler does not call the shared test and so inherits
   none of its CMP dword ptr [0x00069da0],0x0 / JNZ early return.  So a verdict
   a chapter event recorded is overwritten by either arm, in both directions --
   a recorded clear becomes the defeat, a recorded defeat becomes the clear. */
static void ch03_a_recorded_verdict_is_overwritten(void)
{
    stage_ch03(5, 2);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(MAGE_SLOT, SIDE_ENEMY, 0);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch03(5, 1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(MAGE_SLOT, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The shared default end test is not called.  Every unit on the enemy side is
   retired, which is the one situation where that test writes its 2 and leaves
   it standing; slot 0 and slot 4 are both live, so this handler writes
   nothing and the code stays 0.  Slot 4 is staged on the player side here
   precisely because it has to be standing while the enemy side is empty --
   the index is a position in the array and nothing about the record it finds
   is read by fdps_unit_is_retired except the retirement bit. */
static void ch03_the_shared_end_test_is_not_run(void)
{
    stage_ch03(6, 0);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(MAGE_SLOT, SIDE_PLAYER, 0);
    stage_unit(5, SIDE_ENEMY, FLAG_RETIRED);
    fdps_chapter_03_post_action();
    CHECK_EQ(end_code(), 0);
}

/* Only slots 0 and 4 decide anything.  Every other slot retires in turn with
   both of those standing, so any non-zero answer would be a test the handler
   does not have -- a literal in one of the two PUSHes having drifted, or a
   third test having been invented.  Slot 7 stays a live enemy throughout; the
   case above is what holds the shared test out, not this sweep. */
static void ch03_no_slot_but_zero_and_four_ends_the_battle(void)
{
    int retired_slot;
    int player_slot;

    for (retired_slot = 1; retired_slot < STAGE_UNITS - 1; retired_slot++) {
        if (retired_slot == MAGE_SLOT) {
            continue;
        }
        stage_ch03(STAGE_UNITS, 0);
        for (player_slot = 0; player_slot < STAGE_UNITS - 1; player_slot++) {
            stage_unit(player_slot, SIDE_PLAYER, 0);
        }
        stage_unit(STAGE_UNITS - 1, SIDE_ENEMY, 0);
        stage_units[retired_slot].flags = FLAG_RETIRED;
        fdps_chapter_03_post_action();
        CHECK_EQ(end_code(), 0);
    }
}


/* ------------------------------------------------------------------ *
 * Chapter 8's handler, 0003a710.  Three rules of its own and no CALL
 * 0x0003a2e0 anywhere in the body: PUSH 0x0 / CALL 0x000109b0 and an
 * unguarded MOV dword ptr [0x00069da0],0x1 at 0003a71c..0003a72a, then the
 * turn-gated CMP dword ptr [0x00069ce8],0x3 / JLE over PUSH 0x13 / CALL
 * 0x000109b0 at 0003a734..0003a74d, then the four-term chain PUSH 0xf, 0x10,
 * 0x11, 0x12 at 0003a757..0003a785 over CMP byte ptr [0x000640e9],0x0 / JNZ
 * 0003a7cd at 0003a795 and its two endings.
 *
 * The risk set is the three the assembly makes easy to get wrong.  The
 * missing shared test, which would add a victory condition the chapter does
 * not have and would move the slot-0 rule somewhere it is not.  The turn
 * gate on the 費塔加 test, which is what keeps the handler from reporting a
 * defeat over a unit slot the chapter script has not deployed yet.  And the
 * escape tally, where the clear is every value but 0 and not "all four got
 * out" -- the guide's 失敗條件 村民全滅 is exactly the 0.
 *
 * The staging is this file's, widened: the handler asks about unit index
 * 0x13, past the eight slots the other cases share, so chapter 8 publishes a
 * twenty-record array of its own.  The handler never reads a side byte or
 * the unit count -- it has no walk of the array in it -- so the only fields
 * that matter are the retirement bits.
 *
 * WHETHER EITHER LINE IS DRAWN IS NOT ASSERTED BELOW.  fdps_draw_text takes
 * its whole effect through pixels at the VGA aperture, keeps no state and
 * returns a cursor this handler discards, so a unit test has nothing to read
 * back; the two entry ids are literals in the instruction stream (PUSH 0x1b
 * at 0003a7bb and PUSH 0x23 at 0003a7e0).  The staging keeps the call
 * harmless the same way chapters 1 and 3 do: a fixture text block whose every
 * entry names one lone terminator, so fdps_draw_text walks the entry, paints
 * nothing and returns at once.
 * ------------------------------------------------------------------ */

/* Chapter 8 is chapter id 7, table slot 7: the dword at 000602a8, seven
   entries into the table based at 0006028c, is 0003a710. */
#define CHAPTER_08_ID 7

/* The four slots the handler's third rule asks about -- PUSH 0xf, 0x10, 0x11
   and 0x12 -- and the two it tests on its own account, PUSH 0x0 and PUSH
   0x13. */
#define CH08_RANDIS_SLOT 0
#define CH08_VILLAGER_1_SLOT 0x0f
#define CH08_VILLAGER_2_SLOT 0x10
#define CH08_VILLAGER_3_SLOT 0x11
#define CH08_VILLAGER_4_SLOT 0x12
#define CH08_GUEST_MAGE_SLOT 0x13

/* Twenty records, one past the highest index the handler names. */
#define CH08_STAGE_UNITS 20

/* Element 0x11 of data_fdps_map_cell_event_triggered_flags, the byte at
   0x000640e9 the ending reads. */
#define CH08_TALLY_SLOT 0x11

/* The turn the 費塔加 rule is armed after, CMP dword ptr [0x00069ce8],0x3 /
   JLE at 0003a734: the test runs only above 3. */
#define CH08_GUEST_MAGE_ARMED_AFTER_TURN 3

/* The synthetic chapter text block, built like chapter 1's and chapter 3's:
   every entry names one lone terminator, so fdps_draw_text walks it, paints
   nothing and returns at once with no font staged.  Thirty-six entries is
   what FDETXT08.TXT holds -- its first table slot is the offset 72, which is
   the table's own length at two bytes an entry -- so ids run 0 to 0x23 and
   the 0x23 this handler asks for is the last of them. */
#define CH08_TEXT_ENTRIES 36
#define CH08_TEXT_TERMINATOR (-1)

static struct fdps_unit_record ch08_units[CH08_STAGE_UNITS];
static short ch08_text[CH08_TEXT_ENTRIES + 1];

/* Publish a cleared twenty-record array, the fixture text block, the turn
   counter the gate reads, the escape tally the ending reads and the incoming
   battle-end code.  Every unit starts standing; each case retires only the
   slots it is about. */
static void stage_ch08(int turn_counter, int escaped_villager_count,
                       int battle_end_code)
{
    unsigned char *bytes;
    int entry;
    int i;

    for (entry = 0; entry < CH08_TEXT_ENTRIES; entry++) {
        ch08_text[entry] = (short) (CH08_TEXT_ENTRIES * 2);
    }
    ch08_text[CH08_TEXT_ENTRIES] = CH08_TEXT_TERMINATOR;

    bytes = (unsigned char *) ch08_units;
    for (i = 0; i < (int) sizeof(ch08_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < 32; i++) {
        data_fdps_map_cell_event_triggered_flags[i] = 0;
    }
    data_fdps_map_cell_event_triggered_flags[CH08_TALLY_SLOT] =
        (unsigned char) escaped_villager_count;

    data_fdps_map_unit_array_ptr = (unsigned char *) ch08_units;
    data_fdps_map_unit_count = CH08_STAGE_UNITS;
    data_fdps_chapter_current_chapter_id = CHAPTER_08_ID;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch08_text;
    data_fdps_battle_turn_counter = turn_counter;
    data_fdps_chapter_event_or_battle_end_code =
        (unsigned int) battle_end_code;
}

static void ch08_retire(int unit_index)
{
    ch08_units[unit_index].flags = FLAG_RETIRED;
}

/* Put one record on the player side with the retirement bit clear.  Only the
   case that holds the shared end test out needs this: that test's walk skips
   a record whose side byte is non-zero, and skips a retired one, so a slot
   staged this way is the one thing that keeps its up front 2 standing. */
static void ch08_stand_on_player_side(int unit_index)
{
    ch08_units[unit_index].side = (unsigned char) SIDE_PLAYER;
    ch08_units[unit_index].flags = 0;
}

/* Retire the four captives one short of all of them and the battle is still
   open: the chain at 0003a757..0003a791 is an and, so a single standing
   captive funnels through the JMPs at 0003a773, 0003a783 or 0003a793 to the
   epilogue with no store made.  Each of the four is checked in turn, because
   a chain written as an or -- or one that dropped a term -- would end the
   chapter on any of these four stagings. */
static void ch08_one_captive_still_in_leaves_the_battle_open(void)
{
    int standing_slot;
    int slot;

    for (standing_slot = CH08_VILLAGER_1_SLOT;
         standing_slot <= CH08_VILLAGER_4_SLOT;
         standing_slot++) {
        stage_ch08(1, 2, 0);
        for (slot = CH08_VILLAGER_1_SLOT; slot <= CH08_VILLAGER_4_SLOT;
             slot++) {
            if (slot != standing_slot) {
                ch08_retire(slot);
            }
        }
        fdps_chapter_08_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* All four off the battlefield with at least one of them out alive is the
   clear.  The tally is the byte at 0x000640e9 and the test is CMP against 0,
   so 1, 2, 3 and 4 all take the JNZ at 0003a79c to the victory arm -- a
   handler that asked for all four escapes would answer 1 on the first three
   of these. */
static void ch08_one_survivor_is_enough_to_clear(void)
{
    int escaped;

    for (escaped = 1; escaped <= 4; escaped++) {
        stage_ch08(1, escaped, 0);
        ch08_retire(CH08_VILLAGER_1_SLOT);
        ch08_retire(CH08_VILLAGER_2_SLOT);
        ch08_retire(CH08_VILLAGER_3_SLOT);
        ch08_retire(CH08_VILLAGER_4_SLOT);
        fdps_chapter_08_post_action();
        CHECK_EQ(end_code(), 2);
    }
}

/* The other arm of the same compare: all four gone and none of them out
   alive, which is the guide's 失敗條件 村民全滅.  This is the only way the
   captives can lose the chapter. */
static void ch08_no_survivor_is_a_defeat(void)
{
    stage_ch08(1, 0, 0);
    ch08_retire(CH08_VILLAGER_1_SLOT);
    ch08_retire(CH08_VILLAGER_2_SLOT);
    ch08_retire(CH08_VILLAGER_3_SLOT);
    ch08_retire(CH08_VILLAGER_4_SLOT);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The tally is read as a byte and the compare is against 0, not a signed
   "greater than 0": a tally whose top bit is set still takes the victory arm.
   A count that high cannot arise from the four captives, but declaring the
   array signed or comparing with > would change this answer, and the array is
   data_fdps_map_cell_event_triggered_flags, an unsigned char[32]. */
static void ch08_a_high_bit_tally_still_clears(void)
{
    stage_ch08(1, 0x80, 0);
    ch08_retire(CH08_VILLAGER_1_SLOT);
    ch08_retire(CH08_VILLAGER_2_SLOT);
    ch08_retire(CH08_VILLAGER_3_SLOT);
    ch08_retire(CH08_VILLAGER_4_SLOT);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 2);
}

/* The ending reads element 0x11 of the array and no neighbour of it: with
   0x10 and 0x12 carrying counts and 0x11 clear, the answer is still the
   defeat.  0x000640e9 sits one past the shared one-shot latch at 0x000640e8,
   so an off-by-one on the element index reads a byte other chapters raise. */
static void ch08_the_tally_is_element_seventeen(void)
{
    stage_ch08(1, 0, 0);
    data_fdps_map_cell_event_triggered_flags[CH08_TALLY_SLOT - 1] = 4;
    data_fdps_map_cell_event_triggered_flags[CH08_TALLY_SLOT + 1] = 4;
    ch08_retire(CH08_VILLAGER_1_SLOT);
    ch08_retire(CH08_VILLAGER_2_SLOT);
    ch08_retire(CH08_VILLAGER_3_SLOT);
    ch08_retire(CH08_VILLAGER_4_SLOT);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 1);
}

/* Rule 1 on its own: a retired slot 0 is a defeat, and it is this handler's
   instruction rather than the shared test's -- nothing else in the staging
   could have written the 1, since the captives are standing and the enemy
   side is never looked at. */
static void ch08_a_retired_randis_is_a_defeat(void)
{
    stage_ch08(1, 0, 0);
    ch08_retire(CH08_RANDIS_SLOT);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 1);
}

/* The turn gate.  CMP dword ptr [0x00069ce8],0x3 / JLE 0003a74b skips the
   PUSH 0x13 call entirely, so a retired slot 0x13 is not a defeat while the
   counter is 3 or below -- which is the whole point of the gate: 費塔加 is
   not deployed onto the map until the end of the player's third turn, and an
   ungated test would lose the chapter on turns 1 to 3. */
static void ch08_the_guest_mage_test_is_gated_on_the_turn(void)
{
    int turn;

    for (turn = 1; turn <= CH08_GUEST_MAGE_ARMED_AFTER_TURN; turn++) {
        stage_ch08(turn, 0, 0);
        ch08_retire(CH08_GUEST_MAGE_SLOT);
        fdps_chapter_08_post_action();
        CHECK_EQ(end_code(), 0);
    }
}

/* The other side of the same JLE: one turn past the gate the call is reached
   and the defeat is recorded.  Turn 4 is the first turn on which it can be,
   so this pins the boundary and not just the direction. */
static void ch08_a_retired_guest_mage_past_the_gate_is_a_defeat(void)
{
    stage_ch08(CH08_GUEST_MAGE_ARMED_AFTER_TURN + 1, 0, 0);
    ch08_retire(CH08_GUEST_MAGE_SLOT);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 1);
}

/* Past the gate with 費塔加 standing, the store behind the second TEST is not
   reached and the code is left where it was. */
static void ch08_a_live_guest_mage_past_the_gate_records_nothing(void)
{
    stage_ch08(CH08_GUEST_MAGE_ARMED_AFTER_TURN + 1, 0, 0);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 0);
}

/* The three rules are sequential and every store is unguarded, so the last
   one to fire is the verdict: the same action that loses 蘭迪斯 and empties
   the cells with survivors answers 2, because the captive arm runs after the
   slot-0 store and overwrites it.  Written as an else-if chain, or with the
   captive arm gated on the code still being 0, this would answer 1. */
static void ch08_the_captive_ending_overwrites_an_earlier_defeat(void)
{
    stage_ch08(1, 2, 0);
    ch08_retire(CH08_RANDIS_SLOT);
    ch08_retire(CH08_VILLAGER_1_SLOT);
    ch08_retire(CH08_VILLAGER_2_SLOT);
    ch08_retire(CH08_VILLAGER_3_SLOT);
    ch08_retire(CH08_VILLAGER_4_SLOT);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 2);
}

/* There is no CALL 0x0003a2e0 in the body: an emptied enemy side is not a
   clear on this chapter.  The staging is the one the shared test answers 2
   to and keeps: it writes 2 up front at 0003a2f9, and the only record its
   walk puts the code back to 0 for is one whose side byte is 0 with the
   retirement bit clear (CMP byte ptr [EAX + 0x6],0x0 / AND AL,0x1 at
   0003a331..0003a34a), so every slot here is either on the player side or a
   retired enemy; its closing test is the slot 0 one at 0003a382, and slot 0
   is standing.  Meanwhile all three of this handler's own rules stay silent
   -- slot 0 standing, turn 1 so the 費塔加 gate is shut, and all four
   captives standing so the chain fails on its first term -- so the answer
   must be the 0 the handler was given.  Adding the shared call would make it
   2 and fail here. */
static void ch08_the_shared_end_test_is_not_run(void)
{
    int slot;

    stage_ch08(1, 0, 0);
    for (slot = 0; slot < CH08_STAGE_UNITS; slot++) {
        ch08_units[slot].side = (unsigned char) SIDE_ENEMY;
        ch08_units[slot].flags = FLAG_RETIRED;
    }
    ch08_stand_on_player_side(CH08_RANDIS_SLOT);
    ch08_stand_on_player_side(CH08_VILLAGER_1_SLOT);
    ch08_stand_on_player_side(CH08_VILLAGER_2_SLOT);
    ch08_stand_on_player_side(CH08_VILLAGER_3_SLOT);
    ch08_stand_on_player_side(CH08_VILLAGER_4_SLOT);
    ch08_stand_on_player_side(CH08_GUEST_MAGE_SLOT);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 0);
}

/* No store is unconditional, so a verdict a chapter event already recorded
   survives a call in which none of the three rules fires -- in either
   direction. */
static void ch08_a_recorded_verdict_is_left_alone(void)
{
    stage_ch08(1, 0, 1);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 1);

    stage_ch08(1, 0, 2);
    fdps_chapter_08_post_action();
    CHECK_EQ(end_code(), 2);
}

/* Only slots 0, 0x0f..0x12 and 0x13 are named in the body, so retiring any
   other one on its own leaves the battle open.  The sweep runs at turn 1, at
   which the 0x13 test is behind its gate as well, and the captive chain needs
   all four so no single captive can end it either. */
static void ch08_no_unnamed_slot_ends_the_battle(void)
{
    int retired_slot;

    for (retired_slot = 1; retired_slot < CH08_STAGE_UNITS; retired_slot++) {
        stage_ch08(1, 0, 0);
        ch08_retire(retired_slot);
        fdps_chapter_08_post_action();
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
    RUN_TEST(ch07_the_shared_end_test_runs);
    RUN_TEST(ch07_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(ch07_a_retired_randis_is_a_defeat);
    RUN_TEST(ch07_no_other_slot_ends_the_battle);
    RUN_TEST(ch07_a_recorded_verdict_is_left_alone);
    RUN_TEST(ch07_an_open_battle_stays_open);
    RUN_TEST(ch09_a_retired_brando_outranks_a_cleared_field);
    RUN_TEST(ch09_a_retired_gaia_outranks_a_cleared_field);
    RUN_TEST(ch09_either_retired_guest_is_a_defeat);
    RUN_TEST(ch09_two_live_guests_leave_the_shared_verdict_alone);
    RUN_TEST(ch09_the_shared_slot_zero_test_still_runs);
    RUN_TEST(ch09_the_guest_tests_survive_a_recorded_verdict);
    RUN_TEST(ch09_no_slot_but_zero_six_and_seven_ends_the_battle);
    RUN_TEST(ch10_all_eight_on_the_bottom_row_is_a_clear);
    RUN_TEST(ch10_one_slot_short_leaves_the_battle_open);
    RUN_TEST(ch10_the_bottom_row_test_is_an_equality);
    RUN_TEST(ch10_a_retired_unit_counts_as_escaped);
    RUN_TEST(ch10_a_retired_randis_is_a_defeat);
    RUN_TEST(ch10_only_slot_zero_can_lose_the_chapter);
    RUN_TEST(ch10_the_shared_end_test_is_not_run);
    RUN_TEST(ch10_a_verdict_the_handler_does_not_settle_is_left_alone);
    RUN_TEST(ch11_a_retired_chinchin_outranks_a_cleared_field);
    RUN_TEST(ch11_a_retired_chinchin_is_a_defeat);
    RUN_TEST(ch11_a_live_chinchin_leaves_the_shared_verdict_alone);
    RUN_TEST(ch11_the_shared_slot_zero_test_still_runs);
    RUN_TEST(ch11_the_chinchin_test_survives_a_recorded_verdict);
    RUN_TEST(ch11_no_slot_but_zero_and_eight_ends_the_battle);
    RUN_TEST(ch12_the_shared_end_test_runs);
    RUN_TEST(ch12_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(ch12_a_retired_randis_is_a_defeat);
    RUN_TEST(ch12_no_other_slot_ends_the_battle);
    RUN_TEST(ch12_a_recorded_verdict_is_left_alone);
    RUN_TEST(ch12_an_open_battle_stays_open);
    RUN_TEST(ch13_the_shared_end_test_runs);
    RUN_TEST(ch13_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(ch13_a_retired_randis_is_a_defeat);
    RUN_TEST(ch13_no_other_slot_ends_the_battle);
    RUN_TEST(ch13_a_recorded_verdict_is_left_alone);
    RUN_TEST(ch13_an_open_battle_stays_open);
    RUN_TEST(ch14_the_shared_end_test_runs);
    RUN_TEST(ch14_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(ch14_a_retired_randis_is_a_defeat);
    RUN_TEST(ch14_no_other_slot_ends_the_battle);
    RUN_TEST(ch14_a_recorded_verdict_is_left_alone);
    RUN_TEST(ch14_an_open_battle_stays_open);
    RUN_TEST(ch01_the_shared_end_test_runs);
    RUN_TEST(ch01_a_live_enemy_keeps_the_battle_going);
    RUN_TEST(ch01_a_retired_randis_is_a_defeat);
    RUN_TEST(ch01_a_retired_sol_outranks_a_cleared_field);
    RUN_TEST(ch01_a_retired_sol_is_a_defeat);
    RUN_TEST(ch01_a_live_sol_leaves_the_shared_verdict_alone);
    RUN_TEST(ch01_the_sol_test_survives_a_recorded_verdict);
    RUN_TEST(ch01_no_slot_but_zero_and_two_ends_the_battle);
    RUN_TEST(ch03_a_retired_randis_is_a_defeat);
    RUN_TEST(ch03_a_retired_randis_outranks_a_retired_mage);
    RUN_TEST(ch03_a_retired_mage_is_a_clear);
    RUN_TEST(ch03_neither_test_firing_leaves_the_code_alone);
    RUN_TEST(ch03_a_recorded_verdict_is_overwritten);
    RUN_TEST(ch03_the_shared_end_test_is_not_run);
    RUN_TEST(ch03_no_slot_but_zero_and_four_ends_the_battle);
    RUN_TEST(ch08_one_captive_still_in_leaves_the_battle_open);
    RUN_TEST(ch08_one_survivor_is_enough_to_clear);
    RUN_TEST(ch08_no_survivor_is_a_defeat);
    RUN_TEST(ch08_a_high_bit_tally_still_clears);
    RUN_TEST(ch08_the_tally_is_element_seventeen);
    RUN_TEST(ch08_a_retired_randis_is_a_defeat);
    RUN_TEST(ch08_the_guest_mage_test_is_gated_on_the_turn);
    RUN_TEST(ch08_a_retired_guest_mage_past_the_gate_is_a_defeat);
    RUN_TEST(ch08_a_live_guest_mage_past_the_gate_records_nothing);
    RUN_TEST(ch08_the_captive_ending_overwrites_an_earlier_defeat);
    RUN_TEST(ch08_the_shared_end_test_is_not_run);
    RUN_TEST(ch08_a_recorded_verdict_is_left_alone);
    RUN_TEST(ch08_no_unnamed_slot_ends_the_battle);
}
