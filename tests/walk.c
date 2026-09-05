/* tests/walk.c -- cover for src/walk.c.
 *
 * Every expected value below is read off the assembly at 0002d360 -- MOVSX
 * word ptr [EAX+9] and IMUL EAX,EAX,0x18 for the map height, MOV AL,byte ptr
 * [EAX+1] / AND EAX,0xff / IMUL EAX,EAX,0x18 for the starting pixel row, MOV
 * byte ptr [EAX+3],0x0 for the facing, CMP dword ptr [EBP-0x8],0x7 / JL for
 * the six passes, CMP EDX,0x78 / JLE and CMP EAX,[0x00069ce0] / JG for the two
 * scroll tests, ADD dword ptr [0x00069ce0],0x4 and ADD dword ptr
 * [0x00069ccc],0x4 for the two advances, INC byte ptr [EAX+1] and MOV byte ptr
 * [EAX+4],0x0 for the commit, and the two SAR EDX,0x1f / IDIV EBX pairs at
 * 0002d448 and 0002d45e for the arrival tile -- and from the record layouts
 * ticket 17 settled.  None of them is read off the emitted C.
 *
 * The unit record and the four map layers are staged here rather than read
 * from a game file: the function takes its whole input from those pointers and
 * from three globals, so pointing them at local blocks is the only way to
 * reach the body.  Nothing below asserts what any global holds on its own;
 * ticket 23 owns that.
 *
 * THE SCANCODE LATCH IS HELD AT 3 THROUGHOUT.  fdps_render_view_frame is real
 * emitted code that spins on the VGA input status port and on the timer tick
 * counter, so a test that let it be called would never return.  Scancode 3 is
 * the value that suppresses the per-pass frame inside the loop AND leaves the
 * post-loop catch-up frame undrawn, which is the one latch value that takes
 * the function all the way through without a single call into the renderer.
 * That is why nothing below asserts anything about the frames drawn: the
 * difference between the in-loop test (2 or 3) and the tail test (2 only) is
 * settled by reading 0002d3ff..0002d42d and cannot be reached from here.
 *
 * The upward step at 0002d480 is covered by the second group of cases, and its
 * expected values come the same way: MOV AL,byte ptr [EAX+1] / AND EAX,0xff /
 * IMUL EAX,EAX,0x18 at 0002d49a for the starting pixel row, MOV byte ptr
 * [EAX+3],0x2 for the facing, CMP EDX,0x30 / JGE at 0002d4de and CMP dword ptr
 * [0x00069ce0],0x4 / JGE at 0002d4e3 for the two scroll tests, SUB dword ptr
 * [0x00069ce0],0x4 and SUB dword ptr [0x00069ccc],0x4 for the two retreats,
 * DEC byte ptr [EAX+1] and MOV byte ptr [EAX+4],0x0 for the commit, and the
 * SAR EDX,0x1f / IDIV EBX pairs at 0002d55c and 0002d572 for the arrival tile.
 * The latch stays at 3 for those cases too, and for the same reason.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "maptile.h"
#include "walk.h"

#define STAGE_MAP_WIDTH 12
#define STAGE_MAP_CELLS 240
#define STAGE_ATTR_ROWS 16

#define TERRAIN_WIDTH_AT  0x07
#define TERRAIN_HEIGHT_AT 0x09
#define TERRAIN_CELLS_AT  0x0b
#define ATTR_ROWS_AT      0x11
#define EVENT_WIDTH_AT    0x07
#define EVENT_CELLS_AT    0x10
#define EVENT_TABLE_AT    0x31

#define TILE 0x18

static struct fdps_unit_record stage_units[4];
static unsigned char stage_tile_map[TERRAIN_CELLS_AT + STAGE_MAP_CELLS * 2];
static unsigned char stage_attr[ATTR_ROWS_AT + STAGE_ATTR_ROWS * 4];
static unsigned char stage_grid[4 + STAGE_MAP_CELLS * 2];
static unsigned char stage_event[EVENT_CELLS_AT + STAGE_MAP_CELLS];
static unsigned char stage_event_table[EVENT_TABLE_AT + 64];

/* Put the map layers, the unit array and the three globals the step routine
   reads into a known state.

   Every terrain cell holds tile id 0 and attribute row 0 is left with flags 0,
   so the arrival cell always passes fdps_map_set_pending_tile_event's 0x60
   gate and the event code alone decides whether an event fires.  Every event
   cell is 0 -- the "no event" sentinel -- so unless a case writes one in, the
   arrival report is a no-op and data_fdps_chapter_pending_event_idx keeps the
   sentinel this function puts in it.

   map_tile_height goes into the terrain header at +9, which is the only header
   word the step routine reads; the width at +7 is there because
   fdps_map_load_tile_info indexes the arrival cell with it. */
static void stage(int map_tile_height, int unit_pos_y, int view_origin_y,
                  int cursor_tile_x, int cursor_tile_y)
{
    int i;

    for (i = 0; i < TERRAIN_CELLS_AT; i++) {
        stage_tile_map[i] = 0xaa;
    }
    *(short *) (stage_tile_map + TERRAIN_WIDTH_AT) = (short) STAGE_MAP_WIDTH;
    *(short *) (stage_tile_map + TERRAIN_HEIGHT_AT) = (short) map_tile_height;
    for (i = 0; i < STAGE_MAP_CELLS; i++) {
        *(short *) (stage_tile_map + TERRAIN_CELLS_AT + i * 2) = (short) 0;
    }

    for (i = 0; i < ATTR_ROWS_AT + STAGE_ATTR_ROWS * 4; i++) {
        stage_attr[i] = 0;
    }

    *(short *) stage_grid = (short) STAGE_MAP_WIDTH;
    *(short *) (stage_grid + 2) = (short) map_tile_height;
    for (i = 0; i < STAGE_MAP_CELLS * 2; i++) {
        stage_grid[4 + i] = 0;
    }

    for (i = 0; i < EVENT_CELLS_AT; i++) {
        stage_event[i] = 0xaa;
    }
    *(short *) (stage_event + EVENT_WIDTH_AT) = (short) STAGE_MAP_WIDTH;
    for (i = 0; i < STAGE_MAP_CELLS; i++) {
        stage_event[EVENT_CELLS_AT + i] = 0;
    }

    for (i = 0; i < EVENT_TABLE_AT + 64; i++) {
        stage_event_table[i] = 0xff;
    }

    for (i = 0; i < 4; i++) {
        stage_units[i].pos_x = (unsigned char) (0x20 + i);
        stage_units[i].pos_y = (unsigned char) (0x30 + i * 4);
        stage_units[i].facing = 0xee;
        stage_units[i].walk_step = 0xdd;
    }
    stage_units[1].pos_x = 1;
    stage_units[1].pos_y = (unsigned char) unit_pos_y;

    data_fdps_scene_layer_tile_map_ptrs[0] = stage_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = stage_attr;
    data_fdps_battle_move_grid_ptr = stage_grid;
    data_fdps_map_cell_event_code_layer_ptr = stage_event;
    data_fdps_tile_event_data_table_ptr = stage_event_table;
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;

    data_fdps_battle_view_window_origin_y = view_origin_y;
    data_fdps_map_cursor_world_x = cursor_tile_x * TILE;
    data_fdps_map_cursor_world_y = cursor_tile_y * TILE;
    data_fdps_chapter_pending_event_idx = 0x7777;
    data_fdps_input_last_scancode = 3;
}

/* The stride the C's pointer arithmetic has to land on, and the three record
   offsets the step routine touches: IMUL EAX,dword ptr [EBP+0x14],0x50 at
   0002d36c, [EAX+1] for the tile row, [EAX+3] for the facing and [EAX+4] for
   the sub-step counter. */
static void unit_record_offsets_are_what_the_step_reads(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, facing), 3);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, walk_step), 4);
}

/* One step, taken with plenty of map below and the unit well clear of the top
   of the view, so every one of the six passes scrolls.

   Unit 1 starts on tile row 7, so its starting pixel row is 7 * 24 = 168 and
   the gap over a view origin of 0 is 168, comfortably past the 120 the JLE at
   0002d3d1 wants beaten.  The map is 20 tiles tall, 480 pixels, so the bottom
   limit the JG at 0002d3e1 tests is 480 - 192 = 288 and the view never
   approaches it.  Six passes of ADD dword ptr [0x00069ce0],0x4 leave the view
   origin at 24, and six passes of ADD dword ptr [0x00069ccc],0x4 leave the
   cursor row 24 pixels lower than the 2 * 24 = 48 it started at.

   The facing byte is written 0 before the loop and the sub-step counter is
   cleared after it; the tile row is incremented once, and the column is not
   touched at all -- this routine moves down and nothing else. */
static void steps_one_tile_down_and_scrolls_every_pass(void)
{
    stage(20, 7, 0, 3, 2);
    fdps_walk_step_down(1);

    CHECK_EQ(stage_units[1].facing, 0);
    CHECK_EQ(stage_units[1].walk_step, 0);
    CHECK_EQ(stage_units[1].pos_y, 8);
    CHECK_EQ(stage_units[1].pos_x, 1);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 24);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE + 24);
    CHECK_EQ(data_fdps_map_cursor_world_x, 3 * TILE);
}

/* The index really is scaled by 0x50: unit 2 moves and its neighbours do not.

   Unit 2 keeps the stage's own pos_y of 0x38, so its starting pixel row is
   0x38 * 24 = 1344 and every pass scrolls; what the case is for is the records
   on either side of it keeping the sentinel values stage() wrote, which they
   only do if the record address really was base + 2 * 0x50.  The three staged
   rows are four apart so no neighbour's row can be mistaken for unit 2's
   incremented one. */
static void indexes_the_unit_array_by_record_stride(void)
{
    stage(64, 7, 0, 3, 2);
    fdps_walk_step_down(2);

    CHECK_EQ(stage_units[2].pos_y, 0x39);
    CHECK_EQ(stage_units[2].facing, 0);
    CHECK_EQ(stage_units[2].walk_step, 0);
    CHECK_EQ(stage_units[1].pos_y, 7);
    CHECK_EQ(stage_units[1].facing, 0xee);
    CHECK_EQ(stage_units[1].walk_step, 0xdd);
    CHECK_EQ(stage_units[3].pos_y, 0x3c);
    CHECK_EQ(stage_units[3].facing, 0xee);
}

/* A gap of exactly 120 pixels does not scroll.

   Tile row 5 is pixel row 120, and with the view origin at 0 the subtraction
   at 0002d3c8 gives exactly 0x78, which JLE sends round the scroll.  Because
   the starting row is measured once before the loop and the view origin never
   moves, the same test fails all six times and the view is still at 0 when the
   step ends.  The cursor row advances regardless -- its ADD is outside the
   scroll test -- which is what separates "the scroll was skipped" from "the
   loop did not run". */
static void does_not_scroll_at_exactly_the_trigger_gap(void)
{
    stage(20, 5, 0, 3, 2);
    fdps_walk_step_down(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE + 24);
    CHECK_EQ(stage_units[1].pos_y, 6);
}

/* The scroll stops partway through the loop, because the gap closes under it.

   Tile row 6 is pixel row 144 and the view starts at 8, so the gap is 136.
   Each pass that scrolls costs the gap four pixels, and the test wants it
   strictly greater than 120: passes one to four scroll, taking the view origin
   8 -> 12 -> 16 -> 20 -> 24 and the gap 136 -> 120, and passes five and six
   find exactly 120 and leave it alone.  Four scrolls out of six is the whole
   point -- a routine that remeasured the unit's row against the moving view,
   or that used >= instead of >, would come out at 32. */
static void stops_scrolling_when_the_gap_closes_mid_loop(void)
{
    stage(20, 6, 8, 3, 2);
    fdps_walk_step_down(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 24);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE + 24);
}

/* The view stops at the bottom of the map even though the unit keeps walking.

   The map is 10 tiles tall, 240 pixels, so the last view origin with map still
   under it is 240 - 192 = 48.  The unit sits on tile row 15, pixel row 360, so
   the first test passes every pass and only the bottom limit can stop the
   scroll.  From 40 the view takes two steps to 48 and then stays: CMP
   EAX,[0x00069ce0] / JG at 0002d3db wants the limit strictly greater than the
   origin, so an origin sitting exactly on 48 does not move again. */
static void stops_scrolling_at_the_bottom_of_the_map(void)
{
    stage(10, 15, 40, 3, 2);
    fdps_walk_step_down(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 48);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE + 24);
    CHECK_EQ(stage_units[1].pos_y, 16);
}

/* The arrival tile handed to fdps_map_set_pending_tile_event comes from the
   map cursor and from its POST-loop value, not from the unit record.

   Three event cells are armed, each with its own handler index in the event
   data table at code * 2 + 0x31:

     the cell the cursor ends on, (3, 3) -- cursor x 3 * 24 and cursor y
       2 * 24 + 24 = 72, and 72 / 24 = 3 -- carries code 5, handler 0x11;
     the cell the cursor started on, (3, 2), carries code 7, handler 0x33, so
       reporting the departure tile instead of the arrival tile is visible;
     the cell the unit record now names, (1, 8) -- pos_x 1 and the pos_y the
       INC at 0002d437 has just made 8 -- carries code 9, handler 0x22, so
       substituting the record for the cursor is visible too.

   Each entry's trigger byte at code * 2 + 0x32 is 0, the occasion this call
   site reports, so all three would fire if they were reached.  0x11 is the one
   that must come back. */
static void reports_the_arrival_tile_from_the_map_cursor(void)
{
    stage(20, 7, 0, 3, 2);

    stage_event[EVENT_CELLS_AT + 3 * STAGE_MAP_WIDTH + 3] = 5;
    stage_event[EVENT_CELLS_AT + 2 * STAGE_MAP_WIDTH + 3] = 7;
    stage_event[EVENT_CELLS_AT + 8 * STAGE_MAP_WIDTH + 1] = 9;
    stage_event_table[EVENT_TABLE_AT + 5 * 2] = 0x11;
    stage_event_table[EVENT_TABLE_AT + 5 * 2 + 1] = 0;
    stage_event_table[EVENT_TABLE_AT + 7 * 2] = 0x33;
    stage_event_table[EVENT_TABLE_AT + 7 * 2 + 1] = 0;
    stage_event_table[EVENT_TABLE_AT + 9 * 2] = 0x22;
    stage_event_table[EVENT_TABLE_AT + 9 * 2 + 1] = 0;

    fdps_walk_step_down(1);

    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x11);
}

/* A cursor left of and above the map origin divides towards zero, not into a
   huge unsigned tile number.

   SAR EDX,0x1f before each IDIV makes both divisions signed, so a cursor x of
   -12 comes out as tile 0 and a cursor y that is -24 before the loop is -24 +
   24 = 0 after it, tile 0 as well.  Cell (0, 0) is armed with code 4, handler
   0x44, so the assertion says the pair of divisions landed there; read
   unsigned, -12 / 24 is 178956970 and the cell reached would be far outside
   the staged block. */
static void divides_the_cursor_signed(void)
{
    stage(20, 7, 0, 0, 0);
    data_fdps_map_cursor_world_x = -12;
    data_fdps_map_cursor_world_y = -24;

    stage_event[EVENT_CELLS_AT + 0] = 4;
    stage_event_table[EVENT_TABLE_AT + 4 * 2] = 0x44;
    stage_event_table[EVENT_TABLE_AT + 4 * 2 + 1] = 0;

    fdps_walk_step_down(1);

    CHECK_EQ(data_fdps_map_cursor_world_y, 0);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x44);
}

/* The map height is read MOVSX from +9 of the terrain header, so a header word
   of 0xffff is -1 tiles and not 65535.

   With height -1 the bottom limit is -1 * 24 - 192 = -216, which no view
   origin of 0 can be below, so the second test fails every pass and the view
   never scrolls however far below the top of the view the unit is.  Read
   unsigned the limit would be 1572672 and all six passes would scroll. */
static void reads_the_map_height_signed(void)
{
    stage(20, 30, 0, 3, 2);
    *(short *) (stage_tile_map + TERRAIN_HEIGHT_AT) = (short) -1;

    fdps_walk_step_down(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE + 24);
}

/* The unit's tile row widens UNSIGNED into the pixel measurement: AND
   EAX,0xff at 0002d390 after the byte load.

   Tile row 200 is pixel row 4800, a gap of 4800 over a view origin of 0, so
   every pass scrolls and the view ends at 24.  Read through a signed char the
   row would be -56, pixel row -1344, and the gap of -1344 would fail the first
   test on every pass and leave the view at 0.  The map is made 400 tiles tall
   so the bottom limit is nowhere near. */
static void widens_the_unit_row_unsigned(void)
{
    stage(400, 200, 0, 3, 2);
    fdps_walk_step_down(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 24);
    CHECK_EQ(stage_units[1].pos_y, 201);
}

/* One step up, with the unit close enough to the top of the view that every
   one of the six passes scrolls.

   Unit 1 starts on tile row 1, pixel row 24, and the view origin starts at 40,
   so the gap is -16: far below the 48 the JGE at 0002d4e1 needs to skip the
   scroll, and it only grows to 8 by the last pass.  The view origin stays at
   or above 4 the whole way -- 40, 36, 32, 28, 24, 20 going in -- so the second
   test never stops it either, and six passes of SUB dword ptr
   [0x00069ce0],0x4 leave it at 16.  Six passes of SUB dword ptr
   [0x00069ccc],0x4 take the cursor row from 2 * 24 = 48 to 24.

   The facing byte is written 2 before the loop -- MOV byte ptr [EAX+3],0x2 --
   the sub-step counter is cleared after it, the tile row is decremented once,
   and the column is not touched at all. */
static void steps_one_tile_up_and_scrolls_every_pass(void)
{
    stage(20, 1, 40, 3, 2);
    fdps_animate_move_step_up(1);

    CHECK_EQ(stage_units[1].facing, 2);
    CHECK_EQ(stage_units[1].walk_step, 0);
    CHECK_EQ(stage_units[1].pos_y, 0);
    CHECK_EQ(stage_units[1].pos_x, 1);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 16);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE - 24);
    CHECK_EQ(data_fdps_map_cursor_world_x, 3 * TILE);
}

/* The index really is scaled by 0x50 here too: unit 2 moves and its neighbours
   do not.

   Unit 2 keeps the stage's own pos_y of 0x38, so its starting pixel row is
   1344, the first scroll test fails on every pass and the view never moves --
   which is beside the point of this case.  What it is for is the records on
   either side keeping the sentinel values stage() wrote, which they only do if
   the record address really was base + 2 * 0x50. */
static void indexes_the_unit_array_by_record_stride_up(void)
{
    stage(20, 7, 0, 3, 2);
    fdps_animate_move_step_up(2);

    CHECK_EQ(stage_units[2].pos_y, 0x37);
    CHECK_EQ(stage_units[2].facing, 2);
    CHECK_EQ(stage_units[2].walk_step, 0);
    CHECK_EQ(stage_units[1].pos_y, 7);
    CHECK_EQ(stage_units[1].facing, 0xee);
    CHECK_EQ(stage_units[1].walk_step, 0xdd);
    CHECK_EQ(stage_units[3].pos_y, 0x3c);
    CHECK_EQ(stage_units[3].facing, 0xee);
}

/* A gap of exactly 48 pixels does not scroll.

   Tile row 4 is pixel row 96 and the view starts at 48, so the subtraction at
   0002d4d8 gives exactly 0x30 and JGE sends every pass round the scroll.  The
   view origin is well clear of the >= 4 limit, so the first test is the only
   thing that can be stopping it, and because the starting row is measured once
   and the view never moves the same test fails all six times.  The cursor row
   retreats regardless -- its SUB is outside the scroll test -- which is what
   separates "the scroll was skipped" from "the loop did not run". */
static void does_not_scroll_at_exactly_the_trigger_gap_up(void)
{
    stage(20, 4, 48, 3, 2);
    fdps_animate_move_step_up(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 48);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE - 24);
    CHECK_EQ(stage_units[1].pos_y, 3);
}

/* The scroll stops partway through the loop, because the gap OPENS under it.

   Tile row 4 is pixel row 96 and the view starts at 60, so the gap is 36.
   Every pass that scrolls costs the view four pixels and therefore widens the
   gap by four, and the test wants it strictly under 48: passes one to three
   scroll, taking the view origin 60 -> 56 -> 52 -> 48 and the gap 36 -> 48,
   and passes four to six find exactly 48 and leave it alone.  Three scrolls
   out of six is the whole point -- a routine that remeasured the unit's row
   against the moving view would never widen the gap at all and would scroll
   all six times, ending at 36. */
static void stops_scrolling_when_the_gap_opens_mid_loop(void)
{
    stage(20, 4, 60, 3, 2);
    fdps_animate_move_step_up(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 48);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE - 24);
}

/* The view stops at the top of the map even though the unit keeps walking.

   Tile row 1 is pixel row 24 and the view starts at 12, so the gap runs 12,
   16, 20, 24 and never reaches 48: the first test passes on every one of the
   six passes and only the >= 4 limit can stop the scroll.  From 12 the view
   takes three steps to 0 -- the third is taken with the origin sitting exactly
   on 4, which JGE at 0002d4ea allows -- and then stays, because 0 is below the
   limit.  Three scrolls out of six, and an origin of 0 rather than -12, is
   what says the limit is >= 4 and not > 4 or a bare test against zero. */
static void stops_scrolling_at_the_top_of_the_map(void)
{
    stage(20, 1, 12, 3, 2);
    fdps_animate_move_step_up(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE - 24);
    CHECK_EQ(stage_units[1].pos_y, 0);
}

/* The arrival tile handed to fdps_map_set_pending_tile_event comes from the
   map cursor and from its POST-loop value, not from the unit record.

   Three event cells are armed, each with its own handler index in the event
   data table at code * 2 + 0x31:

     the cell the cursor ends on, (3, 1) -- cursor x 3 * 24 and cursor y
       2 * 24 - 24 = 24, and 24 / 24 = 1 -- carries code 5, handler 0x11;
     the cell the cursor started on, (3, 2), carries code 7, handler 0x33, so
       reporting the departure tile instead of the arrival tile is visible;
     the cell the unit record now names, (1, 6) -- pos_x 1 and the pos_y the
       DEC at 0002d540 has just made 6 -- carries code 9, handler 0x22, so
       substituting the record for the cursor is visible too.

   Each entry's trigger byte at code * 2 + 0x32 is 0, the occasion this call
   site reports, so all three would fire if they were reached.  0x11 is the one
   that must come back. */
static void reports_the_arrival_tile_from_the_map_cursor_up(void)
{
    stage(20, 7, 0, 3, 2);

    stage_event[EVENT_CELLS_AT + 1 * STAGE_MAP_WIDTH + 3] = 5;
    stage_event[EVENT_CELLS_AT + 2 * STAGE_MAP_WIDTH + 3] = 7;
    stage_event[EVENT_CELLS_AT + 6 * STAGE_MAP_WIDTH + 1] = 9;
    stage_event_table[EVENT_TABLE_AT + 5 * 2] = 0x11;
    stage_event_table[EVENT_TABLE_AT + 5 * 2 + 1] = 0;
    stage_event_table[EVENT_TABLE_AT + 7 * 2] = 0x33;
    stage_event_table[EVENT_TABLE_AT + 7 * 2 + 1] = 0;
    stage_event_table[EVENT_TABLE_AT + 9 * 2] = 0x22;
    stage_event_table[EVENT_TABLE_AT + 9 * 2 + 1] = 0;

    fdps_animate_move_step_up(1);

    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x11);
}

/* A cursor left of the map origin divides towards zero, not into a huge
   unsigned tile number.

   SAR EDX,0x1f before each IDIV makes both divisions signed, so a cursor x of
   -12 comes out as tile 0; the cursor y set to 24 before the loop is 24 - 24 =
   0 after it, tile 0 as well.  Cell (0, 0) is armed with code 4, handler 0x44,
   so the assertion says the pair of divisions landed there; read unsigned,
   -12 / 24 is 178956970 and the cell reached would be far outside the staged
   block. */
static void divides_the_cursor_signed_up(void)
{
    stage(20, 7, 0, 0, 0);
    data_fdps_map_cursor_world_x = -12;
    data_fdps_map_cursor_world_y = 24;

    stage_event[EVENT_CELLS_AT + 0] = 4;
    stage_event_table[EVENT_TABLE_AT + 4 * 2] = 0x44;
    stage_event_table[EVENT_TABLE_AT + 4 * 2 + 1] = 0;

    fdps_animate_move_step_up(1);

    CHECK_EQ(data_fdps_map_cursor_world_y, 0);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x44);
}

/* The unit's tile row widens UNSIGNED into the pixel measurement here as well:
   AND EAX,0xff at 0002d4a0 after the byte load.

   Tile row 200 is pixel row 4800, a gap of 4760 over a view origin of 40, so
   the first test fails on every pass and the view is still at 40 when the step
   ends.  Read through a signed char the row would be -56, pixel row -1344, and
   a gap of -1384 would pass the first test every pass while the origin stayed
   above 4, scrolling all six times down to 16. */
static void widens_the_unit_row_unsigned_up(void)
{
    stage(20, 200, 40, 3, 2);
    fdps_animate_move_step_up(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_y, 40);
    CHECK_EQ(stage_units[1].pos_y, 199);
}

/* The leftward step at 0002d590 works on the other axis, so its cases need the
   unit's tile COLUMN and the horizontal view origin staged as well.  stage()
   leaves unit 1 on tile row 7 with the vertical view origin at 0 and neither is
   touched by this direction, so the two axes cannot be confused: anything that
   moved on the y axis is a defect and case one asserts as much.

   Expected values below come from 0002d590 -- MOV AL,byte ptr [EAX] / AND
   EAX,0xff / IMUL EAX,EAX,0x18 at 0002d5ad for the starting pixel column, MOV
   byte ptr [EAX+3],0x1 at 0002d5bd for the facing, CMP EDX,0x30 / JGE at
   0002d5ed and CMP dword ptr [0x00069ce4],0x4 / JGE at 0002d5f2 for the two
   scroll tests, SUB dword ptr [0x00069ce4],0x4 and SUB dword ptr
   [0x00069cd4],0x4 for the two retreats, DEC byte ptr [EAX] at 0002d64f and
   MOV byte ptr [EAX+4],0x0 for the commit, and the SAR EDX,0x1f / IDIV EBX
   pairs at 0002d66a and 0002d680 for the arrival tile.  The latch stays at 3
   for these cases too, and for the same reason as the others. */
static void stage_left(int unit_pos_x, int view_origin_x,
                       int cursor_tile_x, int cursor_tile_y)
{
    stage(20, 7, 0, cursor_tile_x, cursor_tile_y);
    stage_units[1].pos_x = (unsigned char) unit_pos_x;
    data_fdps_battle_view_window_origin_x = view_origin_x;
}

/* The tile column the step reads and decrements is the record's first byte:
   MOV AL,byte ptr [EAX] at 0002d5ad and DEC byte ptr [EAX] at 0002d64f both
   address the record with no displacement at all. */
static void unit_record_column_offset_is_zero(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
}

/* One step left, with the unit close enough to the left edge of the view that
   every one of the six passes scrolls.

   Unit 1 starts on tile column 1, pixel column 24, and the horizontal view
   origin starts at 40, so the gap is -16: far below the 48 the JGE at 0002d5f0
   needs to skip the scroll, and it only grows to 4 by the last pass.  The view
   origin stays at or above 4 the whole way -- 40, 36, 32, 28, 24, 20 going in
   -- so the second test never stops it either, and six passes of SUB dword ptr
   [0x00069ce4],0x4 leave it at 16.  Six passes of SUB dword ptr
   [0x00069cd4],0x4 take the cursor column from 3 * 24 = 72 to 48.

   The facing byte is written 1 before the loop -- MOV byte ptr [EAX+3],0x1 --
   the sub-step counter is cleared after it, the tile column is decremented
   once, and nothing on the y axis moves: the tile row, the vertical view origin
   and the cursor row all keep the values stage() gave them. */
static void steps_one_tile_left_and_scrolls_every_pass(void)
{
    stage_left(1, 40, 3, 2);
    fdps_animate_move_step_left(1);

    CHECK_EQ(stage_units[1].facing, 1);
    CHECK_EQ(stage_units[1].walk_step, 0);
    CHECK_EQ(stage_units[1].pos_x, 0);
    CHECK_EQ(stage_units[1].pos_y, 7);
    CHECK_EQ(data_fdps_battle_view_window_origin_x, 16);
    CHECK_EQ(data_fdps_map_cursor_world_x, 3 * TILE - 24);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y, 2 * TILE);
}

/* The index really is scaled by 0x50 here too: unit 2 moves and its neighbours
   do not.

   Unit 2 keeps the stage's own pos_x of 0x22, so its starting pixel column is
   0x22 * 24 = 816, the first scroll test fails on every pass and the view never
   moves -- which is beside the point of this case.  What it is for is the
   records on either side keeping the values stage() wrote, which they only do
   if the record address really was base + 2 * 0x50.  Unit 2's decremented
   column, 0x21, matches no neighbour's. */
static void indexes_the_unit_array_by_record_stride_left(void)
{
    stage_left(1, 0, 3, 2);
    fdps_animate_move_step_left(2);

    CHECK_EQ(stage_units[2].pos_x, 0x21);
    CHECK_EQ(stage_units[2].facing, 1);
    CHECK_EQ(stage_units[2].walk_step, 0);
    CHECK_EQ(stage_units[1].pos_x, 1);
    CHECK_EQ(stage_units[1].facing, 0xee);
    CHECK_EQ(stage_units[1].walk_step, 0xdd);
    CHECK_EQ(stage_units[3].pos_x, 0x23);
    CHECK_EQ(stage_units[3].facing, 0xee);
}

/* A gap of exactly 48 pixels does not scroll.

   Tile column 4 is pixel column 96 and the view starts at 48, so the
   subtraction at 0002d5e7 gives exactly 0x30 and JGE sends every pass round the
   scroll.  The view origin is well clear of the >= 4 limit, so the first test is
   the only thing that can be stopping it, and because the starting column is
   measured once and the view never moves the same test fails all six times.
   The cursor column retreats regardless -- its SUB is outside the scroll test
   -- which is what separates "the scroll was skipped" from "the loop did not
   run". */
static void does_not_scroll_at_exactly_the_trigger_gap_left(void)
{
    stage_left(4, 48, 3, 2);
    fdps_animate_move_step_left(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 48);
    CHECK_EQ(data_fdps_map_cursor_world_x, 3 * TILE - 24);
    CHECK_EQ(stage_units[1].pos_x, 3);
}

/* The scroll stops partway through the loop, because the gap OPENS under it.

   Tile column 4 is pixel column 96 and the view starts at 60, so the gap is 36.
   Every pass that scrolls costs the view four pixels and therefore widens the
   gap by four, and the test wants it strictly under 48: passes one to three
   scroll, taking the view origin 60 -> 56 -> 52 -> 48 and the gap 36 -> 48, and
   passes four to six find exactly 48 and leave it alone.  Three scrolls out of
   six is the whole point -- a routine that remeasured the unit's column against
   the moving view would never widen the gap at all and would scroll all six
   times, ending at 36. */
static void stops_scrolling_when_the_gap_opens_mid_loop_left(void)
{
    stage_left(4, 60, 3, 2);
    fdps_animate_move_step_left(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 48);
    CHECK_EQ(data_fdps_map_cursor_world_x, 3 * TILE - 24);
}

/* The view stops at the left edge of the map even though the unit keeps
   walking.

   Tile column 1 is pixel column 24 and the view starts at 12, so the gap runs
   12, 16, 20, 24 and never reaches 48: the first test passes on every one of
   the six passes and only the >= 4 limit can stop the scroll.  From 12 the view
   takes three steps to 0 -- the third is taken with the origin sitting exactly
   on 4, which JGE at 0002d5f9 allows -- and then stays, because 0 is below the
   limit.  Three scrolls out of six, and an origin of 0 rather than -12, is what
   says the limit is >= 4 and not > 4 or a bare test against zero. */
static void stops_scrolling_at_the_left_edge_of_the_map(void)
{
    stage_left(1, 12, 3, 2);
    fdps_animate_move_step_left(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 0);
    CHECK_EQ(data_fdps_map_cursor_world_x, 3 * TILE - 24);
    CHECK_EQ(stage_units[1].pos_x, 0);
}

/* The arrival tile handed to fdps_map_set_pending_tile_event comes from the map
   cursor and from its POST-loop value, not from the unit record.

   Three event cells are armed, each with its own handler index in the event
   data table at code * 2 + 0x31:

     the cell the cursor ends on, (2, 2) -- cursor x 3 * 24 - 24 = 48 and
       48 / 24 = 2, cursor y 2 * 24 -- carries code 5, handler 0x11;
     the cell the cursor started on, (3, 2), carries code 7, handler 0x33, so
       reporting the departure tile instead of the arrival tile is visible;
     the cell the unit record now names, (6, 7) -- the pos_x the DEC at 0002d64f
       has just made 6, and pos_y 7 -- carries code 9, handler 0x22, so
       substituting the record for the cursor is visible too.

   Each entry's trigger byte at code * 2 + 0x32 is 0, the occasion this call
   site reports, so all three would fire if they were reached.  0x11 is the one
   that must come back.  The unit starts on column 7, pixel 168, so no pass
   scrolls and the cursor is the only thing moving. */
static void reports_the_arrival_tile_from_the_map_cursor_left(void)
{
    stage_left(7, 0, 3, 2);

    stage_event[EVENT_CELLS_AT + 2 * STAGE_MAP_WIDTH + 2] = 5;
    stage_event[EVENT_CELLS_AT + 2 * STAGE_MAP_WIDTH + 3] = 7;
    stage_event[EVENT_CELLS_AT + 7 * STAGE_MAP_WIDTH + 6] = 9;
    stage_event_table[EVENT_TABLE_AT + 5 * 2] = 0x11;
    stage_event_table[EVENT_TABLE_AT + 5 * 2 + 1] = 0;
    stage_event_table[EVENT_TABLE_AT + 7 * 2] = 0x33;
    stage_event_table[EVENT_TABLE_AT + 7 * 2 + 1] = 0;
    stage_event_table[EVENT_TABLE_AT + 9 * 2] = 0x22;
    stage_event_table[EVENT_TABLE_AT + 9 * 2 + 1] = 0;

    fdps_animate_move_step_left(1);

    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x11);
}

/* A cursor column left of the map origin divides towards zero, not into a huge
   unsigned tile number.

   SAR EDX,0x1f before the IDIV at 0002d683 makes the column division signed, so
   a cursor x that the loop's six retreats take from 12 down to 12 - 24 = -12
   comes out as tile 0; the cursor y is left at 0, tile 0 as well.  Cell (0, 0)
   is armed with code 4, handler 0x44, so the assertion says the pair of
   divisions landed there; read unsigned, -12 / 24 is 178956970 and the cell
   reached would be far outside the staged block. */
static void divides_the_cursor_signed_left(void)
{
    stage_left(7, 0, 0, 0);
    data_fdps_map_cursor_world_x = 12;

    stage_event[EVENT_CELLS_AT + 0] = 4;
    stage_event_table[EVENT_TABLE_AT + 4 * 2] = 0x44;
    stage_event_table[EVENT_TABLE_AT + 4 * 2 + 1] = 0;

    fdps_animate_move_step_left(1);

    CHECK_EQ(data_fdps_map_cursor_world_x, -12);
    CHECK_EQ(data_fdps_chapter_pending_event_idx, 0x44);
}

/* The unit's tile column widens UNSIGNED into the pixel measurement: AND
   EAX,0xff at 0002d5af after the byte load.

   Tile column 200 is pixel column 4800, a gap of 4760 over a view origin of 40,
   so the first test fails on every pass and the view is still at 40 when the
   step ends.  Read through a signed char the column would be -56, pixel column
   -1344, and a gap of -1384 would pass the first test every pass while the
   origin stayed above 4, scrolling all six times down to 16. */
static void widens_the_unit_column_unsigned_left(void)
{
    stage_left(200, 40, 3, 2);
    fdps_animate_move_step_left(1);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 40);
    CHECK_EQ(stage_units[1].pos_x, 199);
}

void run_walk_tests(void)
{
    RUN_TEST(unit_record_offsets_are_what_the_step_reads);
    RUN_TEST(steps_one_tile_down_and_scrolls_every_pass);
    RUN_TEST(indexes_the_unit_array_by_record_stride);
    RUN_TEST(does_not_scroll_at_exactly_the_trigger_gap);
    RUN_TEST(stops_scrolling_when_the_gap_closes_mid_loop);
    RUN_TEST(stops_scrolling_at_the_bottom_of_the_map);
    RUN_TEST(reports_the_arrival_tile_from_the_map_cursor);
    RUN_TEST(divides_the_cursor_signed);
    RUN_TEST(reads_the_map_height_signed);
    RUN_TEST(widens_the_unit_row_unsigned);
    RUN_TEST(steps_one_tile_up_and_scrolls_every_pass);
    RUN_TEST(indexes_the_unit_array_by_record_stride_up);
    RUN_TEST(does_not_scroll_at_exactly_the_trigger_gap_up);
    RUN_TEST(stops_scrolling_when_the_gap_opens_mid_loop);
    RUN_TEST(stops_scrolling_at_the_top_of_the_map);
    RUN_TEST(reports_the_arrival_tile_from_the_map_cursor_up);
    RUN_TEST(divides_the_cursor_signed_up);
    RUN_TEST(widens_the_unit_row_unsigned_up);
    RUN_TEST(unit_record_column_offset_is_zero);
    RUN_TEST(steps_one_tile_left_and_scrolls_every_pass);
    RUN_TEST(indexes_the_unit_array_by_record_stride_left);
    RUN_TEST(does_not_scroll_at_exactly_the_trigger_gap_left);
    RUN_TEST(stops_scrolling_when_the_gap_opens_mid_loop_left);
    RUN_TEST(stops_scrolling_at_the_left_edge_of_the_map);
    RUN_TEST(reports_the_arrival_tile_from_the_map_cursor_left);
    RUN_TEST(divides_the_cursor_signed_left);
    RUN_TEST(widens_the_unit_column_unsigned_left);
}
