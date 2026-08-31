/* gauge.c -- the game's gauge bars and where a combat gauge is placed.
 *
 * See gauge.h for what a caller has to know.  Nothing in this file holds
 * state: the unit record comes from fdps_get_unit_record (unit.h), the scroll
 * position from the two view window origin globals and the bar art from the
 * gauge sheet pointers gamedata.h declares, and the only thing written is the
 * caller's own memory -- a pair of ints, or a destination surface.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "unit.h"
#include "gauge.h"

/* The status gauge bar is 117 pixels wide and 8 rows tall, and the sheet holds
   its three graphics that many bytes apart at that row pitch: PUSH 0x75 for
   the source stride and PUSH 0x8 for the row count at both blits, and
   IMUL EAX,dword ptr [EBP+0x1c],0x3a8 at 0001774c for the graphic stride.
   0x3a8 is 0x75 * 8, so the sheet is three tightly packed frames and nothing
   pads between them. */
#define GAUGE_BAR_WIDTH 0x75
#define GAUGE_BAR_HEIGHT 8
#define GAUGE_BAR_GRAPHIC_STRIDE 0x3a8

/* See gauge.h.  The zero case is written first because that is the order the
   original has its blocks in: CMP dword ptr [EBP+0x20],0x0 / JG at 00017700
   jumps forward to the division and falls through to the empty bar.  The
   division itself is signed -- MOV EAX,EDX / SAR EDX,0x1f / IDIV at 00017713
   -- so the numerator is sign extended and a negative result truncates toward
   zero, which the callee's own clamp then turns into an empty bar.

   The rounding is up, not down: ADD EDX,[EBP+0x20] / DEC EDX at 0001770f adds
   max - 1 to the product before the divide.  That is what keeps one filled
   pixel on screen for any current of 1 or more, however large max is. */
void fdps_draw_gauge_bar_proportional(unsigned char *dst, int dst_stride,
                                      int bar_index, int max, int current)
{
    /* [EBP-4]: how many of the bar's 117 columns the filled graphic supplies.
       The original stages it in this local and reads it back to push it, so it
       is a named local here rather than an expression in the call. */
    int fill_width;

    if (max <= 0) {
        fill_width = 0;
    } else {
        fill_width = (current * GAUGE_BAR_WIDTH + max - 1) / max;
    }

    fdps_draw_gauge_bar(dst, dst_stride, bar_index, fill_width);
}

/* See gauge.h.  The three compares are separate and in this order -- CMP
   dword ptr [EBP+0x20],0x0 / JLE at 00017762 guarding the filled blit, then
   CMP ...,0x0 / JGE at 00017784 doing the clamp, then CMP ...,0x75 / JGE at
   0001778d guarding the remainder -- so a fill_width of exactly 0 skips the
   first blit and still draws the whole track, and a negative one is clamped
   only after the first blit has already been skipped.  The clamp writes the
   parameter slot itself at 00017786 and the remainder blit reads it back, so
   the clamped value is what positions and sizes the second half.

   The graphic base is worked out before the first compare, at 0001774c, and
   so is computed even on the path that never blits from it.  That is codegen
   and not behaviour: bar_index is only ever 0 to 2 here and the multiply
   cannot trap. */
void fdps_draw_gauge_bar(unsigned char *dst, int dst_stride, int bar_index,
                         int fill_width)
{
    /* [EBP-4]: the filled colour's graphic within the sheet.  The remainder
       deliberately does not go through it. */
    unsigned char *filled_graphic;

    filled_graphic = data_fdps_status_gauge_bar_sheet_ptr
                     + bar_index * GAUGE_BAR_GRAPHIC_STRIDE;

    if (fill_width > 0) {
        fdps_blit_transparent_rect(filled_graphic, GAUGE_BAR_WIDTH, dst,
                                   dst_stride, fill_width, GAUGE_BAR_HEIGHT);
    }

    if (fill_width < 0) {
        fill_width = 0;
    }

    if (fill_width < GAUGE_BAR_WIDTH) {
        fdps_blit_transparent_rect(data_fdps_status_gauge_bar_sheet_ptr
                                       + fill_width,
                                   GAUGE_BAR_WIDTH, dst + fill_width,
                                   dst_stride, GAUGE_BAR_WIDTH - fill_width,
                                   GAUGE_BAR_HEIGHT);
    }
}

/* The combat gauge's fill strip is 125 pixels wide and 5 rows tall, and the
   four strips of the scratch sheet are that many bytes apart at that row
   pitch: PUSH 0x7d for the source stride and PUSH 0x5 for the row count at
   000192ab and 0001929d, and IMUL EAX,dword ptr [EBP+0x1c],0x271 at 0001925c
   for the strip stride.  0x271 is 0x7d * 5, so the four strips are one packed
   0x9c4-byte surface with nothing between them. */
#define GAUGE_FILL_WIDTH 0x7d
#define GAUGE_FILL_HEIGHT 5
#define GAUGE_FILL_STRIP_STRIDE 0x271

/* CMP dword ptr [EBP+0x1c],0x2 / JGE at 0001927b: strips 0 and 1 fill
   right-to-left, 2 and upwards fill left-to-right. */
#define GAUGE_FILL_LEFT_ALIGNED_FROM 2

/* See gauge.h.  The three compares are in this order in the original and the
   order is behaviour, not layout: CMP dword ptr [EBP+0x20],0x0 / JGE at
   0001926e clamps first and writes the parameter slot itself at 00019274, so
   the alignment shift and the blit width both read the clamped value; then
   CMP ...,0x2 / JGE at 0001927b picks the alignment; then CMP ...,0x7d / JG
   at 00019297 decides whether anything is drawn at all.  All three are signed
   compares, so a negative fill_width and a negative gauge_index take the
   branch a value that had been read as unsigned would not.

   The strip base is worked out before the clamp, at 0001925c, and so is
   computed on the path that never blits.  That is codegen and not behaviour:
   the multiply cannot trap and the pointer is only dereferenced inside
   fdps_blit_transparent_rect.

   The original recomputes 0x7d - fill_width once for the strip at 00019286
   and again for dest at 00019291 rather than keeping it; the same value is
   added to both either way. */
void fdps_draw_gauge_fill(unsigned char *dest, int dest_stride,
                          int gauge_index, int fill_width)
{
    /* [EBP-4]: where in the sheet the run to copy starts -- strip
       gauge_index, advanced within that strip's own row when the gauge fills
       from the right. */
    unsigned char *fill_strip;

    fill_strip = data_fdps_gauge_fill_sheet_ptr
                 + gauge_index * GAUGE_FILL_STRIP_STRIDE;

    if (fill_width < 0) {
        fill_width = 0;
    }

    if (gauge_index < GAUGE_FILL_LEFT_ALIGNED_FROM) {
        fill_strip += GAUGE_FILL_WIDTH - fill_width;
        dest += GAUGE_FILL_WIDTH - fill_width;
    }

    if (fill_width <= GAUGE_FILL_WIDTH) {
        fdps_blit_transparent_rect(fill_strip, GAUGE_FILL_WIDTH, dest,
                                   dest_stride, fill_width,
                                   GAUGE_FILL_HEIGHT);
    }
}

/* See gauge.h.  The zero case is written first because that is the order the
   original has its blocks in: CMP dword ptr [EBP+0x20],0x0 / JG at 000192cc
   jumps forward to the division and falls through to the empty gauge, so a
   max of 0 and every negative max never reach the IDIV.

   The division is signed throughout -- IMUL EDX,dword ptr [EBP+0x24],0x7d /
   ADD EDX,dword ptr [EBP+0x20] / DEC EDX / MOV EAX,EDX / SAR EDX,0x1f / IDIV
   dword ptr [EBP+0x20] at 000192db..000192eb -- and the ADD/DEC pair is what
   makes it a ceiling rather than the obvious current * 0x7d / max, so any
   current of 1 or more keeps one lit pixel on screen however large max is.

   Nothing caps the width at the span's own 0x7d, and that is behaviour:
   fdps_draw_gauge_fill drops its blit outright above 0x7d, so a current above
   max leaves the gauge blank where an added min() would fill it. */
void fdps_draw_stat_gauge(unsigned char *dest, int dest_stride,
                          int gauge_index, int max, int current)
{
    /* [EBP-4]: how many of the span's 125 columns the fill strip supplies.
       The original stages it in this local and reads it back to push it, so
       it is a named local here rather than an expression in the call. */
    int fill_width;

    if (max <= 0) {
        fill_width = 0;
    } else {
        fill_width = (current * GAUGE_FILL_WIDTH + max - 1) / max;
    }

    fdps_draw_gauge_fill(dest, dest_stride, gauge_index, fill_width);
}

/* The unit gauge bar is 43 pixels wide and 6 rows tall, and the sheet holds
   its three graphics that many bytes apart at that row pitch: PUSH 0x2b for
   the source stride and PUSH 0x6 for the row count at every one of the twelve
   blits, and IMUL EAX,dword ptr [EBP+0x1c],0x102 at 0001cb0c for the graphic
   stride.  0x102 is 0x2b * 6, so the sheet is three tightly packed frames and
   nothing pads between them. */
#define UNIT_GAUGE_WIDTH 0x2b
#define UNIT_GAUGE_HEIGHT 6
#define UNIT_GAUGE_GRAPHIC_STRIDE 0x102

/* The rounded cap at each end is two pixels wide -- PUSH 0x2 at the first and
   the last blit of every mode -- and the right one begins at column 0x29,
   which is also the width of the interior the fill and the remainder divide
   between them: MOV EAX,0x29 / SUB EAX,dword ptr [EBP+0x20] at 0001cb79.
   0x29 + 2 is the bar's own 0x2b, so the remainder reaches the last column of
   the bar and the right cap is painted back over its last two. */
#define UNIT_GAUGE_CAP_WIDTH 2
#define UNIT_GAUGE_RIGHT_CAP_COLUMN 0x29

/* CMP dword ptr [EBP+0x24],0x0 / JNZ at 0001cb2b and CMP ...,0x1 / JNZ at
   0001cbcb.  These two are the only values with a meaning of their own;
   everything else falls through to the tint painter and is the tint colour. */
#define UNIT_GAUGE_MODE_PLAIN 0
#define UNIT_GAUGE_MODE_BLEND 1

/* See gauge.h.  The clamp is at 0001cb1e, before the mode test, so all three
   painters see the clamped value; the graphic base is worked out before it, at
   0001cb0c, and so is computed even in the modes and on the paths that never
   read from it.  That is codegen and not behaviour: the multiply cannot trap
   and the pointer is only dereferenced inside the blits.

   The four segments are written out once per mode rather than through a
   per-segment helper because that is the shape of the original -- three blocks
   of four calls, each block reached by its own branch -- and because the three
   painters take three different argument lists.  Within a block the guard on
   the fill run is CMP dword ptr [EBP+0x20],0x0 / JLE, so a fill_width of
   exactly 0 skips it and the whole interior still comes out of graphic 0.

   The two tables reach the blended painters as arguments and not as globals
   the painters read for themselves: MOV EAX,0x653f0 / PUSH EAX and
   MOV EAX,0x643f0 / PUSH EAX at every one of the eight blended calls. */
void fdps_draw_unit_gauge(unsigned char *dst, int dst_stride, int gfx_index,
                          int fill_width, int blit_mode, int alpha)
{
    /* [EBP-4]: the filled colour's graphic within the sheet.  The remainder
       deliberately does not go through it. */
    unsigned char *filled_graphic;

    filled_graphic = data_fdps_unit_gauge_sheet_ptr
                     + gfx_index * UNIT_GAUGE_GRAPHIC_STRIDE;

    if (fill_width < 0) {
        fill_width = 0;
    }

    if (blit_mode == UNIT_GAUGE_MODE_PLAIN) {
        fdps_blit_transparent_rect(filled_graphic, UNIT_GAUGE_WIDTH, dst,
                                   dst_stride, UNIT_GAUGE_CAP_WIDTH,
                                   UNIT_GAUGE_HEIGHT);
        if (fill_width > 0) {
            fdps_blit_transparent_rect(filled_graphic + UNIT_GAUGE_CAP_WIDTH,
                                       UNIT_GAUGE_WIDTH,
                                       dst + UNIT_GAUGE_CAP_WIDTH, dst_stride,
                                       fill_width, UNIT_GAUGE_HEIGHT);
        }
        fdps_blit_transparent_rect(data_fdps_unit_gauge_sheet_ptr
                                       + UNIT_GAUGE_CAP_WIDTH + fill_width,
                                   UNIT_GAUGE_WIDTH,
                                   dst + UNIT_GAUGE_CAP_WIDTH + fill_width,
                                   dst_stride,
                                   UNIT_GAUGE_RIGHT_CAP_COLUMN - fill_width,
                                   UNIT_GAUGE_HEIGHT);
        fdps_blit_transparent_rect(filled_graphic
                                       + UNIT_GAUGE_RIGHT_CAP_COLUMN,
                                   UNIT_GAUGE_WIDTH,
                                   dst + UNIT_GAUGE_RIGHT_CAP_COLUMN,
                                   dst_stride, UNIT_GAUGE_CAP_WIDTH,
                                   UNIT_GAUGE_HEIGHT);
    } else if (blit_mode == UNIT_GAUGE_MODE_BLEND) {
        fdps_blit_blend_transparent_rect(filled_graphic, UNIT_GAUGE_WIDTH, dst,
                                         dst_stride, dst, dst_stride,
                                         UNIT_GAUGE_CAP_WIDTH,
                                         UNIT_GAUGE_HEIGHT,
                                         data_fdps_palette_shade_ramp_table,
                                         data_fdps_inverse_palette_cube,
                                         alpha);
        if (fill_width > 0) {
            fdps_blit_blend_transparent_rect(
                filled_graphic + UNIT_GAUGE_CAP_WIDTH, UNIT_GAUGE_WIDTH,
                dst + UNIT_GAUGE_CAP_WIDTH, dst_stride,
                dst + UNIT_GAUGE_CAP_WIDTH, dst_stride, fill_width,
                UNIT_GAUGE_HEIGHT, data_fdps_palette_shade_ramp_table,
                data_fdps_inverse_palette_cube, alpha);
        }
        fdps_blit_blend_transparent_rect(
            data_fdps_unit_gauge_sheet_ptr + UNIT_GAUGE_CAP_WIDTH + fill_width,
            UNIT_GAUGE_WIDTH, dst + UNIT_GAUGE_CAP_WIDTH + fill_width,
            dst_stride, dst + UNIT_GAUGE_CAP_WIDTH + fill_width, dst_stride,
            UNIT_GAUGE_RIGHT_CAP_COLUMN - fill_width, UNIT_GAUGE_HEIGHT,
            data_fdps_palette_shade_ramp_table, data_fdps_inverse_palette_cube,
            alpha);
        fdps_blit_blend_transparent_rect(
            filled_graphic + UNIT_GAUGE_RIGHT_CAP_COLUMN, UNIT_GAUGE_WIDTH,
            dst + UNIT_GAUGE_RIGHT_CAP_COLUMN, dst_stride,
            dst + UNIT_GAUGE_RIGHT_CAP_COLUMN, dst_stride,
            UNIT_GAUGE_CAP_WIDTH, UNIT_GAUGE_HEIGHT,
            data_fdps_palette_shade_ramp_table, data_fdps_inverse_palette_cube,
            alpha);
    } else {
        fdps_blit_tint_transparent_rect(filled_graphic, UNIT_GAUGE_WIDTH, dst,
                                        dst_stride, UNIT_GAUGE_CAP_WIDTH,
                                        UNIT_GAUGE_HEIGHT,
                                        data_fdps_palette_shade_ramp_table,
                                        data_fdps_inverse_palette_cube,
                                        blit_mode, alpha);
        if (fill_width > 0) {
            fdps_blit_tint_transparent_rect(
                filled_graphic + UNIT_GAUGE_CAP_WIDTH, UNIT_GAUGE_WIDTH,
                dst + UNIT_GAUGE_CAP_WIDTH, dst_stride, fill_width,
                UNIT_GAUGE_HEIGHT, data_fdps_palette_shade_ramp_table,
                data_fdps_inverse_palette_cube, blit_mode, alpha);
        }
        fdps_blit_tint_transparent_rect(
            data_fdps_unit_gauge_sheet_ptr + UNIT_GAUGE_CAP_WIDTH + fill_width,
            UNIT_GAUGE_WIDTH, dst + UNIT_GAUGE_CAP_WIDTH + fill_width,
            dst_stride, UNIT_GAUGE_RIGHT_CAP_COLUMN - fill_width,
            UNIT_GAUGE_HEIGHT, data_fdps_palette_shade_ramp_table,
            data_fdps_inverse_palette_cube, blit_mode, alpha);
        fdps_blit_tint_transparent_rect(
            filled_graphic + UNIT_GAUGE_RIGHT_CAP_COLUMN, UNIT_GAUGE_WIDTH,
            dst + UNIT_GAUGE_RIGHT_CAP_COLUMN, dst_stride,
            UNIT_GAUGE_CAP_WIDTH, UNIT_GAUGE_HEIGHT,
            data_fdps_palette_shade_ramp_table, data_fdps_inverse_palette_cube,
            blit_mode, alpha);
    }
}

/* One map tile is 24 pixels square, and a map object's view position is its
   tile times this minus the view window origin.  IMUL EAX,EAX,0x18 at
   0001d5f5 and 0001d613. */
#define MAP_TILE_SIZE 0x18

/* The extra 4 pixels x carries over the plain tile conversion.  It is part of
   the position the gauge is measured from, not part of a step off the unit:
   ADD EDX,0x4 at 0001d603 happens before any of the branches. */
#define GAUGE_ANCHOR_X_SHIFT 4

/* The facing byte, record offset 3, is 0 down, 1 left, 2 up, 3 right, and the
   placement splits it at 2 -- CMP EAX,0x2 / JGE at 0001d62f.  Facings below
   this go above and to the right, facings from it up go below and to the
   left. */
#define GAUGE_FIRST_LOW_LEFT_FACING 2

/* The preferred vertical steps, up for the first pair of facings and down for
   the second, and the single nudge that replaces either of them when it would
   put the gauge off the top or the bottom of the screen.  The nudge is
   downward in both cases, which is why a clamped gauge sits on the unit
   rather than on the unit's other side. */
#define GAUGE_UP_STEP 0x10
#define GAUGE_DOWN_STEP 0x16
#define GAUGE_CLAMPED_Y_NUDGE 5

/* The two vertical limits.  The upward step is taken only while it leaves y
   past the top margin -- CMP EAX,0x4 / JG at 0001d63d, so y - 0x10 must reach
   5 -- and the downward step only while it leaves y short of 0xc3, which is
   where the gauge's six rows stop fitting above the bottom of the screen. */
#define GAUGE_TOP_MARGIN 4
#define GAUGE_BOTTOM_LIMIT 0xc3

/* The horizontal steps.  Right is preferred for the first pair of facings and
   left for the second, and each has its own fallback in the other direction;
   the fallbacks are 0x2d and 0x1c against preferred steps of 0x18 and 0x2c,
   so neither pair mirrors the other and all four are written out literally in
   the original. */
#define GAUGE_RIGHT_STEP 0x18
#define GAUGE_CLAMPED_LEFT_STEP 0x2d
#define GAUGE_LEFT_STEP 0x2c
#define GAUGE_CLAMPED_RIGHT_STEP 0x1c

/* The two horizontal limits.  0x45 is the right step plus the 0x2d-pixel box
   the gauge occupies, so the right step is taken only while the gauge's far
   edge stays short of 0x13b; the left step is taken only while it leaves x
   past the same 4-pixel margin the top uses -- CMP EAX,0x4 / JG at
   0001d699. */
#define GAUGE_RIGHT_EXTENT 0x45
#define GAUGE_RIGHT_LIMIT 0x13b
#define GAUGE_LEFT_MARGIN 4

/* See gauge.h.  The original recomputes each coordinate out of the caller's
   pair at every step rather than holding it in a register, and the pair is
   written before the branches rather than at the end, so a caller that passed
   two ints it also reads from elsewhere sees the intermediate values; nothing
   in the image does.

   Each of the four clamps is written with the fallback as the THEN arm, which
   is the way round the original has them: its JG at 0001d640 and JL at
   0001d65f jump to the preferred step and fall through to the fallback.  The
   other way round is the same behaviour and only mirrors the two blocks, but
   this way the emitted object comes out with the original's block order and
   jump senses, which is one less difference to explain to the next reader. */
void fdps_battle_compute_unit_gauge_position(int *out_position, int unit_index)
{
    struct fdps_unit_record *unit;

    unit = fdps_get_unit_record(unit_index);

    /* Both record bytes are widened unsigned -- MOV AL,byte ptr [EAX] then
       AND EAX,0xff at 0001d5ee and 0001d60b -- so a tile column past 127 is
       far to the right and not far to the left. */
    out_position[0] = (int) unit->pos_x * MAP_TILE_SIZE
                      - data_fdps_battle_view_window_origin_x
                      + GAUGE_ANCHOR_X_SHIFT;
    out_position[1] = (int) unit->pos_y * MAP_TILE_SIZE
                      - data_fdps_battle_view_window_origin_y;

    if (unit->facing < GAUGE_FIRST_LOW_LEFT_FACING) {
        if (out_position[1] - GAUGE_UP_STEP <= GAUGE_TOP_MARGIN) {
            out_position[1] += GAUGE_CLAMPED_Y_NUDGE;
        } else {
            out_position[1] -= GAUGE_UP_STEP;
        }
        if (out_position[0] + GAUGE_RIGHT_EXTENT >= GAUGE_RIGHT_LIMIT) {
            out_position[0] -= GAUGE_CLAMPED_LEFT_STEP;
        } else {
            out_position[0] += GAUGE_RIGHT_STEP;
        }
    } else {
        if (out_position[1] + GAUGE_DOWN_STEP >= GAUGE_BOTTOM_LIMIT) {
            out_position[1] += GAUGE_CLAMPED_Y_NUDGE;
        } else {
            out_position[1] += GAUGE_DOWN_STEP;
        }
        if (out_position[0] - GAUGE_LEFT_STEP <= GAUGE_LEFT_MARGIN) {
            out_position[0] += GAUGE_CLAMPED_RIGHT_STEP;
        } else {
            out_position[0] -= GAUGE_LEFT_STEP;
        }
    }
}
