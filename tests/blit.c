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
}
