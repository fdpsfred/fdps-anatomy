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
 * (rebuild_info/build_flags.md).  malloc, free and rand come from <stdlib.h>
 * and are calls as well: CALL 0003d375, CALL 0003d478 and CALL 00042cf8.
 */
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "palette.h"
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

/* 0002f6d0.  Eight effects out of one body.  The dispatch at 0002f746 is
   CMP [EBP+0x34],0x7 followed by JA and then a jump through the eight-entry
   table at 0002f6e0, so the range check is unsigned: a style of 8, or a
   negative one, animates nothing and drops straight to the closing blit.

   Both scratch buffers are allocated and the snapshot is taken before the
   dispatch (0002f708, 0002f71b and the blit at 0002f73e), so the four styles
   that never read either one still pay for both, and neither malloc result is
   tested before it is used as a blit destination.

   All eight branches run the same loop: the position starts at step, the test
   is JL -- signed -- against height for styles 0-3 and against width for
   styles 4-7, and the increment is step.  The position that would reach or
   pass the extent ends the loop instead of being clamped, so the last partial
   step is never drawn and the frame that would show the whole incoming
   picture is never composed.  What completes the transition is the blit at
   0002fb38, which is outside every branch and outside the switch.

   THE FOUR ODD STYLES COMPOSE OFF-SCREEN, THE FOUR EVEN ONES DRAW STRAIGHT
   ONTO dst.  Styles 1, 3, 5 and 7 build a whole frame in frame_buf from two
   blits -- one rectangle of the incoming picture, one of the snapshot -- and
   present the whole of it, so every byte of the destination rectangle is
   rewritten each frame.  Styles 0, 2, 4 and 6 blit their partial rectangle
   onto dst and leave the rest of what is already there alone.

   STYLE 6 ANCHORS ON dst + (height - position) WHILE ITS LOOP COUNTS TO
   width.  0002fa76 is ADD EAX,[EBP+0x28] and then SUB EAX,[EBP-0x10]: the
   height, where the other three horizontal branches work in columns of width.
   It is a bug in the original and it has to stay -- see transit.h. */
void fdps_transition_slide(unsigned char *src, int src_pitch,
                           unsigned char *dst, int dst_pitch,
                           int width, int height,
                           int step, int frame_delay, int style)
{
    /* The original's frame, in slot order: [EBP-0x4] the snapshot of the
       outgoing picture, [EBP-0x8] the frame buffer, [EBP-0xc] the extent of
       the rectangle the incoming picture has not covered yet, [EBP-0x10] how
       far the slide has advanced.  The last two are rows for styles 0-3 and
       columns for styles 4-7; the assembly reuses one slot for each of them
       across all eight branches, and so does this. */
    unsigned char *saved_rect;
    unsigned char *frame_buf;
    int remaining_extent;
    int slide_pos;

    frame_buf = (unsigned char *) malloc((size_t) (width * height));
    saved_rect = (unsigned char *) malloc((size_t) (width * height));

    /* The picture that is on the destination surface right now.  dst_pitch on
       the way out, width on the way in: the snapshot is packed. */
    fdps_blit_rect((unsigned int) dst, dst_pitch, saved_rect, width,
                   width, height);

    switch (style) {
    case 0:
        /* The incoming picture descends from the top edge: its bottom
           slide_pos rows land on the top slide_pos rows of dst, and the
           outgoing picture below them is left where it is. */
        for (slide_pos = step; slide_pos < height; slide_pos += step) {
            fdps_blit_rect((unsigned int) (src + (height - slide_pos)
                                                     * src_pitch),
                           src_pitch, dst, dst_pitch, width, slide_pos);
            delay((unsigned int) frame_delay);
        }
        break;

    case 1:
        /* The outgoing picture slides off the top edge and uncovers the
           incoming one: the snapshot is composed slide_pos rows higher than
           it was, and the bottom slide_pos rows of the incoming picture fill
           the gap it leaves at the bottom -- at the rows they will finally
           occupy, not shifted. */
        for (slide_pos = step; slide_pos < height; slide_pos += step) {
            remaining_extent = height - slide_pos;
            fdps_blit_rect((unsigned int) (src + remaining_extent * src_pitch),
                           src_pitch, frame_buf + remaining_extent * width,
                           width, width, slide_pos);
            fdps_blit_rect((unsigned int) (saved_rect + slide_pos * width),
                           width, frame_buf, width, width, remaining_extent);
            fdps_blit_rect((unsigned int) frame_buf, width, dst, dst_pitch,
                           width, height);
            delay((unsigned int) frame_delay);
        }
        break;

    case 2:
        /* The incoming picture rises from the bottom edge: its top slide_pos
           rows land on the bottom slide_pos rows of dst. */
        for (slide_pos = step; slide_pos < height; slide_pos += step) {
            fdps_blit_rect((unsigned int) src, src_pitch,
                           dst + (height - slide_pos) * dst_pitch, dst_pitch,
                           width, slide_pos);
            delay((unsigned int) frame_delay);
        }
        break;

    case 3:
        /* The outgoing picture slides off the bottom edge: the top slide_pos
           rows of the incoming picture stay where they belong and the
           snapshot is composed slide_pos rows lower. */
        for (slide_pos = step; slide_pos < height; slide_pos += step) {
            remaining_extent = height - slide_pos;
            fdps_blit_rect((unsigned int) src, src_pitch, frame_buf, width,
                           width, slide_pos);
            fdps_blit_rect((unsigned int) saved_rect, width,
                           frame_buf + slide_pos * width, width,
                           width, remaining_extent);
            fdps_blit_rect((unsigned int) frame_buf, width, dst, dst_pitch,
                           width, height);
            delay((unsigned int) frame_delay);
        }
        break;

    case 4:
        /* The incoming picture arrives from the left edge: its rightmost
           slide_pos columns land on the leftmost slide_pos columns of dst. */
        for (slide_pos = step; slide_pos < width; slide_pos += step) {
            fdps_blit_rect((unsigned int) (src + (width - slide_pos)),
                           src_pitch, dst, dst_pitch, slide_pos, height);
            delay((unsigned int) frame_delay);
        }
        break;

    case 5:
        /* The outgoing picture slides off the left edge: the snapshot is
           composed slide_pos columns further left, and the rightmost
           slide_pos columns of the incoming picture fill the gap in place. */
        for (slide_pos = step; slide_pos < width; slide_pos += step) {
            remaining_extent = width - slide_pos;
            fdps_blit_rect((unsigned int) (src + remaining_extent), src_pitch,
                           frame_buf + remaining_extent, width,
                           slide_pos, height);
            fdps_blit_rect((unsigned int) (saved_rect + slide_pos), width,
                           frame_buf, width, remaining_extent, height);
            fdps_blit_rect((unsigned int) frame_buf, width, dst, dst_pitch,
                           width, height);
            delay((unsigned int) frame_delay);
        }
        break;

    case 6:
        /* The incoming picture arrives from the right edge -- and the anchor
           is height, not width.  Keep it: transit.h says what writing the
           obvious width here would change. */
        for (slide_pos = step; slide_pos < width; slide_pos += step) {
            fdps_blit_rect((unsigned int) src, src_pitch,
                           dst + (height - slide_pos), dst_pitch,
                           slide_pos, height);
            delay((unsigned int) frame_delay);
        }
        break;

    case 7:
        /* The outgoing picture slides off the right edge: the leftmost
           slide_pos columns of the incoming picture stay where they belong
           and the snapshot is composed slide_pos columns further right. */
        for (slide_pos = step; slide_pos < width; slide_pos += step) {
            remaining_extent = width - slide_pos;
            fdps_blit_rect((unsigned int) src, src_pitch, frame_buf, width,
                           slide_pos, height);
            fdps_blit_rect((unsigned int) saved_rect, width,
                           frame_buf + slide_pos, width,
                           remaining_extent, height);
            fdps_blit_rect((unsigned int) frame_buf, width, dst, dst_pitch,
                           width, height);
            delay((unsigned int) frame_delay);
        }
        break;
    }

    /* Outside the switch, and the only thing that guarantees the destination
       holds the whole incoming picture -- whatever style ran, and whether it
       drew a frame or not. */
    fdps_blit_rect((unsigned int) src, src_pitch, dst, dst_pitch,
                   width, height);

    free(frame_buf);
    free(saved_rect);
}

/* 0002fb80.  The mosaic.  The picture arrives one phase at a time: the
   rectangle is cut into block_w x block_h blocks, each block is assigned to
   one cell of a grid_cols x grid_rows phase grid by its position modulo the
   grid, the cells are shuffled, and then every block of one cell is drawn
   before delay() and the next cell.

   THE FIRST TWO ARGUMENTS ARE THE SOURCE PAIR AND THE SECOND TWO THE
   DESTINATION, WHICH IS THE OPPOSITE WAY ROUND FROM THE TWO ROUTINES ABOVE.
   The six pushes at 0002fdc5 onward are block_h, block_w, [EBP+0x20],
   [EBP+0x1c]-based, [EBP+0x18], [EBP+0x14]-based, and fdps_blit_rect takes
   (src_or_fill, src_stride, dst, dst_stride, bytes_per_row, rows) -- so
   [EBP+0x14] and [EBP+0x18] are what that routine reads from and [EBP+0x1c]
   and [EBP+0x20] are what it writes to.  All four call sites confirm it:
   000244d7 and 0002446c push 0xa0000 into the third slot and the loaded
   picture into the first.

   THE SOURCE SIDE HAS THE ZERO-STRIDE SPECIAL CASE AND THE DESTINATION SIDE
   DOES NOT.  0002fdac tests src_pitch, not dst_pitch, and when it is zero the
   block's byte offset is dropped from the source argument so that
   fdps_blit_rect receives the argument unchanged.  That is exactly what its
   fill mode needs (blit.h): with src_pitch 0 the first argument is a palette
   index rather than an address, and adding the block's offset to it would
   paint a different colour in every column.  The destination address is built
   the same way on both paths.

   EVERY DIVISION AND EVERY COMPARISON HERE IS SIGNED.  The six IDIV from
   0002fc2e onward each carry their SAR EDX,0x1f, and the loop tests and both
   bound tests are JL / JGE.  The two cell fields are read back with MOVSX at
   0002fd26 and 0002fd39, so they are signed 16-bit.

   THE SHUFFLE IS NOT FISHER-YATES AND THE PERMUTATION IS NOT UNIFORM.  Each
   pass draws two independent rand() values and swaps entry i with the entry
   the pair names, so an entry can be picked again and again and the draw is
   biased.  Both draws are in range by construction -- the products cannot
   exceed cell_count - 1 -- so the table stays a permutation and every cell is
   still drawn exactly once, whatever the sequence.  The order of the two
   rand() calls is fixed here in two statements rather than left inside one
   expression: C does not order the two calls within an expression, and which
   draw is taken modulo which extent decides the permutation whenever the grid
   is not square.

   THE VERTICAL BAND INDEX IS MULTIPLIED BY grid_cols WHILE ITS BAND COUNT IS
   DIVIDED BY grid_rows.  0002fd5f is IMUL EAX,[EBP+0x2c], the column count,
   and 0002fccb divides by [EBP+0x30], the row count.  On the square 16x16
   grid every caller uses they are the same number and it cannot be seen; on
   any other grid it means the pixel rows a cell reaches skip over whole bands,
   and rows of the rectangle are left carrying whatever was there before.
   Writing the obvious grid_rows here covers those rows.

   THE HORIZONTAL BOUND TEST COMPARES A BLOCK INDEX WITH A PIXEL WIDTH.
   0002fd9d is CMP EAX,[EBP+0x24] against the block column index, while the
   vertical test beside it compares a pixel row with the pixel height.  The
   horizontal test therefore hardly ever fires and what really bounds that axis
   is the blocks_x count, so the last band of a width that does not divide
   evenly is drawn past the right-hand edge.  Tightening the test to
   block_x * block_w changes what reaches the surface.

   ON THE GEOMETRY ALL FOUR CALL SITES PASS -- 320 x 200, 4 x 3 blocks, a 16 x
   16 grid -- THE COLUMNS COME OUT EXACT AND THE ROWS DO NOT.  80 block columns
   over 5 bands cover pixel columns 0 to 319 exactly, while the row that
   survives the height test furthest down starts at pixel row 198 and is three
   rows tall, so the last band writes one row past the bottom of the visible
   screen.  It lands in the unused tail of the 64K VGA window and does no harm;
   a clip added to stop it would also be a change to what the transition
   draws.

   The malloc result is not tested -- 0002fba9 stores EAX straight into the
   frame -- and the table is freed at 0002fe22 before the return. */
void fdps_transition_random_blocks(unsigned int src_or_fill, int src_pitch,
                                   unsigned char *dst, int dst_pitch,
                                   int width, int height,
                                   int grid_cols, int grid_rows,
                                   int block_w, int block_h, int frame_delay)
{
    /* The phase-cell table: two shorts per cell, the cell's column then its
       row, which is the four-byte entry the original allocates and moves as
       one dword.  [EBP-0x4] holds its base, and the fill loop walks it with a
       byte offset where this walks it with a short index -- the same entries
       in the same order (ADR-0001). */
    short *cell_table;
    int cell_count;
    int fill_pos;
    int fill_row;
    int fill_col;
    int cell;
    int swap_index;
    int random_row;
    int random_col;
    short saved_col;
    short saved_row;
    /* How many whole blocks the rectangle is across and down, before the phase
       grid divides them into bands. */
    int cols_total;
    int rows_total;
    /* How many blocks of one cell there are along each axis: the band counts,
       rounded up so a rectangle that does not divide evenly still gets its
       last partial band. */
    int blocks_x;
    int blocks_y;
    int cell_col;
    int cell_row;
    int row_band;
    int col_band;
    int block_x;
    int pixel_row;
    int src_x_offset;

    cell_count = grid_cols * grid_rows;
    cell_table = (short *) malloc((size_t) (cell_count * 4));

    /* Every cell of the phase grid, in row-major order. */
    fill_pos = 0;
    for (fill_row = 0; fill_row < grid_rows; fill_row++) {
        for (fill_col = 0; fill_col < grid_cols; fill_col++) {
            cell_table[fill_pos] = (short) fill_col;
            cell_table[fill_pos + 1] = (short) fill_row;
            fill_pos += 2;
        }
    }

    for (cell = 0; cell < cell_count; cell++) {
        random_row = rand() % grid_rows;
        random_col = rand() % grid_cols;
        swap_index = grid_cols * random_row + random_col;

        saved_col = cell_table[cell * 2];
        saved_row = cell_table[cell * 2 + 1];
        cell_table[cell * 2] = cell_table[swap_index * 2];
        cell_table[cell * 2 + 1] = cell_table[swap_index * 2 + 1];
        cell_table[swap_index * 2] = saved_col;
        cell_table[swap_index * 2 + 1] = saved_row;
    }

    cols_total = width / block_w;
    rows_total = height / block_h;
    blocks_x = cols_total / grid_cols;
    blocks_y = rows_total / grid_rows;
    if (cols_total % grid_cols != 0) {
        blocks_x++;
    }
    if (rows_total % grid_rows != 0) {
        blocks_y++;
    }

    for (cell = 0; cell < cell_count; cell++) {
        cell_col = cell_table[cell * 2];
        cell_row = cell_table[cell * 2 + 1];

        for (row_band = 0; row_band < blocks_y; row_band++) {
            pixel_row = block_h * (row_band * grid_cols + cell_row);

            for (col_band = 0; col_band < blocks_x; col_band++) {
                block_x = cell_col + col_band * grid_cols;

                if (block_x < width && pixel_row < height) {
                    if (src_pitch == 0) {
                        src_x_offset = 0;
                    } else {
                        src_x_offset = block_x * block_w;
                    }

                    fdps_blit_rect(src_or_fill + (unsigned int) src_x_offset
                                       + (unsigned int) (pixel_row * src_pitch),
                                   src_pitch,
                                   dst + block_x * block_w
                                       + pixel_row * dst_pitch,
                                   dst_pitch, block_w, block_h);
                }
            }
        }

        /* One pause per cell, after all of that cell's blocks and not after
           each of them: the pause at 0002fe11 is outside both band loops.  It
           is the whole pacing of the effect. */
        delay((unsigned int) frame_delay);
    }

    free(cell_table);
}

/* The VGA graphics aperture as a flat linear address and the size of one whole
   mode 13h frame.  Both are hard-coded in the original -- PUSH 0xa0000 at
   00031883, 000318b0 and 000318e2, PUSH 0xfa00 at 0003187a and 000318db -- and
   stay literals here: 0xa0000 is where the display adapter answers, not the
   address of anything the linker places, so there is no symbol to reference
   instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_BYTES 0xfa00

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, and it is the only bit the zoom looks at: TEST AL,0x8 at 000317db
   and 0003181d. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* How many steps the zoom has, and the index of the one that is 1:1.  CMP
   dword ptr [EBP-0x18],0x9 / JL at 000317a2 bounds the pass counter, and 8 is
   the last step in four separate roles: the subtrahend that reverses the order
   at 000317c3, the subtrahend of the palette bias at 000317df, the divisor of
   the centre interpolation at 00031838 and 00031860, and the value the 1:1
   test compares against at 00031874. */
#define ZOOM_STEP_COUNT 9
#define ZOOM_LAST_STEP 8

/* How much darker each step still to come leaves the palette: NEG EAX then
   LEA EAX,[EAX + EAX*0x2] at 000317e7 on the distance from the last step, so
   step 0 uploads at -24 and step 8 at 0. */
#define ZOOM_BIAS_PER_STEP 3

/* The point the centre interpolation walks toward, in pixels: SUB EDX,0x9f at
   00031824 and SUB EDX,0x63 at 0003184f.  It is the middle of the 320x200
   screen. */
#define SCREEN_CENTER_X 0x9f
#define SCREEN_CENTER_Y 0x63

/* fdps_blit_rotated_scaled takes its centre in quarter pixels rather than
   pixels (blit.h), which is what the LEA EAX,[EAX*0x4 + 0x0] at 00031842 and
   0003186a converts to. */
#define QUARTER_PIXELS_PER_PIXEL 4

/* 00031780.  The zoom the village and the five shop screens open and close
   with.  Nine steps, each one a palette upload sandwiched between the two
   halves of a retrace wait, then one redraw of the whole picture, then a wait
   for the timer tick to move.

   THE STEP INDEX IS NOT THE LOOP COUNTER.  The counter always runs 0..8 (CMP
   0x9 / JL at 000317a2); the direction is applied by deriving the step from it
   at 000317b5, so the loop body is written once and read forwards or
   backwards.  zoom_out is tested as a byte -- CMP byte ptr [EBP+0x20],0x0 at
   000317b5 and 000318d5 -- which is why it is a char here and not an int; the
   callers all push a whole dword holding 0 or 1.

   THE 1:1 STEP IS A memmove AND NOT A BLIT, AND THAT IS VISIBLE.
   fdps_blit_rotated_scaled writes only 318 x 198 of the page (blit.h), so the
   last two columns of every row and the bottom two rows would keep whatever
   was on screen before; the memmove at 00031888 is what makes the settled
   picture reach the edges.  It is the last step in the opening direction and
   the first in the closing one.

   BOTH CENTRE DIVISIONS TRUNCATE TOWARD ZERO.  0003182e onward is the
   SAR EDX,0x1f / SHL EDX,0x3 / SBB EAX,EDX / SAR EAX,0x3 idiom, which is a
   signed divide by 8 that rounds toward zero, not an arithmetic shift.  It
   only shows for a centre left of or above the screen centre, where the
   numerator is negative -- the village's signboard table has entries on both
   sides of it -- and writing the obvious >> 3 moves that step's centre by a
   quarter pixel.

   THE TICK THE FIRST STEP WAITS FOR IS WHATEVER WAS ON THE STACK.  last_tick
   is read at 000318bd before anything has written it, so the first step's wait
   ends immediately unless the garbage happens to equal the counter, and only
   the eight steps after it are tick-paced.  Latching the counter before the
   loop -- which is what writing this loop cleanly leads to -- adds a tick to
   every transition in the game.

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of this loop: nothing inside it writes the counter, so a build that
   was allowed to hoist the load would spin here forever. */
void fdps_transition_zoom(unsigned char *src_image, int center_x,
                          int center_y, char zoom_out)
{
    /* The camera heights the nine steps are drawn at, low to high.  In the
       original this is a nine-dword template at 0x00031040 copied onto the
       frame with REP MOVSD at 00031799, which is what an initialised
       automatic array compiles to; it is not a global and has no other
       reader.  fdps_blit_rotated_scaled reads a camera height as an inverse
       scale, so 50 is the four-times magnification the transition starts from
       and 1500 is 1:1 (blit.h). */
    int camera_height_ramp[ZOOM_STEP_COUNT] = {50, 200, 500, 800, 1000,
                                               1200, 1300, 1400, 1500};
    /* Which of the nine steps has been reached, always counting up. */
    int pass;
    /* Which step to draw, 0 at the magnified end and 8 at 1:1.  This is what
       the direction is expressed in. */
    int step;
    /* The per-channel darkening this step's palette upload carries. */
    int palette_bias;
    /* The point the picture is magnified about, in quarter pixels, which is
       what fdps_blit_rotated_scaled wants. */
    int blit_center_x;
    int blit_center_y;
    /* The tick the previous step ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    for (pass = 0; pass < ZOOM_STEP_COUNT; pass++) {
        if (zoom_out != 0) {
            step = pass;
        } else {
            step = ZOOM_LAST_STEP - pass;
        }

        /* The upload straddles the retrace: wait for it to start, rewrite the
           whole DAC while the beam is off the picture, then wait for it to
           end so that the redraw below does not begin inside the same
           retrace. */
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        palette_bias = -(ZOOM_LAST_STEP - step) * ZOOM_BIAS_PER_STEP;
        fdps_set_palette_range(
            (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
            0, 0xff, palette_bias, palette_bias, palette_bias);
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }

        blit_center_x = (center_x - (center_x - SCREEN_CENTER_X) * step
                                        / ZOOM_LAST_STEP)
                        * QUARTER_PIXELS_PER_PIXEL;
        blit_center_y = (center_y - (center_y - SCREEN_CENTER_Y) * step
                                        / ZOOM_LAST_STEP)
                        * QUARTER_PIXELS_PER_PIXEL;

        if (step == ZOOM_LAST_STEP) {
            memmove((void *) VGA_SCREEN_BASE, src_image,
                    (size_t) VGA_SCREEN_BYTES);
        } else {
            fdps_blit_rotated_scaled((unsigned char *) VGA_SCREEN_BASE,
                                     src_image, camera_height_ramp[step],
                                     0.0, blit_center_x, blit_center_y, 0.0);
        }

        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    /* The closing direction ends on the magnified step, so the picture is
       still on screen when the loop stops; this is what leaves the screen
       blank for whatever the caller draws next.  The opening direction ended
       on the memmove and keeps it. */
    if (zoom_out == 0) {
        memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);
    }

    /* Whichever direction ran, the DAC is left holding the master palette
       unbiased: the closing direction's last upload was at -24 and would
       otherwise leave the next screen dark. */
    fdps_set_palette_range(
        (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
        0, 0xff, 0, 0, 0);
}
