/* tests/aitarget.c -- cover for src/aitarget.c.
 *
 * Expected values come from the assembly at 000109f0 and from the record
 * offsets ticket 17 measured (pos_x +0, pos_y +1, flags +5, side +6, stride
 * 0x50); none of them is read off the emitted C.
 *
 * The map unit array is staged here rather than read from a game file: the
 * function takes its input entirely from data_fdps_map_unit_array_ptr and
 * data_fdps_map_unit_count, so pointing those at a local array is the only way
 * to reach the filter at all.  Nothing below asserts what either global holds
 * on its own -- ticket 23 owns that.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "aitarget.h"

#define STAGE_UNITS 8

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero the whole staging array and hand it to the function under test as the
   map unit array of `n` records. */
static void stage(int n)
{
    unsigned char *raw;
    int i;

    raw = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        raw[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = n;
}

static void place(int i, int x, int y, int side, int flags)
{
    stage_units[i].pos_x = (unsigned char) x;
    stage_units[i].pos_y = (unsigned char) y;
    stage_units[i].side = (unsigned char) side;
    stage_units[i].flags = (unsigned char) flags;
}

/* IMUL EBX,[EBP-0x1c],0x50 at 00010a22: the walk steps one record every 0x50
   bytes, which is the size the record layout has to come out as for the index
   arithmetic in the C to land on the same bytes. */
static void area_record_stride_is_0x50(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
}

/* CMP EAX,[0x00060150] / JL at 00010a0d: an empty battle never enters the
   loop, whatever the array behind the pointer still holds. */
static void area_empty_battle_counts_nothing(void)
{
    stage(0);
    place(0, 4, 4, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 0);
}

/* The bound is the global, not the size of the array: a unit sitting past
   data_fdps_map_unit_count is never looked at. */
static void area_bound_is_the_unit_count(void)
{
    stage(2);
    place(0, 4, 4, 0, 0);
    place(1, 4, 4, 0, 0);
    place(2, 4, 4, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 2);
}

/* CMP EAX,[EBP+0x1c] / JL at 00010a7e is strictly less-than, so max_dist 1
   selects the centre tile alone and a neighbour needs max_dist 2. */
static void area_distance_limit_is_exclusive(void)
{
    stage(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 1);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 2, NULL, 0), 2);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 0, NULL, 0), 0);
}

/* Two CALLs to the CRT abs at 00010a4c and 00010a5d, their results added at
   00010a65: the metric is Manhattan and each axis is taken absolutely, so a
   unit up and to the left of the centre is as near as one down and right. */
static void area_distance_is_manhattan_and_absolute(void)
{
    stage(4);
    place(0, 2, 4, 0, 0);   /* dx -2, dy  0 -> 2 */
    place(1, 6, 4, 0, 0);   /* dx +2, dy  0 -> 2 */
    place(2, 3, 3, 0, 0);   /* dx -1, dy -1 -> 2 */
    place(3, 4, 7, 0, 0);   /* dx  0, dy +3 -> 3 */
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 3, NULL, 0), 3);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 4, NULL, 0), 4);
}

/* The coordinates come from bytes +0 and +1 of the record, not from any other
   field: moving only pos_x moves the unit out of a one-tile area. */
static void area_reads_position_bytes(void)
{
    stage(1);
    place(0, 4, 4, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 1);
    stage_units[0].pos_x = 5;
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 0);
    stage_units[0].pos_x = 4;
    stage_units[0].pos_y = 5;
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 0);
}

/* AND AL,0x1 / JNZ at 00010a70: bit 0 of the status byte drops the record
   before any mode is considered, and it is bit 0 alone -- 0x02 in the same
   byte does not drop it. */
static void area_retired_bit_drops_the_unit(void)
{
    stage(1);
    place(0, 4, 4, 0, 0x01);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 0);
    place(0, 4, 4, 2, 0x81);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 3), 0);
    place(0, 4, 4, 0, 0x02);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 1);
}

/* CMP [EBP+0x24],0 / CMP byte ptr [EAX+0x6],0 / JZ accept at 00010a88: mode 0
   keeps side 0 and nothing else. */
static void area_mode0_keeps_side0(void)
{
    stage(3);
    place(0, 4, 4, 0, 0);
    place(1, 4, 4, 1, 0);
    place(2, 4, 4, 2, 0);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 1);
}

/* CMP [EBP+0x24],1 / CMP byte ptr [EAX+0x6],0 / JNZ accept at 00010a97: mode 1
   keeps every non-zero side, and the acted bit plays no part. */
static void area_mode1_keeps_nonzero_sides(void)
{
    stage(3);
    place(0, 4, 4, 0, 0);
    place(1, 4, 4, 1, 0);
    place(2, 4, 4, 2, 0x80);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 1), 2);
}

/* CMP EAX,0x2 at 00010abb followed by AND AL,0x80 at 00010ac8: mode 2 wants
   side 2 AND the acted bit, so side 2 without it and side 1 with it are both
   refused. */
static void area_mode2_wants_side2_that_acted(void)
{
    stage(3);
    place(0, 4, 4, 2, 0x80);
    place(1, 4, 4, 2, 0x00);
    place(2, 4, 4, 1, 0x80);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 2), 1);
}

/* CMP [EBP+0x24],3 / CMP EAX,0x2 / JZ accept at 00010ad7: mode 3 keeps side 2
   with no state test at all, which is what separates it from mode 2. */
static void area_mode3_keeps_side2_either_way(void)
{
    stage(3);
    place(0, 4, 4, 2, 0x80);
    place(1, 4, 4, 2, 0x00);
    place(2, 4, 4, 1, 0x00);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 3), 2);
}

/* The chain ends with JMP 0x00010b06 at 00010aed: a mode outside 0..3 accepts
   nothing, so the ITEM.DAT select_mode values 4 and 5 count zero targets even
   with units standing on the tile. */
static void area_unknown_mode_matches_nothing(void)
{
    stage(3);
    place(0, 4, 4, 0, 0);
    place(1, 4, 4, 1, 0);
    place(2, 4, 4, 2, 0x80);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 4), 0);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 5), 0);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, -1), 0);
}

/* MOV AL,byte ptr [EBP-0x1c] / MOV byte ptr [EDX],AL at 00010afb: the loop
   index, not a serial number of the match, is stored, one byte per match, at
   out_indices[count]. */
static void area_appends_unit_indices(void)
{
    unsigned char out[4];

    stage(4);
    place(0, 9, 9, 0, 0);
    place(1, 4, 4, 0, 0);
    place(2, 9, 9, 0, 0);
    place(3, 4, 4, 0, 0);
    out[0] = 0xee;
    out[1] = 0xee;
    out[2] = 0xee;
    out[3] = 0xee;
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, out, 0), 2);
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[1], 3);
    CHECK_EQ(out[2], 0xee);  /* nothing written past the matches */
    CHECK_EQ(out[3], 0xee);
}

/* CMP [EBP+0x20],0 / JZ 0x00010b00 skips the store only: the increment at
   00010b03 is outside the guard, so the count-only call -- the only call there
   is -- still counts. */
static void area_null_out_still_counts(void)
{
    stage(2);
    place(0, 4, 4, 0, 0);
    place(1, 4, 4, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 1, NULL, 0), 2);
}

/* The array is only ever written through out_indices, and the record fields
   are only ever read: a collect leaves every staged record as it was. */
static void area_does_not_touch_the_records(void)
{
    stage(2);
    place(0, 4, 4, 2, 0x80);
    place(1, 5, 4, 2, 0x00);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 2, NULL, 3), 2);
    CHECK_EQ(stage_units[0].pos_x, 4);
    CHECK_EQ(stage_units[0].flags, 0x80);
    CHECK_EQ(stage_units[0].side, 2);
    CHECK_EQ(stage_units[1].pos_x, 5);
    CHECK_EQ(stage_units[1].flags, 0x00);
    CHECK_EQ(stage_units[1].side, 2);
}

/* The whole filter at once, in the shape fdps_map_cursor_select_loop uses it:
   a spell with an area of effect over a mixed field, counted with NULL. */
static void area_combined_filter(void)
{
    stage(6);
    place(0, 4, 4, 0, 0x00);   /* enemy on the centre tile              */
    place(1, 5, 4, 0, 0x01);   /* enemy, retired                        */
    place(2, 3, 4, 0, 0x00);   /* enemy, one tile away                  */
    place(3, 4, 5, 2, 0x00);   /* party member, one tile away           */
    place(4, 4, 2, 0, 0x00);   /* enemy, two tiles away                 */
    place(5, 4, 4, 1, 0x00);   /* guest NPC on the centre tile          */
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 2, NULL, 0), 2);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 2, NULL, 1), 2);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 2, NULL, 3), 1);
    CHECK_EQ(fdps_collect_targets_in_area(4, 4, 3, NULL, 0), 3);
}

void run_aitarget_tests(void)
{
    RUN_TEST(area_record_stride_is_0x50);
    RUN_TEST(area_empty_battle_counts_nothing);
    RUN_TEST(area_bound_is_the_unit_count);
    RUN_TEST(area_distance_limit_is_exclusive);
    RUN_TEST(area_distance_is_manhattan_and_absolute);
    RUN_TEST(area_reads_position_bytes);
    RUN_TEST(area_retired_bit_drops_the_unit);
    RUN_TEST(area_mode0_keeps_side0);
    RUN_TEST(area_mode1_keeps_nonzero_sides);
    RUN_TEST(area_mode2_wants_side2_that_acted);
    RUN_TEST(area_mode3_keeps_side2_either_way);
    RUN_TEST(area_unknown_mode_matches_nothing);
    RUN_TEST(area_appends_unit_indices);
    RUN_TEST(area_null_out_still_counts);
    RUN_TEST(area_does_not_touch_the_records);
    RUN_TEST(area_combined_filter);
}
