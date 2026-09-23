/* tests/rlebase.c -- cover for src/rlebase.asm: the pass-through, scaled, row-skipping and mirroring kernels.
 *
 * The kernels in rlebase.asm are hand-written assembly transcribed from the
 * original with no C interface -- they take their inputs in registers and out
 * of fdps_blit_dispatch's own stack frame -- so every case here calls
 * fdps_blit_dispatch with the kernel's mode.  The cases carry over every
 * situation the C-translation tests in tests/rle.c verify (those are kept
 * for reference under #if 0), and none of them depends on which spelling of
 * the kernels is linked (rebuild_info/emit_pipeline.md).
 */
#include "testharn.h"
#include "blit.h"
#include "gamedata.h"

/* --- fdps_rle_blit_passthrough (00056a0d), blit mode 0 ----------------------
 *
 * Expected values are read off the assembly: the op selector is SHL CL,1 / JC
 * taken twice, the length is SHR CL,2 / INC CL, the row ends on OR BX,BX / JNZ
 * and the row advance is the ADD EDI,EDX at 00056a81, where EDX is the
 * dispatcher's pitch - width.  So a width w and advance a is a dispatch with
 * src_width w and dest_pitch a + w.  The destination is pre-filled with
 * PASS_SENTINEL so a byte the kernel is meant to leave alone can be told apart
 * from one it wrote; the mode operand is not read by mode 0 and is passed 0.
 */
#define PASS_MODE     0
#define PASS_SENTINEL 0x5a

static unsigned char pass_surface[80];

static void pass_clear(void)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof pass_surface; byte_index++) {
        pass_surface[byte_index] = PASS_SENTINEL;
    }
}

/* Op 00, length (0x03 & 0x3f) + 1 = 4: one pixel byte follows and is written
   over four destination bytes by REP STOSB.  The fifth byte is outside the run
   and outside the row, and the kernel's DEC of the row count leaves it 0. */
static void pass_fill_run_writes_len_bytes(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface, 4, 1, 4, 0, PASS_MODE);

    CHECK_EQ(pass_surface[0], 0xaa);
    CHECK_EQ(pass_surface[3], 0xaa);
    CHECK_EQ(pass_surface[4], PASS_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 10 (0x82, length 3): REP MOVSB, so the three bytes after the command byte
   reach the destination in order and unchanged. */
static void pass_literal_run_copies_stream_bytes(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface, 3, 1, 3, 0, PASS_MODE);

    CHECK_EQ(pass_surface[0], 0x11);
    CHECK_EQ(pass_surface[1], 0x22);
    CHECK_EQ(pass_surface[2], 0x33);
    CHECK_EQ(pass_surface[3], PASS_SENTINEL);
}

/* Op 11 (0xc1, length 2) is ADD EDI,ECX with no write at all: the two bytes it
   covers keep the surface's own content and the fill that follows (0x01,
   length 2) lands two bytes further on. */
static void pass_skip_run_leaves_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = 0x77;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface, 4, 1, 4, 0, PASS_MODE);

    CHECK_EQ(pass_surface[0], PASS_SENTINEL);
    CHECK_EQ(pass_surface[1], PASS_SENTINEL);
    CHECK_EQ(pass_surface[2], 0x77);
    CHECK_EQ(pass_surface[3], 0x77);
    CHECK_EQ(pass_surface[4], PASS_SENTINEL);
}

/* Op 01 (0x41, length 2): INC EDI then STOSB, twice, so bytes 1 and 3 are
   written and bytes 0 and 2 are left as they were -- not a stretch that
   doubles each pixel.  The row is four bytes wide because the op subtracts the
   length twice (SUB BX,CX at 00056a41 and 00056a44). */
static void pass_stretched_run_writes_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = 0x99;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface, 4, 1, 4, 0, PASS_MODE);

    CHECK_EQ(pass_surface[0], PASS_SENTINEL);
    CHECK_EQ(pass_surface[1], 0x99);
    CHECK_EQ(pass_surface[2], PASS_SENTINEL);
    CHECK_EQ(pass_surface[3], 0x99);
    CHECK_EQ(pass_surface[4], PASS_SENTINEL);
}

/* A literal run (0x81, length 2) then a length-1 op 01 (0x40) leaves exactly
   one untouched column between two painted ones. */
static void pass_stretched_run_punches_one_pixel_hole(void)
{
    unsigned char stream[5];

    stream[0] = 0x81;
    stream[1] = 0x21;
    stream[2] = 0x22;
    stream[3] = 0x40;
    stream[4] = 0x33;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface, 4, 1, 4, 0, PASS_MODE);

    CHECK_EQ(pass_surface[0], 0x21);
    CHECK_EQ(pass_surface[1], 0x22);
    CHECK_EQ(pass_surface[2], PASS_SENTINEL);
    CHECK_EQ(pass_surface[3], 0x33);
}

/* All four ops in one row, each at length 1: 0x00 fill, 0x40 stretched, 0x80
   literal, 0xc0 skip.  The widths they consume are 1, 2, 1 and 1, so the row
   reaches exactly five and the skip is the op that ends it. */
static void pass_all_four_ops_in_one_row(void)
{
    unsigned char stream[7];

    stream[0] = 0x00;
    stream[1] = 0xa1;
    stream[2] = 0x40;
    stream[3] = 0xb2;
    stream[4] = 0x80;
    stream[5] = 0xc3;
    stream[6] = 0xc0;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface, 5, 1, 5, 0, PASS_MODE);

    CHECK_EQ(pass_surface[0], 0xa1);
    CHECK_EQ(pass_surface[1], PASS_SENTINEL);
    CHECK_EQ(pass_surface[2], 0xb2);
    CHECK_EQ(pass_surface[3], 0xc3);
    CHECK_EQ(pass_surface[4], PASS_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* 0x3f is the widest run the six length bits carry: (0x3f & 0x3f) + 1 is 64,
   not 63. */
static void pass_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0xcc;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface, 64, 1, 64, 0, PASS_MODE);

    CHECK_EQ(pass_surface[0], 0xcc);
    CHECK_EQ(pass_surface[63], 0xcc);
    CHECK_EQ(pass_surface[64], PASS_SENTINEL);
}

/* Two rows, width 2 in a pitch of 4: the dispatcher's advance of 2 steps the
   destination over the part of the scanline the sprite does not cover.  The
   width is re-read at the top of the second row (MOV BX at 00056a0d), and the
   row count comes back at zero. */
static void pass_second_row_starts_after_row_advance(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface, 2, 2, 4, 0, PASS_MODE);

    CHECK_EQ(pass_surface[0], 0xaa);
    CHECK_EQ(pass_surface[1], 0xaa);
    CHECK_EQ(pass_surface[2], PASS_SENTINEL);
    CHECK_EQ(pass_surface[3], PASS_SENTINEL);
    CHECK_EQ(pass_surface[4], 0xbb);
    CHECK_EQ(pass_surface[5], 0xbb);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The row advance is signed (ADD EDI,EDX in 32 bits).  A 2x2 image drawn at
   surface + 2 with a pitch of -2 gets the advance -2 - 2 = -4, so after the
   first row ends at surface + 4 the second row starts at surface + 0: the
   first source row lands on the lower line. */
static void pass_negative_row_advance_walks_upward(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    pass_clear();
    fdps_blit_dispatch(stream, pass_surface + 2, 2, 2, -2, 0, PASS_MODE);

    CHECK_EQ(pass_surface[2], 0xaa);
    CHECK_EQ(pass_surface[3], 0xaa);
    CHECK_EQ(pass_surface[0], 0xbb);
    CHECK_EQ(pass_surface[1], 0xbb);
}

void run_rlebase_tests(void)
{
    RUN_TEST(pass_fill_run_writes_len_bytes);
    RUN_TEST(pass_literal_run_copies_stream_bytes);
    RUN_TEST(pass_skip_run_leaves_destination_alone);
    RUN_TEST(pass_stretched_run_writes_second_of_each_pair);
    RUN_TEST(pass_stretched_run_punches_one_pixel_hole);
    RUN_TEST(pass_all_four_ops_in_one_row);
    RUN_TEST(pass_run_length_tops_out_at_64);
    RUN_TEST(pass_second_row_starts_after_row_advance);
    RUN_TEST(pass_negative_row_advance_walks_upward);
}
