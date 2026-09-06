/* overview.c -- the battle map overview screen and its scaled map render.
 *
 * See overview.h for the surface convention and for what the fixed-point
 * units mean.  Nothing here owns state: the map geometry is read from the
 * loaded terrain layer the chapter loader owns, the markers from the battle's
 * unit array and cursor, and everything else is either an argument or lives on
 * the frame for the length of the call.
 */
#include <stdlib.h>
#include <string.h>
#include "blit.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "maptile.h"
#include "sprite.h"
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

/* The whole tile pointer table, TILE_TABLE_PITCH squared entries of four
   bytes: PUSH 0x4000 at 0002e24f.  It is a fixed 64 x 64 whatever the map's
   own size is, and only the part the map covers is ever written. */
#define TILE_TABLE_BYTES 0x4000

/* The VGA graphics aperture as a flat linear address.  It is where the display
   adapter answers and not the address of anything the linker places, so it
   stays a literal here exactly as it does in blit.c. */
#define VGA_SCREEN_BASE 0x000a0000

/* A tile is 24 pixels on the battle map itself, which is what turns the
   camera's pixel origin and the cursor's pixel position into tile indices. */
#define TILE_PIXELS 0x18

/* One tile in the expanded sheet: 24 * 24 bytes of pixels, and the first of
   them starts past the 6-byte shape header (width, height, count) that
   fdps_cel_expand_sheet_24x24 writes at the front of the block it returns. */
#define TILE_SHEET_STRIDE 0x240
#define TILE_SHEET_HEADER_BYTES 6

/* Half a tile in the fixed-point grid.  A centre is measured in these: the
   camera is centred half a window past its top-left tile, and the map is
   centred half its own size past tile (0, 0). */
#define HALF_TILE_UNITS 0x600

/* How much of the map the battle view shows, in whole tiles -- 0x138 / 24 and
   0xc0 / 24, the same window fdps_render_map_overview_scaled fills.  These are
   the literals 0xd and 8 at 0002e193 and 0002e19a. */
#define VIEW_TILE_COLUMNS 0xd
#define VIEW_TILE_ROWS 8

/* The markers.  marker_pitch is how many screen pixels one map tile is worth
   and is also handed to fdps_fill_screen_square as its cell pitch, so the
   square it paints is one pixel smaller than the spacing; zoom_span is how far
   past 1:1 the fly-in zooms out.  A map more than TALL_MAP_ROWS tiles tall
   takes the second pair of values, which shrinks the markers and pulls the
   camera further back so that the taller map still fits. */
#define MARKER_PITCH 4
#define MARKER_PITCH_TALL 3
#define ZOOM_SPAN 0x280
#define ZOOM_SPAN_TALL 0x380
#define TALL_MAP_ROWS 0x28

/* Where the map's centre sits on the screen while the markers are drawn: the
   middle of the 320 x 200 display. */
#define MARKER_CENTRE_X 0xa0
#define MARKER_CENTRE_Y 0x64

/* The fly-in runs steps 1 to 7 and the fly-out steps 6 down to 1, and both
   interpolate the same way: the distance from the camera's centre to the map's
   is taken in eighths of (step + 1), so step 7 lands exactly on the map's
   centre and the fly-out stops two eighths short of the camera's rather than
   returning to it.  The zoom itself is span * step / 7 past 1:1, and 1:1 is
   PIXEL_UNITS above: the render's step for one source pixel per output pixel
   is the same 0x80 that is added at 0002e304. */
#define ZOOM_IN_LAST 7
#define ZOOM_OUT_FIRST 6
#define ZOOM_DENOM 8
#define ZOOM_SPAN_DENOM 7

/* The pulse every marker shares: an offset added to whichever colour base the
   marker uses, walked one step per drawn frame and turned round at each end,
   so it runs 7, 6 ... 1, 0, 0, 1 ... 7, 7, 6 and repeats. */
#define PULSE_MAX 7

/* The cursor's colour base.  The three unit bases come from a table; this one
   is the literal 0x82 added at 0002e474. */
#define CURSOR_COLOUR_BASE 0x82

/* How far the tick counter has to have moved for the markers to be drawn
   again: CMP EAX,0x2 / JGE at 0002e393. */
#define MARKER_FRAME_TICKS 2

/* A scancode of 0x80 or above is a break code or the 0xff the queue answers
   with when it is empty, and neither closes the screen: AND EAX,0xff / CMP
   EAX,0x7f / JLE at 0002e377 is what ends the hold. */
#define SCANCODE_LAST_MAKE_CODE 0x7f

/* 0002e160.  Four things here are behaviour rather than style.

   THE FRAME LATCH IS DELIBERATELY LEFT UNINITIALISED.  last_frame_tick is read
   at 0002e390 and 0002e39b before anything writes it -- the only store is at
   0002e509, at the bottom of a pass that has already drawn -- so what the
   first pass compares against is whatever the frame slot happened to hold.
   Seeding it from the counter is the natural fix and it changes when the first
   marker pass runs (rebuild_info/pitfalls.md).

   The counter is re-read from the global at the bottom of the pass rather than
   the copy taken at the top being reused (MOV EAX,[0x00069d64] at 0002e504,
   not a load of the local), and the global is volatile: the timer interrupt
   can advance it while the pass is drawing, and the latch is meant to carry
   that later value.

   The gate is signed on both arms.  CMP EAX,0x2 / JGE and TEST EAX,EAX / JGE
   over the difference means a counter that has moved backwards -- which is
   what a wrap looks like -- also draws, where an unsigned compare would make
   the difference enormous and draw anyway for the wrong reason.  Both locals
   are ints for that, even though the counter itself is unsigned.

   The marker colour bases are three dwords copied onto the frame from a rodata
   constant (MOVSD three times out of 0x2b294 at 0002e190), which is what an
   initialised local array compiles to; the address 0x2b294 is where the
   original linker put the constant and means nothing in the rebuild
   (rebuild_info/pitfalls.md).  The unit's side byte indexes it with no bound
   check of any kind, exactly as the original does. */
void fdps_battle_map_overview(void)
{
    int marker_colour_base[3] = { 0x28, 0xb4, 0xd6 };
    struct fdps_unit_record *unit;
    unsigned char *page;
    unsigned char *tile_sheet;
    unsigned char **tile_table;
    int map_width;
    int map_height;
    int view_tile_x;
    int view_tile_y;
    int view_centre_x;
    int view_centre_y;
    int map_centre_x;
    int map_centre_y;
    int marker_pitch;
    int zoom_span;
    int zoom_step;
    int tile_id;
    int tile_x;
    int tile_y;
    int unit_index;
    int current_tick;
    int last_frame_tick;
    int pulse_offset;
    int pulse_step;

    marker_pitch = MARKER_PITCH;
    zoom_span = ZOOM_SPAN;
    pulse_offset = PULSE_MAX;
    pulse_step = -1;

    map_width = (int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0] + 7);
    map_height = (int) *(short *) (data_fdps_scene_layer_tile_map_ptrs[0] + 9);
    view_tile_x = data_fdps_battle_view_window_origin_x / TILE_PIXELS;
    view_tile_y = data_fdps_battle_view_window_origin_y / TILE_PIXELS;

    page = (unsigned char *) malloc((size_t) OVERVIEW_PAGE_BYTES);
    /* fdps_cel_expand_sheet_24x24 ends the process rather than answering NULL,
       so the original tests nothing here and neither does this. */
    tile_sheet = fdps_cel_expand_sheet_24x24();

    if (map_height > TALL_MAP_ROWS) {
        marker_pitch = MARKER_PITCH_TALL;
        zoom_span = ZOOM_SPAN_TALL;
    }

    view_centre_x = view_tile_x * TILE_UNITS
                    + VIEW_TILE_COLUMNS * HALF_TILE_UNITS;
    view_centre_y = view_tile_y * TILE_UNITS
                    + VIEW_TILE_ROWS * HALF_TILE_UNITS;
    map_centre_x = map_width * HALF_TILE_UNITS;
    map_centre_y = map_height * HALF_TILE_UNITS;

    tile_table = (unsigned char **) malloc((size_t) TILE_TABLE_BYTES);

    /* Only the map's own tiles are given a pointer; the rest of the 64 x 64
       table keeps whatever the allocator handed over, which is why the render
       must not dereference a fetch it has not range-checked (overview.h). */
    for (tile_y = 0; tile_y < map_height; tile_y++) {
        for (tile_x = 0; tile_x < map_width; tile_x++) {
            fdps_map_load_tile_info(tile_x, tile_y);
            tile_id = data_fdps_map_tile_info_tile_id;
            tile_table[tile_y * TILE_TABLE_PITCH + tile_x] =
                tile_sheet + TILE_SHEET_HEADER_BYTES
                + tile_id * TILE_SHEET_STRIDE;
        }
    }

    for (zoom_step = 1; zoom_step <= ZOOM_IN_LAST; zoom_step++) {
        fdps_render_map_overview_scaled(page,
            (zoom_step + 1) * (map_centre_x - view_centre_x) / ZOOM_DENOM
            + view_centre_x,
            (map_centre_y - view_centre_y) * (zoom_step + 1) / ZOOM_DENOM
            + view_centre_y,
            zoom_span * zoom_step / ZOOM_SPAN_DENOM + PIXEL_UNITS, tile_table);
        memmove((void *) VGA_SCREEN_BASE, page, (size_t) OVERVIEW_PAGE_BYTES);
    }

    fdps_flush_keyboard_queue();

    while (fdps_read_keyboard_queue() > SCANCODE_LAST_MAKE_CODE) {
        current_tick = (int) data_fdps_timer_tick_counter;
        if (current_tick - last_frame_tick >= MARKER_FRAME_TICKS
                || current_tick - last_frame_tick < 0) {
            for (unit_index = 0; unit_index < data_fdps_map_unit_count;
                 unit_index++) {
                unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
                       + unit_index;
                if ((unit->flags & 1) == 0) {
                    fdps_fill_screen_square(
                        marker_pitch * unit->pos_x + MARKER_CENTRE_X
                        - map_width * marker_pitch / 2,
                        unit->pos_y * marker_pitch + MARKER_CENTRE_Y
                        - map_height * marker_pitch / 2,
                        marker_colour_base[unit->side] + pulse_offset,
                        marker_pitch);
                }
            }
            fdps_fill_screen_square(
                marker_pitch * (data_fdps_map_cursor_world_x / TILE_PIXELS)
                + MARKER_CENTRE_X - map_width * marker_pitch / 2,
                marker_pitch * (data_fdps_map_cursor_world_y / TILE_PIXELS)
                + MARKER_CENTRE_Y - map_height * marker_pitch / 2,
                pulse_offset + CURSOR_COLOUR_BASE, marker_pitch);
            pulse_offset += pulse_step;
            if (pulse_offset < 0 || pulse_offset > PULSE_MAX) {
                pulse_step = -pulse_step;
                pulse_offset += pulse_step;
            }
            last_frame_tick = (int) data_fdps_timer_tick_counter;
        }
    }

    for (zoom_step = ZOOM_OUT_FIRST; zoom_step > 0; zoom_step--) {
        fdps_render_map_overview_scaled(page,
            (zoom_step + 1) * (map_centre_x - view_centre_x) / ZOOM_DENOM
            + view_centre_x,
            (map_centre_y - view_centre_y) * (zoom_step + 1) / ZOOM_DENOM
            + view_centre_y,
            zoom_span * zoom_step / ZOOM_SPAN_DENOM + PIXEL_UNITS, tile_table);
        memmove((void *) VGA_SCREEN_BASE, page, (size_t) OVERVIEW_PAGE_BYTES);
    }

    free(page);
    free(tile_table);
    free(tile_sheet);
}

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
