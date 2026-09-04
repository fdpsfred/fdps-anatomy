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
#include "fdpstype.h"
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


/* ------------------------------------------------------------------ */
/* fdps_draw_map_unit @ 0002cda0                                       */
/* ------------------------------------------------------------------ */

/* Four unit records is all any case below needs, and only index 0 is drawn;
   the array is what data_fdps_map_unit_array_ptr is pointed at, so the stride
   under test is struct fdps_unit_record's own 0x50. */
#define UNIT_SLOTS 4

/* The sprite cache reaches index 40 in the cache-slot case (slot 3 of twelve,
   facing 1, frame 1) and index 21 in the facing-above-three case, so 64
   streams cover every index any case forms.  ITS offset table starts at the
   cache base, not at +0x0f: the cache is a table fdps_cache_cel_sprite_group
   builds rather than a loaded .CEL file. */
#define CACHE_SPRITES 64
#define CACHE_STREAMS_AT (CACHE_SPRITES * 4)

/* The shadow sheet and the status-icon sheet are real .CEL sheets, so their
   offset tables sit at +0x0f.  Three shadow frames are reachable and five icon
   slots exist; one spare each keeps a fixture overrun visible. */
#define SHEET_TABLE_AT 0x0f
#define SHADOW_SPRITES 4
#define SHADOW_STREAMS_AT (SHEET_TABLE_AT + SHADOW_SPRITES * 4)
#define ICON_SPRITES 5
#define ICON_ROWS 11
#define ICON_STREAM_BYTES (ICON_ROWS * 2)
#define ICON_STREAMS_AT (SHEET_TABLE_AT + ICON_SPRITES * 4)

/* Icon colours are put well clear of the sprite and shadow colours (1..64 and
   1..4) so a pixel says which sheet drew it. */
#define ICON_COLOR_BASE 0x80

static struct fdps_unit_record units[UNIT_SLOTS];
static unsigned char sprite_cache[CACHE_STREAMS_AT
                                  + CACHE_SPRITES * TILE_STREAM_BYTES];
static unsigned char shadow_sheet[SHADOW_STREAMS_AT
                                  + SHADOW_SPRITES * TILE_STREAM_BYTES];
static unsigned char icon_sheet[ICON_STREAMS_AT
                                + ICON_SPRITES * ICON_STREAM_BYTES];

/* Sprite index i paints palette index i + 1 over its whole 24x24 block, and
   shadow frame k likewise paints k + 1, so an opaque pixel names the stream
   that drew it.  Icon slot k paints ICON_COLOR_BASE + k over 24x11. */
static int sprite_color(int sprite_index)
{
    return (sprite_index + 1) & 0xff;
}

static void build_unit_sheets(void)
{
    int i;
    int row;
    int stream_at;

    for (i = 0; i < CACHE_SPRITES; i++) {
        stream_at = CACHE_STREAMS_AT + i * TILE_STREAM_BYTES;
        *(int *) (sprite_cache + i * 4) = stream_at;
        for (row = 0; row < TILE; row++) {
            sprite_cache[stream_at + row * 2] = 0x17;
            sprite_cache[stream_at + row * 2 + 1] =
                (unsigned char) sprite_color(i);
        }
    }

    for (i = 0; i < SHADOW_SPRITES; i++) {
        stream_at = SHADOW_STREAMS_AT + i * TILE_STREAM_BYTES;
        *(int *) (shadow_sheet + SHEET_TABLE_AT + i * 4) = stream_at;
        for (row = 0; row < TILE; row++) {
            shadow_sheet[stream_at + row * 2] = 0x17;
            shadow_sheet[stream_at + row * 2 + 1] =
                (unsigned char) sprite_color(i);
        }
    }

    for (i = 0; i < ICON_SPRITES; i++) {
        stream_at = ICON_STREAMS_AT + i * ICON_STREAM_BYTES;
        *(int *) (icon_sheet + SHEET_TABLE_AT + i * 4) = stream_at;
        for (row = 0; row < ICON_ROWS; row++) {
            icon_sheet[stream_at + row * 2] = 0x17;
            icon_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (ICON_COLOR_BASE + i);
        }
    }
}

/* The blend staging for this half of the file.  fdps_draw_map_unit asks for
   three non-opaque draws and no two of them read the same pair of shade-ramp
   rows, which is what lets one staging serve all three:

     mode 9 at level 7   -- the semi-transparent portrait ids.  Source through
                            row 16, destination through row 7.  Row 16 is a
                            constant 0x70 everywhere and row 7 is zero, so the
                            pixel comes back CUBE_BASE + 7 whatever the sprite
                            and whatever the destination held.
     mode 9 at level 0xa -- the shadow.  Source through row 6, destination
                            through row 15.  Row 6 carries (colour & 0x0f) << 4
                            and row 15 is zero, so the pixel comes back
                            CUBE_BASE + (shadow colour & 0x0f) and names the
                            shadow frame that drew it.
     mode 0x0b at level 8 -- the unit that has already acted.  Tint colour 0
                            through row 8, which is zero, and the pixel through
                            row 17, which carries (colour & 0x0f) << 4, so the
                            pixel comes back CUBE_BASE + (sprite colour & 0x0f)
                            and names the sprite that drew it.

   Rows 7, 8 and 15 staying zero is what makes each of the three a single term,
   and the cube is CUBE_BASE + (index & 0x0f) as it is for the layer cases. */
static void stage_unit_blend_tables(void)
{
    int i;

    for (i = 0; i < 18 * 256; i++) {
        data_fdps_palette_shade_ramp_table[i] = 0;
    }
    for (i = 0; i < 256; i++) {
        data_fdps_palette_shade_ramp_table[6 * 256 + i] =
            (unsigned int) ((i & 0x0f) << 4);
        data_fdps_palette_shade_ramp_table[16 * 256 + i] = 0x70;
        data_fdps_palette_shade_ramp_table[17 * 256 + i] =
            (unsigned int) ((i & 0x0f) << 4);
    }
    for (i = 0; i < 4096; i++) {
        data_fdps_inverse_palette_cube[i] =
            (unsigned char) (CUBE_BASE + (i & 0x0f));
    }
}

/* The state every unit case starts from: one unit at tile (0, 0) facing down,
   standing still, opaque portrait id 0, no ailments; the camera at the origin;
   the sprite pass selected; and the tick already latched, so the walk clock
   does not move under a case that is not testing it. */
static void stage_unit(void)
{
    unsigned char *record_bytes;
    int i;

    build_unit_sheets();
    stage_unit_blend_tables();
    fill_scene(SCENE_UNTOUCHED);

    record_bytes = (unsigned char *) units;
    for (i = 0; i < (int) sizeof(units); i++) {
        record_bytes[i] = 0;
    }

    data_fdps_map_unit_array_ptr = (unsigned char *) units;
    data_fdps_map_unit_count = UNIT_SLOTS;
    data_fdps_cel_sprite_cache_ptr = sprite_cache;
    data_fdps_shadow_sprite_sheet_ptr = shadow_sheet;
    data_fdps_unit_status_icon_sheet_ptr = icon_sheet;

    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_unit_shadow_pass_flag = 0;
    data_fdps_map_unit_walk_anim_counter = 0;
    data_fdps_map_unit_status_icon_tick_counter = 0;
    data_fdps_map_unit_status_icon_cycle = 0;
    data_fdps_timer_tick_counter = 100;
    data_fdps_map_unit_anim_last_tick = 100;
}

/* The clock steps once when the tick has moved and not at all when it has not
   -- CMP EAX,dword ptr [0x00069d18] / JZ at 0002cdd1 -- which is the whole
   point of the latch: fdps_draw_map_units calls this routine twice per unit
   per frame and the walk cycle still advances once.  Portrait id 0x80 sends
   the body home right after the clock, so nothing but the clock is in play. */
static void test_walk_clock_steps_only_when_the_tick_moves(void)
{
    stage_unit();
    units[0].portrait_id = 0x80;
    data_fdps_map_unit_walk_anim_counter = 3;
    data_fdps_map_unit_status_icon_tick_counter = 7;
    data_fdps_timer_tick_counter = 101;

    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(data_fdps_map_unit_anim_last_tick, 101);
    CHECK_EQ(data_fdps_map_unit_walk_anim_counter, 4);
    CHECK_EQ(data_fdps_map_unit_status_icon_tick_counter, 8);

    fdps_draw_map_unit(0, scene, 0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(data_fdps_map_unit_walk_anim_counter, 4);
    CHECK_EQ(data_fdps_map_unit_status_icon_tick_counter, 8);
}

/* The walk counter wraps at 0x10 (IDIV by 0x10 at 0002cdf6), which is what
   makes the four walk frames repeat every sixteen ticks. */
static void test_walk_counter_wraps_at_sixteen(void)
{
    stage_unit();
    units[0].portrait_id = 0x80;
    data_fdps_map_unit_walk_anim_counter = 15;
    data_fdps_timer_tick_counter = 101;

    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(data_fdps_map_unit_walk_anim_counter, 0);
}

/* The status-icon counter wraps at 0x19 and the cycle is bumped only on the
   wrap to zero (CMP dword ptr [0x00069d1c],0x0 / JNZ at 0002ce17), so a unit
   carrying more than one ailment shows the next of them every 25 ticks. */
static void test_icon_counter_wraps_and_bumps_the_cycle(void)
{
    stage_unit();
    units[0].portrait_id = 0x80;
    data_fdps_map_unit_status_icon_tick_counter = 23;
    data_fdps_map_unit_status_icon_cycle = 5;

    data_fdps_timer_tick_counter = 101;
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(data_fdps_map_unit_status_icon_tick_counter, 24);
    CHECK_EQ(data_fdps_map_unit_status_icon_cycle, 5);

    data_fdps_timer_tick_counter = 102;
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(data_fdps_map_unit_status_icon_tick_counter, 0);
    CHECK_EQ(data_fdps_map_unit_status_icon_cycle, 6);
}

/* Portrait id 0x80 marks a record with no map sprite and returns at 0002ce79,
   and bit 0 of the flag byte retires the unit and returns at 0002ceb2.
   Neither draws anything at all -- not even a shadow. */
static void test_no_sprite_and_retired_units_draw_nothing(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;
    units[0].portrait_id = 0x80;
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), SCENE_UNTOUCHED);

    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;
    units[0].flags = 0x01;
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), SCENE_UNTOUCHED);

    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;
    units[0].flags = 0x01;
    data_fdps_map_unit_shadow_pass_flag = 1;
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), SCENE_UNTOUCHED);
}

/* Where a standing unit lands.  Tile (1, 1) is world pixel (24, 24) and the
   sprite is lifted six pixels (SUB EAX,0x6 at 0002cefd), so with the camera at
   the origin the block runs from buffer row 24 - 6 + 24 = 42 and column
   24 + 24 = 48 for 24 pixels each way.  The row above it and the row below the
   last must still hold the sentinel: the lift is what puts the sprite's feet
   on the tile rather than its head. */
static void test_standing_sprite_is_lifted_six_pixels(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;

    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(0));
    CHECK_EQ(pixel_at(41, 48), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(65, 71), sprite_color(0));
    CHECK_EQ(pixel_at(66, 71), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(42, 47), SCENE_UNTOUCHED);
}

/* The facing byte does two things at once and this pins both: it picks the
   per-step displacement -- (0,+4), (-4,0), (0,-4) and (+4,0) for 0, 1, 2 and
   the else -- and it selects the facing's three sprites at facing * 3.  Walk
   step 3 makes the displacement 12 pixels, and the colour of the drawn block
   names the sprite index that came out. */
static void test_facing_picks_the_displacement_and_the_sprite_row(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;
    units[0].walk_step = 3;

    units[0].facing = 0;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(54, 48), sprite_color(0));

    units[0].facing = 1;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 36), sprite_color(3));

    units[0].facing = 2;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(30, 48), sprite_color(6));

    units[0].facing = 3;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 60), sprite_color(9));
}

/* Facing 3 is reached by the else of a three-test chain, not by a fourth test,
   so a facing byte of 7 walks right exactly as 3 does -- and still indexes the
   sprite table at facing * 3, which for 7 is 21 and lands well outside the
   twelve sprites of the unit's own cache slot. */
static void test_facing_above_three_falls_into_the_walk_right_branch(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;
    units[0].walk_step = 3;
    units[0].facing = 7;

    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 60), sprite_color(21));
}

/* The cache slot at record +2 is twelve sprites wide, so the flat index is
   slot * 12 + facing * 3 + frame: slot 3, facing 1 and a walk counter of 4 --
   frame 1 -- is index 40. */
static void test_cache_slot_is_twelve_sprites_wide(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;
    units[0].sprite_cache_slot = 3;
    units[0].facing = 1;
    data_fdps_map_unit_walk_anim_counter = 4;

    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(40));
}

/* The walk frame is the counter divided by four with 3 folded back to 1, so
   the three sprites of a facing play 0, 1, 2, 1 over the counter's sixteen
   values.  The fold is the CMP ...,0x3 / MOV ...,0x1 at 0002cf1c; without it
   the fourth quarter of the cycle would index a fourth sprite that the
   twelve-per-slot layout does not have. */
static void test_walk_frame_folds_three_back_to_one(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;

    data_fdps_map_unit_walk_anim_counter = 0;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(0));

    data_fdps_map_unit_walk_anim_counter = 4;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(1));

    data_fdps_map_unit_walk_anim_counter = 8;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(2));

    data_fdps_map_unit_walk_anim_counter = 12;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(1));
}

/* A unit carrying the paralysis counter at record +0x26 -- status_timers[4] --
   is pinned to frame 0 however far the walk counter has got, and is displaced
   by the counter modulo 2 in x instead, which is what makes it shiver in
   place.  A counter of 8 would otherwise be frame 2 and is not displaced; 9 is
   still frame 0 and is displaced one pixel right, which moves the block's last
   column from 47 to 48. */
static void test_paralysed_unit_is_pinned_to_frame_zero_and_shivers(void)
{
    stage_unit();
    units[0].status_timers[4] = 3;

    data_fdps_map_unit_walk_anim_counter = 8;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(18, 24), sprite_color(0));
    CHECK_EQ(pixel_at(18, 47), sprite_color(0));
    CHECK_EQ(pixel_at(18, 48), SCENE_UNTOUCHED);

    data_fdps_map_unit_walk_anim_counter = 9;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(18, 24), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(18, 25), sprite_color(0));
    CHECK_EQ(pixel_at(18, 48), sprite_color(0));
}

/* The horizontal window is origin - 0x18 < x < origin + 0x138, both bounds
   strict (JGE at 0002cf5c rejects equality at the low end, JG at 0002cf6b
   requires it at the high end).  Facing 1 at walk step 6 puts x at exactly
   -24 and is dropped; step 5 puts it at -20 and is drawn.  Tile 13 puts x at
   exactly 312 and is dropped; tile 12 puts it at 288 and is drawn. */
static void test_horizontal_window_rejects_both_bounds_on_equality(void)
{
    stage_unit();
    units[0].pos_y = 1;
    units[0].facing = 1;

    units[0].walk_step = 6;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 0), SCENE_UNTOUCHED);

    units[0].walk_step = 5;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 4), sprite_color(3));

    units[0].facing = 3;
    units[0].walk_step = 0;

    units[0].pos_x = 13;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 336), SCENE_UNTOUCHED);

    units[0].pos_x = 12;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 312), sprite_color(9));
}

/* The sprite pass's vertical window is origin - 0x18 < y < origin + 0xc0, both
   bounds strict.  The camera is put at 98 so that a rejected y still maps to a
   row inside the buffer and the test can look at where the block would have
   gone.  y = 74 is exactly origin - 24 and is dropped; y = 78 is drawn at row
   4.  y = 290 is exactly origin + 192 and is dropped; y = 286 is drawn at row
   212. */
static void test_sprite_pass_vertical_window_rejects_both_bounds(void)
{
    stage_unit();
    data_fdps_battle_view_window_origin_y = 98;
    units[0].pos_x = 1;
    units[0].pos_y = 3;

    units[0].walk_step = 2;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(0, 48), SCENE_UNTOUCHED);

    units[0].walk_step = 3;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(4, 48), sprite_color(0));

    units[0].pos_y = 12;

    units[0].walk_step = 2;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(216, 48), SCENE_UNTOUCHED);

    units[0].walk_step = 1;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(212, 48), sprite_color(0));
}

/* The shadow pass's window is two pixels higher at BOTH ends -- origin - 0x1a
   to origin + 0xbe -- because the shadow is laid down two scanlines below the
   cell, and that is what keeps a shadow and its sprite appearing and
   disappearing together.  A unit's y is always the tile times 24 less 6 plus a
   multiple of 4, so which of the two-pixel gaps a case can land in depends on
   the camera: at 98 the value 74 falls in the near gap, inside the shadow
   window (> 72) and outside the sprite one (not > 74), and at 100 the value
   290 falls in the far one, outside the shadow window (not < 290) and inside
   the sprite one (< 292).  One shared vertical test would get both wrong. */
static void test_shadow_window_sits_two_pixels_above_the_sprite_window(void)
{
    stage_unit();
    data_fdps_battle_view_window_origin_y = 98;
    units[0].pos_x = 1;
    units[0].pos_y = 3;
    units[0].walk_step = 2;

    data_fdps_map_unit_shadow_pass_flag = 1;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(2, 48), CUBE_BASE + sprite_color(0));

    data_fdps_map_unit_shadow_pass_flag = 0;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(0, 48), SCENE_UNTOUCHED);

    data_fdps_battle_view_window_origin_y = 100;
    units[0].pos_y = 12;
    units[0].walk_step = 2;

    data_fdps_map_unit_shadow_pass_flag = 1;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(216, 48), SCENE_UNTOUCHED);

    data_fdps_map_unit_shadow_pass_flag = 0;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(214, 48), sprite_color(0));

    units[0].walk_step = 1;
    data_fdps_map_unit_shadow_pass_flag = 1;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(212, 48), CUBE_BASE + sprite_color(0));
}

/* The shadow goes down two scanlines below the cell (ADD EAX,0x2d0 at
   0002d041, which is 2 * 0x168) through mode 9 at level 0xa, and its frame is
   the walk frame -- taken out of the Shadow.cel offset table at +0x0f, not out
   of the sprite cache.  A unit that has already acted is pinned to shadow
   sprite 1 instead (the +0x13 read at 0002d060). */
static void test_shadow_frame_follows_the_walk_and_pins_when_acted(void)
{
    stage_unit();
    data_fdps_map_unit_shadow_pass_flag = 1;
    units[0].pos_x = 1;
    units[0].pos_y = 1;

    data_fdps_map_unit_walk_anim_counter = 0;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), CUBE_BASE + sprite_color(0));
    CHECK_EQ(pixel_at(43, 48), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(67, 48), CUBE_BASE + sprite_color(0));

    data_fdps_map_unit_walk_anim_counter = 8;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), CUBE_BASE + sprite_color(2));

    units[0].flags = 0x80;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), CUBE_BASE + sprite_color(1));
}

/* The shadow pass drops five sets of portrait ids: 0x8e, 0x69, 0x6a, the span
   0x24..0x27 and the span 0x3c..0x3e.  Each span is checked at both ends and
   at the id either side of it, because the compares are JL/JLE pairs and an
   off-by-one at either end is exactly what they would produce. */
static void test_shadow_pass_drops_the_no_shadow_portrait_ids(void)
{
    stage_unit();
    data_fdps_map_unit_shadow_pass_flag = 1;
    units[0].pos_x = 1;
    units[0].pos_y = 1;

    units[0].portrait_id = 0x8e;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), SCENE_UNTOUCHED);

    units[0].portrait_id = 0x69;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), SCENE_UNTOUCHED);

    units[0].portrait_id = 0x6a;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), SCENE_UNTOUCHED);

    units[0].portrait_id = 0x24;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), SCENE_UNTOUCHED);

    units[0].portrait_id = 0x27;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), SCENE_UNTOUCHED);

    units[0].portrait_id = 0x3c;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), SCENE_UNTOUCHED);

    units[0].portrait_id = 0x3e;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), SCENE_UNTOUCHED);

    units[0].portrait_id = 0x23;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), CUBE_BASE + sprite_color(0));

    units[0].portrait_id = 0x28;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), CUBE_BASE + sprite_color(0));

    units[0].portrait_id = 0x3b;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), CUBE_BASE + sprite_color(0));

    units[0].portrait_id = 0x3f;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), CUBE_BASE + sprite_color(0));

    units[0].portrait_id = 0x8d;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(44, 48), CUBE_BASE + sprite_color(0));
}

/* In the sprite pass the ids 0x69, 0x6a and the span 0x24..0x27 go through
   mode 9 at level 7, which the staging above resolves to CUBE_BASE + 7
   whatever the sprite; every other id draws opaque and comes back as its own
   sprite colour.  The 0x3c..0x3e span is NOT in this set: those cast no shadow
   but draw opaque, which is the difference between the two portrait tests. */
static void test_translucent_portrait_ids_take_mode_nine_at_level_seven(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;

    units[0].portrait_id = 0x24;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), CUBE_BASE + 7);

    units[0].portrait_id = 0x27;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), CUBE_BASE + 7);

    units[0].portrait_id = 0x69;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), CUBE_BASE + 7);

    units[0].portrait_id = 0x6a;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), CUBE_BASE + 7);

    units[0].portrait_id = 0x23;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(0));

    units[0].portrait_id = 0x28;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(0));

    units[0].portrait_id = 0x3d;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(0));

    units[0].portrait_id = 0x6b;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(0));
}

/* A unit that has already acted -- bit 0x80 of the flag byte -- is drawn at
   frame 1 of its facing through mode 0x0b at level 8 with tint colour 0, which
   the staging resolves to CUBE_BASE + (sprite colour & 0x0f).  The frame is
   pinned whatever the walk counter says: a counter of 8 would otherwise be
   frame 2.  Facing 2 is checked as well, because the pinned index is
   slot * 12 + facing * 3 + 1 and not the constant 1. */
static void test_acted_unit_is_frame_one_through_the_tinting_blit(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;
    data_fdps_map_unit_walk_anim_counter = 8;

    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), sprite_color(2));

    units[0].flags = 0x80;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), CUBE_BASE + (sprite_color(1) & 0x0f));

    units[0].facing = 2;
    fill_scene(0);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(42, 48), CUBE_BASE + (sprite_color(7) & 0x0f));
}

/* The status icon is a 0x18 by 0x0b strip out of IconSts.cel put down 13
   scanlines below the top of the unit's cell -- ADD EAX,0x1248, which is
   13 * 0x168 -- and only in the sprite pass.  The icon slot is whatever
   fdps_unit_select_status_icon answers for the cycle: record +0x23 is slot 0
   and +0x22 is slot 1, so the two timers pick different icons.  A unit with no
   ailment gets -1 and no strip at all. */
static void test_status_icon_lands_thirteen_rows_below_the_cell(void)
{
    stage_unit();
    units[0].pos_x = 1;
    units[0].pos_y = 1;

    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(55, 48), sprite_color(0));

    units[0].status_timers[1] = 5;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(54, 48), sprite_color(0));
    CHECK_EQ(pixel_at(55, 48), ICON_COLOR_BASE + 0);
    CHECK_EQ(pixel_at(65, 48), ICON_COLOR_BASE + 0);
    CHECK_EQ(pixel_at(66, 48), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(55, 71), ICON_COLOR_BASE + 0);
    CHECK_EQ(pixel_at(55, 72), SCENE_UNTOUCHED);

    units[0].status_timers[1] = 0;
    units[0].status_timers[0] = 5;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(55, 48), ICON_COLOR_BASE + 1);

    data_fdps_map_unit_shadow_pass_flag = 1;
    fill_scene(SCENE_UNTOUCHED);
    fdps_draw_map_unit(0, scene, 0);
    CHECK_EQ(pixel_at(55, 48), CUBE_BASE + sprite_color(0));
}

/* ------------------------------------------------------------------ */
/* fdps_draw_map_units @ 0002d240                                      */
/* ------------------------------------------------------------------ */

/* Two units stacked one tile apart in the same column, which is the only
   arrangement that can tell the two-sweep order from a fused one.  Unit 0 sits
   at tile (1, 1): its sprite block runs rows 42..65 (tile 24 less the six
   pixel lift plus the 24-pixel border) and its shadow rows 44..67.  Unit 1
   sits at tile (1, 0) one tile higher: sprite rows 18..41, shadow rows 20..43.
   So rows 42 and 43 are covered by unit 1's SHADOW and by unit 0's SPRITE and
   by nothing else, and which of the two a pixel there holds says which went
   down last.

   The two are given different facings so a drawn pixel also names the unit
   that drew it: facing 0 selects sprite 0 and facing 1 sprite 3, while both
   shadows come out of the walk counter and so are shadow frame 0. */
static void stage_two_stacked_units(void)
{
    stage_unit();
    data_fdps_map_unit_count = 2;

    units[0].pos_x = 1;
    units[0].pos_y = 1;
    units[0].facing = 0;

    units[1].pos_x = 1;
    units[1].pos_y = 0;
    units[1].facing = 1;

    fill_scene(SCENE_UNTOUCHED);
}

/* THE WHOLE POINT OF THE FUNCTION.  Every shadow is on the surface before the
   first sprite goes down, so the rows where unit 1's shadow overlaps unit 0's
   sprite come back as the sprite.  A single fused loop -- draw unit 0's shadow
   and sprite, then unit 1's shadow and sprite -- puts unit 1's shadow over
   unit 0's sprite there instead and rows 42 and 43 come back CUBE_BASE + 1.
   Row 66 is below every sprite and holds unit 0's own shadow, which is what
   says the shadow sweep ran at all. */
static void test_every_shadow_is_laid_before_the_first_sprite(void)
{
    stage_two_stacked_units();

    fdps_draw_map_units(scene, 0);

    CHECK_EQ(pixel_at(42, 48), sprite_color(0));
    CHECK_EQ(pixel_at(43, 48), sprite_color(0));
    CHECK_EQ(pixel_at(66, 48), CUBE_BASE + sprite_color(0));
    CHECK_EQ(pixel_at(67, 48), CUBE_BASE + sprite_color(0));
    CHECK_EQ(pixel_at(41, 48), sprite_color(3));
    CHECK_EQ(pixel_at(18, 48), sprite_color(3));
    CHECK_EQ(pixel_at(68, 48), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(17, 48), SCENE_UNTOUCHED);
}

/* Both sweeps stop at data_fdps_map_unit_count (MOV byte ptr [0x0006014c] then
   the JL pair at 0002d263 and 0002d29e), so a count of 1 draws unit 0 and
   leaves unit 1's rows -- 18..41 for its sprite and 20..43 for its shadow --
   untouched apart from where unit 0 itself reaches. */
static void test_both_sweeps_stop_at_the_unit_count(void)
{
    stage_two_stacked_units();
    data_fdps_map_unit_count = 1;

    fdps_draw_map_units(scene, 0);

    CHECK_EQ(pixel_at(42, 48), sprite_color(0));
    CHECK_EQ(pixel_at(66, 48), CUBE_BASE + sprite_color(0));
    CHECK_EQ(pixel_at(41, 48), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(18, 48), SCENE_UNTOUCHED);
}

/* The count is compared SIGNED -- JL, not JB -- so a negative count draws
   nothing at all rather than sweeping the array four billion times
   (rebuild_info/pitfalls.md, contract C).  A zero count likewise draws
   nothing, and both leave the pass byte where the second sweep puts it. */
static void test_zero_and_negative_counts_draw_nothing(void)
{
    stage_two_stacked_units();
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_shadow_pass_flag = 0xaa;

    fdps_draw_map_units(scene, 0);

    CHECK_EQ(pixel_at(42, 48), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(18, 48), SCENE_UNTOUCHED);
    CHECK_EQ(data_fdps_map_unit_shadow_pass_flag, 0);

    stage_two_stacked_units();
    data_fdps_map_unit_count = -1;
    data_fdps_map_unit_shadow_pass_flag = 0xaa;

    fdps_draw_map_units(scene, 0);

    CHECK_EQ(pixel_at(42, 48), SCENE_UNTOUCHED);
    CHECK_EQ(pixel_at(18, 48), SCENE_UNTOUCHED);
    CHECK_EQ(data_fdps_map_unit_shadow_pass_flag, 0);
}

/* The pass byte is 1 for the first sweep and 0 for the second, and 0 is what
   it is left holding: the shadows on screen prove it was 1 while they were
   drawn, and the byte read afterwards proves the second store ran. */
static void test_the_pass_byte_is_left_clear(void)
{
    stage_two_stacked_units();
    data_fdps_map_unit_shadow_pass_flag = 0xaa;

    fdps_draw_map_units(scene, 0);

    CHECK_EQ(data_fdps_map_unit_shadow_pass_flag, 0);
    CHECK_EQ(pixel_at(66, 48), CUBE_BASE + sprite_color(0));
}

/* The walk clock steps ONCE for the frame although this routine calls
   fdps_draw_map_unit twice for every unit -- four calls here.  The latch in
   the callee is what holds it, and it is the reason the two sweeps cost the
   animation nothing. */
static void test_four_calls_step_the_walk_clock_once(void)
{
    stage_two_stacked_units();
    data_fdps_map_unit_walk_anim_counter = 3;
    data_fdps_map_unit_status_icon_tick_counter = 7;
    data_fdps_timer_tick_counter = 101;
    data_fdps_map_unit_anim_last_tick = 100;

    fdps_draw_map_units(scene, 0);

    CHECK_EQ(data_fdps_map_unit_anim_last_tick, 101);
    CHECK_EQ(data_fdps_map_unit_walk_anim_counter, 4);
    CHECK_EQ(data_fdps_map_unit_status_icon_tick_counter, 8);
}

/* The second argument is forwarded to every call and read nowhere else, and
   the callee overwrites its own copy with 0 before touching it, so passing
   0xff paints exactly the same scene as passing 0.  Only the low byte is read
   here (XOR EAX,EAX / MOV AL,byte ptr [EBP + 0x18]). */
static void test_the_forwarded_flag_changes_nothing(void)
{
    unsigned char with_zero[8];
    int i;

    stage_two_stacked_units();
    fdps_draw_map_units(scene, 0);
    for (i = 0; i < 8; i++) {
        with_zero[i] = (unsigned char) pixel_at(40 + i, 48);
    }

    stage_two_stacked_units();
    fdps_draw_map_units(scene, 0xff);
    for (i = 0; i < 8; i++) {
        CHECK_EQ(pixel_at(40 + i, 48), with_zero[i]);
    }
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

    RUN_TEST(test_walk_clock_steps_only_when_the_tick_moves);
    RUN_TEST(test_walk_counter_wraps_at_sixteen);
    RUN_TEST(test_icon_counter_wraps_and_bumps_the_cycle);
    RUN_TEST(test_no_sprite_and_retired_units_draw_nothing);
    RUN_TEST(test_standing_sprite_is_lifted_six_pixels);
    RUN_TEST(test_facing_picks_the_displacement_and_the_sprite_row);
    RUN_TEST(test_facing_above_three_falls_into_the_walk_right_branch);
    RUN_TEST(test_cache_slot_is_twelve_sprites_wide);
    RUN_TEST(test_walk_frame_folds_three_back_to_one);
    RUN_TEST(test_paralysed_unit_is_pinned_to_frame_zero_and_shivers);
    RUN_TEST(test_horizontal_window_rejects_both_bounds_on_equality);
    RUN_TEST(test_sprite_pass_vertical_window_rejects_both_bounds);
    RUN_TEST(test_shadow_window_sits_two_pixels_above_the_sprite_window);
    RUN_TEST(test_shadow_frame_follows_the_walk_and_pins_when_acted);
    RUN_TEST(test_shadow_pass_drops_the_no_shadow_portrait_ids);
    RUN_TEST(test_translucent_portrait_ids_take_mode_nine_at_level_seven);
    RUN_TEST(test_acted_unit_is_frame_one_through_the_tinting_blit);
    RUN_TEST(test_status_icon_lands_thirteen_rows_below_the_cell);

    RUN_TEST(test_every_shadow_is_laid_before_the_first_sprite);
    RUN_TEST(test_both_sweeps_stop_at_the_unit_count);
    RUN_TEST(test_zero_and_negative_counts_draw_nothing);
    RUN_TEST(test_the_pass_byte_is_left_clear);
    RUN_TEST(test_four_calls_step_the_walk_clock_once);
    RUN_TEST(test_the_forwarded_flag_changes_nothing);
}
