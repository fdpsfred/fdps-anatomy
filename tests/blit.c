/* tests/blit.c -- cover for src/blit.c.
 *
 * Expected values come from the assembly at 0002e5e0 -- IMUL EAX,[EBP+0x18],
 * 0x140 then ADD EAX,0xa0000 then ADD EDX,EAX for the destination, the two
 * DEC EAX at 0002e60a and 0002e61d that make both the row count and the
 * memset length one less than the argument, the CMP EAX,[EBP-0x4] / JG signed
 * loop test, and ADD dword ptr [EBP-0x8],0x140 for the row advance.  None of
 * them is read off the emitted C.
 *
 * HOW THE WRITES ARE OBSERVED.  fdps_fill_screen_square has no destination
 * argument: it paints at 0xa0000 + y * 0x140 + x, and in a text-mode console
 * the VGA aperture at 0xa0000 is not even mapped, so reading it back would
 * measure nothing.  What makes the routine testable is the thing its plate
 * comment calls out as its defining property -- it clips nothing and checks
 * nothing, x is added to the aperture address as a plain signed int.  So each
 * case hands it the column offset that carries that sum onto a byte of the
 * canvas below, computed here from the aperture address and the 0x140 pitch
 * the assembly uses, and then reads the canvas.
 *
 * That keeps the destination arithmetic under test rather than assumed: a
 * routine that multiplied y by the 0x138 offscreen pitch, or that dropped the
 * aperture base, would land somewhere else in memory and every byte checked
 * below would still hold its guard value.
 *
 * The canvas is deliberately far larger than any square painted into it, and
 * every case fills it with a guard byte first, so a run that is one pixel too
 * wide, one row too tall or one scanline off is caught by a guard byte that
 * changed rather than by a painted byte that did not.
 */
#include <string.h>
#include "testharn.h"
#include "blit.h"

/* Repeated here rather than shared with src/blit.c: these are what the
   assembly encodes, and a test that took them from the emitted header would
   only prove the code agrees with itself. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* Eight scanlines of canvas.  Nothing below paints past the fourth. */
#define CANVAS_ROWS 8
#define CANVAS_BYTES (VGA_SCREEN_PITCH * CANVAS_ROWS)

/* Neither value is a palette index the cases paint with, so a byte holding
   one was not touched. */
#define GUARD 0xee

/* Where inside the canvas a square's top-left corner is put.  Two scanlines
   down and a few columns in, so an off-by-one in any direction has canvas on
   both sides of it. */
#define ORIGIN (VGA_SCREEN_PITCH * 2 + 16)

static unsigned char canvas[CANVAS_BYTES];

static void fill_canvas_with_guard(void)
{
    memset(canvas, GUARD, (size_t) CANVAS_BYTES);
}

/* The x argument that makes fdps_fill_screen_square's own destination sum --
   0xa0000 + y * 0x140 + x -- come out at &canvas[offset].  y is passed
   through so the caller can choose it freely and still hit the same byte,
   which is what puts the y * 0x140 term under test. */
static int column_offset_for(int offset, int y)
{
    return (int) ((unsigned long) &canvas[offset] - (unsigned long) VGA_SCREEN_BASE)
           - y * VGA_SCREEN_PITCH;
}

static int canvas_byte(int offset)
{
    return (int) canvas[offset];
}

/* cell_pitch 4 -- the value fdps_battle_map_overview uses at normal zoom --
   paints a 3x3 square, not a 4x4 one: three bytes on each of three rows. */
static void pitch_four_paints_a_three_by_three_square(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x21, 4);

    CHECK_EQ(canvas_byte(ORIGIN + 0), 0x21);
    CHECK_EQ(canvas_byte(ORIGIN + 1), 0x21);
    CHECK_EQ(canvas_byte(ORIGIN + 2), 0x21);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH + 0), 0x21);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH + 1), 0x21);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH + 2), 0x21);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 2 + 0), 0x21);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 2 + 1), 0x21);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 2 + 2), 0x21);
}

/* The fourth column and the fourth row stay as they were: this is the
   one-pixel gap between adjacent overview markers, and it is the difference
   between the two DEC EAX being there and not. */
static void pitch_four_leaves_the_fourth_column_and_row_alone(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x21, 4);

    CHECK_EQ(canvas_byte(ORIGIN + 3), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH + 3), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 2 + 3), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 3 + 0), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 3 + 1), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 3 + 2), GUARD);
}

/* Nothing is painted before the corner either: the square starts exactly at
   the computed address, and the row above it is untouched. */
static void nothing_is_painted_before_the_corner(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x21, 4);

    CHECK_EQ(canvas_byte(ORIGIN - 1), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN - VGA_SCREEN_PITCH), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN - VGA_SCREEN_PITCH + 1), GUARD);
}

/* cell_pitch 3 -- what the overview switches to for a map taller than 0x28
   tiles -- paints 2x2. */
static void pitch_three_paints_a_two_by_two_square(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x55, 3);

    CHECK_EQ(canvas_byte(ORIGIN + 0), 0x55);
    CHECK_EQ(canvas_byte(ORIGIN + 1), 0x55);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH + 0), 0x55);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH + 1), 0x55);
    CHECK_EQ(canvas_byte(ORIGIN + 2), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 2), GUARD);
}

/* A row of the square is one contiguous memset of cell_pitch-1 bytes and the
   next row begins exactly 0x140 bytes on, so with a wide square the bytes
   between the end of one row and the start of the next are untouched. */
static void rows_are_one_scanline_apart(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x7f, 6);

    CHECK_EQ(canvas_byte(ORIGIN + 4), 0x7f);
    CHECK_EQ(canvas_byte(ORIGIN + 5), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH - 1), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH), 0x7f);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 4), 0x7f);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH * 5), GUARD);
}

/* The row argument is scaled by 0x140, the mode 13h pitch, and by nothing
   else: three calls that name three different rows land on the same byte once
   the column offset compensates for exactly y * 0x140. */
static void the_row_argument_is_scaled_by_the_screen_pitch(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 5), 5, 0x11, 2);
    CHECK_EQ(canvas_byte(ORIGIN), 0x11);

    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 100), 100, 0x12, 2);
    CHECK_EQ(canvas_byte(ORIGIN), 0x12);

    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, -3), -3, 0x13, 2);
    CHECK_EQ(canvas_byte(ORIGIN), 0x13);
}

/* color reaches the screen through memset, which fills with the low byte of
   its int argument: 0x1234 paints 0x34.  The overview's own colours are all
   under 0x100, so this pins the width rather than a value the game relies
   on. */
static void only_the_low_byte_of_color_is_painted(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x1234, 3);

    CHECK_EQ(canvas_byte(ORIGIN), 0x34);
    CHECK_EQ(canvas_byte(ORIGIN + 1), 0x34);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH), 0x34);
}

/* cell_pitch 1 gives the bound 0 and cell_pitch 2 gives 1: the first paints
   nothing at all, the second paints a single byte. */
static void pitch_one_paints_nothing_and_pitch_two_paints_one_byte(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x60, 1);
    CHECK_EQ(canvas_byte(ORIGIN), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH), GUARD);

    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x60, 2);
    CHECK_EQ(canvas_byte(ORIGIN), 0x60);
    CHECK_EQ(canvas_byte(ORIGIN + 1), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH), GUARD);
}

/* The loop test is signed (JG), so a cell_pitch of 0 gives the bound -1 and
   the loop is not entered.  Were cell_pitch read as unsigned the bound would
   be 0xffffffff and this call would paint its way across the address space,
   so what this case really guards is the declared signedness of the
   argument. */
static void pitch_zero_paints_nothing(void)
{
    fill_canvas_with_guard();
    fdps_fill_screen_square(column_offset_for(ORIGIN, 0), 0, 0x60, 0);

    CHECK_EQ(canvas_byte(ORIGIN), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN + VGA_SCREEN_PITCH), GUARD);
    CHECK_EQ(canvas_byte(ORIGIN - 1), GUARD);
}

/* ------------------------------------------------------------------ *
 * fdps_blit_rect @ 0002f2f0
 *
 * This one takes its destination as an argument, so unlike the routine
 * above it can simply be pointed at a buffer here and the buffer read
 * back.  Expected values come from the assembly: the JNZ at 0002f308 that
 * makes a zero src_stride mean fill rather than copy, the two
 * CMP EAX,[EBP+0x28] / JL loop tests that count rows signed, the separate
 * ADD [EBP-0x8],[EBP+0x18] and ADD [EBP-0x4],[EBP+0x20] that advance the
 * two cursors independently, and CALL 0x0003d514 -- memmove, not memcpy --
 * for the per-row transfer.
 * ------------------------------------------------------------------ */

/* Room for every offset the cases below name, with slack past the end so an
   over-long row lands in the buffer and is seen rather than corrupting
   something else. */
#define RECT_DST_BYTES 128
#define RECT_SRC_BYTES 64

/* Outside the range the source is filled with, so a byte holding it was not
   written. */
#define RECT_GUARD 0xd7

static unsigned char rect_dst[RECT_DST_BYTES];
static unsigned char rect_src[RECT_SRC_BYTES];

/* The source bytes are all distinct, so an assertion says which source byte
   arrived and not merely that something did. */
static void prepare_rect_buffers(void)
{
    int i;

    memset(rect_dst, RECT_GUARD, (size_t) RECT_DST_BYTES);
    for (i = 0; i < RECT_SRC_BYTES; i++) {
        rect_src[i] = (unsigned char) (0x40 + i);
    }
}

static int rect_dst_byte(int offset)
{
    return (int) rect_dst[offset];
}

/* The two strides are read from different arguments and applied to different
   cursors, so a rectangle can be gathered at one pitch and laid down at
   another: three rows of four bytes taken every 8 source bytes and written
   every 6 destination bytes.  Source rows 0, 8 and 16 are 0x40, 0x48 and
   0x50, which is what says the source pitch was 8 and not 4 or 6. */
static void copy_mode_uses_independent_strides(void)
{
    prepare_rect_buffers();
    fdps_blit_rect((unsigned int) rect_src, 8, rect_dst, 6, 4, 3);

    CHECK_EQ(rect_dst_byte(0), 0x40);
    CHECK_EQ(rect_dst_byte(1), 0x41);
    CHECK_EQ(rect_dst_byte(2), 0x42);
    CHECK_EQ(rect_dst_byte(3), 0x43);
    CHECK_EQ(rect_dst_byte(6), 0x48);
    CHECK_EQ(rect_dst_byte(9), 0x4b);
    CHECK_EQ(rect_dst_byte(12), 0x50);
    CHECK_EQ(rect_dst_byte(15), 0x53);

    /* The gaps the two strides leave behind stay as they were: a row is
       exactly bytes_per_row long and the next one starts a whole dst_stride
       on, so the four bytes between them are never touched. */
    CHECK_EQ(rect_dst_byte(4), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(5), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(10), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(11), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(16), RECT_GUARD);
}

/* rows is compared with JL, a signed test, so a count of zero or below
   transfers nothing at all.  Read as unsigned, -1 would be four billion rows
   and would walk the copy off the end of memory. */
static void rows_is_a_signed_count(void)
{
    prepare_rect_buffers();
    fdps_blit_rect((unsigned int) rect_src, 8, rect_dst, 6, 4, 0);
    CHECK_EQ(rect_dst_byte(0), RECT_GUARD);

    prepare_rect_buffers();
    fdps_blit_rect((unsigned int) rect_src, 8, rect_dst, 6, 4, -1);
    CHECK_EQ(rect_dst_byte(0), RECT_GUARD);

    /* One row means one row: the second row's destination is untouched. */
    prepare_rect_buffers();
    fdps_blit_rect((unsigned int) rect_src, 8, rect_dst, 6, 4, 1);
    CHECK_EQ(rect_dst_byte(0), 0x40);
    CHECK_EQ(rect_dst_byte(3), 0x43);
    CHECK_EQ(rect_dst_byte(6), RECT_GUARD);
}

/* A src_stride of zero switches to fill mode instead of repeating one source
   row: src_or_fill is 0xab here, which is not an address anything could be
   read from, and the destination comes out full of 0xab.  The destination
   stride still applies, so the two rows are five bytes apart with the gap
   left alone. */
static void zero_src_stride_fills_instead_of_copying(void)
{
    prepare_rect_buffers();
    fdps_blit_rect(0xab, 0, rect_dst, 5, 3, 2);

    CHECK_EQ(rect_dst_byte(0), 0xab);
    CHECK_EQ(rect_dst_byte(1), 0xab);
    CHECK_EQ(rect_dst_byte(2), 0xab);
    CHECK_EQ(rect_dst_byte(3), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(4), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(5), 0xab);
    CHECK_EQ(rect_dst_byte(7), 0xab);
    CHECK_EQ(rect_dst_byte(8), RECT_GUARD);
}

/* The fill value goes through memset, which paints with the low byte of its
   int argument, so the upper 24 bits of src_or_fill are dropped. */
static void fill_mode_writes_only_the_low_byte(void)
{
    prepare_rect_buffers();
    fdps_blit_rect(0x12345678u, 0, rect_dst, 4, 2, 1);

    CHECK_EQ(rect_dst_byte(0), 0x78);
    CHECK_EQ(rect_dst_byte(1), 0x78);
    CHECK_EQ(rect_dst_byte(2), RECT_GUARD);
}

/* The per-row transfer is memmove, so a row copied one byte along itself
   comes out shifted, not smeared: the five bytes 1..5 at offset 0 become the
   five bytes 1..5 at offset 1.  A memcpy that copied forwards would leave
   1,1,1,1,1 there, and the transitions that slide a page across itself would
   streak. */
static void a_row_that_overlaps_itself_is_moved_not_smeared(void)
{
    prepare_rect_buffers();
    rect_dst[0] = 1;
    rect_dst[1] = 2;
    rect_dst[2] = 3;
    rect_dst[3] = 4;
    rect_dst[4] = 5;

    fdps_blit_rect((unsigned int) rect_dst, 16, rect_dst + 1, 16, 5, 1);

    CHECK_EQ(rect_dst_byte(0), 1);
    CHECK_EQ(rect_dst_byte(1), 1);
    CHECK_EQ(rect_dst_byte(2), 2);
    CHECK_EQ(rect_dst_byte(3), 3);
    CHECK_EQ(rect_dst_byte(4), 4);
    CHECK_EQ(rect_dst_byte(5), 5);
    CHECK_EQ(rect_dst_byte(6), RECT_GUARD);
}

/* The cursor is advanced by adding the stride, not by scaling anything, so a
   negative stride walks the rows backwards up memory: rows land at 40, 30 and
   20 rather than 40, 50 and 60. */
static void a_negative_destination_stride_walks_backwards(void)
{
    prepare_rect_buffers();
    fdps_blit_rect(0x33, 0, rect_dst + 40, -10, 2, 3);

    CHECK_EQ(rect_dst_byte(40), 0x33);
    CHECK_EQ(rect_dst_byte(41), 0x33);
    CHECK_EQ(rect_dst_byte(30), 0x33);
    CHECK_EQ(rect_dst_byte(31), 0x33);
    CHECK_EQ(rect_dst_byte(20), 0x33);
    CHECK_EQ(rect_dst_byte(21), 0x33);
    CHECK_EQ(rect_dst_byte(42), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(39), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(50), RECT_GUARD);
}

/* bytes_per_row is passed straight through as the transfer length and is not
   checked, so zero runs the loop the full rows times and writes nothing.
   Both modes behave the same way. */
static void a_zero_row_length_transfers_nothing(void)
{
    prepare_rect_buffers();
    fdps_blit_rect((unsigned int) rect_src, 8, rect_dst, 6, 0, 3);
    CHECK_EQ(rect_dst_byte(0), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(6), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(12), RECT_GUARD);

    prepare_rect_buffers();
    fdps_blit_rect(0x33, 0, rect_dst, 6, 0, 3);
    CHECK_EQ(rect_dst_byte(0), RECT_GUARD);
    CHECK_EQ(rect_dst_byte(6), RECT_GUARD);
}

/* ------------------------------------------------------------------ *
 * fdps_blit_transparent_rect @ 0002f390
 *
 * Expected values come from the assembly: the two signed CMP / JL loop
 * tests at 0002f3b2 and 0002f3cb that count height and width, the
 * XOR EAX,EAX / MOV AL,[EDX] load at 0002f3e0 followed by
 * CMP dword ptr [EBP-0x10],0x0 / JZ at 0002f3e7 that skips the store for a
 * zero source byte, the single MOV byte ptr [EDX],AL at 0002f3f6 that is
 * the only write in the routine, and the separate ADD [EBP-0x8],[EBP+0x18]
 * and ADD [EBP-0x4],[EBP+0x20] at 0002f3fd and 0002f403 that advance the
 * two cursors.  The routine has no CALL in it at all, so nothing below
 * depends on what a library routine hands back.
 * ------------------------------------------------------------------ */

#define TKEY_DST_BYTES 128
#define TKEY_SRC_BYTES 64

/* Outside 0x40..0x7f, the range the source is filled with, so a byte still
   holding it was never written. */
#define TKEY_GUARD 0x9c

static unsigned char tkey_dst[TKEY_DST_BYTES];
static unsigned char tkey_src[TKEY_SRC_BYTES];

/* Every source byte distinct and every one non-zero, so an assertion says
   which source byte arrived and no byte is skipped by accident.  Cases that
   want a transparent pixel poke a zero in themselves. */
static void prepare_tkey_buffers(void)
{
    int i;

    memset(tkey_dst, TKEY_GUARD, (size_t) TKEY_DST_BYTES);
    for (i = 0; i < TKEY_SRC_BYTES; i++) {
        tkey_src[i] = (unsigned char) (0x40 + i);
    }
}

static int tkey_dst_byte(int offset)
{
    return (int) tkey_dst[offset];
}

/* The defining property: a source byte of 0 is the transparency key, so the
   destination pixel under it is left exactly as it was rather than being
   painted with colour 0.  A per-row memmove -- what the sibling
   fdps_blit_rect does -- would put 0 into every one of those four gaps. */
static void a_zero_source_byte_leaves_the_destination_pixel_alone(void)
{
    prepare_tkey_buffers();
    tkey_src[1] = 0;
    tkey_src[3] = 0;
    tkey_src[4] = 0;

    fdps_blit_transparent_rect(tkey_src, 8, tkey_dst, 8, 6, 1);

    CHECK_EQ(tkey_dst_byte(0), 0x40);
    CHECK_EQ(tkey_dst_byte(1), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(2), 0x42);
    CHECK_EQ(tkey_dst_byte(3), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(4), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(5), 0x45);
}

/* Every non-zero value passes through unchanged, including the two ends of
   the byte range: only equality with 0 is tested, and the byte written is
   the byte read. */
static void every_non_zero_byte_is_stored_verbatim(void)
{
    prepare_tkey_buffers();
    tkey_src[0] = 0x01;
    tkey_src[1] = 0x80;
    tkey_src[2] = 0xff;
    tkey_src[3] = 0x00;

    fdps_blit_transparent_rect(tkey_src, 8, tkey_dst, 8, 4, 1);

    CHECK_EQ(tkey_dst_byte(0), 0x01);
    CHECK_EQ(tkey_dst_byte(1), 0x80);
    CHECK_EQ(tkey_dst_byte(2), 0xff);
    CHECK_EQ(tkey_dst_byte(3), TKEY_GUARD);
}

/* The two strides are read from different arguments and applied to different
   cursors, so the source sheet's pitch and the destination page's pitch are
   independent: three rows of four bytes gathered every 8 source bytes and
   laid down every 6 destination bytes.  Rows starting 0x40, 0x48 and 0x50 are
   what says the source pitch was 8 and not 4 or 6. */
static void independent_strides_are_applied_to_their_own_cursors(void)
{
    prepare_tkey_buffers();
    fdps_blit_transparent_rect(tkey_src, 8, tkey_dst, 6, 4, 3);

    CHECK_EQ(tkey_dst_byte(0), 0x40);
    CHECK_EQ(tkey_dst_byte(3), 0x43);
    CHECK_EQ(tkey_dst_byte(6), 0x48);
    CHECK_EQ(tkey_dst_byte(9), 0x4b);
    CHECK_EQ(tkey_dst_byte(12), 0x50);
    CHECK_EQ(tkey_dst_byte(15), 0x53);

    /* Exactly width bytes are considered on each row, so the two bytes the
       destination stride leaves between rows are never looked at. */
    CHECK_EQ(tkey_dst_byte(4), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(5), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(10), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(11), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(16), TKEY_GUARD);
}

/* Both extents are compared with JL, a signed test, so 0 or below on either
   transfers nothing.  Read as unsigned, -1 would be four billion rows or
   columns and would walk the copy off the end of memory. */
static void both_extents_are_signed_counts(void)
{
    prepare_tkey_buffers();
    fdps_blit_transparent_rect(tkey_src, 8, tkey_dst, 6, 4, 0);
    CHECK_EQ(tkey_dst_byte(0), TKEY_GUARD);

    prepare_tkey_buffers();
    fdps_blit_transparent_rect(tkey_src, 8, tkey_dst, 6, 4, -1);
    CHECK_EQ(tkey_dst_byte(0), TKEY_GUARD);

    prepare_tkey_buffers();
    fdps_blit_transparent_rect(tkey_src, 8, tkey_dst, 6, 0, 3);
    CHECK_EQ(tkey_dst_byte(0), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(6), TKEY_GUARD);

    prepare_tkey_buffers();
    fdps_blit_transparent_rect(tkey_src, 8, tkey_dst, 6, -1, 3);
    CHECK_EQ(tkey_dst_byte(0), TKEY_GUARD);

    /* One row means one row: the second row's destination is untouched. */
    prepare_tkey_buffers();
    fdps_blit_transparent_rect(tkey_src, 8, tkey_dst, 6, 4, 1);
    CHECK_EQ(tkey_dst_byte(0), 0x40);
    CHECK_EQ(tkey_dst_byte(3), 0x43);
    CHECK_EQ(tkey_dst_byte(6), TKEY_GUARD);
}

/* A source stride of zero is NOT the fill mode fdps_blit_rect switches into:
   there is no such branch here, src stays a pointer and is dereferenced on
   every row, so all three destination rows come out holding the same two
   source bytes.  In fill mode the low byte of the source argument -- an
   address -- would have been painted instead, which is neither 0x40 nor
   0x41. */
static void a_zero_source_stride_repeats_one_source_row(void)
{
    prepare_tkey_buffers();
    fdps_blit_transparent_rect(tkey_src, 0, tkey_dst, 4, 2, 3);

    CHECK_EQ(tkey_dst_byte(0), 0x40);
    CHECK_EQ(tkey_dst_byte(1), 0x41);
    CHECK_EQ(tkey_dst_byte(4), 0x40);
    CHECK_EQ(tkey_dst_byte(5), 0x41);
    CHECK_EQ(tkey_dst_byte(8), 0x40);
    CHECK_EQ(tkey_dst_byte(9), 0x41);
    CHECK_EQ(tkey_dst_byte(2), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(12), TKEY_GUARD);
}

/* Each cursor is advanced by adding its stride, nothing is scaled and nothing
   is made unsigned, so a negative stride walks that side of the transfer
   backwards up memory: the source rows are read at 16, 8 and 0 while the
   destination rows land at 40, 30 and 20. */
static void negative_strides_walk_both_cursors_backwards(void)
{
    prepare_tkey_buffers();
    fdps_blit_transparent_rect(tkey_src + 16, -8, tkey_dst + 40, -10, 2, 3);

    CHECK_EQ(tkey_dst_byte(40), 0x50);
    CHECK_EQ(tkey_dst_byte(41), 0x51);
    CHECK_EQ(tkey_dst_byte(30), 0x48);
    CHECK_EQ(tkey_dst_byte(31), 0x49);
    CHECK_EQ(tkey_dst_byte(20), 0x40);
    CHECK_EQ(tkey_dst_byte(21), 0x41);
    CHECK_EQ(tkey_dst_byte(42), TKEY_GUARD);
    CHECK_EQ(tkey_dst_byte(50), TKEY_GUARD);
}

/* ------------------------------------------------------------------ *
 * fdps_blit_mosaic_rect @ 0002fe40
 *
 * Expected values come from the assembly: the IDIV plus TEST EDX,EDX / JZ
 * pairs at 0002fe4c and 0002fe71 that make both cell counts ceilings, the
 * SUB EAX,EDX / SAR EAX,1 at 0002fe96 and 0002feea that start each sample
 * position at block/2, the JLE overrun tests at 0002ff10 and 0002ff40 that
 * shorten the last band and the last cell, the ADD / CMP / JL pairs at
 * 0002ff9f and 0002ffda that snap the sample position to
 * extent - extent%block once it reaches the extent, the memset at 0002ff83
 * with band_row + dest_x and the ADD [EBP-0x4],[EBP+0x20] that follows it,
 * and IMUL EAX,[EBP+0x30] at 0002ffcd for the band advance.
 *
 * The routine's one call is memset, whose return value the assembly
 * discards (ADD ESP,0xc and nothing else at 0002ff88), so no assertion
 * below depends on what the CRT hands back.
 *
 * WHY THE SOURCE IS A GRADIENT AND NOT A PATTERN.  Every source byte is
 * mos_pixel(row, col) = row * 16 + col + 1, distinct over the whole area
 * any case samples, so an assertion names the exact source pixel a cell was
 * flooded from rather than merely showing that a colour arrived.  That is
 * what lets the trailing-cell cases tell the original's snapped sample
 * apart from the clamped sample the obvious rewrite would take: the two
 * differ by one row or one column, which is a difference of 16 or 1 here.
 *
 * The two pitches are deliberately different from each other and from every
 * extent passed, so a routine that confused one for the other would land
 * somewhere the guard byte still is.
 * ------------------------------------------------------------------ */

#define MOS_SRC_PITCH 32
#define MOS_SRC_ROWS 20
#define MOS_SRC_BYTES (MOS_SRC_PITCH * MOS_SRC_ROWS)

#define MOS_DST_PITCH 24
#define MOS_DST_ROWS 20
#define MOS_DST_BYTES (MOS_DST_PITCH * MOS_DST_ROWS)

/* Above every value mos_pixel produces in the sampled area, so a byte still
   holding it was never written. */
#define MOS_GUARD 0xff

static unsigned char mos_src[MOS_SRC_BYTES];
static unsigned char mos_dst[MOS_DST_BYTES];

static int mos_pixel(int row, int col)
{
    return row * 16 + col + 1;
}

static void prepare_mosaic(void)
{
    int row;
    int col;

    memset(mos_dst, MOS_GUARD, (size_t) MOS_DST_BYTES);

    for (row = 0; row < MOS_SRC_ROWS; row++) {
        for (col = 0; col < MOS_SRC_PITCH; col++) {
            mos_src[row * MOS_SRC_PITCH + col] =
                (unsigned char) mos_pixel(row, col);
        }
    }
}

static int mos_dst_byte(int row, int col)
{
    return (int) mos_dst[row * MOS_DST_PITCH + col];
}

static int mos_dst_flat(int offset)
{
    return (int) mos_dst[offset];
}

/* An extent that divides exactly gives cell_cols = width / block_w with no
   extra cell, and each cell is flooded from the pixel at its own middle:
   block 2 over a 4x4 region samples (1,1), (1,3), (3,1) and (3,3), which is
   block/2 plus a whole block per step and nothing else.  A cell is flat --
   both corners checked hold the same byte -- and the region's own edges are
   respected. */
static void exact_division_floods_each_cell_from_its_middle_pixel(void)
{
    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, MOS_DST_PITCH,
                          4, 4, 2, 2);

    CHECK_EQ(mos_dst_byte(0, 0), mos_pixel(1, 1));
    CHECK_EQ(mos_dst_byte(1, 1), mos_pixel(1, 1));
    CHECK_EQ(mos_dst_byte(0, 2), mos_pixel(1, 3));
    CHECK_EQ(mos_dst_byte(1, 3), mos_pixel(1, 3));
    CHECK_EQ(mos_dst_byte(2, 0), mos_pixel(3, 1));
    CHECK_EQ(mos_dst_byte(3, 1), mos_pixel(3, 1));
    CHECK_EQ(mos_dst_byte(2, 2), mos_pixel(3, 3));
    CHECK_EQ(mos_dst_byte(3, 3), mos_pixel(3, 3));

    CHECK_EQ(mos_dst_byte(0, 4), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(4, 0), MOS_GUARD);
}

/* Width 14 over blocks of 4 is three whole cells and a 2-pixel remainder, so
   the ceiling gives a fourth cell that is 2 wide.  Its colour is the crux:
   sample_col reaches 14 after the third cell and is snapped to
   14 - 14%4 = 12, so the last two columns are flooded from source column 12.
   The clamped rewrite would compute min(3*4 + 2, 13) = 13 and take column 13
   instead, which is mos_pixel(1,13) -- one greater than what is asserted
   here. */
static void a_trailing_partial_column_is_sampled_at_its_first_pixel(void)
{
    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, MOS_DST_PITCH,
                          14, 2, 4, 2);

    CHECK_EQ(mos_dst_byte(0, 0), mos_pixel(1, 2));
    CHECK_EQ(mos_dst_byte(1, 3), mos_pixel(1, 2));
    CHECK_EQ(mos_dst_byte(0, 4), mos_pixel(1, 6));
    CHECK_EQ(mos_dst_byte(0, 8), mos_pixel(1, 10));
    CHECK_EQ(mos_dst_byte(0, 12), mos_pixel(1, 12));
    CHECK_EQ(mos_dst_byte(1, 13), mos_pixel(1, 12));

    /* The short cell is 2 wide, not 4: nothing is written past column 13. */
    CHECK_EQ(mos_dst_byte(0, 14), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(2, 0), MOS_GUARD);
}

/* The same snap in the other direction, and the same disagreement: height 14
   over blocks of 4 leaves a 2-row band whose source row is
   14 - 14%4 = 12, not the min(3*4 + 2, 13) = 13 the clamped rewrite would
   use.  This is the case the portrait pane actually hits -- 149 rows with a
   block of 7 -- reduced to numbers a buffer can hold. */
static void a_trailing_partial_band_is_sampled_at_its_first_row(void)
{
    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, MOS_DST_PITCH,
                          2, 14, 2, 4);

    CHECK_EQ(mos_dst_byte(0, 0), mos_pixel(2, 1));
    CHECK_EQ(mos_dst_byte(3, 1), mos_pixel(2, 1));
    CHECK_EQ(mos_dst_byte(4, 0), mos_pixel(6, 1));
    CHECK_EQ(mos_dst_byte(8, 0), mos_pixel(10, 1));
    CHECK_EQ(mos_dst_byte(12, 0), mos_pixel(12, 1));
    CHECK_EQ(mos_dst_byte(13, 1), mos_pixel(12, 1));

    /* The short band is 2 rows, not 4. */
    CHECK_EQ(mos_dst_byte(14, 0), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(0, 2), MOS_GUARD);
}

/* Both remainders at once, which is the shape every real call has: 7 over
   blocks of 3 is two whole cells and a 1-pixel remainder in each direction.
   The last band is one row and the last cell one column, and the three
   sample rows are 1, 4 and 6 -- the accumulator's 1 and 4, then the snap to
   7 - 7%3 = 6.  Cell interiors are checked away from their corners so a
   routine that painted only the sampled pixel would be seen. */
static void both_extents_partial_at_once(void)
{
    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, MOS_DST_PITCH,
                          7, 7, 3, 3);

    CHECK_EQ(mos_dst_byte(0, 0), mos_pixel(1, 1));
    CHECK_EQ(mos_dst_byte(2, 2), mos_pixel(1, 1));
    CHECK_EQ(mos_dst_byte(0, 3), mos_pixel(1, 4));
    CHECK_EQ(mos_dst_byte(0, 6), mos_pixel(1, 6));
    CHECK_EQ(mos_dst_byte(3, 0), mos_pixel(4, 1));
    CHECK_EQ(mos_dst_byte(5, 5), mos_pixel(4, 4));
    CHECK_EQ(mos_dst_byte(6, 0), mos_pixel(6, 1));
    CHECK_EQ(mos_dst_byte(6, 6), mos_pixel(6, 6));

    CHECK_EQ(mos_dst_byte(0, 7), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(7, 0), MOS_GUARD);
}

/* A block of 1 leaves both sample positions at 0 and both run lengths at 1,
   so the mosaic degenerates into a plain pixel-for-pixel copy.  That is the
   last step of the caller's fade-in sequence and the frame the portrait has
   to arrive undistorted in, so every pixel of the region is checked rather
   than a sample of them. */
static void a_block_of_one_copies_the_region_pixel_for_pixel(void)
{
    int row;
    int col;

    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, MOS_DST_PITCH,
                          3, 3, 1, 1);

    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            CHECK_EQ(mos_dst_byte(row, col), mos_pixel(row, col));
        }
    }

    CHECK_EQ(mos_dst_byte(0, 3), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(3, 0), MOS_GUARD);
}

/* A block bigger than the region is one cell that is shortened to the
   region's own extents -- but the sample position is NOT shortened with it.
   block 9 over a 3x3 region leaves both sample positions at 9/2 = 4, so the
   whole region is flooded from source pixel (4,4), which lies outside the
   region on both axes.  A rewrite that clamped the sample to the region
   would flood from (2,2) instead. */
static void the_sample_position_is_not_clamped_to_the_region(void)
{
    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, MOS_DST_PITCH,
                          3, 3, 9, 9);

    CHECK_EQ(mos_dst_byte(0, 0), mos_pixel(4, 4));
    CHECK_EQ(mos_dst_byte(2, 2), mos_pixel(4, 4));
    CHECK_EQ(mos_dst_byte(0, 3), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(3, 0), MOS_GUARD);
}

/* The source pitch scales the sample row and nothing else, and the
   destination pitch spaces the written scanlines and nothing else: neither
   is derived from the other or from an extent.  Doubling the source pitch
   makes sample row 1 land on source row 2, and a destination pitch of 5 puts
   the second written row five bytes on with the gap untouched. */
static void the_two_pitches_are_read_from_their_own_arguments(void)
{
    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH * 2, mos_dst, MOS_DST_PITCH,
                          2, 2, 2, 2);

    CHECK_EQ(mos_dst_byte(0, 0), mos_pixel(2, 1));
    CHECK_EQ(mos_dst_byte(1, 1), mos_pixel(2, 1));
    CHECK_EQ(mos_dst_byte(0, 2), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(2, 0), MOS_GUARD);

    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, 5, 2, 2, 2, 2);

    CHECK_EQ(mos_dst_flat(0), mos_pixel(1, 1));
    CHECK_EQ(mos_dst_flat(1), mos_pixel(1, 1));
    CHECK_EQ(mos_dst_flat(2), MOS_GUARD);
    CHECK_EQ(mos_dst_flat(4), MOS_GUARD);
    CHECK_EQ(mos_dst_flat(5), mos_pixel(1, 1));
    CHECK_EQ(mos_dst_flat(6), mos_pixel(1, 1));
    CHECK_EQ(mos_dst_flat(7), MOS_GUARD);
}

/* An extent of 0 divides exactly, so the ceiling adds no cell and that
   loop's count is 0.  A width of 0 leaves the cell-column loop unentered on
   every band and a height of 0 leaves the cell-row loop unentered outright;
   either way nothing is written.  The extents are signed, so this is the
   guard that a zero region does not become four billion cells. */
static void a_zero_extent_paints_nothing(void)
{
    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, MOS_DST_PITCH,
                          0, 4, 2, 2);
    CHECK_EQ(mos_dst_byte(0, 0), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(3, 0), MOS_GUARD);

    prepare_mosaic();
    fdps_blit_mosaic_rect(mos_src, MOS_SRC_PITCH, mos_dst, MOS_DST_PITCH,
                          4, 0, 2, 2);
    CHECK_EQ(mos_dst_byte(0, 0), MOS_GUARD);
    CHECK_EQ(mos_dst_byte(0, 3), MOS_GUARD);
}

/* ------------------------------------------------------------------ *
 * fdps_blit_tint_rect @ 00030010
 *
 * Expected values come from the assembly: the signed CMP [EBP+0x38],0x8 / JG
 * at 0003001c that folds alpha, the MOV EAX,0x10 / SUB EAX,[EBP+0x38] at
 * 00030040 that rewrites it, the 0x900 row offsets at 00030029 and 00030032
 * that are applied BEFORE the LEA EAX,[EAX*0x4+0x0] at 0003005f and 000300b6
 * and so count entries rather than bytes, the SHL EAX,0xa at 00030051 that
 * scales the folded alpha into a row base, the two signed CMP / JL loop tests
 * at 00030078 and 00030094, the XOR EAX,EAX / MOV AL,[EDX] load at 000300a9,
 * the ADD / SAR 0x4 / AND 0xf0f0f / (AND 0xffff | SAR 0xc) sequence at
 * 000300c5..000300e9, and the unconditional MOV byte ptr [EDX],AL at 000300fa.
 * The routine has no CALL in it at all, so nothing below depends on what a
 * library routine hands back.
 *
 * HOW A CASE NAMES THE EXACT CUBE ENTRY THAT WAS READ.  Both tables are
 * arguments here rather than globals, so each case builds its own.  The cube
 * is filled with a guard byte and only the entries a case expects are marked,
 * so a routine that computed any other index reads the guard and the assertion
 * fails with it.  That turns "the destination byte is 0x2a" into "index 0x537
 * and no other index was used".
 *
 * WHERE THE INDICES COME FROM.  0x537 is what the assembly's own sequence
 * makes of the scaled sum 0x00305070: SAR 4 gives 0x00030507, AND 0xf0f0f
 * leaves it alone, AND 0xffff gives 0x0507 and SAR 0xc gives 0x30, so the OR
 * is 0x537.  0xaf3 is the same sequence over 0x00f5a73b -> 0x000f5a73 ->
 * 0x000f0a03 -> 0x0a03 | 0xf0.  0x213 is 0x00102030 -> 0x00010203 -> 0x0203 |
 * 0x10.  None is read off the emitted C.
 *
 * The ramp built here is the full [18][256] the routine indexes, so a case
 * that names a row nine on from another is really reaching the entry the
 * original would.
 * ------------------------------------------------------------------ */

#define TINT_TABLE_ROWS 18
#define TINT_ROW_ENTRIES 256
#define TINT_TABLE_SIZE (TINT_TABLE_ROWS * TINT_ROW_ENTRIES)

#define TINT_CUBE_SIZE 4096

/* No palette index any case marks, so a destination byte holding it was read
   from an unmarked cube entry. */
#define TINT_CUBE_GUARD 0xff

/* Not a value any cube entry is marked with either, so a destination byte
   holding it was never written at all. */
#define TINT_DST_GUARD 0xd3

#define TINT_SRC_PITCH 16
#define TINT_SRC_ROWS 8
#define TINT_SRC_BYTES (TINT_SRC_PITCH * TINT_SRC_ROWS)

#define TINT_DST_PITCH 12
#define TINT_DST_ROWS 8
#define TINT_DST_BYTES (TINT_DST_PITCH * TINT_DST_ROWS)

static unsigned int tint_ramp[TINT_TABLE_SIZE];
static unsigned char tint_cube[TINT_CUBE_SIZE];
static unsigned char tint_src[TINT_SRC_BYTES];
static unsigned char tint_dst[TINT_DST_BYTES];

static void prepare_tint(void)
{
    int i;

    for (i = 0; i < TINT_TABLE_SIZE; i++) {
        tint_ramp[i] = 0;
    }

    memset(tint_cube, TINT_CUBE_GUARD, (size_t) TINT_CUBE_SIZE);
    memset(tint_src, 0, (size_t) TINT_SRC_BYTES);
    memset(tint_dst, TINT_DST_GUARD, (size_t) TINT_DST_BYTES);
}

static void set_ramp(int weight_row, int entry, unsigned int scaled_color)
{
    tint_ramp[weight_row * TINT_ROW_ENTRIES + entry] = scaled_color;
}

static int tint_dst_byte(int offset)
{
    return (int) tint_dst[offset];
}

/* Row 9 is what a call with alpha 0 reads its source pixels from, so filling
   it with a value that carries the pixel's two nibbles into two different
   channel slots turns the routine into a lookup that names the source pixel:
   pixel p produces index (p >> 4) << 8 | (p & 0x0f), whose cube entry is
   marked 0x40 + p.  Only pixels below 0x40 are mapped, so a case that read a
   pixel it did not mean to still lands on the guard.  This is what lets the
   geometry cases below say which source pixel arrived at which destination
   byte instead of only that something did. */
#define TINT_PASS_LIMIT 0x40

static int tint_pass_index(int pixel)
{
    return ((pixel >> 4) << 8) | (pixel & 0x0f);
}

static int tint_pass_byte(int pixel)
{
    return 0x40 + pixel;
}

static void prepare_tint_passthrough(void)
{
    int pixel;

    prepare_tint();

    for (pixel = 0; pixel < TINT_PASS_LIMIT; pixel++) {
        set_ramp(9, pixel,
                 (unsigned int) (((pixel & 0x0f) << 4)
                                 | (((pixel >> 4) & 0x0f) << 12)));
        tint_cube[tint_pass_index(pixel)] =
            (unsigned char) tint_pass_byte(pixel);
    }
}

/* alpha 8 or below leaves alpha alone and gives the tint row offset 0, so the
   constant colour is read from row alpha itself.  Row 9+alpha carries a decoy
   whose index is 0x777, an entry that is never marked, so a routine that took
   the tint from the complementary block fails on the guard.  The second call
   moves alpha by one and the tint entry moves a row with it. */
static void the_tint_is_read_from_the_alpha_row(void)
{
    prepare_tint();
    set_ramp(3, 0x11, 0x00305070);
    set_ramp(12, 0x11, 0x00707070);
    tint_cube[0x537] = 0x2a;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0x11, 3);
    CHECK_EQ(tint_dst_byte(0), 0x2a);

    prepare_tint();
    set_ramp(3, 0x11, 0x00305070);
    set_ramp(4, 0x11, 0x00f5a73b);
    tint_cube[0x537] = 0x2a;
    tint_cube[0xaf3] = 0x2b;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0x11, 4);
    CHECK_EQ(tint_dst_byte(0), 0x2b);
}

/* The other half of the same fold: with alpha 8 or below the source pixels
   carry offset 0x900, which is nine rows and not 0x900 bytes, so pixel 0x22 is
   read from row 12 rather than row 3.  The decoy sits at row 3 entry 0x22,
   where a routine that gave the source pixels the tint's offset would look. */
static void the_source_pixel_is_read_from_nine_rows_past_the_alpha_row(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(12, 0x22, 0x00305070);
    set_ramp(3, 0x22, 0x00808080);
    tint_cube[0x537] = 0x3b;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0x00, 3);

    CHECK_EQ(tint_dst_byte(0), 0x3b);
}

/* alpha above 8 becomes 16-alpha and the two offsets change places, so alpha
   12 reads its tint from row 13 and its source pixels from row 4.  Both
   contributions are non-zero here and neither alone reaches 0x537, so this
   also pins that the two scaled colours are added rather than one of them
   being used: 0x00102030 on its own indexes 0x213 and 0x00203040 on its own
   indexes 0x324, and neither of those entries is marked. */
static void alpha_above_eight_folds_and_swaps_the_two_rows(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(13, 0x11, 0x00102030);
    set_ramp(4, 0x22, 0x00203040);
    set_ramp(4, 0x11, 0x00808080);
    set_ramp(13, 0x22, 0x00808080);
    tint_cube[0x537] = 0x4c;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0x11, 12);

    CHECK_EQ(tint_dst_byte(0), 0x4c);
}

/* The fold's test is JG against 8, so 8 itself takes the unfolded side and 9
   is the first value that folds.  At alpha 8 the tint is row 8; at alpha 9 it
   folds to 7 and the tint moves to row 16.  Each call carries the other
   branch's row as a decoy. */
static void the_fold_turns_over_between_eight_and_nine(void)
{
    prepare_tint();
    set_ramp(8, 0x11, 0x00305070);
    set_ramp(17, 0x11, 0x00707070);
    tint_cube[0x537] = 0x61;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0x11, 8);
    CHECK_EQ(tint_dst_byte(0), 0x61);

    prepare_tint();
    set_ramp(16, 0x11, 0x00f5a73b);
    set_ramp(9, 0x11, 0x00707070);
    tint_cube[0xaf3] = 0x62;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0x11, 9);
    CHECK_EQ(tint_dst_byte(0), 0x62);
}

/* The two ends of the range the callers actually walk.  alpha 0 is rows 0 and
   9; alpha 0x10 folds to 0 and becomes rows 9 and 0 -- the same pair the other
   way round, which is what makes 0 source-only and 0x10 tint-only once the
   real weights are in the table. */
static void alpha_zero_and_alpha_sixteen_use_the_same_pair_reversed(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(0, 0x11, 0x00102030);
    set_ramp(9, 0x22, 0x00203040);
    tint_cube[0x537] = 0x71;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0x11, 0);
    CHECK_EQ(tint_dst_byte(0), 0x71);

    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(9, 0x11, 0x00102030);
    set_ramp(0, 0x22, 0x00203040);
    tint_cube[0x537] = 0x72;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0x11, 16);
    CHECK_EQ(tint_dst_byte(0), 0x72);
}

/* The sum is shifted right by four and masked to one nibble per channel, so
   the low nibble of each byte and the high nibble of each byte after the shift
   are both thrown away.  0x00f5a73b and 0x00f0a030 differ in exactly those
   discarded bits and reach the same entry, 0xaf3 -- a routine that masked
   before shifting, or that kept a whole byte per channel, would separate
   them. */
static void the_shift_and_mask_keep_one_nibble_per_channel(void)
{
    prepare_tint();
    set_ramp(0, 1, 0x00f5a73b);
    set_ramp(0, 2, 0x00f0a030);
    tint_cube[0xaf3] = 0x5d;

    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 1, 0);
    CHECK_EQ(tint_dst_byte(0), 0x5d);

    memset(tint_dst, TINT_DST_GUARD, (size_t) TINT_DST_BYTES);
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 2, 0);
    CHECK_EQ(tint_dst_byte(0), 0x5d);
}

/* The fold (v & 0xffff) | (v >> 12) puts green above red: for the masked value
   0x00010203 -- red 1, green 2, blue 3 in the 0x000R0G0B layout the ramp holds
   -- it produces 0x213, green in bits 8..11, red in bits 4..7 and blue in bits
   0..3.  That is the order fdps_build_palette_tables fills the cube in, green
   outermost.  The straightforward r:g:b packing would give 0x123 instead, and
   that entry is marked with a different byte here so the two are told apart
   rather than merely one of them being confirmed. */
static void the_index_transposes_the_middle_two_channels(void)
{
    prepare_tint();
    set_ramp(0, 1, 0x00102030);
    tint_cube[0x213] = 0x6e;
    tint_cube[0x123] = 0x6f;

    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 1, 0);

    CHECK_EQ(tint_dst_byte(0), 0x6e);
}

/* The source byte is loaded XOR EAX,EAX / MOV AL, so pixel 0xff indexes entry
   255 of the source row.  Sign-extended it would be -1, which lands one entry
   before the row -- row 8 entry 255 -- and that is where the decoy sits. */
static void the_source_pixel_is_zero_extended(void)
{
    prepare_tint();
    tint_src[0] = 0xff;
    set_ramp(9, 255, 0x00305070);
    set_ramp(8, 255, 0x00707070);
    tint_cube[0x537] = 0x5f;

    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0, 0);

    CHECK_EQ(tint_dst_byte(0), 0x5f);
}

/* There is no transparency test in this routine at all: a source pixel of 0 is
   blended and stored like any other, and the destination byte under it does
   not survive.  fdps_blit_transparent_rect above is the one that keys colour 0
   out, and a rewrite that carried that behaviour over here would leave the
   guard byte in place. */
static void a_source_pixel_of_zero_is_still_written(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 0;
    tint_src[1] = 1;

    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        2, 1, tint_ramp, tint_cube, 0, 0);

    CHECK_EQ(tint_dst_byte(0), tint_pass_byte(0));
    CHECK_EQ(tint_dst_byte(1), tint_pass_byte(1));
}

/* Every pixel of the rectangle is written and nothing outside it is: three
   columns on each of two rows, with the source rows a whole source pitch apart
   and the destination rows a whole destination pitch apart.  The two pitches
   differ from each other and from the width, so a routine that used one for
   the other would land where the guard still is. */
static void every_pixel_of_the_rectangle_is_written(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 1;
    tint_src[1] = 2;
    tint_src[2] = 3;
    tint_src[TINT_SRC_PITCH + 0] = 4;
    tint_src[TINT_SRC_PITCH + 1] = 5;
    tint_src[TINT_SRC_PITCH + 2] = 6;

    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        3, 2, tint_ramp, tint_cube, 0, 0);

    CHECK_EQ(tint_dst_byte(0), tint_pass_byte(1));
    CHECK_EQ(tint_dst_byte(1), tint_pass_byte(2));
    CHECK_EQ(tint_dst_byte(2), tint_pass_byte(3));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 0), tint_pass_byte(4));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 1), tint_pass_byte(5));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 2), tint_pass_byte(6));

    /* Exactly width bytes on exactly height rows: the gap the destination
       pitch leaves between the rows and the row past the last are untouched. */
    CHECK_EQ(tint_dst_byte(3), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH - 1), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 3), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 2), TINT_DST_GUARD);
}

/* Each cursor is advanced by adding its own stride, nothing is scaled and
   nothing is made unsigned, so a negative stride walks that side of the
   transfer backwards: the source rows are read at 32, 16 and 0 while the
   destination rows land at 36, 24 and 12. */
static void negative_tint_strides_walk_both_cursors_backwards(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 1;
    tint_src[TINT_SRC_PITCH] = 2;
    tint_src[TINT_SRC_PITCH * 2] = 3;

    fdps_blit_tint_rect(tint_src + TINT_SRC_PITCH * 2, -TINT_SRC_PITCH,
                        tint_dst + TINT_DST_PITCH * 3, -TINT_DST_PITCH,
                        1, 3, tint_ramp, tint_cube, 0, 0);

    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 3), tint_pass_byte(3));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 2), tint_pass_byte(2));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 1), tint_pass_byte(1));
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 4), TINT_DST_GUARD);
}

/* Both extents are compared with JL, a signed test, so 0 or below on either
   writes nothing.  Read as unsigned, -1 would be four billion rows or columns
   and would walk the blit off the end of memory. */
static void both_tint_extents_are_signed_counts(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 1;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 0, tint_ramp, tint_cube, 0, 0);
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);

    prepare_tint_passthrough();
    tint_src[0] = 1;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, -1, tint_ramp, tint_cube, 0, 0);
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);

    prepare_tint_passthrough();
    tint_src[0] = 1;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        0, 2, tint_ramp, tint_cube, 0, 0);
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH), TINT_DST_GUARD);

    prepare_tint_passthrough();
    tint_src[0] = 1;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        -1, 2, tint_ramp, tint_cube, 0, 0);
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);

    /* One row of one pixel means exactly that. */
    prepare_tint_passthrough();
    tint_src[0] = 1;
    tint_src[1] = 2;
    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp, tint_cube, 0, 0);
    CHECK_EQ(tint_dst_byte(0), tint_pass_byte(1));
    CHECK_EQ(tint_dst_byte(1), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH), TINT_DST_GUARD);
}

/* alpha is signed too: CMP dword ptr [EBP+0x38],0x8 / JG at 0003001c is a
   signed compare, so a negative alpha takes the unfolded side and scales into
   a row BEFORE the table's own base.  The table pointer handed over here is
   one row into the array so that row -1 is a real row, and alpha -1 then reads
   its tint from array row 0 and its source pixels from array row 9 -- the
   passthrough row -- which is what the assertion sees.  Read as unsigned,
   alpha would be 0xffffffff, fail the JG, become 17 and index a row 0x1100
   entries past the pointer instead. */
static void alpha_is_a_signed_value(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 5;

    fdps_blit_tint_rect(tint_src, TINT_SRC_PITCH, tint_dst, TINT_DST_PITCH,
                        1, 1, tint_ramp + TINT_ROW_ENTRIES, tint_cube, 0, -1);

    CHECK_EQ(tint_dst_byte(0), tint_pass_byte(5));
}

/* ------------------------------------------------------------------
 * fdps_blit_tint_transparent_rect, 00030120.
 *
 * The same ramp, cube, source and destination fixtures as the tinting blit
 * above -- the routine reads its two tables exactly the same way, so the same
 * marked entries name the same indices -- with one thing changed in every case:
 * a source pixel that must survive the key is non-zero.  Where a case above
 * left the source byte at 0 because the blend ran anyway, the case below sets
 * it to 0x22 and leaves that entry of the source row unmarked, so the source
 * term is still 0 and the sum, the index and the expected byte are unchanged
 * while the pixel is no longer keyed out.
 *
 * The indices 0x537, 0xaf3 and 0x213 are the ones derived in the section
 * header above, from the assembly's own SAR 4 / AND 0xf0f0f / (v & 0xffff) |
 * (v >> 12) sequence at 000301e1-000301ff.  None is read off the emitted C.
 * ------------------------------------------------------------------ */

/* CMP dword ptr [EBP+0x38],0x8 / JG at 0003012c gives the tint row offset 0 for
   alpha 8 or below, so the constant colour comes from row alpha itself.  Row
   9+alpha carries a decoy indexing 0x777, an entry never marked, so taking the
   tint from the complementary block lands on the cube guard.  The second call
   moves alpha by one and the tint entry moves a row with it. */
static void keyed_tint_is_read_from_the_alpha_row(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(3, 0x11, 0x00305070);
    set_ramp(12, 0x11, 0x00707070);
    tint_cube[0x537] = 0x2a;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    0x11, 3);
    CHECK_EQ(tint_dst_byte(0), 0x2a);

    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(3, 0x11, 0x00305070);
    set_ramp(4, 0x11, 0x00f5a73b);
    tint_cube[0x537] = 0x2a;
    tint_cube[0xaf3] = 0x2b;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    0x11, 4);
    CHECK_EQ(tint_dst_byte(0), 0x2b);
}

/* The other half of the fold: with alpha 8 or below the source pixels carry
   offset 0x900, which is nine rows and not 0x900 bytes, so pixel 0x22 is read
   from row 12 rather than row 3.  The decoy sits at row 3 entry 0x22, where a
   routine that gave the source pixels the tint's offset would look. */
static void keyed_source_pixel_is_read_from_nine_rows_past_the_alpha_row(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(12, 0x22, 0x00305070);
    set_ramp(3, 0x22, 0x00808080);
    tint_cube[0x537] = 0x3b;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    0x00, 3);

    CHECK_EQ(tint_dst_byte(0), 0x3b);
}

/* alpha above 8 becomes 16-alpha and the two offsets change places, so alpha 12
   reads its tint from row 13 and its source pixels from row 4.  Both
   contributions are non-zero and neither alone reaches 0x537 -- 0x00102030 on
   its own indexes 0x213 and 0x00203040 indexes 0x324, neither of them marked --
   so this also pins that the two scaled colours are added rather than one being
   dropped now that the key stands between them. */
static void keyed_alpha_above_eight_folds_and_swaps_the_two_rows(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(13, 0x11, 0x00102030);
    set_ramp(4, 0x22, 0x00203040);
    set_ramp(4, 0x11, 0x00808080);
    set_ramp(13, 0x22, 0x00808080);
    tint_cube[0x537] = 0x4c;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    0x11, 12);

    CHECK_EQ(tint_dst_byte(0), 0x4c);
}

/* The fold's test is JG against 8, so 8 itself takes the unfolded side and 9 is
   the first value that folds: at alpha 8 the tint is row 8, at alpha 9 it folds
   to 7 and the tint moves to row 16.  Each call carries the other branch's row
   as a decoy.  The source pixel is 0x22 in both and neither source row has that
   entry marked, so the sum is the tint's alone. */
static void keyed_fold_turns_over_between_eight_and_nine(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(8, 0x11, 0x00305070);
    set_ramp(17, 0x11, 0x00707070);
    tint_cube[0x537] = 0x61;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    0x11, 8);
    CHECK_EQ(tint_dst_byte(0), 0x61);

    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(16, 0x11, 0x00f5a73b);
    set_ramp(9, 0x11, 0x00707070);
    tint_cube[0xaf3] = 0x62;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    0x11, 9);
    CHECK_EQ(tint_dst_byte(0), 0x62);
}

/* The sum is shifted right by four and then masked to one nibble per channel,
   so the low nibble of each byte and the high nibble of each byte after the
   shift are both thrown away.  0x00f5a73b and 0x00f0a030 differ in exactly
   those discarded bits and reach the same entry, 0xaf3; masking before shifting
   or keeping a whole byte per channel would separate them. */
static void keyed_shift_and_mask_keep_one_nibble_per_channel(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(0, 1, 0x00f5a73b);
    set_ramp(0, 2, 0x00f0a030);
    tint_cube[0xaf3] = 0x5d;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    1, 0);
    CHECK_EQ(tint_dst_byte(0), 0x5d);

    memset(tint_dst, TINT_DST_GUARD, (size_t) TINT_DST_BYTES);
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    2, 0);
    CHECK_EQ(tint_dst_byte(0), 0x5d);
}

/* The fold (v & 0xffff) | (v >> 12) puts green above red: the masked value
   0x00010203 -- red 1, green 2, blue 3 in the ramp's 0x000R0G0B layout --
   produces 0x213 and not the straightforward 0x123, and both entries are marked
   with different bytes so the two packings are told apart. */
static void keyed_index_transposes_the_middle_two_channels(void)
{
    prepare_tint();
    tint_src[0] = 0x22;
    set_ramp(0, 1, 0x00102030);
    tint_cube[0x213] = 0x6e;
    tint_cube[0x123] = 0x6f;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    1, 0);

    CHECK_EQ(tint_dst_byte(0), 0x6e);
}

/* XOR EAX,EAX / MOV AL at 000301b9 loads the source byte zero-extended, so
   pixel 0xff is entry 255 of the source row and passes the key.  Sign-extended
   it would be -1, one entry before the row -- row 8 entry 255 -- and that is
   where the decoy sits. */
static void keyed_source_pixel_is_zero_extended(void)
{
    prepare_tint();
    tint_src[0] = 0xff;
    set_ramp(9, 255, 0x00305070);
    set_ramp(8, 255, 0x00707070);
    tint_cube[0x537] = 0x5f;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    0, 0);

    CHECK_EQ(tint_dst_byte(0), 0x5f);
}

/* The whole difference from fdps_blit_tint_rect.  The tint here is non-zero and
   on its own indexes 0x213, which is marked: a routine that blended every pixel
   and stored the result would put 0x33 under the transparent pixel, and one
   that stored the tint alone would put it there too.  The destination guard
   surviving is what says the store never happened.  The neighbouring pixel is
   0x22 and reaches 0x537, so the same call proves the blend still runs for a
   pixel that passes the key. */
static void a_zero_source_pixel_is_keyed_out_and_the_destination_survives(void)
{
    prepare_tint();
    tint_src[0] = 0;
    tint_src[1] = 0x22;
    set_ramp(0, 0x11, 0x00102030);
    set_ramp(9, 0x22, 0x00203040);
    tint_cube[0x213] = 0x33;
    tint_cube[0x537] = 0x34;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 2, 1, tint_ramp, tint_cube,
                                    0x11, 0);

    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(1), 0x34);
}

/* Every non-zero pixel of the rectangle is blended and nothing else is written:
   three columns on each of two rows, with a transparent pixel in the middle of
   the second row.  Both pitches differ from each other and from the width, and
   the row advance sits outside the key, so the third pixel of the second row
   still lands where it should even though the pixel before it was skipped. */
static void every_non_zero_pixel_of_the_rectangle_is_blended(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 1;
    tint_src[1] = 2;
    tint_src[2] = 3;
    tint_src[TINT_SRC_PITCH + 0] = 4;
    tint_src[TINT_SRC_PITCH + 1] = 0;
    tint_src[TINT_SRC_PITCH + 2] = 6;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 3, 2, tint_ramp, tint_cube,
                                    0, 0);

    CHECK_EQ(tint_dst_byte(0), tint_pass_byte(1));
    CHECK_EQ(tint_dst_byte(1), tint_pass_byte(2));
    CHECK_EQ(tint_dst_byte(2), tint_pass_byte(3));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 0), tint_pass_byte(4));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 1), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 2), tint_pass_byte(6));

    /* Exactly width bytes on exactly height rows: the gap the destination pitch
       leaves between the rows and the row past the last are untouched. */
    CHECK_EQ(tint_dst_byte(3), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH - 1), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 3), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 2), TINT_DST_GUARD);
}

/* A row with no surviving pixel in it still steps both cursors: ADD dword ptr
   [EBP+0x14],EAX and ADD dword ptr [EBP+0x1c],EAX at 00030217 and 0003021d are
   reached from the column loop's exit, not from inside the key.  The first
   source row is entirely transparent and the second is not, so an advance that
   ran only when something was stored would put the second row's pixel on the
   first destination row. */
static void a_fully_transparent_row_still_advances_both_cursors(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 0;
    tint_src[1] = 0;
    tint_src[TINT_SRC_PITCH + 0] = 7;
    tint_src[TINT_SRC_PITCH + 1] = 8;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 2, 2, tint_ramp, tint_cube,
                                    0, 0);

    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(1), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 0), tint_pass_byte(7));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH + 1), tint_pass_byte(8));
}

/* Each cursor is advanced by adding its own stride and nothing is made
   unsigned, so a negative stride walks that side backwards: the source rows are
   read at 32, 16 and 0 while the destination rows land at 36, 24 and 12. */
static void keyed_negative_strides_walk_both_cursors_backwards(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 1;
    tint_src[TINT_SRC_PITCH] = 2;
    tint_src[TINT_SRC_PITCH * 2] = 3;

    fdps_blit_tint_transparent_rect(tint_src + TINT_SRC_PITCH * 2,
                                    -TINT_SRC_PITCH,
                                    tint_dst + TINT_DST_PITCH * 3,
                                    -TINT_DST_PITCH, 1, 3, tint_ramp, tint_cube,
                                    0, 0);

    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 3), tint_pass_byte(3));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 2), tint_pass_byte(2));
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 1), tint_pass_byte(1));
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH * 4), TINT_DST_GUARD);
}

/* Both extents are compared with JL at 0003018b and 000301a7, a signed test, so
   0 or below on either writes nothing.  Read as unsigned, -1 would be four
   billion rows or columns. */
static void keyed_extents_are_signed_counts(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 1;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 0, tint_ramp, tint_cube,
                                    0, 0);
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);

    prepare_tint_passthrough();
    tint_src[0] = 1;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, -1, tint_ramp, tint_cube,
                                    0, 0);
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);

    prepare_tint_passthrough();
    tint_src[0] = 1;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 0, 2, tint_ramp, tint_cube,
                                    0, 0);
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH), TINT_DST_GUARD);

    prepare_tint_passthrough();
    tint_src[0] = 1;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, -1, 2, tint_ramp, tint_cube,
                                    0, 0);
    CHECK_EQ(tint_dst_byte(0), TINT_DST_GUARD);

    /* One row of one pixel means exactly that. */
    prepare_tint_passthrough();
    tint_src[0] = 1;
    tint_src[1] = 2;
    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1, tint_ramp, tint_cube,
                                    0, 0);
    CHECK_EQ(tint_dst_byte(0), tint_pass_byte(1));
    CHECK_EQ(tint_dst_byte(1), TINT_DST_GUARD);
    CHECK_EQ(tint_dst_byte(TINT_DST_PITCH), TINT_DST_GUARD);
}

/* CMP dword ptr [EBP+0x38],0x8 / JG at 0003012c is a signed compare, so a
   negative alpha takes the unfolded side and scales into a row BEFORE the
   table's own base.  The pointer handed over is one row in so that row -1 is a
   real row: alpha -1 reads its tint from array row 0 and its source pixels from
   array row 9, the passthrough row.  Read as unsigned, alpha would fail the JG,
   become 17 and index 0x1100 entries past the pointer. */
static void keyed_alpha_is_a_signed_value(void)
{
    prepare_tint_passthrough();
    tint_src[0] = 5;

    fdps_blit_tint_transparent_rect(tint_src, TINT_SRC_PITCH, tint_dst,
                                    TINT_DST_PITCH, 1, 1,
                                    tint_ramp + TINT_ROW_ENTRIES, tint_cube, 0,
                                    -1);

    CHECK_EQ(tint_dst_byte(0), tint_pass_byte(5));
}

void run_blit_tests(void)
{
    RUN_TEST(pitch_four_paints_a_three_by_three_square);
    RUN_TEST(pitch_four_leaves_the_fourth_column_and_row_alone);
    RUN_TEST(nothing_is_painted_before_the_corner);
    RUN_TEST(pitch_three_paints_a_two_by_two_square);
    RUN_TEST(rows_are_one_scanline_apart);
    RUN_TEST(the_row_argument_is_scaled_by_the_screen_pitch);
    RUN_TEST(only_the_low_byte_of_color_is_painted);
    RUN_TEST(pitch_one_paints_nothing_and_pitch_two_paints_one_byte);
    RUN_TEST(pitch_zero_paints_nothing);

    RUN_TEST(copy_mode_uses_independent_strides);
    RUN_TEST(rows_is_a_signed_count);
    RUN_TEST(zero_src_stride_fills_instead_of_copying);
    RUN_TEST(fill_mode_writes_only_the_low_byte);
    RUN_TEST(a_row_that_overlaps_itself_is_moved_not_smeared);
    RUN_TEST(a_negative_destination_stride_walks_backwards);
    RUN_TEST(a_zero_row_length_transfers_nothing);

    RUN_TEST(a_zero_source_byte_leaves_the_destination_pixel_alone);
    RUN_TEST(every_non_zero_byte_is_stored_verbatim);
    RUN_TEST(independent_strides_are_applied_to_their_own_cursors);
    RUN_TEST(both_extents_are_signed_counts);
    RUN_TEST(a_zero_source_stride_repeats_one_source_row);
    RUN_TEST(negative_strides_walk_both_cursors_backwards);

    RUN_TEST(exact_division_floods_each_cell_from_its_middle_pixel);
    RUN_TEST(a_trailing_partial_column_is_sampled_at_its_first_pixel);
    RUN_TEST(a_trailing_partial_band_is_sampled_at_its_first_row);
    RUN_TEST(both_extents_partial_at_once);
    RUN_TEST(a_block_of_one_copies_the_region_pixel_for_pixel);
    RUN_TEST(the_sample_position_is_not_clamped_to_the_region);
    RUN_TEST(the_two_pitches_are_read_from_their_own_arguments);
    RUN_TEST(a_zero_extent_paints_nothing);

    RUN_TEST(the_tint_is_read_from_the_alpha_row);
    RUN_TEST(the_source_pixel_is_read_from_nine_rows_past_the_alpha_row);
    RUN_TEST(alpha_above_eight_folds_and_swaps_the_two_rows);
    RUN_TEST(the_fold_turns_over_between_eight_and_nine);
    RUN_TEST(alpha_zero_and_alpha_sixteen_use_the_same_pair_reversed);
    RUN_TEST(the_shift_and_mask_keep_one_nibble_per_channel);
    RUN_TEST(the_index_transposes_the_middle_two_channels);
    RUN_TEST(the_source_pixel_is_zero_extended);
    RUN_TEST(a_source_pixel_of_zero_is_still_written);
    RUN_TEST(every_pixel_of_the_rectangle_is_written);
    RUN_TEST(negative_tint_strides_walk_both_cursors_backwards);
    RUN_TEST(both_tint_extents_are_signed_counts);
    RUN_TEST(alpha_is_a_signed_value);

    RUN_TEST(keyed_tint_is_read_from_the_alpha_row);
    RUN_TEST(keyed_source_pixel_is_read_from_nine_rows_past_the_alpha_row);
    RUN_TEST(keyed_alpha_above_eight_folds_and_swaps_the_two_rows);
    RUN_TEST(keyed_fold_turns_over_between_eight_and_nine);
    RUN_TEST(keyed_shift_and_mask_keep_one_nibble_per_channel);
    RUN_TEST(keyed_index_transposes_the_middle_two_channels);
    RUN_TEST(keyed_source_pixel_is_zero_extended);
    RUN_TEST(a_zero_source_pixel_is_keyed_out_and_the_destination_survives);
    RUN_TEST(every_non_zero_pixel_of_the_rectangle_is_blended);
    RUN_TEST(a_fully_transparent_row_still_advances_both_cursors);
    RUN_TEST(keyed_negative_strides_walk_both_cursors_backwards);
    RUN_TEST(keyed_extents_are_signed_counts);
    RUN_TEST(keyed_alpha_is_a_signed_value);
}
