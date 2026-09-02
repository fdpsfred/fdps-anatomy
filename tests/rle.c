/* tests/rle.c -- cover for src/rle.c.
 *
 * Every expected value is read off the assembly at 00056a0d: the op selector
 * is SHL CL,1 / JC taken twice, the length is SHR CL,2 / INC CL, the row ends
 * on OR BX,BX / JNZ and the row advance is the ADD EDI,EDX at 00056a81.  None
 * of them is taken from the emitted C.
 *
 * The destination is always pre-filled with 0x5a, so a byte the kernel is
 * meant to leave alone can be told apart from one it wrote.  That sentinel is
 * what the op 01 and op 11 cases actually test.
 */
#include "testharn.h"
#include "gamedata.h"
#include "rle.h"

#define SENTINEL 0x5a

static unsigned char dest_surface[80];

/* The two globals are inputs here, written the way fdps_blit_dispatch writes
   them at 000568f8 and 00056903 before it calls the kernel.  Their real
   contents belong to no one -- they are scratch -- so nothing below asserts
   what they hold on the way in, only that the kernel leaves the row count at
   zero, which is its own doing (DEC word ptr [0x00070022] until JNZ falls
   through). */
static void blit_setup(unsigned short src_width, unsigned short rows)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof dest_surface; byte_index++) {
        dest_surface[byte_index] = SENTINEL;
    }
    data_fdps_graphics_rle_blit_src_width = src_width;
    data_fdps_graphics_rle_blit_remaining_rows = rows;
}

/* Op 00, length (0x03 & 0x3f) + 1 = 4: one pixel byte follows and is written
   over four destination bytes.  The fifth byte is outside the run and outside
   the row. */
static void rle_fill_run_writes_len_bytes(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    blit_setup(4, 1);
    fdps_rle_blit_passthrough(stream, dest_surface, 0);

    CHECK_EQ(dest_surface[0], 0xaa);
    CHECK_EQ(dest_surface[3], 0xaa);
    CHECK_EQ(dest_surface[4], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 10 (0x82): REP MOVSB, so the three bytes after the command byte reach the
   destination in order and unchanged. */
static void rle_literal_run_copies_stream_bytes(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    blit_setup(3, 1);
    fdps_rle_blit_passthrough(stream, dest_surface, 0);

    CHECK_EQ(dest_surface[0], 0x11);
    CHECK_EQ(dest_surface[1], 0x22);
    CHECK_EQ(dest_surface[2], 0x33);
    CHECK_EQ(dest_surface[3], SENTINEL);
}

/* Op 11 (0xc1, length 2) is ADD EDI,ECX with no write at all: the two bytes it
   covers keep the surface's own content and the fill that follows lands two
   bytes further on. */
static void rle_skip_run_leaves_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = 0x77;
    blit_setup(4, 1);
    fdps_rle_blit_passthrough(stream, dest_surface, 0);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], SENTINEL);
    CHECK_EQ(dest_surface[2], 0x77);
    CHECK_EQ(dest_surface[3], 0x77);
    CHECK_EQ(dest_surface[4], SENTINEL);
}

/* Op 01 (0x41, length 2): INC EDI then STOSB, twice, so bytes 1 and 3 are
   written and bytes 0 and 2 are left as they were.  This is the case that
   would come out wrong if the op were read as a stretch that doubles each
   pixel -- that spelling paints all four bytes.  The row is four bytes wide
   because the op subtracts the length twice (SUB BX,CX at 00056a41 and
   00056a44). */
static void rle_stretched_run_writes_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = 0x99;
    blit_setup(4, 1);
    fdps_rle_blit_passthrough(stream, dest_surface, 0);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], 0x99);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], 0x99);
    CHECK_EQ(dest_surface[4], SENTINEL);
}

/* The one-pixel hole the plate comment measured on the shipped .CEL sheets: a
   literal run, then a length-1 op 01, leaves exactly one untouched column
   between two painted ones. */
static void rle_stretched_run_punches_one_pixel_hole(void)
{
    unsigned char stream[5];

    stream[0] = 0x81;
    stream[1] = 0x21;
    stream[2] = 0x22;
    stream[3] = 0x40;
    stream[4] = 0x33;
    blit_setup(4, 1);
    fdps_rle_blit_passthrough(stream, dest_surface, 0);

    CHECK_EQ(dest_surface[0], 0x21);
    CHECK_EQ(dest_surface[1], 0x22);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], 0x33);
}

/* All four ops in one row, each at length 1, which is the selector's own test:
   0x00 fill, 0x40 stretched, 0x80 literal, 0xc0 skip.  The widths they consume
   are 1, 2, 1 and 1, which is what makes the row exactly five pixels. */
static void rle_all_four_ops_in_one_row(void)
{
    unsigned char stream[7];

    stream[0] = 0x00;
    stream[1] = 0xa1;
    stream[2] = 0x40;
    stream[3] = 0xb2;
    stream[4] = 0x80;
    stream[5] = 0xc3;
    stream[6] = 0xc0;
    blit_setup(5, 1);
    fdps_rle_blit_passthrough(stream, dest_surface, 0);

    CHECK_EQ(dest_surface[0], 0xa1);
    CHECK_EQ(dest_surface[1], SENTINEL);
    CHECK_EQ(dest_surface[2], 0xb2);
    CHECK_EQ(dest_surface[3], 0xc3);
    CHECK_EQ(dest_surface[4], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* 0x3f is the widest run the six length bits can carry: (0x3f & 0x3f) + 1 is
   64, not 63 and not 0x3f. */
static void rle_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0xcc;
    blit_setup(64, 1);
    fdps_rle_blit_passthrough(stream, dest_surface, 0);

    CHECK_EQ(dest_surface[0], 0xcc);
    CHECK_EQ(dest_surface[63], 0xcc);
    CHECK_EQ(dest_surface[64], SENTINEL);
}

/* Two rows, and the row advance stepping the destination over the part of the
   scanline the sprite does not cover: width 2 in a pitch of 4 gives the
   advance of 2 that fdps_blit_dispatch computes as pitch - width.  The width
   is re-read at the top of the second row, and the row count comes back at
   zero. */
static void rle_second_row_starts_after_row_advance(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    blit_setup(2, 2);
    fdps_rle_blit_passthrough(stream, dest_surface, 2);

    CHECK_EQ(dest_surface[0], 0xaa);
    CHECK_EQ(dest_surface[1], 0xaa);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], SENTINEL);
    CHECK_EQ(dest_surface[4], 0xbb);
    CHECK_EQ(dest_surface[5], 0xbb);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The row advance is signed, which is the whole of how blit mode 8 draws
   bottom-up: fdps_rle_blit_mirrored_vertical points the destination at the
   last raster line and reaches this decoder with NEG EDX (00057614).  Here the
   image is 2x2 in a pitch of 2, so the advance is -(2 + 2) and the first
   source row lands on the bottom line. */
static void rle_negative_row_advance_walks_upward(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    blit_setup(2, 2);
    fdps_rle_blit_passthrough(stream, dest_surface + 2, -4);

    CHECK_EQ(dest_surface[2], 0xaa);
    CHECK_EQ(dest_surface[3], 0xaa);
    CHECK_EQ(dest_surface[0], 0xbb);
    CHECK_EQ(dest_surface[1], 0xbb);
}

void run_rle_tests(void)
{
    RUN_TEST(rle_fill_run_writes_len_bytes);
    RUN_TEST(rle_literal_run_copies_stream_bytes);
    RUN_TEST(rle_skip_run_leaves_destination_alone);
    RUN_TEST(rle_stretched_run_writes_second_of_each_pair);
    RUN_TEST(rle_stretched_run_punches_one_pixel_hole);
    RUN_TEST(rle_all_four_ops_in_one_row);
    RUN_TEST(rle_run_length_tops_out_at_64);
    RUN_TEST(rle_second_row_starts_after_row_advance);
    RUN_TEST(rle_negative_row_advance_walks_upward);
}
