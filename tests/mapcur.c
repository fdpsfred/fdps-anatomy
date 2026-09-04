/* tests/mapcur.c -- cover for src/mapcur.c.
 *
 * Every expected value below is worked out from the assembly of
 * fdps_draw_map_cursor at 0002c6a0 by hand -- the six CMP dword ptr
 * [0x00069cd0] tests at 0002c6ac, 0002c6d4, 0002c6fc, 0002c79c, 0002c930 and
 * 0002cbc1, and the push group ahead of every CALL 0x0002cc20, which fixes
 * that cell's displacement and its sprite index -- and none of them is read
 * off the emitted C.
 *
 * The drawer is exercised against the real fdps_blit_cursor_tile and the real
 * blit kernels rather than a stand-in, because what it decides -- which sprite
 * goes on which tile -- is only visible in the pixels those leave behind.  The
 * sprite sheet is staged as a byte buffer, exactly as tests/sprite.c stages
 * one, because the drawer reaches it through a global that
 * fdps_load_global_resources fills and because a sheet giving every piece its
 * own flat colour is what lets a painted tile name the sprite that painted it:
 * sprite index i fills its whole 24 by 24 block with the palette index
 * 0x40 + i.
 *
 * The scene buffer is the real 360 by 240 with one row of slack, filled with a
 * sentinel first, so a byte still holding the sentinel was never written and a
 * blank tile can be asserted as firmly as a painted one.
 *
 * Nothing here asserts what any global holds on its own -- ticket 23 owns
 * their contents -- and every global the drawer reads is put back afterwards.
 */
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "mapcur.h"

#define MAPCUR_PITCH 0x168
#define MAPCUR_SCENE_ROWS 0xf0
#define MAPCUR_SCENE_BYTES (MAPCUR_PITCH * (MAPCUR_SCENE_ROWS + 1))
#define MAPCUR_TILE 24
#define MAPCUR_BORDER 24
#define MAPCUR_TILE_BYTES (MAPCUR_TILE * MAPCUR_TILE)
#define MAPCUR_GUARD 0x5a

/* The .CEL offset table starts straight after the 15-byte header and every
   entry is measured from the start of the file (resource_info/cel.md), which
   is what fdps_blit_cursor_tile reads.  Eighteen entries cover sprite 0
   through 0x11, the highest index any arm of the drawer names. */
#define MAPCUR_SPRITE_COUNT 18
#define MAPCUR_TABLE_AT 0x0f
#define MAPCUR_STREAM0_AT 0x60
#define MAPCUR_STREAM_STRIDE 0x40
#define MAPCUR_SHEET_SIZE 0x500
#define MAPCUR_FILL_RUN_24 0x17
#define MAPCUR_PIXEL_BASE 0x40

/* A cursor position well inside the 312 by 192 visible window, and far enough
   from every edge that all 21 cells of the widest shape land inside it: the
   mode 5 arm reaches three tiles, 72 pixels, on each axis. */
#define MAPCUR_CURSOR_X 144
#define MAPCUR_CURSOR_Y 96

/* A movement grid big enough to hold the cursor tile above, staged inside a
   larger buffer whose lead-in and trail bytes are poisoned: a drawer that
   floored its signed division instead of truncating it would form a negative
   cell index and land in the lead-in. */
#define MAPCUR_GRID_W 10
#define MAPCUR_GRID_H 8
#define MAPCUR_GRID_LEAD 64
#define MAPCUR_GRID_TRAIL 64
#define MAPCUR_GRID_CELLS (MAPCUR_GRID_W * MAPCUR_GRID_H)
#define MAPCUR_GRID_BYTES \
    (MAPCUR_GRID_LEAD + 4 + MAPCUR_GRID_CELLS * 2 + MAPCUR_GRID_TRAIL)
#define MAPCUR_GRID_POISON 0xa5
#define MAPCUR_CELL_FLAGS 0x40
#define MAPCUR_CELL_MARKER 0xff

static unsigned char mapcur_sheet[MAPCUR_SHEET_SIZE];
static unsigned char mapcur_scene[MAPCUR_SCENE_BYTES];
static unsigned char mapcur_grid[MAPCUR_GRID_BYTES];

static void mapcur_u16(unsigned char *buffer, int at, unsigned long value)
{
    buffer[at] = (unsigned char) (value & 0xff);
    buffer[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void mapcur_u32(unsigned char *buffer, int at, unsigned long value)
{
    buffer[at] = (unsigned char) (value & 0xff);
    buffer[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    buffer[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    buffer[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* 24 rows of one 24-pixel fill: a whole tile in the size fdps_blit_cursor_tile
   hardwires, and the plainest stream that covers every column of every row. */
static void mapcur_stage_sheet(void)
{
    int sprite_index;
    int row;
    int stream_at;

    memset(mapcur_sheet, 0, MAPCUR_SHEET_SIZE);
    mapcur_sheet[0] = 'C';
    mapcur_sheet[1] = 'E';
    mapcur_sheet[2] = 'L';

    for (sprite_index = 0; sprite_index < MAPCUR_SPRITE_COUNT;
         sprite_index++) {
        stream_at = MAPCUR_STREAM0_AT + sprite_index * MAPCUR_STREAM_STRIDE;
        mapcur_u32(mapcur_sheet, MAPCUR_TABLE_AT + sprite_index * 4,
                   (unsigned long) stream_at);
        for (row = 0; row < MAPCUR_TILE; row++) {
            mapcur_sheet[stream_at + row * 2] = MAPCUR_FILL_RUN_24;
            mapcur_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (MAPCUR_PIXEL_BASE + sprite_index);
        }
    }
}

/* Every cell reachable, every zone bit set, so a cleared marker is the only
   change the drawer can possibly have made and the flags byte beside it says
   whether it clobbered its neighbour in the cell. */
static void mapcur_stage_grid(void)
{
    unsigned char *grid_base;
    int cell_index;

    memset(mapcur_grid, MAPCUR_GRID_POISON, MAPCUR_GRID_BYTES);
    grid_base = mapcur_grid + MAPCUR_GRID_LEAD;
    mapcur_u16(grid_base, 0, MAPCUR_GRID_W);
    mapcur_u16(grid_base, 2, MAPCUR_GRID_H);
    for (cell_index = 0; cell_index < MAPCUR_GRID_CELLS; cell_index++) {
        grid_base[4 + cell_index * 2] = MAPCUR_CELL_FLAGS;
        grid_base[4 + cell_index * 2 + 1] = MAPCUR_CELL_MARKER;
    }
}

static unsigned char mapcur_cell_byte(int cell_index, int which_byte)
{
    return mapcur_grid[MAPCUR_GRID_LEAD + 4 + cell_index * 2 + which_byte];
}

/* How many bytes of the poisoned lead-in have been written.  A signed division
   that floored, or an unsigned one, sends the cell index out of the array; the
   lead-in is where the near miss lands. */
static int mapcur_lead_in_touched(void)
{
    int index;
    int touched;

    touched = 0;
    for (index = 0; index < MAPCUR_GRID_LEAD; index++) {
        if (mapcur_grid[index] != MAPCUR_GRID_POISON) {
            touched++;
        }
    }
    return touched;
}

/* Both cursor globals, the mode, the view origin, the sheet and the grid go in
   and come back out again, so the drawer's dependencies do not leak into
   whatever test file runs next. */
static void mapcur_draw(int mode, int cursor_x, int cursor_y,
                        int origin_x, int origin_y)
{
    unsigned char *saved_sheet;
    unsigned char *saved_grid;
    int saved_mode;
    int saved_cursor_x;
    int saved_cursor_y;
    int saved_origin_x;
    int saved_origin_y;

    memset(mapcur_scene, MAPCUR_GUARD, MAPCUR_SCENE_BYTES);

    saved_sheet = data_fdps_cursor_highlight_sprite_sheet_ptr;
    saved_grid = data_fdps_battle_move_grid_ptr;
    saved_mode = data_fdps_map_cursor_draw_mode;
    saved_cursor_x = data_fdps_map_cursor_world_x;
    saved_cursor_y = data_fdps_map_cursor_world_y;
    saved_origin_x = data_fdps_battle_view_window_origin_x;
    saved_origin_y = data_fdps_battle_view_window_origin_y;

    data_fdps_cursor_highlight_sprite_sheet_ptr = mapcur_sheet;
    data_fdps_battle_move_grid_ptr = mapcur_grid + MAPCUR_GRID_LEAD;
    data_fdps_map_cursor_draw_mode = mode;
    data_fdps_map_cursor_world_x = cursor_x;
    data_fdps_map_cursor_world_y = cursor_y;
    data_fdps_battle_view_window_origin_x = origin_x;
    data_fdps_battle_view_window_origin_y = origin_y;

    fdps_draw_map_cursor(mapcur_scene);

    data_fdps_cursor_highlight_sprite_sheet_ptr = saved_sheet;
    data_fdps_battle_move_grid_ptr = saved_grid;
    data_fdps_map_cursor_draw_mode = saved_mode;
    data_fdps_map_cursor_world_x = saved_cursor_x;
    data_fdps_map_cursor_world_y = saved_cursor_y;
    data_fdps_battle_view_window_origin_x = saved_origin_x;
    data_fdps_battle_view_window_origin_y = saved_origin_y;
}

/* The whole shape at once: mode, at the standard cursor position, with the
   view window unscrolled. */
static void mapcur_draw_mode(int mode)
{
    mapcur_draw(mode, MAPCUR_CURSOR_X, MAPCUR_CURSOR_Y, 0, 0);
}

static int mapcur_painted(void)
{
    int index;
    int painted;

    painted = 0;
    for (index = 0; index < MAPCUR_SCENE_BYTES; index++) {
        if (mapcur_scene[index] != MAPCUR_GUARD) {
            painted++;
        }
    }
    return painted;
}

/* How many of the 576 bytes of the tile (tile_dx, tile_dy) tiles away from the
   cursor are NOT the given palette index.  Zero says the whole tile holds it,
   which for MAPCUR_GUARD means the tile was never drawn at all. */
static int mapcur_tile_wrong(int tile_dx, int tile_dy, int pixel)
{
    int scan_row;
    int scan_column;
    int row;
    int column;
    int wrong;

    row = MAPCUR_CURSOR_Y + tile_dy * MAPCUR_TILE + MAPCUR_BORDER;
    column = MAPCUR_CURSOR_X + tile_dx * MAPCUR_TILE + MAPCUR_BORDER;
    wrong = 0;
    for (scan_row = 0; scan_row < MAPCUR_TILE; scan_row++) {
        for (scan_column = 0; scan_column < MAPCUR_TILE; scan_column++) {
            if (mapcur_scene[(row + scan_row) * MAPCUR_PITCH
                             + column + scan_column]
                != (unsigned char) pixel) {
                wrong++;
            }
        }
    }
    return wrong;
}

/* Shorthand: the tile that sprite `sprite_index` should have filled. */
static int mapcur_tile_sprite(int tile_dx, int tile_dy, int sprite_index)
{
    return mapcur_tile_wrong(tile_dx, tile_dy,
                             MAPCUR_PIXEL_BASE + sprite_index);
}

static int mapcur_tile_blank(int tile_dx, int tile_dy)
{
    return mapcur_tile_wrong(tile_dx, tile_dy, MAPCUR_GUARD);
}

/* Mode 1 -- PUSH 0x0 at 0002c6b9, the cursor coordinates unmodified. */
static void test_mode_one_draws_sprite_zero_on_the_cursor_tile(void)
{
    mapcur_draw_mode(1);
    CHECK_EQ(mapcur_tile_sprite(0, 0, 0), 0);
    CHECK_EQ(mapcur_painted(), MAPCUR_TILE_BYTES);
}

/* Mode 2 -- PUSH 0x1 at 0002c6e1.  The same single tile, the other style. */
static void test_mode_two_draws_sprite_one_on_the_cursor_tile(void)
{
    mapcur_draw_mode(2);
    CHECK_EQ(mapcur_tile_sprite(0, 0, 1), 0);
    CHECK_EQ(mapcur_painted(), MAPCUR_TILE_BYTES);
}

/* Mode 3 -- the five calls at 0002c709, 0002c723, 0002c740, 0002c75d and
   0002c77a: sprite 0x0e on the centre and 2/3/4/5 above, left, right, below.
   The centre taking 0x0e and not 0 or 1 is the whole point of the arm. */
static void test_mode_three_draws_the_radius_one_diamond(void)
{
    mapcur_draw_mode(3);
    CHECK_EQ(mapcur_tile_sprite(0, 0, 0x0e), 0);
    CHECK_EQ(mapcur_tile_sprite(0, -1, 2), 0);
    CHECK_EQ(mapcur_tile_sprite(-1, 0, 3), 0);
    CHECK_EQ(mapcur_tile_sprite(1, 0, 4), 0);
    CHECK_EQ(mapcur_tile_sprite(0, 1, 5), 0);
    CHECK_EQ(mapcur_painted(), 5 * MAPCUR_TILE_BYTES);
}

/* The four diagonals of the radius-1 shape get no call, so a drawer that
   filled a 3 by 3 block would show up here. */
static void test_mode_three_leaves_the_diagonals_blank(void)
{
    mapcur_draw_mode(3);
    CHECK_EQ(mapcur_tile_blank(-1, -1), 0);
    CHECK_EQ(mapcur_tile_blank(1, -1), 0);
    CHECK_EQ(mapcur_tile_blank(-1, 1), 0);
    CHECK_EQ(mapcur_tile_blank(1, 1), 0);
}

/* Mode 4 -- thirteen calls from 0002c7a9 to 0002c90e.  The order the sprites
   go on is the point: 2/3/4/5 land on the tips TWO tiles out (SUB EAX,0x30 at
   0002c7ce and its three companions) while a/b/c/d land on the tiles one out,
   which is the reverse of the reading order a loop would take. */
static void test_mode_four_draws_the_radius_two_diamond(void)
{
    mapcur_draw_mode(4);
    CHECK_EQ(mapcur_tile_sprite(0, 0, 1), 0);
    CHECK_EQ(mapcur_tile_sprite(0, -2, 2), 0);
    CHECK_EQ(mapcur_tile_sprite(-2, 0, 3), 0);
    CHECK_EQ(mapcur_tile_sprite(2, 0, 4), 0);
    CHECK_EQ(mapcur_tile_sprite(0, 2, 5), 0);
    CHECK_EQ(mapcur_tile_sprite(-1, -1, 6), 0);
    CHECK_EQ(mapcur_tile_sprite(1, -1, 7), 0);
    CHECK_EQ(mapcur_tile_sprite(-1, 1, 8), 0);
    CHECK_EQ(mapcur_tile_sprite(1, 1, 9), 0);
    CHECK_EQ(mapcur_tile_sprite(0, -1, 0x0a), 0);
    CHECK_EQ(mapcur_tile_sprite(-1, 0, 0x0b), 0);
    CHECK_EQ(mapcur_tile_sprite(1, 0, 0x0c), 0);
    CHECK_EQ(mapcur_tile_sprite(0, 1, 0x0d), 0);
    CHECK_EQ(mapcur_painted(), 13 * MAPCUR_TILE_BYTES);
}

/* The (1,2)-offset cells belong to the radius-3 shape, not this one. */
static void test_mode_four_leaves_the_outer_ring_blank(void)
{
    mapcur_draw_mode(4);
    CHECK_EQ(mapcur_tile_blank(-1, -2), 0);
    CHECK_EQ(mapcur_tile_blank(2, 1), 0);
    CHECK_EQ(mapcur_tile_blank(0, -3), 0);
    CHECK_EQ(mapcur_tile_blank(3, 0), 0);
}

/* Mode 5 -- twenty-one calls from 0002c93d to 0002cb9f.  Sprites 6 through 9
   are each pushed twice, for the (1,2) and (2,1) cells of the outer ring
   (0002c9cb and 0002c9eb for 6, and so on), which no per-cell sprite table
   would produce. */
static void test_mode_five_draws_the_radius_three_diamond(void)
{
    mapcur_draw_mode(5);
    CHECK_EQ(mapcur_tile_sprite(0, 0, 1), 0);
    CHECK_EQ(mapcur_tile_sprite(0, -3, 2), 0);
    CHECK_EQ(mapcur_tile_sprite(-3, 0, 3), 0);
    CHECK_EQ(mapcur_tile_sprite(3, 0, 4), 0);
    CHECK_EQ(mapcur_tile_sprite(0, 3, 5), 0);
    CHECK_EQ(mapcur_tile_sprite(-1, -2, 6), 0);
    CHECK_EQ(mapcur_tile_sprite(-2, -1, 6), 0);
    CHECK_EQ(mapcur_tile_sprite(1, -2, 7), 0);
    CHECK_EQ(mapcur_tile_sprite(2, -1, 7), 0);
    CHECK_EQ(mapcur_tile_sprite(-1, 2, 8), 0);
    CHECK_EQ(mapcur_tile_sprite(-2, 1, 8), 0);
    CHECK_EQ(mapcur_tile_sprite(1, 2, 9), 0);
    CHECK_EQ(mapcur_tile_sprite(2, 1, 9), 0);
    CHECK_EQ(mapcur_tile_sprite(0, -2, 0x0a), 0);
    CHECK_EQ(mapcur_tile_sprite(-2, 0, 0x0b), 0);
    CHECK_EQ(mapcur_tile_sprite(2, 0, 0x0c), 0);
    CHECK_EQ(mapcur_tile_sprite(0, 2, 0x0d), 0);
    CHECK_EQ(mapcur_painted(), 21 * MAPCUR_TILE_BYTES);
}

/* The two lower diagonals take 0x11 on the left and 0x10 on the right -- PUSH
   0x11 at 0002cb83 with x + 0x18 and PUSH 0x10 at 0002cba3 with x + 0x18 on
   the other side of y -- the one place in the whole call list where the sprite
   ids do not ascend with position. */
static void test_mode_five_lower_diagonals_take_their_ids_out_of_order(void)
{
    mapcur_draw_mode(5);
    CHECK_EQ(mapcur_tile_sprite(-1, -1, 0x0e), 0);
    CHECK_EQ(mapcur_tile_sprite(1, -1, 0x0f), 0);
    CHECK_EQ(mapcur_tile_sprite(-1, 1, 0x11), 0);
    CHECK_EQ(mapcur_tile_sprite(1, 1, 0x10), 0);
}

/* The four tiles orthogonally adjacent to the centre get no call in mode 5, so
   the shape has a hole a loop over a diamond radius would fill in. */
static void test_mode_five_leaves_the_orthogonal_neighbours_blank(void)
{
    mapcur_draw_mode(5);
    CHECK_EQ(mapcur_tile_blank(0, -1), 0);
    CHECK_EQ(mapcur_tile_blank(-1, 0), 0);
    CHECK_EQ(mapcur_tile_blank(1, 0), 0);
    CHECK_EQ(mapcur_tile_blank(0, 1), 0);
}

/* Mode 0 falls out of the last JNZ at 0002cbc8 onto the epilogue, and so does
   any mode the chain does not name. */
static void test_unnamed_modes_draw_nothing(void)
{
    mapcur_draw_mode(0);
    CHECK_EQ(mapcur_painted(), 0);
    mapcur_draw_mode(7);
    CHECK_EQ(mapcur_painted(), 0);
    mapcur_draw_mode(-1);
    CHECK_EQ(mapcur_painted(), 0);
}

/* The coordinates handed to fdps_blit_cursor_tile are world pixels, not screen
   pixels: the view origin is subtracted inside the blitter, so scrolling the
   window moves the same cursor tile across the scene buffer. */
static void test_the_view_origin_moves_the_tile_in_the_scene(void)
{
    int expect_index;

    mapcur_draw(1, MAPCUR_CURSOR_X, MAPCUR_CURSOR_Y, MAPCUR_TILE,
                MAPCUR_TILE);
    expect_index = (MAPCUR_CURSOR_Y - MAPCUR_TILE + MAPCUR_BORDER)
                       * MAPCUR_PITCH
                   + (MAPCUR_CURSOR_X - MAPCUR_TILE) + MAPCUR_BORDER;
    CHECK_EQ(mapcur_scene[expect_index], MAPCUR_PIXEL_BASE);
    CHECK_EQ(mapcur_painted(), MAPCUR_TILE_BYTES);
}

/* No clipping happens in the drawer: a cell outside the visible window is
   dropped whole by fdps_blit_cursor_tile's own bounds test.  With the cursor
   on the map origin the two cells at -24 fall below the window's inclusive
   lower bound and vanish, leaving three of the five. */
static void test_cells_outside_the_window_are_dropped_whole(void)
{
    mapcur_draw(3, 0, 0, 0, 0);
    CHECK_EQ(mapcur_scene[MAPCUR_BORDER * MAPCUR_PITCH + MAPCUR_BORDER],
             MAPCUR_PIXEL_BASE + 0x0e);
    CHECK_EQ(mapcur_scene[MAPCUR_BORDER * MAPCUR_PITCH + MAPCUR_BORDER
                          + MAPCUR_TILE],
             MAPCUR_PIXEL_BASE + 4);
    CHECK_EQ(mapcur_scene[(MAPCUR_BORDER + MAPCUR_TILE) * MAPCUR_PITCH
                          + MAPCUR_BORDER],
             MAPCUR_PIXEL_BASE + 5);
    CHECK_EQ(mapcur_painted(), 3 * MAPCUR_TILE_BYTES);
}

/* Mode 6 draws nothing at all and clears byte 1 of the grid cell under the
   cursor instead -- MOV byte ptr [EAX + 0x5],0x0 at 0002cc0e, which is the
   four-byte header plus the cell's own byte 1.  Cursor (144, 96) is tile
   (6, 4), cell 4 * 10 + 6 = 46 of a ten-wide grid. */
static void test_mode_six_clears_the_marker_and_draws_nothing(void)
{
    mapcur_stage_grid();
    mapcur_draw_mode(6);
    CHECK_EQ(mapcur_painted(), 0);
    CHECK_EQ(mapcur_cell_byte(46, 1), 0);
    CHECK_EQ(mapcur_cell_byte(46, 0), MAPCUR_CELL_FLAGS);
    CHECK_EQ(mapcur_cell_byte(45, 1), MAPCUR_CELL_MARKER);
    CHECK_EQ(mapcur_cell_byte(47, 1), MAPCUR_CELL_MARKER);
    CHECK_EQ(mapcur_lead_in_touched(), 0);
}

/* The row stride comes out of the grid header, so the cell reached depends on
   the width word rather than on any constant: tile (2, 3) of the ten-wide grid
   is cell 32. */
static void test_mode_six_indexes_through_the_header_width(void)
{
    mapcur_stage_grid();
    mapcur_draw(6, 2 * MAPCUR_TILE, 3 * MAPCUR_TILE, 0, 0);
    CHECK_EQ(mapcur_cell_byte(32, 1), 0);
    CHECK_EQ(mapcur_cell_byte(31, 1), MAPCUR_CELL_MARKER);
    CHECK_EQ(mapcur_cell_byte(22, 1), MAPCUR_CELL_MARKER);
    CHECK_EQ(mapcur_lead_in_touched(), 0);
}

/* Any pixel inside a tile picks that tile: the division discards the
   remainder, so the last pixel of tile (2, 3) reaches the same cell 32. */
static void test_mode_six_takes_the_tile_the_pixel_lies_in(void)
{
    mapcur_stage_grid();
    mapcur_draw(6, 2 * MAPCUR_TILE + 23, 3 * MAPCUR_TILE + 23, 0, 0);
    CHECK_EQ(mapcur_cell_byte(32, 1), 0);
    CHECK_EQ(mapcur_cell_byte(33, 1), MAPCUR_CELL_MARKER);
    CHECK_EQ(mapcur_cell_byte(42, 1), MAPCUR_CELL_MARKER);
}

/* Both divisions are signed and truncate towards zero -- SAR EDX,0x1f / IDIV
   EBX at 0002cbda and 0002cbfd -- so a cursor one pixel left of or above the
   map origin still lands on tile 0.  A division that floored would form cell
   -11 and write into the poisoned lead-in; an unsigned one would form a cell
   index of hundreds of millions. */
static void test_mode_six_divides_negative_coordinates_towards_zero(void)
{
    mapcur_stage_grid();
    mapcur_draw(6, -1, -1, 0, 0);
    CHECK_EQ(mapcur_cell_byte(0, 1), 0);
    CHECK_EQ(mapcur_cell_byte(1, 1), MAPCUR_CELL_MARKER);
    CHECK_EQ(mapcur_lead_in_touched(), 0);

    mapcur_stage_grid();
    mapcur_draw(6, 6 * MAPCUR_TILE, -1, 0, 0);
    CHECK_EQ(mapcur_cell_byte(6, 1), 0);
    CHECK_EQ(mapcur_cell_byte(5, 1), MAPCUR_CELL_MARKER);
    CHECK_EQ(mapcur_lead_in_touched(), 0);
}

/* The drawing arms never touch the grid, and mode 6 never touches the scene:
   the two halves of this function share nothing but the dispatch. */
static void test_the_drawing_modes_leave_the_grid_alone(void)
{
    mapcur_stage_grid();
    mapcur_draw_mode(5);
    CHECK_EQ(mapcur_cell_byte(46, 1), MAPCUR_CELL_MARKER);
    CHECK_EQ(mapcur_cell_byte(46, 0), MAPCUR_CELL_FLAGS);
    CHECK_EQ(mapcur_lead_in_touched(), 0);
}


/* ------------------------------------------------------------------------
   fdps_draw_cursor_info_panel, 0002dcf0.

   The panel is exercised against the real fdps_map_load_tile_info,
   fdps_cel_blit_sprite, fdps_battle_find_unit_at_cursor, fdps_blit_dispatch
   and fdps_draw_number, because almost everything the function decides is only
   visible in what those leave in the buffer: which colour row a figure came
   out of, which cache entry the walking sprite came from, and which of the two
   terrain tables an index reached.

   Every sheet is staged so that a painted pixel names the sprite that painted
   it -- the panel background paints 0x11, terrain tile t paints 0x20 + t,
   sprite cache entry e paints 0x30 + e and Number.cel's glyph g of colour row
   r paints 0x60 + r * 13 + g -- and the scene is filled with a guard first, so
   an untouched pixel is as assertable as a painted one.

   Expected positions come from the four folded displacements in the assembly:
   0xe6b3, 0xf621, 0x10b39 and 0x11d8b are row * 0x168 + column for rows 164,
   175, 190 and 203, and the two cel blits carry rows 0xa0 and 0xaf as
   arguments.  Expected figures come from the assembly's argument groups, never
   from the emitted C.

   Nothing here asserts what any global holds on its own -- ticket 23 owns
   their contents -- and every global the panel reads or writes is put back
   afterwards. */

/* The map is wide enough that a cursor eleven tiles into the view window, the
   position that parks the panel on the left, still names a cell inside it. */
#define PANEL_MAP_W 16
#define PANEL_MAP_H 10
#define PANEL_MAP_CELLS (PANEL_MAP_W * PANEL_MAP_H)

#define PANEL_TILEMAP_BYTES 0x180
#define PANEL_ATTR_BYTES 0x80
#define PANEL_EVENT_BYTES 0x100
#define PANEL_GRID_BYTES 0x180

/* Two cells with different graphics and different terrain classes, so an
   assertion about which cell was looked up is not vacuous. */
#define PANEL_TILE_ID_HOME 5
#define PANEL_TERRAIN_HOME 3
#define PANEL_TILE_ID_AWAY 6
#define PANEL_TERRAIN_AWAY 1

/* The cell the unit stands on, well inside the top five rows of the view
   window so that no test that is not about the dodge triggers one. */
#define PANEL_HOME_TILE_X 4
#define PANEL_HOME_TILE_Y 2

/* The cell the dodge tests aim at: low enough down the window for the row test
   to pass, so only the column decides. */
#define PANEL_AWAY_TILE_X 1
#define PANEL_AWAY_TILE_Y 6

/* Neither of the two columns the function itself parks the panel at, so a
   figure drawn at the right place proves the panel column was read from the
   global rather than assumed. */
#define PANEL_TEST_COLUMN 40
#define PANEL_PARK_RIGHT 0x124
#define PANEL_PARK_LEFT 0x19

/* Number.cel's colour row a test leaves standing before the call: neither 0,
   which the function forces on the way out, nor 3, which a hurt unit's HP
   figure asks for. */
#define PANEL_AMBIENT_ROW 2
#define PANEL_HURT_ROW 3

#define PANEL_GUARD 0xee
#define PANEL_WIN_PIXEL 0x11
#define PANEL_TILE_PIXEL_BASE 0x20
#define PANEL_CACHE_PIXEL_BASE 0x30
#define PANEL_GLYPH_PIXEL_BASE 0x60

/* Where each piece lands, relative to the panel column. */
#define PANEL_WIN_ROW 0xa0
#define PANEL_WIN_COL 0
#define PANEL_CELL_ROW 0xaf
#define PANEL_CELL_COL 9
#define PANEL_AP_ROW 164
#define PANEL_AP_COL 19
#define PANEL_HP_ROW 190
#define PANEL_HP_COL 9
#define PANEL_DEF_ROW 203
#define PANEL_DEF_COL 19

#define PANEL_GLYPH_W 6
#define PANEL_GLYPH_H 8
#define PANEL_CELL_SIZE 24

/* The panel background is deliberately tiny -- four by two -- so that it
   cannot reach any of the four fields and every painted pixel has exactly one
   owner. */
#define PANEL_WIN_W 4
#define PANEL_WIN_H 2

#define PANEL_WIN_SHEET_BYTES 0x40
#define PANEL_TILE_SPRITES 8
#define PANEL_TILE_STREAM0 0x40
#define PANEL_TILE_STREAM_STRIDE 0x40
#define PANEL_TILE_SHEET_BYTES 0x240

/* Twelve cache entries to a slot, and three slots staged, so the entry the
   panel picks for cache slot 2 is well away from entry 0. */
#define PANEL_CACHE_SLOT 2
#define PANEL_CACHE_SLOT_ENTRIES 12
#define PANEL_CACHE_ENTRIES 36
#define PANEL_CACHE_STREAM0 0x90
#define PANEL_CACHE_STREAM_STRIDE 0x40
#define PANEL_CACHE_BYTES 0x9a0

#define PANEL_GLYPHS_PER_ROW 13
#define PANEL_GLYPH_SPRITES 65
#define PANEL_NUM_TABLE_AT 0x0f
#define PANEL_NUM_STREAM0 0x120
#define PANEL_NUM_STREAM_STRIDE 0x20
#define PANEL_NUM_SHEET_BYTES 0x960

/* fdps_draw_number's glyph indices: the ten digits are 0..9, then '+', '-'
   and '?' (src/text.c). */
#define PANEL_GLYPH_PLUS 10
#define PANEL_GLYPH_MINUS 11

/* The two figures the terrain tables are staged with.  A negative defense
   modifier is the case that pins the tables as signed: read unsigned, -7 would
   print as a ten-digit figure. */
#define PANEL_AP_MODIFIER 25
#define PANEL_DEF_MODIFIER (-7)

#define PANEL_HP_HURT 12
#define PANEL_HP_FULL 20

static unsigned char panel_win_sheet[PANEL_WIN_SHEET_BYTES];
static unsigned char panel_tile_sheet[PANEL_TILE_SHEET_BYTES];
static unsigned char panel_cache[PANEL_CACHE_BYTES];
static unsigned char panel_num_sheet[PANEL_NUM_SHEET_BYTES];
static unsigned char panel_tilemap[PANEL_TILEMAP_BYTES];
static unsigned char panel_attr[PANEL_ATTR_BYTES];
static unsigned char panel_events[PANEL_EVENT_BYTES];
static unsigned char panel_grid[PANEL_GRID_BYTES];
static struct fdps_unit_record panel_units[1];

static struct {
    unsigned char hud_enabled;
    unsigned char play_active;
    short panel_offset;
    int cursor_x;
    int cursor_y;
    int origin_x;
    int origin_y;
    unsigned char *tile_map;
    unsigned char *tile_attr;
    unsigned char *tile_sheet;
    unsigned char *grid;
    unsigned char *event_layer;
    unsigned char *win_sheet;
    unsigned char *num_sheet;
    unsigned char *units;
    int unit_count;
    unsigned char *cache;
    unsigned int tick;
    int color_row;
    int ap_table[6];
    int def_table[6];
    short tile_id;
    unsigned char terrain;
} panel_saved;

/* One fill command per row: the top two bits of a command byte are the op and
   its low six bits plus one are the run length (src/rle.c), so a run of `width`
   opaque pixels is the byte width - 1 followed by the pixel. */
static void panel_fill_stream(unsigned char *buffer, int at, int width,
                              int rows, unsigned char pixel)
{
    int row;

    for (row = 0; row < rows; row++) {
        buffer[at + row * 2] = (unsigned char) (width - 1);
        buffer[at + row * 2 + 1] = pixel;
    }
}

static void panel_stage_sheets(void)
{
    int sprite_index;
    int stream_at;

    memset(panel_win_sheet, 0, PANEL_WIN_SHEET_BYTES);
    mapcur_u16(panel_win_sheet, 7, PANEL_WIN_W);
    mapcur_u16(panel_win_sheet, 9, PANEL_WIN_H);
    mapcur_u32(panel_win_sheet, MAPCUR_TABLE_AT, 0x20);
    panel_fill_stream(panel_win_sheet, 0x20, PANEL_WIN_W, PANEL_WIN_H,
                      PANEL_WIN_PIXEL);

    memset(panel_tile_sheet, 0, PANEL_TILE_SHEET_BYTES);
    mapcur_u16(panel_tile_sheet, 7, PANEL_CELL_SIZE);
    mapcur_u16(panel_tile_sheet, 9, PANEL_CELL_SIZE);
    for (sprite_index = 0; sprite_index < PANEL_TILE_SPRITES; sprite_index++) {
        stream_at = PANEL_TILE_STREAM0
                    + sprite_index * PANEL_TILE_STREAM_STRIDE;
        mapcur_u32(panel_tile_sheet, MAPCUR_TABLE_AT + sprite_index * 4,
                   (unsigned long) stream_at);
        panel_fill_stream(panel_tile_sheet, stream_at, PANEL_CELL_SIZE,
                          PANEL_CELL_SIZE,
                          (unsigned char) (PANEL_TILE_PIXEL_BASE
                                           + sprite_index));
    }

    /* The sprite cache's offset table starts at the block's own base, not at a
       .CEL's +0xf: it is a table fdps_cache_cel_sprite_group builds. */
    memset(panel_cache, 0, PANEL_CACHE_BYTES);
    for (sprite_index = 0; sprite_index < PANEL_CACHE_ENTRIES;
         sprite_index++) {
        stream_at = PANEL_CACHE_STREAM0
                    + sprite_index * PANEL_CACHE_STREAM_STRIDE;
        mapcur_u32(panel_cache, sprite_index * 4, (unsigned long) stream_at);
        panel_fill_stream(panel_cache, stream_at, PANEL_CELL_SIZE,
                          PANEL_CELL_SIZE,
                          (unsigned char) (PANEL_CACHE_PIXEL_BASE
                                           + sprite_index));
    }

    memset(panel_num_sheet, 0, PANEL_NUM_SHEET_BYTES);
    for (sprite_index = 0; sprite_index < PANEL_GLYPH_SPRITES;
         sprite_index++) {
        stream_at = PANEL_NUM_STREAM0
                    + sprite_index * PANEL_NUM_STREAM_STRIDE;
        mapcur_u32(panel_num_sheet, PANEL_NUM_TABLE_AT + sprite_index * 4,
                   (unsigned long) stream_at);
        panel_fill_stream(panel_num_sheet, stream_at, PANEL_GLYPH_W,
                          PANEL_GLYPH_H,
                          (unsigned char) (PANEL_GLYPH_PIXEL_BASE
                                           + sprite_index));
    }
}

/* A tile map whose header carries the width at +7 and whose 16-bit tile ids
   start at +0xb, an attribute table whose 4-byte rows start at +0x11, an event
   code layer and a movement grid: the four blocks fdps_map_load_tile_info
   walks before the panel draws anything. */
static void panel_stage_map(void)
{
    int cell;

    memset(panel_tilemap, 0, PANEL_TILEMAP_BYTES);
    mapcur_u16(panel_tilemap, 7, PANEL_MAP_W);
    mapcur_u16(panel_tilemap, 9, PANEL_MAP_H);
    for (cell = 0; cell < PANEL_MAP_CELLS; cell++) {
        mapcur_u16(panel_tilemap, 0x0b + cell * 2, PANEL_TILE_ID_HOME);
    }
    mapcur_u16(panel_tilemap,
               0x0b + (PANEL_AWAY_TILE_Y * PANEL_MAP_W + PANEL_AWAY_TILE_X)
                   * 2,
               PANEL_TILE_ID_AWAY);

    memset(panel_attr, 0, PANEL_ATTR_BYTES);
    panel_attr[0x11 + PANEL_TILE_ID_HOME * 4 + 2] = PANEL_TERRAIN_HOME;
    panel_attr[0x11 + PANEL_TILE_ID_AWAY * 4 + 2] = PANEL_TERRAIN_AWAY;

    memset(panel_events, 0, PANEL_EVENT_BYTES);
    mapcur_u16(panel_events, 7, PANEL_MAP_W);
    mapcur_u16(panel_events, 9, PANEL_MAP_H);

    memset(panel_grid, 0, PANEL_GRID_BYTES);
    mapcur_u16(panel_grid, 0, PANEL_MAP_W);
    mapcur_u16(panel_grid, 2, PANEL_MAP_H);
}

static void panel_save(void)
{
    int index;

    panel_saved.hud_enabled = data_fdps_ui_terrain_hud_user_enabled;
    panel_saved.play_active = data_fdps_ui_play_active_flag;
    panel_saved.panel_offset = data_fdps_ui_terrain_hud_panel_offset;
    panel_saved.cursor_x = data_fdps_map_cursor_world_x;
    panel_saved.cursor_y = data_fdps_map_cursor_world_y;
    panel_saved.origin_x = data_fdps_battle_view_window_origin_x;
    panel_saved.origin_y = data_fdps_battle_view_window_origin_y;
    panel_saved.tile_map = data_fdps_scene_layer_tile_map_ptrs[0];
    panel_saved.tile_attr = data_fdps_scene_layer_tile_attr_ptr[0];
    panel_saved.tile_sheet = data_fdps_scene_layer_tile_sheet_ptrs[0];
    panel_saved.grid = data_fdps_battle_move_grid_ptr;
    panel_saved.event_layer = data_fdps_map_cell_event_code_layer_ptr;
    panel_saved.win_sheet = data_fdps_ui_terrain_hud_panel_sheet_ptr;
    panel_saved.num_sheet = data_fdps_number_glyph_sheet_ptr;
    panel_saved.units = data_fdps_map_unit_array_ptr;
    panel_saved.unit_count = data_fdps_map_unit_count;
    panel_saved.cache = data_fdps_cel_sprite_cache_ptr;
    panel_saved.tick = data_fdps_timer_tick_counter;
    panel_saved.color_row = data_fdps_number_glyph_color_row;
    panel_saved.tile_id = data_fdps_map_tile_info_tile_id;
    panel_saved.terrain = data_fdps_map_tile_terrain_type;
    for (index = 0; index < 6; index++) {
        panel_saved.ap_table[index] =
            data_fdps_battle_tile_attr_ap_modifier_table[index];
        panel_saved.def_table[index] =
            data_fdps_battle_tile_attr_def_modifier_table[index];
    }
}

static void panel_restore(void)
{
    int index;

    data_fdps_ui_terrain_hud_user_enabled = panel_saved.hud_enabled;
    data_fdps_ui_play_active_flag = panel_saved.play_active;
    data_fdps_ui_terrain_hud_panel_offset = panel_saved.panel_offset;
    data_fdps_map_cursor_world_x = panel_saved.cursor_x;
    data_fdps_map_cursor_world_y = panel_saved.cursor_y;
    data_fdps_battle_view_window_origin_x = panel_saved.origin_x;
    data_fdps_battle_view_window_origin_y = panel_saved.origin_y;
    data_fdps_scene_layer_tile_map_ptrs[0] = panel_saved.tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = panel_saved.tile_attr;
    data_fdps_scene_layer_tile_sheet_ptrs[0] = panel_saved.tile_sheet;
    data_fdps_battle_move_grid_ptr = panel_saved.grid;
    data_fdps_map_cell_event_code_layer_ptr = panel_saved.event_layer;
    data_fdps_ui_terrain_hud_panel_sheet_ptr = panel_saved.win_sheet;
    data_fdps_number_glyph_sheet_ptr = panel_saved.num_sheet;
    data_fdps_map_unit_array_ptr = panel_saved.units;
    data_fdps_map_unit_count = panel_saved.unit_count;
    data_fdps_cel_sprite_cache_ptr = panel_saved.cache;
    data_fdps_timer_tick_counter = panel_saved.tick;
    data_fdps_number_glyph_color_row = panel_saved.color_row;
    data_fdps_map_tile_info_tile_id = panel_saved.tile_id;
    data_fdps_map_tile_terrain_type = panel_saved.terrain;
    for (index = 0; index < 6; index++) {
        data_fdps_battle_tile_attr_ap_modifier_table[index] =
            panel_saved.ap_table[index];
        data_fdps_battle_tile_attr_def_modifier_table[index] =
            panel_saved.def_table[index];
    }
}

/* Everything switched on, the cursor on the unit's own tile with the view
   window unscrolled, one live unit in the array and the panel parked at a
   column the function never chooses for itself. */
static void panel_install(void)
{
    data_fdps_ui_terrain_hud_user_enabled = 1;
    data_fdps_ui_play_active_flag = 1;
    data_fdps_ui_terrain_hud_panel_offset = PANEL_TEST_COLUMN;
    data_fdps_map_cursor_world_x = PANEL_HOME_TILE_X * PANEL_CELL_SIZE;
    data_fdps_map_cursor_world_y = PANEL_HOME_TILE_Y * PANEL_CELL_SIZE;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_scene_layer_tile_map_ptrs[0] = panel_tilemap;
    data_fdps_scene_layer_tile_attr_ptr[0] = panel_attr;
    data_fdps_scene_layer_tile_sheet_ptrs[0] = panel_tile_sheet;
    data_fdps_battle_move_grid_ptr = panel_grid;
    data_fdps_map_cell_event_code_layer_ptr = panel_events;
    data_fdps_ui_terrain_hud_panel_sheet_ptr = panel_win_sheet;
    data_fdps_number_glyph_sheet_ptr = panel_num_sheet;
    data_fdps_cel_sprite_cache_ptr = panel_cache;
    data_fdps_timer_tick_counter = 0;
    data_fdps_number_glyph_color_row = PANEL_AMBIENT_ROW;
    data_fdps_battle_tile_attr_ap_modifier_table[PANEL_TERRAIN_HOME] =
        PANEL_AP_MODIFIER;
    data_fdps_battle_tile_attr_def_modifier_table[PANEL_TERRAIN_HOME] =
        PANEL_DEF_MODIFIER;
    data_fdps_battle_tile_attr_ap_modifier_table[PANEL_TERRAIN_AWAY] = 0;
    data_fdps_battle_tile_attr_def_modifier_table[PANEL_TERRAIN_AWAY] = 0;

    memset(panel_units, 0, sizeof(panel_units));
    panel_units[0].pos_x = PANEL_HOME_TILE_X;
    panel_units[0].pos_y = PANEL_HOME_TILE_Y;
    panel_units[0].sprite_cache_slot = PANEL_CACHE_SLOT;
    panel_units[0].hp_current = PANEL_HP_FULL;
    panel_units[0].hp_max = PANEL_HP_FULL;
    data_fdps_map_unit_array_ptr = (unsigned char *) panel_units;
    data_fdps_map_unit_count = 1;
}

static void panel_run(void)
{
    memset(mapcur_scene, PANEL_GUARD, MAPCUR_SCENE_BYTES);
    fdps_draw_cursor_info_panel(mapcur_scene);
}

static unsigned char panel_px(int row, int col)
{
    return mapcur_scene[row * MAPCUR_PITCH + col];
}

/* A pixel two in and two down from the top left of glyph `slot` of a figure
   whose first cell starts at `row`, `col` -- inside the cell whichever glyph
   it is. */
static unsigned char panel_glyph_px(int row, int col, int slot)
{
    return panel_px(row + 2, col + slot * PANEL_GLYPH_W + 2);
}

static int panel_glyph_pixel(int color_row, int glyph_index)
{
    return PANEL_GLYPH_PIXEL_BASE + color_row * PANEL_GLYPHS_PER_ROW
           + glyph_index;
}

static int panel_painted(void)
{
    int index;
    int painted;

    painted = 0;
    for (index = 0; index < MAPCUR_SCENE_BYTES; index++) {
        if (mapcur_scene[index] != PANEL_GUARD) {
            painted++;
        }
    }
    return painted;
}

/* Puts the cursor on a tile of the map without moving the view window. */
static void panel_cursor_at(int tile_x, int tile_y)
{
    data_fdps_map_cursor_world_x = tile_x * PANEL_CELL_SIZE;
    data_fdps_map_cursor_world_y = tile_y * PANEL_CELL_SIZE;
}

/* The panel reads the unit record at +2, +0x40 and +0x42 -- MOV AL,byte ptr
   [EAX+0x2] at 0002decd and the two MOVSX word ptr at 0002df2b and 0002df35 --
   and walks the array with the 0x50 stride.  Those four numbers are what makes
   struct fdps_unit_record the right window onto the array. */
static void test_the_record_fields_sit_where_the_panel_reads_them(void)
{
    struct fdps_unit_record probe;

    CHECK_EQ((char *) &probe.sprite_cache_slot - (char *) &probe, 2);
    CHECK_EQ((char *) &probe.hp_current - (char *) &probe, 0x40);
    CHECK_EQ((char *) &probe.hp_max - (char *) &probe, 0x42);
    CHECK_EQ(sizeof(struct fdps_unit_record), 0x50);
}

/* CMP byte ptr [0x00060158],0x0 / JZ at 0002dd32 jumps straight to the
   epilogue, so with the player's toggle off nothing is drawn, the panel column
   is not moved, and -- because the gate sits ahead of the call at 0002dd75 --
   the tile-info block is not refreshed either. */
static void test_the_user_toggle_off_draws_nothing_at_all(void)
{
    panel_save();
    panel_install();
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_map_tile_info_tile_id = 0x123;
    panel_cursor_at(PANEL_AWAY_TILE_X, PANEL_AWAY_TILE_Y);
    panel_run();

    CHECK_EQ(panel_painted(), 0);
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_TEST_COLUMN);
    CHECK_EQ(data_fdps_map_tile_info_tile_id, 0x123);
    CHECK_EQ(data_fdps_number_glyph_color_row, PANEL_AMBIENT_ROW);
    panel_restore();
}

/* The second half of the same gate, CMP byte ptr [0x00060159],0x0 / JNZ at
   0002dd3b: a menu, a status window or a cut scene owns the screen and the
   panel keeps out of it. */
static void test_the_map_not_being_live_draws_nothing_at_all(void)
{
    panel_save();
    panel_install();
    data_fdps_ui_play_active_flag = 0;
    data_fdps_map_tile_info_tile_id = 0x123;
    panel_cursor_at(PANEL_AWAY_TILE_X, PANEL_AWAY_TILE_Y);
    panel_run();

    CHECK_EQ(panel_painted(), 0);
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_TEST_COLUMN);
    CHECK_EQ(data_fdps_map_tile_info_tile_id, 0x123);
    panel_restore();
}

/* CMP [EBP-0x4],0x4 / JLE then CMP [EBP-0x8],0x2 / JL at 0002dd7d: the cursor
   has to be BELOW window row 4 and LEFT of window column 2 before the panel
   moves to 0x124.  Row 4 itself does not qualify, and neither does column 2. */
static void test_the_panel_parks_right_under_a_low_left_cursor(void)
{
    panel_save();
    panel_install();
    panel_cursor_at(1, 5);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_PARK_RIGHT);

    data_fdps_ui_terrain_hud_panel_offset = PANEL_TEST_COLUMN;
    panel_cursor_at(0, 6);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_PARK_RIGHT);

    data_fdps_ui_terrain_hud_panel_offset = PANEL_TEST_COLUMN;
    panel_cursor_at(1, 4);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_TEST_COLUMN);

    data_fdps_ui_terrain_hud_panel_offset = PANEL_TEST_COLUMN;
    panel_cursor_at(2, 6);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_TEST_COLUMN);
    panel_restore();
}

/* CMP [EBP-0x8],0xa / JG at 0002dd9c, reached only when the first arm did not
   fire: below window row 4 and past window column 10 parks the panel at 0x19.
   Column 10 itself does not qualify. */
static void test_the_panel_parks_left_under_a_low_right_cursor(void)
{
    panel_save();
    panel_install();
    panel_cursor_at(11, 5);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_PARK_LEFT);

    data_fdps_ui_terrain_hud_panel_offset = PANEL_TEST_COLUMN;
    panel_cursor_at(10, 6);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_TEST_COLUMN);

    data_fdps_ui_terrain_hud_panel_offset = PANEL_TEST_COLUMN;
    panel_cursor_at(11, 4);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_TEST_COLUMN);
    panel_restore();
}

/* Neither arm has an else, so a cursor the two tests do not name leaves the
   column exactly where the last dodge put it -- which is why the column is a
   global at all. */
static void test_a_middle_cursor_leaves_the_panel_where_it_was(void)
{
    panel_save();
    panel_install();
    data_fdps_ui_terrain_hud_panel_offset = PANEL_PARK_LEFT;
    panel_cursor_at(5, 6);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_PARK_LEFT);

    panel_cursor_at(1, 1);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_PARK_LEFT);
    panel_restore();
}

/* The dodge is decided on the cursor MINUS the view origin (0002dcfc,
   0002dd17) while the tile whose information is shown is the cursor's absolute
   map tile (0002dd49, 0002dd5f).  Scrolling the window under a stationary
   cursor therefore changes where the panel sits without changing which cell is
   read. */
static void test_the_dodge_is_window_relative_and_the_lookup_is_not(void)
{
    panel_save();
    panel_install();
    panel_cursor_at(PANEL_AWAY_TILE_X, PANEL_AWAY_TILE_Y);
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_PARK_RIGHT);
    CHECK_EQ(data_fdps_map_tile_info_tile_id, PANEL_TILE_ID_AWAY);
    CHECK_EQ(data_fdps_map_tile_terrain_type, PANEL_TERRAIN_AWAY);

    data_fdps_ui_terrain_hud_panel_offset = PANEL_TEST_COLUMN;
    data_fdps_battle_view_window_origin_y = 5 * PANEL_CELL_SIZE;
    panel_run();
    CHECK_EQ(data_fdps_ui_terrain_hud_panel_offset, PANEL_TEST_COLUMN);
    CHECK_EQ(data_fdps_map_tile_info_tile_id, PANEL_TILE_ID_AWAY);
    CHECK_EQ(data_fdps_map_tile_terrain_type, PANEL_TERRAIN_AWAY);
    panel_restore();
}

/* Both cel blits go through fdps_cel_blit_sprite with the panel column as
   their x: the background at row 0xa0 and sprite 0, the cursor tile's own
   graphic at row 0xaf, column + 9 and sprite = the tile id. */
static void test_the_background_and_the_tile_graphic_land_on_the_panel(void)
{
    panel_save();
    panel_install();
    data_fdps_map_unit_count = 0;
    panel_run();

    CHECK_EQ(panel_px(PANEL_WIN_ROW, PANEL_TEST_COLUMN + PANEL_WIN_COL),
             PANEL_WIN_PIXEL);
    CHECK_EQ(panel_px(PANEL_WIN_ROW + PANEL_WIN_H - 1,
                      PANEL_TEST_COLUMN + PANEL_WIN_COL + PANEL_WIN_W - 1),
             PANEL_WIN_PIXEL);
    CHECK_EQ(panel_px(PANEL_WIN_ROW + PANEL_WIN_H,
                      PANEL_TEST_COLUMN + PANEL_WIN_COL),
             PANEL_GUARD);

    CHECK_EQ(panel_px(PANEL_CELL_ROW, PANEL_TEST_COLUMN + PANEL_CELL_COL),
             PANEL_TILE_PIXEL_BASE + PANEL_TILE_ID_HOME);
    CHECK_EQ(panel_px(PANEL_CELL_ROW + PANEL_CELL_SIZE - 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + PANEL_CELL_SIZE - 1),
             PANEL_TILE_PIXEL_BASE + PANEL_TILE_ID_HOME);
    CHECK_EQ(panel_px(PANEL_CELL_ROW, PANEL_TEST_COLUMN + PANEL_CELL_COL - 1),
             PANEL_GUARD);
    panel_restore();
}

/* The attack figure comes out of the table at 0x60040 and the defense figure
   out of the one at 0x60058, both indexed by the terrain class the tile lookup
   just published, both drawn with digit_count 0 -- natural width, no padding
   -- and show_plus 1.  So +25 is three glyphs and no fourth, and -7 is two:
   the leading '+' is suppressed for a negative figure by fdps_draw_number's own
   rule, and a table read as unsigned would print ten glyphs instead of two. */
static void test_the_two_terrain_figures_come_from_their_own_tables(void)
{
    panel_save();
    panel_install();
    data_fdps_map_unit_count = 0;
    panel_run();

    CHECK_EQ(panel_glyph_px(PANEL_AP_ROW, PANEL_TEST_COLUMN + PANEL_AP_COL, 0),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, PANEL_GLYPH_PLUS));
    CHECK_EQ(panel_glyph_px(PANEL_AP_ROW, PANEL_TEST_COLUMN + PANEL_AP_COL, 1),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, 2));
    CHECK_EQ(panel_glyph_px(PANEL_AP_ROW, PANEL_TEST_COLUMN + PANEL_AP_COL, 2),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, 5));
    CHECK_EQ(panel_glyph_px(PANEL_AP_ROW, PANEL_TEST_COLUMN + PANEL_AP_COL, 3),
             PANEL_GUARD);

    CHECK_EQ(panel_glyph_px(PANEL_DEF_ROW, PANEL_TEST_COLUMN + PANEL_DEF_COL,
                            0),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, PANEL_GLYPH_MINUS));
    CHECK_EQ(panel_glyph_px(PANEL_DEF_ROW, PANEL_TEST_COLUMN + PANEL_DEF_COL,
                            1),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, 7));
    CHECK_EQ(panel_glyph_px(PANEL_DEF_ROW, PANEL_TEST_COLUMN + PANEL_DEF_COL,
                            2),
             PANEL_GUARD);
    panel_restore();
}

/* Nothing writes data_fdps_number_glyph_color_row before those two figures, so
   they come out in whatever row the previous drawing call left standing -- and
   with no unit under the cursor the function never touches the row at all. */
static void test_the_terrain_figures_keep_the_ambient_colour_row(void)
{
    panel_save();
    panel_install();
    data_fdps_map_unit_count = 0;
    data_fdps_number_glyph_color_row = 4;
    panel_run();

    CHECK_EQ(panel_glyph_px(PANEL_AP_ROW, PANEL_TEST_COLUMN + PANEL_AP_COL, 1),
             panel_glyph_pixel(4, 2));
    CHECK_EQ(data_fdps_number_glyph_color_row, 4);
    panel_restore();
}

/* With no unit on the cursor's tile fdps_battle_find_unit_at_cursor answers -1
   and CMP [EBP-0x1c],-0x1 / JZ at 0002de84 leaves for the epilogue: no sprite
   over the tile graphic and no HP figure under it, so the tile cell still
   shows the terrain graphic all the way down. */
static void test_an_empty_tile_draws_neither_sprite_nor_hp(void)
{
    panel_save();
    panel_install();
    panel_units[0].pos_x = PANEL_HOME_TILE_X + 1;
    panel_run();

    CHECK_EQ(panel_px(PANEL_CELL_ROW + 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + 2),
             PANEL_TILE_PIXEL_BASE + PANEL_TILE_ID_HOME);
    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 0),
             PANEL_TILE_PIXEL_BASE + PANEL_TILE_ID_HOME);
    CHECK_EQ(data_fdps_number_glyph_color_row, PANEL_AMBIENT_ROW);
    panel_restore();
}

/* The walk frame is (tick >> 2) & 3 with 3 folded back onto 1 (0002deb4 and
   0002debd), so the cycle rocks 0, 1, 2, 1 instead of snapping back, and the
   cache entry is the unit's slot times twelve plus that frame.  A fold to 0,
   or a wrap, would put a different entry's colour on the cell. */
static void test_the_walk_frame_rocks_and_indexes_the_cache_slot(void)
{
    int base_entry;

    panel_save();
    panel_install();
    base_entry = PANEL_CACHE_SLOT * PANEL_CACHE_SLOT_ENTRIES;

    data_fdps_timer_tick_counter = 0;
    panel_run();
    CHECK_EQ(panel_px(PANEL_CELL_ROW + 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + 2),
             PANEL_CACHE_PIXEL_BASE + base_entry);

    data_fdps_timer_tick_counter = 4;
    panel_run();
    CHECK_EQ(panel_px(PANEL_CELL_ROW + 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + 2),
             PANEL_CACHE_PIXEL_BASE + base_entry + 1);

    data_fdps_timer_tick_counter = 8;
    panel_run();
    CHECK_EQ(panel_px(PANEL_CELL_ROW + 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + 2),
             PANEL_CACHE_PIXEL_BASE + base_entry + 2);

    data_fdps_timer_tick_counter = 12;
    panel_run();
    CHECK_EQ(panel_px(PANEL_CELL_ROW + 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + 2),
             PANEL_CACHE_PIXEL_BASE + base_entry + 1);

    data_fdps_timer_tick_counter = 15;
    panel_run();
    CHECK_EQ(panel_px(PANEL_CELL_ROW + 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + 2),
             PANEL_CACHE_PIXEL_BASE + base_entry + 1);

    data_fdps_timer_tick_counter = 16;
    panel_run();
    CHECK_EQ(panel_px(PANEL_CELL_ROW + 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + 2),
             PANEL_CACHE_PIXEL_BASE + base_entry);
    panel_restore();
}

/* The sprite is a 24 by 24 blit at exactly the cell the terrain tile went
   into, so it covers the tile rather than sitting beside it; the HP figure
   then goes over the sprite's lower rows. */
static void test_the_sprite_covers_the_tile_cell(void)
{
    panel_save();
    panel_install();
    panel_run();

    CHECK_EQ(panel_px(PANEL_CELL_ROW, PANEL_TEST_COLUMN + PANEL_CELL_COL),
             PANEL_CACHE_PIXEL_BASE
                 + PANEL_CACHE_SLOT * PANEL_CACHE_SLOT_ENTRIES);
    CHECK_EQ(panel_px(PANEL_CELL_ROW + PANEL_CELL_SIZE - 1,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL + PANEL_CELL_SIZE - 1),
             PANEL_CACHE_PIXEL_BASE
                 + PANEL_CACHE_SLOT * PANEL_CACHE_SLOT_ENTRIES);
    CHECK_EQ(panel_px(PANEL_CELL_ROW + PANEL_CELL_SIZE,
                      PANEL_TEST_COLUMN + PANEL_CELL_COL),
             PANEL_GUARD);
    panel_restore();
}

/* A unit below full HP has its figure drawn in colour row 3 -- MOV dword ptr
   [0x0006000c],0x3 at 0002df44, guarded by the JZ at 0002df42 -- and the
   figure is zero-padded to four digits, so 12 is "0012" and not "12". */
static void test_a_hurt_unit_draws_its_hp_in_colour_row_three(void)
{
    panel_save();
    panel_install();
    panel_units[0].hp_current = PANEL_HP_HURT;
    panel_run();

    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 0),
             panel_glyph_pixel(PANEL_HURT_ROW, 0));
    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 1),
             panel_glyph_pixel(PANEL_HURT_ROW, 0));
    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 2),
             panel_glyph_pixel(PANEL_HURT_ROW, 1));
    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 3),
             panel_glyph_pixel(PANEL_HURT_ROW, 2));
    CHECK_EQ(data_fdps_number_glyph_color_row, 0);
    panel_restore();
}

/* At full HP the colour row is not written before the figure, so it comes out
   in the ambient row -- and the store of 0 at 0002df74 runs all the same, so
   the function still leaves the row at 0. */
static void test_a_whole_unit_draws_its_hp_in_the_ambient_row(void)
{
    panel_save();
    panel_install();
    panel_run();

    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 0),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, 0));
    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 2),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, 2));
    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 3),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, 0));
    CHECK_EQ(data_fdps_number_glyph_color_row, 0);
    panel_restore();
}

/* The HP comparison is on the two signed 16-bit fields as MOVSX widens them
   (0002df2b, 0002df35), so a unit healed above its maximum counts as hurt too
   -- the test is an inequality, not a "below". */
static void test_hp_over_the_maximum_still_counts_as_hurt(void)
{
    panel_save();
    panel_install();
    panel_units[0].hp_current = PANEL_HP_FULL + 1;
    panel_run();

    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_TEST_COLUMN + PANEL_HP_COL, 3),
             panel_glyph_pixel(PANEL_HURT_ROW, 1));
    CHECK_EQ(data_fdps_number_glyph_color_row, 0);
    panel_restore();
}

/* Every figure and every sprite is placed at the panel column the global
   holds, so parking the panel moves the whole thing together. */
static void test_every_piece_follows_the_panel_column(void)
{
    panel_save();
    panel_install();
    data_fdps_ui_terrain_hud_panel_offset = PANEL_PARK_RIGHT;
    panel_run();

    CHECK_EQ(panel_px(PANEL_WIN_ROW, PANEL_PARK_RIGHT + PANEL_WIN_COL),
             PANEL_WIN_PIXEL);
    CHECK_EQ(panel_px(PANEL_CELL_ROW, PANEL_PARK_RIGHT + PANEL_CELL_COL),
             PANEL_CACHE_PIXEL_BASE
                 + PANEL_CACHE_SLOT * PANEL_CACHE_SLOT_ENTRIES);
    CHECK_EQ(panel_glyph_px(PANEL_AP_ROW, PANEL_PARK_RIGHT + PANEL_AP_COL, 0),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, PANEL_GLYPH_PLUS));
    CHECK_EQ(panel_glyph_px(PANEL_HP_ROW, PANEL_PARK_RIGHT + PANEL_HP_COL, 3),
             panel_glyph_pixel(PANEL_AMBIENT_ROW, 0));
    CHECK_EQ(panel_px(PANEL_WIN_ROW, PANEL_TEST_COLUMN + PANEL_WIN_COL),
             PANEL_GUARD);
    panel_restore();
}

void run_mapcur_tests(void)
{
    mapcur_stage_sheet();
    mapcur_stage_grid();
    panel_stage_sheets();
    panel_stage_map();

    RUN_TEST(test_mode_one_draws_sprite_zero_on_the_cursor_tile);
    RUN_TEST(test_mode_two_draws_sprite_one_on_the_cursor_tile);
    RUN_TEST(test_mode_three_draws_the_radius_one_diamond);
    RUN_TEST(test_mode_three_leaves_the_diagonals_blank);
    RUN_TEST(test_mode_four_draws_the_radius_two_diamond);
    RUN_TEST(test_mode_four_leaves_the_outer_ring_blank);
    RUN_TEST(test_mode_five_draws_the_radius_three_diamond);
    RUN_TEST(test_mode_five_lower_diagonals_take_their_ids_out_of_order);
    RUN_TEST(test_mode_five_leaves_the_orthogonal_neighbours_blank);
    RUN_TEST(test_unnamed_modes_draw_nothing);
    RUN_TEST(test_the_view_origin_moves_the_tile_in_the_scene);
    RUN_TEST(test_cells_outside_the_window_are_dropped_whole);
    RUN_TEST(test_mode_six_clears_the_marker_and_draws_nothing);
    RUN_TEST(test_mode_six_indexes_through_the_header_width);
    RUN_TEST(test_mode_six_takes_the_tile_the_pixel_lies_in);
    RUN_TEST(test_mode_six_divides_negative_coordinates_towards_zero);
    RUN_TEST(test_the_drawing_modes_leave_the_grid_alone);

    RUN_TEST(test_the_record_fields_sit_where_the_panel_reads_them);
    RUN_TEST(test_the_user_toggle_off_draws_nothing_at_all);
    RUN_TEST(test_the_map_not_being_live_draws_nothing_at_all);
    RUN_TEST(test_the_panel_parks_right_under_a_low_left_cursor);
    RUN_TEST(test_the_panel_parks_left_under_a_low_right_cursor);
    RUN_TEST(test_a_middle_cursor_leaves_the_panel_where_it_was);
    RUN_TEST(test_the_dodge_is_window_relative_and_the_lookup_is_not);
    RUN_TEST(test_the_background_and_the_tile_graphic_land_on_the_panel);
    RUN_TEST(test_the_two_terrain_figures_come_from_their_own_tables);
    RUN_TEST(test_the_terrain_figures_keep_the_ambient_colour_row);
    RUN_TEST(test_an_empty_tile_draws_neither_sprite_nor_hp);
    RUN_TEST(test_the_walk_frame_rocks_and_indexes_the_cache_slot);
    RUN_TEST(test_the_sprite_covers_the_tile_cell);
    RUN_TEST(test_a_hurt_unit_draws_its_hp_in_colour_row_three);
    RUN_TEST(test_a_whole_unit_draws_its_hp_in_the_ambient_row);
    RUN_TEST(test_hp_over_the_maximum_still_counts_as_hurt);
    RUN_TEST(test_every_piece_follows_the_panel_column);
}
