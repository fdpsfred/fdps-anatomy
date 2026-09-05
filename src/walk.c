/* walk.c -- animating a map unit along a movement path, one tile at a time.
 *
 * See walk.h for the shape the four per-direction step routines share and for
 * what each of them owns.  Nothing here holds state of its own: the unit
 * records, the battle view origin, the map cursor and the scene layers all
 * belong elsewhere and this file only reads and advances them.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapdraw.h"
#include "maptile.h"
#include "walk.h"

/* A scene tile is 24 pixels square, and the step routines convert between tile
   and pixel coordinates with that factor in both directions (IMUL EAX,EAX,0x18
   at 0002d384 and 0002d395, IDIV EBX with EBX = 0x18 at 0002d456 and
   0002d46c). */
#define SCENE_TILE_SIZE 0x18

/* Offset of the scene terrain layer's signed 16-bit tile height inside its
   header; the width sits at +7 (maptile.h). */
#define SCENE_TILE_MAP_HEIGHT_AT 9

/* Six passes of four pixels make one 24-pixel tile.  The sub-step counter
   stored in the unit record runs 1..6 while the animation plays and is cleared
   once the tile is committed. */
#define WALK_SUB_STEPS 6
#define WALK_STEP_PIXELS 4

/* The facing code src/mapdraw.c and fdps_unit_face_target use for a unit
   facing down the screen. */
#define MAP_UNIT_FACING_DOWN 0

/* The battle view is 192 pixels tall, so the lowest view origin that still has
   map underneath it is the map's pixel height less that. */
#define MAP_VIEW_HEIGHT 0xc0

/* The view only starts following the unit once the unit is more than 120
   pixels below the top of the view: above that it is still comfortably inside
   the window and the map stays put. */
#define VIEW_SCROLL_TRIGGER_Y 0x78

/* The two scancodes that fast-forward movement.  Either one suppresses the
   per-pass frame inside the loop; only the first one still draws the catch-up
   frame after it. */
#define SCANCODE_FAST_FORWARD 2
#define SCANCODE_SKIP_ANIMATION 3

/* The occasion fdps_map_set_pending_tile_event is asked about here: a unit
   finishing a step onto the cell during movement (maptile.h). */
#define TILE_EVENT_ON_ARRIVAL 0

/* 0002d360.  One tile down the screen.

   Three things here are behaviour rather than style.

   THE TWO PIXEL MEASUREMENTS ARE TAKEN ONCE, BEFORE THE LOOP.  The map's pixel
   height lands in [EBP-4] at 0002d387 and the unit's starting pixel row in
   [EBP-0xc] at 0002d398, and neither slot is written again.  The scroll test
   inside the loop therefore compares a FIXED starting row against a view
   origin that moves under it, and the gap closes by four pixels a pass until
   the test stops firing.  Remeasuring the unit's row each pass -- which is
   what reaching for unit->pos_y inside the loop would do -- keeps the gap
   constant instead and scrolls a different number of times.

   BOTH SCROLL TESTS ARE SIGNED AND BOTH ARE STRICT.  The assembly spells them
   CMP EDX,0x78 / JLE at 0002d3ce and CMP EAX,[0x00069ce0] / JG at 0002d3db,
   so a gap of exactly 120 does not scroll and a view origin already sitting
   exactly on the bottom limit does not move.  All three values are signed: a
   view origin above the map origin has to stay negative rather than becoming a
   huge unsigned number that passes every test.

   THE ARRIVAL TILE COMES FROM THE MAP CURSOR, NOT FROM THE RECORD.  The two
   IDIVs at 0002d456 and 0002d46c divide data_fdps_map_cursor_world_x and
   data_fdps_map_cursor_world_y by 24; the record's own pos_x and pos_y, which
   the increment three instructions earlier has just made current, are not
   read.  The two agree only while the view is locked onto the moving unit.
   The division is signed (SAR EDX,0x1f before each IDIV), so a cursor pixel
   left of or above the map origin truncates towards zero.

   The scancode latch is read through fdps_keyboard_scancode_ptr on every test,
   and the in-loop test really does call it twice -- CALL at 0002d3f3 and again
   at 0002d404 -- because the second comparison re-fetches the pointer.  The
   calls are kept because that is the original's own shape; the function is
   LEA/RET and has no side effect, so the count is unobservable either way. */
void fdps_walk_step_down(int unit_index)
{
    /* The moving unit's record inside the map unit array. */
    struct fdps_unit_record *unit;
    /* The whole map's height in pixels, from the terrain layer's header. */
    int map_height_px;
    /* Where the unit's row sat, in pixels, before the step began. */
    int start_pixel_y;
    /* The animation sub-step being played, 1..6. */
    int walk_step;

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
           + unit_index;
    map_height_px = (int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0]
                                      + SCENE_TILE_MAP_HEIGHT_AT)
                    * SCENE_TILE_SIZE;
    start_pixel_y = unit->pos_y * SCENE_TILE_SIZE;
    unit->facing = MAP_UNIT_FACING_DOWN;

    for (walk_step = 1; walk_step <= WALK_SUB_STEPS; walk_step++) {
        unit->walk_step = (unsigned char) walk_step;

        if (start_pixel_y - data_fdps_battle_view_window_origin_y
                > VIEW_SCROLL_TRIGGER_Y
            && data_fdps_battle_view_window_origin_y
                < map_height_px - MAP_VIEW_HEIGHT) {
            data_fdps_battle_view_window_origin_y += WALK_STEP_PIXELS;
        }
        data_fdps_map_cursor_world_y += WALK_STEP_PIXELS;

        if (*fdps_keyboard_scancode_ptr() != SCANCODE_FAST_FORWARD
            && *fdps_keyboard_scancode_ptr() != SCANCODE_SKIP_ANIMATION) {
            fdps_render_view_frame();
        }
    }

    if (*fdps_keyboard_scancode_ptr() == SCANCODE_FAST_FORWARD) {
        fdps_render_view_frame();
    }

    unit->pos_y++;
    unit->walk_step = 0;
    fdps_map_set_pending_tile_event(
        data_fdps_map_cursor_world_x / SCENE_TILE_SIZE,
        data_fdps_map_cursor_world_y / SCENE_TILE_SIZE,
        TILE_EVENT_ON_ARRIVAL);
}
