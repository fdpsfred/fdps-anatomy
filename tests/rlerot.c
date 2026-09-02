/* tests/rlerot.c -- cover for src/rlerot.c.
 *
 * Every expected value is read off the assembly at 00056e2a: the four quadrant
 * arms are the two signed tests CMP DX,0 / JG and CMP AX,0 / JG at 00056e4d and
 * 00056e57, the per-pixel step is ADD DX,[0x0007004c] / CMP DX,0x1000 / JC with
 * a single SUB DX,0x1000 on the carry, the gap-filling store is the MOV
 * [EDI],AL at 00056f79 that runs before the cursor is moved by the y step, and
 * the row step is the pair of accumulators at 0x00070048 and 0x0007004a that
 * are cleared at 00056e2a and never again.  None of them is taken from the
 * emitted C.
 *
 * The destination is always pre-filled with 0x5a so a byte the kernel is meant
 * to leave alone can be told apart from one it wrote, and the rotating cases
 * start from the middle of the surface because their cursor moves in both
 * directions.  The surface is 16 bytes to a row, which is the pitch every case
 * hands the kernel.
 */
#include "testharn.h"
#include "gamedata.h"
#include "rlerot.h"

#define SENTINEL 0x5a
#define SURFACE_PITCH 16
/* Row 8, column 8: far enough from either edge that a rotated run stays inside
   the surface whichever quadrant it walks into. */
#define SURFACE_MIDDLE (8 * SURFACE_PITCH + 8)

static unsigned char dest_surface[256];

/* The three globals are inputs here, written the way fdps_blit_dispatch writes
   them at 000568ea, 000568f8 and 00056903 before it calls the kernel.  Their
   real contents belong to no one -- they are scratch -- so nothing below
   asserts what they hold on the way in, only what the kernel leaves behind. */
static void blit_setup(unsigned short src_width, unsigned short rows)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof dest_surface; byte_index++) {
        dest_surface[byte_index] = SENTINEL;
    }
    data_fdps_graphics_rle_blit_src_width = src_width;
    data_fdps_graphics_rle_blit_remaining_rows = rows;
    data_fdps_graphics_rle_blit_dst_pitch = SURFACE_PITCH;
}

/* A vector of (0x1000, 0) is one whole destination pixel to the right per
   source pixel and nothing vertical, so the kernel degenerates to the plain
   blitter: a length-4 fill covers four consecutive bytes and the fifth is
   outside both the run and the row.  The row counter is the kernel's own doing
   -- DEC word ptr [0x00070022] until JNZ falls through. */
static void rotate_unit_vector_draws_the_source_row(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    blit_setup(4, 1);
    fdps_rle_blit_rotated(stream, dest_surface, 0x1000, 0);

    CHECK_EQ(dest_surface[0], 0xaa);
    CHECK_EQ(dest_surface[3], 0xaa);
    CHECK_EQ(dest_surface[4], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* With dy at zero the row step is |dx| into the second accumulator alone, so
   each row carries the origin down by exactly one pitch (ADD EDI,[0x00070040]
   at 000570f9, the arm's +pitch). */
static void rotate_second_row_starts_one_pitch_down(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0x11;
    stream[2] = 0x01;
    stream[3] = 0x22;
    blit_setup(2, 2);
    fdps_rle_blit_rotated(stream, dest_surface, 0x1000, 0);

    CHECK_EQ(dest_surface[0], 0x11);
    CHECK_EQ(dest_surface[1], 0x11);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[SURFACE_PITCH], 0x22);
    CHECK_EQ(dest_surface[SURFACE_PITCH + 1], 0x22);
}

/* A vector of (0, 0x1000) is a quarter turn: the horizontal accumulator never
   reaches 0x1000 and the vertical one carries on every step, so the source row
   is painted up a destination column.  Upward, because the pixel step in y is
   -sign(dy) * pitch (NEG EBP at 00056efa).  dx is zero, which is not greater
   than zero, so the test at 00056e4d takes the negative arm and the x step is
   -1 even though nothing ever applies it. */
static void rotate_quarter_turn_walks_a_column_upward(void)
{
    unsigned char stream[2];

    stream[0] = 0x02;
    stream[1] = 0xbb;
    blit_setup(3, 1);
    fdps_rle_blit_rotated(stream, dest_surface + SURFACE_MIDDLE, 0, 0x1000);

    CHECK_EQ(dest_surface[SURFACE_MIDDLE], 0xbb);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE - SURFACE_PITCH], 0xbb);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE - 2 * SURFACE_PITCH], 0xbb);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE - 3 * SURFACE_PITCH], SENTINEL);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE + 1], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
}

/* The gap filler.  A vector of (0x1000, 0x1000) crosses a column and a row on
   the same step, and the store at 00056f79 puts the pixel down at the cursor
   the horizontal carry has already moved, before the vertical step moves it
   again.  Without that store the diagonal neighbour would be the only byte
   written and the run would dot instead of joining up. */
static void rotate_diagonal_step_paints_both_neighbours(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0xcc;
    blit_setup(1, 1);
    fdps_rle_blit_rotated(stream, dest_surface + SURFACE_MIDDLE,
                          0x1000, 0x1000);

    CHECK_EQ(dest_surface[SURFACE_MIDDLE], 0xcc);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE + 1], 0xcc);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE + 1 - SURFACE_PITCH], SENTINEL);
}

/* One 0x1000 comes off the accumulator per step and no more, so a vector three
   whole pixels long still advances one byte per source pixel: four source
   pixels land on four consecutive bytes.  The `accumulator >> 12` spelling
   would put them on bytes 0, 3, 6 and 9, which is why bytes 1 and 6 are both
   asserted (rebuild_info/pitfalls.md). */
static void rotate_oversized_step_advances_one_pixel_only(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xdd;
    blit_setup(4, 1);
    fdps_rle_blit_rotated(stream, dest_surface, 0x3000, 0);

    CHECK_EQ(dest_surface[0], 0xdd);
    CHECK_EQ(dest_surface[1], 0xdd);
    CHECK_EQ(dest_surface[2], 0xdd);
    CHECK_EQ(dest_surface[3], 0xdd);
    CHECK_EQ(dest_surface[4], SENTINEL);
    CHECK_EQ(dest_surface[6], SENTINEL);
}

/* Half a pixel per source pixel: the accumulator carries on every second step,
   so four source pixels collapse onto two destination bytes.  Shrinking is the
   only rescale this mode can do. */
static void rotate_half_step_shrinks_the_run(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xee;
    blit_setup(4, 1);
    fdps_rle_blit_rotated(stream, dest_surface, 0x800, 0);

    CHECK_EQ(dest_surface[0], 0xee);
    CHECK_EQ(dest_surface[1], 0xee);
    CHECK_EQ(dest_surface[2], SENTINEL);
}

/* The row-to-row accumulators carry across rows.  |dy| is half a pixel, so the
   first row's contribution does not move the origin sideways and the second
   row's does: the three one-pixel rows land at the middle, one pitch down, and
   two pitches down AND one byte across.  Zeroing the pair at the top of every
   row would leave the third row directly below the second, which is a shear. */
static void rotate_row_accumulators_carry_between_rows(void)
{
    unsigned char stream[6];

    stream[0] = 0x00;
    stream[1] = 0x31;
    stream[2] = 0x00;
    stream[3] = 0x32;
    stream[4] = 0x00;
    stream[5] = 0x33;
    blit_setup(1, 3);
    fdps_rle_blit_rotated(stream, dest_surface + SURFACE_MIDDLE, 0x1000, 0x800);

    CHECK_EQ(dest_surface[SURFACE_MIDDLE], 0x31);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE + SURFACE_PITCH], 0x32);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE + 2 * SURFACE_PITCH + 1], 0x33);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE + 2 * SURFACE_PITCH], SENTINEL);
}

/* Those accumulators are cleared on entry, at 00056e2a and 00056e33, so
   whatever the previous blit left in them does not shift this one.  Both are
   loaded with 0xfff first: carried in, they would carry immediately and put the
   second row one byte and one pitch further on than it belongs. */
static void rotate_clears_the_row_accumulators_on_entry(void)
{
    unsigned char stream[4];

    stream[0] = 0x00;
    stream[1] = 0x41;
    stream[2] = 0x00;
    stream[3] = 0x42;
    blit_setup(1, 2);
    data_fdps_graphics_rle_blit_rot_row_step_x_accumulator = 0xfff;
    data_fdps_graphics_rle_blit_rot_row_step_y_accumulator = 0xfff;
    fdps_rle_blit_rotated(stream, dest_surface + SURFACE_MIDDLE, 0x1000, 0x800);

    CHECK_EQ(dest_surface[SURFACE_MIDDLE], 0x41);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE + SURFACE_PITCH], 0x42);
    CHECK_EQ(dest_surface[SURFACE_MIDDLE + SURFACE_PITCH + 1], SENTINEL);
}

/* Op 11 is the one op with no store at all, not even the gap-filling one: the
   two bytes it covers keep the surface's own content and the fill that follows
   lands two bytes further on. */
static void rotate_skip_run_leaves_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x00;
    stream[2] = 0x77;
    blit_setup(3, 1);
    fdps_rle_blit_rotated(stream, dest_surface, 0x1000, 0);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], SENTINEL);
    CHECK_EQ(dest_surface[2], 0x77);
    CHECK_EQ(dest_surface[3], SENTINEL);
}

/* Op 01 covers two columns per unit of length and the phase starts at zero (XOR
   AH,AH at 00056f99), so the first column of each pair is stepped over and only
   the second is painted.  The row is two bytes wide because the length is
   doubled before it is taken off the counter (SHL CX,1 at 00056f92). */
static void rotate_halftone_run_writes_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x40;
    stream[1] = 0x99;
    blit_setup(2, 1);
    fdps_rle_blit_rotated(stream, dest_surface, 0x1000, 0);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], 0x99);
    CHECK_EQ(dest_surface[2], SENTINEL);
}

/* Op 10 LODSBs one pixel byte per step, so the three bytes after the command
   byte reach the destination in order and unchanged. */
static void rotate_literal_run_copies_stream_bytes(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    blit_setup(3, 1);
    fdps_rle_blit_rotated(stream, dest_surface, 0x1000, 0);

    CHECK_EQ(dest_surface[0], 0x11);
    CHECK_EQ(dest_surface[1], 0x22);
    CHECK_EQ(dest_surface[2], 0x33);
    CHECK_EQ(dest_surface[3], SENTINEL);
}

/* The length is the low six bits plus one -- SHR CL,2 / INC CL at 00056f3a --
   so 0x3f is 64 and no longer run can be encoded. */
static void rotate_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0x66;
    blit_setup(64, 1);
    fdps_rle_blit_rotated(stream, dest_surface, 0x1000, 0);

    CHECK_EQ(dest_surface[0], 0x66);
    CHECK_EQ(dest_surface[63], 0x66);
    CHECK_EQ(dest_surface[64], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The four quadrant arms.  Each writes the same four step globals with its own
   signs folded in and stores |dx| and |dy| as the magnitudes the accumulators
   gather, so a blit of one pixel is enough to read the arm back out.  The
   expected values are the arm's own stores: sign(dx) for the pixel step in x,
   sign(dx) * pitch for the row step in y, -sign(dy) * pitch for the pixel step
   in y, and sign(dy) for the row step in x. */
static void rotate_arm_dx_positive_dy_positive(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x01;
    blit_setup(1, 1);
    fdps_rle_blit_rotated(stream, dest_surface + SURFACE_MIDDLE,
                          0x1000, 0x1000);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             SURFACE_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             -SURFACE_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

static void rotate_arm_dx_positive_dy_negative(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x02;
    blit_setup(1, 1);
    fdps_rle_blit_rotated(stream, dest_surface + SURFACE_MIDDLE,
                          0x1000, -0x1000);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             SURFACE_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             SURFACE_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

/* This arm and the next are also what pin the sign test down as a signed one:
   read unsigned, a dx of -0x1000 is 0xf000 and would take the positive arm
   above. */
static void rotate_arm_dx_negative_dy_positive(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x03;
    blit_setup(1, 1);
    fdps_rle_blit_rotated(stream, dest_surface + SURFACE_MIDDLE,
                          -0x1000, 0x1000);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             -SURFACE_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             -SURFACE_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

static void rotate_arm_dx_negative_dy_negative(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x04;
    blit_setup(1, 1);
    fdps_rle_blit_rotated(stream, dest_surface + SURFACE_MIDDLE,
                          -0x1000, -0x1000);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             -SURFACE_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             SURFACE_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

void run_rlerot_tests(void)
{
    RUN_TEST(rotate_unit_vector_draws_the_source_row);
    RUN_TEST(rotate_second_row_starts_one_pitch_down);
    RUN_TEST(rotate_quarter_turn_walks_a_column_upward);
    RUN_TEST(rotate_diagonal_step_paints_both_neighbours);
    RUN_TEST(rotate_oversized_step_advances_one_pixel_only);
    RUN_TEST(rotate_half_step_shrinks_the_run);
    RUN_TEST(rotate_row_accumulators_carry_between_rows);
    RUN_TEST(rotate_clears_the_row_accumulators_on_entry);
    RUN_TEST(rotate_skip_run_leaves_destination_alone);
    RUN_TEST(rotate_halftone_run_writes_second_of_each_pair);
    RUN_TEST(rotate_literal_run_copies_stream_bytes);
    RUN_TEST(rotate_run_length_tops_out_at_64);
    RUN_TEST(rotate_arm_dx_positive_dy_positive);
    RUN_TEST(rotate_arm_dx_positive_dy_negative);
    RUN_TEST(rotate_arm_dx_negative_dy_positive);
    RUN_TEST(rotate_arm_dx_negative_dy_negative);
}
