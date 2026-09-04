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

void run_mapcur_tests(void)
{
    mapcur_stage_sheet();
    mapcur_stage_grid();

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
}
