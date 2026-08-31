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
