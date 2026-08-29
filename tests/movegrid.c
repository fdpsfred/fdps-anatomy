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
}
