/* gauge.c -- the game's gauge bars and where a combat gauge is placed.
 *
 * See gauge.h for what a caller has to know.  Nothing in this file holds
 * state: the unit record comes from fdps_get_unit_record (unit.h) and the
 * scroll position from the two view window origin globals gamedata.h
 * declares, and the only thing written is the caller's own pair of ints.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "gauge.h"

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
