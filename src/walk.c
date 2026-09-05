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

/* Offset of the scene terrain layer's signed 16-bit tile width in the same
   header (maptile.h).  MOVSX word ptr [EAX+7] at 0002d6c0. */
#define SCENE_TILE_MAP_WIDTH_AT 7

/* Six passes of four pixels make one 24-pixel tile.  The sub-step counter
   stored in the unit record runs 1..6 while the animation plays and is cleared
   once the tile is committed. */
#define WALK_SUB_STEPS 6
#define WALK_STEP_PIXELS 4

/* The facing codes src/mapdraw.c and fdps_unit_face_target use for a unit
   facing down, left and up the screen. */
#define MAP_UNIT_FACING_DOWN 0
#define MAP_UNIT_FACING_LEFT 1
#define MAP_UNIT_FACING_UP 2
#define MAP_UNIT_FACING_RIGHT 3

/* The battle view is 192 pixels tall, so the lowest view origin that still has
   map underneath it is the map's pixel height less that. */
#define MAP_VIEW_HEIGHT 0xc0

/* The battle view is 312 pixels wide, so the rightmost view origin that still
   has map underneath it is the map's pixel width less that.  SUB EAX,0x138 at
   0002d718. */
#define MAP_VIEW_WIDTH 0x138

/* The view only starts following the unit once the unit is more than 120
   pixels below the top of the view: above that it is still comfortably inside
   the window and the map stays put. */
#define VIEW_SCROLL_TRIGGER_Y 0x78

/* Walking right, the view follows only while the unit's starting column is
   more than 240 pixels right of the left edge of the view: any nearer and the
   unit is still comfortably inside the 312-pixel window and the map stays
   put. */
#define VIEW_SCROLL_TRIGGER_X 0xf0

/* Walking up, the view follows only while the unit's starting row is less than
   two tiles below the top of the view; once it sits further down than that the
   unit is comfortably inside the window and the map stays put. */
#define VIEW_SCROLL_UP_TRIGGER_Y 0x30

/* Walking left, the view follows only while the unit's starting column is less
   than two tiles right of the left edge of the view; further right than that
   the unit is comfortably inside the window and the map stays put. */
#define VIEW_SCROLL_LEFT_TRIGGER_X 0x30

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

/* 0002d480.  One tile up the screen.

   The shape is the downward step's, mirrored, with three differences that are
   behaviour rather than style.

   THERE IS NO MAP EXTENT TO CLAMP AGAINST, AND NONE IS READ.  Walking down
   needs the terrain layer's tile height so the view stops at the bottom of the
   map; walking up needs only CMP dword ptr [0x00069ce0],0x4 at 0002d4e3, which
   stops the view the moment another four-pixel step would carry it above the
   map's own row 0.  The terrain header is never touched here, so this routine
   works with no scene layer loaded at all, and adding a symmetrical height
   lookup would give it a dependency the original does not have.

   THE STARTING ROW IS MEASURED ONCE AND THE GAP GROWS UNDER IT.  [EBP-0x8] is
   written at 0002d4a8 and never again, so the loop compares a fixed pixel row
   against a view origin that moves up beneath it four pixels a pass.  The gap
   therefore INCREASES, and the scroll switches itself off once it reaches 0x30
   -- the mirror of the downward step, where it closes instead.  Both tests are
   signed and their senses are opposite: JGE at 0002d4e1 skips the scroll when
   the gap has reached 48, so 47 scrolls and 48 does not, and JGE at 0002d4ea
   takes it when the origin is at least 4, so 4 scrolls and 3 does not.  A
   starting row above the view's top gives a negative gap, which is why the
   comparison has to stay signed.

   THE ARRIVAL TILE COMES FROM THE MAP CURSOR, NOT FROM THE RECORD.  The two
   IDIVs at 0002d55f and 0002d575 divide data_fdps_map_cursor_world_x and
   data_fdps_map_cursor_world_y by 24 after the loop has moved the cursor; the
   record's pos_x and the pos_y the DEC at 0002d540 has just made current are
   not read.  The two agree only while the view is locked onto the moving unit.
   Both divisions are signed (SAR EDX,0x1f ahead of each IDIV).

   As in the downward step the in-loop scancode test calls
   fdps_keyboard_scancode_ptr twice -- 0002d4fc and 0002d50d -- because the
   second comparison re-fetches the pointer, and the tail test at 0002d527
   draws the catch-up frame on scancode 2 only while the in-loop suppression
   covers 2 and 3 alike. */
void fdps_animate_move_step_up(int unit_index)
{
    /* The moving unit's record inside the map unit array. */
    struct fdps_unit_record *unit;
    /* Where the unit's row sat, in pixels, before the step began. */
    int start_pixel_y;
    /* The animation sub-step being played, 1..6. */
    int walk_step;

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
           + unit_index;
    start_pixel_y = unit->pos_y * SCENE_TILE_SIZE;
    unit->facing = MAP_UNIT_FACING_UP;

    for (walk_step = 1; walk_step <= WALK_SUB_STEPS; walk_step++) {
        unit->walk_step = (unsigned char) walk_step;

        if (start_pixel_y - data_fdps_battle_view_window_origin_y
                < VIEW_SCROLL_UP_TRIGGER_Y
            && data_fdps_battle_view_window_origin_y >= WALK_STEP_PIXELS) {
            data_fdps_battle_view_window_origin_y -= WALK_STEP_PIXELS;
        }
        data_fdps_map_cursor_world_y -= WALK_STEP_PIXELS;

        if (*fdps_keyboard_scancode_ptr() != SCANCODE_FAST_FORWARD
            && *fdps_keyboard_scancode_ptr() != SCANCODE_SKIP_ANIMATION) {
            fdps_render_view_frame();
        }
    }

    if (*fdps_keyboard_scancode_ptr() == SCANCODE_FAST_FORWARD) {
        fdps_render_view_frame();
    }

    unit->pos_y--;
    unit->walk_step = 0;
    fdps_map_set_pending_tile_event(
        data_fdps_map_cursor_world_x / SCENE_TILE_SIZE,
        data_fdps_map_cursor_world_y / SCENE_TILE_SIZE,
        TILE_EVENT_ON_ARRIVAL);
}

/* 0002d590.  One tile left across the screen.

   This is the upward step turned through ninety degrees: the same six passes
   of four pixels, the same pair of scroll tests with the same senses, the same
   scancode handling and the same arrival report.  What changes is the axis --
   the tile COLUMN at record+0, the view origin data_fdps_battle_view_window_
   origin_x and the cursor column data_fdps_map_cursor_world_x -- and the
   facing code.  Four things are behaviour rather than style.

   THERE IS NO MAP EXTENT TO CLAMP AGAINST, AND NONE IS READ.  The whole body
   touches no map layer: CMP dword ptr [0x00069ce4],0x4 at 0002d5f2 is the only
   thing stopping the view, and it stops it the moment another four-pixel step
   would carry it left of the map's own column 0.  A symmetrical width lookup
   out of the terrain header -- the mirror of what the downward step does with
   the height -- would give this routine a dependency the original does not
   have.

   THE STARTING COLUMN IS MEASURED ONCE AND THE GAP GROWS UNDER IT.  [EBP-0x8]
   is written at 0002d5b7 and never again, so the loop compares a fixed pixel
   column against a view origin that moves left beneath it four pixels a pass.
   The gap therefore INCREASES, and the scroll switches itself off once it
   reaches 0x30.  Both tests are signed and their senses are opposite: JGE at
   0002d5f0 skips the scroll when the gap has reached 48, so 47 scrolls and 48
   does not, and JGE at 0002d5f9 takes it when the origin is at least 4, so 4
   scrolls and 3 does not.  A starting column left of the view's edge gives a
   negative gap, which is why the comparison has to stay signed.

   THE COLUMN WIDENS UNSIGNED.  MOV AL,byte ptr [EAX] then AND EAX,0xff at
   0002d5ad..0002d5af: the tile column is a plain byte and column 200 is pixel
   4800, not -1344.

   THE ARRIVAL TILE COMES FROM THE MAP CURSOR, NOT FROM THE RECORD.  The two
   IDIVs at 0002d67d and 0002d683 divide data_fdps_map_cursor_world_x and
   data_fdps_map_cursor_world_y by 24 after the loop has moved the cursor; the
   record's pos_y and the pos_x the DEC at 0002d64f has just made current are
   not read.  The two agree only while the view is locked onto the moving unit.
   Both divisions are signed (SAR EDX,0x1f ahead of each IDIV).

   As in the other two directions the in-loop scancode test calls
   fdps_keyboard_scancode_ptr twice -- 0002d60b and 0002d61c -- because the
   second comparison re-fetches the pointer, and the tail test at 0002d636
   draws the catch-up frame on scancode 2 only while the in-loop suppression
   covers 2 and 3 alike. */
void fdps_animate_move_step_left(int unit_index)
{
    /* The moving unit's record inside the map unit array. */
    struct fdps_unit_record *unit;
    /* Where the unit's column sat, in pixels, before the step began. */
    int start_pixel_x;
    /* The animation sub-step being played, 1..6. */
    int walk_step;

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
           + unit_index;
    start_pixel_x = unit->pos_x * SCENE_TILE_SIZE;
    unit->facing = MAP_UNIT_FACING_LEFT;

    for (walk_step = 1; walk_step <= WALK_SUB_STEPS; walk_step++) {
        unit->walk_step = (unsigned char) walk_step;

        if (start_pixel_x - data_fdps_battle_view_window_origin_x
                < VIEW_SCROLL_LEFT_TRIGGER_X
            && data_fdps_battle_view_window_origin_x >= WALK_STEP_PIXELS) {
            data_fdps_battle_view_window_origin_x -= WALK_STEP_PIXELS;
        }
        data_fdps_map_cursor_world_x -= WALK_STEP_PIXELS;

        if (*fdps_keyboard_scancode_ptr() != SCANCODE_FAST_FORWARD
            && *fdps_keyboard_scancode_ptr() != SCANCODE_SKIP_ANIMATION) {
            fdps_render_view_frame();
        }
    }

    if (*fdps_keyboard_scancode_ptr() == SCANCODE_FAST_FORWARD) {
        fdps_render_view_frame();
    }

    unit->pos_x--;
    unit->walk_step = 0;
    fdps_map_set_pending_tile_event(
        data_fdps_map_cursor_world_x / SCENE_TILE_SIZE,
        data_fdps_map_cursor_world_y / SCENE_TILE_SIZE,
        TILE_EVENT_ON_ARRIVAL);
}

/* 0002d6a0.  One tile right across the screen.

   This is the DOWNWARD step turned through ninety degrees, not the leftward
   one: it is the pair of directions that walk towards the far edge of the map
   that need the map's own extent to stop the view, and this direction reads
   the terrain header for it exactly as the downward step does.  What changes
   from the downward step is the axis -- the tile COLUMN at record+0, the view
   origin data_fdps_battle_view_window_origin_x, the cursor column
   data_fdps_map_cursor_world_x and the terrain header's WIDTH at +7 rather
   than its height at +9 -- and the facing code.  Four things are behaviour
   rather than style.

   THE TWO PIXEL MEASUREMENTS ARE TAKEN ONCE, BEFORE THE LOOP.  The map's pixel
   width lands in [EBP-4] at 0002d6c7 and the unit's starting pixel column in
   [EBP-0xc] at 0002d6d7, and neither slot is written again.  The scroll test
   inside the loop therefore compares a FIXED starting column against a view
   origin that moves under it, and the gap closes by four pixels a pass until
   the test stops firing.  Recomputing the unit's drawn column each pass --
   which is what reaching for unit->pos_x inside the loop would do -- keeps the
   gap constant instead and scrolls a different number of times.

   BOTH SCROLL TESTS ARE SIGNED AND BOTH ARE STRICT.  The assembly spells them
   CMP EDX,0xf0 / JLE at 0002d70d and CMP EAX,[0x00069ce4] / JG at 0002d71d, so
   a gap of exactly 240 does not scroll and a view origin already sitting
   exactly on the right-hand limit does not move.  All three values are signed:
   a view origin left of the map origin has to stay negative rather than
   becoming a huge unsigned number that passes every test, and the map width is
   MOVSX so a header word of 0xffff is -1 tiles and not 65535.

   THE COLUMN WIDENS UNSIGNED.  MOV AL,byte ptr [EAX] then AND EAX,0xff at
   0002d6cd..0002d6cf: the tile column is a plain byte and column 200 is pixel
   4800, not -1344.

   THE ARRIVAL TILE COMES FROM THE MAP CURSOR, NOT FROM THE RECORD.  The two
   IDIVs at 0002d797 and 0002d7ad divide data_fdps_map_cursor_world_x and
   data_fdps_map_cursor_world_y by 24 after the loop has moved the cursor; the
   record's pos_y and the pos_x the INC at 0002d779 has just made current are
   not read.  The two agree only while the view is locked onto the moving unit.
   Both divisions are signed (SAR EDX,0x1f ahead of each IDIV).

   As in the other three directions the in-loop scancode test calls
   fdps_keyboard_scancode_ptr twice -- 0002d735 and 0002d746 -- because the
   second comparison re-fetches the pointer, and the tail test at 0002d760
   draws the catch-up frame on scancode 2 only while the in-loop suppression
   covers 2 and 3 alike. */
void fdps_animate_move_step_right(int unit_index)
{
    /* The moving unit's record inside the map unit array. */
    struct fdps_unit_record *unit;
    /* The whole map's width in pixels, from the terrain layer's header. */
    int map_width_px;
    /* Where the unit's column sat, in pixels, before the step began. */
    int start_pixel_x;
    /* The animation sub-step being played, 1..6. */
    int walk_step;

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
           + unit_index;
    map_width_px = (int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0]
                                     + SCENE_TILE_MAP_WIDTH_AT)
                   * SCENE_TILE_SIZE;
    start_pixel_x = unit->pos_x * SCENE_TILE_SIZE;
    unit->facing = MAP_UNIT_FACING_RIGHT;

    for (walk_step = 1; walk_step <= WALK_SUB_STEPS; walk_step++) {
        unit->walk_step = (unsigned char) walk_step;

        if (start_pixel_x - data_fdps_battle_view_window_origin_x
                > VIEW_SCROLL_TRIGGER_X
            && data_fdps_battle_view_window_origin_x
                < map_width_px - MAP_VIEW_WIDTH) {
            data_fdps_battle_view_window_origin_x += WALK_STEP_PIXELS;
        }
        data_fdps_map_cursor_world_x += WALK_STEP_PIXELS;

        if (*fdps_keyboard_scancode_ptr() != SCANCODE_FAST_FORWARD
            && *fdps_keyboard_scancode_ptr() != SCANCODE_SKIP_ANIMATION) {
            fdps_render_view_frame();
        }
    }

    if (*fdps_keyboard_scancode_ptr() == SCANCODE_FAST_FORWARD) {
        fdps_render_view_frame();
    }

    unit->pos_x++;
    unit->walk_step = 0;
    fdps_map_set_pending_tile_event(
        data_fdps_map_cursor_world_x / SCENE_TILE_SIZE,
        data_fdps_map_cursor_world_y / SCENE_TILE_SIZE,
        TILE_EVENT_ON_ARRIVAL);
}
