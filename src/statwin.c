/* statwin.c -- the battle unit status window.
 *
 * See statwin.h for the window's geometry and what a caller has to know.
 * Nothing here owns state: every routine works on the images and the step
 * number it is handed.
 *
 * inp comes from <conio.h> and delay from <i86.h>, which is where Watcom
 * 10.0a declares them, and both are ordinary calls in the original rather
 * than an inline IN instruction: 00016e80 issues CALL 0003d4e4 and CALL
 * 0003d370.  Watcom only turns inp into an instruction when -oi is given, and
 * it is not in this build's flag set (rebuild_info/build_flags.md).
 */
#include <conio.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "blit.h"
#include "statwin.h"

/* The VGA graphics aperture as a flat linear address, the mode 13h scanline
   pitch, the row count and the size of one whole frame.  All four are
   hard-coded in the original (PUSH 0xa0000, PUSH 0x140, 0xc8, PUSH 0xfa00)
   and stay literals here: 0xa0000 is where the display adapter answers, not
   the address of anything the linker places, so there is no symbol to
   reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES 0xfa00

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit this file looks at. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* How many steps the slide-in has, and so how long each of the four offset
   tables is.  Both callers that open the window walk 0..8 (CMP ...,0x9 / JL
   at 00016bc3 and 00025a8e); the one that closes it walks 5 down to 0. */
#define ANIM_STEP_COUNT 9

/* PUSH 0xf / CALL delay at 00017067: every step holds its frame for fifteen
   milliseconds on top of the retrace wait. */
#define ANIM_FRAME_DELAY_MS 0xf

/* Where the four panels sit once the window has finished opening, in pixels.
   The two left panels share a column span and the two right ones share
   another, and the two spans meet exactly: LEFT_COLUMN_X + LEFT_COLUMN_WIDTH
   is RIGHT_COLUMN_X.  The horizontal split between the panels is at a
   different row on each side -- row 108 on the left, row 44 on the right --
   which is why the four heights are not two pairs. */
#define LEFT_COLUMN_X 0x0f
#define LEFT_COLUMN_WIDTH 0x85
#define UPPER_LEFT_HEIGHT 0x6c
#define LOWER_LEFT_Y 0x6c
#define RIGHT_COLUMN_X 0x94
#define RIGHT_COLUMN_WIDTH 0x9e
#define UPPER_RIGHT_HEIGHT 0x2c
#define LOWER_RIGHT_Y 0x2c
#define LOWER_RIGHT_HEIGHT 0x9c

/* 00016e80.  The four tables are the four panels' positions, one entry per
   step, and each panel travels along one axis only: the two left panels are
   fixed in the other axis by LEFT_COLUMN_X, the two right ones by
   RIGHT_COLUMN_X and LOWER_RIGHT_Y.  A position outside the screen means the
   panel is still partly off that edge, and each blit below is cut down to
   whatever part of it is on screen -- there is no clipping anywhere else, so
   these four tests are the whole of it.

   The tables live in the original as four nine-int local arrays initialised
   from templates at 0x14788, 0x147ac, 0x147d0 and 0x147f4, copied in with
   four REP MOVSD of ECX=9 at the top of the frame.  They are locals and not
   globals: nothing else in the image reads those addresses, and emitting them
   as data symbols would put four arrays in the rebuild that the original does
   not have as separate objects.

   THE ENTRIES ARE SIGNED AND THREE TABLES REALLY DO GO NEGATIVE.  CMP dword
   ptr [EAX+EBP-0x30],0x0 / JGE at 00016eff is the signed test, and the first
   five entries of upper_left_x and of upper_right_y are below zero.  Read as
   unsigned, upper_left_x[0] is about four billion, the JGE branch is taken,
   and the first frame blits 133 columns from four billion bytes past the
   window image.

   Steps 6 and 7 step back one pixel from step 5 before step 8 returns to it:
   the window overshoots its resting place by a pixel and settles.  That is
   not an off-by-one in the tables, it is the animation.

   The last two steps of lower_right_x are inside the screen by more than the
   panel is wide, which is what the clamp below is for. */
void fdps_draw_status_window_anim_frame(void *background, void *window_image,
                                        int step)
{
    /* Declared in this order because it is the order the original's frame is
       laid out in -- [EBP-4] is the off-screen buffer, [EBP-8] the upper-left
       panel's destination column and the four arrays run down from [EBP-0x30]
       -- and wcc386 hands out the slots in declaration order.  Nothing
       depends on it; the arrangement is codegen, not behaviour.

       The original holds all three panel extents below in the single slot at
       [EBP-0xc], one after another, so its frame is eight bytes smaller than
       this one's.  They are three different measurements of three different
       panels and are named as such here (ADR-0001: the standard is observable
       behaviour, and a stack slot is not). */
    unsigned char *frame;
    int upper_left_dest_x;
    int upper_left_width;
    int lower_right_width;
    int upper_right_height;
    int upper_left_x[ANIM_STEP_COUNT] =
        { -120, -100, -70, -40, -15, 15, 14, 14, 15 };
    int lower_left_y[ANIM_STEP_COUNT] =
        { 190, 175, 160, 140, 125, 108, 109, 109, 108 };
    int lower_right_x[ANIM_STEP_COUNT] =
        { 300, 270, 240, 215, 175, 148, 149, 149, 148 };
    int upper_right_y[ANIM_STEP_COUNT] =
        { -36, -32, -24, -16, -8, 0, -1, -1, 0 };

    /* One whole frame's worth of off-screen buffer, allocated and freed per
       call.  The original does not check the result and neither does this:
       PUSH 0xfa00 / CALL malloc / MOV [EBP-4],EAX at 00016ecb goes straight
       into the memmove that follows. */
    frame = (unsigned char *) malloc(VGA_SCREEN_BYTES);
    memmove(frame, background, (size_t) VGA_SCREEN_BYTES);

    /* Upper-left panel, sliding in from the left edge.  While its x is
       negative only its rightmost LEFT_COLUMN_WIDTH + x columns are on
       screen, and they go at column 0; once x reaches zero the whole panel is
       drawn at column x.  Either way the source column is
       RIGHT_COLUMN_X - width, which is LEFT_COLUMN_X shifted right by however
       much of the panel is still off the edge. */
    if (upper_left_x[step] < 0) {
        upper_left_width = upper_left_x[step] + LEFT_COLUMN_WIDTH;
        upper_left_dest_x = 0;
    } else {
        upper_left_width = LEFT_COLUMN_WIDTH;
        upper_left_dest_x = upper_left_x[step];
    }
    fdps_blit_rect((unsigned int) window_image
                       + (RIGHT_COLUMN_X - upper_left_width),
                   VGA_SCREEN_PITCH,
                   frame + upper_left_dest_x,
                   VGA_SCREEN_PITCH,
                   upper_left_width, UPPER_LEFT_HEIGHT);

    /* Lower-left panel, sliding up from below the screen.  Its top row is the
       table entry and its height is simply what is left above the bottom
       scanline, so the part still below the screen is never blitted rather
       than being blitted and clipped. */
    fdps_blit_rect((unsigned int) window_image
                       + (LOWER_LEFT_Y * VGA_SCREEN_PITCH + LEFT_COLUMN_X),
                   VGA_SCREEN_PITCH,
                   frame + lower_left_y[step] * VGA_SCREEN_PITCH
                       + LEFT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   LEFT_COLUMN_WIDTH, VGA_SCREEN_ROWS - lower_left_y[step]);

    /* Lower-right panel, sliding in from the right edge.  Its width is the
       room left between its x and the right edge of the screen, and it stops
       growing once the whole panel fits: CMP dword ptr [EBP-0xc],0x9e / JLE
       at 00016fc8, the signed test.  The clamp is load bearing and not
       defensive -- the last four steps sit at x 148 or 149, which leave 172
       and 171 columns of room, and without it those frames would blit past
       the panel's own right edge into whatever the window image holds
       there. */
    lower_right_width = VGA_SCREEN_PITCH - lower_right_x[step];
    if (lower_right_width > RIGHT_COLUMN_WIDTH) {
        lower_right_width = RIGHT_COLUMN_WIDTH;
    }
    fdps_blit_rect((unsigned int) window_image
                       + (LOWER_RIGHT_Y * VGA_SCREEN_PITCH + RIGHT_COLUMN_X),
                   VGA_SCREEN_PITCH,
                   frame + LOWER_RIGHT_Y * VGA_SCREEN_PITCH
                       + lower_right_x[step],
                   VGA_SCREEN_PITCH,
                   lower_right_width, LOWER_RIGHT_HEIGHT);

    /* Upper-right panel, sliding down from above the screen.  Its y is zero
       or negative, so the height on screen is UPPER_RIGHT_HEIGHT + y and the
       source starts that far down the panel; the destination is always row 0.
       No clamp is needed on this one because y never goes positive. */
    upper_right_height = upper_right_y[step] + UPPER_RIGHT_HEIGHT;
    fdps_blit_rect((unsigned int) window_image
                       + (UPPER_RIGHT_HEIGHT - upper_right_height)
                           * VGA_SCREEN_PITCH
                       + RIGHT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   frame + RIGHT_COLUMN_X,
                   VGA_SCREEN_PITCH,
                   RIGHT_COLUMN_WIDTH, upper_right_height);

    /* The frame's pacing, in the order the original has it: hold for fifteen
       milliseconds, then wait for a vertical retrace to start and then for it
       to finish, and only then put the buffer up.  Presenting on the far side
       of the retrace rather than at its start is what keeps the copy off the
       displayed scanlines; the delay on top of it is what makes the window
       take about a third of a second to open rather than nine frames. */
    delay((unsigned int) ANIM_FRAME_DELAY_MS);
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
    }
    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
    }

    memmove((void *) VGA_SCREEN_BASE, frame, (size_t) VGA_SCREEN_BYTES);
    free(frame);
}
