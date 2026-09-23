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

/* --- fdps_rle_blit_scaled (00056c5e), blit mode 4 ---------------------------
 *
 * The source rectangle is what the dispatcher publishes: src_width into
 * data_fdps_graphics_rle_blit_src_width, src_rows into
 * data_fdps_graphics_rle_blit_remaining_rows (the source HEIGHT here -- the
 * kernel only compares and subtracts it, never decrements it) and dest_pitch
 * into data_fdps_graphics_rle_blit_dst_pitch.  The destination width and
 * height are the mode operand's low and high words, read straight out of the
 * dispatcher's frame (MOV DX,[EBP+0x1c] and [EBP+0x1e] at 00056c5e).  Mode 4
 * gets no row advance from the dispatcher; the kernel derives its own as
 * dst_pitch - dest_width in sixteen bits (00056c8b..00056c99).
 *
 * Expected values come from the Bresenham step at 00056cca and its three
 * copies: the accumulator starts at the destination width, an accumulator
 * below the source width consumes one source pixel and adds the destination
 * width, and otherwise one destination pixel is emitted and the source width
 * subtracted.  Every stream is padded with 0xc0, a length-1 skip, so a decoder
 * that gets a length or a byte cost wrong walks forward through padding
 * instead of off the end of the buffer.
 */
#define SCAL_MODE     4
#define SCAL_SENTINEL 0x5a
#define SCAL_OPERAND(dest_width, dest_height) \
    ((unsigned int) (dest_width) | ((unsigned int) (dest_height) << 16))

static unsigned char scal_surface[80];

static void scal_clear(void)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof scal_surface; byte_index++) {
        scal_surface[byte_index] = SCAL_SENTINEL;
    }
}

/* A 4x1 source drawn at 4x1 in a pitch of 8: both accumulators divide out
   exactly and the row is the same four pixels the pass-through kernel would
   draw.  This also pins the parameter block the kernel publishes -- the two
   scale words into 00070028 and 0007002a, the row advance as 8 - 4, the
   destination row counter consumed to zero -- and the vertical accumulator,
   seeded with the destination height, ends at 1 + 1 - 1.  The source height
   the dispatcher published is only read, so it is still 1. */
static void scal_one_to_one_draws_the_source_row(void)
{
    unsigned char stream[4];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 4, 1, 8, SCAL_OPERAND(4, 1),
                       SCAL_MODE);

    CHECK_EQ(scal_surface[0], 0xaa);
    CHECK_EQ(scal_surface[3], 0xaa);
    CHECK_EQ(scal_surface[4], SCAL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_width, 4);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_height, 1);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 4);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
    CHECK_EQ(data_fdps_graphics_rle_blit_vscale_accumulator, 1);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 1);
}

/* Horizontal upscale, a 2-pixel literal row drawn 4 wide: the accumulator
   starts at 4, so each of the two literal bytes is emitted twice before the
   next is consumed (INC ESI at 00056d45 only on the consume branch). */
static void scal_upscale_repeats_each_source_pixel(void)
{
    unsigned char stream[6];

    stream[0] = 0x81;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0xc0;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 2, 1, 4, SCAL_OPERAND(4, 1),
                       SCAL_MODE);

    CHECK_EQ(scal_surface[0], 0x11);
    CHECK_EQ(scal_surface[1], 0x11);
    CHECK_EQ(scal_surface[2], 0x22);
    CHECK_EQ(scal_surface[3], 0x22);
    CHECK_EQ(scal_surface[4], SCAL_SENTINEL);
}

/* Horizontal downscale, a 4-pixel source row drawn 2 wide.  The accumulator
   starts at 2, below the source width of 4, so the FIRST thing the loop does
   is consume a source pixel rather than emit one: 0x11 and 0x33 are dropped
   and 0x22 and 0x44 are what land. */
static void scal_downscale_drops_source_pixels(void)
{
    unsigned char stream[7];

    stream[0] = 0x83;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    stream[4] = 0x44;
    stream[5] = 0xc0;
    stream[6] = 0xc0;
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 4, 1, 2, SCAL_OPERAND(2, 1),
                       SCAL_MODE);

    CHECK_EQ(scal_surface[0], 0x22);
    CHECK_EQ(scal_surface[1], 0x44);
    CHECK_EQ(scal_surface[2], SCAL_SENTINEL);
}

/* Op 11 at 1:1 -- INC EDI with no write at 00056d7f -- so the two pixels it
   covers keep the surface's own content and the fill that follows lands two
   bytes further on. */
static void scal_skip_run_leaves_destination_alone(void)
{
    unsigned char stream[5];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = 0x77;
    stream[3] = 0xc0;
    stream[4] = 0xc0;
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 4, 1, 4, SCAL_OPERAND(4, 1),
                       SCAL_MODE);

    CHECK_EQ(scal_surface[0], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[1], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[2], 0x77);
    CHECK_EQ(scal_surface[3], 0x77);
    CHECK_EQ(scal_surface[4], SCAL_SENTINEL);
}

/* Op 01 at 1:1 (0x41, length 2 doubled to 4 by SHL CX,1 at 00056cf2): the
   phase in AH starts at zero and flips only when a source pixel is consumed,
   so the run covers four columns and writes the second of each pair.  Without
   the doubling the run would end after two columns and the next command byte
   would be read from the padding. */
static void scal_halftone_run_writes_second_of_each_pair(void)
{
    unsigned char stream[5];

    stream[0] = 0x41;
    stream[1] = 0x99;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    stream[4] = 0xc0;
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 4, 1, 4, SCAL_OPERAND(4, 1),
                       SCAL_MODE);

    CHECK_EQ(scal_surface[0], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[1], 0x99);
    CHECK_EQ(scal_surface[2], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[3], 0x99);
    CHECK_EQ(scal_surface[4], SCAL_SENTINEL);
}

/* Vertical upscale: one source row drawn as two destination rows.  The source
   cursor is popped back to the row start (POP ESI at 00056d84), and the
   vertical accumulator -- seeded with the destination height of 2, above the
   source height of 1 -- skips no source row before the second pass, so the
   same encoded row is decoded twice.  The accumulator ends at 2 - 1, then
   1 + 2 - 1.

   The advance (4 - 2) is added to a cursor already standing at the end of the
   row it just drew (ADD EDI,[0x00070030] at 00056db5), so the second row
   starts at byte 4 and bytes 2 and 3 are untouched. */
static void scal_vertical_upscale_redraws_the_source_row(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 2, 1, 4, SCAL_OPERAND(2, 2),
                       SCAL_MODE);

    CHECK_EQ(scal_surface[0], 0xaa);
    CHECK_EQ(scal_surface[1], 0xaa);
    CHECK_EQ(scal_surface[2], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[3], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[4], 0xaa);
    CHECK_EQ(scal_surface[5], 0xaa);
    CHECK_EQ(scal_surface[6], SCAL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_vscale_accumulator, 2);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
}

/* Vertical downscale: two source rows, one destination row.  The accumulator
   starts at 1, not above the source height of 2, so fdps_rle_skip_row is
   called twice after the only row is drawn (00056d99) and the second source
   row is never decoded for its pixels -- 0xbb reaches nothing, not even the
   bytes where a second row would have gone. */
static void scal_vertical_downscale_drops_a_source_row(void)
{
    unsigned char stream[6];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 2, 2, 4, SCAL_OPERAND(2, 1),
                       SCAL_MODE);

    CHECK_EQ(scal_surface[0], 0xaa);
    CHECK_EQ(scal_surface[1], 0xaa);
    CHECK_EQ(scal_surface[2], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[3], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[4], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[5], SCAL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dest_rows_remaining, 0);
}

/* The row advance is computed sixteen bits wide and stored zero-extended into
   the dword (SUB BP,[0x00070028] then MOV [0x00070030],EBP with EBP's top half
   zeroed by XOR EBP,EBP at 00056c74).  A destination four wide in a pitch of
   two therefore stores 65534, not -2.  This is independent of the dispatcher's
   own 32-bit pitch - width, which mode 4 is never handed.  The four
   destination pixels come from the single source pixel of a 1-wide row. */
static void scal_row_advance_is_zero_extended(void)
{
    unsigned char stream[4];

    stream[0] = 0x00;
    stream[1] = 0x77;
    stream[2] = 0xc0;
    stream[3] = 0xc0;
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 1, 1, 2, SCAL_OPERAND(4, 1),
                       SCAL_MODE);

    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 65534L);
    CHECK_EQ(scal_surface[0], 0x77);
    CHECK_EQ(scal_surface[3], 0x77);
    CHECK_EQ(scal_surface[4], SCAL_SENTINEL);
}

/* The case the push and pop of the source cursor exist for: a 4-wide source
   drawn 2 wide leaves each row's literal run half decoded, with the cursor on
   the last of its four pixel bytes.  The row still ends at the next row's
   first command byte, because the cursor is popped back to the row start and
   fdps_rle_skip_row walks the whole encoded row.  A decoder that carried the
   cursor on from where it stopped would read 0x44 as the second row's command
   byte.  The second destination row starts at byte 4, not byte 2: the advance
   of pitch - width is added to a cursor already at the end of the row. */
static void scal_partial_row_resyncs_through_skip_row(void)
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
    scal_clear();
    fdps_blit_dispatch(stream, scal_surface, 4, 2, 4, SCAL_OPERAND(2, 2),
                       SCAL_MODE);

    CHECK_EQ(scal_surface[0], 0x22);
    CHECK_EQ(scal_surface[1], 0x44);
    CHECK_EQ(scal_surface[2], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[3], SCAL_SENTINEL);
    CHECK_EQ(scal_surface[4], 0x66);
    CHECK_EQ(scal_surface[5], 0x88);
    CHECK_EQ(scal_surface[6], SCAL_SENTINEL);
}

/* --- fdps_rle_skip_row (00056dc9), reached through blit mode 4 --------------
 *
 * The row skipper has no mode of its own and writes nothing, so what it
 * produces -- how far the stream cursor moves -- is observed through the row
 * that is drawn next.  Every case dispatches a two-row source to a two-row
 * destination through the scaled kernel.  Vertically that is 1:1, and the
 * loop at 00056d84 then calls the skipper exactly once after each drawn row:
 * the cursor is popped back to the row start (POP ESI), the accumulator of 2
 * is not above the source height of 2 so CALL fdps_rle_skip_row walks the row
 * just drawn, and 2 + 2 is above it so the loop stops.  The second row is
 * therefore decoded from wherever the skipper left the cursor after walking
 * the first, and nothing else moves the cursor in between.  (The skipper also
 * walks the second row after it is drawn; every stream carries a complete
 * second row for that.)
 *
 * Row 0 is the stream under test.  Row 1 is a fill run of SKROW_MARK exactly
 * as wide as the row, drawn 1:1 horizontally where the destination width
 * equals the source width, so it lands as an unbroken run of SKROW_MARK
 * starting at byte SKROW_PITCH (the kernel's own advance, pitch minus
 * destination width, added to a cursor already at the end of row 0).  A
 * skipper that stopped one byte early or late, charged a wrong width, or
 * ended the row on a wrong test would make row 1 decode from some other byte
 * -- a pixel byte, a skip run or the mark itself read as a command -- and the
 * run of marks would not be where the checks look for it.
 *
 * Expected advances are read off the assembly: LODSB plus INC ESI for fill
 * (00056deb) and for stretched (00056df7), LODSB plus ADD ESI,ECX for a
 * literal (00056e14), LODSB alone for a skip.  The width charge is the SUB
 * BX,CX in each arm, twice in the stretched one (00056df8, 00056dfb), and the
 * only terminator is OR BX,BX / JNZ at the bottom of each arm.
 */
#define SKROW_MODE     4
#define SKROW_SENTINEL 0x5a
#define SKROW_MARK     0xee
#define SKROW_PITCH    72
#define SKROW_OPERAND(dest_width) \
    ((unsigned int) (dest_width) | ((unsigned int) 2 << 16))

static unsigned char skrow_surface[2 * SKROW_PITCH + 8];

/* Room for the two wrap cases: the zero-width one needs two rows of over a
   thousand command bytes each. */
static unsigned char skrow_wrap_stream[2052];

static void skrow_blit(unsigned char *stream, int src_width, int dest_width)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof skrow_surface; byte_index++) {
        skrow_surface[byte_index] = SKROW_SENTINEL;
    }
    fdps_blit_dispatch(stream, skrow_surface, src_width, 2, SKROW_PITCH,
                       SKROW_OPERAND(dest_width), SKROW_MODE);
}

/* Row 1 came out as `width` marks at the start of the second scanline and
   nothing after them. */
static void skrow_check_mark_row(int width)
{
    CHECK_EQ(skrow_surface[SKROW_PITCH], SKROW_MARK);
    CHECK_EQ(skrow_surface[SKROW_PITCH + width - 1], SKROW_MARK);
    CHECK_EQ(skrow_surface[SKROW_PITCH + width], SKROW_SENTINEL);
}

/* Op 00 (0x03, length 4) on a four-pixel row: the command byte and its single
   pixel byte, so row 1 starts two bytes on. */
static void skrow_fill_run_advances_two_bytes(void)
{
    unsigned char stream[6];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    stream[2] = 0x03;
    stream[3] = SKROW_MARK;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    skrow_blit(stream, 4, 4);

    skrow_check_mark_row(4);
}

/* Op 01 (0x41, length 2): two bytes, but it accounts for four columns.  An arm
   that charged the length once would leave two pixels owing and read row 1's
   command byte as part of row 0. */
static void skrow_stretched_run_consumes_two_columns_per_unit(void)
{
    unsigned char stream[6];

    stream[0] = 0x41;
    stream[1] = 0x99;
    stream[2] = 0x03;
    stream[3] = SKROW_MARK;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    skrow_blit(stream, 4, 4);

    skrow_check_mark_row(4);
}

/* Op 10 (0x82, length 3): the command byte plus its three pixel bytes, so row
   1 starts four bytes on. */
static void skrow_literal_run_advances_past_its_pixels(void)
{
    unsigned char stream[8];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    stream[4] = 0x02;
    stream[5] = SKROW_MARK;
    stream[6] = 0xc0;
    stream[7] = 0xc0;
    skrow_blit(stream, 3, 3);

    skrow_check_mark_row(3);
}

/* Op 11 (0xc1, length 2): nothing follows the command byte, so a two-pixel
   row costs one byte. */
static void skrow_transparent_run_advances_one_byte(void)
{
    unsigned char stream[5];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = SKROW_MARK;
    stream[3] = 0xc0;
    stream[4] = 0xc0;
    skrow_blit(stream, 2, 2);

    skrow_check_mark_row(2);
}

/* All four ops at length 1: widths 1, 2, 1 and 1 make the five-pixel row and
   byte costs 2, 2, 2 and 1 make the seven-byte advance -- the same stream the
   pass-through case draws, so the two agree on where the row ends. */
static void skrow_walks_every_op_to_exact_width(void)
{
    unsigned char stream[11];

    stream[0] = 0x00;
    stream[1] = 0xa1;
    stream[2] = 0x40;
    stream[3] = 0xb2;
    stream[4] = 0x80;
    stream[5] = 0xc3;
    stream[6] = 0xc0;
    stream[7] = 0x04;
    stream[8] = SKROW_MARK;
    stream[9] = 0xc0;
    stream[10] = 0xc0;
    skrow_blit(stream, 5, 5);

    skrow_check_mark_row(5);
}

/* SHR CL,2 / INC CL makes 0x3f a length of 64, so a 64-wide row is one op; a
   skipper that read it as 63 would take row 1's command byte into row 0. */
static void skrow_run_length_tops_out_at_64(void)
{
    unsigned char stream[6];

    stream[0] = 0x3f;
    stream[1] = 0xcc;
    stream[2] = 0x3f;
    stream[3] = SKROW_MARK;
    stream[4] = 0xc0;
    stream[5] = 0xc0;
    skrow_blit(stream, 64, 64);

    skrow_check_mark_row(64);
}

/* The widest literal, 0xbf: 64 pixel bytes after the command byte, so row 1
   starts 65 bytes on.  This is the arm that adds the length to the pointer
   (ADD ESI,ECX at 00056e14), at the largest step it can take.  The pixel bytes
   are 0xc0, length-1 skips, so a cursor left inside them draws holes where
   the marks should be. */
static void skrow_literal_run_at_max_length_advances_65(void)
{
    static unsigned char stream[69];
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof stream; byte_index++) {
        stream[byte_index] = 0xc0;
    }
    stream[0] = 0xbf;
    stream[65] = 0x3f;
    stream[66] = SKROW_MARK;
    skrow_blit(stream, 64, 64);

    skrow_check_mark_row(64);
}

/* Neither global is written: the width is only read (MOV BX,[0x00070024]) and
   there is no DEC word ptr [0x00070022] in the body -- the caller keeps its
   own row count across the rows this one walks.  The dispatcher publishes the
   source height into 0x00070022 itself, so the value to find there afterwards
   is that published 2 rather than a hand-seeded one; the scaled kernel only
   compares and subtracts it, so after two skipper calls it is still 2, and
   the width is still 3. */
static void skrow_leaves_the_blit_globals_alone(void)
{
    unsigned char stream[8];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    stream[4] = 0x02;
    stream[5] = SKROW_MARK;
    stream[6] = 0xc0;
    stream[7] = 0xc0;
    skrow_blit(stream, 3, 3);

    skrow_check_mark_row(3);
    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, 3);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 2);
}

/* The exact-zero terminator on an unsigned sixteen-bit counter.  A length-4
   fill on a two-pixel row leaves 2 - 4 = 0xfffe rather than ending the row,
   and the skipper goes on until a later run lands the counter on zero: 1023
   length-64 skips take it to 62 and one length-62 skip (0xfd) finishes it, so
   row 1 starts 1026 bytes on.  Under a `width <= 0` terminator it would start
   at byte 2, on a length-64 skip, and row 1 would stay all sentinel.  Row 0
   is drawn 1:1, so its draw stops after two pixels of the fill. */
static void skrow_overshooting_run_wraps_the_width_counter(void)
{
    int op_index;

    skrow_wrap_stream[0] = 0x03;
    skrow_wrap_stream[1] = 0xaa;
    for (op_index = 0; op_index < 1023; op_index++) {
        skrow_wrap_stream[2 + op_index] = 0xff;
    }
    skrow_wrap_stream[1025] = 0xfd;
    skrow_wrap_stream[1026] = 0x01;
    skrow_wrap_stream[1027] = SKROW_MARK;
    skrow_wrap_stream[1028] = 0xc0;
    skrow_wrap_stream[1029] = 0xc0;
    skrow_blit(skrow_wrap_stream, 2, 2);

    skrow_check_mark_row(2);
}

/* The terminator sits at the bottom of the loop -- the first op is decoded
   before the width is ever tested -- so a width of zero is a row of 0x10000
   pixels, not an empty one: 1024 length-64 skips bring the counter back to
   zero and row 1 starts 1024 bytes on.  A skipper that tested first would not
   move the cursor and row 1 would be drawn from the skip at byte 0.

   The destination is 1 wide, because a 0-wide one would make the draw's own
   DEC BX wrap.  With a source width of 0 the draw's CMP BP,DX always takes
   the emit branch, so each row emits one copy of its first op: nothing for
   row 0's skip, one mark for row 1's length-1 fill.  Row 1 is itself a full
   0x10000-pixel row -- the fill, 1023 length-64 skips and a length-63 skip
   (0xfe) -- because the skipper walks it too after it is drawn. */
static void skrow_zero_width_walks_a_full_row(void)
{
    int op_index;

    for (op_index = 0; op_index < 1024; op_index++) {
        skrow_wrap_stream[op_index] = 0xff;
    }
    skrow_wrap_stream[1024] = 0x00;
    skrow_wrap_stream[1025] = SKROW_MARK;
    for (op_index = 0; op_index < 1023; op_index++) {
        skrow_wrap_stream[1026 + op_index] = 0xff;
    }
    skrow_wrap_stream[2049] = 0xfe;
    skrow_wrap_stream[2050] = 0xc0;
    skrow_wrap_stream[2051] = 0xc0;
    skrow_blit(skrow_wrap_stream, 0, 1);

    skrow_check_mark_row(1);
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
    RUN_TEST(scal_one_to_one_draws_the_source_row);
    RUN_TEST(scal_upscale_repeats_each_source_pixel);
    RUN_TEST(scal_downscale_drops_source_pixels);
    RUN_TEST(scal_skip_run_leaves_destination_alone);
    RUN_TEST(scal_halftone_run_writes_second_of_each_pair);
    RUN_TEST(scal_vertical_upscale_redraws_the_source_row);
    RUN_TEST(scal_vertical_downscale_drops_a_source_row);
    RUN_TEST(scal_row_advance_is_zero_extended);
    RUN_TEST(scal_partial_row_resyncs_through_skip_row);
    RUN_TEST(skrow_fill_run_advances_two_bytes);
    RUN_TEST(skrow_stretched_run_consumes_two_columns_per_unit);
    RUN_TEST(skrow_literal_run_advances_past_its_pixels);
    RUN_TEST(skrow_transparent_run_advances_one_byte);
    RUN_TEST(skrow_walks_every_op_to_exact_width);
    RUN_TEST(skrow_run_length_tops_out_at_64);
    RUN_TEST(skrow_literal_run_at_max_length_advances_65);
    RUN_TEST(skrow_leaves_the_blit_globals_alone);
    RUN_TEST(skrow_overshooting_run_wraps_the_width_counter);
    RUN_TEST(skrow_zero_width_walks_a_full_row);
}
