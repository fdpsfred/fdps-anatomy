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
#include <stdlib.h>
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

/* fdps_transition_slide, 0002f6d0.
 *
 * WHAT IS OBSERVABLE FROM OUTSIDE THE CALL.  Seven of the eight styles write
 * only inside the width x height destination rectangle, and the blit at
 * 0002fb38 -- outside the switch, unconditional, last -- rewrites every byte
 * of that rectangle with the incoming picture.  So for those seven the whole
 * unit-observable surface is the destination's final content, the fact that
 * the source is untouched, and the fact that the call ends.  The two scratch
 * buffers are malloc'd and freed inside the call and never handed out, and
 * every frame in between is painted over.
 *
 * STYLE 6 IS THE EXCEPTION AND THAT IS WHY THE GEOMETRY HERE IS TALLER THAN
 * IT IS WIDE.  Its loop counts columns to width but it anchors each frame at
 * dst + (height - position): 0002fa76 is ADD EAX,[EBP+0x28], the height, then
 * SUB EAX,[EBP-0x10].  With a rectangle 6 wide and 10 tall the last frame
 * lands at dst + 6 and is 4 columns wide, so it writes columns 6 to 9 -- past
 * the right-hand edge, where the closing blit never reaches and the check can
 * see it.  Anchoring on width instead would put every frame inside the
 * rectangle and leave nothing behind at all, so the two spellings are told
 * apart here rather than left to the playtest.
 *
 * WHAT IS LEFT TO THE PLAYTEST.  Which direction each style slides, how many
 * frames it takes and what the screen holds part-way through are visible only
 * while the animation is running: for styles 0-5 and 7 a build that slid the
 * wrong way would still leave the destination holding exactly the bytes
 * checked below.  delay() is in the same position and every case passes 0.
 *
 * Expected values come from the assembly: the six pushes of the closing blit
 * at 0002fb38 (height, width, dst_pitch, dst, src_pitch, src), the CMP
 * against 7 with JA at 0002f746 that makes the style range check unsigned,
 * the JL loop tests at 0002f76a and 0002f964 against height and width
 * respectively, and the eight-entry jump table at 0002f6e0.
 */

/* Deliberately taller than it is wide, with two strides that are both larger
   than the width and different from each other, so neither stride can stand
   in for the width or for the other, and a height/width mix-up is visible. */
#define SLIDE_W 6
#define SLIDE_H 10
#define SLIDE_SP 9
#define SLIDE_DP 16

/* One guard row above the rectangle and one below it in each buffer. */
#define SLIDE_SRC_BYTES (SLIDE_SP * (SLIDE_H + 2))
#define SLIDE_DST_BYTES (SLIDE_DP * (SLIDE_H + 2))

/* The source is a ramp over its whole buffer: slide_src[i] == 0x40 + i.  The
   buffer is 108 bytes, so every byte in it is distinct and a transfer that
   used the wrong stride or started at the wrong offset lands on a value that
   names where it came from. */
#define SLIDE_RAMP_BASE 0x40

/* Two fills for what the destination is carrying on entry.  Neither can be
   produced by the ramp, which runs from 0x40 to 0xab. */
#define SLIDE_OUT_A 0x11
#define SLIDE_OUT_B 0x22

/* Both rectangles start one whole row into their buffer. */
#define SLIDE_SRC_ORIGIN SLIDE_SP
#define SLIDE_DST_ORIGIN SLIDE_DP

/* The columns style 6 leaves behind with this geometry: the last frame runs
   at position 4, anchors at dst + (10 - 4) and is 4 columns wide. */
#define SLIDE_SPILL_LO 6
#define SLIDE_SPILL_HI 9

static unsigned char slide_src[SLIDE_SRC_BYTES];
static unsigned char slide_dstbuf[SLIDE_DST_BYTES];

static void slide_stage(int outgoing)
{
    int i;

    for (i = 0; i < SLIDE_SRC_BYTES; i++) {
        slide_src[i] = (unsigned char) (SLIDE_RAMP_BASE + i);
    }
    memset(slide_dstbuf, outgoing, (size_t) SLIDE_DST_BYTES);
}

static void run_slide(int step, int style)
{
    fdps_transition_slide(&slide_src[SLIDE_SRC_ORIGIN], SLIDE_SP,
                          &slide_dstbuf[SLIDE_DST_ORIGIN], SLIDE_DP,
                          SLIDE_W, SLIDE_H, step, 0, style);
}

/* The source byte that belongs at (row, col), spelled out as ramp arithmetic
   rather than read back through the same pointer arithmetic the code under
   test uses. */
static int slide_expected(int row, int col)
{
    return (SLIDE_RAMP_BASE + SLIDE_SRC_ORIGIN + row * SLIDE_SP + col) & 0xff;
}

static int slide_dst(int row, int col)
{
    return (int) slide_dstbuf[SLIDE_DST_ORIGIN + row * SLIDE_DP + col];
}

/* Bytes of the destination rectangle that are not the incoming picture. */
static int slide_rect_bad(void)
{
    int row;
    int col;
    int bad;

    bad = 0;
    for (row = 0; row < SLIDE_H; row++) {
        for (col = 0; col < SLIDE_W; col++) {
            if (slide_dst(row, col) != slide_expected(row, col)) {
                bad++;
            }
        }
    }
    return bad;
}

/* Bytes of the destination buffer outside the rectangle -- the padding
   columns on every row and the two guard rows -- that no longer hold the
   fill the destination was carrying.  Columns skip_lo to skip_hi of the
   rectangle's own rows are left out of the count, which is how style 6's
   spill is excluded; pass a range that is empty to count everything. */
static int slide_outside_bad(int outgoing, int skip_lo, int skip_hi)
{
    int i;
    int row;
    int col;
    int bad;

    bad = 0;
    for (i = 0; i < SLIDE_DST_BYTES; i++) {
        row = (i / SLIDE_DP) - 1;
        col = i % SLIDE_DP;
        if (row >= 0 && row < SLIDE_H) {
            if (col < SLIDE_W) {
                continue;
            }
            if (col >= skip_lo && col <= skip_hi) {
                continue;
            }
        }
        if ((int) slide_dstbuf[i] != outgoing) {
            bad++;
        }
    }
    return bad;
}

/* Bytes of the source that are no longer their ramp value.  The routine reads
   src on every path and writes it on none. */
static int slide_src_bad(void)
{
    int i;
    int bad;

    bad = 0;
    for (i = 0; i < SLIDE_SRC_BYTES; i++) {
        if ((int) slide_src[i] != ((SLIDE_RAMP_BASE + i) & 0xff)) {
            bad++;
        }
    }
    return bad;
}

/* Bytes of style 6's spill that are not the source column the height anchor
   selects: the last frame copies source columns 0 to 3 of every row to
   destination columns 6 to 9 of the same row. */
static int slide_spill_bad(void)
{
    int row;
    int col;
    int bad;

    bad = 0;
    for (row = 0; row < SLIDE_H; row++) {
        for (col = SLIDE_SPILL_LO; col <= SLIDE_SPILL_HI; col++) {
            if (slide_dst(row, col)
                != slide_expected(row, col - SLIDE_SPILL_LO)) {
                bad++;
            }
        }
    }
    return bad;
}

/* Whatever the style, the destination holds the whole incoming picture when
   the call returns, read at src_pitch and written at dst_pitch, and the
   source is unchanged.  What puts it there is the blit at 0002fb38, which is
   outside the switch: no branch can be responsible for this. */
static void slide_every_style_lands_the_new_picture(void)
{
    int style;

    for (style = 0; style <= 7; style++) {
        slide_stage(SLIDE_OUT_A);
        run_slide(2, style);
        CHECK_EQ(slide_rect_bad(), 0);
        CHECK_EQ(slide_src_bad(), 0);

        /* A second fill, so nothing of what the destination was carrying can
           be passing for a source byte. */
        slide_stage(SLIDE_OUT_B);
        run_slide(2, style);
        CHECK_EQ(slide_rect_bad(), 0);
    }
}

/* Seven of the eight styles keep every write inside the rectangle: the four
   odd ones compose in a packed scratch buffer and present the whole of it at
   dst_pitch, and styles 0, 2 and 4 anchor their partial rectangle inside it.
   A wrong stride or a wrong anchor in any of them lands in the padding
   columns or the guard rows, where the closing blit never reaches. */
static void slide_seven_styles_stay_inside_the_rectangle(void)
{
    static int inside_styles[7] = {0, 1, 2, 3, 4, 5, 7};
    int i;

    for (i = 0; i < 7; i++) {
        slide_stage(SLIDE_OUT_A);
        run_slide(2, inside_styles[i]);
        CHECK_EQ(slide_outside_bad(SLIDE_OUT_A, 1, 0), 0);
    }
}

/* Style 6 anchors on dst + (height - position) while its loop counts columns
   to width, so on this 6 x 10 rectangle its last frame writes columns 6 to 9
   -- outside the rectangle, where nothing overwrites it.  The spill is
   exactly source columns 0 to 3 of each row.  Anchoring on width instead
   would put every frame inside the rectangle and leave the padding columns
   untouched, so slide_spill_bad would count all forty. */
static void slide_style_six_anchors_on_the_height(void)
{
    slide_stage(SLIDE_OUT_A);
    run_slide(2, 6);

    CHECK_EQ(slide_rect_bad(), 0);
    CHECK_EQ(slide_spill_bad(), 0);
    CHECK_EQ(slide_dst(0, SLIDE_SPILL_LO), SLIDE_RAMP_BASE + SLIDE_SP);
    CHECK_EQ(slide_dst(0, SLIDE_SPILL_HI), SLIDE_RAMP_BASE + SLIDE_SP + 3);
    CHECK_EQ(slide_dst(SLIDE_H - 1, SLIDE_SPILL_LO),
             SLIDE_RAMP_BASE + SLIDE_SP * SLIDE_H);
    CHECK_EQ(slide_dst(SLIDE_H - 1, SLIDE_SPILL_HI),
             SLIDE_RAMP_BASE + SLIDE_SP * SLIDE_H + 3);

    /* And nothing outside the rectangle other than that spill was touched. */
    CHECK_EQ(slide_outside_bad(SLIDE_OUT_A, SLIDE_SPILL_LO, SLIDE_SPILL_HI),
             0);
}

/* The style is range-checked with CMP 7 / JA, an unsigned compare, so 8 and
   -1 both miss the jump table.  Neither animates anything, and both still
   complete the transition, because the blit that completes it is outside the
   switch. */
static void slide_a_style_outside_the_table_still_lands(void)
{
    slide_stage(SLIDE_OUT_A);
    run_slide(2, 8);
    CHECK_EQ(slide_rect_bad(), 0);
    CHECK_EQ(slide_outside_bad(SLIDE_OUT_A, 1, 0), 0);

    slide_stage(SLIDE_OUT_A);
    run_slide(2, -1);
    CHECK_EQ(slide_rect_bad(), 0);
    CHECK_EQ(slide_outside_bad(SLIDE_OUT_A, 1, 0), 0);
}

/* The loop test is at the top and is strict, and styles 0-3 test against the
   height while styles 4-7 test against the width.  A step that already fills
   its extent draws no frame at all and the transition still completes.

   The style 6 run is the one that pins which extent that branch bounds: a
   step of 6 is not below the width, so no frame runs and nothing spills.  Had
   the branch bounded itself by the height instead, 6 would be below 10, one
   frame would run at dst + (10 - 6) and four columns of spill would be
   sitting outside the rectangle. */
static void slide_a_step_that_fills_the_extent_draws_no_frame(void)
{
    slide_stage(SLIDE_OUT_A);
    run_slide(SLIDE_H, 0);
    CHECK_EQ(slide_rect_bad(), 0);
    CHECK_EQ(slide_outside_bad(SLIDE_OUT_A, 1, 0), 0);

    slide_stage(SLIDE_OUT_A);
    run_slide(SLIDE_H + 2, 1);
    CHECK_EQ(slide_rect_bad(), 0);
    CHECK_EQ(slide_outside_bad(SLIDE_OUT_A, 1, 0), 0);

    slide_stage(SLIDE_OUT_A);
    run_slide(SLIDE_W, 4);
    CHECK_EQ(slide_rect_bad(), 0);
    CHECK_EQ(slide_outside_bad(SLIDE_OUT_A, 1, 0), 0);

    slide_stage(SLIDE_OUT_A);
    run_slide(SLIDE_W, 6);
    CHECK_EQ(slide_rect_bad(), 0);
    CHECK_EQ(slide_outside_bad(SLIDE_OUT_A, 1, 0), 0);
}

/* fdps_transition_random_blocks, 0002fb80.
 *
 * WHAT IS OBSERVABLE FROM OUTSIDE THE CALL.  There is no closing blit here:
 * the destination ends up holding exactly the blocks the routine drew and
 * nothing else, so unlike its two neighbours above every one of its bounds,
 * band counts and offsets is visible in the final content of the destination
 * buffer.  The cell table is malloc'd and freed inside the call and never
 * handed out, and the order the blocks are drawn in is the one thing that is
 * not observable, because the shuffle is a permutation and every cell is drawn
 * exactly once whatever order it lands in.
 *
 * THAT IS WHY THE SEEDS ARE VARIED RATHER THAN PINNED.  rand() is the CRT's
 * and its sequence is not this rebuild's to define; what the cases below rely
 * on is the property the assembly guarantees for any sequence -- both draws
 * are taken modulo an extent and recombined into grid_cols * row + col, which
 * cannot exceed cell_count - 1, so the swap at 0002fc7b never leaves the table
 * and never loses a cell.  A shuffle that could drop one would leave that
 * cell's blocks carrying the outgoing fill, and the coverage check would count
 * them.
 *
 * Expected values come from the assembly: the six pushes at 0002fdc5 onward,
 * which put [EBP+0x14] and [EBP+0x18] in fdps_blit_rect's source pair and
 * [EBP+0x1c] and [EBP+0x20] in its destination pair; the two IDIV at 0002fcb3
 * and 0002fcc2 with the TEST EDX,EDX / INC pairs at 0002fcdd and 0002fcf3 that
 * round both band counts up; the IMUL EAX,[EBP+0x2c] at 0002fd5f that scales
 * the vertical band index by the COLUMN count; the CMP EAX,[EBP+0x24] at
 * 0002fd9d that bounds a block index by a pixel width; and the CMP
 * [EBP+0x18],0x0 at 0002fdac that drops the block offset in fill mode.
 *
 * WHAT IS LEFT TO THE PLAYTEST.  In what order the patches appear, and the
 * pacing delay() gives them.  Every case passes 0 for frame_delay.
 */

/* Chosen so that neither axis divides evenly into whole bands, which is what
   makes both roundings and both bounds visible:

     cols_total = 14 / 2 = 7, over 2 grid columns -> 4 bands, so the block
       column index runs 0..7 and the last band draws at pixel columns 14 and
       15 -- past the width of 14, which the index-against-width test lets
       through;
     rows_total = 9 / 3 = 3, over 2 grid rows -> 2 bands, so the vertical band
       index runs 0..3 and the pixel rows are 0, 3, 6 and 9 -- and 9 is
       dropped by the row-against-height test.

   The strides are both larger than the width and different from each other, so
   neither can stand in for the width or for the other. */
#define MOS_W 14
#define MOS_H 9
#define MOS_SP 18
#define MOS_DP 20
#define MOS_BW 2
#define MOS_BH 3
#define MOS_GC 2
#define MOS_GR 2

/* Pixel columns 0..15 and pixel rows 0..8 are what the blocks cover: two
   columns more than the width, and exactly the height. */
#define MOS_COVER_W 16
#define MOS_COVER_H 9

/* Eight rows of slack past the rectangle in both buffers.  A build that lost
   either bound would draw the dropped band at pixel row 9 and the two rows
   under it, and the slack is what keeps that inside the buffer where the guard
   check can see it instead of past the end of it. */
#define MOS_SRC_BYTES (MOS_SP * (MOS_H + 8))
#define MOS_DST_BYTES (MOS_DP * (MOS_H + 8))

/* The source is a ramp: mos_src[i] == 0x20 + i.  The rectangle and everything
   a mis-bounded build could reach lie in the first 200 bytes, where the ramp
   has not wrapped, so every byte a block could pick up names exactly where it
   came from. */
#define MOS_RAMP 0x20

/* What the destination is carrying on entry.  Neither value appears in the
   part of the ramp any block can reach. */
#define MOS_OUT_A 0xf1
#define MOS_OUT_B 0xf2

/* The palette index the fill-mode case hands over in place of an address. */
#define MOS_FILL 0x5a

/* Both rectangles start one whole row into their buffer. */
#define MOS_SRC_ORIGIN MOS_SP
#define MOS_DST_ORIGIN MOS_DP

static unsigned char mos_src[MOS_SRC_BYTES];
static unsigned char mos_dstbuf[MOS_DST_BYTES];

static void mos_stage(int outgoing)
{
    int i;

    for (i = 0; i < MOS_SRC_BYTES; i++) {
        mos_src[i] = (unsigned char) (MOS_RAMP + i);
    }
    memset(mos_dstbuf, outgoing, (size_t) MOS_DST_BYTES);
}

static void run_mosaic(unsigned int src_or_fill, int src_pitch)
{
    fdps_transition_random_blocks(src_or_fill, src_pitch,
                                  &mos_dstbuf[MOS_DST_ORIGIN], MOS_DP,
                                  MOS_W, MOS_H, MOS_GC, MOS_GR,
                                  MOS_BW, MOS_BH, 0);
}

static void run_mosaic_copy(void)
{
    run_mosaic((unsigned int) &mos_src[MOS_SRC_ORIGIN], MOS_SP);
}

/* The source byte that belongs at (row, col) of the destination, spelled out
   as ramp arithmetic rather than read back through the same pointer arithmetic
   the code under test uses. */
static int mos_expected(int row, int col)
{
    return (MOS_RAMP + MOS_SRC_ORIGIN + row * MOS_SP + col) & 0xff;
}

static int mos_dst(int row, int col)
{
    return (int) mos_dstbuf[MOS_DST_ORIGIN + row * MOS_DP + col];
}

/* Bytes of the covered area -- pixel rows 0..8, pixel columns 0..15 -- that
   are not the source byte the blocks should have put there. */
static int mos_covered_bad(void)
{
    int row;
    int col;
    int bad;

    bad = 0;
    for (row = 0; row < MOS_COVER_H; row++) {
        for (col = 0; col < MOS_COVER_W; col++) {
            if (mos_dst(row, col) != mos_expected(row, col)) {
                bad++;
            }
        }
    }
    return bad;
}

/* Bytes of the destination buffer outside the covered area that no longer hold
   the fill the destination was carrying: the columns from MOS_COVER_W to the
   pitch on the covered rows, and every byte of the guard row above and the
   seven rows below. */
static int mos_outside_bad(int outgoing)
{
    int i;
    int row;
    int col;
    int bad;

    bad = 0;
    for (i = 0; i < MOS_DST_BYTES; i++) {
        row = (i / MOS_DP) - 1;
        col = i % MOS_DP;
        if (row >= 0 && row < MOS_COVER_H && col < MOS_COVER_W) {
            continue;
        }
        if ((int) mos_dstbuf[i] != outgoing) {
            bad++;
        }
    }
    return bad;
}

static int mos_src_bad(void)
{
    int i;
    int bad;

    bad = 0;
    for (i = 0; i < MOS_SRC_BYTES; i++) {
        if ((int) mos_src[i] != ((MOS_RAMP + i) & 0xff)) {
            bad++;
        }
    }
    return bad;
}

/* Every block the two bounds admit is drawn, and drawn once: the covered area
   ends up holding the incoming picture read at src_pitch and written at
   dst_pitch, and nothing outside it is touched.  Six different rand() states
   give six different permutations of the four phase cells, and the end state
   is the same under all of them -- which is what the swap staying inside the
   table buys.  A shuffle that could lose a cell would leave four of the
   sixteen columns, or three of the nine rows, still carrying the fill. */
static void mosaic_covers_every_block_whatever_the_shuffle(void)
{
    int seed;

    for (seed = 1; seed <= 6; seed++) {
        srand((unsigned int) seed);
        mos_stage(MOS_OUT_A);
        run_mosaic_copy();
        CHECK_EQ(mos_covered_bad(), 0);
        CHECK_EQ(mos_outside_bad(MOS_OUT_A), 0);
    }

    /* And a second outgoing fill, so no byte of what the destination was
       carrying can be passing for a source byte. */
    srand(7);
    mos_stage(MOS_OUT_B);
    run_mosaic_copy();
    CHECK_EQ(mos_covered_bad(), 0);
    CHECK_EQ(mos_outside_bad(MOS_OUT_B), 0);
}

/* The source is read at src_pitch and the destination written at dst_pitch,
   and neither is the width.  The three expected values are written out as ramp
   arithmetic rather than through the helpers, so this case fails if the
   helpers and the code under test ever agree on the wrong stride. */
static void mosaic_the_two_strides_are_independent_of_the_width(void)
{
    srand(3);
    mos_stage(MOS_OUT_A);
    run_mosaic_copy();

    CHECK_EQ(mos_dst(0, 0), MOS_RAMP + MOS_SP);
    CHECK_EQ(mos_dst(1, 0), MOS_RAMP + MOS_SP * 2);
    CHECK_EQ(mos_dst(8, 13), MOS_RAMP + MOS_SP * 9 + 13);
    CHECK_EQ((int) mos_dstbuf[MOS_DST_ORIGIN + MOS_DP - 1], MOS_OUT_A);
    CHECK_EQ((int) mos_dstbuf[MOS_DST_ORIGIN + MOS_DP],
             MOS_RAMP + MOS_SP * 2);
}

/* The horizontal bound is the block column index against the pixel width, so
   the fourth band -- block columns 6 and 7, pixel columns 12 to 15 -- is drawn
   in full even though its right half is past the width of 14.  Those two
   columns are the whole difference between this test and a
   block_x * block_w < width one, which would leave them at the outgoing fill.
   The band count is what really bounds the axis, and it stops at column 15:
   columns 16 to 19 are untouched. */
static void mosaic_the_last_band_is_drawn_past_the_width(void)
{
    int row;

    srand(11);
    mos_stage(MOS_OUT_A);
    run_mosaic_copy();

    for (row = 0; row < MOS_COVER_H; row++) {
        CHECK_EQ(mos_dst(row, MOS_W), mos_expected(row, MOS_W));
        CHECK_EQ(mos_dst(row, MOS_W + 1), mos_expected(row, MOS_W + 1));
        CHECK_EQ(mos_dst(row, MOS_COVER_W), MOS_OUT_A);
        CHECK_EQ(mos_dst(row, MOS_DP - 1), MOS_OUT_A);
    }
}

/* The vertical bound is a pixel row against the pixel height, and the band
   count is rounded up, so the four vertical band indices give pixel rows 0, 3,
   6 and 9 and the last one is dropped.  Rows 6 to 8 being the incoming picture
   is the rounding: without the INC at 0002fcf3 the second band would not exist
   and they would still hold the fill.  Rows 9 to 11 being the fill is the
   bound: without the test at 0002fda5 the dropped band would be drawn there.
   The two are checked together because each one alone is ambiguous. */
static void mosaic_the_band_past_the_height_is_dropped(void)
{
    int col;
    int row;

    srand(5);
    mos_stage(MOS_OUT_A);
    run_mosaic_copy();

    for (col = 0; col < MOS_COVER_W; col++) {
        CHECK_EQ(mos_dst(6, col), mos_expected(6, col));
        CHECK_EQ(mos_dst(8, col), mos_expected(8, col));
    }
    for (row = MOS_H; row < MOS_H + 3; row++) {
        CHECK_EQ(mos_dst(row, 0), MOS_OUT_A);
        CHECK_EQ(mos_dst(row, MOS_COVER_W - 1), MOS_OUT_A);
    }
}

/* src_pitch 0 puts fdps_blit_rect in fill mode, and the first argument then
   has to arrive at every block exactly as it was handed in: the test at
   0002fdac drops the block's byte offset, and the row term is zero because it
   is multiplied by the stride.  So every covered byte is the one palette index
   MOS_FILL.  A build that added the block offset in fill mode would paint
   MOS_FILL + 0, + 2, + 4 ... across the bands, which is what the per-column
   checks below would catch; one that added the row term would step the colour
   down the rows. */
static void mosaic_a_zero_source_stride_fills_one_colour(void)
{
    int row;
    int col;
    int bad;

    srand(13);
    mos_stage(MOS_OUT_A);
    run_mosaic(MOS_FILL, 0);

    bad = 0;
    for (row = 0; row < MOS_COVER_H; row++) {
        for (col = 0; col < MOS_COVER_W; col++) {
            if (mos_dst(row, col) != MOS_FILL) {
                bad++;
            }
        }
    }

    CHECK_EQ(bad, 0);
    CHECK_EQ(mos_dst(0, 0), MOS_FILL);
    CHECK_EQ(mos_dst(0, MOS_COVER_W - 1), MOS_FILL);
    CHECK_EQ(mos_dst(MOS_COVER_H - 1, 0), MOS_FILL);
    CHECK_EQ(mos_dst(MOS_COVER_H - 1, MOS_COVER_W - 1), MOS_FILL);
    CHECK_EQ(mos_outside_bad(MOS_OUT_A), 0);

    /* Fill mode never dereferences the first argument, so the ramp buffer is
       not even read; it is certainly not written. */
    CHECK_EQ(mos_src_bad(), 0);
}

/* The incoming picture is a source on the only blit this routine makes. */
static void mosaic_the_source_is_never_written(void)
{
    srand(17);
    mos_stage(MOS_OUT_A);
    run_mosaic_copy();
    CHECK_EQ(mos_src_bad(), 0);
}

/* A grid that is not square, which is the only geometry where the vertical
   band index scaling by grid_cols can be told from scaling by grid_rows.
   3 columns by 2 rows, 2x2 blocks, on a 12 x 10 rectangle:

     cols_total = 12 / 2 = 6, over 3 grid columns -> 2 bands, block columns
       0..5, pixel columns 0..11 -- the full width;
     rows_total = 10 / 2 = 5, over 2 grid rows -> 3 bands, and the vertical
       index is band * 3 + cell_row with cell_row in {0, 1}, so it takes the
       values 0, 1, 3, 4, 6 and 7 and never 2 or 5.  Pixel rows 0, 2, 6, 8, 12
       and 14; the last two are dropped by the height, and pixel rows 4 and 5
       are never covered by any cell at all.

   Scaling by grid_rows instead would give indices 0..5 and cover every row.
   So the two rows that keep the outgoing fill are the whole assertion. */
#define QW 12
#define QH 10
#define QSP 16
#define QDP 18
#define QBW 2
#define QBH 2
#define QGC 3
#define QGR 2
#define QSRC_BYTES (QSP * (QH + 8))
#define QDST_BYTES (QDP * (QH + 8))
#define QSRC_ORIGIN QSP
#define QDST_ORIGIN QDP

/* The two pixel rows the vertical index skips over. */
#define QSKIP_LO 4
#define QSKIP_HI 5

static unsigned char q_src[QSRC_BYTES];
static unsigned char q_dstbuf[QDST_BYTES];

static int q_expected(int row, int col)
{
    return (MOS_RAMP + QSRC_ORIGIN + row * QSP + col) & 0xff;
}

static int q_dst(int row, int col)
{
    return (int) q_dstbuf[QDST_ORIGIN + row * QDP + col];
}

static void mosaic_a_non_square_grid_skips_whole_rows(void)
{
    int i;
    int row;
    int col;
    int drawn_bad;
    int skipped_bad;
    int outside_bad;

    for (i = 0; i < QSRC_BYTES; i++) {
        q_src[i] = (unsigned char) (MOS_RAMP + i);
    }
    memset(q_dstbuf, MOS_OUT_A, (size_t) QDST_BYTES);

    srand(23);
    fdps_transition_random_blocks((unsigned int) &q_src[QSRC_ORIGIN], QSP,
                                  &q_dstbuf[QDST_ORIGIN], QDP,
                                  QW, QH, QGC, QGR, QBW, QBH, 0);

    drawn_bad = 0;
    skipped_bad = 0;
    for (row = 0; row < QH; row++) {
        for (col = 0; col < QW; col++) {
            if (row >= QSKIP_LO && row <= QSKIP_HI) {
                if (q_dst(row, col) != MOS_OUT_A) {
                    skipped_bad++;
                }
            } else if (q_dst(row, col) != q_expected(row, col)) {
                drawn_bad++;
            }
        }
    }

    outside_bad = 0;
    for (i = 0; i < QDST_BYTES; i++) {
        row = (i / QDP) - 1;
        col = i % QDP;
        if (row >= 0 && row < QH && col < QW) {
            continue;
        }
        if ((int) q_dstbuf[i] != MOS_OUT_A) {
            outside_bad++;
        }
    }

    CHECK_EQ(drawn_bad, 0);
    CHECK_EQ(skipped_bad, 0);
    CHECK_EQ(outside_bad, 0);
    CHECK_EQ(q_dst(3, 0), q_expected(3, 0));
    CHECK_EQ(q_dst(4, 0), MOS_OUT_A);
    CHECK_EQ(q_dst(5, QW - 1), MOS_OUT_A);
    CHECK_EQ(q_dst(6, 0), q_expected(6, 0));
    CHECK_EQ(q_dst(QH - 1, QW - 1), q_expected(QH - 1, QW - 1));
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
    RUN_TEST(slide_every_style_lands_the_new_picture);
    RUN_TEST(slide_seven_styles_stay_inside_the_rectangle);
    RUN_TEST(slide_style_six_anchors_on_the_height);
    RUN_TEST(slide_a_style_outside_the_table_still_lands);
    RUN_TEST(slide_a_step_that_fills_the_extent_draws_no_frame);
    RUN_TEST(mosaic_covers_every_block_whatever_the_shuffle);
    RUN_TEST(mosaic_the_two_strides_are_independent_of_the_width);
    RUN_TEST(mosaic_the_last_band_is_drawn_past_the_width);
    RUN_TEST(mosaic_the_band_past_the_height_is_dropped);
    RUN_TEST(mosaic_a_zero_source_stride_fills_one_colour);
    RUN_TEST(mosaic_the_source_is_never_written);
    RUN_TEST(mosaic_a_non_square_grid_skips_whole_rows);
}
