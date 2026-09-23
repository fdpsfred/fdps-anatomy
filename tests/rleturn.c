/* tests/rleturn.c -- cover for src/rleturn.asm: the two rotating kernels.
 *
 * The kernels in rleturn.asm are hand-written assembly transcribed from the
 * original with no C interface -- they take their inputs in registers and out
 * of fdps_blit_dispatch's own stack frame -- so every case here calls
 * fdps_blit_dispatch with the kernel's mode.  The cases carry over every
 * situation the C-translation tests in tests/rlerot.c verify (those are kept
 * for reference under #if 0), and none of them depends on which spelling of
 * the kernels is linked (rebuild_info/emit_pipeline.md).
 */
#include "testharn.h"
#include "blit.h"
#include "gamedata.h"
#include "rlerot.h"

/* --- fdps_rle_blit_rotated (00056e2a), blit mode 5 --------------------------
 *
 * Expected values are read off the assembly at 00056e2a: the four quadrant
 * arms are the two signed tests CMP DX,0 / JG and CMP AX,0 / JG at 00056e4d
 * and 00056e57, the per-pixel step is ADD DX,[cos] / CMP DX,0x1000 / JB with a
 * single SUB DX,0x1000 on the carry, the gap-filling store is the MOV [EDI],AL
 * at 00056f79 that runs before the cursor is moved by the y step, and the row
 * step is the pair of accumulators at 0x00070048 and 0x0007004a that are
 * cleared at 00056e2a and never again.
 *
 * Mode 5 is handed no row advance, so each case dispatches with its width,
 * its row count and the surface's own pitch, ROTATED_PITCH.  The mode operand
 * is read as two words out of the dispatcher's frame, [EBP+1Ch] for dx and
 * [EBP+1Eh] for dy, which is what ROTATED_OPERAND packs.  The destination is
 * pre-filled with ROTATED_SENTINEL so a byte the kernel is meant to leave
 * alone can be told apart from one it wrote, and the rotating cases start from
 * the middle of the surface because their cursor moves in both directions.
 */
#define ROTATED_MODE     5
#define ROTATED_SENTINEL 0x5a
#define ROTATED_PITCH    16
/* Row 8, column 8: far enough from either edge that a rotated run stays inside
   the surface whichever quadrant it walks into. */
#define ROTATED_MIDDLE   (8 * ROTATED_PITCH + 8)
#define ROTATED_OPERAND(dx, dy) \
    (((unsigned int) (unsigned short) (dy) << 16) | \
     (unsigned int) (unsigned short) (dx))

static unsigned char rotated_surface[256];

static void rotated_clear(void)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof rotated_surface;
         byte_index++) {
        rotated_surface[byte_index] = ROTATED_SENTINEL;
    }
}

/* A vector of (0x1000, 0) is one whole destination pixel to the right per
   source pixel and nothing vertical, so the kernel degenerates to the plain
   blitter: a length-4 fill covers four consecutive bytes and the fifth is
   outside both the run and the row.  The row counter is the kernel's own doing
   -- DEC word ptr [0x00070022] until JNZ falls through. */
static void rotated_unit_vector_draws_the_source_row(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface, 4, 1, ROTATED_PITCH,
                       ROTATED_OPERAND(0x1000, 0), ROTATED_MODE);

    CHECK_EQ(rotated_surface[0], 0xaa);
    CHECK_EQ(rotated_surface[3], 0xaa);
    CHECK_EQ(rotated_surface[4], ROTATED_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* With dy at zero the row step is |dx| into the second accumulator alone, so
   each row carries the origin down by exactly one pitch (ADD EDI,[0x00070040]
   at 000570f9, the arm's +pitch). */
static void rotated_second_row_starts_one_pitch_down(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0x11;
    stream[2] = 0x01;
    stream[3] = 0x22;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface, 2, 2, ROTATED_PITCH,
                       ROTATED_OPERAND(0x1000, 0), ROTATED_MODE);

    CHECK_EQ(rotated_surface[0], 0x11);
    CHECK_EQ(rotated_surface[1], 0x11);
    CHECK_EQ(rotated_surface[2], ROTATED_SENTINEL);
    CHECK_EQ(rotated_surface[ROTATED_PITCH], 0x22);
    CHECK_EQ(rotated_surface[ROTATED_PITCH + 1], 0x22);
}

/* A vector of (0, 0x1000) is a quarter turn: the horizontal accumulator never
   reaches 0x1000 and the vertical one carries on every step, so the source row
   is painted up a destination column.  Upward, because the pixel step in y is
   -sign(dy) * pitch (NEG EBP at 00056e91).  dx is zero, which is not greater
   than zero, so the test at 00056e4d takes the negative arm and the x step is
   -1 even though nothing ever applies it. */
static void rotated_quarter_turn_walks_a_column_upward(void)
{
    unsigned char stream[2];

    stream[0] = 0x02;
    stream[1] = 0xbb;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface + ROTATED_MIDDLE, 3, 1,
                       ROTATED_PITCH, ROTATED_OPERAND(0, 0x1000),
                       ROTATED_MODE);

    CHECK_EQ(rotated_surface[ROTATED_MIDDLE], 0xbb);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE - ROTATED_PITCH], 0xbb);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE - 2 * ROTATED_PITCH], 0xbb);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE - 3 * ROTATED_PITCH],
             ROTATED_SENTINEL);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE + 1], ROTATED_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
}

/* The gap filler.  A vector of (0x1000, 0x1000) crosses a column and a row on
   the same step, and the store at 00056f79 puts the pixel down at the cursor
   the horizontal carry has already moved, before the vertical step moves it
   again.  Without that store the diagonal neighbour would be the only byte
   written and the run would dot instead of joining up. */
static void rotated_diagonal_step_paints_both_neighbours(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0xcc;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface + ROTATED_MIDDLE, 1, 1,
                       ROTATED_PITCH, ROTATED_OPERAND(0x1000, 0x1000),
                       ROTATED_MODE);

    CHECK_EQ(rotated_surface[ROTATED_MIDDLE], 0xcc);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE + 1], 0xcc);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE + 1 - ROTATED_PITCH],
             ROTATED_SENTINEL);
}

/* One 0x1000 comes off the accumulator per step and no more, so a vector three
   whole pixels long still advances one byte per source pixel: four source
   pixels land on four consecutive bytes.  The `accumulator >> 12` spelling
   would put them on bytes 0, 3, 6 and 9, which is why bytes 1 and 6 are both
   asserted (rebuild_info/pitfalls.md). */
static void rotated_oversized_step_advances_one_pixel_only(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xdd;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface, 4, 1, ROTATED_PITCH,
                       ROTATED_OPERAND(0x3000, 0), ROTATED_MODE);

    CHECK_EQ(rotated_surface[0], 0xdd);
    CHECK_EQ(rotated_surface[1], 0xdd);
    CHECK_EQ(rotated_surface[2], 0xdd);
    CHECK_EQ(rotated_surface[3], 0xdd);
    CHECK_EQ(rotated_surface[4], ROTATED_SENTINEL);
    CHECK_EQ(rotated_surface[6], ROTATED_SENTINEL);
}

/* Half a pixel per source pixel: the accumulator carries on every second step,
   so four source pixels collapse onto two destination bytes.  Shrinking is the
   only rescale this mode can do. */
static void rotated_half_step_shrinks_the_run(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xee;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface, 4, 1, ROTATED_PITCH,
                       ROTATED_OPERAND(0x800, 0), ROTATED_MODE);

    CHECK_EQ(rotated_surface[0], 0xee);
    CHECK_EQ(rotated_surface[1], 0xee);
    CHECK_EQ(rotated_surface[2], ROTATED_SENTINEL);
}

/* The row-to-row accumulators carry across rows.  |dy| is half a pixel, so the
   first row's contribution does not move the origin sideways and the second
   row's does: the three one-pixel rows land at the middle, one pitch down, and
   two pitches down AND one byte across.  Zeroing the pair at the top of every
   row would leave the third row directly below the second, which is a shear. */
static void rotated_row_accumulators_carry_between_rows(void)
{
    unsigned char stream[6];

    stream[0] = 0x00;
    stream[1] = 0x31;
    stream[2] = 0x00;
    stream[3] = 0x32;
    stream[4] = 0x00;
    stream[5] = 0x33;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface + ROTATED_MIDDLE, 1, 3,
                       ROTATED_PITCH, ROTATED_OPERAND(0x1000, 0x800),
                       ROTATED_MODE);

    CHECK_EQ(rotated_surface[ROTATED_MIDDLE], 0x31);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE + ROTATED_PITCH], 0x32);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE + 2 * ROTATED_PITCH + 1], 0x33);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE + 2 * ROTATED_PITCH],
             ROTATED_SENTINEL);
}

/* Those accumulators are cleared on entry, at 00056e2a and 00056e33, so
   whatever the previous blit left in them does not shift this one.  The
   dispatcher never touches them, so loading both with 0xfff before the
   dispatch reaches the kernel: carried in, they would carry immediately and
   put the second row one byte and one pitch further on than it belongs. */
static void rotated_clears_the_row_accumulators_on_entry(void)
{
    unsigned char stream[4];

    stream[0] = 0x00;
    stream[1] = 0x41;
    stream[2] = 0x00;
    stream[3] = 0x42;
    rotated_clear();
    data_fdps_graphics_rle_blit_rot_row_step_x_accumulator = 0xfff;
    data_fdps_graphics_rle_blit_rot_row_step_y_accumulator = 0xfff;
    fdps_blit_dispatch(stream, rotated_surface + ROTATED_MIDDLE, 1, 2,
                       ROTATED_PITCH, ROTATED_OPERAND(0x1000, 0x800),
                       ROTATED_MODE);

    CHECK_EQ(rotated_surface[ROTATED_MIDDLE], 0x41);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE + ROTATED_PITCH], 0x42);
    CHECK_EQ(rotated_surface[ROTATED_MIDDLE + ROTATED_PITCH + 1],
             ROTATED_SENTINEL);
}

/* Op 11 is the one op with no store at all, not even the gap-filling one: the
   two bytes it covers keep the surface's own content and the fill that follows
   lands two bytes further on. */
static void rotated_skip_run_leaves_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x00;
    stream[2] = 0x77;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface, 3, 1, ROTATED_PITCH,
                       ROTATED_OPERAND(0x1000, 0), ROTATED_MODE);

    CHECK_EQ(rotated_surface[0], ROTATED_SENTINEL);
    CHECK_EQ(rotated_surface[1], ROTATED_SENTINEL);
    CHECK_EQ(rotated_surface[2], 0x77);
    CHECK_EQ(rotated_surface[3], ROTATED_SENTINEL);
}

/* Op 01 covers two columns per unit of length and the phase starts at zero (XOR
   AH,AH at 00056f99), so the first column of each pair is stepped over and only
   the second is painted.  The row is two bytes wide because the length is
   doubled before it is taken off the counter (SHL CX,1 at 00056f92). */
static void rotated_halftone_run_writes_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x40;
    stream[1] = 0x99;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface, 2, 1, ROTATED_PITCH,
                       ROTATED_OPERAND(0x1000, 0), ROTATED_MODE);

    CHECK_EQ(rotated_surface[0], ROTATED_SENTINEL);
    CHECK_EQ(rotated_surface[1], 0x99);
    CHECK_EQ(rotated_surface[2], ROTATED_SENTINEL);
}

/* Op 10 LODSBs one pixel byte per step, so the three bytes after the command
   byte reach the destination in order and unchanged. */
static void rotated_literal_run_copies_stream_bytes(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface, 3, 1, ROTATED_PITCH,
                       ROTATED_OPERAND(0x1000, 0), ROTATED_MODE);

    CHECK_EQ(rotated_surface[0], 0x11);
    CHECK_EQ(rotated_surface[1], 0x22);
    CHECK_EQ(rotated_surface[2], 0x33);
    CHECK_EQ(rotated_surface[3], ROTATED_SENTINEL);
}

/* The length is the low six bits plus one -- SHR CL,2 / INC CL at 00056f3a --
   so 0x3f is 64 and no longer run can be encoded. */
static void rotated_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0x66;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface, 64, 1, ROTATED_PITCH,
                       ROTATED_OPERAND(0x1000, 0), ROTATED_MODE);

    CHECK_EQ(rotated_surface[0], 0x66);
    CHECK_EQ(rotated_surface[63], 0x66);
    CHECK_EQ(rotated_surface[64], ROTATED_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The four quadrant arms.  Each writes the same four step globals with its own
   signs folded in and stores |dx| and |dy| as the magnitudes the accumulators
   gather, so a blit of one pixel is enough to read the arm back out.  The
   expected values are the arm's own stores: sign(dx) for the pixel step in x,
   sign(dx) * pitch for the row step in y, -sign(dy) * pitch for the pixel step
   in y, and sign(dy) for the row step in x.  The pitch the arms fold in is the
   one the dispatcher published into data_fdps_graphics_rle_blit_dst_pitch. */
static void rotated_arm_dx_positive_dy_positive(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x01;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface + ROTATED_MIDDLE, 1, 1,
                       ROTATED_PITCH, ROTATED_OPERAND(0x1000, 0x1000),
                       ROTATED_MODE);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             ROTATED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             -ROTATED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

static void rotated_arm_dx_positive_dy_negative(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x02;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface + ROTATED_MIDDLE, 1, 1,
                       ROTATED_PITCH, ROTATED_OPERAND(0x1000, -0x1000),
                       ROTATED_MODE);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             ROTATED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             ROTATED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

/* This arm and the next are also what pin the sign test down as a signed one:
   read unsigned, a dx word of -0x1000 is 0xf000 and would take the positive
   arms above. */
static void rotated_arm_dx_negative_dy_positive(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x03;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface + ROTATED_MIDDLE, 1, 1,
                       ROTATED_PITCH, ROTATED_OPERAND(-0x1000, 0x1000),
                       ROTATED_MODE);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             -ROTATED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             -ROTATED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

static void rotated_arm_dx_negative_dy_negative(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x04;
    rotated_clear();
    fdps_blit_dispatch(stream, rotated_surface + ROTATED_MIDDLE, 1, 1,
                       ROTATED_PITCH, ROTATED_OPERAND(-0x1000, -0x1000),
                       ROTATED_MODE);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             -ROTATED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             ROTATED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

void run_rleturn_tests(void)
{
    RUN_TEST(rotated_unit_vector_draws_the_source_row);
    RUN_TEST(rotated_second_row_starts_one_pitch_down);
    RUN_TEST(rotated_quarter_turn_walks_a_column_upward);
    RUN_TEST(rotated_diagonal_step_paints_both_neighbours);
    RUN_TEST(rotated_oversized_step_advances_one_pixel_only);
    RUN_TEST(rotated_half_step_shrinks_the_run);
    RUN_TEST(rotated_row_accumulators_carry_between_rows);
    RUN_TEST(rotated_clears_the_row_accumulators_on_entry);
    RUN_TEST(rotated_skip_run_leaves_destination_alone);
    RUN_TEST(rotated_halftone_run_writes_second_of_each_pair);
    RUN_TEST(rotated_literal_run_copies_stream_bytes);
    RUN_TEST(rotated_run_length_tops_out_at_64);
    RUN_TEST(rotated_arm_dx_positive_dy_positive);
    RUN_TEST(rotated_arm_dx_positive_dy_negative);
    RUN_TEST(rotated_arm_dx_negative_dy_positive);
    RUN_TEST(rotated_arm_dx_negative_dy_negative);
}
