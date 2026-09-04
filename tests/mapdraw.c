/* tests/mapdraw.c -- cover for src/mapdraw.c.
 *
 * Every expected value below is worked out from the assembly at 0002c220 and
 * 0002c330 by hand; none of them is read off the emitted C.
 *
 * The globals the two functions read are staged here rather than asserted on:
 * the layer count, the depth bytes, the tick counter, the two animation
 * globals and the palette tables are what a chapter load and the timer put
 * there, and ticket 23 owns their contents.  Writing them is the only way to
 * reach the bodies at all.
 *
 * fdps_draw_scene_layer is exercised against the real fdps_blit_dispatch and
 * the real RLE kernels, because what it decides -- which tile, at which
 * address, through which blit mode, at which blend level -- is only visible in
 * the pixels those kernels leave behind.  The fixtures below are built so that
 * each of those four decisions reads back out of one byte of the scene buffer:
 *
 *   Which tile.  Tile id t's stream fills its whole 24x24 block with the
 *   palette index t + 1, so a drawn pixel names the tile that drew it.  The
 *   stream is 24 rows of the op-0 command 0x17 -- (0x17 & 0x3f) + 1 = 24
 *   pixels of one colour, exactly the row width fdps_blit_dispatch publishes.
 *
 *   At which address.  The scene buffer is filled with a sentinel first, so a
 *   byte that still holds it was never written and a test can pin both where
 *   the window starts and where it stops.
 *
 *   Through which mode, and at which level.  The shade ramp is staged so that
 *   rows 9..17 -- the ones the translucent kernel weighs the SOURCE pixel by
 *   and the tinting kernel weighs the sprite PIXEL by -- hold level << 4 for
 *   every one of their 256 entries, and rows 0..8 hold zero everywhere except
 *   at TINT_COLOR, where they hold 0x10.  Against a destination byte of 0 that
 *   makes mode 9 resolve cube entry `level` and mode 0x0a resolve cube entry
 *   `level + 1`, and the cube is staged as 0xa0 + index.  So a blended pixel
 *   of 0xa5 says "mode 9 at level 5" and one of 0xa6 says "mode 0x0a at level
 *   5", which is what lets a test see a blend level and a blit mode at all.
 */
#include "testharn.h"
#include "gamedata.h"
#include "mapdraw.h"

#define ORDER_SLOTS 8
#define SENTINEL 0x7bad

static int order[ORDER_SLOTS];

/* Put the six depth bytes and the active-slot count in place and poison the
   whole output array, so that a slot holding its slot index afterwards can
   only have got there from the fill loop. */
static void stage(int count, int d0, int d1, int d2, int d3, int d4, int d5)
{
    int i;

    data_fdps_scene_layer_count = count;
    data_fdps_scene_layer_draw_depth[0] = (unsigned char) d0;
    data_fdps_scene_layer_draw_depth[1] = (unsigned char) d1;
    data_fdps_scene_layer_draw_depth[2] = (unsigned char) d2;
    data_fdps_scene_layer_draw_depth[3] = (unsigned char) d3;
    data_fdps_scene_layer_draw_depth[4] = (unsigned char) d4;
    data_fdps_scene_layer_draw_depth[5] = (unsigned char) d5;

    for (i = 0; i < ORDER_SLOTS; i++) {
        order[i] = SENTINEL;
    }
}

/* Depths already ascending: the fill loop's identity permutation survives the
   sort untouched, because no comparison is ever a strict greater-than. */
static void test_ascending_depths_keep_identity_order(void)
{
    stage(6, 0, 1, 2, 3, 4, 5);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], 1);
    CHECK_EQ(order[2], 2);
    CHECK_EQ(order[3], 3);
    CHECK_EQ(order[4], 4);
    CHECK_EQ(order[5], 5);
    CHECK_EQ(order[6], SENTINEL);
    CHECK_EQ(order[7], SENTINEL);
}

/* Depths strictly descending: every pass moves the current maximum to the far
   end, and the six passes' worth of bound shrinking still leaves the list
   fully reversed -- which is the check that the outer bound is count - 1 and
   not something shorter. */
static void test_descending_depths_reverse_the_list(void)
{
    stage(6, 50, 40, 30, 20, 10, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 5);
    CHECK_EQ(order[1], 4);
    CHECK_EQ(order[2], 3);
    CHECK_EQ(order[3], 2);
    CHECK_EQ(order[4], 1);
    CHECK_EQ(order[5], 0);
    CHECK_EQ(order[6], SENTINEL);
    CHECK_EQ(order[7], SENTINEL);
}

/* All six depths equal.  The swap fires only on a strict greater-than (JBE
   skips it), so the identity order is kept exactly -- the stability the plate
   comment and rebuild_info/pitfalls.md say fdps_draw_scene_layers depends on
   for which of two equal-depth layers covers the other. */
static void test_equal_depths_are_left_in_slot_order(void)
{
    stage(6, 7, 7, 7, 7, 7, 7);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], 1);
    CHECK_EQ(order[2], 2);
    CHECK_EQ(order[3], 3);
    CHECK_EQ(order[4], 4);
    CHECK_EQ(order[5], 5);
}

/* Two depth values interleaved -- slots 1, 3, 5 at depth 1 and slots 0, 2, 4
   at depth 2.  Hand-running the six passes gives 1, 3, 5, 0, 2, 4: each group
   comes out in ascending slot order, which an unstable sort is free not to do
   even while producing a correctly sorted key sequence. */
static void test_ties_keep_slot_order_within_each_depth(void)
{
    stage(6, 2, 1, 2, 1, 2, 1);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 3);
    CHECK_EQ(order[2], 5);
    CHECK_EQ(order[3], 0);
    CHECK_EQ(order[4], 2);
    CHECK_EQ(order[5], 4);
}

/* The signedness of the key (contract C).  Slot 0's depth is 0x80 and slot 1's
   is 0x01.  CMP AL,... / JBE is the unsigned compare, so 0x80 is 128, the swap
   fires and slot 1 comes first.  Read through a signed char 0x80 would be -128
   and the pair would come out 0, 1 instead. */
static void test_depth_key_is_compared_unsigned(void)
{
    stage(2, 0x80, 0x01, 0, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 0);
    CHECK_EQ(order[2], SENTINEL);
}

/* The same question at the other end of the byte: 0xff against 0xfe.  Unsigned
   these differ by one and no swap fires; signed they are -1 and -2 and the
   pair would be exchanged. */
static void test_high_depth_bytes_order_by_magnitude(void)
{
    stage(2, 0xfe, 0xff, 0, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], 1);
    CHECK_EQ(order[2], SENTINEL);
}

/* A count below the array width.  Three slots are filled and sorted -- depths
   2, 0, 1 give 1, 2, 0 -- and nothing is written past index 2, even though the
   depth bytes of slots 3 to 5 would sort earlier if the loops ran to six. */
static void test_only_the_active_slots_are_written(void)
{
    stage(3, 2, 0, 1, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 2);
    CHECK_EQ(order[2], 0);
    CHECK_EQ(order[3], SENTINEL);
    CHECK_EQ(order[4], SENTINEL);
    CHECK_EQ(order[5], SENTINEL);
}

/* One active slot.  The fill loop writes index 0, the outer pass test
   count - 1 > 0 is false at once and the inner loop never runs. */
static void test_single_slot_writes_one_entry(void)
{
    stage(1, 9, 0, 0, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], SENTINEL);
}

/* No active slots.  Every one of the three loop tests fails on entry, so the
   caller's array comes back exactly as it went in -- the case a rewrite using
   a do/while or a count - 1 unsigned bound would get wrong. */
static void test_zero_count_writes_nothing(void)
{
    stage(0, 3, 2, 1, 0, 0, 0);
    fdps_build_scene_layer_draw_order(order);
    CHECK_EQ(order[0], SENTINEL);
    CHECK_EQ(order[1], SENTINEL);
    CHECK_EQ(order[2], SENTINEL);
}

/* ------------------------------------------------------------------ */
/* fdps_draw_scene_layer @ 0002c330                                    */
/* ------------------------------------------------------------------ */

/* The scene buffer is exactly as large as the window can reach: nine rows of
   24 starting 24 pixels down, plus the 24 rows of the last tile, is 240 rows
   of the 360-byte pitch, and fourteen columns of 24 starting 24 across is the
   whole 360. */
#define SCENE_ROWS 240
#define SCENE_PITCH 360
#define TILE 24
#define WINDOW_COLS 14
#define WINDOW_ROWS 9

/* Big enough for the largest map any case below stages, and for a tile id
   pushed 3 * 0x60 forward by the four-frame animation. */
#define MAP_MAX_DIM 16
#define MAP_CELLS (MAP_MAX_DIM * MAP_MAX_DIM)
/* The offset table has to cover the largest tile id anything below can
   form, which is the largest id a 16x16 map holds plus the four-frame
   animation's 3 * 0x60: 255 + 288. */
#define TILESET_TILES 576
#define TILE_STREAM_BYTES (TILE * 2)
#define CEL_TABLE_AT 0x0f
#define STREAMS_AT (CEL_TABLE_AT + TILESET_TILES * 4)
#define ATTR_HEADER 0x11

#define SCENE_UNTOUCHED 0x5a
#define TINT_COLOR 200
#define CUBE_BASE 0xa0

static unsigned char scene[SCENE_ROWS * SCENE_PITCH];
static unsigned char tile_map[0x0b + MAP_CELLS * 2];
static unsigned char move_grid[4 + MAP_CELLS * 2];
static unsigned char tile_attr[ATTR_HEADER + TILESET_TILES * 4];
static unsigned char tileset[STREAMS_AT + TILESET_TILES * TILE_STREAM_BYTES];

/* Tile id t paints palette index t + 1, so a pixel names its tile. */
static int tile_color(int tile_id)
{
    return (tile_id + 1) & 0xff;
}

static int pixel_at(int y, int x)
{
    return scene[y * SCENE_PITCH + x];
}

/* One 24x24 block of a single colour per tile, and the .CEL offset table that
   points at each block from the sheet base. */
static void build_tileset(void)
{
    int tile_id;
    int row;
    int stream_at;

    for (tile_id = 0; tile_id < TILESET_TILES; tile_id++) {
        stream_at = STREAMS_AT + tile_id * TILE_STREAM_BYTES;
        *(int *) (tileset + CEL_TABLE_AT + tile_id * 4) = stream_at;
        for (row = 0; row < TILE; row++) {
            tileset[stream_at + row * 2] = 0x17;
            tileset[stream_at + row * 2 + 1] = (unsigned char) tile_color(tile_id);
        }
    }
}

/* A width x height map whose cell (x, y) holds the tile id y * width + x, so a
   drawn colour names the cell it came from as well as the tile. */
static void build_map(int width, int height)
{
    int x;
    int y;

    *(short *) (tile_map + 7) = (short) width;
    *(short *) (tile_map + 9) = (short) height;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            *(short *) (tile_map + 0x0b + (y * width + x) * 2) =
                (short) (y * width + x);
        }
    }
}

/* Every grid cell gets the same marker byte.  Byte 0 is left at a value that
   is not read, so a reader that took the wrong byte of the cell would see it. */
static void stage_grid(int marker)
{
    int i;

    for (i = 0; i < MAP_CELLS; i++) {
        move_grid[4 + i * 2] = 0x33;
        move_grid[4 + i * 2 + 1] = (unsigned char) marker;
    }
}

/* Every attribute row gets the same flags and level.  The 0x11-byte header is
   filled with 0xff, so a body that failed to step past it would read flags
   0xff -- translucent, at level 0xff -- rather than what is staged here. */
static void stage_attr(int flags, int level)
{
    int i;

    for (i = 0; i < ATTR_HEADER; i++) {
        tile_attr[i] = 0xff;
    }
    for (i = 0; i < TILESET_TILES; i++) {
        tile_attr[ATTR_HEADER + i * 4] = (unsigned char) flags;
        tile_attr[ATTR_HEADER + i * 4 + 1] = (unsigned char) level;
        tile_attr[ATTR_HEADER + i * 4 + 2] = 0;
        tile_attr[ATTR_HEADER + i * 4 + 3] = 0;
    }
}

static void fill_scene(int value)
{
    int i;

    for (i = 0; i < SCENE_ROWS * SCENE_PITCH; i++) {
        scene[i] = (unsigned char) value;
    }
}

/* The ramp and cube staging this file's header comment describes: a blended
   pixel comes back as CUBE_BASE + level for mode 9 and CUBE_BASE + level + 1
   for mode 0x0a, against a destination byte of 0. */
static void stage_blend_tables(void)
{
    int row;
    int i;

    for (i = 0; i < 18 * 256; i++) {
        data_fdps_palette_shade_ramp_table[i] = 0;
    }
    for (row = 0; row <= 8; row++) {
        data_fdps_palette_shade_ramp_table[row * 256 + TINT_COLOR] = 0x10;
        for (i = 0; i < 256; i++) {
            data_fdps_palette_shade_ramp_table[(row + 9) * 256 + i] =
                (unsigned int) (row << 4);
        }
    }
    for (i = 0; i < 4096; i++) {
        data_fdps_inverse_palette_cube[i] =
            (unsigned char) (CUBE_BASE + (i & 0x0f));
    }
    data_fdps_scene_marked_tile_tint_color = TINT_COLOR;
}

/* The state every drawing case starts from: a 16x16 map, an unmarked grid, no
   attribute flags, and a tick counter latched so that neither the animation
   phase nor the latch moves during the call. */
static void stage_scene(void)
{
    build_tileset();
    build_map(MAP_MAX_DIM, MAP_MAX_DIM);
    stage_grid(0xff);
    stage_attr(0, 0);
    fill_scene(SCENE_UNTOUCHED);
    stage_blend_tables();
    data_fdps_marked_tile_blend_phase = 0;
    data_fdps_scene_tile_anim_phase = 0;
    data_fdps_timer_tick_counter = 100;
    data_fdps_scene_tile_anim_last_flip_tick = 100;
}

/* The highlight phase steps by one and wraps at 0x38 -- INC EDX / IDIV EBX
   with EBX 0x38 at 0002c353, and it happens once per CALL, which is once per
   layer.  layer_mode 0 is used so that nothing reads the ramp the phase
   indexes; only the counter is under test. */
static void test_highlight_phase_steps_once_per_call_and_wraps(void)
{
    stage_scene();

    data_fdps_marked_tile_blend_phase = 5;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);
    CHECK_EQ(data_fdps_marked_tile_blend_phase, 6);

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);
    CHECK_EQ(data_fdps_marked_tile_blend_phase, 7);

    data_fdps_marked_tile_blend_phase = 0x37;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);
    CHECK_EQ(data_fdps_marked_tile_blend_phase, 0);
}

/* A latch of zero means "never latched": it is seeded from the tick counter
   (CMP dword ptr [0x00060164],0x0 / JNZ at 0002c37c) before the difference is
   taken, so the very first call of a session cannot flip the phase however
   large the tick counter already is. */
static void test_zero_latch_is_seeded_and_does_not_flip(void)
{
    stage_scene();
    data_fdps_scene_tile_anim_last_flip_tick = 0;
    data_fdps_timer_tick_counter = 5000;
    data_fdps_scene_tile_anim_phase = 0;

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);
    CHECK_EQ(data_fdps_scene_tile_anim_last_flip_tick, 5000);
    CHECK_EQ(data_fdps_scene_tile_anim_phase, 0);
}

/* CMP EAX,0x3 / JBE at 0002c39a: the flip needs the difference to be strictly
   greater than three, and the latch is re-seeded only when it fires. */
static void test_anim_phase_flips_only_past_three_ticks(void)
{
    stage_scene();
    data_fdps_scene_tile_anim_last_flip_tick = 100;
    data_fdps_scene_tile_anim_phase = 0;

    data_fdps_timer_tick_counter = 103;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);
    CHECK_EQ(data_fdps_scene_tile_anim_phase, 0);
    CHECK_EQ(data_fdps_scene_tile_anim_last_flip_tick, 100);

    data_fdps_timer_tick_counter = 104;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);
    CHECK_EQ(data_fdps_scene_tile_anim_phase, 1);
    CHECK_EQ(data_fdps_scene_tile_anim_last_flip_tick, 104);

    data_fdps_timer_tick_counter = 109;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);
    CHECK_EQ(data_fdps_scene_tile_anim_phase, 0);
    CHECK_EQ(data_fdps_scene_tile_anim_last_flip_tick, 109);
}

/* Contract C on the tick difference.  A latch of 1 against a counter of
   0x80000001 leaves a difference of 0x80000000, which the unsigned JBE takes
   as far more than three and flips.  Read signed the same bits are
   -2147483648, which is not greater than three, and the phase would sit
   still. */
static void test_tick_difference_is_compared_unsigned(void)
{
    stage_scene();
    data_fdps_scene_tile_anim_last_flip_tick = 1;
    data_fdps_timer_tick_counter = 0x80000001u;
    data_fdps_scene_tile_anim_phase = 0;

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);
    CHECK_EQ(data_fdps_scene_tile_anim_phase, 1);
    CHECK_EQ(data_fdps_scene_tile_anim_last_flip_tick, 0x80000001u);
}

/* layer_mode 0 with no scroll: the window is inset 24 pixels into the buffer
   in both directions, each cell sits 24 further on, and the fourteenth column
   and ninth row land exactly on the buffer's far edge.  Both the attribute
   flags and the grid markers are staged to values that would change every draw
   if they were read, which they are not in this mode.  The sentinel checks pin
   where the window stops: nothing above row 24, nothing left of column 24. */
static void test_layer_mode_zero_fills_the_window_opaque(void)
{
    stage_scene();
    stage_attr(0x80, 4);
    stage_grid(0x01);

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);

    CHECK_EQ(pixel_at(TILE, TILE), tile_color(0));
    CHECK_EQ(pixel_at(TILE + TILE - 1, TILE + TILE - 1), tile_color(0));
    CHECK_EQ(pixel_at(TILE, TILE + TILE), tile_color(1));
    CHECK_EQ(pixel_at(TILE + TILE, TILE), tile_color(MAP_MAX_DIM));
    CHECK_EQ(pixel_at(TILE + 8 * TILE, TILE + 13 * TILE),
             tile_color(8 * MAP_MAX_DIM + 13));
    CHECK_EQ(pixel_at(SCENE_ROWS - 1, SCENE_PITCH - 1),
             tile_color(8 * MAP_MAX_DIM + 13));

    CHECK_EQ(pixel_at(TILE - 1, TILE), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(TILE, TILE - 1), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(0, 0), SCENE_UNTOUCHED);
}

/* A positive scroll that is not a whole tile: 30 pixels is one tile and six
   pixels, so the window's first column is cell 1 drawn at x = 24 - 6, and
   nothing is written left of it. */
static void test_positive_scroll_splits_into_tiles_and_pixels(void)
{
    stage_scene();

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 30, 0,
                          tile_attr, 0);

    CHECK_EQ(pixel_at(TILE, TILE - 6), tile_color(1));
    CHECK_EQ(pixel_at(TILE, TILE - 7), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(TILE, TILE - 6 + TILE), tile_color(2));
}

/* A negative scroll on both axes.  x = -30 truncates to -1 remainder -6, and
   the sign fix-up makes it tile -2 and pixel 18, so column 0 starts at x = 6
   and names cell x -2, which wraps to 14.  y = -1 truncates to 0 remainder -1
   and becomes tile -1, pixel 23, so row 0 starts at scanline 1 and names cell
   y 15.  The tile drawn there is 15 * 16 + 14. */
static void test_negative_scroll_floors_the_tile_count(void)
{
    stage_scene();

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, -30, -1,
                          tile_attr, 0);

    CHECK_EQ(pixel_at(1, 6), tile_color(15 * MAP_MAX_DIM + 14));
    CHECK_EQ(pixel_at(0, 6), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(1, 5), SCENE_UNTOUCHED);
}

/* The fix-up at an exact negative multiple, which is where it looks wrong and
   is not.  x = -24 truncates to -1 remainder 0, and the decrement and the + 24
   still fire: tile -2, pixel 24, so column 0 starts at x = 0 and shows cell 14.
   A real floor would leave tile -1, pixel 0, start column 0 at x = 24 and put
   the fourteenth column at x = 336.  Both agree about what sits at x = 24; the
   two sentinel checks are what tell them apart. */
static void test_exact_negative_multiple_still_takes_the_fix_up(void)
{
    stage_scene();

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, -24, 0,
                          tile_attr, 0);

    CHECK_EQ(pixel_at(TILE, 0), tile_color(14));
    CHECK_EQ(pixel_at(TILE, TILE), tile_color(15));
    CHECK_EQ(pixel_at(TILE, 13 * TILE), tile_color(11));
    CHECK_EQ(pixel_at(TILE, SCENE_PITCH - 1), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(TILE, 14 * TILE), SCENE_UNTOUCHED);
}

/* A map smaller than the window, so both coordinates wrap.  Columns 0..2 take
   the range test's fast path and columns 3..13 go through the modulo; the same
   split happens down the rows.  Cell (col % 3, row % 3) is tile
   (row % 3) * 3 + (col % 3). */
static void test_cell_coordinates_wrap_modulo_the_map(void)
{
    stage_scene();
    build_map(3, 3);

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 0);

    CHECK_EQ(pixel_at(TILE, TILE + 2 * TILE), tile_color(2));
    CHECK_EQ(pixel_at(TILE, TILE + 3 * TILE), tile_color(0));
    CHECK_EQ(pixel_at(TILE + 5 * TILE, TILE + 4 * TILE), tile_color(7));
    CHECK_EQ(pixel_at(TILE + 8 * TILE, TILE + 13 * TILE), tile_color(7));
}

/* layer_mode 1 with the flags clear and every cell unmarked draws exactly the
   opaque picture layer_mode 0 does: the 0xff marker is fdps_map_grid_reset's
   "no movement range here" and takes the mode-0 blit at 0002c627. */
static void test_unmarked_cells_draw_opaque_in_attribute_mode(void)
{
    stage_scene();

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 1);

    CHECK_EQ(pixel_at(TILE, TILE), tile_color(0));
    CHECK_EQ(pixel_at(TILE + 8 * TILE, TILE + 13 * TILE),
             tile_color(8 * MAP_MAX_DIM + 13));
}

/* Animation kind 1 adds the two-frame phase to the tile id, so the same map
   cell draws tile 0 at phase 0 and tile 1 at phase 1.  The tick state is held
   still either side so the phase under test is the one that was staged. */
static void test_two_frame_animation_adds_the_phase(void)
{
    stage_scene();
    stage_attr(1, 0);

    data_fdps_scene_tile_anim_phase = 0;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 1);
    CHECK_EQ(pixel_at(TILE, TILE), tile_color(0));

    data_fdps_scene_tile_anim_phase = 1;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 1);
    CHECK_EQ(pixel_at(TILE, TILE), tile_color(1));
}

/* Animation kind 2 takes the tick counter's bits 2 and 3 and steps the tile id
   by 0x60 per frame: SHR EAX,0x2 / AND EAX,0x3 / IMUL EAX,EAX,0x60 at
   0002c542.  Tick 8 selects frame 2 and tick 108 selects frame 3, and the
   latch is moved with the counter so no flip interferes. */
static void test_four_frame_animation_steps_by_0x60(void)
{
    stage_scene();
    stage_attr(2, 0);

    data_fdps_timer_tick_counter = 8;
    data_fdps_scene_tile_anim_last_flip_tick = 8;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 1);
    CHECK_EQ(pixel_at(TILE, TILE), tile_color(2 * 0x60));

    data_fdps_timer_tick_counter = 12;
    data_fdps_scene_tile_anim_last_flip_tick = 12;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 1);
    CHECK_EQ(pixel_at(TILE, TILE), tile_color(3 * 0x60));

    data_fdps_timer_tick_counter = 16;
    data_fdps_scene_tile_anim_last_flip_tick = 16;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 1);
    CHECK_EQ(pixel_at(TILE, TILE), tile_color(0));
}

/* Bit 0x80 draws the tile translucent at the level in the attribute row, and
   the row it takes that level from is the ANIMATED tile's, not the one the
   flags came from.  Row 0 carries flags 0x81 -- translucent plus the two-frame
   animation -- and level 3; row 1 carries level 6.  With the phase at 1 the
   tile drawn is 1, so the level is 6 and the blended pixel is 0xa6; a body that
   read the level from row 0 would leave 0xa3.

   Every cell is staged unmarked, so this also pins that 0x80 is tested before
   the grid is consulted. */
static void test_translucent_flag_takes_the_level_of_the_animated_row(void)
{
    stage_scene();
    stage_attr(0, 0);
    fill_scene(0);
    tile_attr[ATTR_HEADER + 0 * 4] = 0x81;
    tile_attr[ATTR_HEADER + 0 * 4 + 1] = 3;
    tile_attr[ATTR_HEADER + 1 * 4] = 0;
    tile_attr[ATTR_HEADER + 1 * 4 + 1] = 6;
    data_fdps_scene_tile_anim_phase = 1;

    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 2);

    CHECK_EQ(pixel_at(TILE, TILE), CUBE_BASE + 6);
    CHECK_EQ(pixel_at(TILE, TILE + TILE), tile_color(1));
}

/* A marked cell -- any marker but 0xff -- pulses at blend_ramp[phase / 4] out
   of the 14-byte table {2,3,4,5,6,7,7,7,6,5,4,3,2,2}.  The phase is staged one
   short of the value under test because the body steps it before reading.
   Phase 12 gives ramp[3] = 5, phase 28 gives ramp[7] = 7 and phase 52 gives
   ramp[13] = 2, the far end of the ramp that a table one entry short would
   miss. */
static void test_marked_cells_pulse_through_the_blend_ramp(void)
{
    stage_scene();
    fill_scene(0);
    stage_grid(0x01);

    data_fdps_marked_tile_blend_phase = 11;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 2);
    CHECK_EQ(pixel_at(TILE, TILE), CUBE_BASE + 5);

    data_fdps_marked_tile_blend_phase = 27;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 2);
    CHECK_EQ(pixel_at(TILE, TILE), CUBE_BASE + 7);

    data_fdps_marked_tile_blend_phase = 51;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 2);
    CHECK_EQ(pixel_at(TILE, TILE), CUBE_BASE + 2);
}

/* Which blit mode a marked cell goes through is layer_mode's alone: 1 is mode
   0x0a, which weighs the tint colour in, and every other non-zero value is
   mode 9, which weighs the destination byte in instead.  Staged against a
   destination of 0 the two come back one cube entry apart. */
static void test_layer_mode_one_tints_and_other_modes_blend(void)
{
    stage_scene();
    fill_scene(0);
    stage_grid(0x01);

    data_fdps_marked_tile_blend_phase = 11;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 2);
    CHECK_EQ(pixel_at(TILE, TILE), CUBE_BASE + 5);

    fill_scene(0);
    data_fdps_marked_tile_blend_phase = 11;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 1);
    CHECK_EQ(pixel_at(TILE, TILE), CUBE_BASE + 5 + 1);

    fill_scene(0);
    data_fdps_marked_tile_blend_phase = 11;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 7);
    CHECK_EQ(pixel_at(TILE, TILE), CUBE_BASE + 5);
}

/* The marker is read per cell, not per layer: only cell (0, 0) is marked, and
   its neighbours keep the opaque draw.  It is byte 1 of the cell that decides
   -- byte 0 is staged to 0x33 throughout, which as a marker would mark
   everything. */
static void test_only_marked_cells_leave_the_opaque_path(void)
{
    stage_scene();
    fill_scene(0);
    move_grid[4 + 1] = 0x01;

    data_fdps_marked_tile_blend_phase = 11;
    fdps_draw_scene_layer(scene, tile_map, tileset, move_grid, 0, 0,
                          tile_attr, 2);

    CHECK_EQ(pixel_at(TILE, TILE), CUBE_BASE + 5);
    CHECK_EQ(pixel_at(TILE, TILE + TILE), tile_color(1));
    CHECK_EQ(pixel_at(TILE + TILE, TILE), tile_color(MAP_MAX_DIM));
}

void run_mapdraw_tests(void)
{
    RUN_TEST(test_ascending_depths_keep_identity_order);
    RUN_TEST(test_descending_depths_reverse_the_list);
    RUN_TEST(test_equal_depths_are_left_in_slot_order);
    RUN_TEST(test_ties_keep_slot_order_within_each_depth);
    RUN_TEST(test_depth_key_is_compared_unsigned);
    RUN_TEST(test_high_depth_bytes_order_by_magnitude);
    RUN_TEST(test_only_the_active_slots_are_written);
    RUN_TEST(test_single_slot_writes_one_entry);
    RUN_TEST(test_zero_count_writes_nothing);

    RUN_TEST(test_highlight_phase_steps_once_per_call_and_wraps);
    RUN_TEST(test_zero_latch_is_seeded_and_does_not_flip);
    RUN_TEST(test_anim_phase_flips_only_past_three_ticks);
    RUN_TEST(test_tick_difference_is_compared_unsigned);
    RUN_TEST(test_layer_mode_zero_fills_the_window_opaque);
    RUN_TEST(test_positive_scroll_splits_into_tiles_and_pixels);
    RUN_TEST(test_negative_scroll_floors_the_tile_count);
    RUN_TEST(test_exact_negative_multiple_still_takes_the_fix_up);
    RUN_TEST(test_cell_coordinates_wrap_modulo_the_map);
    RUN_TEST(test_unmarked_cells_draw_opaque_in_attribute_mode);
    RUN_TEST(test_two_frame_animation_adds_the_phase);
    RUN_TEST(test_four_frame_animation_steps_by_0x60);
    RUN_TEST(test_translucent_flag_takes_the_level_of_the_animated_row);
    RUN_TEST(test_marked_cells_pulse_through_the_blend_ramp);
    RUN_TEST(test_layer_mode_one_tints_and_other_modes_blend);
    RUN_TEST(test_only_marked_cells_leave_the_opaque_path);
}
