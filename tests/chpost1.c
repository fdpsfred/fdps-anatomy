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
}
