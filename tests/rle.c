/* tests/rle.c -- cover for src/rle.c.
 *
 * Every expected value is read off the assembly at 00056a0d and 00056dc9: the
 * op selector is SHL CL,1 / JC taken twice, the length is SHR CL,2 / INC CL,
 * the row ends on OR BX,BX / JNZ and the row advance is the ADD EDI,EDX at
 * 00056a81.  None of them is taken from the emitted C.
 *
 * For the drawing kernel the destination is always pre-filled with 0x5a, so a
 * byte it is meant to leave alone can be told apart from one it wrote.  That
 * sentinel is what the op 01 and op 11 cases actually test.  The row-skipping
 * routine draws nothing, so its cases assert how far the stream cursor moved
 * instead.
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

/* --- fdps_rle_blit_scaled (00056c5e) ----------------------------------------
 *
 * The scaled kernel's inputs are split three ways: the source rectangle is the
 * two globals the pass-through kernel reads (the row count is the source
 * HEIGHT here and is not decremented), the destination pitch is a third
 * global, and the destination width and height are parameters, because the
 * original reads them out of fdps_blit_dispatch's frame.  scaled_setup below
 * writes the three globals; every case then passes the two scale words.
 *
 * Expected values come from the Bresenham step at 00056cca and its three
 * copies: the accumulator starts at the destination width, an accumulator
 * below the source width consumes one source pixel and adds the destination
 * width, and otherwise one destination pixel is emitted and the source width
 * subtracted.  Every stream is padded with 0xc0, a length-1 skip, so a decoder
 * that gets a length or a byte cost wrong walks forward through padding
 * instead of off the end of the buffer.
 */
static void scaled_setup(unsigned short src_width, unsigned short src_height,
                         unsigned short dst_pitch)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof dest_surface; byte_index++) {
        dest_surface[byte_index] = SENTINEL;
    }
    data_fdps_graphics_rle_blit_src_width = src_width;
    data_fdps_graphics_rle_blit_remaining_rows = src_height;
    data_fdps_graphics_rle_blit_dst_pitch = dst_pitch;
}

/* A 4x1 source drawn at 4x1: both accumulators divide out exactly and the row
   is the same four pixels the pass-through kernel would draw.  This is also
   where the parameter block the routine publishes is pinned -- the two scale
   words into 00070028 and 0007002a, the row advance as pitch - width, and the
   row counter consumed to zero -- and the vertical accumulator, which is
   seeded with the destination height, ends at 1 + 1 - 1. */
static void scaled_one_to_one_draws_the_source_row(void)
{
    unsigned char stream[4];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    scaled_setup(4, 1, 8);
    fdps_rle_blit_scaled(stream, dest_surface, 4, 1);

    CHECK_EQ(dest_surface[0], 0xaa);
    CHECK_EQ(dest_surface[3], 0xaa);
    CHECK_EQ(dest_surface[4], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_width, 4);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_height, 1);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 4);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
    CHECK_EQ(data_fdps_graphics_rle_blit_vscale_accumulator, 1);
}

/* Horizontal upscale, a 2-pixel source row drawn 4 wide: the accumulator
   starts at 4, so each of the two literal bytes is emitted twice before the
   next is consumed. */
static void scaled_upscale_repeats_each_source_pixel(void)
{
    unsigned char stream[6];

    stream[0] = 0x81;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0xc0;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    scaled_setup(2, 1, 4);
    fdps_rle_blit_scaled(stream, dest_surface, 4, 1);

    CHECK_EQ(dest_surface[0], 0x11);
    CHECK_EQ(dest_surface[1], 0x11);
    CHECK_EQ(dest_surface[2], 0x22);
    CHECK_EQ(dest_surface[3], 0x22);
    CHECK_EQ(dest_surface[4], SENTINEL);
}

/* Horizontal downscale, a 4-pixel source row drawn 2 wide.  The accumulator
   starts at 2, below the source width of 4, so the FIRST thing the loop does
   is consume a source pixel rather than emit one: 0x11 and 0x33 are dropped
   and 0x22 and 0x44 are what land.  A decoder that emitted before consuming
   would write 0x11 and 0x33 instead. */
static void scaled_downscale_drops_source_pixels(void)
{
    unsigned char stream[7];

    stream[0] = 0x83;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    stream[4] = 0x44;
    stream[5] = 0xc0;
    stream[6] = 0xc0;
    scaled_setup(4, 1, 2);
    fdps_rle_blit_scaled(stream, dest_surface, 2, 1);

    CHECK_EQ(dest_surface[0], 0x22);
    CHECK_EQ(dest_surface[1], 0x44);
    CHECK_EQ(dest_surface[2], SENTINEL);
}

/* Op 11 at 1:1 -- INC EDI with no write at 00056d7f -- so the two pixels it
   covers keep the surface's own content and the fill that follows lands two
   bytes further on. */
static void scaled_skip_run_leaves_destination_alone(void)
{
    unsigned char stream[5];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = 0x77;
    stream[3] = 0xc0;
    stream[4] = 0xc0;
    scaled_setup(4, 1, 4);
    fdps_rle_blit_scaled(stream, dest_surface, 4, 1);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], SENTINEL);
    CHECK_EQ(dest_surface[2], 0x77);
    CHECK_EQ(dest_surface[3], 0x77);
    CHECK_EQ(dest_surface[4], SENTINEL);
}

/* Op 01 at 1:1 (0x41, length 2 doubled to 4 by SHL CX,1): the phase starts at
   zero and flips only when a source pixel is consumed, so the run covers four
   columns and writes the second of each pair.  Without the doubling the run
   would end after two columns and the next command byte would be read from the
   padding. */
static void scaled_halftone_run_writes_second_of_each_pair(void)
{
    unsigned char stream[5];

    stream[0] = 0x41;
    stream[1] = 0x99;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    stream[4] = 0xc0;
    scaled_setup(4, 1, 4);
    fdps_rle_blit_scaled(stream, dest_surface, 4, 1);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], 0x99);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], 0x99);
    CHECK_EQ(dest_surface[4], SENTINEL);
}

/* Vertical upscale: one source row drawn as two destination rows.  The source
   cursor is popped back to the row start, and the vertical accumulator --
   seeded with the destination height of 2, which is above the source height of
   1 -- skips no source row before the second pass, so the same encoded row is
   decoded twice.  The accumulator ends at 2 - 1, then 1 + 2 - 1.

   The advance is added to a cursor that already sits at the end of the row it
   just drew (ADD EDI,[0x00070030] at 00056db5, after the row's own writes have
   moved EDI), so a 2-wide destination in a pitch of 4 puts the second row at
   byte 4 and leaves bytes 2 and 3 untouched. */
static void scaled_vertical_upscale_redraws_the_source_row(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    scaled_setup(2, 1, 4);
    fdps_rle_blit_scaled(stream, dest_surface, 2, 2);

    CHECK_EQ(dest_surface[0], 0xaa);
    CHECK_EQ(dest_surface[1], 0xaa);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], SENTINEL);
    CHECK_EQ(dest_surface[4], 0xaa);
    CHECK_EQ(dest_surface[5], 0xaa);
    CHECK_EQ(dest_surface[6], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_vscale_accumulator, 2);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
}

/* Vertical downscale: two source rows, one destination row.  The accumulator
   starts at 1, which is at or below the source height of 2, so
   fdps_rle_skip_row is called twice after the only row is drawn and the second
   source row is never decoded for its pixels -- 0xbb reaches nothing. */
static void scaled_vertical_downscale_drops_a_source_row(void)
{
    unsigned char stream[6];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    scaled_setup(2, 2, 4);
    fdps_rle_blit_scaled(stream, dest_surface, 2, 1);

    CHECK_EQ(dest_surface[0], 0xaa);
    CHECK_EQ(dest_surface[1], 0xaa);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
}

/* The row advance is computed sixteen bits wide and stored zero-extended into
   the dword (SUB BP,[0x00070028] then MOV [0x00070030],EBP with EBP's top half
   zeroed at 00056c74).  A destination four wide in a pitch of two therefore
   stores 65534, not -2, which is what the same subtraction written as a signed
   int would give.  The four destination pixels come from the single source
   pixel of a 1-wide row. */
static void scaled_row_advance_is_zero_extended(void)
{
    unsigned char stream[4];

    stream[0] = 0x00;
    stream[1] = 0x77;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    scaled_setup(1, 1, 2);
    fdps_rle_blit_scaled(stream, dest_surface, 4, 1);

    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 65534L);
    CHECK_EQ(dest_surface[0], 0x77);
    CHECK_EQ(dest_surface[3], 0x77);
    CHECK_EQ(dest_surface[4], SENTINEL);
}

/* The case the push and pop of the source cursor exist for: a 4-wide source
   drawn 2 wide leaves each row's literal run half decoded, with the cursor
   sitting on the third of its four pixel bytes.  The row still ends at the
   next row's first command byte, because the cursor is popped back to the row
   start and fdps_rle_skip_row walks the whole encoded row.  A decoder that
   carried the cursor on from where it stopped would read 0x44 as the second
   row's command byte and draw something else entirely.

   The second destination row starts at byte 4, not byte 2: the row advance of
   pitch - width is added to a cursor already standing at the end of the row it
   drew. */
static void scaled_partial_row_resyncs_through_skip_row(void)
{
    unsigned char stream[12];

    stream[0] = 0x83;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    stream[4] = 0x44;
    stream[5] = 0x83;
    stream[6] = 0x55;
    stream[7] = 0x66;
    stream[8] = 0x77;
    stream[9] = 0x88;
    stream[10] = 0xc0;
    stream[11] = 0xc0;
    scaled_setup(4, 2, 4);
    fdps_rle_blit_scaled(stream, dest_surface, 2, 2);

    CHECK_EQ(dest_surface[0], 0x22);
    CHECK_EQ(dest_surface[1], 0x44);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], SENTINEL);
    CHECK_EQ(dest_surface[4], 0x66);
    CHECK_EQ(dest_surface[5], 0x88);
    CHECK_EQ(dest_surface[6], SENTINEL);
}

/* --- fdps_rle_skip_row (00056dc9) -------------------------------------------
 *
 * The routine writes nothing, so what every case below pins is the two things
 * it does produce: how far the stream cursor moved, and that the row ended
 * where the width says it does.  A wrong per-op byte cost or a wrong width
 * charge shows up as an advance that is not the expected one, because the
 * padding after each stream is 0xc0 -- a length-1 skip -- which keeps a
 * decoder that has not finished the row walking forward instead of running
 * off into whatever follows.
 *
 * Expected advances are read off the assembly: LODSB plus INC ESI for fill
 * (00056deb) and for stretched (00056df7), LODSB plus ADD ESI,ECX for a
 * literal (00056e14), and LODSB alone for a skip.  The width charge is the
 * SUB BX,CX in each arm, twice in the stretched one (00056df8, 00056dfb).
 */

/* A row-of-many-ops buffer for the two wrap cases below, which need over a
   thousand command bytes to bring a wrapped sixteen-bit counter back to zero.
   1026 is what the first of them uses exactly. */
static unsigned char wrap_stream[1026];

/* Op 00 (0x03, length 4): the command byte and the single pixel byte that
   follows it, so the cursor lands two bytes on, and the run accounts for the
   whole four-pixel row. */
static void skip_fill_run_advances_two_bytes(void)
{
    unsigned char stream[4];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    data_fdps_graphics_rle_blit_src_width = 4;

    CHECK_EQ((long) (fdps_rle_skip_row(stream) - stream), 2);
}

/* Op 01 (0x41, length 2): also two bytes, but the width it accounts for is
   twice its length -- four columns, not two.  The row is four wide, so an arm
   that subtracted the length once would leave two pixels owing and walk on
   into the padding instead of stopping at 2. */
static void skip_stretched_run_consumes_two_columns_per_unit(void)
{
    unsigned char stream[4];

    stream[0] = 0x41;
    stream[1] = 0x99;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    data_fdps_graphics_rle_blit_src_width = 4;

    CHECK_EQ((long) (fdps_rle_skip_row(stream) - stream), 2);
}

/* Op 10 (0x82, length 3): the command byte plus the three pixel bytes it
   carries, so the cursor lands four bytes on. */
static void skip_literal_run_advances_past_its_pixels(void)
{
    unsigned char stream[6];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    data_fdps_graphics_rle_blit_src_width = 3;

    CHECK_EQ((long) (fdps_rle_skip_row(stream) - stream), 4);
}

/* Op 11 (0xc1, length 2): nothing follows the command byte, so a two-pixel
   run costs one byte. */
static void skip_transparent_run_advances_one_byte(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0xc0;
    stream[2] = 0xc0;
    data_fdps_graphics_rle_blit_src_width = 2;

    CHECK_EQ((long) (fdps_rle_skip_row(stream) - stream), 1);
}

/* All four ops at length 1 in one row: the widths they account for are 1, 2,
   1 and 1, which is the five-pixel row, and the bytes they cost are 2, 2, 2
   and 1, which is the seven-byte advance.  This is the same stream the
   pass-through case above draws, so the two agree on where the row ends. */
static void skip_row_walks_every_op_to_exact_width(void)
{
    unsigned char stream[9];

    stream[0] = 0x00;
    stream[1] = 0xa1;
    stream[2] = 0x40;
    stream[3] = 0xb2;
    stream[4] = 0x80;
    stream[5] = 0xc3;
    stream[6] = 0xc0;
    stream[7] = 0xc0;
    stream[8] = 0xc0;
    data_fdps_graphics_rle_blit_src_width = 5;

    CHECK_EQ((long) (fdps_rle_skip_row(stream) - stream), 7);
}

/* SHR CL,2 / INC CL makes 0x3f a length of 64.  A row exactly 64 wide is
   therefore one op, and a decoder that read the length as 63 would not end
   the row here. */
static void skip_run_length_tops_out_at_64(void)
{
    unsigned char stream[4];

    stream[0] = 0x3f;
    stream[1] = 0xcc;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    data_fdps_graphics_rle_blit_src_width = 64;

    CHECK_EQ((long) (fdps_rle_skip_row(stream) - stream), 2);
}

/* The widest literal, 0xbf: 64 pixel bytes after the command byte, so the
   cursor lands 65 bytes on.  This is the arm that adds the length to the
   pointer (ADD ESI,ECX at 00056e14) rather than stepping it by a constant,
   and 64 is the largest step it can take. */
static void skip_literal_run_at_max_length_advances_65(void)
{
    static unsigned char stream[68];
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof stream; byte_index++) {
        stream[byte_index] = 0xc0;
    }
    stream[0] = 0xbf;
    data_fdps_graphics_rle_blit_src_width = 64;

    CHECK_EQ((long) (fdps_rle_skip_row(stream) - stream), 65);
}

/* Neither global is written: the width is only read (MOV BX,[0x00070024]) and
   there is no DEC word ptr [0x00070022] anywhere in the body, which is what
   separates this from the drawing kernels -- the caller keeps its own row
   count across all the rows this one walks. */
static void skip_row_leaves_the_blit_globals_alone(void)
{
    unsigned char stream[6];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    data_fdps_graphics_rle_blit_src_width = 3;
    data_fdps_graphics_rle_blit_remaining_rows = 0x1234;

    CHECK_EQ((long) (fdps_rle_skip_row(stream) - stream), 4);
    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, 3);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0x1234);
}

/* The exact-zero terminator, which is the whole reason the counter is an
   unsigned short.  A length-4 fill on a two-pixel row leaves 2 - 4 = 0xfffe
   rather than ending the row, and the decoder goes on until some later run
   lands the counter on zero: 1023 length-64 skips take it to 62 and one
   length-62 skip (0xc0 | 61) finishes it.  Under a `width <= 0` terminator
   the advance would be 2. */
static void skip_overshooting_run_wraps_the_width_counter(void)
{
    int op_index;

    wrap_stream[0] = 0x03;
    wrap_stream[1] = 0xaa;
    for (op_index = 0; op_index < 1023; op_index++) {
        wrap_stream[2 + op_index] = 0xff;
    }
    wrap_stream[1025] = 0xfd;
    data_fdps_graphics_rle_blit_src_width = 2;

    CHECK_EQ((long) (fdps_rle_skip_row(wrap_stream) - wrap_stream), 1026);
}

/* The terminator is also at the bottom of the loop -- the first op is decoded
   before the width is ever tested -- so a width of zero is a row of 0x10000
   pixels, not an empty one.  1024 length-64 skips bring the counter back to
   zero. */
static void skip_zero_width_walks_a_full_row(void)
{
    int op_index;

    for (op_index = 0; op_index < 1024; op_index++) {
        wrap_stream[op_index] = 0xff;
    }
    data_fdps_graphics_rle_blit_src_width = 0;

    CHECK_EQ((long) (fdps_rle_skip_row(wrap_stream) - wrap_stream), 1024);
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
    RUN_TEST(scaled_one_to_one_draws_the_source_row);
    RUN_TEST(scaled_upscale_repeats_each_source_pixel);
    RUN_TEST(scaled_downscale_drops_source_pixels);
    RUN_TEST(scaled_skip_run_leaves_destination_alone);
    RUN_TEST(scaled_halftone_run_writes_second_of_each_pair);
    RUN_TEST(scaled_vertical_upscale_redraws_the_source_row);
    RUN_TEST(scaled_vertical_downscale_drops_a_source_row);
    RUN_TEST(scaled_row_advance_is_zero_extended);
    RUN_TEST(scaled_partial_row_resyncs_through_skip_row);
    RUN_TEST(skip_fill_run_advances_two_bytes);
    RUN_TEST(skip_stretched_run_consumes_two_columns_per_unit);
    RUN_TEST(skip_literal_run_advances_past_its_pixels);
    RUN_TEST(skip_transparent_run_advances_one_byte);
    RUN_TEST(skip_row_walks_every_op_to_exact_width);
    RUN_TEST(skip_run_length_tops_out_at_64);
    RUN_TEST(skip_literal_run_at_max_length_advances_65);
    RUN_TEST(skip_row_leaves_the_blit_globals_alone);
    RUN_TEST(skip_overshooting_run_wraps_the_width_counter);
    RUN_TEST(skip_zero_width_walks_a_full_row);
}
