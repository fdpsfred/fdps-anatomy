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

void run_chpost1_tests(void)
{
    RUN_TEST(the_shared_end_test_runs);
    RUN_TEST(a_live_enemy_keeps_the_battle_going);
    RUN_TEST(a_retired_randis_is_a_defeat);
    RUN_TEST(no_other_slot_ends_the_battle);
    RUN_TEST(a_recorded_verdict_is_left_alone);
    RUN_TEST(an_open_battle_stays_open);
}
