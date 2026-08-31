/* overview.c -- the battle map overview screen's scaled map render.
 *
 * See overview.h for the surface convention and for what the fixed-point
 * units mean.  Nothing here owns state: the map geometry is read from the
 * loaded terrain layer the chapter loader owns, and everything else arrives
 * as an argument.
 */
#include <string.h>
#include "gamedata.h"
#include "overview.h"

/* The offscreen page: a full 320x200 8bpp screen, cleared in one go, with the
   visible screen's pitch. */
#define OVERVIEW_PAGE_BYTES 0xfa00
#define OVERVIEW_PAGE_PITCH 0x140

/* The map window inside that page: 0x138 x 0xc0 pixels with its top-left
   corner at row 4, column 4, which is 4 * 0x140 + 4 = 0x504.  The two halves
   are the constants the original multiplies step by to find the top-left
   world position from the centre. */
#define OVERVIEW_WINDOW_ORIGIN 0x504
#define OVERVIEW_WINDOW_WIDTH 0x138
#define OVERVIEW_WINDOW_HEIGHT 0xc0
#define OVERVIEW_HALF_WIDTH 0x9c
#define OVERVIEW_HALF_HEIGHT 0x60

/* The fixed-point grid: one tile is 0xc00 units and one source pixel is 0x80
   of them, so a tile is 24 pixels on a side and a tile bitmap's row is 0x18
   bytes.  The tile pointer table's row pitch is a fixed 0x40 entries. */
#define TILE_UNITS 0xc00
#define PIXEL_UNITS 0x80
#define TILE_ROW_BYTES 0x18
#define TILE_TABLE_PITCH 0x40

/* 0002e710.  Four things here are behaviour rather than style.

   The map's width and height are MOVSX word ptr [EAX+7] and [EAX+9] --
   signed -- and both are compared against a tile index with JL (0002e82b and
   0002e876).  A header word of 0xffff has to come out as -1 and reject every
   tile; read unsigned it becomes 65535 and lets the whole walk index off the
   end of the tile table instead.

   The split of a world position into tile index and remainder is an IDIV
   followed by a hand-written floor correction (0002e79b and 0002e7ae), and
   the correction is what makes a negative position land on the tile to its
   left rather than truncating towards zero.  Without it a top-left corner one
   unit left of tile 0 draws tile 0 at output column 0 instead of leaving the
   column cleared, and every column of the row is off by one.

   The clear is the full 0xfa00 bytes of the page and not just the window:
   what the map does not cover is background, and the caller presents the
   whole page.  memset's return value is discarded, so nothing here depends on
   what the CRT hands back.

   The tile pointer is fetched before tile_x is range-checked, both at the
   start of a row (0002e832, ahead of the 0002e86a check) and after every wrap
   (0002e8b4).  The fetch of a row whose first tile_x is negative therefore
   reads tile_table one entry short of the row; the pointer is never
   dereferenced, because only the copy is guarded.  tile_y, by contrast, is
   checked before anything is fetched, so a row above or below the map costs
   nothing at all.

   The row restart is unconditional and sits after the inner loop (0002e8d1),
   which is why it also runs for a row that was skipped entirely: tile_x and
   the x remainder go back to where the row began, and only then does the y
   remainder advance. */
void fdps_render_map_overview_scaled(unsigned char *dest, int world_x,
                                     int world_y, int step,
                                     unsigned char **tile_table)
{
    unsigned char *dest_row;
    unsigned char *src_row;
    int map_width;
    int map_height;
    int left_world_x;
    int top_world_y;
    int tile_x;
    int tile_y;
    int rem_x;
    int rem_y;
    int row_start_tile_x;
    int row_start_rem_x;
    int src_row_offset;
    int screen_x;
    int screen_y;

    map_width = (int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0] + 7);
    map_height = (int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0] + 9);

    left_world_x = world_x - step * OVERVIEW_HALF_WIDTH;
    top_world_y = world_y - step * OVERVIEW_HALF_HEIGHT;

    tile_x = left_world_x / TILE_UNITS;
    rem_x = left_world_x % TILE_UNITS;
    tile_y = top_world_y / TILE_UNITS;
    rem_y = top_world_y % TILE_UNITS;

    if (rem_y < 0) {
        rem_y += TILE_UNITS;
        tile_y--;
    }
    if (rem_x < 0) {
        rem_x += TILE_UNITS;
        tile_x--;
    }

    row_start_tile_x = tile_x;
    row_start_rem_x = rem_x;

    dest_row = dest + OVERVIEW_WINDOW_ORIGIN;
    memset(dest, 0, OVERVIEW_PAGE_BYTES);

    for (screen_y = 0; screen_y < OVERVIEW_WINDOW_HEIGHT; screen_y++) {
        src_row_offset = rem_y / PIXEL_UNITS * TILE_ROW_BYTES;

        if (tile_y >= 0 && tile_y < map_height) {
            src_row = tile_table[tile_y * TILE_TABLE_PITCH + tile_x] +
                      src_row_offset;

            for (screen_x = 0; screen_x < OVERVIEW_WINDOW_WIDTH; screen_x++) {
                if (tile_x >= 0 && tile_x < map_width) {
                    dest_row[screen_x] = src_row[rem_x / PIXEL_UNITS];
                }

                rem_x += step;
                if (rem_x >= TILE_UNITS) {
                    tile_x++;
                    rem_x -= TILE_UNITS;
                    src_row = tile_table[tile_y * TILE_TABLE_PITCH + tile_x] +
                              src_row_offset;
                }
            }
        }

        tile_x = row_start_tile_x;
        rem_x = row_start_rem_x;

        rem_y += step;
        if (rem_y >= TILE_UNITS) {
            tile_y++;
            rem_y -= TILE_UNITS;
        }

        dest_row += OVERVIEW_PAGE_PITCH;
    }
}
