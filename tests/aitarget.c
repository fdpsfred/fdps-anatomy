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

/* ------------------------------------------------------------------
 * 00013670 fdps_collect_targets_in_line
 *
 * This one owns no scan of its own: it drives the two map cursor globals one
 * tile at a time and asks fdps_battle_find_unit_at_cursor (src/unit.c, real
 * emitted code) what is standing there, so the fixture is the unit array the
 * finder walks plus the movement grid's four header bytes, which is all this
 * function reads of the grid.
 *
 * Expected values come from the assembly at 00013670 -- the step selection at
 * 000136b5-000136ef, IMUL EAX,EAX,0x18 over both header words at 000136a0 and
 * 000136af, the four signed bounds tests at 00013747-00013767, the two side
 * tests at 00013788 and 00013797, the one-byte append at 000137a8 and the
 * cursor restore at 000137be -- and from the plate comment's reading of the
 * four call sites.  None of them is read off the emitted C.
 * ------------------------------------------------------------------ */

#define LINE_CURSOR_MARK_X 0x1234
#define LINE_CURSOR_MARK_Y 0x5678
#define LINE_OUT_FILL 0xee
#define LINE_OUT_CELLS 16

static unsigned char line_grid[4];
static unsigned char line_out[LINE_OUT_CELLS];

/* Hand the function a map of width x height TILES, an empty out buffer filled
   with a byte no unit index can be, and cursor globals parked on two values no
   walk can produce so that the restore at 000137be is visible. */
static void stage_line(int width, int height, int units)
{
    int i;

    *(short *) line_grid = (short) width;
    *(short *) (line_grid + 2) = (short) height;
    data_fdps_battle_move_grid_ptr = line_grid;
    data_fdps_map_cursor_world_x = LINE_CURSOR_MARK_X;
    data_fdps_map_cursor_world_y = LINE_CURSOR_MARK_Y;
    for (i = 0; i < LINE_OUT_CELLS; i++) {
        line_out[i] = LINE_OUT_FILL;
    }
    stage(units);
}

/* CMP [EBP+0x20],[EBP+0x14] / JNZ at 000136bb takes the horizontal branch on
   any x difference and the y difference is then never read, so a diagonal aim
   still sweeps the origin's own row. */
static void line_walk_is_horizontal_unless_x_matches(void)
{
    stage_line(8, 8, 2);
    place(0, 3, 2, 0, 0);   /* on the origin's row      */
    place(1, 3, 3, 0, 0);   /* on the diagonal          */
    CHECK_EQ(fdps_collect_targets_in_line(5, 5, line_out, 2, 2, 3, 1), 1);
    CHECK_EQ(line_out[0], 0);
}

/* The vertical step is taken only on the equal-x branch at 000136bd. */
static void line_walk_is_vertical_when_x_matches(void)
{
    stage_line(8, 8, 2);
    place(0, 2, 3, 0, 0);   /* on the origin's column   */
    place(1, 3, 3, 0, 0);   /* off it                   */
    CHECK_EQ(fdps_collect_targets_in_line(2, 5, line_out, 2, 2, 3, 1), 1);
    CHECK_EQ(line_out[0], 0);
}

/* JLE at 000136c3 and at 000136dd pick the sign: the step is negative only
   when the aim is strictly below the origin on the axis being walked. */
static void line_step_sign_follows_the_aim(void)
{
    stage_line(8, 8, 1);
    place(0, 4, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(0, 2, line_out, 5, 2, 3, 1), 1);

    stage_line(8, 8, 1);
    place(0, 2, 4, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(2, 0, line_out, 2, 5, 3, 1), 1);

    /* The other direction on each axis, so neither case passes by accident. */
    stage_line(8, 8, 1);
    place(0, 4, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 5, 2, 3, 1), 0);
}

/* Aiming at the origin tile takes the equal-x branch and then JLE at 000136c3
   is taken, so the walk goes DOWN the column rather than nowhere. */
static void line_aim_at_the_origin_walks_down(void)
{
    stage_line(8, 8, 2);
    place(0, 2, 3, 0, 0);
    place(1, 2, 1, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(2, 2, line_out, 2, 2, 3, 1), 1);
    CHECK_EQ(line_out[0], 0);
}

/* The step is added at 0001372d before the bounds test and the finder call, so
   the origin tile is never examined however the line is aimed. */
static void line_origin_tile_is_never_examined(void)
{
    stage_line(8, 8, 1);
    place(0, 2, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(5, 2, line_out, 2, 2, 3, 1), 0);
    CHECK_EQ(fdps_collect_targets_in_line(2, 5, line_out, 2, 2, 3, 1), 0);
}

/* CMP [EBP-0x24],[EBP+0x28] / JL at 0001371b: line_length is a count of tiles
   starting one step beyond the origin, so 0 examines nothing. */
static void line_length_counts_tiles_beyond_the_origin(void)
{
    stage_line(8, 8, 3);
    place(0, 3, 2, 0, 0);
    place(1, 4, 2, 0, 0);
    place(2, 5, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 0, 1), 0);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 1, 1), 1);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 1), 3);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 9, 1), 3);
}

/* The bounds are the grid header's two tile words times 0x18, x against the
   word at +0 and y against the word at +2, so a map narrower or shorter than
   the walk cuts it off. */
static void line_bounds_come_from_the_grid_header(void)
{
    stage_line(4, 8, 2);
    place(0, 3, 2, 0, 0);   /* last column of a 4-wide map */
    place(1, 5, 2, 0, 0);   /* past the right edge         */
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 5, 1), 1);
    CHECK_EQ(line_out[0], 0);

    stage_line(8, 3, 2);
    place(0, 2, 2, 0, 0);   /* last row of a 3-tall map    */
    place(1, 2, 5, 0, 0);   /* past the bottom edge        */
    CHECK_EQ(fdps_collect_targets_in_line(2, 7, line_out, 2, 1, 5, 1), 1);
    CHECK_EQ(line_out[0], 0);
}

/* CMP [0x00069cd4],0x0 / JGE at 00013750 and the same on y at 00013767: a
   negative cursor is skipped rather than divided into tile -1, and a skipped
   tile does not end the walk. */
static void line_walk_off_the_left_edge_is_skipped(void)
{
    stage_line(8, 8, 1);
    place(0, 0, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(0, 2, line_out, 1, 2, 3, 1), 1);
    CHECK_EQ(line_out[0], 0);
    CHECK_EQ(line_out[1], LINE_OUT_FILL);
}

/* The two side tests at 00013788 and 00013797 run the OPPOSITE way round from
   the select_mode of the two collectors above: zero keeps the non-zero sides
   and non-zero keeps side 0.  The reachable values are 0, 1 and 5. */
static void line_side_filter_is_inverted(void)
{
    stage_line(8, 8, 3);
    place(0, 3, 2, 0, 0);   /* enemy side 0     */
    place(1, 4, 2, 1, 0);   /* guest NPC side 1 */
    place(2, 5, 2, 2, 0);   /* party side 2     */

    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 0), 2);
    CHECK_EQ(line_out[0], 1);
    CHECK_EQ(line_out[1], 2);

    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 1), 1);
    CHECK_EQ(line_out[0], 0);

    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 5), 1);
    CHECK_EQ(line_out[0], 0);
}

/* MOV AL,byte ptr [EBP-0x18] / MOV byte ptr [EDX],AL at 000137ae writes ONE
   byte per match at the running count, so the buffer past the count is
   untouched and the indices arrive in walk order. */
static void line_appends_one_byte_per_match(void)
{
    stage_line(8, 8, 3);
    place(0, 5, 2, 0, 0);
    place(1, 4, 2, 0, 0);
    place(2, 3, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 1), 3);
    CHECK_EQ(line_out[0], 2);
    CHECK_EQ(line_out[1], 1);
    CHECK_EQ(line_out[2], 0);
    CHECK_EQ(line_out[3], LINE_OUT_FILL);
}

/* The retired filter is inside fdps_battle_find_unit_at_cursor -- PUSH EAX /
   CALL 0x000109b0 / TEST EAX,EAX / JZ at 0002db1e -- and this body repeats no
   test of its own, so a retired unit on the line is never reported. */
static void line_retired_unit_is_never_reported(void)
{
    stage_line(8, 8, 2);
    place(0, 3, 2, 0, 1);   /* retired enemy */
    place(1, 4, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 1), 1);
    CHECK_EQ(line_out[0], 1);
}

/* MOV [0x00069cd4],EAX at 000137c1 and the same on y at 000137c9 put the
   caller's cursor back, whatever the walk did to it and whether or not it
   found anything. */
static void line_restores_the_cursor_globals(void)
{
    stage_line(8, 8, 1);
    place(0, 3, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 1), 1);
    CHECK_EQ(data_fdps_map_cursor_world_x, LINE_CURSOR_MARK_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, LINE_CURSOR_MARK_Y);

    stage_line(8, 8, 0);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 1), 0);
    CHECK_EQ(data_fdps_map_cursor_world_x, LINE_CURSOR_MARK_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, LINE_CURSOR_MARK_Y);
}

/* Nothing in the body writes through the record pointer: it reads byte +6 and
   that is all. */
static void line_does_not_touch_the_records(void)
{
    stage_line(8, 8, 2);
    place(0, 3, 2, 0, 0);
    place(1, 4, 2, 2, 0x80);
    CHECK_EQ(fdps_collect_targets_in_line(7, 2, line_out, 2, 2, 3, 1), 1);
    CHECK_EQ(stage_units[0].pos_x, 3);
    CHECK_EQ(stage_units[0].pos_y, 2);
    CHECK_EQ(stage_units[0].side, 0);
    CHECK_EQ(stage_units[0].flags, 0);
    CHECK_EQ(stage_units[1].side, 2);
    CHECK_EQ(stage_units[1].flags, 0x80);
}

/* ------------------------------------------------------------------
 * 00011e50 fdps_collect_targets_in_range
 *
 * This one needs five blocks live rather than one.  It reads the movement grid
 * through data_fdps_battle_move_grid_ptr, and on the flood-filled branch it
 * calls fdps_get_class_record (which reads data_fdps_class_table_ptr) and
 * fdps_move_grid_flood_fill_range, which in turn calls fdps_map_load_tile_info
 * over the terrain layer, the attribute table and the event-code layer.  All
 * five are staged here for the same reason the unit array is above: the
 * function takes its whole input from those globals plus its six arguments.
 * Nothing below asserts what any global holds on its own; ticket 23 owns that.
 *
 * The grid always arrives as fdps_map_grid_reset leaves it -- every flags byte
 * 0x00 and every marker 0xff -- because that is the state the function is
 * documented to expect and every call site in the image supplies.  Cells past
 * width*height get marker 0xdd instead, so "the routine wrote here" and "the
 * routine never reached here" stay distinguishable from "this is off the map".
 *
 * Every attribute row is terrain type 0 and every movement cost in the staged
 * class record is 1, so a flood of n points reaches exactly n tiles of open
 * ground and the terrain plays no part unless a case sets a flags byte.
 *
 * Expected values come from the assembly at 00011e50 -- CMP [EBP+0x20],0x10 /
 * JGE at 00011e7a, PUSH 0x0 / CALL 0x00018b70 at 00011e90, CMP EAX,[EBP+0x24] /
 * JGE at 00011f0f, CMP EAX,[EBP+0x20] / JG at 00011f56 and 00011f9d, ADD
 * [EBP+0x20],-0x10 at 00011f2a, the marker store MOV byte ptr [EAX+0x5],0x0,
 * CMP EAX,0xff at 00012038 and the four side tests at 00012044-00012094 -- and
 * by walking the flood fill over the fixture by hand.  None of them is read off
 * the emitted C.
 * ------------------------------------------------------------------ */

#define RANGE_CELLS 64
#define RANGE_ATTR_ROWS 64
#define RANGE_TILES_AT 0x0b
#define RANGE_ATTR_AT 0x11
#define RANGE_EVENT_AT 0x10

static unsigned char range_grid[4 + RANGE_CELLS * 2];
static unsigned char range_tile_map[RANGE_TILES_AT + RANGE_CELLS * 2];
static unsigned char range_attr[RANGE_ATTR_AT + RANGE_ATTR_ROWS * 4];
static unsigned char range_event[RANGE_EVENT_AT + RANGE_CELLS];
static struct fdps_class_record range_class;

/* Build all five blocks and hang the five globals off them.  The class table
   base points straight at the one record, because fdps_get_class_record(0) --
   the literal the function pushes -- resolves to base + 0. */
static void stage_range(int width, int height)
{
    int i;
    int in_grid_cells;

    in_grid_cells = width * height;
    if (in_grid_cells < 0) {
        in_grid_cells = 0;
    }
    if (in_grid_cells > RANGE_CELLS) {
        in_grid_cells = RANGE_CELLS;
    }

    *(short *) range_grid = (short) width;
    *(short *) (range_grid + 2) = (short) height;
    for (i = 0; i < RANGE_CELLS; i++) {
        range_grid[4 + i * 2] = 0x00;
        if (i < in_grid_cells) {
            range_grid[4 + i * 2 + 1] = 0xff;
        } else {
            range_grid[4 + i * 2 + 1] = 0xdd;
        }
    }

    for (i = 0; i < RANGE_TILES_AT; i++) {
        range_tile_map[i] = 0xaa;
    }
    *(short *) (range_tile_map + 7) = (short) width;
    for (i = 0; i < RANGE_CELLS; i++) {
        *(short *) (range_tile_map + RANGE_TILES_AT + i * 2) = (short) i;
    }

    for (i = 0; i < RANGE_ATTR_AT; i++) {
        range_attr[i] = 0xaa;
    }
    for (i = 0; i < RANGE_ATTR_ROWS * 4; i++) {
        range_attr[RANGE_ATTR_AT + i] = 0x00;
    }

    for (i = 0; i < RANGE_EVENT_AT; i++) {
        range_event[i] = 0xaa;
    }
    *(short *) (range_event + 7) = (short) width;
    for (i = 0; i < RANGE_CELLS; i++) {
        range_event[RANGE_EVENT_AT + i] = 0x00;
    }

    for (i = 0; i < 8; i++) {
        range_class.move_cost[i] = 1;
    }
    range_class.critical = 0;
    range_class.magic_resist_complement = 0;

    data_fdps_battle_move_grid_ptr = range_grid;
    data_fdps_scene_layer_tile_map_ptrs[0] = range_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = range_attr;
    data_fdps_map_cell_event_code_layer_ptr = range_event;
    data_fdps_class_table_ptr = (unsigned char *) &range_class;
}

static int range_marker(int index)
{
    return (int) range_grid[4 + index * 2 + 1];
}

static void set_range_cell_flags(int index, int flags)
{
    range_grid[4 + index * 2] = (unsigned char) flags;
}

/* The cell address is formed as grid + 5 + 2 * index -- MOV byte ptr
   [EAX+0x5],0x0 at 00011f6f over EAX = base + 2 * index -- so the marker has to
   be the second byte of a two-byte cell for the C to land on the same byte. */
static void range_cell_is_two_bytes_marker_second(void)
{
    CHECK_EQ((int) sizeof(struct fdps_move_grid_cell), 2);
    CHECK_EQ((int) offsetof(struct fdps_move_grid_cell, marker), 1);
}

/* ADD [EBP+0x20],-0x10 at 00011f2a: range_code 0x10 is a cross with arms of
   zero, so the centre tile alone is marked and its neighbour is left at the
   0xff sentinel. */
static void range_line_reach_is_code_minus_0x10(void)
{
    stage_range(5, 5);
    stage(2);
    place(0, 2, 2, 0, 0);
    place(1, 3, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 0), 1);
    CHECK_EQ(range_marker(2 * 5 + 2), 0);
    CHECK_EQ(range_marker(2 * 5 + 3), 0xff);
}

/* CMP EAX,[EBP+0x20] / JG at 00011f56 and 00011f9d is the inclusive test, so
   arms of one reach exactly one tile out along the row and the column and stop.
   The two loops write nothing off those two lines, so a diagonal neighbour
   keeps the sentinel. */
static void range_line_arms_are_inclusive(void)
{
    stage_range(7, 7);
    stage(0);
    CHECK_EQ(fdps_collect_targets_in_range(3, 3, NULL, 0x11, 0, 0), 0);
    CHECK_EQ(range_marker(3 * 7 + 2), 0);
    CHECK_EQ(range_marker(3 * 7 + 4), 0);
    CHECK_EQ(range_marker(3 * 7 + 1), 0xff);
    CHECK_EQ(range_marker(3 * 7 + 5), 0xff);
    CHECK_EQ(range_marker(2 * 7 + 3), 0);
    CHECK_EQ(range_marker(4 * 7 + 3), 0);
    CHECK_EQ(range_marker(1 * 7 + 3), 0xff);
    CHECK_EQ(range_marker(5 * 7 + 3), 0xff);
    CHECK_EQ(range_marker(2 * 7 + 2), 0xff);
}

/* The straight-line branch is a cross and not a square: only the centre's own
   row and column are painted, so a unit one tile diagonally away is refused
   however long the arms are. */
static void range_line_is_a_cross_not_a_square(void)
{
    unsigned char out[4];

    stage_range(5, 5);
    stage(4);
    place(0, 0, 2, 0, 0);   /* on the row    */
    place(1, 1, 1, 0, 0);   /* diagonal      */
    place(2, 2, 4, 0, 0);   /* on the column */
    place(3, 4, 4, 0, 0);   /* corner        */
    out[0] = 0xee;
    out[1] = 0xee;
    out[2] = 0xee;
    out[3] = 0xee;
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, out, 0x12, 0, 0), 2);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 2);
    CHECK_EQ(out[2], 0xee);
}

/* The min_dist sweep lives under the range_code < 0x10 arm alone -- JMP
   0x00011fbc at 00011f25 leaves the straight-line branch before it -- so a
   cross keeps its own centre tile whatever min_dist says. */
static void range_line_ignores_min_dist(void)
{
    stage_range(5, 5);
    stage(1);
    place(0, 2, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 99, 0), 1);
}

/* Below 0x10 the reach comes from fdps_move_grid_flood_fill_range over the
   default class row, whose eight terrain costs are all 1, so range_code buys
   that many tiles of open ground and a unit one tile further out is refused. */
static void range_flood_reach_is_the_movement_points(void)
{
    stage_range(5, 5);
    stage(4);
    place(0, 2, 2, 0, 0);   /* distance 0 */
    place(1, 3, 2, 0, 0);   /* distance 1 */
    place(2, 4, 2, 0, 0);   /* distance 2 */
    place(3, 4, 3, 0, 0);   /* distance 3 */
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 2, 0, 0), 3);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 1, 0, 0), 2);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0, 0, 0), 1);
}

/* CMP EAX,[EBP+0x24] / JGE at 00011f0f: the Manhattan cut is strictly
   less-than, so min_dist 1 drops the centre tile alone and min_dist 2 drops the
   centre and its four neighbours. */
static void range_min_dist_is_exclusive(void)
{
    stage_range(5, 5);
    stage(3);
    place(0, 2, 2, 0, 0);   /* distance 0 */
    place(1, 3, 2, 0, 0);   /* distance 1 */
    place(2, 4, 2, 0, 0);   /* distance 2 */
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 2, 0, 0), 3);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 2, 1, 0), 2);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 2, 2, 0), 1);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 2, 3, 0), 0);
}

/* Two CALLs to the CRT abs at 00011ef1 and 00011f02, added at 00011f0a: the cut
   is Manhattan and each axis is taken absolutely, so a unit above and to the
   left of the centre is cut at the same min_dist as one below and to the
   right. */
static void range_min_dist_is_manhattan_and_absolute(void)
{
    stage_range(5, 5);
    stage(4);
    place(0, 1, 1, 0, 0);   /* dx -1, dy -1 -> 2 */
    place(1, 3, 3, 0, 0);   /* dx +1, dy +1 -> 2 */
    place(2, 1, 2, 0, 0);   /* dx -1, dy  0 -> 1 */
    place(3, 3, 2, 0, 0);   /* dx +1, dy  0 -> 1 */
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 2, 2, 0), 2);
}

/* The maximum reach is a flood fill and NOT a Manhattan disc
   (rebuild_info/pitfalls.md): the only two-step route from (2,2) to (4,2) runs
   through (3,2), so marking that cell impassable with flag bit 0x40 puts the
   unit at (4,2) out of range even though its Manhattan distance is still 2. */
static void range_flood_is_blocked_by_impassable_cells(void)
{
    stage_range(5, 5);
    stage(1);
    place(0, 4, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 2, 0, 0), 1);
    stage_range(5, 5);
    set_range_cell_flags(2 * 5 + 3, 0x40);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 2, 0, 0), 0);
}

/* CMP EAX,0xff / JNZ at 00012038: a unit whose cell still holds the
   unreachable sentinel is dropped, and that is the only thing the range shape
   contributes to the answer. */
static void range_unreached_cell_drops_the_unit(void)
{
    stage_range(5, 5);
    stage(2);
    place(0, 2, 2, 0, 0);
    place(1, 0, 0, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 0), 1);
}

/* The routine does not reset the grid, it paints over what it is handed
   (00011e63 reads the header and the first write follows immediately).  A grid
   arriving with every marker already relaxed therefore accepts every unit on
   the map, whatever the range says -- which is why every call site resets it
   again on the way out. */
static void range_does_not_reset_the_grid(void)
{
    int i;

    stage_range(5, 5);
    for (i = 0; i < 25; i++) {
        range_grid[4 + i * 2 + 1] = 0x00;
    }
    stage(1);
    place(0, 4, 4, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_range(0, 0, NULL, 0x10, 0, 0), 1);
}

/* AND AL,0x1 / JNZ at 00012023: bit 0 of the status byte drops the record
   before the cell or the side is considered, and it is bit 0 alone. */
static void range_retired_bit_drops_the_unit(void)
{
    stage_range(5, 5);
    stage(1);
    place(0, 2, 2, 0, 0x01);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 0), 0);
    stage_range(5, 5);
    place(0, 2, 2, 0, 0x02);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 0), 1);
}

/* The four side tests at 00012044-00012094.  Mode 2 is CMP EAX,0x1 at 00012077
   -- side 1, no state test -- which is NOT what fdps_collect_targets_in_area
   reads from the same ITEM.DAT byte, and mode 3 is CMP EAX,0x2 with no state
   test either. */
static void range_side_filter_table(void)
{
    stage_range(5, 5);
    stage(3);
    place(0, 2, 2, 0, 0);
    place(1, 2, 2, 1, 0);
    place(2, 2, 2, 2, 0x80);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 0), 1);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 1), 2);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 2), 1);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 3), 1);
}

/* Mode 2 takes side 1 and refuses side 2, mode 3 the other way round: the two
   have to be told apart on the same field, because reading either as the other
   collector's table would swap them. */
static void range_mode2_is_side1_and_mode3_is_side2(void)
{
    unsigned char out[2];

    stage_range(5, 5);
    stage(2);
    place(0, 2, 2, 1, 0);
    place(1, 2, 2, 2, 0);
    out[0] = 0xee;
    out[1] = 0xee;
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, out, 0x10, 0, 2), 1);
    CHECK_EQ(out[0], 0);
    stage_range(5, 5);
    out[0] = 0xee;
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, out, 0x10, 0, 3), 1);
    CHECK_EQ(out[0], 1);
}

/* The chain ends with JMP 0x000120af at 00012096: a mode outside 0..3 accepts
   nothing, so the ITEM.DAT select_mode values 4 and 5 come back with a count of
   zero even with units standing on the centre tile. */
static void range_unknown_mode_matches_nothing(void)
{
    stage_range(5, 5);
    stage(3);
    place(0, 2, 2, 0, 0);
    place(1, 2, 2, 1, 0);
    place(2, 2, 2, 2, 0);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 4), 0);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 5), 0);
    stage_range(5, 5);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, -1), 0);
}

/* CMP [EBP+0x1c],0x0 / JZ 0x000120a9 at 00012098 skips the store only: the
   increment at 000120ac is outside the guard, so a probing call that passes
   NULL still gets a real count back. */
static void range_null_out_still_counts(void)
{
    stage_range(5, 5);
    stage(2);
    place(0, 2, 2, 0, 0);
    place(1, 2, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 0), 2);
}

/* MOV AL,byte ptr [EBP-0x2c] / MOV byte ptr [EDX],AL at 000120a4: the loop
   index, not a serial number of the match, is stored one byte per match at
   out_indices[count], and nothing is written past the last match. */
static void range_appends_unit_indices(void)
{
    unsigned char out[4];

    stage_range(5, 5);
    stage(4);
    place(0, 0, 0, 0, 0);
    place(1, 2, 2, 0, 0);
    place(2, 0, 0, 0, 0);
    place(3, 2, 2, 0, 0);
    out[0] = 0xee;
    out[1] = 0xee;
    out[2] = 0xee;
    out[3] = 0xee;
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, out, 0x10, 0, 0), 2);
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[1], 3);
    CHECK_EQ(out[2], 0xee);
    CHECK_EQ(out[3], 0xee);
}

/* CMP EAX,[0x00060150] / JL at 00011fc3: the scan stops at the unit count and
   not at the end of whatever block the pointer names. */
static void range_scan_stops_at_the_unit_count(void)
{
    stage_range(5, 5);
    stage(2);
    place(0, 2, 2, 0, 0);
    place(1, 2, 2, 0, 0);
    place(2, 2, 2, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_range(2, 2, NULL, 0x10, 0, 0), 2);
}

/* IMUL EAX,[EBP-0xc] at 00012002 scales the unit's row by the MOVEMENT GRID's
   width, and nothing checks either coordinate against the header (the plate at
   00011e50 says so in as many words).  On a grid four columns wide the unit at
   (5, 0) therefore reads the very cell the unit at (1, 1) stands on and is
   accepted with it. */
static void range_unit_cell_uses_the_grid_width_unchecked(void)
{
    stage_range(4, 4);
    stage(2);
    place(0, 1, 1, 0, 0);
    place(1, 5, 0, 0, 0);
    CHECK_EQ(fdps_collect_targets_in_range(1, 1, NULL, 0x10, 0, 0), 2);
    CHECK_EQ(range_marker(1 * 4 + 1), 0);
}

/* ------------------------------------------------------------------
 * 000125c0 fdps_check_can_counter_attack_from_tile
 *
 * Two blocks live: the map unit array, staged by stage() above, and the
 * ITEM.DAT table, because the range test reaches it through
 * fdps_unit_find_equipped_slot -> fdps_get_item_record.  Both are staged here
 * for the same reason as everything above -- the function takes its whole
 * input from those two globals plus its three arguments.  Nothing below
 * asserts what either global holds on its own; ticket 23 owns that.
 *
 * The item block is published one record PAST the start of its storage so
 * record -1 is real addressable memory, the same arrangement tests/unititem.c
 * uses, because fdps_unit_get_item_id widens the id byte without sign and an
 * id of 0xff must reach record 255.
 *
 * Expected values come from the assembly at 000125c0 -- CMP byte ptr
 * [EAX+0x26],0x0 / JZ at 000125de, the two CALL 0x0003d364 abs pairs at
 * 00012602 and 00012620 with CMP EAX,0x1 / JZ at 00012631, PUSH 0x0 / CALL
 * 0x00025140 at 0001263f with CMP [EBP-0x10],-0x1 / JNZ at 00012650, MOV AL,
 * byte ptr [EAX+0xb] / AND EAX,0xff / CMP EAX,0x1 / JLE at 00012684, and the
 * four MOV [EBP-0x4],0xffffffff stores against the single MOV [EBP-0x4],0x1 at
 * 0001269a -- from the record layouts ticket 17 settled (status_timers at
 * +0x22, item record 0x17 with range_min at +0x0b) and from assets/items.md
 * for item 0x63.  None of them is read off the emitted C.
 * ------------------------------------------------------------------ */

/* IMUL EAX,dword ptr [EBP+0x14],0x17 in fdps_get_item_record. */
#define ITEM_RECORD_STRIDE 0x17
#define COUNTER_ITEM_COUNT 256

/* AND AL,0x40 in fdps_unit_find_equipped_slot: bit 0x40 of an inventory
   entry's flag byte is what marks the entry equipped. */
#define INVENTORY_FLAG_EQUIPPED 0x40
#define INVENTORY_FLAG_CARRIED 0x00

/* Item type 1 is a plain weapon and 0x16 the lowest armour type
   (assets/items.md); fdps_unit_find_equipped_slot with want_armor 0 accepts
   the first and refuses the second. */
#define ITEM_TYPE_WEAPON 0x01
#define ITEM_TYPE_ARMOR 0x16

static unsigned char counter_items[(COUNTER_ITEM_COUNT + 1) * ITEM_RECORD_STRIDE];

static struct fdps_item_effect *counter_item(int item_id)
{
    return (struct fdps_item_effect *)
           (counter_items + (item_id + 1) * ITEM_RECORD_STRIDE);
}

/* Zero both blocks, publish both bases, and give the battle `units` records.
   A unit staged this way has every inventory flag byte clear -- nothing
   equipped -- and every status timer at zero. */
static void stage_counter(int units)
{
    int i;

    for (i = 0; i < (int) sizeof(counter_items); i++) {
        counter_items[i] = 0;
    }
    stage(units);
    data_fdps_item_effect_table_ptr = counter_items + ITEM_RECORD_STRIDE;
}

/* Put item_id in slot `slot` of unit `unit_index` with the equipped bit set,
   and give that item record the type and the two range bytes asked for. */
static void arm(int unit_index, int slot, int item_id, int item_type,
                int range_min, int range_max)
{
    struct fdps_item_effect *item;

    stage_units[unit_index].inventory_slots[slot * 2] =
        (unsigned char) INVENTORY_FLAG_EQUIPPED;
    stage_units[unit_index].inventory_slots[slot * 2 + 1] =
        (unsigned char) item_id;
    item = counter_item(item_id);
    item->type = (unsigned char) item_type;
    item->range_min = (unsigned char) range_min;
    item->range_max = (unsigned char) range_max;
}

/* The three record bytes the body addresses by literal displacement: +0x26 for
   the paralysis counter, +0x0b for range_min and +0x0c for the range_max it
   pointedly does not read.  If the layout moved, every case below would still
   pass while reading the wrong bytes. */
static void counter_reads_the_measured_offsets(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) + 4, 0x26);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, range_min), 0x0b);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, range_max), 0x0c);
    CHECK_EQ((int) sizeof(struct fdps_item_effect), ITEM_RECORD_STRIDE);
}

/* Four MOV dword ptr [EBP-0x4],0xffffffff against one MOV [EBP-0x4],0x1: the
   refusal is -1 and never 0, which is what makes the bare-predicate spelling
   wrong (rebuild_info/pitfalls.md).  All four refusals answer the same value.
 */
static void counter_refusal_is_minus_one(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);

    /* paralysed, everything else in order */
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    stage_units[0].status_timers[4] = 1;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);
    stage_units[0].status_timers[4] = 0;

    /* not adjacent */
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 6, 4), -1);

    /* nothing equipped */
    stage_units[0].inventory_slots[0] = (unsigned char) INVENTORY_FLAG_CARRIED;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);

    /* equipped, but the weapon cannot reach the next tile */
    stage_units[0].inventory_slots[0] = (unsigned char) INVENTORY_FLAG_EQUIPPED;
    counter_item(0x10)->range_min = 2;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);

    /* and the one acceptance is exactly 1 */
    counter_item(0x10)->range_min = 1;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);
}

/* CMP EAX,0x1 / JZ at 00012631 on the sum of the two abs calls: the sum must
   EQUAL one, so all four orthogonal neighbours count and nothing else does --
   the defender's own tile at sum 0 and every diagonal at sum 2 are refused
   just as a tile two away is. */
static void counter_adjacency_is_exactly_one_tile(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);

    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 3, 4), 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 4, 5), 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 4, 3), 1);

    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 4, 4), -1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 5), -1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 3, 3), -1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 3), -1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 6, 4), -1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 4, 6), -1);
}

/* The tile bytes are read from record offsets +0 and +1 and zero-extended
   (AND EAX,0xff at 000125f5 and 00012613), so a defender standing on a high
   tile number is as reachable as one near the origin and neither delta is ever
   negative on the way into abs. */
static void counter_position_bytes_are_unsigned(void)
{
    stage_counter(1);
    place(0, 200, 250, 0, 0);
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);

    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 201, 250), 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 199, 250), 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 200, 249), 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 200, 252), -1);
}

/* CMP byte ptr [EAX+0x26],0x0 / JZ at 000125de: the paralysis counter is the
   only timer consulted, and it blocks on any non-zero count rather than on a
   particular one.  The other five timers at +0x22..+0x25 and +0x27 leave the
   answer alone. */
static void counter_paralysis_timer_blocks(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);

    stage_units[0].status_timers[4] = 1;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);
    stage_units[0].status_timers[4] = 2;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);
    stage_units[0].status_timers[4] = 0xff;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);

    stage_units[0].status_timers[4] = 0;
    stage_units[0].status_timers[0] = 3;
    stage_units[0].status_timers[1] = 3;
    stage_units[0].status_timers[2] = 3;
    stage_units[0].status_timers[3] = 3;
    stage_units[0].status_timers[5] = 3;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);
}

/* CMP dword ptr [EBP-0x10],-0x1 / JNZ at 00012650, over a call that passes
   PUSH 0x0 for want_armor at 0001263f: a defender with nothing equipped, or
   carrying its weapon without the 0x40 bit, or wearing armour and no weapon,
   does not strike back. */
static void counter_needs_an_equipped_weapon(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);

    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    stage_units[0].inventory_slots[0] = (unsigned char) INVENTORY_FLAG_CARRIED;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);

    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 0, 0x40, ITEM_TYPE_ARMOR, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);

    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);
}

/* CMP EAX,0x1 / JLE at 0001268c: the reach test is range_min BELOW 2, so 0 and
   1 both pass and 2 upwards fails.  The twin at 000137e0 tests the same byte
   for equality with 1, which is why 0 has to be pinned here separately
   (rebuild_info/pitfalls.md). */
static void counter_range_min_below_two(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);

    counter_item(0x10)->range_min = 1;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);
    counter_item(0x10)->range_min = 2;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);
    counter_item(0x10)->range_min = 3;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);
    counter_item(0x10)->range_min = 0xff;
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);
}

/* Item 0x63 光束砲 is type 0x15 -- a weapon type, so the equipped-slot search
   accepts it -- with range 0-0 (assets/items.md).  This function says its
   holder counterattacks; that is the one item on which it and the unit-index
   twin disagree, and it is the reason the two may not share a helper. */
static void counter_accepts_the_zero_range_weapon(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 0, 0x63, 0x15, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);
}

/* range_max at +0x0c is never loaded: a weapon whose minimum reach is one
   answers 1 with a maximum of zero behind it, and one whose minimum is five
   answers -1 however far its maximum stretches. */
static void counter_ignores_range_max(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 0);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);

    arm(0, 0, 0x11, ITEM_TYPE_WEAPON, 5, 9);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);
}

/* The item whose range decides the answer is the one in the slot
   fdps_unit_find_equipped_slot returned, and that search stops at the FIRST
   equipped weapon.  With a short-ranged weapon equipped in slot 1 and a
   melee one in slot 4, the answer is the slot 1 weapon's. */
static void counter_uses_the_first_equipped_weapon(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 4, 0x21, ITEM_TYPE_WEAPON, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);

    arm(0, 1, 0x20, ITEM_TYPE_WEAPON, 3, 5);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), -1);
}

/* Every record read goes through fdps_get_unit_record(defender_unit), so the
   subject is the unit the index names and not the first one: unit 2's tile and
   unit 2's weapon decide the answer while unit 0 stands adjacent with a
   perfectly good sword. */
static void counter_subject_is_the_named_unit(void)
{
    stage_counter(3);
    place(0, 4, 4, 0, 0);
    place(1, 9, 9, 0, 0);
    place(2, 1, 1, 0, 0);
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    arm(2, 0, 0x11, ITEM_TYPE_WEAPON, 1, 1);

    CHECK_EQ(fdps_check_can_counter_attack_from_tile(2, 1, 2), 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(2, 5, 4), -1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(1, 9, 8), -1);
}

/* Nothing in the body writes: the four fields it reads come back unchanged
   after an accepting call and a refusing one. */
static void counter_does_not_touch_the_records(void)
{
    stage_counter(1);
    place(0, 4, 4, 0, 0);
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 7);
    stage_units[0].status_timers[4] = 0;

    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 5, 4), 1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(0, 7, 4), -1);
    CHECK_EQ(stage_units[0].pos_x, 4);
    CHECK_EQ(stage_units[0].pos_y, 4);
    CHECK_EQ(stage_units[0].status_timers[4], 0);
    CHECK_EQ(stage_units[0].inventory_slots[0], INVENTORY_FLAG_EQUIPPED);
    CHECK_EQ(stage_units[0].inventory_slots[1], 0x10);
    CHECK_EQ(counter_item(0x10)->range_min, 1);
    CHECK_EQ(counter_item(0x10)->range_max, 7);
}

/* ------------------------------------------------------------------
 * 000137e0 fdps_check_can_counter_attack
 *
 * The same two blocks as the tile twin above, staged the same way by
 * stage_counter()/arm(): the map unit array, because both units are resolved
 * through fdps_get_unit_record, and the ITEM.DAT table, because the range test
 * reaches it through fdps_unit_find_equipped_slot -> fdps_get_item_record.
 * Nothing below asserts what either global holds on its own; ticket 23 owns
 * that.
 *
 * Expected values come from the assembly at 000137e0 -- the two CALL 0x0002d210
 * record fetches at 000137f0 and 000137ff, CMP byte ptr [EAX+0x26],0x0 / JZ at
 * 0001380d on the SECOND of them, the two CALL 0x0003d364 abs pairs at 00013835
 * and 00013856 over deltas formed attacker-minus-defender and zero-extended
 * (XOR EDX,EDX / MOV DL and AND EAX,0xff), CMP EAX,0x1 / JZ at 00013867, PUSH
 * 0x0 / CALL 0x00025140 at 00013875 with CMP [EBP-0x8],-0x1 / JNZ at 00013886,
 * MOV AL,byte ptr [EAX+0xb] / AND EAX,0xff / CMP EAX,0x1 / JZ at 000138c2, and
 * the four MOV [EBP-0x4],0xffffffff stores against the single MOV
 * [EBP-0x4],0x1 at 000138d0 -- and from assets/items.md for item 0x63.  None of
 * them is read off the emitted C.
 * ------------------------------------------------------------------ */

/* Four MOV dword ptr [EBP-0x4],0xffffffff against one MOV [EBP-0x4],0x1: every
   refusal answers -1 and never 0, which is what all six call sites' CMP EAX,1
   is for.  Unit 0 attacks unit 1 throughout. */
static void unit_counter_refusal_is_minus_one(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);

    /* defender paralysed, everything else in order */
    stage_units[1].status_timers[4] = 1;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    stage_units[1].status_timers[4] = 0;

    /* not adjacent: the attacker two tiles along the row */
    stage_units[0].pos_x = 7;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    stage_units[0].pos_x = 4;

    /* nothing equipped */
    stage_units[1].inventory_slots[0] = (unsigned char) INVENTORY_FLAG_CARRIED;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    stage_units[1].inventory_slots[0] = (unsigned char) INVENTORY_FLAG_EQUIPPED;

    /* equipped, but the weapon's minimum reach is not one */
    counter_item(0x10)->range_min = 2;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);

    /* and the one acceptance */
    counter_item(0x10)->range_min = 1;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
}

/* CMP EAX,0x1 / JZ at 00013867 on the sum of the two abs calls: the sum must
   EQUAL one, so the four orthogonal neighbours count and nothing else does --
   two units sharing a tile (sum 0), a diagonal (sum 2) and a tile two away are
   all refused. */
static void unit_counter_adjacency_is_exactly_one_tile(void)
{
    stage_counter(2);
    place(1, 4, 4, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);

    place(0, 5, 4, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
    place(0, 3, 4, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
    place(0, 4, 5, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
    place(0, 4, 3, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);

    place(0, 4, 4, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    place(0, 5, 5, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    place(0, 3, 3, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    place(0, 6, 4, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    place(0, 4, 6, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
}

/* Both tile bytes are zero-extended -- XOR EDX,EDX / MOV DL for the attacker
   and AND EAX,0xff for the defender at 00013822 and 00013845 -- so a pair
   standing on high tile numbers is judged exactly as one near the origin and
   neither delta is ever negative on the way into abs. */
static void unit_counter_position_bytes_are_unsigned(void)
{
    stage_counter(2);
    place(1, 200, 250, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);

    place(0, 201, 250, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
    place(0, 199, 250, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
    place(0, 200, 249, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
    place(0, 200, 252, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
}

/* CMP byte ptr [EAX+0x26],0x0 / JZ at 0001380d reads the record fetched by the
   SECOND call, the defender's.  Any non-zero count blocks, the other five
   timers do not, and the attacker's own paralysis counter is never looked at --
   a paralysed attacker still gets struck back. */
static void unit_counter_paralysis_is_the_defenders(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);

    stage_units[1].status_timers[4] = 1;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    stage_units[1].status_timers[4] = 0xff;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);

    stage_units[1].status_timers[4] = 0;
    stage_units[1].status_timers[0] = 3;
    stage_units[1].status_timers[3] = 3;
    stage_units[1].status_timers[5] = 3;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);

    stage_units[0].status_timers[4] = 9;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
}

/* CMP dword ptr [EBP-0x8],-0x1 / JNZ at 00013886, over a call that pushes 0x0
   for want_armor at 00013875, and the unit index it passes is [EBP+0x18], the
   defender's: the defender needs an equipped WEAPON, and the attacker's
   equipment plays no part. */
static void unit_counter_needs_the_defenders_equipped_weapon(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);

    /* arming the ATTACKER changes nothing */
    arm(0, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);

    /* the defender carrying its weapon without the equipped bit */
    arm(1, 0, 0x11, ITEM_TYPE_WEAPON, 1, 1);
    stage_units[1].inventory_slots[0] = (unsigned char) INVENTORY_FLAG_CARRIED;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);

    /* the defender wearing armour and no weapon */
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x40, ITEM_TYPE_ARMOR, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);

    /* the defender with a real equipped weapon */
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x11, ITEM_TYPE_WEAPON, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
}

/* CMP EAX,0x1 / JZ at 000138c2: the reach test here is EQUALITY with 1, not the
   tile twin's JLE, so range_min 0 is refused alongside 2 and everything above.
   Pinning 0 is the whole point of this case. */
static void unit_counter_range_min_must_equal_one(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);

    counter_item(0x10)->range_min = 0;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    counter_item(0x10)->range_min = 2;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    counter_item(0x10)->range_min = 3;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    counter_item(0x10)->range_min = 0xff;
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
}

/* Item 0x63 光束砲 is type 0x15 -- a weapon type, so the equipped-slot search
   accepts it -- with range 0-0 (assets/items.md).  The two counterattack
   functions disagree about its holder, and both halves of that disagreement are
   asserted here together: the map AI's tile-shaped question says the shot comes
   back, the exchange's unit-shaped question says it does not.  A shared helper
   cannot produce both answers. */
static void unit_counter_rejects_the_zero_range_weapon(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x63, 0x15, 0, 0);

    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
    CHECK_EQ(fdps_check_can_counter_attack_from_tile(1, 4, 4), 1);
}

/* range_max at +0x0c is never loaded: a weapon whose minimum reach is one
   answers 1 with a maximum of zero behind it, and one whose minimum is five
   answers -1 however far its maximum stretches. */
static void unit_counter_ignores_range_max(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 0);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);

    arm(1, 0, 0x11, ITEM_TYPE_WEAPON, 5, 9);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
}

/* The two arguments are not interchangeable.  Only the attacker's pos_x/pos_y
   are read; every other test is on the defender.  With unit 0 unarmed and
   paralysed next to a fully armed unit 1, asking it the one way round answers 1
   and the other way round -1. */
static void unit_counter_arguments_are_not_symmetric(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    stage_units[0].status_timers[4] = 4;

    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
    CHECK_EQ(fdps_check_can_counter_attack(1, 0), -1);
}

/* Every record read goes through fdps_get_unit_record on the index passed, so
   the subject is the unit each index names and not the first one: unit 2's tile
   and unit 2's weapon decide the answer while unit 0 stands next to unit 1 with
   a perfectly good sword. */
static void unit_counter_subject_is_the_named_unit(void)
{
    stage_counter(4);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    place(2, 1, 1, 0, 0);
    place(3, 1, 2, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 1);
    arm(2, 0, 0x11, ITEM_TYPE_WEAPON, 1, 1);

    CHECK_EQ(fdps_check_can_counter_attack(3, 2), 1);
    CHECK_EQ(fdps_check_can_counter_attack(0, 2), -1);
    CHECK_EQ(fdps_check_can_counter_attack(3, 1), -1);
}

/* The item whose range decides the answer is the one in the slot
   fdps_unit_find_equipped_slot returned, and that search stops at the FIRST
   equipped weapon: a long-ranged weapon in slot 1 outranks a melee one in slot
   4. */
static void unit_counter_uses_the_first_equipped_weapon(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 4, 0x21, ITEM_TYPE_WEAPON, 1, 1);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);

    arm(1, 1, 0x20, ITEM_TYPE_WEAPON, 3, 5);
    CHECK_EQ(fdps_check_can_counter_attack(0, 1), -1);
}

/* Nothing in the body writes: the fields it reads on both records come back
   unchanged after an accepting call and a refusing one. */
static void unit_counter_does_not_touch_the_records(void)
{
    stage_counter(2);
    place(0, 4, 4, 0, 0);
    place(1, 5, 4, 0, 0);
    arm(1, 0, 0x10, ITEM_TYPE_WEAPON, 1, 7);

    CHECK_EQ(fdps_check_can_counter_attack(0, 1), 1);
    CHECK_EQ(fdps_check_can_counter_attack(1, 1), -1);
    CHECK_EQ(stage_units[0].pos_x, 4);
    CHECK_EQ(stage_units[0].pos_y, 4);
    CHECK_EQ(stage_units[1].pos_x, 5);
    CHECK_EQ(stage_units[1].pos_y, 4);
    CHECK_EQ(stage_units[1].status_timers[4], 0);
    CHECK_EQ(stage_units[1].inventory_slots[0], INVENTORY_FLAG_EQUIPPED);
    CHECK_EQ(stage_units[1].inventory_slots[1], 0x10);
    CHECK_EQ(counter_item(0x10)->range_min, 1);
    CHECK_EQ(counter_item(0x10)->range_max, 7);
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

    RUN_TEST(line_walk_is_horizontal_unless_x_matches);
    RUN_TEST(line_walk_is_vertical_when_x_matches);
    RUN_TEST(line_step_sign_follows_the_aim);
    RUN_TEST(line_aim_at_the_origin_walks_down);
    RUN_TEST(line_origin_tile_is_never_examined);
    RUN_TEST(line_length_counts_tiles_beyond_the_origin);
    RUN_TEST(line_bounds_come_from_the_grid_header);
    RUN_TEST(line_walk_off_the_left_edge_is_skipped);
    RUN_TEST(line_side_filter_is_inverted);
    RUN_TEST(line_appends_one_byte_per_match);
    RUN_TEST(line_retired_unit_is_never_reported);
    RUN_TEST(line_restores_the_cursor_globals);
    RUN_TEST(line_does_not_touch_the_records);

    RUN_TEST(range_cell_is_two_bytes_marker_second);
    RUN_TEST(range_line_reach_is_code_minus_0x10);
    RUN_TEST(range_line_arms_are_inclusive);
    RUN_TEST(range_line_is_a_cross_not_a_square);
    RUN_TEST(range_line_ignores_min_dist);
    RUN_TEST(range_flood_reach_is_the_movement_points);
    RUN_TEST(range_min_dist_is_exclusive);
    RUN_TEST(range_min_dist_is_manhattan_and_absolute);
    RUN_TEST(range_flood_is_blocked_by_impassable_cells);
    RUN_TEST(range_unreached_cell_drops_the_unit);
    RUN_TEST(range_does_not_reset_the_grid);
    RUN_TEST(range_retired_bit_drops_the_unit);
    RUN_TEST(range_side_filter_table);
    RUN_TEST(range_mode2_is_side1_and_mode3_is_side2);
    RUN_TEST(range_unknown_mode_matches_nothing);
    RUN_TEST(range_null_out_still_counts);
    RUN_TEST(range_appends_unit_indices);
    RUN_TEST(range_scan_stops_at_the_unit_count);
    RUN_TEST(range_unit_cell_uses_the_grid_width_unchecked);

    RUN_TEST(counter_reads_the_measured_offsets);
    RUN_TEST(counter_refusal_is_minus_one);
    RUN_TEST(counter_adjacency_is_exactly_one_tile);
    RUN_TEST(counter_position_bytes_are_unsigned);
    RUN_TEST(counter_paralysis_timer_blocks);
    RUN_TEST(counter_needs_an_equipped_weapon);
    RUN_TEST(counter_range_min_below_two);
    RUN_TEST(counter_accepts_the_zero_range_weapon);
    RUN_TEST(counter_ignores_range_max);
    RUN_TEST(counter_uses_the_first_equipped_weapon);
    RUN_TEST(counter_subject_is_the_named_unit);
    RUN_TEST(counter_does_not_touch_the_records);

    RUN_TEST(unit_counter_refusal_is_minus_one);
    RUN_TEST(unit_counter_adjacency_is_exactly_one_tile);
    RUN_TEST(unit_counter_position_bytes_are_unsigned);
    RUN_TEST(unit_counter_paralysis_is_the_defenders);
    RUN_TEST(unit_counter_needs_the_defenders_equipped_weapon);
    RUN_TEST(unit_counter_range_min_must_equal_one);
    RUN_TEST(unit_counter_rejects_the_zero_range_weapon);
    RUN_TEST(unit_counter_ignores_range_max);
    RUN_TEST(unit_counter_arguments_are_not_symmetric);
    RUN_TEST(unit_counter_subject_is_the_named_unit);
    RUN_TEST(unit_counter_uses_the_first_equipped_weapon);
    RUN_TEST(unit_counter_does_not_touch_the_records);

    /* Put the globals back before leaving.  The runners share one process and
       every block above is this translation unit's own fixture: a later unit
       that expects an unallocated grid or an empty battle would otherwise
       inherit a live pointer into it and pass or fail for the wrong reason. */
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_attr_ptr[0] = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_class_table_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
}
