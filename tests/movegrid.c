/* tests/movegrid.c -- cover for src/movegrid.c.
 *
 * Expected values come from the assembly at 00010b20 -- MOVSX on the two
 * header words, CMP/JG on the product, AND byte ptr [EAX],0x3f, MOV byte ptr
 * [EAX+1],0xff, ADD [EBP-4],2 -- and from the cell layout ticket 17 settled
 * (flags +0, marker +1, stride 2).  None of them is read off the emitted C.
 *
 * The grid is staged here rather than read from a game file: the function
 * takes its entire input from data_fdps_battle_move_grid_ptr, so pointing that
 * at a local block is the only way to reach the loop.  Nothing below asserts
 * what that global holds on its own -- ticket 23 owns that.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "movegrid.h"

/* Room for a 4x4 grid plus spare cells past the bound, so a walk that runs
   long has somewhere to be caught doing it. */
#define STAGE_CELLS 24

static unsigned char stage_grid[4 + STAGE_CELLS * 2];

/* Fill every cell with flags 0xff / marker 0x00 and write the header.  Both
   sentinels are the opposite of what a reset cell holds (flags 0x3f, marker
   0xff), so a single cell says on its own whether it was walked. */
static void stage(int width, int height)
{
    int i;

    for (i = 0; i < STAGE_CELLS; i++) {
        stage_grid[4 + i * 2] = 0xff;
        stage_grid[4 + i * 2 + 1] = 0x00;
    }
    *(short *) stage_grid = (short) width;
    *(short *) (stage_grid + 2) = (short) height;
    data_fdps_battle_move_grid_ptr = stage_grid;
}

static int cell_flags(int index)
{
    return (int) stage_grid[4 + index * 2];
}

static int cell_marker(int index)
{
    return (int) stage_grid[4 + index * 2 + 1];
}

/* ADD dword ptr [EBP-0x4],0x2 at 00010b81: the walk steps one cell every two
   bytes, which is the size the cell record has to come out as for the pointer
   arithmetic in the C to land on the same bytes. */
static void grid_cell_stride_is_two(void)
{
    CHECK_EQ((int) sizeof(struct fdps_move_grid_cell), 2);
    CHECK_EQ((int) offsetof(struct fdps_move_grid_cell, flags), 0);
    CHECK_EQ((int) offsetof(struct fdps_move_grid_cell, marker), 1);
}

/* CMP dword ptr [0x00060144],0x0 / JZ 0x00010b87 jumps straight to the
   epilogue: with no grid allocated nothing is read and nothing is written.
   The block staged first is left holding its sentinels. */
static void grid_null_pointer_returns_at_once(void)
{
    stage(2, 2);
    data_fdps_battle_move_grid_ptr = NULL;
    fdps_map_grid_reset();
    CHECK_EQ(cell_flags(0), 0xff);
    CHECK_EQ(cell_marker(0), 0x00);
    CHECK_EQ(data_fdps_battle_move_grid_ptr == NULL, 1);
}

/* AND byte ptr [EAX],0x3f clears exactly bit 0x40 (a unit stands here) and bit
   0x80 (this tile adjoins a unit); MOV byte ptr [EAX+1],0xff stores the
   unreachable sentinel.  A cell that came in fully set comes out 0x3f / 0xff. */
static void grid_clears_both_zoc_bits(void)
{
    stage(1, 1);
    fdps_map_grid_reset();
    CHECK_EQ(cell_flags(0), 0x3f);
    CHECK_EQ(cell_marker(0), 0xff);
}

/* The mask preserves the low six bits rather than blanking the byte: 0x2a has
   neither zone bit and survives untouched, 0x6b loses only 0x40, and 0xc0
   loses both and leaves nothing.  Writing 0 into byte 0 would pass the two
   outer cases and fail the middle one. */
static void grid_preserves_low_six_bits(void)
{
    stage(3, 1);
    stage_grid[4] = 0x2a;
    stage_grid[6] = 0x6b;
    stage_grid[8] = 0xc0;
    fdps_map_grid_reset();
    CHECK_EQ(cell_flags(0), 0x2a);
    CHECK_EQ(cell_flags(1), 0x2b);
    CHECK_EQ(cell_flags(2), 0x00);
}

/* The marker store is unconditional -- there is no test around 00010b7d -- so
   a cell that already carried a relaxed cost from the previous pass goes back
   to 0xff rather than being left alone. */
static void grid_marker_is_reset_unconditionally(void)
{
    stage(2, 2);
    stage_grid[5] = 0x00;
    stage_grid[7] = 0x05;
    stage_grid[9] = 0xff;
    stage_grid[11] = 0x7f;
    fdps_map_grid_reset();
    CHECK_EQ(cell_marker(0), 0xff);
    CHECK_EQ(cell_marker(1), 0xff);
    CHECK_EQ(cell_marker(2), 0xff);
    CHECK_EQ(cell_marker(3), 0xff);
}

/* ADD EAX,0x4 at 00010b3a: the walk starts past the two header words, so the
   header survives the reset and the first cell written is the one at +4. */
static void grid_cells_start_after_the_header(void)
{
    stage(2, 3);
    fdps_map_grid_reset();
    CHECK_EQ((int) *(short *) stage_grid, 2);
    CHECK_EQ((int) *(short *) (stage_grid + 2), 3);
    CHECK_EQ(cell_flags(0), 0x3f);
}

/* IMUL EAX,dword ptr [EBP-0xc] / CMP EAX,[EBP-0x8] / JG: the bound is the
   product of the two header words, so a 3x2 grid walks cells 0..5 in row-major
   order and cell 6 is never reached, however much room the block has. */
static void grid_walks_width_times_height_cells(void)
{
    stage(3, 2);
    fdps_map_grid_reset();
    CHECK_EQ(cell_flags(0), 0x3f);
    CHECK_EQ(cell_flags(5), 0x3f);
    CHECK_EQ(cell_marker(5), 0xff);
    CHECK_EQ(cell_flags(6), 0xff);
    CHECK_EQ(cell_marker(6), 0x00);
}

/* The dimensions are read from the header on every call, not remembered: the
   same block reset as 2x2 and then as 4x2 clears a different number of cells. */
static void grid_bound_follows_the_header(void)
{
    stage(2, 2);
    fdps_map_grid_reset();
    CHECK_EQ(cell_flags(4), 0xff);
    stage(4, 2);
    fdps_map_grid_reset();
    CHECK_EQ(cell_flags(4), 0x3f);
    CHECK_EQ(cell_flags(7), 0x3f);
    CHECK_EQ(cell_flags(8), 0xff);
}

/* The product is zero, so the very first JG at 00010b68 falls through to the
   JMP out.  Either dimension being zero blanks nothing. */
static void grid_zero_dimension_touches_nothing(void)
{
    stage(0, 4);
    fdps_map_grid_reset();
    CHECK_EQ(cell_flags(0), 0xff);
    CHECK_EQ(cell_marker(0), 0x00);
    stage(4, 0);
    fdps_map_grid_reset();
    CHECK_EQ(cell_flags(0), 0xff);
    CHECK_EQ(cell_marker(0), 0x00);
}

/* MOVSX word ptr, and JG on the product: a header word of 0xffff is -1, the
   product is -4, and -4 > 0 is false, so nothing is walked.  Reading the
   header as unsigned (65535 * 4) or comparing the bound with JA would walk a
   quarter of a million cells off the end of the block here. */
static void grid_header_words_are_signed(void)
{
    stage(-1, 4);
    fdps_map_grid_reset();
    CHECK_EQ((int) *(short *) stage_grid, -1);
    CHECK_EQ(cell_flags(0), 0xff);
    CHECK_EQ(cell_marker(0), 0x00);
}

/* --- fdps_move_grid_mark_zone_of_control @ 00010c30 -----------------------
 *
 * Expected values come from the assembly: OR byte ptr [EAX],0x80 at 00010ca1,
 * 00010ce5, 00010d2c and 00010d73 for the four neighbours, OR byte ptr
 * [EAX],0x40 at 00010d93 for the centre, the three guards CMP [EBP+0x14],0 /
 * JZ, CMP [EBP+0x18],0 / JZ and DEC EAX / CMP EAX,[EBP+0x14] / JLE, and the
 * address form ADD EAX,EAX / ADD EAX,base / ADD EAX,4 around IMUL EAX,y /
 * ADD EAX,x.  None of them is read off the emitted C.
 */

/* Same block as stage() but blanked to 0x00 rather than 0xff: this function
   ORs bits in, so a cell has to start with none of them for an assertion to
   say which bit arrived and which cell it arrived in. */
static void stage_blank(int width, int height)
{
    int i;

    for (i = 0; i < STAGE_CELLS; i++) {
        stage_grid[4 + i * 2] = 0x00;
        stage_grid[4 + i * 2 + 1] = 0x00;
    }
    *(short *) stage_grid = (short) width;
    *(short *) (stage_grid + 2) = (short) height;
    data_fdps_battle_move_grid_ptr = stage_grid;
}

/* CMP dword ptr [0x00060144],0x0 / JZ 0x00010d96 is the whole function when
   no grid is allocated: not even the centre tile is marked. */
static void zoc_null_pointer_returns_at_once(void)
{
    stage_blank(3, 3);
    data_fdps_battle_move_grid_ptr = NULL;
    fdps_move_grid_mark_zone_of_control(1, 1);
    CHECK_EQ(cell_flags(0), 0x00);
    CHECK_EQ(cell_flags(4), 0x00);
    CHECK_EQ(cell_flags(8), 0x00);
}

/* A tile with all four neighbours present: on a 3x3 grid (1,1) is cell 4, its
   neighbours are cells 3, 1, 5 and 7, and the four diagonals 0, 2, 6 and 8 are
   not touched -- there is no diagonal mark in the function.  The centre takes
   0x40 and only 0x40; the neighbours take 0x80 and only 0x80.  The marker byte
   is never written: every OR is a byte operation on byte 0 of the cell. */
static void zoc_marks_four_neighbours_and_centre(void)
{
    stage_blank(3, 3);
    fdps_move_grid_mark_zone_of_control(1, 1);
    CHECK_EQ(cell_flags(4), 0x40);
    CHECK_EQ(cell_flags(3), 0x80);
    CHECK_EQ(cell_flags(1), 0x80);
    CHECK_EQ(cell_flags(5), 0x80);
    CHECK_EQ(cell_flags(7), 0x80);
    CHECK_EQ(cell_flags(0), 0x00);
    CHECK_EQ(cell_flags(2), 0x00);
    CHECK_EQ(cell_flags(6), 0x00);
    CHECK_EQ(cell_flags(8), 0x00);
    CHECK_EQ(cell_marker(4), 0x00);
    CHECK_EQ(cell_marker(3), 0x00);
}

/* CMP dword ptr [EBP+0x14],0x0 / JZ 0x00010ca4 guards the left neighbour.  On
   column 0 the index width*y + x - 1 would wrap onto the last cell of the row
   above -- cell 2 for (0,1) on a 3x3 -- and a zone would leak across the map's
   edge.  The other three neighbours are still marked. */
static void zoc_column_zero_skips_the_left_neighbour(void)
{
    stage_blank(3, 3);
    fdps_move_grid_mark_zone_of_control(0, 1);
    CHECK_EQ(cell_flags(3), 0x40);
    CHECK_EQ(cell_flags(2), 0x00);
    CHECK_EQ(cell_flags(0), 0x80);
    CHECK_EQ(cell_flags(4), 0x80);
    CHECK_EQ(cell_flags(6), 0x80);
}

/* CMP dword ptr [EBP+0x18],0x0 / JZ 0x00010ce8 guards the upper neighbour.  On
   row 0 that index is negative -- (1,0) on a 3x3 gives -2, which is the four
   bytes of the header -- so the guard is what keeps the grid's own dimensions
   from being ORed with 0x80. */
static void zoc_row_zero_skips_the_upper_neighbour(void)
{
    stage_blank(3, 3);
    fdps_move_grid_mark_zone_of_control(1, 0);
    CHECK_EQ((int) *(short *) stage_grid, 3);
    CHECK_EQ((int) *(short *) (stage_grid + 2), 3);
    CHECK_EQ(cell_flags(1), 0x40);
    CHECK_EQ(cell_flags(0), 0x80);
    CHECK_EQ(cell_flags(2), 0x80);
    CHECK_EQ(cell_flags(4), 0x80);
}

/* The right guard is width-1 > tile_x, so the last column is skipped: (2,1) on
   a 3x3 would otherwise mark index 6, the first cell of the row below. */
static void zoc_last_column_skips_the_right_neighbour(void)
{
    stage_blank(3, 3);
    fdps_move_grid_mark_zone_of_control(2, 1);
    CHECK_EQ(cell_flags(5), 0x40);
    CHECK_EQ(cell_flags(6), 0x00);
    CHECK_EQ(cell_flags(4), 0x80);
    CHECK_EQ(cell_flags(2), 0x80);
    CHECK_EQ(cell_flags(8), 0x80);
}

/* The lower guard is height-1 > tile_y: on the last row the index runs past
   the end of the cell array -- 10 for (1,2) on a 3x3, which is four cells past
   the ninth -- and the block only has that room here because the fixture is
   oversized. */
static void zoc_last_row_skips_the_lower_neighbour(void)
{
    stage_blank(3, 3);
    fdps_move_grid_mark_zone_of_control(1, 2);
    CHECK_EQ(cell_flags(7), 0x40);
    CHECK_EQ(cell_flags(10), 0x00);
    CHECK_EQ(cell_flags(6), 0x80);
    CHECK_EQ(cell_flags(4), 0x80);
    CHECK_EQ(cell_flags(8), 0x80);
}

/* OR, not MOV: the low six bits of byte 0 survive, and so does the marker byte
   beside them.  A cell holding 0x2a comes out 0x6a and one holding 0x15 comes
   out 0x95. */
static void zoc_bits_are_ored_into_the_cell(void)
{
    stage_blank(3, 3);
    stage_grid[4 + 4 * 2] = 0x2a;
    stage_grid[4 + 4 * 2 + 1] = 0x07;
    stage_grid[4 + 3 * 2] = 0x15;
    fdps_move_grid_mark_zone_of_control(1, 1);
    CHECK_EQ(cell_flags(4), 0x6a);
    CHECK_EQ(cell_marker(4), 0x07);
    CHECK_EQ(cell_flags(3), 0x95);
}

/* Nothing is cleared between units, which is why the caller can loop over the
   whole unit array: after (0,0) and then (0,1), cell 0 carries its own 0x40
   and the 0x80 the second unit put there, and cell 3 carries the 0x80 from the
   first and the 0x40 from the second.  Only fdps_map_grid_reset takes them
   off again. */
static void zoc_zones_accumulate_across_calls(void)
{
    stage_blank(3, 3);
    fdps_move_grid_mark_zone_of_control(0, 0);
    fdps_move_grid_mark_zone_of_control(0, 1);
    CHECK_EQ(cell_flags(0), 0xc0);
    CHECK_EQ(cell_flags(3), 0xc0);
    CHECK_EQ(cell_flags(1), 0x80);
    CHECK_EQ(cell_flags(4), 0x80);
    CHECK_EQ(cell_flags(6), 0x80);
}

/* There is no guard at all around the centre store at 00010d76.  On a 2x2 grid
   the column 2 does not exist, yet (2,0) still ORs 0x40 into index 2 -- which
   is cell (0,1) -- while the right neighbour is correctly refused by
   width-1 > tile_x and the lower one lands on index 4, past the grid.  The
   four neighbour guards test the neighbour's coordinate, not the centre's, so
   they do not add up to a range check on the argument. */
static void zoc_centre_has_no_bounds_check(void)
{
    stage_blank(2, 2);
    fdps_move_grid_mark_zone_of_control(2, 0);
    CHECK_EQ(cell_flags(2), 0x40);
    CHECK_EQ(cell_flags(1), 0x80);
    CHECK_EQ(cell_flags(4), 0x80);
    CHECK_EQ(cell_flags(3), 0x00);
}

/* MOVSX word ptr [EAX] at 00010c4e and JLE at 00010cef: a width word of 0xffff
   is -1, -1 - 1 is -2, and -2 > 0 is false, so no right neighbour is marked.
   Read the header unsigned and the same word is 65535, the guard passes, and
   0x80 goes into cell 1. */
static void zoc_width_guard_is_signed(void)
{
    stage_blank(-1, 1);
    fdps_move_grid_mark_zone_of_control(0, 0);
    CHECK_EQ((int) *(short *) stage_grid, -1);
    CHECK_EQ(cell_flags(0), 0x40);
    CHECK_EQ(cell_flags(1), 0x00);
}

/* The same for the height word at 00010c59 and the JLE at 00010d36: a height
   of -1 marks no lower neighbour, while an unsigned read would put 0x80 into
   the cell one row down, which here is cell 1. */
static void zoc_height_guard_is_signed(void)
{
    stage_blank(1, -1);
    fdps_move_grid_mark_zone_of_control(0, 0);
    CHECK_EQ((int) *(short *) (stage_grid + 2), -1);
    CHECK_EQ(cell_flags(0), 0x40);
    CHECK_EQ(cell_flags(1), 0x00);
}

/* --- fdps_move_grid_mark_opposing_zones_of_control @ 00010b90 -------------
 *
 * Expected values come from the assembly: the loop bound CMP EAX,dword ptr
 * [0x00060150] / JL at 00010bae, the record step ADD dword ptr [EBP-0x4],0x50
 * at 00010c19, the tile bytes read from +0 and +1 at 00010bc3 and 00010bcd,
 * the retired test AND AL,0x1 on byte +5 at 00010bde, and the pair of side
 * tests CMP dword ptr [EBP+0x14],0x0 against CMP byte ptr [EAX+0x6],0x0 at
 * 00010be9..00010c05.  Which grid cells a marked tile touches comes from
 * fdps_move_grid_mark_zone_of_control's own assembly, asserted above.
 *
 * Both the unit array and the grid are staged here.  The function takes its
 * entire input from data_fdps_map_unit_array_ptr, data_fdps_map_unit_count and
 * the grid pointer, so pointing those at local blocks is the only way to reach
 * the loop; nothing below asserts what any of the three holds on its own,
 * which is ticket 23's.
 */

#define STAGE_UNITS 4

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Blank every staged record and publish the array.  Zeroing matters: side 0
   and flags 0 is the state the per-case setters below deviate from. */
static void units_reset(int count)
{
    unsigned char *raw;
    int i;

    raw = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        raw[i] = 0x00;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = count;
}

static void put_unit(int slot, int x, int y, int side, int flags)
{
    stage_units[slot].pos_x = (unsigned char) x;
    stage_units[slot].pos_y = (unsigned char) y;
    stage_units[slot].side = (unsigned char) side;
    stage_units[slot].flags = (unsigned char) flags;
}

/* ADD dword ptr [EBP-0x4],0x50 is the whole of the array walk's arithmetic, so
   the record has to come out 0x50 bytes for the pointer step in the C to land
   on the next unit; the four byte offsets the body reads are +0, +1, +5
   and +6. */
static void zones_unit_record_stride_is_0x50(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
}

/* side_select 0 takes the JNZ at 00010bed nowhere and falls into CMP byte ptr
   [EAX+0x6],0x0 / JNZ to the call: the units marked are the ones whose side
   byte is non-zero.  On a 4x4 grid the unit at (2,2) is cell 10 and its four
   neighbours are 6, 9, 11 and 14; the side-0 unit at (0,0) leaves cell 0 and
   its neighbours 1 and 4 untouched. */
static void zones_select_zero_marks_nonzero_sides(void)
{
    stage_blank(4, 4);
    units_reset(2);
    put_unit(0, 0, 0, 0, 0);
    put_unit(1, 2, 2, 1, 0);
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(10), 0x40);
    CHECK_EQ(cell_flags(6), 0x80);
    CHECK_EQ(cell_flags(9), 0x80);
    CHECK_EQ(cell_flags(11), 0x80);
    CHECK_EQ(cell_flags(14), 0x80);
    CHECK_EQ(cell_flags(0), 0x00);
    CHECK_EQ(cell_flags(1), 0x00);
    CHECK_EQ(cell_flags(4), 0x00);
}

/* The mirror: a non-zero side_select takes the JNZ at 00010bed to 00010bf8 and
   marks only the records whose side byte is 0.  Same two units, opposite
   outcome. */
static void zones_select_one_marks_side_zero(void)
{
    stage_blank(4, 4);
    units_reset(2);
    put_unit(0, 0, 0, 0, 0);
    put_unit(1, 2, 2, 1, 0);
    fdps_move_grid_mark_opposing_zones_of_control(1);
    CHECK_EQ(cell_flags(0), 0x40);
    CHECK_EQ(cell_flags(1), 0x80);
    CHECK_EQ(cell_flags(4), 0x80);
    CHECK_EQ(cell_flags(10), 0x00);
    CHECK_EQ(cell_flags(6), 0x00);
}

/* Both tests are against zero, so only the truth value of each side counts.
   With side_select 2 -- the player side's own number, which no caller passes
   today but which the code accepts -- the units marked are still exactly the
   side-0 ones: the neutral unit at (2,0) is cell 2 and the player unit at
   (0,2) is cell 8, and neither is touched.  Writing the test as
   unit->side != side_select would put 0x40 into both of them. */
static void zones_side_is_a_truth_value_not_a_number(void)
{
    stage_blank(4, 4);
    units_reset(3);
    put_unit(0, 0, 0, 0, 0);
    put_unit(1, 2, 0, 1, 0);
    put_unit(2, 0, 2, 2, 0);
    fdps_move_grid_mark_opposing_zones_of_control(2);
    CHECK_EQ(cell_flags(0), 0x40);
    CHECK_EQ(cell_flags(2), 0x00);
    CHECK_EQ(cell_flags(8), 0x00);
    CHECK_EQ(cell_flags(3), 0x00);
    CHECK_EQ(cell_flags(12), 0x00);
}

/* And the same three units with side_select 0: the neutral and the player unit
   are both marked and the enemy one is not, which is what makes the two-call
   sweep with 0 and then 1 cover every unit on the map. */
static void zones_select_zero_marks_neutral_and_player(void)
{
    stage_blank(4, 4);
    units_reset(3);
    put_unit(0, 0, 0, 0, 0);
    put_unit(1, 2, 0, 1, 0);
    put_unit(2, 0, 2, 2, 0);
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(2), 0x40);
    CHECK_EQ(cell_flags(8), 0x40);
    CHECK_EQ(cell_flags(0), 0x00);
}

/* AND AL,0x1 at 00010bde tests bit 0 alone.  The retired unit at (0,0) is
   passed over even though its side qualifies, while the unit at (2,2) carrying
   every other bit of the flags byte is marked normally -- a unit that has
   already acted this turn still projects its zone of control. */
static void zones_retired_bit_is_bit_zero_only(void)
{
    stage_blank(4, 4);
    units_reset(2);
    put_unit(0, 0, 0, 1, 0x01);
    put_unit(1, 2, 2, 1, 0xfe);
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(0), 0x00);
    CHECK_EQ(cell_flags(1), 0x00);
    CHECK_EQ(cell_flags(10), 0x40);
}

/* The bound is data_fdps_map_unit_count and the step is one 0x50-byte record,
   so slot 1 is reached only when the count says two.  The first call also pins
   that the walk stops rather than running on into whatever follows. */
static void zones_walk_stops_at_the_unit_count(void)
{
    stage_blank(4, 4);
    units_reset(1);
    put_unit(0, 0, 0, 1, 0);
    put_unit(1, 2, 2, 1, 0);
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(0), 0x40);
    CHECK_EQ(cell_flags(10), 0x00);

    stage_blank(4, 4);
    data_fdps_map_unit_count = 2;
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(0), 0x40);
    CHECK_EQ(cell_flags(10), 0x40);
}

/* CMP EAX,dword ptr [0x00060150] / JL is the signed compare: a count of -1
   walks nothing.  Read unsigned, 0 < 0xffffffff holds and the walk runs off
   the end of the array. */
static void zones_unit_count_is_signed(void)
{
    stage_blank(4, 4);
    units_reset(0);
    put_unit(0, 0, 0, 1, 0);
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(0), 0x00);

    data_fdps_map_unit_count = -1;
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(0), 0x00);
}

/* MOV EAX,[0x00069cd8] is inside the function, so the base is taken from the
   global on every call rather than from anything cached across calls: pointing
   it one record further along makes the same one-unit walk read slot 1. */
static void zones_base_is_reread_from_the_global(void)
{
    stage_blank(4, 4);
    units_reset(1);
    put_unit(0, 0, 0, 1, 0);
    put_unit(1, 2, 2, 1, 0);
    data_fdps_map_unit_array_ptr = (unsigned char *) &stage_units[1];
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(10), 0x40);
    CHECK_EQ(cell_flags(0), 0x00);
}

/* The tile bytes are zero-extended (XOR EAX,EAX / MOV AL) and handed on as
   they are: a unit sitting outside the grid is not filtered here, it is passed
   to the mark, which has no bounds check on the centre tile either.  On a 4x4
   grid a unit at (5,0) puts its 0x40 into index 5, which is tile (1,1), its
   left mark into index 4 and its lower mark into index 9; the right neighbour
   is the one thing refused, by width-1 > tile_x, which is 3 > 5.  That is the
   original's behaviour rather than a desirable one, and it is asserted so that
   a guard added here would be noticed. */
static void zones_tile_bytes_are_passed_through_unchecked(void)
{
    stage_blank(4, 4);
    units_reset(1);
    put_unit(0, 5, 0, 1, 0);
    fdps_move_grid_mark_opposing_zones_of_control(0);
    CHECK_EQ(cell_flags(5), 0x40);
    CHECK_EQ(cell_flags(4), 0x80);
    CHECK_EQ(cell_flags(6), 0x00);
    CHECK_EQ(cell_flags(9), 0x80);
}

/* 00010da0.  The address is base + 4 + 2 * (width * tile_y + tile_x): IMUL
   EAX,dword ptr [EBP+0x18] takes tile_y, ADD EAX,dword ptr [EBP+0x14] takes
   tile_x, ADD EAX,EAX is the two-byte stride and ADD EAX,0x4 steps over the
   header.  On a 4x4 grid (2,1) is therefore cell 6 and its neighbours in the
   array are untouched.  OR byte ptr [EAX],0x80 is a byte operation on byte 0,
   so the marker byte of that same cell stays where it was. */
static void stop_flag_addresses_the_named_cell(void)
{
    stage_blank(4, 4);
    fdps_move_grid_set_stop_flag(2, 1);
    CHECK_EQ(cell_flags(6), 0x80);
    CHECK_EQ(cell_marker(6), 0x00);
    CHECK_EQ(cell_flags(5), 0x00);
    CHECK_EQ(cell_flags(7), 0x00);
    CHECK_EQ((int) *(short *) stage_grid, 4);
}

/* OR, not MOV.  A cell already carrying 0x40 (a unit stands here) and low bits
   keeps all of them, and byte 1 is not part of the operand: a store would blank
   the 0x40 the zone-of-control mark had just put there. */
static void stop_flag_is_ored_into_the_cell(void)
{
    stage_blank(3, 3);
    stage_grid[4 + 4 * 2] = 0x4f;
    stage_grid[4 + 4 * 2 + 1] = 0x07;
    fdps_move_grid_set_stop_flag(1, 1);
    CHECK_EQ(cell_flags(4), 0xcf);
    CHECK_EQ(cell_marker(4), 0x07);
}

/* MOVSX EAX,word ptr [EAX] reads the width word signed.  With a width of -1
   the index for (0,1) is -1, which is base + 2 -- the height word of the
   header -- and that is where the 0x80 goes.  Read the word unsigned and the
   index is 65535 instead, 128KB past the end of the block, and this fixture
   would be left untouched. */
static void stop_flag_width_word_is_signed(void)
{
    stage_blank(-1, 0);
    fdps_move_grid_set_stop_flag(0, 1);
    CHECK_EQ((int) stage_grid[2], 0x80);
    CHECK_EQ((int) stage_grid[3], 0x00);
    CHECK_EQ((int) stage_grid[0], 0xff);
    CHECK_EQ(cell_flags(0), 0x00);
}

/* The width is read out of the header inside the body, so it is whatever the
   header says at the moment of the call: the same (1,1) lands on cell 5 with a
   width of 4 and on cell 4 with a width of 3. */
static void stop_flag_width_comes_from_the_header(void)
{
    stage_blank(4, 4);
    fdps_move_grid_set_stop_flag(1, 1);
    CHECK_EQ(cell_flags(5), 0x80);
    CHECK_EQ(cell_flags(4), 0x00);

    *(short *) stage_grid = (short) 3;
    fdps_move_grid_set_stop_flag(1, 1);
    CHECK_EQ(cell_flags(4), 0x80);
}

/* Neither coordinate is compared against the header -- there is no CMP at all
   between the prologue and the OR.  On a 4x4 grid a tile_x of 5 marks cell 5,
   which is tile (1,1), and a tile_y of 4 marks cell 16, one row past the last.
   That is the original's behaviour rather than a desirable one; it is asserted
   so that a guard added here would be noticed. */
static void stop_flag_has_no_bounds_check(void)
{
    stage_blank(4, 4);
    fdps_move_grid_set_stop_flag(5, 0);
    CHECK_EQ(cell_flags(5), 0x80);

    stage_blank(4, 4);
    fdps_move_grid_set_stop_flag(0, 4);
    CHECK_EQ(cell_flags(16), 0x80);
}

/* --- fdps_move_grid_block_occupied_tiles @ 000118f0 -----------------------
 *
 * Expected values come from the assembly: the loop bound CMP EAX,dword ptr
 * [0x00060150] / JL at 00011919, the exclude test CMP EAX,dword ptr [EBP+0x14]
 * / JZ at 00011931, the address form IMUL EDX,EAX / ADD EAX,EDX / ADD EAX,EAX
 * / ADD EAX,base / ADD EAX,0x5 at 00011944..00011963, the retired test AND
 * AL,0x1 on byte +5 at 0001196f, the pair of side tests CMP dword ptr
 * [EBP+0x18],0x0 against CMP byte ptr [EAX+0x6],0x0 at 0001197a..00011998, the
 * store MOV byte ptr [EAX],0xff at 0001198c and 0001199d, and the record step
 * ADD dword ptr [EBP-0x4],0x50 at 000119a0.  None of them is read off the
 * emitted C.
 *
 * stage() rather than stage_blank() is the fixture here: it leaves every cell
 * flags 0xff / marker 0x00, and this function writes only 0xff into the marker
 * byte, so one cell says on its own both whether it was blocked and whether
 * anything touched the flags byte beside it.
 */

/* The record is skipped on the loop index alone -- the compare is against the
   loop counter, not against any field of the record -- and the record pointer
   is stepped on the skip path as well, so slot 2 is still read at the right
   offset after slot 1 was passed over.  All three units are on the same side
   and none is retired, so nothing but the index separates them. */
static void block_excludes_the_named_unit_index(void)
{
    stage(4, 4);
    units_reset(3);
    put_unit(0, 0, 0, 1, 0);
    put_unit(1, 1, 0, 1, 0);
    put_unit(2, 2, 0, 1, 0);
    fdps_move_grid_block_occupied_tiles(1, 1);
    CHECK_EQ(cell_marker(0), 0xff);
    CHECK_EQ(cell_marker(1), 0x00);
    CHECK_EQ(cell_marker(2), 0xff);
}

/* side_select 0 leaves the JNZ at 0001197e alone and falls into CMP byte ptr
   [EAX+0x6],0x0 / JNZ past the store: the tiles blocked are the ones whose
   side byte is 0, the enemy side.  The NPC unit at (1,0) and the player unit
   at (2,0) keep their tiles in the range. */
static void block_select_zero_blocks_side_zero_units(void)
{
    stage(4, 4);
    units_reset(3);
    put_unit(0, 0, 0, 0, 0);
    put_unit(1, 1, 0, 1, 0);
    put_unit(2, 2, 0, 2, 0);
    fdps_move_grid_block_occupied_tiles(-1, 0);
    CHECK_EQ(cell_marker(0), 0xff);
    CHECK_EQ(cell_marker(1), 0x00);
    CHECK_EQ(cell_marker(2), 0x00);
}

/* The mirror, and the trap the rebuild note names: a non-zero side_select
   takes the JNZ at 0001197e to 00011991 and blocks every record whose side
   byte is non-zero.  fdps_battle_unit_turn passes the literal 1 for a player
   unit, whose own side byte is 2, so the player unit at (2,0) is blocked by
   this call as well as the NPC unit at (1,0) -- writing the test as
   unit->side == side_select would leave cell 2 reachable and let a player unit
   end its move standing on a comrade. */
static void block_select_nonzero_blocks_npc_and_player(void)
{
    stage(4, 4);
    units_reset(3);
    put_unit(0, 0, 0, 0, 0);
    put_unit(1, 1, 0, 1, 0);
    put_unit(2, 2, 0, 2, 0);
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ(cell_marker(0), 0x00);
    CHECK_EQ(cell_marker(1), 0xff);
    CHECK_EQ(cell_marker(2), 0xff);
}

/* Both compares are against zero, so only the truth value of side_select
   counts: 2 -- the player side's own number -- selects exactly the same
   records as 1 did above, and not the player unit alone. */
static void block_side_select_is_a_truth_value(void)
{
    stage(4, 4);
    units_reset(3);
    put_unit(0, 0, 0, 0, 0);
    put_unit(1, 1, 0, 1, 0);
    put_unit(2, 2, 0, 2, 0);
    fdps_move_grid_block_occupied_tiles(-1, 2);
    CHECK_EQ(cell_marker(0), 0x00);
    CHECK_EQ(cell_marker(1), 0xff);
    CHECK_EQ(cell_marker(2), 0xff);
}

/* MOV byte ptr [EAX],0xff writes one byte, and the address is base + 5 +
   2 * index -- byte 1 of the cell.  Byte 0 keeps its zone-of-control bits, so
   a tile blocked here is still marked as occupied for anything that reads
   them, and a cell the flood fill had relaxed to a step cost of 3 goes back to
   the unreachable sentinel rather than being left alone.  The neighbouring
   cell is untouched in both bytes. */
static void block_writes_only_the_marker_byte(void)
{
    stage(4, 4);
    stage_grid[4 + 5 * 2] = 0x40;
    stage_grid[4 + 5 * 2 + 1] = 0x03;
    units_reset(1);
    put_unit(0, 1, 1, 1, 0);
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ(cell_marker(5), 0xff);
    CHECK_EQ(cell_flags(5), 0x40);
    CHECK_EQ(cell_marker(6), 0x00);
    CHECK_EQ(cell_flags(6), 0xff);
}

/* AND AL,0x1 tests bit 0 alone.  The retired unit at (0,0) keeps its tile in
   the range even though its side qualifies, while the unit at (1,0) carrying
   every other bit of the flags byte blocks its tile normally. */
static void block_retired_bit_is_bit_zero_only(void)
{
    stage(4, 4);
    units_reset(2);
    put_unit(0, 0, 0, 1, 0x01);
    put_unit(1, 1, 0, 1, 0xfe);
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ(cell_marker(0), 0x00);
    CHECK_EQ(cell_marker(1), 0xff);
}

/* The bound is data_fdps_map_unit_count and the compare is JL, the signed one:
   a count of one reaches slot 0 and stops, and a count of -1 walks nothing at
   all.  Read the count unsigned and -1 is four billion and the walk runs off
   the end of the array. */
static void block_walk_stops_at_the_signed_unit_count(void)
{
    stage(4, 4);
    units_reset(1);
    put_unit(0, 0, 0, 1, 0);
    put_unit(1, 1, 0, 1, 0);
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ(cell_marker(0), 0xff);
    CHECK_EQ(cell_marker(1), 0x00);

    stage(4, 4);
    data_fdps_map_unit_count = -1;
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ(cell_marker(0), 0x00);
    CHECK_EQ(cell_marker(1), 0x00);
}

/* The index is width * pos_y + pos_x with the width taken from the header, so
   the same unit at (1,1) blocks cell 5 on a width of 4 and cell 4 on a width
   of 3. */
static void block_index_uses_the_header_width(void)
{
    stage(4, 4);
    units_reset(1);
    put_unit(0, 1, 1, 1, 0);
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ(cell_marker(5), 0xff);
    CHECK_EQ(cell_marker(4), 0x00);

    stage(3, 4);
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ(cell_marker(4), 0xff);
    CHECK_EQ(cell_marker(5), 0x00);
}

/* MOVSX EAX,word ptr [EAX] at 00011901 reads the width word signed.  With a
   width of -1 the index for (0,1) is -1, the cell address is base + 2 -- the
   height word of the header -- and the 0xff lands in its high byte.  Read the
   word unsigned and the index would be 65535 instead, 128KB past the end of
   the block, and this fixture would be left untouched. */
static void block_width_word_is_signed(void)
{
    stage(-1, 4);
    units_reset(1);
    put_unit(0, 0, 1, 1, 0);
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ((int) *(short *) stage_grid, -1);
    CHECK_EQ((int) stage_grid[2], 0x04);
    CHECK_EQ((int) stage_grid[3], 0xff);
    CHECK_EQ(cell_marker(0), 0x00);
}

/* Neither tile byte is compared against the header -- there is no CMP between
   the two zero-extending loads and the store -- so a unit sitting outside the
   grid still blocks whatever cell the arithmetic reaches.  On a 4x4 grid a
   unit at (5,0) blocks index 5, which is tile (1,1), and one at (0,4) blocks
   index 16, one row past the last.  That is the original's behaviour rather
   than a desirable one; it is asserted so that a guard added here would be
   noticed. */
static void block_coordinates_are_not_range_checked(void)
{
    stage(4, 4);
    units_reset(2);
    put_unit(0, 5, 0, 1, 0);
    put_unit(1, 0, 4, 1, 0);
    fdps_move_grid_block_occupied_tiles(-1, 1);
    CHECK_EQ(cell_marker(5), 0xff);
    CHECK_EQ(cell_marker(16), 0xff);
    CHECK_EQ(cell_marker(0), 0x00);
    CHECK_EQ(cell_marker(4), 0x00);
}

/* --- fdps_map_grid_collect_marked_tiles @ 00011da0 ------------------------
 *
 * Expected values come from the assembly: the cursor set-up MOV EAX,
 * [0x00060144] / ADD EAX,0x5 at 00011db3, the two MOVSX word loads at 00011dc3
 * and 00011dce, the two loop tests CMP EAX,[EBP-0xc] / JL and CMP EAX,
 * [EBP-0x10] / JL at 00011de2 and 00011dfb, the marked test MOV AL,byte ptr
 * [EAX] / AND EAX,0xff / CMP EAX,0xff / JZ at 00011e0a..00011e16, the two byte
 * stores MOV byte ptr [EDX],AL and MOV byte ptr [EDX+0x1],AL at 00011e1e and
 * 00011e26, the output step ADD dword ptr [EBP+0x14],0x2 at 00011e29, the
 * cursor step ADD dword ptr [EBP-0x8],0x2 at 00011e33, and the return of the
 * counter at [EBP-0x14].  None of them is read off the emitted C.
 *
 * The grid is staged here for the same reason as above: the function's whole
 * input is data_fdps_battle_move_grid_ptr, so pointing it at a local block is
 * the only way to reach the loops.  Neither the existing stage() nor
 * stage_blank() will do -- both leave every marker byte 0x00, which is every
 * cell marked -- so this section stages the grid the way fdps_map_grid_reset
 * leaves it and marks individual cells by hand.
 */

/* One pair per marked cell, and room for more than a 4x4 grid can produce, so
   an over-long walk has somewhere to be caught writing.  0xee is a value
   neither a coordinate nor a count in these fixtures can be, so a byte still
   holding it was not written. */
static unsigned char collect_out[64];

static void collect_out_reset(void)
{
    int i;

    for (i = 0; i < (int) sizeof collect_out; i++) {
        collect_out[i] = 0xee;
    }
}

/* The grid as fdps_map_grid_reset leaves it: flags cleared, every marker byte
   holding the 0xff unreachable sentinel, so nothing is marked until a test
   says so. */
static void stage_reset_grid(int width, int height)
{
    int i;

    for (i = 0; i < STAGE_CELLS; i++) {
        stage_grid[4 + i * 2] = 0x00;
        stage_grid[4 + i * 2 + 1] = 0xff;
    }
    *(short *) stage_grid = (short) width;
    *(short *) (stage_grid + 2) = (short) height;
    data_fdps_battle_move_grid_ptr = stage_grid;
    collect_out_reset();
}

/* Relax one cell's marker away from the sentinel, the way the flood fill at
   00010de0 leaves a reachable tile's step cost behind. */
static void mark_cell(int index, int marker)
{
    stage_grid[4 + index * 2 + 1] = (unsigned char) marker;
}

static int out_byte(int index)
{
    return (int) collect_out[index];
}

/* The whole contract in one case: the count comes back, one pair is written
   per marked cell, x goes to byte 0 and y to byte 1, and the pairs land
   consecutively in the order the cells are visited.  On a 4x4 grid cell 1 is
   tile (1,0), cell 6 is (2,1) and cell 11 is (3,2).  The seventh output byte
   is still 0xee: the output pointer advanced twice per marked cell and no
   further. */
static void collect_writes_one_xy_pair_per_marked_cell(void)
{
    int count;

    stage_reset_grid(4, 4);
    mark_cell(1, 0x00);
    mark_cell(6, 0x03);
    mark_cell(11, 0xfe);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 3);
    CHECK_EQ(out_byte(0), 1);
    CHECK_EQ(out_byte(1), 0);
    CHECK_EQ(out_byte(2), 2);
    CHECK_EQ(out_byte(3), 1);
    CHECK_EQ(out_byte(4), 3);
    CHECK_EQ(out_byte(5), 2);
    CHECK_EQ(out_byte(6), 0xee);
}

/* CMP EAX,0xff / JZ skips only on the exact sentinel.  0xfe -- a step cost the
   flood fill could genuinely leave -- is marked, 0x00 as written by the
   targeting mask at 00011e50 is marked, and 0xff is the only value that is
   not.  A test written as `marker < 0xff` would agree here; one written as
   `marker != 0` or `marker > 0` would drop the mask's tiles. */
static void collect_sentinel_is_ff_and_nothing_else(void)
{
    int count;

    stage_reset_grid(3, 1);
    mark_cell(0, 0x00);
    mark_cell(1, 0xfe);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 2);
    CHECK_EQ(out_byte(0), 0);
    CHECK_EQ(out_byte(1), 0);
    CHECK_EQ(out_byte(2), 1);
    CHECK_EQ(out_byte(3), 0);
    CHECK_EQ(out_byte(4), 0xee);
}

/* The cursor starts at grid + 5, byte 1 of the first cell, so byte 0 is never
   read: a cell whose zone-of-control byte is 0xff and whose marker is 0 is
   collected, and one whose flags are clear but whose marker is the sentinel is
   not.  Read the wrong byte of the pair and both answers invert. */
static void collect_reads_the_marker_not_the_flags(void)
{
    int count;

    stage_reset_grid(2, 1);
    stage_grid[4 + 0 * 2] = 0xff;
    mark_cell(0, 0x00);
    stage_grid[4 + 1 * 2] = 0x00;
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 1);
    CHECK_EQ(out_byte(0), 0);
    CHECK_EQ(out_byte(1), 0);
    CHECK_EQ(out_byte(2), 0xee);
}

/* The cursor is stepped by two on every cell and never recomputed from x and
   y, so the pairs have to come out in row-major order with x running fastest.
   Every cell of a 3x2 grid is marked, which pins all six pairs and the order
   they arrive in: an inner loop over y instead of x would give the same count
   and a transposed list. */
static void collect_visits_cells_in_row_major_order(void)
{
    int count;

    stage_reset_grid(3, 2);
    mark_cell(0, 0x00);
    mark_cell(1, 0x00);
    mark_cell(2, 0x00);
    mark_cell(3, 0x00);
    mark_cell(4, 0x00);
    mark_cell(5, 0x00);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 6);
    CHECK_EQ(out_byte(0), 0);
    CHECK_EQ(out_byte(1), 0);
    CHECK_EQ(out_byte(2), 1);
    CHECK_EQ(out_byte(3), 0);
    CHECK_EQ(out_byte(4), 2);
    CHECK_EQ(out_byte(5), 0);
    CHECK_EQ(out_byte(6), 0);
    CHECK_EQ(out_byte(7), 1);
    CHECK_EQ(out_byte(8), 1);
    CHECK_EQ(out_byte(9), 1);
    CHECK_EQ(out_byte(10), 2);
    CHECK_EQ(out_byte(11), 1);
    CHECK_EQ(out_byte(12), 0xee);
}

/* A grid straight out of fdps_map_grid_reset has nothing in it: the count is
   0 and not one byte of the caller's buffer is touched.  This is the answer
   the callers test before allocating anything of their own. */
static void collect_returns_zero_when_nothing_is_marked(void)
{
    int count;

    stage_reset_grid(4, 4);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 0);
    CHECK_EQ(out_byte(0), 0xee);
    CHECK_EQ(out_byte(1), 0xee);
}

/* The two bounds are the header words and the walk stops at width*height
   cells: a cell marked past that bound is never reached, however marked it is.
   Cell 5 of a 2x2 grid is one row and a half past the last cell the walk
   visits, and cell 0 inside the bound is collected in the same call. */
static void collect_bound_comes_from_the_header(void)
{
    int count;

    stage_reset_grid(2, 2);
    mark_cell(0, 0x00);
    mark_cell(5, 0x00);
    mark_cell(9, 0x00);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 1);
    CHECK_EQ(out_byte(0), 0);
    CHECK_EQ(out_byte(1), 0);
    CHECK_EQ(out_byte(2), 0xee);
}

/* Both header words arrive through MOVSX and both loop tests are JL.  A height
   of -1 fails the outer test at once, so the cursor is never even read; a
   width of -1 fails the inner test on every row.  Either way the count is 0
   and the buffer is untouched.  Read either word unsigned and -1 is 65535:
   the walk leaves this fixture and runs 65535 cells into whatever follows it.
   Every cell in reach is marked here, so an unsigned read could not come back
   with 0 by accident. */
static void collect_header_words_are_signed(void)
{
    int count;

    stage_reset_grid(2, -1);
    mark_cell(0, 0x00);
    mark_cell(1, 0x00);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 0);
    CHECK_EQ(out_byte(0), 0xee);
    CHECK_EQ((int) *(short *) (stage_grid + 2), -1);

    stage_reset_grid(-1, 2);
    mark_cell(0, 0x00);
    mark_cell(1, 0x00);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 0);
    CHECK_EQ(out_byte(0), 0xee);
    CHECK_EQ((int) *(short *) stage_grid, -1);
}

/* A zero in either header word is the same story without the sign: the loop
   whose bound it is runs no iterations. */
static void collect_zero_dimension_collects_nothing(void)
{
    int count;

    stage_reset_grid(0, 4);
    mark_cell(0, 0x00);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 0);
    CHECK_EQ(out_byte(0), 0xee);

    stage_reset_grid(4, 0);
    mark_cell(0, 0x00);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 0);
    CHECK_EQ(out_byte(0), 0xee);
}

/* The grid is read and never written: there is no store to the cursor anywhere
   in the body.  Callers depend on that -- the range stays on the grid until
   fdps_map_grid_reset wipes it -- so a cell keeps both its flags byte and the
   exact marker value the fill left, sentinel cells included. */
static void collect_does_not_modify_the_grid(void)
{
    int count;

    stage_reset_grid(2, 2);
    stage_grid[4 + 1 * 2] = 0xc0;
    mark_cell(1, 0x03);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 1);
    CHECK_EQ(cell_flags(1), 0xc0);
    CHECK_EQ(cell_marker(1), 0x03);
    CHECK_EQ(cell_marker(0), 0xff);
    CHECK_EQ(cell_marker(3), 0xff);
    CHECK_EQ((int) *(short *) stage_grid, 2);
    CHECK_EQ((int) *(short *) (stage_grid + 2), 2);
}

/* The last cell of the grid is reachable -- the walk covers width*height cells,
   not one fewer -- and a single marked cell at the far end still produces
   exactly one pair at the front of the buffer.  Cell 15 of a 4x4 grid is tile
   (3,3). */
static void collect_reaches_the_last_cell(void)
{
    int count;

    stage_reset_grid(4, 4);
    mark_cell(15, 0x00);
    count = fdps_map_grid_collect_marked_tiles(collect_out);
    CHECK_EQ(count, 1);
    CHECK_EQ(out_byte(0), 3);
    CHECK_EQ(out_byte(1), 3);
    CHECK_EQ(out_byte(2), 0xee);
}

/* ------------------------------------------------------------------ *
 * 00010de0 fdps_move_grid_flood_fill_range
 *
 * The fill needs four blocks live, because it calls fdps_map_load_tile_info
 * (src/maptile.c, already emitted) for every neighbour it considers and that
 * function reads the terrain layer, the attribute table, the grid and the
 * event-code layer.  All four are staged here rather than read from a game
 * file: the function takes its whole input from those globals plus its four
 * arguments, so pointing them at local blocks is the only way to reach the
 * body.  Nothing below asserts what any global holds on its own; ticket 23
 * owns that.
 *
 * The fixture gives cell i the tile id i and attribute row i, so a per-cell
 * terrain type -- and through it a per-cell movement cost -- is one write.
 * The three widths are deliberately all the same here, unlike tests/maptile.c
 * which sets them apart on purpose: this function's own arithmetic uses the
 * grid header for everything, and a fixture whose layers disagree would make
 * fdps_map_load_tile_info fetch a different tile than the fill was asking
 * about and turn every expected cost below into a coincidence.
 *
 * Expected values come from the assembly at 00010de0 -- MOVSX on the two
 * header words, CMP EDX,EAX / JGE on the two edge guards, CMP EAX,[EBP+0x20] /
 * JG, TEST [EBP-0xc],0x40, CMP EAX,[EBP-8] / JL on the three acceptance tests,
 * MOV EAX,[EBP+0x20] under TEST 0x80, IMUL ...,0x190 on every frontier index,
 * and the four direction blocks in their listed order -- and by walking that
 * algorithm over the fixture by hand.  None of them is read off the emitted C.
 * ------------------------------------------------------------------ */

#define FILL_CELLS 32
#define FILL_ATTR_ROWS 32
#define FILL_TILES_AT 0x0b
#define FILL_ATTR_AT 0x11
#define FILL_EVENT_AT 0x10

static unsigned char fill_tile_map[FILL_TILES_AT + FILL_CELLS * 2];
static unsigned char fill_attr[FILL_ATTR_AT + FILL_ATTR_ROWS * 4];
static unsigned char fill_grid[4 + FILL_CELLS * 2];
static unsigned char fill_event[FILL_EVENT_AT + FILL_CELLS];
static struct fdps_class_record fill_class;

/* Build all four blocks and hang the four globals off them.

   Every cell inside width*height comes in as fdps_map_grid_reset leaves it --
   flags 0x00, marker 0xff -- and every cell past the end gets marker 0xdd
   instead, so "the fill wrote here" and "the fill never reached here" are
   distinguishable from "this is off the end of the map".  A negative or
   oversized product yields no in-grid cells at all, which is what the two
   signedness cases want.

   Both auxiliary headers are filled with 0xaa before their width goes in at
   +7, so a read from any other offset lands on 0xaaaa rather than
   coincidentally agreeing.  Every attribute row starts at terrain type 0 and
   every class movement cost at 1, so a case that says nothing about terrain
   gets a uniform cost of one per step. */
static void stage_fill(int width, int height)
{
    int i;
    int in_grid_cells;

    in_grid_cells = width * height;
    if (in_grid_cells < 0) {
        in_grid_cells = 0;
    }
    if (in_grid_cells > FILL_CELLS) {
        in_grid_cells = FILL_CELLS;
    }

    for (i = 0; i < FILL_TILES_AT; i++) {
        fill_tile_map[i] = 0xaa;
    }
    *(short *) (fill_tile_map + 7) = (short) width;
    for (i = 0; i < FILL_CELLS; i++) {
        *(short *) (fill_tile_map + FILL_TILES_AT + i * 2) = (short) i;
    }

    for (i = 0; i < FILL_ATTR_AT; i++) {
        fill_attr[i] = 0xaa;
    }
    for (i = 0; i < FILL_ATTR_ROWS; i++) {
        fill_attr[FILL_ATTR_AT + i * 4] = 0x00;
        fill_attr[FILL_ATTR_AT + i * 4 + 1] = 0x00;
        fill_attr[FILL_ATTR_AT + i * 4 + 2] = 0x00;
        fill_attr[FILL_ATTR_AT + i * 4 + 3] = 0x00;
    }

    *(short *) fill_grid = (short) width;
    *(short *) (fill_grid + 2) = (short) height;
    for (i = 0; i < FILL_CELLS; i++) {
        fill_grid[4 + i * 2] = 0x00;
        if (i < in_grid_cells) {
            fill_grid[4 + i * 2 + 1] = 0xff;
        } else {
            fill_grid[4 + i * 2 + 1] = 0xdd;
        }
    }

    for (i = 0; i < FILL_EVENT_AT; i++) {
        fill_event[i] = 0xaa;
    }
    *(short *) (fill_event + 7) = (short) width;
    for (i = 0; i < FILL_CELLS; i++) {
        fill_event[FILL_EVENT_AT + i] = 0x00;
    }

    for (i = 0; i < 8; i++) {
        fill_class.move_cost[i] = 1;
    }
    fill_class.critical = 0;
    fill_class.magic_resist_complement = 0;

    data_fdps_scene_layer_tile_map_ptrs[0] = fill_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = fill_attr;
    data_fdps_battle_move_grid_ptr = fill_grid;
    data_fdps_map_cell_event_code_layer_ptr = fill_event;
}

static int fill_marker(int index)
{
    return (int) fill_grid[4 + index * 2 + 1];
}

static int fill_cell_flags(int index)
{
    return (int) fill_grid[4 + index * 2];
}

static void set_tile_terrain(int tile_id, int terrain_type)
{
    fill_attr[FILL_ATTR_AT + tile_id * 4 + 2] = (unsigned char) terrain_type;
}

static void set_fill_cell_flags(int index, int flags)
{
    fill_grid[4 + index * 2] = (unsigned char) flags;
}

/* ADD EAX,dword ptr [EBP+0x14] at 00010fc1 adds the terrain type straight to
   the class record pointer with no displacement, so the eight movement costs
   have to be the first eight bytes of the record for the cost byte to land
   where the original reads it. */
static void fill_move_costs_are_the_first_eight_bytes(void)
{
    CHECK_EQ((int) offsetof(struct fdps_class_record, move_cost), 0);
    CHECK_EQ((int) sizeof(fill_class.move_cost), 8);
}

/* MOV byte ptr [EAX + 0x1],0x0 at 00010e3c, with EAX formed as cells +
   start_x * 2 + start_y * (width * 2): the start tile's accumulated cost is
   zero before anything is expanded, and the two frontier arrays are seeded at
   slot 0 of buffer 0 with the start coordinates.  On a 1x1 grid all four edge
   guards fail at once, so the fill stores that one byte and stops. */
static void fill_start_tile_gets_cost_zero(void)
{
    stage_fill(1, 1);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 9);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ((int) data_fdps_battle_move_frontier_x[0], 0);
    CHECK_EQ((int) data_fdps_battle_move_frontier_y[0], 0);
    CHECK_EQ(fill_marker(1), 0xdd);
}

/* The start cell is addressed through the grid header's width, so on a 4x2
   grid tile (2,1) is cell 1 * 4 + 2 = 6 and no other cell is written.  With an
   allowance of 0 every candidate costs 1 and is refused by CMP EAX,[EBP+0x20]
   / JG, which leaves the first wave empty and ends the fill after it. */
static void fill_seeds_buffer_zero_from_the_start_coordinates(void)
{
    stage_fill(4, 2);
    fdps_move_grid_flood_fill_range(&fill_class, 2, 1, 0);
    CHECK_EQ(fill_marker(6), 0);
    CHECK_EQ((int) data_fdps_battle_move_frontier_x[0], 2);
    CHECK_EQ((int) data_fdps_battle_move_frontier_y[0], 1);
    CHECK_EQ(fill_marker(2), 0xff);
    CHECK_EQ(fill_marker(5), 0xff);
    CHECK_EQ(fill_marker(7), 0xff);
}

/* One step per wave along a 4x1 row at a uniform cost of one: cells 0, 1 and 2
   come out 0, 1 and 2, and cell 3 would cost 3 against an allowance of 2 and
   stays at the 0xff sentinel.  The bound is CMP EAX,[EBP+0x20] / JG, so a
   candidate equal to the allowance is accepted and only a greater one is
   refused -- cell 2 is the inclusive end of the range. */
static void fill_relaxes_a_row_until_the_allowance_runs_out(void)
{
    stage_fill(4, 1);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 2);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 1);
    CHECK_EQ(fill_marker(2), 2);
    CHECK_EQ(fill_marker(3), 0xff);
}

/* The same bound stated on its own: on a 2x1 grid a step costing exactly the
   allowance is taken, and the same step against an allowance one lower is not.
   Writing the guard as < instead of <= would shrink every movement range in
   the game by one tile. */
static void fill_allowance_bound_is_inclusive(void)
{
    stage_fill(2, 1);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 1);
    CHECK_EQ(fill_marker(1), 1);

    stage_fill(2, 1);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 0);
    CHECK_EQ(fill_marker(1), 0xff);
}

/* The cost of a step is the class record's move_cost[terrain type of the tile
   being entered], where the terrain type is byte 2 of that tile id's attribute
   row and reaches the fill through data_fdps_map_tile_terrain_type -- the
   global fdps_map_load_tile_info publishes, not the CALL's return value.
   Tiles 1, 2 and 3 are given terrain types 3, 5 and 2 whose costs are 2, 1 and
   4, so the accumulated costs along the row are 0, 2, 3 and 7 rather than the
   uniform 0, 1, 2, 3 a build reading the cost from anywhere else would
   produce. */
static void fill_step_cost_comes_from_the_class_record(void)
{
    stage_fill(4, 1);
    set_tile_terrain(1, 3);
    set_tile_terrain(2, 5);
    set_tile_terrain(3, 2);
    fill_class.move_cost[0] = 1;
    fill_class.move_cost[2] = 4;
    fill_class.move_cost[3] = 2;
    fill_class.move_cost[5] = 1;
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 9);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 2);
    CHECK_EQ(fill_marker(2), 3);
    CHECK_EQ(fill_marker(3), 7);
}

/* TEST dword ptr [EBP-0xc],0x40 / JZ: a cell whose zone-of-control byte says a
   unit stands on it is refused however cheap the step is, is never queued, and
   so hides everything behind it -- cells 2 and 3 of the row both stay at the
   sentinel with an allowance of 9 that would otherwise cover the whole grid.
   Byte 0 itself is untouched by the fill. */
static void fill_impassable_bit_hides_everything_behind_it(void)
{
    stage_fill(4, 1);
    set_fill_cell_flags(2, 0x40);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 9);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 1);
    CHECK_EQ(fill_marker(2), 0xff);
    CHECK_EQ(fill_marker(3), 0xff);
    CHECK_EQ(fill_cell_flags(2), 0x40);
}

/* The rebuild note at 00010ff0.  Cell 1 carries the movement-stops-here bit,
   the step into it costs 1, and what is stored is not 1 but the whole
   allowance of 5.  Cells 2 and 3 are then reachable only because the stop cell
   was still queued: they are entered at a terrain type whose cost is 0, so
   they come out at 5 as well.  Had the original skipped the queue -- the
   obvious reading of "movement stops here" -- both would have stayed at the
   sentinel, and had it stored the real cost of 1 they would have come out at 1
   like the case below. */
static void fill_stop_bit_stores_the_allowance_and_still_queues(void)
{
    stage_fill(4, 1);
    set_tile_terrain(1, 1);
    set_tile_terrain(2, 2);
    set_tile_terrain(3, 3);
    fill_class.move_cost[0] = 1;
    fill_class.move_cost[1] = 1;
    fill_class.move_cost[2] = 0;
    fill_class.move_cost[3] = 0;
    set_fill_cell_flags(1, 0x80);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 5);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 5);
    CHECK_EQ(fill_marker(2), 5);
    CHECK_EQ(fill_marker(3), 5);
    CHECK_EQ(fill_cell_flags(1), 0x80);
}

/* The identical fixture without the 0x80 bit, which is what makes the case
   above say something: the step into cell 1 costs 1 and 1 is what is stored,
   and the two free steps behind it inherit that instead of the allowance. */
static void fill_without_the_stop_bit_stores_the_real_cost(void)
{
    stage_fill(4, 1);
    set_tile_terrain(1, 1);
    set_tile_terrain(2, 2);
    set_tile_terrain(3, 3);
    fill_class.move_cost[0] = 1;
    fill_class.move_cost[1] = 1;
    fill_class.move_cost[2] = 0;
    fill_class.move_cost[3] = 0;
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 5);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 1);
    CHECK_EQ(fill_marker(2), 1);
    CHECK_EQ(fill_marker(3), 1);
}

/* CMP EAX,dword ptr [EBP-0x8] / JL: a cell already holding a cost is written
   again only by a strictly cheaper route.  On a 2x2 grid where entering (1,0)
   costs 9 and every other tile costs 1, cell (1,1) is first reached from (1,0)
   at 10 and then, later in the same wave, from (0,1) at 2 -- and 2 is what it
   keeps.  The expensive tile itself is not revisited: 2 + 9 is not cheaper
   than the 9 already there. */
static void fill_relaxes_a_cell_reached_more_cheaply(void)
{
    stage_fill(2, 2);
    set_tile_terrain(1, 1);
    set_tile_terrain(2, 2);
    set_tile_terrain(3, 3);
    fill_class.move_cost[0] = 1;
    fill_class.move_cost[1] = 9;
    fill_class.move_cost[2] = 1;
    fill_class.move_cost[3] = 1;
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 20);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 9);
    CHECK_EQ(fill_marker(2), 1);
    CHECK_EQ(fill_marker(3), 2);
}

/* Four neighbours and no diagonal.  From the centre of a 3x3 grid with an
   allowance of exactly one step, the four orthogonal cells come out at 1 and
   the four corners stay at the sentinel; an eight-way fill would have reached
   all eight for the same cost. */
static void fill_expands_four_neighbours_and_no_diagonal(void)
{
    stage_fill(3, 3);
    fdps_move_grid_flood_fill_range(&fill_class, 1, 1, 1);
    CHECK_EQ(fill_marker(4), 0);
    CHECK_EQ(fill_marker(1), 1);
    CHECK_EQ(fill_marker(5), 1);
    CHECK_EQ(fill_marker(7), 1);
    CHECK_EQ(fill_marker(3), 1);
    CHECK_EQ(fill_marker(0), 0xff);
    CHECK_EQ(fill_marker(2), 0xff);
    CHECK_EQ(fill_marker(6), 0xff);
    CHECK_EQ(fill_marker(8), 0xff);
}

/* The same fixture read out of the frontier instead of the grid.  The four
   acceptances of the first wave are appended to buffer 1 in the order the four
   direction blocks run -- up (1,0), right (2,1), down (1,2), left (0,1) -- at
   slots 0 to 3, and buffer 1 starts at index 400 of an 800-byte array, which
   is what IMUL ...,0x190 spells at every frontier index in the function.  The
   second wave can afford nothing, so it appends nothing over buffer 0 and the
   seed at slot 0 is still the start tile when the fill returns.

   This is the only assertion in the file that can see the direction order at
   all: two routes arriving at one cell for the same cost leave the same cost
   byte behind whichever of them got there first. */
static void fill_queues_neighbours_up_right_down_left(void)
{
    stage_fill(3, 3);
    fdps_move_grid_flood_fill_range(&fill_class, 1, 1, 1);
    CHECK_EQ((int) data_fdps_battle_move_frontier_x[0], 1);
    CHECK_EQ((int) data_fdps_battle_move_frontier_y[0], 1);
    CHECK_EQ((int) data_fdps_battle_move_frontier_x[400], 1);
    CHECK_EQ((int) data_fdps_battle_move_frontier_y[400], 0);
    CHECK_EQ((int) data_fdps_battle_move_frontier_x[401], 2);
    CHECK_EQ((int) data_fdps_battle_move_frontier_y[401], 1);
    CHECK_EQ((int) data_fdps_battle_move_frontier_x[402], 1);
    CHECK_EQ((int) data_fdps_battle_move_frontier_y[402], 2);
    CHECK_EQ((int) data_fdps_battle_move_frontier_x[403], 0);
    CHECK_EQ((int) data_fdps_battle_move_frontier_y[403], 1);
}

/* The two edge guards hold at the far side of the grid: on a 2x2 block sitting
   in a buffer that has room for far more cells, an allowance of 20 covers
   every cell of the map and writes none of the four beyond it.  The costs are
   the four-connected distances -- (1,1) is two steps away by either route. */
static void fill_does_not_write_past_the_last_row_or_column(void)
{
    stage_fill(2, 2);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 20);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 1);
    CHECK_EQ(fill_marker(2), 1);
    CHECK_EQ(fill_marker(3), 2);
    CHECK_EQ(fill_marker(4), 0xdd);
    CHECK_EQ(fill_marker(5), 0xdd);
    CHECK_EQ(fill_marker(6), 0xdd);
    CHECK_EQ(fill_marker(7), 0xdd);
}

/* Only byte 1 of a cell is ever written.  Every cell of the row comes in with
   the six low bits of byte 0 set to 0x2a -- bits no other code in the program
   reads -- and comes back out with them intact alongside the fresh costs. */
static void fill_writes_only_the_marker_byte(void)
{
    stage_fill(4, 1);
    set_fill_cell_flags(0, 0x2a);
    set_fill_cell_flags(1, 0x2a);
    set_fill_cell_flags(2, 0x2a);
    set_fill_cell_flags(3, 0x2a);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 9);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 1);
    CHECK_EQ(fill_marker(2), 2);
    CHECK_EQ(fill_marker(3), 3);
    CHECK_EQ(fill_cell_flags(0), 0x2a);
    CHECK_EQ(fill_cell_flags(1), 0x2a);
    CHECK_EQ(fill_cell_flags(2), 0x2a);
    CHECK_EQ(fill_cell_flags(3), 0x2a);
}

/* MOVSX EAX,word ptr [EAX] at 00010e06 and CMP EDX,EAX / JGE at 00011081.  A
   header width of 0xffff is -1, width-1 is -2, and column 0 is not less than
   -2, so the right-hand neighbour is never even looked at.  Read the word
   unsigned and the guard becomes 0 < 65534, which relaxes the cell next door
   and walks on off the end of the block; the cell next door is the one
   assertion that can tell the two apart. */
static void fill_header_width_word_is_signed(void)
{
    stage_fill(-1, 1);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 9);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 0xdd);
}

/* MOVSX EAX,word ptr [EAX + 0x2] at 00010e0f and CMP EDX,EAX / JGE at
   000111ce, the same thing for the lower neighbour.  With width 1 and height
   -1 the row below is at cell 0 + width = cell 1, so an unsigned height of
   65535 would write there and a signed -1 leaves it alone. */
static void fill_header_height_word_is_signed(void)
{
    stage_fill(1, -1);
    fdps_move_grid_flood_fill_range(&fill_class, 0, 0, 9);
    CHECK_EQ(fill_marker(0), 0);
    CHECK_EQ(fill_marker(1), 0xdd);
}

/* fdps_move_path_trace, 00011460.
 *
 * Expected values come from the assembly: the direction codes and their scan
 * order (00011641-000116e1 for mode 0, 000116f4-00011820 for mode 1), the four
 * edge guards (CMP dword ptr [EBP+0x24],0x0 / JZ at 00011645 and the DEC/JLE
 * pairs at 00011670 and 0001169a), the strict JL of mode 0 against the JLE plus
 * tie rule of mode 1, the threshold reload MOV EAX,[EBP-0x28] / MOV
 * [EBP-0x20],EAX at 00011631 against the single write of [EBP-0x28] at
 * 00011600, the coordinate updates at 00011824-00011863, the goal test at
 * 00011866, the reversed copy at 000118bf and, for mode 2, the two side tests
 * at 000114dd-000114f9, the AND AL,0x1 at 00011532, the JGE at 00011580 and the
 * CMP ...,0xff at 00011593.  None of them is read off the emitted C.
 *
 * Grid, unit array and output buffer are all staged here: the function takes
 * its entire input from data_fdps_battle_move_grid_ptr,
 * data_fdps_map_unit_array_ptr and data_fdps_map_unit_count, so pointing those
 * at local blocks is the only way to reach the body.  Nothing below asserts
 * what any of the three holds on its own, which is ticket 23's.
 *
 * Every grid below is built so the walk provably reaches its goal.  That is not
 * decoration: the loop's only exit is standing on the goal, so a fixture whose
 * costs lead nowhere does not fail, it hangs.
 */

#define TRACE_OUT_BYTES 16

/* 0xee is neither a direction code nor a tile index in any fixture here, so a
   byte still holding it was not written. */
static unsigned char trace_out[TRACE_OUT_BYTES];

/* The grid as the flood fill leaves an unreached map: every marker byte holds
   the 0xff sentinel, and mark_cell() above relaxes the ones a case needs. */
static void stage_trace(int width, int height)
{
    int i;

    for (i = 0; i < STAGE_CELLS; i++) {
        stage_grid[4 + i * 2] = 0x00;
        stage_grid[4 + i * 2 + 1] = 0xff;
    }
    *(short *) stage_grid = (short) width;
    *(short *) (stage_grid + 2) = (short) height;
    data_fdps_battle_move_grid_ptr = stage_grid;
    for (i = 0; i < TRACE_OUT_BYTES; i++) {
        trace_out[i] = 0xee;
    }
}

static int trace_byte(int index)
{
    return (int) trace_out[index];
}

/* CMP dword ptr [EBP-0x28],0xff / JNZ at 00011603: a start tile the fill never
   reached is refused before the walk begins, and out_path is not touched. */
static void trace_unreachable_start_returns_minus_one(void)
{
    stage_trace(4, 4);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 2, 2, 0), -1);
    CHECK_EQ(trace_byte(0), 0xee);
    CHECK_EQ(trace_byte(1), 0xee);
}

/* The whole of modes 0 and 1's contract in one walk: costs 3,2,1,0 laid out
   west along row 2 and then north up column 0, so the walk steps west once
   (code 3) and north twice (code 0).  The count comes back and the codes land
   REVERSED -- the walk records 3,0,0 and out_path reads 0,0,3 (000118bf:
   out_path[step_count-1-i]).  The fourth output byte is untouched. */
static void trace_walks_downhill_and_writes_the_path_reversed(void)
{
    stage_trace(4, 4);
    mark_cell(0, 0);
    mark_cell(4, 1);
    mark_cell(8, 2);
    mark_cell(9, 3);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 1, 2, 0), 3);
    CHECK_EQ(trace_byte(0), 0);
    CHECK_EQ(trace_byte(1), 0);
    CHECK_EQ(trace_byte(2), 3);
    CHECK_EQ(trace_byte(3), 0xee);
}

/* The other two codes, and the coordinate each one moves: INC dword ptr
   [EBP+0x20] for code 1 at 00011860 and INC dword ptr [EBP+0x24] for code 2 at
   00011831.  Costs 2,1,0 run east along row 0 and then south, so the walk
   records 1 then 2 and out_path reads 2,1. */
static void trace_direction_codes_one_and_two(void)
{
    stage_trace(4, 4);
    mark_cell(0, 2);
    mark_cell(1, 1);
    mark_cell(5, 0);
    CHECK_EQ(fdps_move_path_trace(1, 1, trace_out, 0, 0, 0), 2);
    CHECK_EQ(trace_byte(0), 2);
    CHECK_EQ(trace_byte(1), 1);
    CHECK_EQ(trace_byte(2), 0xee);
}

/* The threshold every step compares against is the START tile's cost, taken
   once at 00011600, and not the cost of the tile the walk is standing on.
   Costs 5,2,3 run east along row 0: the second step stands on 2 and the only
   neighbour it can take costs 3, which is dearer than where it is and cheaper
   than where it started.  The original takes it and reaches the goal; a walk
   that reseeded the threshold from the current cell would find nothing, move
   nothing and never leave the loop. */
static void trace_threshold_is_the_start_tile_cost(void)
{
    stage_trace(4, 4);
    mark_cell(0, 5);
    mark_cell(1, 2);
    mark_cell(2, 3);
    CHECK_EQ(fdps_move_path_trace(2, 0, trace_out, 0, 0, 0), 2);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 1);
}

/* Mode 0's four compares are JL, so an equal-cost neighbour later in the scan
   order never displaces one already accepted.  Costs 5,4,3 run east along row 0
   with a second 3 south of (1,0): the second step sees east 3 and south 3, and
   mode 0 keeps east.  The mode 1 case below is the same grid. */
static void trace_mode_zero_keeps_the_heading_on_a_tie(void)
{
    stage_trace(4, 4);
    mark_cell(0, 5);
    mark_cell(1, 4);
    mark_cell(2, 3);
    mark_cell(5, 3);
    CHECK_EQ(fdps_move_path_trace(2, 0, trace_out, 0, 0, 0), 2);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 1);
}

/* Mode 1 on the same grid takes the equal cost instead, because a direction has
   already been chosen this step (step_dir 1, not 4), one step has been recorded
   and that step's code was 1, not the 2 this branch would write -- the three
   conditions at 0001174f, 00011751 and 00011768.  So the walk turns south where
   mode 0 ran east, and the goal it reaches is a different tile. */
static void trace_mode_one_turns_on_a_tie(void)
{
    stage_trace(4, 4);
    mark_cell(0, 5);
    mark_cell(1, 4);
    mark_cell(2, 3);
    mark_cell(5, 3);
    CHECK_EQ(fdps_move_path_trace(1, 1, trace_out, 0, 0, 1), 2);
    CHECK_EQ(trace_byte(0), 2);
    CHECK_EQ(trace_byte(1), 1);
}

/* The tie is refused when the last RECORDED step already used the code this
   branch would write: MOV AL,byte ptr [EAX+EBP*0x1+0xffffff5f] / CMP EAX,0x2 at
   000117b3.  The walk steps south first (code 2), then meets east 3 and south 3
   -- the same tie as above, with the previous code now 2 -- and keeps east. */
static void trace_mode_one_refuses_a_tie_repeating_the_last_step(void)
{
    stage_trace(4, 4);
    mark_cell(0, 5);
    mark_cell(4, 4);
    mark_cell(5, 3);
    mark_cell(8, 3);
    CHECK_EQ(fdps_move_path_trace(1, 1, trace_out, 0, 0, 1), 2);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 2);
}

/* CMP dword ptr [EBP-0x3c],0x0 / JNZ at 00011751: with nothing recorded yet
   there is no previous code to differ from, so the first step of a mode 1 walk
   refuses an equal cost exactly as mode 0 would.  East 4 and south 4 tie on the
   opening step; east, which got there first, keeps it. */
static void trace_mode_one_refuses_a_tie_on_the_first_step(void)
{
    stage_trace(4, 4);
    mark_cell(0, 5);
    mark_cell(1, 4);
    mark_cell(4, 4);
    CHECK_EQ(fdps_move_path_trace(1, 0, trace_out, 0, 0, 1), 1);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 0xee);
}

/* Code 4 is a step that moves nothing and records nothing (CMP EAX,0x4 / JZ
   past the store at 00011884), and the goal test at 00011866 runs after the
   move rather than before it.  Starting on the goal with every neighbour
   unreachable therefore costs one pass of the loop, writes no output and
   returns a count of zero. */
static void trace_no_move_records_nothing_and_returns_zero(void)
{
    stage_trace(4, 4);
    mark_cell(5, 4);
    CHECK_EQ(fdps_move_path_trace(1, 1, trace_out, 1, 1, 0), 0);
    CHECK_EQ(trace_byte(0), 0xee);
}

/* MOV EAX,[EBP-0x38] / DEC / CMP EAX,[EBP+0x20] / JLE at 00011670 skips the
   east neighbour on the last column.  Without it the read runs past the end of
   the row into the next one: cell 4 is (0,1), it holds the cheapest cost on the
   grid, and taking it would step to column 4. */
static void trace_last_column_skips_the_east_neighbour(void)
{
    stage_trace(4, 4);
    mark_cell(3, 2);
    mark_cell(2, 1);
    mark_cell(4, 0);
    CHECK_EQ(fdps_move_path_trace(2, 0, trace_out, 3, 0, 0), 1);
    CHECK_EQ(trace_byte(0), 3);
    CHECK_EQ(trace_byte(1), 0xee);
}

/* CMP dword ptr [EBP+0x20],0x0 / JZ at 000116c0 skips the west neighbour on
   column 0.  Without it the read lands on the last cell of the previous row --
   cell 3 from cell 4 -- which holds the cheapest cost here. */
static void trace_column_zero_skips_the_west_neighbour(void)
{
    stage_trace(4, 4);
    mark_cell(4, 2);
    mark_cell(5, 1);
    mark_cell(3, 0);
    CHECK_EQ(fdps_move_path_trace(1, 1, trace_out, 0, 1, 0), 1);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 0xee);
}

/* MOV EAX,[EBP-0x34] / DEC / CMP EAX,[EBP+0x24] / JLE at 0001169a skips the
   south neighbour on the last row.  Cell 16 is past the 4x4 grid's last cell
   and holds the cheapest cost, so a missing guard reads a cell the header does
   not cover. */
static void trace_last_row_skips_the_south_neighbour(void)
{
    stage_trace(4, 4);
    mark_cell(12, 2);
    mark_cell(13, 1);
    mark_cell(16, 0);
    CHECK_EQ(fdps_move_path_trace(1, 3, trace_out, 0, 3, 0), 1);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 0xee);
}

/* CMP dword ptr [EBP+0x24],0x0 / JZ at 00011645 skips the north neighbour on
   row 0.  Without it the read lands inside the four-byte header: from cell 2 it
   is stage_grid[1], the high byte of the width word, which is 0 for every width
   this game has and would look like the cheapest cell on the grid. */
static void trace_row_zero_skips_the_north_neighbour(void)
{
    stage_trace(4, 4);
    mark_cell(2, 2);
    mark_cell(3, 1);
    CHECK_EQ((int) stage_grid[1], 0);
    CHECK_EQ(fdps_move_path_trace(3, 0, trace_out, 2, 0, 0), 1);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 0xee);
}

/* Mode 2 with start_x zero keeps the units whose side byte is non-zero (the JNZ
   at 000114ea), walks them all and answers with the lowest marker byte it saw
   and that unit's own tile bytes.  The side-0 unit sits on the cheapest cell of
   the three and is not considered.  goal_x, goal_y and start_y are read nowhere
   on this path, so the nonsense passed for them cannot show up in the answer. */
static void trace_mode_two_finds_the_cheapest_unit_of_the_other_side(void)
{
    stage_trace(4, 4);
    mark_cell(5, 5);
    mark_cell(10, 3);
    mark_cell(15, 1);
    units_reset(3);
    put_unit(0, 1, 1, 1, 0);
    put_unit(1, 2, 2, 2, 0);
    put_unit(2, 3, 3, 0, 0);
    CHECK_EQ(fdps_move_path_trace(99, 99, trace_out, 0, 77, 2), 3);
    CHECK_EQ(trace_byte(0), 2);
    CHECK_EQ(trace_byte(1), 2);
    CHECK_EQ(trace_byte(2), 0xee);
}

/* The mirror, through the JZ at 000114f9: any non-zero start_x keeps the units
   whose side byte is 0.  Same three units, and the answer is the one the case
   above threw away.  side_select is a truth value, not a side number -- the
   value 1 does not mean "side 1", which is the unit it does not pick. */
static void trace_mode_two_side_select_is_a_truth_value(void)
{
    stage_trace(4, 4);
    mark_cell(5, 5);
    mark_cell(10, 3);
    mark_cell(15, 1);
    units_reset(3);
    put_unit(0, 1, 1, 1, 0);
    put_unit(1, 2, 2, 2, 0);
    put_unit(2, 3, 3, 0, 0);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 1, 0, 2), 1);
    CHECK_EQ(trace_byte(0), 3);
    CHECK_EQ(trace_byte(1), 3);
}

/* AND AL,0x1 at 00011532 is bit 0 alone: the retired unit on the cheaper cell
   is passed over, while the unit carrying the unrelated 0x80 of the same byte
   still counts. */
static void trace_mode_two_retired_bit_is_bit_zero_only(void)
{
    stage_trace(4, 4);
    mark_cell(5, 2);
    mark_cell(10, 4);
    units_reset(2);
    put_unit(0, 1, 1, 1, 0x01);
    put_unit(1, 2, 2, 1, 0x80);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 0, 0, 2), 4);
    CHECK_EQ(trace_byte(0), 2);
    CHECK_EQ(trace_byte(1), 2);
}

/* CMP dword ptr [EBP-0x3c],0xff / JNZ at 00011593: the running minimum starts
   at the same 0xff a cell holds when the fill never reached it, and the compare
   at 00011580 is strict, so a unit standing on an unreachable cell can never
   become the answer.  Both arms of "no answer" come back as -1 with out_path
   untouched -- no unit passed the side filter, and one did but had nowhere
   reachable to stand. */
static void trace_mode_two_returns_minus_one_when_nothing_matched(void)
{
    stage_trace(4, 4);
    mark_cell(5, 2);
    units_reset(2);
    put_unit(0, 1, 1, 0, 0);
    put_unit(1, 2, 2, 0, 0);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 0, 0, 2), -1);
    CHECK_EQ(trace_byte(0), 0xee);

    stage_trace(4, 4);
    units_reset(1);
    put_unit(0, 1, 1, 1, 0);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 0, 0, 2), -1);
    CHECK_EQ(trace_byte(0), 0xee);
}

/* JGE at 00011580 skips the store, so of two units on equally cheap cells the
   one the walk met first keeps the answer.  A JG there would hand it to the
   later index instead. */
static void trace_mode_two_ties_go_to_the_lower_index(void)
{
    stage_trace(4, 4);
    mark_cell(5, 3);
    mark_cell(10, 3);
    units_reset(2);
    put_unit(0, 1, 1, 1, 0);
    put_unit(1, 2, 2, 1, 0);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 0, 0, 2), 3);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 1);
}

/* CMP EAX,dword ptr [0x00060150] / JL at 000114b9 bounds the scan by the unit
   count and nothing else: the record past the count is staged, sits on the
   cheapest cell and passes every filter, and the answer ignores it. */
static void trace_mode_two_scan_stops_at_the_unit_count(void)
{
    stage_trace(4, 4);
    mark_cell(5, 5);
    mark_cell(10, 1);
    units_reset(1);
    put_unit(0, 1, 1, 1, 0);
    put_unit(1, 2, 2, 1, 0);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 0, 0, 2), 5);
    CHECK_EQ(trace_byte(0), 1);
    CHECK_EQ(trace_byte(1), 1);
}

/* IMUL EAX,[EBP-0x30] on record byte +0 and IMUL EAX,[EBP-0x2c] on record byte
   +1 at 00011552 and 00011566: the column scales by the cell stride and the row
   by the row stride, never the other way round.  The unit stands at (3,1),
   which is cell 7; cell 13 is (1,3), the cell the swapped arithmetic would
   read, and it is cheaper. */
static void trace_mode_two_cell_is_column_times_two_plus_row_stride(void)
{
    stage_trace(4, 4);
    mark_cell(7, 6);
    mark_cell(13, 0);
    units_reset(1);
    put_unit(0, 3, 1, 1, 0);
    CHECK_EQ(fdps_move_path_trace(0, 0, trace_out, 0, 0, 2), 6);
    CHECK_EQ(trace_byte(0), 3);
    CHECK_EQ(trace_byte(1), 1);
}

void run_movegrid_tests(void)
{
    RUN_TEST(grid_cell_stride_is_two);
    RUN_TEST(grid_null_pointer_returns_at_once);
    RUN_TEST(grid_clears_both_zoc_bits);
    RUN_TEST(grid_preserves_low_six_bits);
    RUN_TEST(grid_marker_is_reset_unconditionally);
    RUN_TEST(grid_cells_start_after_the_header);
    RUN_TEST(grid_walks_width_times_height_cells);
    RUN_TEST(grid_bound_follows_the_header);
    RUN_TEST(grid_zero_dimension_touches_nothing);
    RUN_TEST(grid_header_words_are_signed);

    RUN_TEST(zoc_null_pointer_returns_at_once);
    RUN_TEST(zoc_marks_four_neighbours_and_centre);
    RUN_TEST(zoc_column_zero_skips_the_left_neighbour);
    RUN_TEST(zoc_row_zero_skips_the_upper_neighbour);
    RUN_TEST(zoc_last_column_skips_the_right_neighbour);
    RUN_TEST(zoc_last_row_skips_the_lower_neighbour);
    RUN_TEST(zoc_bits_are_ored_into_the_cell);
    RUN_TEST(zoc_zones_accumulate_across_calls);
    RUN_TEST(zoc_centre_has_no_bounds_check);
    RUN_TEST(zoc_width_guard_is_signed);
    RUN_TEST(zoc_height_guard_is_signed);

    RUN_TEST(stop_flag_addresses_the_named_cell);
    RUN_TEST(stop_flag_is_ored_into_the_cell);
    RUN_TEST(stop_flag_width_word_is_signed);
    RUN_TEST(stop_flag_width_comes_from_the_header);
    RUN_TEST(stop_flag_has_no_bounds_check);

    RUN_TEST(zones_unit_record_stride_is_0x50);
    RUN_TEST(zones_select_zero_marks_nonzero_sides);
    RUN_TEST(zones_select_one_marks_side_zero);
    RUN_TEST(zones_side_is_a_truth_value_not_a_number);
    RUN_TEST(zones_select_zero_marks_neutral_and_player);
    RUN_TEST(zones_retired_bit_is_bit_zero_only);
    RUN_TEST(zones_walk_stops_at_the_unit_count);
    RUN_TEST(zones_unit_count_is_signed);
    RUN_TEST(zones_base_is_reread_from_the_global);
    RUN_TEST(zones_tile_bytes_are_passed_through_unchecked);

    RUN_TEST(block_excludes_the_named_unit_index);
    RUN_TEST(block_select_zero_blocks_side_zero_units);
    RUN_TEST(block_select_nonzero_blocks_npc_and_player);
    RUN_TEST(block_side_select_is_a_truth_value);
    RUN_TEST(block_writes_only_the_marker_byte);
    RUN_TEST(block_retired_bit_is_bit_zero_only);
    RUN_TEST(block_walk_stops_at_the_signed_unit_count);
    RUN_TEST(block_index_uses_the_header_width);
    RUN_TEST(block_width_word_is_signed);
    RUN_TEST(block_coordinates_are_not_range_checked);

    RUN_TEST(collect_writes_one_xy_pair_per_marked_cell);
    RUN_TEST(collect_sentinel_is_ff_and_nothing_else);
    RUN_TEST(collect_reads_the_marker_not_the_flags);
    RUN_TEST(collect_visits_cells_in_row_major_order);
    RUN_TEST(collect_returns_zero_when_nothing_is_marked);
    RUN_TEST(collect_bound_comes_from_the_header);
    RUN_TEST(collect_header_words_are_signed);
    RUN_TEST(collect_zero_dimension_collects_nothing);
    RUN_TEST(collect_does_not_modify_the_grid);
    RUN_TEST(collect_reaches_the_last_cell);

    RUN_TEST(fill_move_costs_are_the_first_eight_bytes);
    RUN_TEST(fill_start_tile_gets_cost_zero);
    RUN_TEST(fill_seeds_buffer_zero_from_the_start_coordinates);
    RUN_TEST(fill_relaxes_a_row_until_the_allowance_runs_out);
    RUN_TEST(fill_allowance_bound_is_inclusive);
    RUN_TEST(fill_step_cost_comes_from_the_class_record);
    RUN_TEST(fill_impassable_bit_hides_everything_behind_it);
    RUN_TEST(fill_stop_bit_stores_the_allowance_and_still_queues);
    RUN_TEST(fill_without_the_stop_bit_stores_the_real_cost);
    RUN_TEST(fill_relaxes_a_cell_reached_more_cheaply);
    RUN_TEST(fill_expands_four_neighbours_and_no_diagonal);
    RUN_TEST(fill_queues_neighbours_up_right_down_left);
    RUN_TEST(fill_does_not_write_past_the_last_row_or_column);
    RUN_TEST(fill_writes_only_the_marker_byte);
    RUN_TEST(fill_header_width_word_is_signed);
    RUN_TEST(fill_header_height_word_is_signed);

    RUN_TEST(trace_unreachable_start_returns_minus_one);
    RUN_TEST(trace_walks_downhill_and_writes_the_path_reversed);
    RUN_TEST(trace_direction_codes_one_and_two);
    RUN_TEST(trace_threshold_is_the_start_tile_cost);
    RUN_TEST(trace_mode_zero_keeps_the_heading_on_a_tie);
    RUN_TEST(trace_mode_one_turns_on_a_tie);
    RUN_TEST(trace_mode_one_refuses_a_tie_repeating_the_last_step);
    RUN_TEST(trace_mode_one_refuses_a_tie_on_the_first_step);
    RUN_TEST(trace_no_move_records_nothing_and_returns_zero);
    RUN_TEST(trace_last_column_skips_the_east_neighbour);
    RUN_TEST(trace_column_zero_skips_the_west_neighbour);
    RUN_TEST(trace_last_row_skips_the_south_neighbour);
    RUN_TEST(trace_row_zero_skips_the_north_neighbour);
    RUN_TEST(trace_mode_two_finds_the_cheapest_unit_of_the_other_side);
    RUN_TEST(trace_mode_two_side_select_is_a_truth_value);
    RUN_TEST(trace_mode_two_retired_bit_is_bit_zero_only);
    RUN_TEST(trace_mode_two_returns_minus_one_when_nothing_matched);
    RUN_TEST(trace_mode_two_ties_go_to_the_lower_index);
    RUN_TEST(trace_mode_two_scan_stops_at_the_unit_count);
    RUN_TEST(trace_mode_two_cell_is_column_times_two_plus_row_stride);


    /* Put the global back before leaving.  stage() points it at this file's
       own stage_grid, and the runners share one process: a later unit that
       expects an unallocated grid would inherit a live pointer into another
       translation unit's fixture and pass or fail for the wrong reason. */
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_attr_ptr[0] = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
}
