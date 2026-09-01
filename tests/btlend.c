/* tests/btlend.c -- cover for src/btlend.c.
 *
 * Expected values come from the assembly at 00018350: the CMP EAX,[0x00060150]
 * / JL at 0001836d that bounds the walk with a signed compare before the body
 * runs, the MOV AL,byte ptr [EAX+0x6] / AND EAX,0xff at 00018391 that widens
 * the side byte without sign, the CMP EAX,[EBP+0x14] / JNZ at 00018399 that
 * matches it as an equality against the argument, the TEST EAX,EAX / JZ at
 * 000183aa that counts the unit only when fdps_unit_is_retired answered zero,
 * and the INC dword ptr [EBP-0xc] at 000183b3 that bumps the tally by one per
 * surviving unit.  The retirement flag is bit 0 of the record's flags byte at
 * offset 5, from src/unit.c's fdps_unit_is_retired at 000109b0.  None of them
 * is read off the emitted C.
 *
 * The three side codes exercised are the ones the only caller pushes --
 * PUSH 0x0 at 00017e14, PUSH 0x2 at 00017e3a and PUSH 0x1 at 00017e60, all
 * inside fdps_battle_show_win_fail_window.
 *
 * The unit array is staged here rather than read from a game file: the
 * function takes its whole input from its argument, from the unit count global
 * and from the records the accessor resolves, so pointing the array global at
 * a local block is the only way to reach the walk.  Nothing below asserts what
 * either global holds on its own -- ticket 23 owns that.
 *
 * The standard end test's expected values come from the assembly at 0003a2e0:
 * the CMP dword ptr [0x00069da0],0x0 / JNZ at 0003a2ec that abandons the whole
 * body for a code that is already non-zero, the MOV dword ptr
 * [0x00069da0],0x2 at 0003a2f9 that writes the cleared verdict before anything
 * is examined, the CMP EAX,[0x00060150] / JL at 0003a30d that bounds the walk
 * signed, the CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 and the MOV AL,byte
 * ptr [EAX+0x5] / AND AL,0x1 at 0003a33a that are the walk's two tests, the
 * CMP [0x00069cf4],0x10 and CMP [0x00069cf4],0x15 at 0003a356 and 0003a35f
 * that pick between PUSH 0x3 and PUSH 0x0, and the two unguarded MOV dword ptr
 * [0x00069da0],0x1 stores at 0003a376 and 0003a390 that are reached without
 * any test of what the walk concluded.  That the chapter ids are 0-based is
 * from the dispatch table at 0x00060304, whose entry 0x10 is
 * fdps_chapter_17_end at 0003ad40.  The 0/1/2 meanings of the code are
 * program_info/architecture.md.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "btlend.h"

/* Eight slots, so a unit can be parked one past the count the walk is given
   and a stray step past the bound would be visible. */
#define STAGE_UNITS 8

/* The side codes the result window asks about, from the caller's pushes. */
#define SIDE_ENEMY  0
#define SIDE_THIRD  1
#define SIDE_PLAYER 2

/* Bit 0 of the flags byte is the retirement flag; bit 7 is the separate
   acted-this-turn flag that fdps_unit_is_retired does not answer for. */
#define FLAG_RETIRED 0x01
#define FLAG_ACTED   0x80

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero every slot and publish the block.  Every unit is then a non-retired
   member of side 0, which is the state each case edits only the fields it is
   about. */
static void stage(int live_unit_count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = live_unit_count;
}

static void stage_unit(int unit_index, int side, int flags)
{
    stage_units[unit_index].side = (unsigned char) side;
    stage_units[unit_index].flags = (unsigned char) flags;
}

/* The record offsets the walk addresses as literals: +0x6 for the side byte it
   reads itself and +0x5 for the flags byte the retirement test reads, plus the
   0x50 stride the accessor multiplies by.  If the record measured anything
   else every lookup past index 0 would read a different unit. */
static void record_layout_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
}

/* CMP EAX,[0x00060150] / JL guards the body, so the count is a bound tested
   first and not a do-while count.  A count of 0 counts nothing, and because
   the compare is the signed JL a negative count does too rather than running
   away as an unsigned one would. */
static void an_empty_battle_counts_nothing(void)
{
    stage(0);
    stage_unit(0, SIDE_PLAYER, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_ENEMY), 0);
    stage(-1);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 0);
}

/* The bound is exclusive: the unit sitting at index == count is outside the
   walk even though the array holds it. */
static void the_bound_is_exclusive(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 3);
}

/* CMP / JNZ over the widened side byte is an equality, so each of the three
   codes the window asks about picks out its own units and nothing else.  The
   totals also pin the INC: one per matching unit, not a flag or a bitwise
   accumulation. */
static void each_side_code_counts_its_own_units(void)
{
    stage(6);
    stage_unit(0, SIDE_ENEMY, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_THIRD, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    stage_unit(5, SIDE_ENEMY, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_ENEMY), 3);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 2);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_THIRD), 1);
}

/* A code no unit carries is not an error and is not a wildcard: it counts
   nothing.  Checked at both ends of the byte range so that a spelling which
   treated the argument as a truth value or as a range would show. */
static void an_unused_side_code_counts_nothing(void)
{
    stage(3);
    stage_unit(0, SIDE_ENEMY, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_THIRD, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(3), 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(0xff), 0);
}

/* TEST EAX,EAX / JZ after the retirement call: a unit whose flags bit 0 is set
   is passed over even though its side matches. */
static void retired_units_are_not_counted(void)
{
    stage(4);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(2, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(3, SIDE_PLAYER, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 2);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(3, SIDE_PLAYER, FLAG_RETIRED);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 0);
}

/* Only bit 0 of the flags byte retires a unit.  Bit 7 is the acted-this-turn
   flag and a unit that has already moved is still standing, so a test written
   against the whole byte rather than against bit 0 would lose it. */
static void the_acted_flag_does_not_retire_a_unit(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, FLAG_ACTED);
    stage_unit(1, SIDE_PLAYER, 0xfe);
    stage_unit(2, SIDE_PLAYER, FLAG_ACTED | FLAG_RETIRED);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 2);
}

/* AND EAX,0xff widens the side byte without sign, so a code with the top bit
   set matches the positive number a caller would push and never a negative
   one.  Reading the byte through a signed char inverts both of these. */
static void the_side_byte_is_zero_extended(void)
{
    stage(2);
    stage_unit(0, 0x80, 0);
    stage_unit(1, 0xff, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(0x80), 1);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(-128), 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(0xff), 1);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(-1), 0);
}

/* The walk resolves each record through the accessor with the loop index, so
   the units it reads are the ones at 0..count-1 of the published block and the
   0x50 stride lands on each in turn.  Moving the block moves the answer,
   which is what re-resolving on every iteration buys. */
static void the_walk_follows_the_published_array(void)
{
    stage(4);
    stage_unit(0, SIDE_ENEMY, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_ENEMY, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_ENEMY), 3);
    data_fdps_map_unit_array_ptr = (unsigned char *) &stage_units[2];
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_ENEMY), 1);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 1);
}

/* The other two globals the standard end test works from: the chapter id it
   branches on and the battle-end code it both reads as a gate and writes as
   its answer.  Staged for the same reason the array is -- the function takes
   no arguments at all. */
static void stage_chapter(int chapter_id, int battle_end_code)
{
    data_fdps_chapter_current_chapter_id = chapter_id;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static int end_code(void)
{
    return (int) data_fdps_chapter_event_or_battle_end_code;
}

/* CMP dword ptr [0x00069da0],0x0 / JNZ jumps to the epilogue, so a code some
   chapter event already recorded is not recomputed.  Each value below would be
   overwritten by a body that ran: the array holds a live enemy, which would
   settle the code at 0, and the slot 0 unit has not retired. */
static void a_recorded_verdict_is_never_recomputed(void)
{
    stage(2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_chapter(0, 1);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0, 2);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
    stage_chapter(0, 7);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 7);
}

/* The 2 written up front survives when the walk finds no live enemy, whether
   the enemies are all retired or the battle holds none at all. */
static void every_enemy_gone_clears_the_chapter(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);

    stage(2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* One unit on side 0 whose flags bit 0 is clear puts the code back to 0, and
   nothing restores the 2 afterwards -- so its position in the array does not
   matter and retired enemies following it do not undo it. */
static void one_live_enemy_keeps_the_battle_going(void)
{
    stage(4);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(3, SIDE_ENEMY, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);

    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);
}

/* AND AL,0x1 masks bit 0 alone, so an enemy that has merely acted this turn --
   bit 7 -- is still standing, and so is one carrying every bit but bit 0. */
static void only_bit_zero_retires_an_enemy(void)
{
    stage(2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_ACTED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);

    stage_unit(1, SIDE_ENEMY, 0xfe);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);

    stage_unit(1, SIDE_ENEMY, FLAG_ACTED | FLAG_RETIRED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* CMP byte ptr [EAX+0x6],0x0 is an equality against zero, so only side 0 is an
   enemy: neither the player side nor the third side nor an unused code holds
   the battle open. */
static void only_side_zero_holds_the_battle_open(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_THIRD, 0);
    stage_unit(2, 3, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);

    stage_unit(2, SIDE_ENEMY, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);
}

/* The bound is the live unit count, tested before the body: a live enemy at
   index == count is invisible, and a count of zero or below examines no record
   at all rather than running away as an unsigned bound would. */
static void the_walk_stops_at_the_live_unit_count(void)
{
    stage(1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);

    data_fdps_map_unit_count = 2;
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);

    stage(0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);

    stage(-1);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* The defeat store carries no guard, and it runs after the walk: a defeat
   overwrites the 2 the walk left standing as readily as it overwrites the 0.
   This is the case an if/else spelling gets backwards. */
static void a_retired_watched_unit_outranks_the_walk(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);

    stage_unit(2, SIDE_ENEMY, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
}

/* Chapter ids 0x10 and 0x15 -- chapters 17 and 22 -- watch slot 3 and every
   other id watches slot 0.  The test is two equalities and not a range, so the
   ids on either side of each take the other arm. */
static void chapters_17_and_22_watch_slot_three(void)
{
    stage(4);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(3, SIDE_PLAYER, 0);
    stage_chapter(0x10, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
    stage_chapter(0x15, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
    stage_chapter(0x0f, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0x11, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0x14, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0x16, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);

    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, FLAG_RETIRED);
    stage_chapter(0x10, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0x15, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* The defeat test pushes a literal slot index and nothing compares it with the
   live unit count, so slot 3 is read in chapter 17 even in a battle that
   deployed one unit. */
static void the_watched_slot_ignores_the_unit_count(void)
{
    stage(1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, FLAG_RETIRED);
    stage_chapter(0x10, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);

    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

void run_btlend_tests(void)
{
    RUN_TEST(record_layout_matches_the_offsets);
    RUN_TEST(an_empty_battle_counts_nothing);
    RUN_TEST(the_bound_is_exclusive);
    RUN_TEST(each_side_code_counts_its_own_units);
    RUN_TEST(an_unused_side_code_counts_nothing);
    RUN_TEST(retired_units_are_not_counted);
    RUN_TEST(the_acted_flag_does_not_retire_a_unit);
    RUN_TEST(the_side_byte_is_zero_extended);
    RUN_TEST(the_walk_follows_the_published_array);
    RUN_TEST(a_recorded_verdict_is_never_recomputed);
    RUN_TEST(every_enemy_gone_clears_the_chapter);
    RUN_TEST(one_live_enemy_keeps_the_battle_going);
    RUN_TEST(only_bit_zero_retires_an_enemy);
    RUN_TEST(only_side_zero_holds_the_battle_open);
    RUN_TEST(the_walk_stops_at_the_live_unit_count);
    RUN_TEST(a_retired_watched_unit_outranks_the_walk);
    RUN_TEST(chapters_17_and_22_watch_slot_three);
    RUN_TEST(the_watched_slot_ignores_the_unit_count);
}
