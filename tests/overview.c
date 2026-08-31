/* tests/overview.c -- cover for src/overview.c.
 *
 * Every expected value below is read off the assembly at 0002e710 -- PUSH
 * 0xfa00 into memset, ADD EAX,0x504 for the window corner, IMUL by 0x9c and
 * 0x60 for the half-window offsets, IDIV by 0xc00 with the two floor
 * corrections at 0002e79b and 0002e7ae, the divide-by-0x80 sequences at
 * 0002e808 and 0002e87a, IMUL EAX,EAX,0x18 for the source row, SHL EAX,0x6
 * for the tile table's row pitch, the two JL bounds at 0002e82b and 0002e876,
 * and the loop bounds 0xc0 and 0x138 -- and never off the emitted C.
 *
 * The map is staged in memory rather than read from a game file because the
 * function takes all of it from arguments and from one layer pointer:
 * pointing that pointer at a local header and handing in a local tile table
 * is the only way to reach the body.  Nothing here asserts what any global
 * holds on its own; ticket 23 owns that.
 *
 * The page is filled with 0xee before every call, so an expectation of 0 is
 * evidence that the clear reached that byte and not merely that nobody wrote
 * there.
 */
#include "testharn.h"
#include "gamedata.h"
#include "overview.h"

#define PAGE_BYTES 0xfa00
#define PAGE_PITCH 0x140
#define WINDOW_ORIGIN 0x504

#define TILE_SIDE 24
#define TILE_BYTES (TILE_SIDE * TILE_SIDE)
#define TILE_POOL 16
#define TABLE_PITCH 0x40
#define TABLE_CELLS (TABLE_PITCH * TABLE_PITCH)

#define HEADER_BYTES 16

/* Step 0x80 is one source pixel per output pixel; the world position the
   caller hands in is the window's centre, so a top-left corner of exactly
   (0, 0) is half a window away from it. */
#define STEP_1TO1 0x80
#define CENTRE_X(step) ((step) * 0x9c)
#define CENTRE_Y(step) ((step) * 0x60)

static unsigned char page[PAGE_BYTES];
static unsigned char header[HEADER_BYTES];
static unsigned char tile_pool[TILE_POOL][TILE_BYTES];
static unsigned char *tile_table[TABLE_CELLS];

/* Every byte of every staged tile is distinct in all three of pool slot, row
   and column, and none of them is 0, so one assertion says which tile was
   reached, which row of it and which column, and tells all three apart from
   the cleared background. */
static unsigned char pool_byte(int pool_index, int row, int col)
{
    return (unsigned char) (1 + (pool_index * 41 + row * 7 + col) % 200);
}

/* The pool slot a map tile is wired to.  Stepping one tile right and one tile
   down land on different slots, so a mix-up between the tile table's row
   pitch and the map's own width would be visible. */
static unsigned char map_pixel(int tile_y, int tile_x, int row, int col)
{
    return pool_byte((tile_y * 5 + tile_x) % TILE_POOL, row, col);
}

/* Rebuild the header, the tile bitmaps, the pointer table and the page.

   The header is filled with 0xaa first and only then has its width written at
   +7 and its height at +9, so a build that took either from another offset
   would read 0xaaaa -- -21846 signed -- and reject every tile rather than
   coincidentally agreeing. */
static void stage(int map_width, int map_height)
{
    int i;
    int pool_index;
    int row;
    int col;
    int tile_y;
    int tile_x;

    for (i = 0; i < HEADER_BYTES; i++) {
        header[i] = 0xaa;
    }
    *(short *) (header + 7) = (short) map_width;
    *(short *) (header + 9) = (short) map_height;
    data_fdps_scene_layer_tile_map_ptrs[0] = header;

    for (pool_index = 0; pool_index < TILE_POOL; pool_index++) {
        for (row = 0; row < TILE_SIDE; row++) {
            for (col = 0; col < TILE_SIDE; col++) {
                tile_pool[pool_index][row * TILE_SIDE + col] =
                    pool_byte(pool_index, row, col);
            }
        }
    }

    for (tile_y = 0; tile_y < TABLE_PITCH; tile_y++) {
        for (tile_x = 0; tile_x < TABLE_PITCH; tile_x++) {
            tile_table[tile_y * TABLE_PITCH + tile_x] =
                tile_pool[(tile_y * 5 + tile_x) % TILE_POOL];
        }
    }

    for (i = 0; i < PAGE_BYTES; i++) {
        page[i] = 0xee;
    }
}

/* The clear is the whole page and not the window: with a 0 x 0 map no tile
   row is ever in range, so nothing at all is drawn and every one of the
   0xfa00 bytes has to come back 0.  A shorter memset would leave the tail of
   the page holding 0xee. */
static void clears_the_whole_page(void)
{
    int i;
    int nonzero;

    stage(0, 0);
    fdps_render_map_overview_scaled(page, CENTRE_X(STEP_1TO1),
                                    CENTRE_Y(STEP_1TO1), STEP_1TO1,
                                    tile_table);

    nonzero = 0;
    for (i = 0; i < PAGE_BYTES; i++) {
        if (page[i] != 0) {
            nonzero++;
        }
    }

    CHECK_EQ(page[0], 0);
    CHECK_EQ(page[WINDOW_ORIGIN], 0);
    CHECK_EQ(page[PAGE_BYTES - 1], 0);
    CHECK_EQ(nonzero, 0);
}

/* At step 0x80 one output pixel is one source pixel, so a tile fills 24
   columns and 24 rows before the walk moves on.  The window's corner is row
   4, column 4, and a 2 x 2 map runs out after 48 of each. */
static void draws_one_source_pixel_per_output_pixel(void)
{
    stage(2, 2);
    fdps_render_map_overview_scaled(page, CENTRE_X(STEP_1TO1),
                                    CENTRE_Y(STEP_1TO1), STEP_1TO1,
                                    tile_table);

    CHECK_EQ(page[WINDOW_ORIGIN - 1], 0);
    CHECK_EQ(page[WINDOW_ORIGIN - PAGE_PITCH], 0);

    CHECK_EQ(page[WINDOW_ORIGIN], map_pixel(0, 0, 0, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 23], map_pixel(0, 0, 0, 23));
    CHECK_EQ(page[WINDOW_ORIGIN + 24], map_pixel(0, 1, 0, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 47], map_pixel(0, 1, 0, 23));
    CHECK_EQ(page[WINDOW_ORIGIN + 48], 0);

    CHECK_EQ(page[WINDOW_ORIGIN + PAGE_PITCH], map_pixel(0, 0, 1, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + PAGE_PITCH + 24], map_pixel(0, 1, 1, 0));

    CHECK_EQ(page[WINDOW_ORIGIN + 24 * PAGE_PITCH], map_pixel(1, 0, 0, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 48 * PAGE_PITCH], 0);
}

/* The window is 0x138 x 0xc0 and nothing outside it is touched.  A 64 x 64
   map covers all of it, so the last column and the last row hold map and the
   one past each holds the cleared background. */
static void fills_exactly_the_window(void)
{
    stage(64, 64);
    fdps_render_map_overview_scaled(page, CENTRE_X(STEP_1TO1),
                                    CENTRE_Y(STEP_1TO1), STEP_1TO1,
                                    tile_table);

    CHECK_EQ(page[WINDOW_ORIGIN + 0x137], map_pixel(0, 12, 0, 23));
    CHECK_EQ(page[WINDOW_ORIGIN + 0x138], 0);
    CHECK_EQ(page[WINDOW_ORIGIN + 0xbf * PAGE_PITCH], map_pixel(7, 0, 23, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 0xc0 * PAGE_PITCH], 0);
}

/* A top-left corner one unit left of tile 0 belongs to tile -1, not to tile 0
   with a negative remainder: the floor correction turns the IDIV's 0 / -1
   into -1 / 0xbff.  Output column 0 therefore falls outside the map and stays
   cleared, and the map starts one column later, at source column 0.

   Truncating towards zero instead would put tile 0 column 0 at output column
   0 and shift the whole row left by one.

   The row is taken from tile row 1 so that the unguarded table fetch for a
   negative tile_x reads entry 63 of the table rather than the entry before
   it. */
static void floors_a_negative_left_edge(void)
{
    stage(2, 2);
    fdps_render_map_overview_scaled(page, CENTRE_X(STEP_1TO1) - 1,
                                    CENTRE_Y(STEP_1TO1) + 0xc00, STEP_1TO1,
                                    tile_table);

    CHECK_EQ(page[WINDOW_ORIGIN], 0);
    CHECK_EQ(page[WINDOW_ORIGIN + 1], map_pixel(1, 0, 0, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 2], map_pixel(1, 0, 0, 1));
    CHECK_EQ(page[WINDOW_ORIGIN + 24], map_pixel(1, 0, 0, 23));
    CHECK_EQ(page[WINDOW_ORIGIN + 25], map_pixel(1, 1, 0, 0));
}

/* The same correction on the vertical axis: a top edge one unit above tile 0
   is tile row -1, which is rejected before any tile pointer is fetched, so
   the whole first output row stays cleared.  The remainder carried into row 1
   is 0xbff + 0x80 - 0xc00 = 0x7f, still inside source row 0, and row 2 is the
   first that reads source row 1. */
static void floors_a_negative_top_edge(void)
{
    stage(2, 2);
    fdps_render_map_overview_scaled(page, CENTRE_X(STEP_1TO1),
                                    CENTRE_Y(STEP_1TO1) - 1, STEP_1TO1,
                                    tile_table);

    CHECK_EQ(page[WINDOW_ORIGIN], 0);
    CHECK_EQ(page[WINDOW_ORIGIN + 24], 0);
    CHECK_EQ(page[WINDOW_ORIGIN + PAGE_PITCH], map_pixel(0, 0, 0, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 2 * PAGE_PITCH], map_pixel(0, 0, 1, 0));
}

/* Step 0x100 advances two source pixels per output pixel in both directions,
   so a tile is 12 output pixels wide and 12 rows tall and the 2 x 2 map runs
   out after 24 of each. */
static void doubles_the_step_at_zoom(void)
{
    stage(2, 2);
    fdps_render_map_overview_scaled(page, CENTRE_X(0x100), CENTRE_Y(0x100),
                                    0x100, tile_table);

    CHECK_EQ(page[WINDOW_ORIGIN], map_pixel(0, 0, 0, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 1], map_pixel(0, 0, 0, 2));
    CHECK_EQ(page[WINDOW_ORIGIN + 11], map_pixel(0, 0, 0, 22));
    CHECK_EQ(page[WINDOW_ORIGIN + 12], map_pixel(0, 1, 0, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 24], 0);

    CHECK_EQ(page[WINDOW_ORIGIN + PAGE_PITCH], map_pixel(0, 0, 2, 0));
    CHECK_EQ(page[WINDOW_ORIGIN + 12 * PAGE_PITCH], map_pixel(1, 0, 0, 0));
}

void run_overview_tests(void)
{
    RUN_TEST(clears_the_whole_page);
    RUN_TEST(draws_one_source_pixel_per_output_pixel);
    RUN_TEST(fills_exactly_the_window);
    RUN_TEST(floors_a_negative_left_edge);
    RUN_TEST(floors_a_negative_top_edge);
    RUN_TEST(doubles_the_step_at_zoom);
}
