/* tests/transit.c -- cover for src/transit.c.
 *
 * Expected values come from the assembly at 0002f410: the closing
 * fdps_blit_rect at 0002f688, whose six pushes are height, width, dst_pitch,
 * dst, src_pitch and src in that order, so it moves a width x height
 * rectangle from src at src_pitch onto dst at dst_pitch; the JG / JLE pair at
 * 0002f48b that makes the closing path's loop test signed and stops it the
 * moment twice a step no longer fits in the surviving extent; the four IDIV
 * at 0002f53d onward with their SAR EDX,0x1f, and the DEC at 0002f567 and
 * 0002f594 that fire only when the matching remainder is zero; and the CMP
 * against 0 with JZ at 0002f5db that ends the opening path.  None of them is
 * read off the emitted C.
 *
 * WHAT IS OBSERVABLE FROM OUTSIDE THE CALL, AND WHAT IS NOT.  Every frame of
 * the animation presents a width x height rectangle to dst at dst_pitch, and
 * so does the closing blit at 0002f688 -- the same extents, the same stride,
 * the same destination.  The closing blit runs last and unconditionally, so
 * it overwrites every byte any frame wrote and no frame leaves a trace behind
 * it.  The two scratch buffers are malloc'd and freed inside the call and are
 * never handed out.  So the whole unit-observable surface of this routine is
 * the destination's final content, plus the fact that it terminates.
 *
 * That is what the cases below pin, and they pin it hard: independent source
 * and destination strides that differ from the width, a guard byte outside
 * the rectangle in both directions, and geometries chosen so the closing path
 * runs three frames, two frames or none and the opening path two, one or
 * none.  A routine that dropped the closing blit, put it inside a branch,
 * used the wrong stride on either side, or ran its loop off the end of a
 * scratch buffer would fail here.
 *
 * WHAT IS LEFT TO THE PLAYTEST.  How many frames run, how large the
 * rectangle is on each of them, and where its corner sits -- including the
 * "one step short" rule the opening path's two DEC implement -- are visible
 * only while the animation is on screen.  Both the frame count and the
 * geometry are contracts of the kind rebuild_info/pitfalls.md collects, not
 * unit-test ones: a build that ran one frame too many would still leave the
 * destination holding exactly the bytes checked here.  delay() is in the same
 * position, and every case passes 0.
 */
#include <string.h>
#include "testharn.h"
#include "transit.h"

/* The rectangle under test, and two strides that are both larger than it and
   different from each other, so neither one can stand in for the width and
   neither can stand in for the other. */
#define RECT_W 12
#define RECT_H 8
#define SRC_PITCH 20
#define DST_PITCH 24

/* One guard row above the rectangle and one below it in each buffer, so a
   transfer that started a row early or ran a row late has somewhere to land
   where it will be noticed. */
#define SRC_BYTES (SRC_PITCH * (RECT_H + 2))
#define DST_BYTES (DST_PITCH * (RECT_H + 2))

/* The source is a ramp over its whole buffer, padding columns and guard rows
   included: src_buf[i] == 0x40 + i.  SRC_BYTES is 200, so every byte in it is
   distinct, and a transfer that used the wrong stride, started at the wrong
   offset or picked up a padding column lands on a value that names exactly
   where it came from. */
#define SRC_RAMP_BASE 0x40

/* Two different values for what the destination is carrying when the call is
   made.  Neither can appear in the source: the ramp would have to reach index
   209 or 226 to produce one, and it stops at 199. */
#define OUTGOING_A 0x11
#define OUTGOING_B 0x22

static unsigned char src_buf[SRC_BYTES];
static unsigned char dst_buf[DST_BYTES];

/* The rectangle starts one whole row into each buffer. */
#define SRC_ORIGIN SRC_PITCH
#define DST_ORIGIN DST_PITCH

static void stage_buffers(int outgoing)
{
    int i;

    for (i = 0; i < SRC_BYTES; i++) {
        src_buf[i] = (unsigned char) (SRC_RAMP_BASE + i);
    }
    memset(dst_buf, outgoing, (size_t) DST_BYTES);
}

static void run_box(int step_x, int step_y, int style)
{
    fdps_transition_box(&src_buf[SRC_ORIGIN], SRC_PITCH,
                        &dst_buf[DST_ORIGIN], DST_PITCH,
                        RECT_W, RECT_H, step_x, step_y, 0, style);
}

/* What the closing blit must have put at (row, col): the source byte the
   src_pitch stride selects, spelled out from the ramp rather than read back
   through the same pointer arithmetic the code under test uses. */
static int expected_pixel(int row, int col)
{
    return (SRC_RAMP_BASE + SRC_ORIGIN + row * SRC_PITCH + col) & 0xff;
}

static int dst_pixel(int row, int col)
{
    return (int) dst_buf[DST_ORIGIN + row * DST_PITCH + col];
}

/* How many bytes of the destination rectangle are not the source byte that
   belongs there. */
static int rect_mismatches(void)
{
    int row;
    int col;
    int bad;

    bad = 0;
    for (row = 0; row < RECT_H; row++) {
        for (col = 0; col < RECT_W; col++) {
            if (dst_pixel(row, col) != expected_pixel(row, col)) {
                bad++;
            }
        }
    }
    return bad;
}

/* How many bytes of the destination buffer outside the rectangle no longer
   hold what the destination was carrying: the padding columns between RECT_W
   and DST_PITCH on every row, and the two guard rows. */
static int outside_mismatches(int outgoing)
{
    int i;
    int row;
    int col;
    int bad;

    bad = 0;
    for (i = 0; i < DST_BYTES; i++) {
        row = (i / DST_PITCH) - 1;
        col = i % DST_PITCH;
        if (row >= 0 && row < RECT_H && col < RECT_W) {
            continue;
        }
        if ((int) dst_buf[i] != outgoing) {
            bad++;
        }
    }
    return bad;
}

/* How many bytes of the source buffer are no longer their ramp value.  The
   routine reads src and never writes it. */
static int src_mismatches(void)
{
    int i;
    int bad;

    bad = 0;
    for (i = 0; i < SRC_BYTES; i++) {
        if ((int) src_buf[i] != ((SRC_RAMP_BASE + i) & 0xff)) {
            bad++;
        }
    }
    return bad;
}

/* step_x 2 and step_y 1 give the closing path three frames -- 12x8 shrinks to
   8x6 then 4x4 then 0x2, and the fourth test fails because twice step_x no
   longer fits -- and the destination still ends up holding the whole of the
   incoming picture, because what puts it there is the blit at 0002f688 and
   not the last frame. */
static void the_closing_path_ends_on_the_incoming_picture(void)
{
    stage_buffers(OUTGOING_A);
    run_box(2, 1, 0);

    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_A), 0);
    CHECK_EQ(dst_pixel(0, 0), expected_pixel(0, 0));
    CHECK_EQ(dst_pixel(0, RECT_W - 1), expected_pixel(0, RECT_W - 1));
    CHECK_EQ(dst_pixel(RECT_H - 1, 0), expected_pixel(RECT_H - 1, 0));
    CHECK_EQ(dst_pixel(RECT_H - 1, RECT_W - 1),
             expected_pixel(RECT_H - 1, RECT_W - 1));
}

/* The same geometry through the opening path, which composes its frames the
   other way round -- memmove of the snapshot at 0002f5e5, then the matching
   rectangle of src laid into it -- and reaches the same end state.  step_x 2
   over a width of 12 gives 3 whole steps and gives one back because the
   division is exact; step_y 1 over a height of 8 gives 4 and gives one back;
   the animation therefore runs the smaller of the two, two frames. */
static void the_opening_path_ends_on_the_incoming_picture(void)
{
    stage_buffers(OUTGOING_A);
    run_box(2, 1, 1);

    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_A), 0);
    CHECK_EQ(dst_pixel(0, 0), expected_pixel(0, 0));
    CHECK_EQ(dst_pixel(RECT_H - 1, RECT_W - 1),
             expected_pixel(RECT_H - 1, RECT_W - 1));
}

/* The source is read at src_pitch and the destination written at dst_pitch,
   and neither is the width.  Row 1 of the destination holds the source bytes
   20 further into the ramp, not 12, and row 1 of the destination starts 24
   bytes into the buffer, not 12 -- so the byte at destination offset RECT_W
   on row 0 is still the outgoing fill and not the first pixel of row 1.

   The three expected values are written out as ramp arithmetic rather than
   through the helpers, so this case fails if the helpers and the code under
   test ever agree on the wrong stride. */
static void the_two_strides_are_independent_of_the_width(void)
{
    stage_buffers(OUTGOING_A);
    run_box(2, 1, 0);

    CHECK_EQ(dst_pixel(0, 0), SRC_RAMP_BASE + SRC_PITCH);
    CHECK_EQ(dst_pixel(1, 0), SRC_RAMP_BASE + SRC_PITCH * 2);
    CHECK_EQ(dst_pixel(7, 11), SRC_RAMP_BASE + SRC_PITCH * 8 + 11);
    CHECK_EQ((int) dst_buf[DST_ORIGIN + RECT_W], OUTGOING_A);
    CHECK_EQ((int) dst_buf[DST_ORIGIN + DST_PITCH - 1], OUTGOING_A);
    CHECK_EQ((int) dst_buf[DST_ORIGIN + DST_PITCH], SRC_RAMP_BASE
                                                        + SRC_PITCH * 2);
}

/* Twice step_x is 16, wider than the rectangle, so the closing path's loop
   test fails on its very first evaluation and no frame is composed at all.
   The transition still completes, because the blit that completes it is
   outside the loop. */
static void the_closing_path_completes_with_no_frames_at_all(void)
{
    stage_buffers(OUTGOING_A);
    run_box(8, 8, 0);

    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_A), 0);
}

/* Both axes divide into exactly one step here -- 12 / (2*6) and 8 / (2*4) --
   so both DEC fire, the frame count is zero and the opening path's counter
   loop never runs.  The counter test at 0002f5db is CMP against 0 with JZ, so
   a count that had come out below zero would run for about two billion
   frames; this case ends, which is the observable that it did not. */
static void the_opening_path_completes_with_no_frames_at_all(void)
{
    stage_buffers(OUTGOING_A);
    run_box(6, 4, 1);

    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_A), 0);
}

/* Steps that are unequal between the axes, and the reverse of the ratio the
   first two cases use, so the per-axis arithmetic cannot be passing by
   symmetry.  step_x 1 with step_y 2 gives the closing path two frames -- the
   height runs out first -- and the opening path one, because the height
   allows two steps and gives one back. */
static void unequal_steps_end_on_the_incoming_picture(void)
{
    stage_buffers(OUTGOING_A);
    run_box(1, 2, 0);
    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_A), 0);

    stage_buffers(OUTGOING_A);
    run_box(1, 2, 1);
    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_A), 0);

    /* A single step in each direction: the closing path shrinks 12x8 by two
       in each axis per frame until the height is gone, and the opening path
       gives up one of both axes' steps. */
    stage_buffers(OUTGOING_A);
    run_box(1, 1, 0);
    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_A), 0);

    stage_buffers(OUTGOING_A);
    run_box(1, 1, 1);
    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_A), 0);
}

/* The picture the destination was carrying is snapshotted and animated over,
   but none of it survives: the end state is the same whatever was there.  The
   two runs use fills that the source ramp cannot produce, so a byte of either
   one left inside the rectangle would be counted. */
static void what_the_destination_carried_does_not_survive(void)
{
    unsigned char first_run[RECT_W * RECT_H];
    int row;
    int col;
    int bad;

    stage_buffers(OUTGOING_A);
    run_box(2, 1, 0);
    for (row = 0; row < RECT_H; row++) {
        for (col = 0; col < RECT_W; col++) {
            first_run[row * RECT_W + col] = (unsigned char) dst_pixel(row, col);
        }
    }

    stage_buffers(OUTGOING_B);
    run_box(2, 1, 0);

    bad = 0;
    for (row = 0; row < RECT_H; row++) {
        for (col = 0; col < RECT_W; col++) {
            if ((int) first_run[row * RECT_W + col] != dst_pixel(row, col)) {
                bad++;
            }
        }
    }

    CHECK_EQ(bad, 0);
    CHECK_EQ(rect_mismatches(), 0);
    CHECK_EQ(outside_mismatches(OUTGOING_B), 0);
}

/* Nothing writes to the incoming picture: it is a source on all four of the
   fdps_blit_rect calls that name it, and the two scratch buffers the frames
   are composed in are the routine's own. */
static void the_incoming_picture_is_never_written(void)
{
    stage_buffers(OUTGOING_A);
    run_box(2, 1, 0);
    CHECK_EQ(src_mismatches(), 0);

    stage_buffers(OUTGOING_A);
    run_box(2, 1, 1);
    CHECK_EQ(src_mismatches(), 0);
}

void run_transit_tests(void)
{
    RUN_TEST(the_closing_path_ends_on_the_incoming_picture);
    RUN_TEST(the_opening_path_ends_on_the_incoming_picture);
    RUN_TEST(the_two_strides_are_independent_of_the_width);
    RUN_TEST(the_closing_path_completes_with_no_frames_at_all);
    RUN_TEST(the_opening_path_completes_with_no_frames_at_all);
    RUN_TEST(unequal_steps_end_on_the_incoming_picture);
    RUN_TEST(what_the_destination_carried_does_not_survive);
    RUN_TEST(the_incoming_picture_is_never_written);
}
