/* mapcur.c -- the battle map cursor: drawing its overlay, the information
 * panel beside it, the select loop and the animated move between tiles.
 *
 * See mapcur.h for what each cursor mode paints.  Everything here works out of
 * the shared cursor globals in gamedata.h; the one global the file owns is the
 * column the information panel is parked at, declared in mapcur.h.
 */
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "aitarget.h"
#include "audio.h"
#include "blit.h"
#include "cdaudio.h"
#include "keybd.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "maptile.h"
#include "movegrid.h"
#include "sprite.h"
#include "table.h"
#include "text.h"
#include "unit.h"

/* One map tile is 24 world pixels on each axis: the 0x18 that every displaced
   call site adds or subtracts, and the divisor of the mode 6 arm. */
#define CURSOR_TILE_STEP 0x18

/* The movement grid's cells start after its four-byte header (movegrid.h). */
#define MOVE_GRID_CELL_BASE 4

/* 0002c6a0.  A chain of six CMP dword ptr [0x00069cd0] / JNZ tests at
   0002c6ac, 0002c6d4, 0002c6fc, 0002c79c, 0002c930 and 0002cbc1, each arm
   ending in a JMP to the shared epilogue at 0002cc12.  A mode the chain does
   not name -- 0, or anything above 6 -- falls out of the last JNZ straight
   onto that epilogue, so it is silently nothing rather than an error.

   THE SHAPES ARE CALL LISTS, NOT LOOPS.  Every arm is written out one
   fdps_blit_cursor_tile call per cell in the original, and they are not the
   shapes a loop over a diamond would produce: mode 5 leaves the four tiles
   orthogonally adjacent to the centre blank, and its two lower diagonal cells
   take sprite 0x11 on the left and 0x10 on the right -- the only place in the
   whole list where the sprite ids do not run in reading order.  Folding any of
   this into a loop over a radius, or assuming the ids ascend with position,
   changes what the player sees (rebuild_info/pitfalls.md).

   The cursor position globals are re-read at every call site rather than being
   copied into the frame once (MOV EAX,[0x00069ccc] appears at each displaced
   push), and fdps_blit_cursor_tile writes to neither of them, so a single copy
   would be the same value each time.  They are spelled as the reads the
   original makes.

   MODE 6 IS THE TRAP.  This function is called from the frame compositor
   fdps_draw_scene_layers, and mode 6 draws nothing whatever: it divides both
   cursor coordinates by the tile size and clears byte 1 of the movement grid
   cell under the cursor -- the marker byte fdps_map_grid_reset fills with the
   0xff unreachable sentinel and fdps_map_grid_collect_marked_tiles reads to
   decide a cell is marked.  fdps_map_cursor_move_to draws a frame at every
   step of a cursor move while the mode is non-zero, so the tiles the cursor
   travels across are marked as a side effect of rendering them.  Hoisting the
   mode 6 arm out of the renderer, or drawing a different number of frames
   during a move, silently changes which tiles end up marked.

   Both divisions are signed -- SAR EDX,0x1f / IDIV EBX at 0002cbda and
   0002cbfd -- so a cursor pixel left of or above the map origin truncates
   towards zero and lands on tile 0, where an unsigned division would produce a
   tile index of hundreds of millions and write off the end of the block.  The
   grid width comes out of the header with MOVSX at 0002cbe5, signed for the
   same reason.

   The grid pointer is NOT checked for null here, unlike every function in
   src/movegrid.c: the original loads it at 0002cbdf and dereferences it
   immediately.  Nothing sets mode 6 before a chapter has been loaded, so the
   check the original leaves out is left out. */
void fdps_draw_map_cursor(unsigned char *scene_buffer)
{
    struct fdps_move_grid_cell *grid_cells;
    int grid_width;

    if (data_fdps_map_cursor_draw_mode == 1) {
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              0, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 2) {
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 3) {
        /* Radius-1 diamond: centre, then above, left, right, below. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              0x0e, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              5, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 4) {
        /* Radius-2 diamond, 13 tiles: centre, the four tips two tiles out,
           the four diagonals, then the four tiles one out. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              5, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0a, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              0x0b, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y,
                              0x0c, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x0d, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 5) {
        /* Radius-3 diamond, 21 tiles: centre, the four tips three tiles out,
           the eight (1,2) and (2,1) cells of that outer ring in four sprite
           pairs, the four tips two tiles out, and the four diagonals one out
           -- the last of which take 0x0e / 0x0f above and 0x11 / 0x10 below,
           out of numeric order.  The four tiles orthogonally adjacent to the
           centre get no call at all. */
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y,
                              1, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 3,
                              2, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 3,
                              data_fdps_map_cursor_world_y,
                              3, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 3,
                              data_fdps_map_cursor_world_y,
                              4, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 3,
                              5, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              6, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              7, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              8, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              9, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  - CURSOR_TILE_STEP * 2,
                              0x0a, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  - CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              0x0b, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x
                                  + CURSOR_TILE_STEP * 2,
                              data_fdps_map_cursor_world_y,
                              0x0c, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x,
                              data_fdps_map_cursor_world_y
                                  + CURSOR_TILE_STEP * 2,
                              0x0d, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0e, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y - CURSOR_TILE_STEP,
                              0x0f, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x - CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x11, scene_buffer);
        fdps_blit_cursor_tile(data_fdps_map_cursor_world_x + CURSOR_TILE_STEP,
                              data_fdps_map_cursor_world_y + CURSOR_TILE_STEP,
                              0x10, scene_buffer);
    } else if (data_fdps_map_cursor_draw_mode == 6) {
        grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
        grid_cells = (struct fdps_move_grid_cell *)
                     (data_fdps_battle_move_grid_ptr + MOVE_GRID_CELL_BASE);
        grid_cells[(data_fdps_map_cursor_world_y / CURSOR_TILE_STEP)
                       * grid_width
                   + data_fdps_map_cursor_world_x / CURSOR_TILE_STEP].marker =
            (unsigned char) 0;
    }
}

/* The battle view window the cursor is walked inside: 312 by 192 pixels of
   map, the same window fdps_render_view_frame presents (mapdraw.h).  The two
   far-edge pull-backs subtract them from the map's own pixel size -- SUB
   EAX,0x138 at 0002d9e7 and SUB EAX,0xc0 at 0002d96c. */
#define MAP_VIEW_WIDTH 0x138
#define MAP_VIEW_HEIGHT 0xc0

/* How far the cursor may get from the view's top-left corner before the view
   is scrolled to follow it.  The near limit is one tile on both axes (CMP
   EAX,0x18 / JGE at 0002d98b and 0002da06); the far limits are 216 across and
   144 down (CMP EAX,0xd8 / JLE at 0002d9c4, CMP EAX,0x90 / JLE at 0002d949).
   So the cursor is held in a band 192 wide and 120 tall inside a window 312 by
   192, and the two are NOT symmetric -- all of the leftover slack is on the
   right and at the bottom, which is where the information panel sits. */
#define CURSOR_VIEW_MIN_OFFSET 0x18
#define CURSOR_VIEW_MAX_OFFSET_X 0xd8
#define CURSOR_VIEW_MAX_OFFSET_Y 0x90

/* 0002d7c0.  Walks the cursor from where it stands to the target pixel a step
   at a time, scrolling the view to keep up and drawing a frame per step.  See
   mapcur.h for what a caller gets out of it.

   THE TWO DELTAS ARE MEASURED ONCE.  Both are taken before the walk and are
   never refreshed, so the axis test inside the loop -- which the original
   re-evaluates, two fresh abs calls a step, at 0002d8bf and 0002d8cd -- is
   asking the same question of the same two numbers every time round.  Hoisting
   it out of the loop is the same function; recomputing the deltas from the
   cursor as it moves is not.

   THE STEP COUNT IS THE DOMINANT AXIS IN TILES, AND THE WALK CAN STOP SHORT.
   n is abs(major delta) / 24 and the major axis advances by the truncated
   quotient major/n, so over n steps it covers n * (major/n), which is the
   whole delta only when n divides it.  Nothing after the loop writes the
   target in -- unlike fdps_icon_script_scroll_view_to_tile (icon.c), which
   closes its own gap with two stores -- so a target the division does not come
   out even on leaves the cursor a few pixels short of it for good.  Every
   caller passes a tile-aligned target, where the quotient is exactly one tile.

   THE MINOR AXIS CARRIES ITS REMAINDER.  Its slot holds an accumulator seeded
   with the whole minor delta; each step advances the axis by acc / n and
   re-seeds acc = minor delta + acc % n.  That distributes the remainder over
   the steps instead of dropping it, and it is what makes the minor axis arrive
   exactly on its delta: writing the obvious minor += delta / n per step leaves
   the cursor short by the remainder, permanently, for the same reason the
   major axis stops short.

   Both divisions on both axes are IDIV with the dividend sign-extended by SAR
   EDX,0x1f (0002d855, 0002d889, 0002d8e8, 0002d919), so they truncate toward
   zero, and the step count goes through abs first -- a move left or up divides
   a POSITIVE distance by the tile size, and dropping the abs would make n
   negative and the loop body never run at all.

   A MOVE OF LESS THAN ONE TILE ON THE DOMINANT AXIS DIVIDES BY ZERO.  n is
   then 0 and the very next IDIV faults; there is no guard anywhere in the
   function.  Nothing in play reaches it: all 34 call sites in the image push a
   whole-tile target -- a literal multiple of 0x18, or a tile number that has
   just gone through IMUL EAX,...,0x18 -- and every other writer of the cursor
   globals leaves them on a tile boundary as well (the two select loops step by
   0x18, the walk animators by 4 six times), so a target the cursor is not
   already standing on is at least one whole tile away on an axis.  Adding a
   step_count != 0 guard would be a check the original does not have.

   A FRAME IS DRAWN WHENEVER THE CURSOR IS VISIBLE OR THE VIEW MOVED, and the
   count of them is behaviour rather than pacing.  With a draw mode of 0 and a
   cursor that stays inside the band, no frame is drawn at all and the walk is
   instant.  With mode 6 the frame compositor clears the movement grid's marker
   byte under the cursor (fdps_draw_map_cursor above), so the tiles a sweep
   marks are exactly the ones a frame was drawn on -- one frame per step, no
   more and no fewer.

   THE FOUR VIEW CLAMPS ARE FOUR SEPARATE TESTS, NOT TWO PAIRS OF ALTERNATIVES.
   Each is its own if and each sets the moved flag; the near test is applied to
   an origin the far test may have just written, which is what pins a map
   smaller than the window at 0 rather than at a negative origin. */
void fdps_map_cursor_move_to(int target_x, int target_y)
{
    /* The map's full extent in pixels, from the two signed 16-bit tile
       dimensions in the movement grid's header (movegrid.h). */
    int map_pixel_width;
    int map_pixel_height;
    /* How far the cursor has to travel on each axis, measured once. */
    int delta_x;
    int delta_y;
    /* How many steps the walk takes, and which one is running. */
    int step_count;
    int step;
    /* What each axis carries.  For the axis with the larger delta this is the
       fixed advance the cursor takes every step; for the other one it is the
       accumulator described above, whose quotient by step_count is this step's
       advance and whose remainder is carried into the next seed. */
    int x_advance;
    int y_advance;
    /* Whether this step scrolled the view. */
    int view_moved;

    map_pixel_width = (int) *(short *) data_fdps_battle_move_grid_ptr
                      * CURSOR_TILE_STEP;
    map_pixel_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2)
                       * CURSOR_TILE_STEP;

    delta_x = target_x - data_fdps_map_cursor_world_x;
    delta_y = target_y - data_fdps_map_cursor_world_y;
    if (delta_x == 0 && delta_y == 0) {
        return;
    }

    if (abs(delta_x) > abs(delta_y)) {
        step_count = abs(delta_x) / CURSOR_TILE_STEP;
        x_advance = delta_x / step_count;
        y_advance = delta_y;
    } else {
        step_count = abs(delta_y) / CURSOR_TILE_STEP;
        y_advance = delta_y / step_count;
        x_advance = delta_x;
    }

    for (step = 0; step < step_count; step++) {
        view_moved = 0;

        if (abs(delta_x) > abs(delta_y)) {
            data_fdps_map_cursor_world_x += x_advance;
            data_fdps_map_cursor_world_y += y_advance / step_count;
            y_advance = delta_y + y_advance % step_count;
        } else {
            data_fdps_map_cursor_world_y += y_advance;
            data_fdps_map_cursor_world_x += x_advance / step_count;
            x_advance = delta_x + x_advance % step_count;
        }

        if (data_fdps_map_cursor_world_y
                - data_fdps_battle_view_window_origin_y
            > CURSOR_VIEW_MAX_OFFSET_Y) {
            data_fdps_battle_view_window_origin_y =
                data_fdps_map_cursor_world_y - CURSOR_VIEW_MAX_OFFSET_Y;
            if (data_fdps_battle_view_window_origin_y + MAP_VIEW_HEIGHT
                > map_pixel_height) {
                data_fdps_battle_view_window_origin_y =
                    map_pixel_height - MAP_VIEW_HEIGHT;
            }
            view_moved = 1;
        }
        if (data_fdps_map_cursor_world_y
                - data_fdps_battle_view_window_origin_y
            < CURSOR_VIEW_MIN_OFFSET) {
            data_fdps_battle_view_window_origin_y =
                data_fdps_map_cursor_world_y - CURSOR_VIEW_MIN_OFFSET;
            if (data_fdps_battle_view_window_origin_y < 0) {
                data_fdps_battle_view_window_origin_y = 0;
            }
            view_moved = 1;
        }
        if (data_fdps_map_cursor_world_x
                - data_fdps_battle_view_window_origin_x
            > CURSOR_VIEW_MAX_OFFSET_X) {
            data_fdps_battle_view_window_origin_x =
                data_fdps_map_cursor_world_x - CURSOR_VIEW_MAX_OFFSET_X;
            if (data_fdps_battle_view_window_origin_x + MAP_VIEW_WIDTH
                > map_pixel_width) {
                data_fdps_battle_view_window_origin_x =
                    map_pixel_width - MAP_VIEW_WIDTH;
            }
            view_moved = 1;
        }
        if (data_fdps_map_cursor_world_x
                - data_fdps_battle_view_window_origin_x
            < CURSOR_VIEW_MIN_OFFSET) {
            data_fdps_battle_view_window_origin_x =
                data_fdps_map_cursor_world_x - CURSOR_VIEW_MIN_OFFSET;
            if (data_fdps_battle_view_window_origin_x < 0) {
                data_fdps_battle_view_window_origin_x = 0;
            }
            view_moved = 1;
        }

        if (data_fdps_map_cursor_draw_mode != 0 || view_moved == 1) {
            fdps_render_view_frame();
        }
    }
}

/* The panel is drawn into fdps_render_view_frame's own scene buffer, which is
   360 bytes to the row -- PUSH 0x168 ahead of every one of the five drawing
   calls -- and 240 rows tall (the malloc of 0x15180 at 0002bebd). */
#define PANEL_BUF_PITCH 0x168

/* The two columns the panel is parked at: 0x124 puts it against the right edge
   and 0x19 against the left (MOV word ptr [0x0006016c] at 0002dd8b and
   0002dda4). */
#define PANEL_COLUMN_RIGHT 0x124
#define PANEL_COLUMN_LEFT 0x19

/* Where the panel dodges to, in tiles of the visible window: the cursor has to
   be below row 4 before the panel moves at all, and then column 0 or 1 sends
   it right and column 11 or beyond sends it left (CMP [EBP-0x4],0x4 with JLE
   at 0002dd7d and 0002dd96, CMP [EBP-0x8],0x2 with JL at 0002dd83 and
   CMP [EBP-0x8],0xa with JG at 0002dd9c).  Every one of those is a signed
   jump. */
#define PANEL_DODGE_ROW 4
#define PANEL_DODGE_COL_LEFT 2
#define PANEL_DODGE_COL_RIGHT 10

/* The panel's own layout, as rows and columns of the scene buffer.  The two
   cel blits carry their position as arguments -- PUSH 0xa0 and PUSH 0xaf for
   the rows, the panel column and the panel column plus 9 for the columns -- and
   the three that take a pixel pointer carry it folded into one displacement:
   0xe6b3, 0xf621, 0x10b39 and 0x11d8b, which are row * 0x168 + column for the
   rows and columns below.

   The unit sprite lands on exactly the cell the terrain tile was drawn into,
   which is what makes the terrain tile the sprite's backdrop rather than a
   second picture beside it. */
#define PANEL_TOP_ROW 0xa0
#define PANEL_TILE_ROW 0xaf
#define PANEL_TILE_COL 9
#define PANEL_AP_ROW 164
#define PANEL_AP_COL 19
#define PANEL_HP_ROW 190
#define PANEL_HP_COL 9
#define PANEL_DEF_ROW 203
#define PANEL_DEF_COL 19

/* The two percentages are drawn at their natural width with a leading '+' on a
   figure that is not negative; the HP figure is zero-padded to four digits and
   never carries a sign (PUSH 0x0 / MOV EAX,0x1 at 0002de0a and 0002de43
   against PUSH 0x4 / XOR EAX,EAX at 0002df4e). */
#define PANEL_MODIFIER_DIGITS 0
#define PANEL_MODIFIER_SHOW_PLUS 1
#define PANEL_HP_DIGITS 4
#define PANEL_HP_SHOW_PLUS 0

/* Number.cel's third colour row, which a unit below full HP has its figure
   drawn in (MOV dword ptr [0x0006000c],0x3 at 0002df44). */
#define PANEL_HP_HURT_COLOR_ROW 3

/* The unit's map sprite comes out of the .CEL sprite cache, whose offset table
   starts at the block's own base rather than at a .CEL's +0xf, twelve entries
   to a cache slot: four facings of three walk frames (0002dee0, and the same
   layout src/mapdraw.c walks). */
#define PANEL_SPRITES_PER_CACHE_SLOT 0x0c
#define CEL_SUB_IMAGE_ENTRY_BYTES 4

/* 0002dcf0.  The panel is composited over the finished scene by
   fdps_render_view_frame, its only caller, which hands it the same buffer it
   has just had fdps_draw_scene_layers fill.

   THE TWO TILE COORDINATES ARE NOT THE SAME PAIR.  The cursor's position
   within the visible window -- cursor minus view origin, over 24 -- decides
   where the panel is parked, while the tile whose information is shown is the
   cursor's absolute map tile, cursor over 24 with no origin subtracted
   (0002dcfc and 0002dd17 against 0002dd49 and 0002dd5f).  Using either pair
   for both jobs puts the panel on the wrong side the moment the map is
   scrolled.  All four divisions are SAR EDX,0x1f / IDIV EBX, so they truncate
   towards zero and a cursor above or left of the origin lands on tile 0 rather
   than on an enormous positive one.

   THE PANEL COLUMN IS ONLY EVER MOVED, NEVER RESET.  Neither arm of the dodge
   is an else of the other in the sense that one of them always runs: a cursor
   in the top five rows, or in the middle columns, leaves
   data_fdps_ui_terrain_hud_panel_offset holding whatever the last dodge put
   there.  That is the behaviour -- the panel stays where it was until the
   cursor actually reaches a corner it would sit under (mapcur.h).

   THE ATTACK AND DEFENSE FIGURES ARE DRAWN IN THE AMBIENT COLOUR ROW.  Nothing
   here writes data_fdps_number_glyph_color_row before those two calls, so they
   come out in whatever row the previous drawing call left behind; only the HP
   figure gets a row chosen for it, and only when the unit is hurt.  The store
   of 0 afterwards is unconditional and runs even when the HP figure was drawn
   in the ambient row (0002df74), so this function always leaves the row at 0
   once it has reached a unit -- and never touches it at all when it has not.

   The terrain class indexes both modifier tables past their six declared
   entries on shipped maps, and unlike the two combat readers this function has
   no precondition that keeps it inside them; what the original reads there is
   set out where those tables are declared (gamedata.h). */
void fdps_draw_cursor_info_panel(unsigned char *scene_buffer)
{
    struct fdps_unit_record *unit;
    unsigned char *unit_sprite_stream;
    int cursor_view_col;
    int cursor_view_row;
    int unit_index;
    int walk_frame;
    int sprite_cache_entry;
    int hp_current;
    int hp_max;

    cursor_view_col = (data_fdps_map_cursor_world_x
                       - data_fdps_battle_view_window_origin_x)
                      / CURSOR_TILE_STEP;
    cursor_view_row = (data_fdps_map_cursor_world_y
                       - data_fdps_battle_view_window_origin_y)
                      / CURSOR_TILE_STEP;

    if (data_fdps_ui_terrain_hud_user_enabled == 0
        || data_fdps_ui_play_active_flag == 0) {
        return;
    }

    fdps_map_load_tile_info(data_fdps_map_cursor_world_x / CURSOR_TILE_STEP,
                            data_fdps_map_cursor_world_y / CURSOR_TILE_STEP);

    if (cursor_view_row > PANEL_DODGE_ROW
        && cursor_view_col < PANEL_DODGE_COL_LEFT) {
        data_fdps_ui_terrain_hud_panel_offset = PANEL_COLUMN_RIGHT;
    } else if (cursor_view_row > PANEL_DODGE_ROW
               && cursor_view_col > PANEL_DODGE_COL_RIGHT) {
        data_fdps_ui_terrain_hud_panel_offset = PANEL_COLUMN_LEFT;
    }

    fdps_cel_blit_sprite(data_fdps_ui_terrain_hud_panel_sheet_ptr, 0,
                         scene_buffer, PANEL_BUF_PITCH,
                         data_fdps_ui_terrain_hud_panel_offset,
                         PANEL_TOP_ROW, 0, 0);
    fdps_cel_blit_sprite(data_fdps_scene_layer_tile_sheet_ptrs[0],
                         data_fdps_map_tile_info_tile_id,
                         scene_buffer, PANEL_BUF_PITCH,
                         data_fdps_ui_terrain_hud_panel_offset
                             + PANEL_TILE_COL,
                         PANEL_TILE_ROW, 0, 0);

    fdps_draw_number(scene_buffer + data_fdps_ui_terrain_hud_panel_offset
                         + PANEL_AP_ROW * PANEL_BUF_PITCH + PANEL_AP_COL,
                     PANEL_BUF_PITCH,
                     data_fdps_battle_tile_attr_ap_modifier_table[
                         data_fdps_map_tile_terrain_type],
                     PANEL_MODIFIER_DIGITS, PANEL_MODIFIER_SHOW_PLUS);
    fdps_draw_number(scene_buffer + data_fdps_ui_terrain_hud_panel_offset
                         + PANEL_DEF_ROW * PANEL_BUF_PITCH + PANEL_DEF_COL,
                     PANEL_BUF_PITCH,
                     data_fdps_battle_tile_attr_def_modifier_table[
                         data_fdps_map_tile_terrain_type],
                     PANEL_MODIFIER_DIGITS, PANEL_MODIFIER_SHOW_PLUS);

    unit_index = fdps_battle_find_unit_at_cursor();
    if (unit_index == -1) {
        return;
    }

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
           + unit_index;

    /* 0, 1, 2, 1 as the tick counter runs: the top of the two-bit phase is
       folded back onto 1 rather than wrapping to 0, so the walk cycle rocks
       between the two outer frames instead of jumping (SHR EAX,0x2 /
       AND EAX,0x3 at 0002deb4 and the CMP ...,0x3 at 0002debd).  The sprite is
       always taken from the first facing's three frames. */
    walk_frame = (int) ((data_fdps_timer_tick_counter >> 2) & 3);
    if (walk_frame == 3) {
        walk_frame = 1;
    }

    sprite_cache_entry = unit->sprite_cache_slot
                             * PANEL_SPRITES_PER_CACHE_SLOT
                         + walk_frame;
    unit_sprite_stream = data_fdps_cel_sprite_cache_ptr
        + *(int *) (data_fdps_cel_sprite_cache_ptr
                    + sprite_cache_entry * CEL_SUB_IMAGE_ENTRY_BYTES);
    fdps_blit_dispatch(unit_sprite_stream,
                       scene_buffer + data_fdps_ui_terrain_hud_panel_offset
                           + PANEL_TILE_ROW * PANEL_BUF_PITCH
                           + PANEL_TILE_COL,
                       CURSOR_TILE_STEP, CURSOR_TILE_STEP, PANEL_BUF_PITCH,
                       0, 0);

    hp_current = unit->hp_current;
    hp_max = unit->hp_max;
    if (hp_current != hp_max) {
        data_fdps_number_glyph_color_row = PANEL_HP_HURT_COLOR_ROW;
    }
    fdps_draw_number(scene_buffer + data_fdps_ui_terrain_hud_panel_offset
                         + PANEL_HP_ROW * PANEL_BUF_PITCH + PANEL_HP_COL,
                     PANEL_BUF_PITCH, hp_current,
                     PANEL_HP_DIGITS, PANEL_HP_SHOW_PLUS);
    data_fdps_number_glyph_color_row = 0;
}

/* The make codes the select loop answers to.  Every one is compared as a full
   int against the latched byte widened without sign (keybd.h). */
#define SCANCODE_ESC 0x01
#define SCANCODE_ENTER 0x1c
#define SCANCODE_Z 0x2c
#define SCANCODE_SPACE 0x39
#define SCANCODE_UP 0x48
#define SCANCODE_LEFT 0x4b
#define SCANCODE_KEYPAD_5 0x4c
#define SCANCODE_RIGHT 0x4d
#define SCANCODE_DOWN 0x50
#define SCANCODE_DELETE 0x53
#define SCANCODE_NONE 0xff

/* The sound every accepted cursor step plays.  It is the copy of the string at
   0x61e78 that this function pushes -- MOV EAX,0x61e78 ahead of all four calls
   -- and not the one at 0x61b04 that audio.c and save.c name; the two are
   separate literals holding the same eight characters. */
#define CURSOR_MOVE_SFX "Beep.wav"

/* The three select modes the loop settles itself rather than forwarding to
   fdps_collect_targets_in_area (aitarget.h), from CMP dword ptr [EBP+0x14]
   against 6, 5 and 4 at 0002b62f, 0002b762 and 0002b7ae. */
#define SELECT_MODE_MARKED_TILE 4
#define SELECT_MODE_CANCEL_ONLY 5
#define SELECT_MODE_MOVE_DEST 6

/* How many passes of the same held make code are swallowed before the cursor
   starts stepping again: CMP dword ptr [EBP-0x34],0x5 / JLE at 0002b896, so
   passes 1 through 5 move nothing and pass 6 onwards repeats. */
#define SELECT_HOLD_SUPPRESS_PASSES 5

/* The portrait id whose presence on the first candidate cancels the opening
   snap, CMP EAX,0x79 at 0002b578.  It is the only comparison against 0x79 in
   the whole image. */
#define SELECT_SNAP_SKIP_PORTRAIT 0x79

/* What the movement grid's marker byte holds for a tile the flood fill never
   reached (movegrid.h); the confirm arm treats it as "not a legal tile". */
#define MOVE_GRID_MARKER_UNREACHABLE 0xff

/* The movement cost a terrain has to come in under for mode 6 to accept the
   tile, CMP EAX,0x14 / JGE at 0002b74c.  PROMAP.DAT's real costs are small
   numbers and its impassable terrains are 0xFF (assets/tables/classes.md), so
   in play the test admits everything the acting class can walk on and rejects
   exactly the impassable rows. */
#define SELECT_MOVE_COST_LIMIT 0x14

/* How far the cursor may get from the view's left edge and from its top before
   the view is nudged after it.  These are NOT fdps_map_cursor_move_to's
   limits: the far edge across is 0x108 here against that function's 0xd8 (CMP
   dword ptr [EBP-0xc],0x108 / JLE at 0002b97f), while the near limit and the
   far edge down are the same numbers.  The nudge is a different action as
   well -- one tile at a time rather than a snap onto the limit. */
#define SELECT_VIEW_MAX_OFFSET_X 0x108
#define SELECT_VIEW_MAX_OFFSET_Y 0x90

/* 0002b4f0.  The player driving the map cursor: see mapcur.h for what each
   select mode confirms on and what the two return values mean.

   ITS PACING IS ONE FRAME A PASS AND THAT IS THE CONTRACT.  Every pass ends in
   fdps_render_view_frame (0002b9db), which spins on the VGA retrace twice, so
   the loop runs at the display's frame rate and the hold counter below counts
   frames rather than instructions.  Nothing here polls a timer.

   THE HOLD COUNTER STARTS ON WHATEVER THE STACK HELD.  Neither the previous
   make code at [EBP-0x1c] nor the counter at [EBP-0x34] is written before the
   loop -- the prologue's two stores, at 0002b4fc and 0002b503, reach other
   slots -- so the first pass compares the freshly cleared 0xff against
   garbage.  It settles on its own: 0xff matches no arm, and the first real key
   differs from whatever the previous code became and resets the counter to 0.
   Seeding either one would be a store the original does not make.

   THE LATCH IS CLEARED ONCE, BEFORE THE LOOP.  0002b5c8 writes 0xff through
   the pointer fdps_keyboard_scancode_ptr handed back, so a key pressed for the
   screen before this one is discarded; inside the loop only the cancel arm
   clears it again, and every other pass reads whatever the INT 09h handler has
   left there since.  A held key therefore reads the same code pass after pass,
   which is what the hold counter is counting.

   THE TWO EXITS ARE NOT THE SAME SHAPE.  A confirm on mode 4 or mode 6 returns
   1 from where it stands (MOV [EBP-0x20],0x1 / JMP 0002b9eb at 0002b7b4 and
   0002b751), so that pass draws no frame at all.  A cancel, and a confirm that
   went through fdps_collect_targets_in_area, instead park the answer in
   [EBP-0x14] and carry on: the rest of that pass still runs the cursor
   movement gate, the view scroll and one more frame, and the value is returned
   at the top of the NEXT pass.  Collapsing those into an immediate return
   loses that trailing frame.

   MODE 6 REUSES THE COUNT ARGUMENT AS THE ACTING UNIT'S INDEX.  0002b52d moves
   it aside and then forces the count to 0, which is what shuts the list
   cycling arm off on that path; both mode 6 call sites, 00025624 in
   fdps_battle_item_menu and 00027ff9 in fdps_battle_spell_command, push a unit
   index there and a null list.  On every other mode the saved slot is never
   written, and the one place that still reads it -- the dead recomputation of
   `unit` at 0002b841 -- never dereferences what it builds.

   THE BLOCKER SWEEP DOES NOT STOP.  It walks the whole unit array with no
   break (JMP 0002b654 at 0002b6c0), so a match late in the array is found
   after one early in it; the flag is only ever set and never cleared, so the
   answer is the same either way and the cost is the full walk on every
   confirm.

   THE MOVE COST IS INDEXED BY THE TERRAIN TYPE WITH NO BOUND.  move_cost holds
   eight entries and the terrain byte is not checked, exactly as in
   fdps_move_grid_flood_fill_range (movegrid.c): a terrain type above 7 reads
   the critical rate or the magic-resistance complement of the same 10-byte
   record instead. */
int fdps_map_cursor_select_loop(int select_mode, int list_count,
                                unsigned char *candidate_list)
{
    /* The map's full extent in pixels, from the two signed 16-bit tile
       dimensions in the movement grid's header (movegrid.h). */
    int map_pixel_width;
    int map_pixel_height;
    /* Mode 6's acting unit, taken out of list_count before that is forced to
       zero.  Written on no other path, and read on no other path either. */
    int actor_unit_index;
    /* The exclusive Manhattan distance handed to fdps_collect_targets_in_area:
       the cursor's own draw mode, less one while that is above 1, which is
       what makes the area accepted match the diamond fdps_draw_map_cursor is
       painting. */
    int target_max_dist;
    /* Which entry of candidate_list the cursor is parked on. */
    int list_pos;
    /* The answer the loop has settled on but not yet returned: 0 while it
       keeps running, 1 for a confirmed selection, -1 for a cancel. */
    int exit_code;
    /* The latched make code the keyboard ISR writes (keybd.h). */
    unsigned char *scancode_latch;
    /* What that byte held this pass, widened without sign. */
    int scancode;
    /* What it held on the previous pass, and how many passes in a row have now
       read the same code.  Deliberately left uninitialised -- see above. */
    int prev_scancode;
    int hold_count;
    /* The unit record the arm being taken is looking at. */
    struct fdps_unit_record *unit;
    /* The unit record the cursor is being snapped onto. */
    struct fdps_unit_record *snap_unit;
    /* The unit index the candidate list gave for that snap. */
    int list_unit_index;
    /* Whether a unit that has not left the field stands on the cursor's tile.
       Held in a byte, as the original's MOV byte ptr [EBP-0x4] does. */
    unsigned char blocker_on_cursor_tile;
    /* Which record the blocker sweep is looking at. */
    int scan_index;
    /* The acting unit's class code, and the PROMAP.DAT row it selects. */
    unsigned char actor_class;
    struct fdps_class_record *actor_class_record;
    /* Where the cursor sits inside the view window, measured once a pass. */
    int cursor_screen_x;
    int cursor_screen_y;

    list_pos = 0;
    exit_code = 0;

    map_pixel_width = (int) *(short *) data_fdps_battle_move_grid_ptr
                      * CURSOR_TILE_STEP;
    map_pixel_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2)
                       * CURSOR_TILE_STEP;

    if (select_mode == SELECT_MODE_MOVE_DEST) {
        actor_unit_index = list_count;
        list_count = 0;
    }

    target_max_dist = data_fdps_map_cursor_draw_mode;
    if (target_max_dist > 1) {
        target_max_dist--;
    }

    if (list_count != 0) {
        list_unit_index = (int) candidate_list[0];
        unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
               + list_unit_index;
        if ((int) unit->portrait_id != SELECT_SNAP_SKIP_PORTRAIT) {
            snap_unit = (struct fdps_unit_record *)
                            data_fdps_map_unit_array_ptr + list_unit_index;
            fdps_map_cursor_move_to((int) snap_unit->pos_x * CURSOR_TILE_STEP,
                                    (int) snap_unit->pos_y * CURSOR_TILE_STEP);
        }
    }

    scancode_latch = fdps_keyboard_scancode_ptr();
    *scancode_latch = (unsigned char) SCANCODE_NONE;

    for (;;) {
        if (exit_code != 0) {
            return exit_code;
        }
        fdps_cd_music_repeat_poll();

        scancode = (int) *scancode_latch;
        if (scancode == prev_scancode) {
            hold_count++;
        } else {
            prev_scancode = scancode;
            hold_count = 0;
        }

        if (scancode == SCANCODE_ESC || scancode == SCANCODE_DELETE) {
            exit_code = -1;
            *scancode_latch = (unsigned char) SCANCODE_NONE;
        } else if (scancode == SCANCODE_SPACE || scancode == SCANCODE_ENTER) {
            if (select_mode == SELECT_MODE_MOVE_DEST) {
                blocker_on_cursor_tile = 0;
                for (scan_index = 0;
                     scan_index < data_fdps_map_unit_count;
                     scan_index++) {
                    unit = (struct fdps_unit_record *)
                               data_fdps_map_unit_array_ptr + scan_index;
                    if ((int) unit->pos_x * CURSOR_TILE_STEP
                            == data_fdps_map_cursor_world_x
                        && (int) unit->pos_y * CURSOR_TILE_STEP
                            == data_fdps_map_cursor_world_y
                        && fdps_unit_is_retired(scan_index) == 0) {
                        blocker_on_cursor_tile = 1;
                    }
                }

                if (blocker_on_cursor_tile == 0) {
                    unit = (struct fdps_unit_record *)
                               data_fdps_map_unit_array_ptr
                           + actor_unit_index;
                    actor_class = unit->clazz;
                    actor_class_record =
                        fdps_get_class_record((int) actor_class + 1);
                    fdps_map_load_tile_info(
                        data_fdps_map_cursor_world_x / CURSOR_TILE_STEP,
                        data_fdps_map_cursor_world_y / CURSOR_TILE_STEP);
                    if ((int) actor_class_record->move_cost[
                                  data_fdps_map_tile_terrain_type]
                        < SELECT_MOVE_COST_LIMIT) {
                        return 1;
                    }
                }
            } else if (select_mode != SELECT_MODE_CANCEL_ONLY) {
                fdps_map_load_tile_info(
                    data_fdps_map_cursor_world_x / CURSOR_TILE_STEP,
                    data_fdps_map_cursor_world_y / CURSOR_TILE_STEP);
                if ((int) data_fdps_map_current_move_grid_marker
                    != MOVE_GRID_MARKER_UNREACHABLE) {
                    if (select_mode == SELECT_MODE_MARKED_TILE) {
                        return 1;
                    }
                    if (fdps_collect_targets_in_area(
                            data_fdps_map_cursor_world_x / CURSOR_TILE_STEP,
                            data_fdps_map_cursor_world_y / CURSOR_TILE_STEP,
                            target_max_dist, NULL, select_mode) != 0) {
                        exit_code = 1;
                    }
                }
            }
        } else if ((scancode == SCANCODE_Z || scancode == SCANCODE_KEYPAD_5)
                   && list_count != 0) {
            list_pos++;
            if (list_pos == list_count) {
                list_pos = 0;
            }
            list_unit_index = (int) candidate_list[list_pos];
            /* 0002b841 recomputes `unit` from the acting unit's index and then
               never reads it again on any path.  It is kept because it is what
               the original does; it dereferences nothing, which is why it is
               harmless on the modes that leave actor_unit_index unwritten. */
            unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
                   + actor_unit_index;
            snap_unit = (struct fdps_unit_record *)
                            data_fdps_map_unit_array_ptr + list_unit_index;
            fdps_map_cursor_move_to((int) snap_unit->pos_x * CURSOR_TILE_STEP,
                                    (int) snap_unit->pos_y * CURSOR_TILE_STEP);
        }

        if (hold_count == 0 || hold_count > SELECT_HOLD_SUPPRESS_PASSES) {
            if (scancode == SCANCODE_UP
                && data_fdps_map_cursor_world_y >= CURSOR_TILE_STEP) {
                data_fdps_map_cursor_world_y -= CURSOR_TILE_STEP;
                fdps_play_sfx(CURSOR_MOVE_SFX);
            } else if (scancode == SCANCODE_DOWN
                       && map_pixel_height - CURSOR_TILE_STEP
                              > data_fdps_map_cursor_world_y) {
                data_fdps_map_cursor_world_y += CURSOR_TILE_STEP;
                fdps_play_sfx(CURSOR_MOVE_SFX);
            } else if (scancode == SCANCODE_LEFT
                       && data_fdps_map_cursor_world_x >= CURSOR_TILE_STEP) {
                data_fdps_map_cursor_world_x -= CURSOR_TILE_STEP;
                fdps_play_sfx(CURSOR_MOVE_SFX);
            } else if (scancode == SCANCODE_RIGHT
                       && map_pixel_width - CURSOR_TILE_STEP
                              > data_fdps_map_cursor_world_x) {
                data_fdps_map_cursor_world_x += CURSOR_TILE_STEP;
                fdps_play_sfx(CURSOR_MOVE_SFX);
            }
        }

        cursor_screen_x = data_fdps_map_cursor_world_x
                          - data_fdps_battle_view_window_origin_x;
        cursor_screen_y = data_fdps_map_cursor_world_y
                          - data_fdps_battle_view_window_origin_y;

        if (data_fdps_battle_view_window_origin_x >= CURSOR_VIEW_MIN_OFFSET
            && cursor_screen_x < CURSOR_VIEW_MIN_OFFSET) {
            data_fdps_battle_view_window_origin_x -= CURSOR_TILE_STEP;
        }
        if (cursor_screen_x > SELECT_VIEW_MAX_OFFSET_X
            && map_pixel_width - MAP_VIEW_WIDTH
                   > data_fdps_battle_view_window_origin_x) {
            data_fdps_battle_view_window_origin_x += CURSOR_TILE_STEP;
        }
        if (data_fdps_battle_view_window_origin_y >= CURSOR_VIEW_MIN_OFFSET
            && cursor_screen_y < CURSOR_VIEW_MIN_OFFSET) {
            data_fdps_battle_view_window_origin_y -= CURSOR_TILE_STEP;
        }
        if (cursor_screen_y > SELECT_VIEW_MAX_OFFSET_Y
            && map_pixel_height - MAP_VIEW_HEIGHT
                   > data_fdps_battle_view_window_origin_y) {
            data_fdps_battle_view_window_origin_y += CURSOR_TILE_STEP;
        }

        fdps_render_view_frame();
    }
}

/* 0002da50.  IMUL EAX,dword ptr [EBP+0x14],0x50 at 0002da5c scales the index
   by the record stride, MOV EDX,dword ptr [0x00069cd8] / ADD EDX,EAX at
   0002da60 adds the array base to it, and the sum is parked in the one stack
   slot the function has.  It is reloaded twice, once for each coordinate:
   MOV AL,byte ptr [EAX+0x1] then AND EAX,0xff then IMUL EAX,EAX,0x18 pushes
   the y argument, and the same three instructions against byte ptr [EAX]
   push the x argument.  ADD ESP,0x8 after the CALL is the caller cleaning up
   its own two arguments.

   THE INDEX IS NOT CHECKED AND MUST NOT BE.  Nothing here reads
   data_fdps_map_unit_count, and the 48 call sites include the whole chapter
   init family passing literals as high as 0x2a -- indices that name a unit
   the battle has not deployed yet on the map the count describes.  An
   `if (unit_index < data_fdps_map_unit_count)` guard, which is the obvious
   thing to write, would silently drop cursor moves the original performs
   (rebuild_info/pitfalls.md).

   BOTH COORDINATE BYTES ARE ZERO-EXTENDED.  AND EAX,0xff at 0002da71 and
   0002da7f is what widens them, so a coordinate of 0x80 or above scales to a
   large positive pixel offset rather than a negative one; reading them
   through Watcom's default signed char would sign-extend instead.  They are
   unsigned char in struct fdps_unit_record, which is the same widening.

   It hands the walk to fdps_map_cursor_move_to above, so the cursor animates
   across to the unit rather than jumping, and everything that function says
   about frames, view scrolling and a target less than one tile away applies
   unchanged. */
void fdps_map_cursor_move_to_unit(int unit_index)
{
    /* The record of the unit the cursor is being sent to. */
    struct fdps_unit_record *unit;

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
           + unit_index;
    fdps_map_cursor_move_to((int) unit->pos_x * CURSOR_TILE_STEP,
                            (int) unit->pos_y * CURSOR_TILE_STEP);
}
