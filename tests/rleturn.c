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

/* --- fdps_rle_blit_rotated_scaled (00057114), blit mode 6 -------------------
 *
 * Expected values are read off the assembly at 00057114: the horizontal
 * counter is CMP BP,[src_width] / JAE with ADD BP,[dest_width] on the consume
 * side and SUB BP,[src_width] on the emit side, seeded from the destination
 * width at 00057236; the vertical one is CMP DX,[remaining_rows] / JA around
 * CALL fdps_rle_skip_row / ADD DX,[dest_height] at 000574c4..000574dd, seeded
 * from the destination height at 00057228; the row ends on DEC BX / JNZ and
 * the blit on DEC word ptr [dest_rows_remaining] / JNZ at 00057543.  The four
 * quadrant arms are the signed CMP DX,0 / JG at 0005714e and CMP AX,0 / JG at
 * 00057158 and 000571b9.
 *
 * Mode 6 is handed no row advance, so each case dispatches with the SOURCE
 * width and row count and the surface's own pitch, ROTSCALED_PITCH; the
 * dispatcher publishes all three and the kernel reads them back from the
 * globals.  The mode operand is loaded as a dword from the dispatcher's frame
 * at 00057114 (MOV EBP,[EBP+1Ch]) and used as a pointer to a four-dword
 * geometry record -- destination width, destination height, dx, dy -- of which
 * only the low word of each slot is read.  rotscaled_geometry is that record.
 *
 * The kernel never decrements data_fdps_graphics_rle_blit_remaining_rows; it
 * counts its own destination rows down in
 * data_fdps_graphics_rle_blit_dest_rows_remaining.  Same sentinel-filled
 * 16-byte-pitch surface as mode 5, its own copy so the two sections stay
 * independent.
 */
#define ROTSCALED_MODE     6
#define ROTSCALED_SENTINEL 0x5a
#define ROTSCALED_PITCH    16
#define ROTSCALED_MIDDLE   (8 * ROTSCALED_PITCH + 8)

static unsigned char rotscaled_surface[256];
static int rotscaled_geometry[4];

static void rotscaled_setup(int dest_width, int dest_height,
                            int rotate_dx, int rotate_dy)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof rotscaled_surface;
         byte_index++) {
        rotscaled_surface[byte_index] = ROTSCALED_SENTINEL;
    }
    rotscaled_geometry[0] = dest_width;
    rotscaled_geometry[1] = dest_height;
    rotscaled_geometry[2] = rotate_dx;
    rotscaled_geometry[3] = rotate_dy;
}

static void rotscaled_dispatch(unsigned char *stream, unsigned char *dest,
                               int src_width, int src_rows)
{
    fdps_blit_dispatch(stream, dest, src_width, src_rows, ROTSCALED_PITCH,
                       (unsigned int) rotscaled_geometry, ROTSCALED_MODE);
}

/* Source and destination the same size and a vector of (0x1000, 0): both
   counters run one-to-one and the kernel degenerates to a plain row copy.  The
   destination row count is decremented to zero, and the vertical accumulator's
   residue is dest_height, plus dest_height once for the one skip its JA makes,
   minus src_height: 1 + 1 - 1 = 1. */
static void rotated_scaled_unit_scale_draws_the_source_row(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    rotscaled_setup(4, 1, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface, 4, 1);

    CHECK_EQ(rotscaled_surface[0], 0xaa);
    CHECK_EQ(rotscaled_surface[3], 0xaa);
    CHECK_EQ(rotscaled_surface[4], ROTSCALED_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
    CHECK_EQ(data_fdps_graphics_rle_blit_vscale_accumulator, 1);
}

/* Two source columns into four destination pixels through a literal run: the
   emit path reads the current source byte with MOV AL,[ESI] at 000573cf
   without advancing, and the accumulator emits twice per consumed column, so
   each source byte lands twice. */
static void rotated_scaled_upscales_the_row_to_the_dest_width(void)
{
    unsigned char stream[3];

    stream[0] = 0x81;
    stream[1] = 0x11;
    stream[2] = 0x22;
    rotscaled_setup(4, 1, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface, 2, 1);

    CHECK_EQ(rotscaled_surface[0], 0x11);
    CHECK_EQ(rotscaled_surface[1], 0x11);
    CHECK_EQ(rotscaled_surface[2], 0x22);
    CHECK_EQ(rotscaled_surface[3], 0x22);
    CHECK_EQ(rotscaled_surface[4], ROTSCALED_SENTINEL);
}

/* Four source columns into two destination pixels.  The accumulator starts at
   the destination width, 2, below the source width, so the first column is
   consumed (INC ESI) before anything is emitted and the bytes that reach the
   surface are the second and the fourth. */
static void rotated_scaled_downscales_the_row_to_the_dest_width(void)
{
    unsigned char stream[5];

    stream[0] = 0x83;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    stream[4] = 0x44;
    rotscaled_setup(2, 1, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface, 4, 1);

    CHECK_EQ(rotscaled_surface[0], 0x22);
    CHECK_EQ(rotscaled_surface[1], 0x44);
    CHECK_EQ(rotscaled_surface[2], ROTSCALED_SENTINEL);
}

/* One source row into two destination rows.  The vertical accumulator is
   seeded with the destination height, 2, already above the source height, so
   the JA at 000574cb leaves without calling fdps_rle_skip_row and the second
   destination row re-decodes the same source row from the cursor POP ESI put
   back.  The third source row is never reached. */
static void rotated_scaled_taller_dest_redraws_the_source_row(void)
{
    unsigned char stream[6];

    stream[0] = 0x00;
    stream[1] = 0x31;
    stream[2] = 0x00;
    stream[3] = 0x32;
    stream[4] = 0x00;
    stream[5] = 0x33;
    rotscaled_setup(1, 2, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 1);

    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE], 0x31);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + ROTSCALED_PITCH], 0x31);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + 2 * ROTSCALED_PITCH],
             ROTSCALED_SENTINEL);
}

/* Four source rows into two destination rows.  After the first row the
   accumulator sits at 2 against a source height of 4, so it calls
   fdps_rle_skip_row twice before it climbs past: the second destination row
   draws source row 2 and rows 1 and 3 are dropped.  The stream carries five
   rows because the last pass walks past the last one drawn. */
static void rotated_scaled_shorter_dest_drops_source_rows(void)
{
    unsigned char stream[10];

    stream[0] = 0x00;
    stream[1] = 0x41;
    stream[2] = 0x00;
    stream[3] = 0x42;
    stream[4] = 0x00;
    stream[5] = 0x43;
    stream[6] = 0x00;
    stream[7] = 0x44;
    stream[8] = 0x00;
    stream[9] = 0x45;
    rotscaled_setup(1, 2, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 4);

    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE], 0x41);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + ROTSCALED_PITCH], 0x43);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + 2 * ROTSCALED_PITCH],
             ROTSCALED_SENTINEL);
}

/* A vector of (0, 0x1000) with both rectangles the same size: the horizontal
   accumulator never reaches 0x1000 and the vertical one carries on every
   destination pixel, so the row is painted up a column.  dx of zero is not
   greater than zero, so the JG at 00057152 falls into the negative arm and the
   x step is -1 even though nothing ever applies it. */
static void rotated_scaled_quarter_turn_walks_a_column_upward(void)
{
    unsigned char stream[2];

    stream[0] = 0x02;
    stream[1] = 0xbb;
    rotscaled_setup(3, 1, 0, 0x1000);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 3, 1);

    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE], 0xbb);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE - ROTSCALED_PITCH], 0xbb);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE - 2 * ROTSCALED_PITCH], 0xbb);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE - 3 * ROTSCALED_PITCH],
             ROTSCALED_SENTINEL);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + 1], ROTSCALED_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
}

/* The gap filler.  A vector of (0x1000, 0x1000) crosses a column and a row on
   the same destination pixel, and the store at 000572dc puts the pixel down at
   the cursor the horizontal carry has already moved, before the vertical step
   moves it again. */
static void rotated_scaled_diagonal_step_paints_both_neighbours(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0xcc;
    rotscaled_setup(1, 1, 0x1000, 0x1000);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 1);

    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE], 0xcc);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + 1], 0xcc);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + 1 - ROTSCALED_PITCH],
             ROTSCALED_SENTINEL);
}

/* One 0x1000 comes off the accumulator per destination pixel and no more (a
   single SUB DX,0x1000 at 000572ac), so a vector three whole pixels long
   still advances one byte per pixel.  The `accumulator >> 12` spelling would
   put the pixels on bytes 0, 3, 6 and 9, which is why bytes 1 and 6 are both
   asserted. */
static void rotated_scaled_oversized_step_advances_one_pixel_only(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xdd;
    rotscaled_setup(4, 1, 0x3000, 0);
    rotscaled_dispatch(stream, rotscaled_surface, 4, 1);

    CHECK_EQ(rotscaled_surface[0], 0xdd);
    CHECK_EQ(rotscaled_surface[1], 0xdd);
    CHECK_EQ(rotscaled_surface[2], 0xdd);
    CHECK_EQ(rotscaled_surface[3], 0xdd);
    CHECK_EQ(rotscaled_surface[4], ROTSCALED_SENTINEL);
    CHECK_EQ(rotscaled_surface[6], ROTSCALED_SENTINEL);
}

/* The row-to-row accumulators carry across destination rows.  |dy| is half a
   pixel, so the first row's contribution does not move the origin sideways
   and the second row's does: three one-pixel rows land at the middle, one
   pitch down, and two pitches down AND one byte across.  Only the per-pixel
   pair is zeroed at the top of every row (00057239, 00057242). */
static void rotated_scaled_row_accumulators_carry_between_rows(void)
{
    unsigned char stream[6];

    stream[0] = 0x00;
    stream[1] = 0x31;
    stream[2] = 0x00;
    stream[3] = 0x32;
    stream[4] = 0x00;
    stream[5] = 0x33;
    rotscaled_setup(1, 3, 0x1000, 0x800);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 3);

    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE], 0x31);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + ROTSCALED_PITCH], 0x32);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + 2 * ROTSCALED_PITCH + 1],
             0x33);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + 2 * ROTSCALED_PITCH],
             ROTSCALED_SENTINEL);
}

/* The row-to-row pair is cleared at 00057133 and 0005713c, before the first
   row.  The dispatcher never touches them, so loading both with 0xfff before
   the dispatch reaches the kernel: carried in, the first row advance would
   carry immediately and put the second row one byte further across. */
static void rotated_scaled_clears_the_row_accumulators_on_entry(void)
{
    unsigned char stream[6];

    stream[0] = 0x00;
    stream[1] = 0x41;
    stream[2] = 0x00;
    stream[3] = 0x42;
    stream[4] = 0x00;
    stream[5] = 0x43;
    rotscaled_setup(1, 2, 0x1000, 0x800);
    data_fdps_graphics_rle_blit_rot_row_step_x_accumulator = 0xfff;
    data_fdps_graphics_rle_blit_rot_row_step_y_accumulator = 0xfff;
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 2);

    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE], 0x41);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + ROTSCALED_PITCH], 0x42);
    CHECK_EQ(rotscaled_surface[ROTSCALED_MIDDLE + ROTSCALED_PITCH + 1],
             ROTSCALED_SENTINEL);
}

/* Op 11 is the one op with no store at all, not even the gap-filling one: the
   two destination pixels it covers keep the surface's own content and the
   fill that follows lands two bytes further on. */
static void rotated_scaled_skip_run_leaves_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x00;
    stream[2] = 0x77;
    rotscaled_setup(3, 1, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface, 3, 1);

    CHECK_EQ(rotscaled_surface[0], ROTSCALED_SENTINEL);
    CHECK_EQ(rotscaled_surface[1], ROTSCALED_SENTINEL);
    CHECK_EQ(rotscaled_surface[2], 0x77);
    CHECK_EQ(rotscaled_surface[3], ROTSCALED_SENTINEL);
}

/* Op 01: the phase starts at zero (XOR AH,AH at 000572fd) and flips only as a
   column is consumed, so the first destination pixel of the run is stepped
   over unwritten.  The length is doubled before the loop (SHL CX,1 at
   000572f9), which is what makes a length-one run cover a two-pixel row. */
static void rotated_scaled_halftone_run_writes_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x40;
    stream[1] = 0x99;
    rotscaled_setup(2, 1, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface, 2, 1);

    CHECK_EQ(rotscaled_surface[0], ROTSCALED_SENTINEL);
    CHECK_EQ(rotscaled_surface[1], 0x99);
    CHECK_EQ(rotscaled_surface[2], ROTSCALED_SENTINEL);
}

/* The length is the low six bits plus one -- SHR CL,2 / INC CL at 0005726a --
   so 0x3f is 64 and no longer run can be encoded. */
static void rotated_scaled_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0x66;
    rotscaled_setup(64, 1, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface, 64, 1);

    CHECK_EQ(rotscaled_surface[0], 0x66);
    CHECK_EQ(rotscaled_surface[63], 0x66);
    CHECK_EQ(rotscaled_surface[64], ROTSCALED_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
}

/* The destination row filling before the run does.  A four-wide source row
   squeezed into two destination pixels ends on DEC BX / JNZ with one unit of
   the fill still uncounted, and the row is abandoned there.  What
   resynchronises the stream is POP ESI plus the one fdps_rle_skip_row call the
   vertical step makes, and the proof is that the second destination row draws
   0x52 rather than a byte read out of the middle of row 0. */
static void rotated_scaled_row_ends_early_and_stream_resyncs(void)
{
    unsigned char stream[6];

    stream[0] = 0x03;
    stream[1] = 0x51;
    stream[2] = 0x03;
    stream[3] = 0x52;
    stream[4] = 0x03;
    stream[5] = 0x53;
    rotscaled_setup(2, 2, 0x1000, 0);
    rotscaled_dispatch(stream, rotscaled_surface, 4, 2);

    CHECK_EQ(rotscaled_surface[0], 0x51);
    CHECK_EQ(rotscaled_surface[1], 0x51);
    CHECK_EQ(rotscaled_surface[2], ROTSCALED_SENTINEL);
    CHECK_EQ(rotscaled_surface[ROTSCALED_PITCH], 0x52);
    CHECK_EQ(rotscaled_surface[ROTSCALED_PITCH + 1], 0x52);
    CHECK_EQ(rotscaled_surface[ROTSCALED_PITCH + 2], ROTSCALED_SENTINEL);
}

/* The four quadrant arms at 000571eb, 000571c3, 0005718f and 00057162.  Each
   writes the same four step globals with its own signs folded in and stores
   |dx| and |dy| as the magnitudes, so a one-pixel blit is enough to read the
   arm back out: sign(dx) for the pixel step in x, sign(dx) * pitch for the row
   step in y, -sign(dy) * pitch for the pixel step in y, and sign(dy) for the
   row step in x.  The pitch folded in is the one the dispatcher published. */
static void rotated_scaled_arm_dx_positive_dy_positive(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x01;
    rotscaled_setup(1, 1, 0x1000, 0x1000);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 1);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             ROTSCALED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             -ROTSCALED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

static void rotated_scaled_arm_dx_positive_dy_negative(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x02;
    rotscaled_setup(1, 1, 0x1000, -0x1000);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 1);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             ROTSCALED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             ROTSCALED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

/* This arm and the next also pin the sign test down as a signed one and the
   record's slots as sixteen bits wide: the slot holds a whole negative int,
   only its low word 0xf000 is read, and read unsigned it would take the
   positive arms above. */
static void rotated_scaled_arm_dx_negative_dy_positive(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x03;
    rotscaled_setup(1, 1, -0x1000, 0x1000);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 1);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             -ROTSCALED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             -ROTSCALED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_cos_magnitude, 0x1000);
    CHECK_EQ(data_fdps_graphics_rle_rotate_sin_magnitude, 0x1000);
}

static void rotated_scaled_arm_dx_negative_dy_negative(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0x04;
    rotscaled_setup(1, 1, -0x1000, -0x1000);
    rotscaled_dispatch(stream, rotscaled_surface + ROTSCALED_MIDDLE, 1, 1);

    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, -1);
    CHECK_EQ(data_fdps_graphics_rle_rotate_dst_y_step_per_src_y,
             -ROTSCALED_PITCH);
    CHECK_EQ(data_fdps_graphics_rle_blit_rotated_src_pixel_step_y,
             ROTSCALED_PITCH);
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
    RUN_TEST(rotated_scaled_unit_scale_draws_the_source_row);
    RUN_TEST(rotated_scaled_upscales_the_row_to_the_dest_width);
    RUN_TEST(rotated_scaled_downscales_the_row_to_the_dest_width);
    RUN_TEST(rotated_scaled_taller_dest_redraws_the_source_row);
    RUN_TEST(rotated_scaled_shorter_dest_drops_source_rows);
    RUN_TEST(rotated_scaled_quarter_turn_walks_a_column_upward);
    RUN_TEST(rotated_scaled_diagonal_step_paints_both_neighbours);
    RUN_TEST(rotated_scaled_oversized_step_advances_one_pixel_only);
    RUN_TEST(rotated_scaled_row_accumulators_carry_between_rows);
    RUN_TEST(rotated_scaled_clears_the_row_accumulators_on_entry);
    RUN_TEST(rotated_scaled_skip_run_leaves_destination_alone);
    RUN_TEST(rotated_scaled_halftone_run_writes_second_of_each_pair);
    RUN_TEST(rotated_scaled_run_length_tops_out_at_64);
    RUN_TEST(rotated_scaled_row_ends_early_and_stream_resyncs);
    RUN_TEST(rotated_scaled_arm_dx_positive_dy_positive);
    RUN_TEST(rotated_scaled_arm_dx_positive_dy_negative);
    RUN_TEST(rotated_scaled_arm_dx_negative_dy_positive);
    RUN_TEST(rotated_scaled_arm_dx_negative_dy_negative);
}
