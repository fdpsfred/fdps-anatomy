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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
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

/* ------------------------------------------------------------------ *
 * fdps_battle_map_overview @ 0002e160
 *
 * Expected values come from the assembly and from nothing else: MOV
 * [EBP-0x38],0x4 and MOV [EBP-0x30],0x280 at 0002e16c for the default marker
 * pitch and zoom span against CMP [EBP-0x14],0x28 / JLE at 0002e201 and the 3
 * / 0x380 pair behind it; ADD EBX,0xa0 at 0002e457 and ADD EBX,0x64 at
 * 0002e431 for where the map is centred on screen; IMUL EDX,[EBP-0x38] / SAR
 * EAX,0x1 at 0002e447 and 0002e421 for the half-map offsets that are taken off
 * them; MOV EBX,0x18 / IDIV EBX at 0002e47a and 0002e4ad for the cursor's
 * divide down to a tile; ADD EAX,0x82 at 0002e474 for the cursor's colour
 * base; the three dwords 0x28, 0xb4 and 0xd6 read out of 0x2b294 for the unit
 * bases; AND AL,0x1 / JNZ at 0002e3ec for the skip; MOV [EBP-0x24],0x7 with
 * MOV [EBP-0x20],-0x1 and the turn-round at 0002e4ef for the pulse's range;
 * and AND EAX,0xff / CMP EAX,0x7f / JLE at 0002e377 for what ends the hold.
 * The square being one pixel smaller than the pitch is fdps_fill_screen_square
 * at 0002e5e0, covered in tests/blit.c and relied on here.
 *
 * WHAT THE RUN IS OBSERVED THROUGH.  Two channels.  The markers are painted
 * straight into the mode 13h aperture by fdps_fill_screen_square, so every run
 * puts the adapter into that mode the way the game does and reads the frame
 * back; and the heap is walked the way tests/main.c walks it.
 *
 * WHY THE FRAME IS CAPTURED FROM INSIDE THE INTERRUPT.  The fly-out that
 * follows the hold presents the whole 0xfa00-byte page over the screen six
 * times, so by the time the call returns nothing of the markers is left.  The
 * capture therefore happens while the hold is still running, out of the same
 * timer handler that paces it.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED AT ALL.  The markers are redrawn only
 * when data_fdps_timer_tick_counter has moved, and in the game that counter is
 * advanced by the timer handler off AIL's interrupt.  Nothing advances it in a
 * test image, so no marker would ever be drawn.  Each run hooks IRQ0 for the
 * duration of the call and chains to the handler that was there.
 *
 * HOW THE HOLD IS ENDED.  The call empties the ring itself before the hold
 * begins, so nothing staged beforehand survives; the ring is fed while the
 * call is running, exactly as the game's INT 09h handler feeds it, and only
 * when it is empty -- which makes one push mean one code read.  Every push is
 * 0x80, the exact value the threshold must NOT let through, until the run has
 * gone far enough, and then one 0x7f, the first value below it.  The call
 * coming back at all is therefore the reading that 0x80 does not end the hold
 * and 0x7f does; the ring being empty answering 0xff does not end it either,
 * because the hold outlasts every gap between pushes.
 *
 * WHY THE MAP IS ONE TILE WIDE OR ONE TILE TALL AND NEVER BOTH.  The tile
 * pointers the render walks are filled from fdps_map_load_tile_info, whose
 * three other layer globals ticket 23 has not written, and they point into the
 * block fdps_cel_expand_sheet_24x24 builds out of a tile sheet ticket 23 has
 * not written either -- so what the render would put on the screen is not
 * determined from here.  With either of the map's two dimensions 0 the
 * render's own bounds reject every tile, the page stays cleared and the
 * presented frame is all zeroes, so a non-zero byte in the capture is a marker
 * and nothing else.  The other dimension is still whatever the case wants,
 * which is what keeps the halving of the map's size in view.
 *
 * THE TILE SHEET IS STAGED ALL THE SAME.  fdps_cel_expand_sheet_24x24 takes
 * its sprite count out of the header at data_fdps_scene_layer_tile_sheet_ptrs
 * [0] and sizes an allocation from it, and that global is null until ticket
 * 23: left alone it reads a count out of the front of memory and allocates
 * against whatever it finds.  A header naming no sprites makes the expansion
 * exactly its own six-byte block, which is all these cases ask of it.
 *
 * WHY THE MARKER COLOUR IS CHECKED AS A RANGE.  The pulse advances once per
 * drawn frame and the capture lands on an unpredictable one, so what is
 * determined is the base and the pulse's range, not their sum.  The four bases
 * are at least 0x2a apart, so a window of eight still tells any two of them
 * apart, and a marker drawn from the wrong row of the table fails.
 * ------------------------------------------------------------------ */

#define OV_VGA_BASE 0x000a0000
#define OV_MODE_TEXT 0x03
#define OV_MODE_13H 0x13
#define OV_SCREEN_PITCH 320
#define OV_SCREEN_ROWS 200
#define OV_SCREEN_BYTES ((long) OV_SCREEN_PITCH * OV_SCREEN_ROWS)

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the hold spins. */
#define OV_TIMER_VECTOR 8

/* The two scancodes and when each is fed.  Six pushes of the value the
   threshold must not let through is five reads and so at least four ticks
   inside the hold, which is more than the two the marker gate needs. */
#define OV_AT_THRESHOLD 0x80
#define OV_BELOW_THRESHOLD 0x7f
#define OV_CAPTURE_AFTER 6
#define OV_END_AFTER 7

/* The pulse's range, added to whichever base a marker draws from. */
#define OV_PULSE_SPAN 8

/* The three unit colour bases and the cursor's. */
#define OV_BASE_SIDE_0 0x28
#define OV_BASE_SIDE_1 0xb4
#define OV_BASE_SIDE_2 0xd6
#define OV_BASE_CURSOR 0x82

#define OV_UNIT_SLOTS 4

/* The staged tile sheet: a whole struct fdps_cel_header of zeroes but for the
   magic, so the sprite count it publishes is 0. */
#define OV_CEL_HEADER_BYTES 15

static unsigned char ov_tile_sheet[OV_CEL_HEADER_BYTES];
static struct fdps_unit_record ov_units[OV_UNIT_SLOTS];
static unsigned char *ov_capture;
static void (__interrupt __far *ov_saved_timer)();
static int ov_pushes;
static int ov_at_threshold;
static int ov_captured;
static int ov_returned;
static int ov_heap_before;
static int ov_heap_after;

/* Advances the counter the way the game's timer does, feeds the ring the way
   the game's INT 09h handler feeds it, and takes the frame while the hold
   still owns the screen. */
static void __interrupt __far ov_timer_isr(void)
{
    unsigned char code;

    ++data_fdps_timer_tick_counter;
    if (data_fdps_input_scancode_queue_head
            == data_fdps_input_scancode_queue_write_index) {
        if (ov_pushes == OV_CAPTURE_AFTER && ov_capture != NULL) {
            memmove(ov_capture, (void *) OV_VGA_BASE,
                    (size_t) OV_SCREEN_BYTES);
            ov_captured = 1;
        }
        code = OV_AT_THRESHOLD;
        if (ov_pushes >= OV_END_AFTER) {
            code = OV_BELOW_THRESHOLD;
        } else {
            ov_at_threshold++;
        }
        data_fdps_input_scancode_queue[
            data_fdps_input_scancode_queue_write_index] = code;
        data_fdps_input_scancode_queue_write_index =
            (data_fdps_input_scancode_queue_write_index + 1)
            % SCANCODE_QUEUE_LEN;
        ov_pushes++;
    }
    _chain_intr(ov_saved_timer);
}

/* Used entries currently in the heap, counted the way tests/main.c counts
   them: a used entry becomes a free entry the moment it is released. */
static int ov_used_heap_blocks(void)
{
    struct _heapinfo entry;
    int used;

    used = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) {
            used++;
        }
    }
    return used;
}

static void ov_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Everything the call reads that a test image does not otherwise have.  The
   map header goes through stage() above, so the width and height are written
   at +7 and +9 of a buffer that is 0xaa everywhere else. */
static void ov_stage(int map_width, int map_height, int unit_count,
                     int cursor_world_x, int cursor_world_y)
{
    int slot;

    stage(map_width, map_height);
    memset(ov_tile_sheet, 0, (size_t) OV_CEL_HEADER_BYTES);
    ov_tile_sheet[0] = 'C';
    ov_tile_sheet[1] = 'E';
    ov_tile_sheet[2] = 'L';
    data_fdps_scene_layer_tile_sheet_ptrs[0] = ov_tile_sheet;
    for (slot = 0; slot < OV_UNIT_SLOTS; slot++) {
        memset(&ov_units[slot], 0, sizeof(struct fdps_unit_record));
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ov_units;
    data_fdps_map_unit_count = unit_count;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = cursor_world_x;
    data_fdps_map_cursor_world_y = cursor_world_y;
    data_fdps_timer_tick_counter = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    ov_pushes = 0;
    ov_at_threshold = 0;
    ov_captured = 0;
    ov_returned = 0;
}

/* One whole overview, with the adapter in the mode the game shows it in and a
   timer interrupt running.  Leaves the held frame in ov_capture[] and the
   other readings in their statics. */
static void ov_run(void)
{
    ov_heap_before = ov_used_heap_blocks();
    ov_set_mode(OV_MODE_13H);
    ov_saved_timer = _dos_getvect(OV_TIMER_VECTOR);
    _dos_setvect(OV_TIMER_VECTOR, ov_timer_isr);
    fdps_battle_map_overview();
    _dos_setvect(OV_TIMER_VECTOR, ov_saved_timer);
    ov_set_mode(OV_MODE_TEXT);
    ov_heap_after = ov_used_heap_blocks();
    ov_returned = 1;
}

/* The globals the production loaders free, put back the way a freshly loaded
   image has them: a pointer left at one of this file's statics is a free() of
   non-heap memory the next time any loader runs. */
static void ov_restore(void)
{
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_sheet_ptrs[0] = NULL;
}

/* Whether the captured pixel is a marker drawn from the given colour base:
   base + 0 through base + 7, the pulse's whole range. */
static int ov_marker(int x, int y, int base)
{
    int value;

    value = ov_capture[(long) y * OV_SCREEN_PITCH + x];
    return value >= base && value < base + OV_PULSE_SPAN;
}

static int ov_pixel(int x, int y)
{
    return ov_capture[(long) y * OV_SCREEN_PITCH + x];
}

/* ---------------------------------------------------------------------- */

/* 0x80 does not end the hold and 0x7f does.  Every code fed before the run has
   gone far enough is 0x80, so the call returning at all says the compare is
   against 0x7f and not against 0xff, and the count of those says the hold sat
   through several of them rather than ending on the first thing it read.  An
   empty ring answers 0xff, which is above the threshold too, so the gaps
   between pushes do not end it either.

   The heap is read on the same run: the page, the tile pointer table and the
   block fdps_cel_expand_sheet_24x24 hands back are all three taken and given
   back inside the call, so a run leaves the heap exactly as it found it. */
static void the_hold_ends_on_a_make_code_and_on_nothing_else(void)
{
    ov_capture = (unsigned char *) malloc((size_t) OV_SCREEN_BYTES);
    ov_stage(0, 0, 0, 0, 0);
    ov_run();

    CHECK_EQ(ov_returned, 1);
    CHECK_EQ(ov_at_threshold >= 2, 1);
    CHECK_EQ(ov_heap_after, ov_heap_before);

    free(ov_capture);
    ov_capture = NULL;
    ov_restore();
}

/* Where a unit's marker lands, with the map 20 tiles wide.  x is 0xa0 plus the
   tile column times the pitch, less half the map's width in pixels: 160 + 4x -
   40.  y has no half to take off here, because the map's height is 0, so it is
   just 100 + 4y.  The square is three pixels on a side and not four, and the
   fourth column and row are the gap that leaves between neighbouring markers.

   Each of the three sides draws from its own row of the colour table, the unit
   with bit 0 of its flag byte set draws nothing at all, and the cursor draws
   last from its own base with its position divided down from world pixels --
   233 and 143, neither a multiple of 24, so a division that rounded the other
   way would put it a tile further along. */
static void unit_markers_are_centred_on_the_maps_width(void)
{
    ov_capture = (unsigned char *) malloc((size_t) OV_SCREEN_BYTES);
    ov_stage(20, 0, 4, 9 * 24 + 17, 5 * 24 + 23);
    ov_units[0].pos_x = 0;
    ov_units[0].pos_y = 0;
    ov_units[0].side = 0;
    ov_units[1].pos_x = 3;
    ov_units[1].pos_y = 2;
    ov_units[1].side = 1;
    ov_units[2].pos_x = 5;
    ov_units[2].pos_y = 1;
    ov_units[2].side = 2;
    ov_units[3].pos_x = 7;
    ov_units[3].pos_y = 3;
    ov_units[3].side = 0;
    ov_units[3].flags = 1;
    ov_run();

    CHECK_EQ(ov_captured, 1);

    CHECK_EQ(ov_marker(120, 100, OV_BASE_SIDE_0), 1);
    CHECK_EQ(ov_marker(122, 102, OV_BASE_SIDE_0), 1);
    CHECK_EQ(ov_pixel(123, 100), 0);
    CHECK_EQ(ov_pixel(120, 103), 0);
    CHECK_EQ(ov_pixel(119, 100), 0);
    CHECK_EQ(ov_pixel(120, 99), 0);

    CHECK_EQ(ov_marker(132, 108, OV_BASE_SIDE_1), 1);
    CHECK_EQ(ov_marker(140, 104, OV_BASE_SIDE_2), 1);
    CHECK_EQ(ov_pixel(148, 112), 0);
    CHECK_EQ(ov_marker(156, 120, OV_BASE_CURSOR), 1);

    free(ov_capture);
    ov_capture = NULL;
    ov_restore();
}

/* A map more than 0x28 tiles tall drops the pitch to 3, so the markers are two
   pixels on a side and three apart.  41 rows also makes the half-height an odd
   division: 41 * 3 is 123 and the halving truncates to 61, so the map's top
   row sits at 100 - 61 = 39 rather than at 100 - 62.

   The map's width is 0 here, so nothing is taken off x and the tile column
   alone places the marker; between this case and the one above, each of the
   two halvings is the only one in play once. */
static void a_tall_map_shrinks_the_markers_and_pulls_them_up(void)
{
    ov_capture = (unsigned char *) malloc((size_t) OV_SCREEN_BYTES);
    ov_stage(0, 41, 2, 4 * 24, 6 * 24);
    ov_units[0].pos_x = 0;
    ov_units[0].pos_y = 0;
    ov_units[0].side = 0;
    ov_units[1].pos_x = 2;
    ov_units[1].pos_y = 3;
    ov_units[1].side = 2;
    ov_run();

    CHECK_EQ(ov_captured, 1);

    CHECK_EQ(ov_marker(160, 39, OV_BASE_SIDE_0), 1);
    CHECK_EQ(ov_marker(161, 40, OV_BASE_SIDE_0), 1);
    CHECK_EQ(ov_pixel(162, 39), 0);
    CHECK_EQ(ov_pixel(160, 41), 0);
    CHECK_EQ(ov_pixel(160, 38), 0);

    CHECK_EQ(ov_marker(166, 48, OV_BASE_SIDE_2), 1);
    CHECK_EQ(ov_marker(172, 57, OV_BASE_CURSOR), 1);

    free(ov_capture);
    ov_capture = NULL;
    ov_restore();
}

void run_overview_tests(void)
{
    RUN_TEST(clears_the_whole_page);
    RUN_TEST(draws_one_source_pixel_per_output_pixel);
    RUN_TEST(fills_exactly_the_window);
    RUN_TEST(floors_a_negative_left_edge);
    RUN_TEST(floors_a_negative_top_edge);
    RUN_TEST(doubles_the_step_at_zoom);
    RUN_TEST(the_hold_ends_on_a_make_code_and_on_nothing_else);
    RUN_TEST(unit_markers_are_centred_on_the_maps_width);
    RUN_TEST(a_tall_map_shrinks_the_markers_and_pulls_them_up);
}
