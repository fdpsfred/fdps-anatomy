/* tests/menu.c -- cover for src/menu.c.
 *
 * Expected values come from the assembly at 000160e0 and 00015860, from the
 * descriptor bytes the image actually holds at 00024d60 and from the four
 * command icon ids the image holds at 000146e0; none of them is read off the
 * emitted C.
 *
 * HOW THE RING MENU IS WATCHED.  fdps_render_ring_menu_frame composes its
 * frame on a page it allocates and frees itself and then blits that page's
 * 312x192 window straight over the live mode 13h screen, so the adapter is the
 * only place its output can be read back from.  Every ring case therefore sets
 * mode 13h, fills the frame with a border sentinel, calls, snapshots the 64,000
 * bytes and returns to text mode -- the same way tests/anim.c watches the turn
 * banner.
 *
 * WHY THE PAGE IS SEEDED THROUGH THE HEAP.  The page is not cleared: whatever
 * malloc hands over shows through everywhere the compositor and the four
 * buttons do not reach, and with no scene layers, no map cursor and no units
 * staged the compositor reaches nothing at all.  Each case therefore allocates
 * a block of exactly the page's 0x15180 bytes, fills it with a seed value and
 * frees it immediately before the call, so the block the function is handed is
 * that one.  ring_window_is_the_page_and_nothing_else is that assumption stated
 * as an assertion: if the allocator ever stopped handing the freed block
 * straight back, that case fails first and says so, rather than every other
 * case quietly comparing against rubbish.
 *
 * THE SHEET IS SYNTHETIC AND THAT IS THE POINT.  Command.cel is a member of
 * MISC.VFS and its sub-images are artwork; what is under test is which
 * sub-image id each of the eight blits resolves and where it lands, not what
 * the artwork looks like.  So sub-image n here paints palette index 0x80 + n
 * and nothing else, which makes every pixel on the screen name the id that
 * drew it.  Plate ids paint all 22 rows; every other id paints its top row and
 * leaves the remaining 21 transparent, so a cell shows the icon on its first
 * row and the plate under it on the rest and both are readable at once.
 */
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "mapdraw.h"
#include "testharn.h"
#include "menu.h"

/* CMP dword ptr [EAX],0 / JNZ on the very first iteration: entry 0 selectable
   short-circuits the whole scan. */
static void menu_first_entry_wins(void)
{
    int desc[4];

    desc[0] = 0;
    desc[1] = 1;
    desc[2] = 1;
    desc[3] = 1;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 0);
}

/* The scan walks upward and returns on the first hit, so entry 1 is the answer
   even though entry 2 is selectable too -- INC [EBP-8] / JMP back to the test,
   with the store to the result slot only on the taken branch. */
static void menu_returns_lowest_index(void)
{
    int desc[4];

    desc[0] = 1;
    desc[1] = 0;
    desc[2] = 0;
    desc[3] = 1;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 1);
}

/* Index 3 is inside the bound: JL is against 4, so the fourth entry is still
   tested before the loop falls out. */
static void menu_last_entry_reachable(void)
{
    int desc[4];

    desc[0] = 1;
    desc[1] = 1;
    desc[2] = 1;
    desc[3] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 3);
}

/* Falling out of the loop stores 0xffffffff into the result slot. */
static void menu_all_disabled(void)
{
    int desc[4];

    desc[0] = 1;
    desc[1] = 1;
    desc[2] = 1;
    desc[3] = 1;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), -1);
}

/* The entry test is JNZ, not a sign test: -1 and 7 are both "greyed out", so
   the first selectable entry is 2.  A "> 0 means disabled" reading would
   answer 0 here. */
static void menu_any_nonzero_is_disabled(void)
{
    int desc[4];

    desc[0] = -1;
    desc[1] = 7;
    desc[2] = 0;
    desc[3] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 2);
}

/* The bound is a hard-coded 4 and does not come from the caller: a selectable
   entry sitting at index 4 is never looked at. */
static void menu_bound_is_four(void)
{
    int desc[6];

    desc[0] = 1;
    desc[1] = 1;
    desc[2] = 1;
    desc[3] = 1;
    desc[4] = 0;
    desc[5] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), -1);
}

/* The descriptor is only ever loaded from -- there is no store through the
   parameter anywhere in the function -- and fdps_battle_item_menu reuses its
   copy for the cursor-move legality checks afterwards. */
static void menu_does_not_write_descriptor(void)
{
    int desc[4];

    desc[0] = 1;
    desc[1] = 1;
    desc[2] = 0;
    desc[3] = 1;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 2);
    CHECK_EQ(desc[0], 1);
    CHECK_EQ(desc[1], 1);
    CHECK_EQ(desc[2], 0);
    CHECK_EQ(desc[3], 1);
}

/* The item menu's static descriptor: the sixteen bytes at 00024d60 that
   fdps_battle_item_menu copies onto its stack are all zero, so every entry is
   selectable and the cursor starts on entry 0. */
static void menu_item_menu_descriptor(void)
{
    int desc[4];

    desc[0] = 0;
    desc[1] = 0;
    desc[2] = 0;
    desc[3] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 0);
}

/* fdps_battle_item_menu patches entry 1 to 1 when the list it just built came
   back empty (MOV dword ptr [EBP-0x50],1 at 00025340, which is the second int
   of that copy).  The cursor still starts on entry 0. */
static void menu_item_menu_patched(void)
{
    int desc[4];

    desc[0] = 0;
    desc[1] = 1;
    desc[2] = 0;
    desc[3] = 0;
    CHECK_EQ(fdps_menu_find_first_enabled_entry(desc), 0);
}

/* ------------------------------------------------------------------ */
/* fdps_render_ring_menu_frame @ 00015860                              */
/* ------------------------------------------------------------------ */

/* The adapter, the frame it presents and the two modes the cases switch
   between. */
#define RING_VGA_BASE 0x000a0000
#define RING_SCREEN_W 0x140
#define RING_SCREEN_H 0xc8
#define RING_SCREEN_BYTES (RING_SCREEN_W * RING_SCREEN_H)
#define RING_MODE_TEXT 0x03
#define RING_MODE_320X200X256 0x13

/* The window the function copies out of its page, PUSH 0xa0504 / PUSH 0x138 /
   PUSH 0xc0 at 00015b49 through 00015b58: 312 x 192 landing at screen pixel
   (4,4).  A slot placed at page coordinate (x,y) is therefore at screen pixel
   (y + 4, x + 4), which is what every expected position below is built from. */
#define RING_WINDOW_ROW 4
#define RING_WINDOW_COL 4
#define RING_WINDOW_W 0x138
#define RING_WINDOW_H 0xc0
#define RING_WINDOW_BYTES (RING_WINDOW_W * RING_WINDOW_H)
#define RING_BORDER_BYTES (RING_SCREEN_BYTES - RING_WINDOW_BYTES)

/* The page, and the two fill values that say where a pixel came from: the seed
   is what the page holds where nothing was drawn, the border fill is what the
   screen holds outside the presented window. */
#define RING_PAGE_BYTES 0x15180
#define RING_PAGE_SEED 0x5a
#define RING_BORDER_FILL 0xa5

/* The synthetic Command.cel.  Sub-image n paints palette index 0x80 + n, so
   0x3a -- the highest id any case forms, the greyed-out bank's copy of icon
   0x16 -- has to be inside the table. */
#define RING_SHEET_SPRITES 0x40
#define RING_SHEET_TABLE_AT 0x0f
#define RING_SHEET_STREAMS_AT (RING_SHEET_TABLE_AT + RING_SHEET_SPRITES * 4)
#define RING_SPRITE_W 0x19
#define RING_SPRITE_H 0x16
#define RING_STREAM_BYTES (RING_SPRITE_H * 2)
#define RING_COLOR_BASE 0x80

/* RLE command bytes for a whole 25-pixel row (rle.h): the length is the low six
   bits plus one and the op is the top two.  0x18 is a 25-pixel fill followed by
   its colour byte, 0xd8 a 25-pixel transparent skip on its own. */
#define RING_FILL_CMD 0x18
#define RING_SKIP_CMD 0xd8

/* The three plate sub-images: 0x1a highlighted, 0x1b plain, and 0x1c the plain
   one bumped by a set cmd_disabled entry. */
#define RING_PLATE_HI 0x1a
#define RING_PLATE_PLAIN 0x1b
#define RING_PLATE_PLAIN_OFF 0x1c
#define RING_ICON_BANK 0x24

/* The four command icon ids fdps_battle_system_menu copies out of the template
   at 000146e0: 0x16, 0x0b, 0x0c, 0x13 in slot order up, left, right, down. */
#define RING_ICON_UP 0x16
#define RING_ICON_LEFT 0x0b
#define RING_ICON_RIGHT 0x0c
#define RING_ICON_DOWN 0x13

/* Colour of the pixel a given sub-image id paints. */
#define RING_COLOR(id) (RING_COLOR_BASE + (id))

/* The cursor the ring is hung on: world pixel (144,120) with the camera at the
   origin, so the ring's centre is page pixel (144,120) and the cursor's tile is
   (6,5). */
#define RING_CURSOR_X 144
#define RING_CURSOR_Y 120
#define RING_TILE 0x18

/* The resting radius and its two legs.  cos(0x18 * 0.06544979) * 0x18 is
   0.0000333 and sin is 23.99999999998, and both are truncated toward zero, so
   the ring rests 23 pixels out and not 24. */
#define RING_RESTING_RADIUS 0x18
#define RING_RESTING_DX 0
#define RING_RESTING_DY 23

/* A radius partway through the opening sweep.  0x15 is one of the steps
   fdps_menu_animate_open walks; cos(0x15 * 0.06544979) * 0x15 is 4.0969 and sin
   is 20.5964, truncating to 4 and 20. */
#define RING_SPIRAL_RADIUS 0x15
#define RING_SPIRAL_DX 4
#define RING_SPIRAL_DY 20

/* The unit staged under the cursor for the last two cases: no shadow, no
   status icons, facing right, and a sprite cache whose every entry paints one
   colour so the walk frame the tick happens to land on cannot matter. */
#define RING_CACHE_SPRITES 0x10
#define RING_CACHE_STREAM_BYTES (RING_TILE * 2)
#define RING_TILE_FILL_CMD 0x17
#define RING_UNIT_COLOR 0x70
#define RING_NO_SHADOW_PORTRAIT 0x8e
#define RING_FACING_RIGHT 3
#define RING_STEP_PIXELS 4

static unsigned char ring_sheet[RING_SHEET_STREAMS_AT
                                + RING_SHEET_SPRITES * RING_STREAM_BYTES];
static unsigned char ring_cache[RING_CACHE_SPRITES * 4
                                + RING_CACHE_SPRITES * RING_CACHE_STREAM_BYTES];
static struct fdps_unit_record ring_units[2];
static unsigned char ring_screen[RING_SCREEN_BYTES];

static int ring_is_plate(int sprite_id)
{
    return sprite_id >= RING_PLATE_HI && sprite_id <= RING_PLATE_PLAIN_OFF;
}

static void ring_build_sheet(void)
{
    int sprite_id;
    int row;
    int stream_at;

    for (sprite_id = 0; sprite_id < RING_SHEET_SPRITES; sprite_id++) {
        stream_at = RING_SHEET_STREAMS_AT + sprite_id * RING_STREAM_BYTES;
        *(int *) (ring_sheet + RING_SHEET_TABLE_AT + sprite_id * 4) = stream_at;
        ring_sheet[stream_at] = RING_FILL_CMD;
        ring_sheet[stream_at + 1] = (unsigned char) RING_COLOR(sprite_id);
        for (row = 1; row < RING_SPRITE_H; row++) {
            if (ring_is_plate(sprite_id)) {
                ring_sheet[stream_at + row * 2] = RING_FILL_CMD;
                ring_sheet[stream_at + row * 2 + 1] =
                    (unsigned char) RING_COLOR(sprite_id);
            } else {
                ring_sheet[stream_at + 1 + row] = RING_SKIP_CMD;
            }
        }
    }
}

/* The unit sprite cache is a table fdps_cache_cel_sprite_group builds, so its
   offset table starts at the cache base and not at a .CEL's +0x0f. */
static void ring_build_cache(void)
{
    int sprite_index;
    int row;
    int stream_at;

    for (sprite_index = 0; sprite_index < RING_CACHE_SPRITES; sprite_index++) {
        stream_at = RING_CACHE_SPRITES * 4
                    + sprite_index * RING_CACHE_STREAM_BYTES;
        *(int *) (ring_cache + sprite_index * 4) = stream_at;
        for (row = 0; row < RING_TILE; row++) {
            ring_cache[stream_at + row * 2] = RING_TILE_FILL_CMD;
            ring_cache[stream_at + row * 2 + 1] = RING_UNIT_COLOR;
        }
    }
}

/* Nothing on the map and nothing in the way: no scene layers, no map cursor
   overlay and no units, so the compositor writes nothing into the page and
   every pixel the window shows is either a button or the seed. */
static void ring_stage(void)
{
    ring_build_sheet();
    data_fdps_command_sprite_sheet_ptr = ring_sheet;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = (unsigned char *) ring_units;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = RING_CURSOR_X;
    data_fdps_map_cursor_world_y = RING_CURSOR_Y;
}

/* Puts the staged globals back to the state a freshly started program has
   them in, and every case ends with a call to this.

   IT IS NOT TIDINESS, IT IS THE HEAP.  Three of the globals staged above hold
   blocks the program's own loaders release -- fdps_field_load_chapter_
   resources frees data_fdps_map_unit_array_ptr when data_fdps_map_unit_count
   is non-zero and data_fdps_cel_sprite_cache_ptr when data_fdps_cel_sprite_
   cache_count is, fdps_deploy_map_units does the same, and
   fdps_shutdown_free_resources frees data_fdps_command_sprite_sheet_ptr
   unguarded.  A case that walks away leaving one of them pointing at a static
   in this file hands a later test's call of one of those loaders a free() of
   storage that never came from the heap, which corrupts the allocator for
   every test that follows and is not noticed anywhere near here.  Zero count
   plus null pointer is the bss state those guards were written against, and
   free(NULL) is a no-op, so nothing downstream has to know these cases ran. */
static void ring_unstage(void)
{
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
}

static void ring_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Leave a block of exactly the page's size, filled with the seed, at the head
   of the free list. */
static void ring_seed_page(void)
{
    unsigned char *page;

    page = (unsigned char *) malloc((size_t) RING_PAGE_BYTES);
    if (page != NULL) {
        memset(page, RING_PAGE_SEED, (size_t) RING_PAGE_BYTES);
        free(page);
    }
}

/* Used entries currently in the heap, so a case can say that the page the
   function allocated came back.  A released entry becomes a free entry, which
   is why the used ones are counted and the free ones are not. */
static int ring_used_heap_blocks(void)
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

/* The used-block count on either side of the last ring_run. */
static int ring_blocks_before;
static int ring_blocks_after;

static void ring_run(int *cmd_icons, int *cmd_disabled, int radius,
                     int cursor_dir)
{
    ring_blocks_before = ring_used_heap_blocks();
    ring_set_mode(RING_MODE_320X200X256);
    memset((void *) RING_VGA_BASE, RING_BORDER_FILL,
           (size_t) RING_SCREEN_BYTES);
    ring_seed_page();
    fdps_render_ring_menu_frame(cmd_icons, cmd_disabled, radius, cursor_dir);
    memmove(ring_screen, (void *) RING_VGA_BASE, (size_t) RING_SCREEN_BYTES);
    ring_set_mode(RING_MODE_TEXT);
    ring_blocks_after = ring_used_heap_blocks();
}

static int ring_pixel(int row, int col)
{
    return (int) ring_screen[row * RING_SCREEN_W + col];
}

static int ring_count(int value)
{
    int index;
    int seen;

    seen = 0;
    for (index = 0; index < RING_SCREEN_BYTES; index++) {
        if ((int) ring_screen[index] == value) {
            seen++;
        }
    }
    return seen;
}

/* The four enabled icons in slot order, and the descriptor that greys none of
   them out.  Rewritten by every case that starts from them. */
static void ring_default_menu(int *cmd_icons, int *cmd_disabled)
{
    cmd_icons[0] = RING_ICON_UP;
    cmd_icons[1] = RING_ICON_LEFT;
    cmd_icons[2] = RING_ICON_RIGHT;
    cmd_icons[3] = RING_ICON_DOWN;
    cmd_disabled[0] = 0;
    cmd_disabled[1] = 0;
    cmd_disabled[2] = 0;
    cmd_disabled[3] = 0;
}

/* The premise every ring case rests on, and the window's own geometry.  With
   the ring centre 2000 pixels above the page every slot fails the y test and
   nothing is drawn at all, so the screen has to come back as exactly the seeded
   page inside the 312x192 window at (4,4) and exactly the border fill outside
   it.  The four corner pairs pin the rectangle; the two counts pin that no
   stray byte of either value landed on the wrong side of it.

   It is also the strongest form of the bounds test: JGE against -0x18 at
   000159cb is what drops all four, and if the box were unbounded below the
   plates would have been written 2000 rows before the page. */
static void ring_window_is_the_page_and_nothing_else(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    data_fdps_battle_view_window_origin_y = RING_CURSOR_Y + 2000;
    ring_run(cmd_icons, cmd_disabled, RING_RESTING_RADIUS, 0);

    CHECK_EQ(ring_count(RING_PAGE_SEED), RING_WINDOW_BYTES);
    CHECK_EQ(ring_count(RING_BORDER_FILL), RING_BORDER_BYTES);
    CHECK_EQ(ring_pixel(4, 4), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(3, 4), RING_BORDER_FILL);
    CHECK_EQ(ring_pixel(4, 3), RING_BORDER_FILL);
    CHECK_EQ(ring_pixel(195, 315), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(196, 315), RING_BORDER_FILL);
    CHECK_EQ(ring_pixel(195, 316), RING_BORDER_FILL);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* The resting ring's up slot, box and all.  Its origin is (144, 120 - 23), so
   the cell is screen rows 101..122 and columns 148..172, and the row above it,
   the row below it and the columns either side are still the seed.

   ROW 100 BEING THE SEED IS THE WHOLE POINT.  A sine that came back as exactly
   1, or a conversion that rounded instead of truncating, would put this cell's
   top row at 100 and its plate a pixel further from the cursor in all four
   directions.  Row 101 carries the icon because the icon stream paints only its
   first row; rows 102 to 122 carry the plate underneath it. */
static void ring_resting_slot_is_twenty_three_pixels_out(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    ring_run(cmd_icons, cmd_disabled, RING_RESTING_RADIUS, 0);

    CHECK_EQ(ring_pixel(100, 148), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(101, 148), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(102, 148), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(122, 148), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(123, 148), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(102, 147), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(102, 172), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(102, 173), RING_PAGE_SEED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* All four slots of the resting ring, and the order they stand in.  With dx 0
   and dy 23 the offsets are (0,-23), (-23,0), (23,0) and (0,23), so the icons
   land at screen (101,148), (124,125), (124,171) and (147,148) -- one tile up,
   left, right and down of the cursor, which is the order
   fdps_menu_cursor_input_loop moves the menu cursor in.  Swapping the sin and
   cos legs, or the sign of either, would put left where up is.

   The seed a row above each of the left and right slots is what pins dx at 0:
   they sit on the cursor's own row, not a pixel off it. */
static void ring_resting_slots_stand_up_left_right_down(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    ring_run(cmd_icons, cmd_disabled, RING_RESTING_RADIUS, 0);

    CHECK_EQ(ring_pixel(101, 148), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(124, 125), RING_COLOR(RING_ICON_LEFT));
    CHECK_EQ(ring_pixel(124, 171), RING_COLOR(RING_ICON_RIGHT));
    CHECK_EQ(ring_pixel(147, 148), RING_COLOR(RING_ICON_DOWN));
    CHECK_EQ(ring_pixel(123, 125), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(123, 171), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(146, 148), RING_PAGE_SEED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* Partway through the opening sweep the ring is a quarter turn spread over four
   different offsets, and the four slots are no longer on the axes: at radius
   0x15 the legs are 4 and 20, so the origins are (140,100), (124,124),
   (164,116) and (148,140) and the icons land at screen (104,144), (128,128),
   (120,168) and (144,152).

   A radius used only as a length -- the obvious reading of "radius pixels out"
   -- would put all four back on the axes at 21 pixels and this case would fail
   on every one of them. */
static void ring_spiral_offsets_come_from_one_radius(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    ring_run(cmd_icons, cmd_disabled, RING_SPIRAL_RADIUS, 2);

    CHECK_EQ(ring_pixel(104, 144), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(128, 128), RING_COLOR(RING_ICON_LEFT));
    CHECK_EQ(ring_pixel(120, 168), RING_COLOR(RING_ICON_RIGHT));
    CHECK_EQ(ring_pixel(144, 152), RING_COLOR(RING_ICON_DOWN));
    CHECK_EQ(ring_pixel(103, 144), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(119, 190), RING_PAGE_SEED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* cursor_dir picks exactly one plate.  With it on the left slot that slot gets
   0x1a and the other three keep 0x1b, and the compare is against the loop index
   rather than anything derived from it (CMP EAX,dword ptr [EBP-0x8] / JNZ at
   000159eb).  The slots overlap nowhere at the resting radius, so each plate
   pixel names its own slot. */
static void ring_highlight_follows_cursor_dir(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    ring_run(cmd_icons, cmd_disabled, RING_RESTING_RADIUS, 1);

    CHECK_EQ(ring_pixel(102, 148), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(ring_pixel(125, 125), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(125, 171), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(ring_pixel(148, 148), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* cursor_dir is not range checked and is not clamped: a value outside 0..3
   matches no slot, so all four come out on the plain plate and the menu is
   drawn with nothing selected.  Clamping it into range, which is what a
   defensive rewrite would do, lights the up slot instead. */
static void ring_cursor_dir_out_of_range_highlights_nothing(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    ring_run(cmd_icons, cmd_disabled, RING_RESTING_RADIUS, 9);

    CHECK_EQ(ring_pixel(102, 148), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(ring_pixel(125, 125), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(ring_pixel(125, 171), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(ring_pixel(148, 148), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* The greyed-out flag does two different things to the two sub-images, and this
   is what separates them: it is ADDED to the plate id and MULTIPLIED BY 0x24
   into the icon id.

   The up slot is both highlighted and greyed out, so its plate is 0x1a + 1 --
   the plain plate, which is how a disabled entry loses its highlight without
   any test for it -- and its icon is 0x16 + 0x24 = 0x3a.  The left slot is
   greyed out and not highlighted, so its plate is 0x1b + 1 = 0x1c and its icon
   0x0b + 0x24 = 0x2f.  The right slot is untouched.  Adding the flag to the
   icon id instead of banking it would put 0x17 and 0x0c on the first two
   slots, and 0x0c is the right slot's own enabled icon. */
static void ring_disabled_bumps_the_plate_and_banks_the_icon(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    cmd_disabled[0] = 1;
    cmd_disabled[1] = 1;
    ring_run(cmd_icons, cmd_disabled, RING_RESTING_RADIUS, 0);

    CHECK_EQ(ring_pixel(101, 148), RING_COLOR(RING_ICON_UP + RING_ICON_BANK));
    CHECK_EQ(ring_pixel(102, 148), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(ring_pixel(124, 125), RING_COLOR(RING_ICON_LEFT + RING_ICON_BANK));
    CHECK_EQ(ring_pixel(125, 125), RING_COLOR(RING_PLATE_PLAIN_OFF));
    CHECK_EQ(ring_pixel(124, 171), RING_COLOR(RING_ICON_RIGHT));
    CHECK_EQ(ring_pixel(125, 171), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* The ring hangs on the cursor's position IN THE VIEW, which is its world pixel
   minus the camera scroll.  Moving both by the same 48 pixels leaves every
   button exactly where the previous cases found it; reading the cursor global
   without the subtraction would move the whole ring 48 pixels down and right
   the moment the map scrolled. */
static void ring_camera_scroll_is_subtracted(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    data_fdps_map_cursor_world_x = RING_CURSOR_X + 48;
    data_fdps_map_cursor_world_y = RING_CURSOR_Y + 48;
    data_fdps_battle_view_window_origin_x = 48;
    data_fdps_battle_view_window_origin_y = 48;
    ring_run(cmd_icons, cmd_disabled, RING_RESTING_RADIUS, 0);

    CHECK_EQ(ring_pixel(101, 148), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(124, 125), RING_COLOR(RING_ICON_LEFT));
    CHECK_EQ(ring_pixel(124, 171), RING_COLOR(RING_ICON_RIGHT));
    CHECK_EQ(ring_pixel(147, 148), RING_COLOR(RING_ICON_DOWN));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* The bounds test is on the slot's ORIGIN and lets the plate hang off the page.
   With the ring centre at page x = -24 the up and down slots sit exactly on the
   -0x18 limit and are drawn -- 24 of their 25 columns are off the left of the
   presented window, leaving one pixel of each icon at screen column 4 -- the
   right slot sits at -1 and shows 24 of its columns, and the left slot at -47
   is one past the limit and is not drawn at all.

   The counts are what make the last of those airtight: nothing else on the
   screen paints the left icon's colour, so a slot that had been clipped rather
   than dropped would show up here as any number other than zero, and a limit
   written as -0x19 or 0 would change the other three counts. */
static void ring_slot_origin_test_is_not_a_clip(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    data_fdps_map_cursor_world_x = 0;
    data_fdps_battle_view_window_origin_x = 24;
    data_fdps_map_cursor_world_y = 100;
    ring_run(cmd_icons, cmd_disabled, RING_RESTING_RADIUS, 9);

    CHECK_EQ(ring_count(RING_COLOR(RING_ICON_UP)), 1);
    CHECK_EQ(ring_count(RING_COLOR(RING_ICON_LEFT)), 0);
    CHECK_EQ(ring_count(RING_COLOR(RING_ICON_RIGHT)), 24);
    CHECK_EQ(ring_count(RING_COLOR(RING_ICON_DOWN)), 1);
    CHECK_EQ(ring_pixel(81, 4), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(81, 5), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(82, 4), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(ring_pixel(104, 4), RING_COLOR(RING_ICON_RIGHT));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* Stages one unit whose sprite lands at page (168,138) -- screen rows 118..141,
   columns 148..171 -- however it is placed there, and puts the ring at the
   spiral radius so the up slot's plate covers screen rows 104..125 and columns
   144..168.  Screen (120,150) is inside both, and (119,170) is inside the unit
   and outside every plate. */
static void ring_stage_unit(int tile_x, int walk_step)
{
    ring_stage();
    ring_build_cache();
    memset(ring_units, 0, sizeof(ring_units));
    ring_units[0].pos_x = (unsigned char) tile_x;
    ring_units[0].pos_y = (unsigned char) (RING_CURSOR_Y / RING_TILE);
    ring_units[0].facing = RING_FACING_RIGHT;
    ring_units[0].walk_step = (unsigned char) walk_step;
    ring_units[0].portrait_id = RING_NO_SHADOW_PORTRAIT;
    data_fdps_map_unit_count = 1;
    data_fdps_cel_sprite_cache_ptr = ring_cache;
    data_fdps_map_unit_walk_anim_counter = 0;
    data_fdps_map_unit_status_icon_tick_counter = 0;
    data_fdps_map_unit_status_icon_cycle = 0;
}

/* The unit standing on the cursor tile is drawn a second time, AFTER the four
   buttons, which is what puts the unit whose menu this is on top of them.  The
   compositor has already drawn it underneath, so the two draws differ only in
   their order and only where a plate and the sprite overlap: screen (120,150)
   comes back the unit's colour and not the up slot's plate.

   fdps_battle_find_unit_at_cursor's answer is what gates the second draw, and
   it is used as an index and not as a flag -- PUSH EAX at 00015b36 hands the
   same dword straight to fdps_draw_map_unit -- so the case that follows this
   one moves the unit off the cursor tile without moving its sprite. */
static void ring_unit_under_cursor_is_drawn_over_the_buttons(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage_unit(RING_CURSOR_X / RING_TILE, 0);
    ring_default_menu(cmd_icons, cmd_disabled);
    ring_run(cmd_icons, cmd_disabled, RING_SPIRAL_RADIUS, 0);

    CHECK_EQ(ring_pixel(119, 170), RING_UNIT_COLOR);
    CHECK_EQ(ring_pixel(120, 150), RING_UNIT_COLOR);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* The same picture with the unit one tile to the left and six walk steps into
   its move, which puts its sprite at exactly the same page pixel -- 5 * 24 +
   4 * 6 is 144, the same as 6 * 24 -- while its tile no longer matches the
   cursor's.  fdps_battle_find_unit_at_cursor compares tiles and answers -1, the
   CMP against -0x1 at 00015b26 skips the second draw, and the overlap comes
   back the up slot's highlighted plate instead of the unit.  The pixel inside
   the unit and outside every plate is unchanged, which is what says the
   compositor still drew it and only the second pass is gone. */
static void ring_no_unit_under_cursor_leaves_the_buttons_on_top(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    ring_stage_unit(RING_CURSOR_X / RING_TILE - 1, RING_TILE / RING_STEP_PIXELS);
    ring_default_menu(cmd_icons, cmd_disabled);
    ring_run(cmd_icons, cmd_disabled, RING_SPIRAL_RADIUS, 0);

    CHECK_EQ(ring_pixel(119, 170), RING_UNIT_COLOR);
    CHECK_EQ(ring_pixel(120, 150), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);    ring_unstage();
}

/* ------------------------------------------------------------------ */
/* fdps_menu_animate_open @ 00016130                                   */
/* ------------------------------------------------------------------ */

/* The eight radii the sweep draws, read off the assembly: MOV [EBP-4],1 at
   0001614a, CMP against 0x18 / JL at 00016151 and ADD 4 at 00016159 give
   1, 5, 9, 0xd, 0x11, 0x15, and PUSH 0x18 at 0001617d and PUSH 0x19 at
   00016193 give the two frames after the loop. */
#define OPEN_LADDER_FRAMES 8
#define OPEN_OVERSHOOT_RADIUS 0x19

static int open_ladder[OPEN_LADDER_FRAMES] = {
    1, 5, 9, 0xd, 0x11, 0x15, RING_RESTING_RADIUS, OPEN_OVERSHOOT_RADIUS
};

/* The screen a hand-run ladder leaves, kept apart from ring_screen so a case
   can hold both pictures at once. */
static unsigned char open_reference[RING_SCREEN_BYTES];

/* The sound pack fdps_play_sfx looks the cue up in.  An image whose entry
   count is zero makes the lookup miss without touching the name and without
   allocating anything, so the cue is a silent miss and the frames are all that
   is left to watch -- which is what the original does on a machine whose
   effect pack does not hold the member (audio.h).  Whether the cue reaches the
   mixer is fdps_play_sfx's own contract and is covered where it lives. */
static struct fdps_vfs_image_header open_sfx_pack;

static void open_stage(void)
{
    ring_stage();
    memset(&open_sfx_pack, 0, sizeof(open_sfx_pack));
    data_fdps_audio_basewav_sfx_bank_buf_ptr = (unsigned char *) &open_sfx_pack;
}

/* ring_unstage plus the pack, for the same reason: fdps_shutdown_free_resources
   frees data_fdps_audio_basewav_sfx_bank_buf_ptr unguarded, and the pack above
   is a static. */
static void open_unstage(void)
{
    ring_unstage();
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
}

/* The same watch ring_run keeps, over the whole animation instead of one
   frame.  The page is seeded once: the frames after the first are handed the
   block the frame before them freed, contents and all, which is how the sweep
   accumulates on the screen. */
static void open_run(int *cmd_icons, int *cmd_disabled, int cursor_dir)
{
    ring_blocks_before = ring_used_heap_blocks();
    ring_set_mode(RING_MODE_320X200X256);
    memset((void *) RING_VGA_BASE, RING_BORDER_FILL,
           (size_t) RING_SCREEN_BYTES);
    ring_seed_page();
    fdps_menu_animate_open(cmd_icons, cmd_disabled, cursor_dir);
    memmove(ring_screen, (void *) RING_VGA_BASE, (size_t) RING_SCREEN_BYTES);
    ring_set_mode(RING_MODE_TEXT);
    ring_blocks_after = ring_used_heap_blocks();
}

/* The same run with the frames called out by hand, from the same seeded page
   and the same filled screen, so the two pictures are comparable byte for
   byte. */
static void open_run_ladder(int *cmd_icons, int *cmd_disabled, int cursor_dir,
                            int *radii, int frames)
{
    int frame;

    ring_set_mode(RING_MODE_320X200X256);
    memset((void *) RING_VGA_BASE, RING_BORDER_FILL,
           (size_t) RING_SCREEN_BYTES);
    ring_seed_page();
    for (frame = 0; frame < frames; frame++) {
        fdps_render_ring_menu_frame(cmd_icons, cmd_disabled, radii[frame],
                                    cursor_dir);
    }
    memmove(open_reference, (void *) RING_VGA_BASE, (size_t) RING_SCREEN_BYTES);
    ring_set_mode(RING_MODE_TEXT);
}

/* The whole sweep is those eight radii in that order and nothing else.  The
   frames are not erased between one another -- each one composes on the page
   the one before it freed -- so every frame that ran leaves something on the
   screen and the finished picture names the whole ladder, not just its last
   frame.  Byte-equal against the hand-run ladder is therefore the strongest
   statement available here: an extra frame, a missing one, a different step or
   a different starting radius all change the picture. */
static void open_ladder_is_the_eight_frames(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    open_run(cmd_icons, cmd_disabled, 0);
    open_run_ladder(cmd_icons, cmd_disabled, 0, open_ladder,
                    OPEN_LADDER_FRAMES);

    CHECK_EQ(memcmp(ring_screen, open_reference, (size_t) RING_SCREEN_BYTES),
             0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);
    open_unstage();
}

/* What the case above is worth depends on a shorter ladder being a different
   picture, so both plausible shortenings are run and neither is allowed to
   match: dropping the overshoot frame leaves the buttons on the resting ring,
   and dropping the six loop frames leaves the spiral trail off the page. */
static void open_ladder_has_teeth(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    open_run(cmd_icons, cmd_disabled, 0);

    open_run_ladder(cmd_icons, cmd_disabled, 0, open_ladder,
                    OPEN_LADDER_FRAMES - 1);
    CHECK_EQ(memcmp(ring_screen, open_reference,
                    (size_t) RING_SCREEN_BYTES) != 0, 1);

    open_run_ladder(cmd_icons, cmd_disabled, 0,
                    open_ladder + OPEN_LADDER_FRAMES - 2, 2);
    CHECK_EQ(memcmp(ring_screen, open_reference,
                    (size_t) RING_SCREEN_BYTES) != 0, 1);
    open_unstage();
}

/* The animation stops a pixel past where the menu rests.  At radius 0x19 the
   legs are -1 and 24, so the up slot's origin is page (145,96) and its cell is
   screen rows 100..121, columns 149..173 -- one row up and one column right of
   the resting cell the frame before it drew at page (144,97).  Both are
   readable at once because the last frame does not erase the one before it:
   column 148 still carries the resting frame's own top row, and row 122 still
   carries the bottom of its plate.

   Row 99 is the seed because 24 is the largest leg any of the eight frames
   produces.  A sweep that ended on the resting radius would leave row 100 seed
   as well and put the icon on column 148 at row 101 with nothing to its
   right. */
static void open_last_frame_overshoots_the_resting_ring(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    open_run(cmd_icons, cmd_disabled, 0);

    CHECK_EQ(ring_pixel(99, 149), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(100, 149), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(101, 149), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(101, 148), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(122, 148), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);
    open_unstage();
}

/* Two of the loop's own frames, still on the screen when the sweep is over.
   At radius 0x11 the legs are 7 and 15 and the up slot's cell starts at screen
   (109,141); at radius 0x15 they are 4 and 20 and it starts at (104,144).
   Neither cell is reached by any later frame, so both survive, and the icon on
   the cell's first row with the plate on the row under it is what says a whole
   frame was drawn there rather than a stray column.

   These are the two radii the loop's step decides: 1 + 4 * 4 and 1 + 5 * 4.  A
   step of 3 or 5, or a first radius of 0, puts the intermediate cells
   somewhere else entirely and neither pixel is the icon any more. */
static void open_loop_frames_stay_on_the_page(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    open_run(cmd_icons, cmd_disabled, 0);

    CHECK_EQ(ring_pixel(109, 141), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(110, 141), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(104, 144), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(105, 144), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);
    open_unstage();
}

/* All three arguments reach every frame exactly as they came in.  cursor_dir 2
   lights the right slot of the last frame -- cell at screen (125,172) -- and
   leaves the up slot on the plain plate; the greyed-out fourth entry banks its
   icon by 0x24 and bumps its plate by one in the last frame's down cell at
   (148,147); and the two descriptor arrays are unchanged afterwards, which is
   what lets fdps_battle_item_menu reuse its copy for the cursor loop that
   follows this call. */
static void open_forwards_its_arguments(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    cmd_disabled[3] = 1;
    open_run(cmd_icons, cmd_disabled, 2);

    CHECK_EQ(ring_pixel(125, 172), RING_COLOR(RING_ICON_RIGHT));
    CHECK_EQ(ring_pixel(126, 172), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(101, 149), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(ring_pixel(148, 147), RING_COLOR(RING_ICON_DOWN + RING_ICON_BANK));
    CHECK_EQ(ring_pixel(149, 147), RING_COLOR(RING_PLATE_PLAIN_OFF));
    CHECK_EQ(cmd_icons[0], RING_ICON_UP);
    CHECK_EQ(cmd_icons[1], RING_ICON_LEFT);
    CHECK_EQ(cmd_icons[2], RING_ICON_RIGHT);
    CHECK_EQ(cmd_icons[3], RING_ICON_DOWN);
    CHECK_EQ(cmd_disabled[0], 0);
    CHECK_EQ(cmd_disabled[1], 0);
    CHECK_EQ(cmd_disabled[2], 0);
    CHECK_EQ(cmd_disabled[3], 1);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);
    open_unstage();
}

/* ------------------------------------------------------------------ */
/* fdps_menu_animate_close @ 000161b0                                  */
/* ------------------------------------------------------------------ */

/* The six radii the retraction draws, read off the assembly: MOV [EBP-4],0x17
   at 000161ca, CMP against 0 / JG at 000161d1 and ADD -4 at 000161d9.  There is
   no frame after the loop, unlike the opening sweep. */
#define CLOSE_LADDER_FRAMES 6

static int close_ladder[CLOSE_LADDER_FRAMES] = {
    0x17, 0x13, 0xf, 0xb, 7, 3
};

/* The same watch open_run keeps, over the retraction instead of the sweep. */
static void close_run(int *cmd_icons, int *cmd_disabled, int cursor_dir)
{
    ring_blocks_before = ring_used_heap_blocks();
    ring_set_mode(RING_MODE_320X200X256);
    memset((void *) RING_VGA_BASE, RING_BORDER_FILL,
           (size_t) RING_SCREEN_BYTES);
    ring_seed_page();
    fdps_menu_animate_close(cmd_icons, cmd_disabled, cursor_dir);
    memmove(ring_screen, (void *) RING_VGA_BASE, (size_t) RING_SCREEN_BYTES);
    ring_set_mode(RING_MODE_TEXT);
    ring_blocks_after = ring_used_heap_blocks();
}

/* The whole retraction is those six radii in that order and nothing else.  The
   frames accumulate on the page for the same reason the opening sweep's do, so
   the finished picture names the whole ladder and byte-equal against the
   hand-run one is the strongest statement available: a seventh frame, a missing
   one, a different step or a different starting radius all change it. */
static void close_ladder_is_the_six_frames(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    close_run(cmd_icons, cmd_disabled, 0);
    open_run_ladder(cmd_icons, cmd_disabled, 0, close_ladder,
                    CLOSE_LADDER_FRAMES);

    CHECK_EQ(memcmp(ring_screen, open_reference, (size_t) RING_SCREEN_BYTES),
             0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);
    open_unstage();
}

/* What the case above is worth depends on a shorter ladder being a different
   picture, so both ends are cut and neither is allowed to match: dropping the
   last frame leaves the buttons three pixels further out, and dropping the
   first leaves the outermost cell unpainted. */
static void close_ladder_has_teeth(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    close_run(cmd_icons, cmd_disabled, 0);

    open_run_ladder(cmd_icons, cmd_disabled, 0, close_ladder,
                    CLOSE_LADDER_FRAMES - 1);
    CHECK_EQ(memcmp(ring_screen, open_reference,
                    (size_t) RING_SCREEN_BYTES) != 0, 1);

    open_run_ladder(cmd_icons, cmd_disabled, 0, close_ladder + 1,
                    CLOSE_LADDER_FRAMES - 1);
    CHECK_EQ(memcmp(ring_screen, open_reference,
                    (size_t) RING_SCREEN_BYTES) != 0, 1);
    open_unstage();
}

/* The retraction starts one pixel in from where the menu was resting.  At
   radius 0x17 the legs are 1 and 22, so the up slot's origin is page (143,98)
   and its cell is screen rows 102..123, columns 147..171 -- the outermost cell
   of the whole animation, which nothing drawn afterwards reaches.

   SCREEN (101,148) BEING THE SEED IS THE POINT.  That is where the resting
   frame puts the up icon, and a ladder that began at 0x18 rather than 0x17
   would paint it. */
static void close_first_frame_is_inside_the_resting_ring(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    close_run(cmd_icons, cmd_disabled, 0);

    CHECK_EQ(ring_pixel(101, 148), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(101, 147), RING_PAGE_SEED);
    CHECK_EQ(ring_pixel(102, 147), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(103, 147), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);
    open_unstage();
}

/* The last frame is radius 3 and the buttons are still standing when the
   function returns.  At radius 3 the legs are 2 and 0, so the up slot's origin
   is page (142,120) and its cell begins at screen (124,146).

   SCREEN (124,148) IS WHAT SAYS NO RADIUS-0 FRAME WAS DRAWN.  It comes back the
   right slot's plain plate here, because at radius 3 the right slot's cell
   covers it and nothing later does; a seventh frame at radius 0 would stack all
   four slots on the cursor itself and leave the down slot's icon there. */
static void close_stops_three_pixels_out(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    close_run(cmd_icons, cmd_disabled, 0);

    CHECK_EQ(ring_pixel(124, 146), RING_COLOR(RING_ICON_UP));
    CHECK_EQ(ring_pixel(125, 146), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(124, 148), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);
    open_unstage();
}

/* All three arguments reach every frame exactly as they came in.  cursor_dir 2
   keeps the right slot highlighted all the way in -- its last cell is at screen
   (122,148) -- and leaves the up slot on the plain plate; the greyed-out fourth
   entry banks its icon by 0x24 and bumps its plate by one in the last frame's
   down cell at (124,150); and neither descriptor array is written back, which
   is what lets fdps_battle_action_menu read its own copy again after the menu
   has closed. */
static void close_forwards_its_arguments(void)
{
    int cmd_icons[4];
    int cmd_disabled[4];

    open_stage();
    ring_default_menu(cmd_icons, cmd_disabled);
    cmd_disabled[3] = 1;
    close_run(cmd_icons, cmd_disabled, 2);

    CHECK_EQ(ring_pixel(122, 148), RING_COLOR(RING_ICON_RIGHT));
    CHECK_EQ(ring_pixel(123, 148), RING_COLOR(RING_PLATE_HI));
    CHECK_EQ(ring_pixel(124, 150), RING_COLOR(RING_ICON_DOWN + RING_ICON_BANK));
    CHECK_EQ(ring_pixel(125, 150), RING_COLOR(RING_PLATE_PLAIN_OFF));
    CHECK_EQ(ring_pixel(125, 146), RING_COLOR(RING_PLATE_PLAIN));
    CHECK_EQ(cmd_icons[0], RING_ICON_UP);
    CHECK_EQ(cmd_icons[1], RING_ICON_LEFT);
    CHECK_EQ(cmd_icons[2], RING_ICON_RIGHT);
    CHECK_EQ(cmd_icons[3], RING_ICON_DOWN);
    CHECK_EQ(cmd_disabled[0], 0);
    CHECK_EQ(cmd_disabled[1], 0);
    CHECK_EQ(cmd_disabled[2], 0);
    CHECK_EQ(cmd_disabled[3], 1);
    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(ring_blocks_after - ring_blocks_before, 0);
    open_unstage();
}

void run_menu_tests(void)
{
    RUN_TEST(menu_first_entry_wins);
    RUN_TEST(menu_returns_lowest_index);
    RUN_TEST(menu_last_entry_reachable);
    RUN_TEST(menu_all_disabled);
    RUN_TEST(menu_any_nonzero_is_disabled);
    RUN_TEST(menu_bound_is_four);
    RUN_TEST(menu_does_not_write_descriptor);
    RUN_TEST(menu_item_menu_descriptor);
    RUN_TEST(menu_item_menu_patched);
    RUN_TEST(ring_window_is_the_page_and_nothing_else);
    RUN_TEST(ring_resting_slot_is_twenty_three_pixels_out);
    RUN_TEST(ring_resting_slots_stand_up_left_right_down);
    RUN_TEST(ring_spiral_offsets_come_from_one_radius);
    RUN_TEST(ring_highlight_follows_cursor_dir);
    RUN_TEST(ring_cursor_dir_out_of_range_highlights_nothing);
    RUN_TEST(ring_disabled_bumps_the_plate_and_banks_the_icon);
    RUN_TEST(ring_camera_scroll_is_subtracted);
    RUN_TEST(ring_slot_origin_test_is_not_a_clip);
    RUN_TEST(ring_unit_under_cursor_is_drawn_over_the_buttons);
    RUN_TEST(ring_no_unit_under_cursor_leaves_the_buttons_on_top);    RUN_TEST(open_ladder_is_the_eight_frames);
    RUN_TEST(open_ladder_has_teeth);
    RUN_TEST(open_last_frame_overshoots_the_resting_ring);
    RUN_TEST(open_loop_frames_stay_on_the_page);
    RUN_TEST(open_forwards_its_arguments);
    RUN_TEST(close_ladder_is_the_six_frames);
    RUN_TEST(close_ladder_has_teeth);
    RUN_TEST(close_first_frame_is_inside_the_resting_ring);
    RUN_TEST(close_stops_three_pixels_out);
    RUN_TEST(close_forwards_its_arguments);
}
