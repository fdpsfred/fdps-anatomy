/* transit.c -- full-screen picture transitions.
 *
 * See transit.h for the surface convention and for what a caller has to
 * guarantee.  Nothing in this file holds state: every routine works entirely
 * on the two surfaces it is handed and on scratch buffers it allocates and
 * frees within the one call.
 *
 * delay comes from <i86.h>, which is where Watcom 10.0a declares it, and it
 * is a real library call in the original too: CALL 0003d370.  memmove comes
 * from <string.h> and is likewise a call, CALL 0003d514, not an inline
 * expansion -- the flag that would inline it, -oi, is not in this build's set
 * (rebuild_info/build_flags.md).
 */
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include "blit.h"
#include "transit.h"

/* 0002f410.  The frame is composed in one scratch buffer and the outgoing
   picture is preserved in a second, both malloc'd at width * height and both
   addressed with width as their stride (0002f41c and 0002f42f are the same
   IMUL of the two extents, and every blit that touches either buffer pushes
   [EBP+0x24], the width, as its stride).

   Neither malloc result is tested: 0002f42c and 0002f43f store EAX straight
   into the frame and nothing looks at it before it is used as a destination.

   The two paths differ in what they compose each frame.  The closing path
   rebuilds the frame from the incoming picture and lays the surviving
   rectangle of the snapshot over it; the opening path starts from a copy of
   the snapshot and lays the matching rectangle of the incoming picture into
   it.  That asymmetry is in the assembly: 0002f4a4 blits the whole of src
   into the frame buffer every pass, while 0002f5e5 memmoves the snapshot over
   it instead.

   EVERY EXTENT COMPARISON AND EVERY DIVISION HERE IS SIGNED.  The loop test
   at 0002f48b is JG / JLE, and the four divisions at 0002f53d onward are IDIV
   preceded by SAR EDX,0x1f.  Reading any of the extents or the steps as
   unsigned changes which branch a negative or an oversized argument takes.

   steps_x and steps_y are separate locals here where the original reuses the
   two slots at [EBP-0x18] and [EBP-0x14] for the per-axis frame count first
   and the pixel inset afterwards.  Two names for two quantities; the extra
   pair of stack slots is codegen, not behaviour (ADR-0001).

   The counter test at 0002f5db is CMP against 0 with JZ -- not a signed
   greater-than -- so a frame count that started below zero would run for
   about two billion frames rather than none.  It cannot start below zero for
   any positive extent: the subtraction only fires when the division came out
   exact, which needs a quotient of at least one.  A width of 0 with a
   non-zero step is the case that breaks it, and the original breaks there
   too. */
void fdps_transition_box(unsigned char *src, int src_pitch,
                         unsigned char *dst, int dst_pitch,
                         int width, int height,
                         int step_x, int step_y,
                         int frame_delay, int style)
{
    /* Declared in the order the original's frame is laid out -- [EBP-0x4] is
       the snapshot of the outgoing rectangle, [EBP-0x8] the frame buffer,
       [EBP-0xc] the counter, [EBP-0x10] the closing path's byte offset,
       [EBP-0x14] and [EBP-0x18] the two insets, [EBP-0x1c] and [EBP-0x20] the
       rectangle's current extents -- because wcc386 hands out the slots in
       declaration order.  Nothing depends on it. */
    unsigned char *saved_rect;
    unsigned char *frame_buf;
    int frames_left;
    int blit_offset;
    int inset_y;
    int inset_x;
    int cur_h;
    int cur_w;
    int steps_x;
    int steps_y;

    frame_buf = (unsigned char *) malloc((size_t) (width * height));
    saved_rect = (unsigned char *) malloc((size_t) (width * height));

    /* Take the outgoing picture off the destination surface before anything
       is drawn over it.  dst_pitch on the way out, width on the way in: the
       snapshot is packed. */
    fdps_blit_rect((unsigned int) dst, dst_pitch, saved_rect, width,
                   width, height);

    if (style == 0) {
        /* The outgoing picture closes in.  The rectangle of it that survives
           starts as the whole thing and loses step_x from each side and
           step_y from the top and bottom every frame, while blit_offset walks
           its top-left corner inward by exactly one step in both axes so its
           centre stays put. */
        cur_w = width;
        cur_h = height;
        blit_offset = inset_x = inset_y = 0;

        while (step_x * 2 <= cur_w && step_y * 2 <= cur_h) {
            fdps_blit_rect((unsigned int) src, src_pitch, frame_buf, width,
                           width, height);
            fdps_blit_rect((unsigned int) saved_rect + blit_offset, width,
                           frame_buf + blit_offset, width, cur_w, cur_h);
            fdps_blit_rect((unsigned int) frame_buf, width, dst, dst_pitch,
                           width, height);
            delay((unsigned int) frame_delay);

            cur_w -= step_x * 2;
            cur_h -= step_y * 2;
            blit_offset += step_y * width + step_x;
        }
    } else {
        /* The incoming picture opens out.  Each axis is asked how many whole
           steps fit across it, and an axis that divides evenly gives up one
           of them, which is what makes the last frame stop short of the full
           rectangle.  The animation runs for whichever axis allows fewer
           frames, so both edges arrive together. */
        steps_x = width / (step_x * 2);
        if (width % (step_x * 2) == 0) {
            steps_x--;
        }
        steps_y = height / (step_y * 2);
        if (height % (step_y * 2) == 0) {
            steps_y--;
        }

        if (steps_x > steps_y) {
            frames_left = steps_y;
        } else {
            frames_left = steps_x;
        }

        /* From here the two slots carry pixels rather than frames: the inset
           of the rectangle's top-left corner from the picture's, which shrinks
           by one step per frame until the rectangle is nearly the whole
           picture. */
        inset_x = frames_left * step_x;
        inset_y = frames_left * step_y;
        cur_w = width - inset_x * 2;
        cur_h = height - inset_y * 2;

        while (frames_left != 0) {
            memmove(frame_buf, saved_rect, (size_t) (width * height));
            fdps_blit_rect((unsigned int) src + inset_x + inset_y * src_pitch,
                           src_pitch,
                           frame_buf + inset_x + inset_y * width, width,
                           cur_w, cur_h);
            fdps_blit_rect((unsigned int) frame_buf, width, dst, dst_pitch,
                           width, height);
            delay((unsigned int) frame_delay);

            cur_w += step_x * 2;
            cur_h += step_y * 2;
            inset_x -= step_x;
            inset_y -= step_y;
            frames_left--;
        }
    }

    /* This is the blit that actually completes the transition, and it is
       outside both paths: whatever the animation did or did not manage to
       draw, the destination ends up holding the whole of src. */
    fdps_blit_rect((unsigned int) src, src_pitch, dst, dst_pitch,
                   width, height);

    free(frame_buf);
    free(saved_rect);
}
