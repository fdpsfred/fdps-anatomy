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

    /* Put the global back before leaving.  stage() points it at this file's
       own stage_grid, and the runners share one process: a later unit that
       expects an unallocated grid would inherit a live pointer into another
       translation unit's fixture and pass or fail for the wrong reason. */
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
}
