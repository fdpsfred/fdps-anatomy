/* tests/statunit.c -- cover for src/statunit.c.
 *
 * Three functions, and each is observed through the surface it writes rather
 * than through anything it returns.
 *
 * fdps_unit_status_window_wait_input IS COVERED ONLY ON THE PASS THAT ENDS
 * THE WAIT.  Its loop polls the keyboard and draws a frame whenever the timer
 * tick has moved; nothing in the test binary moves that tick, because the
 * counter is written by the game's timer interrupt handler and no test
 * installs it.  So a call that is made to draw one frame cannot then be made
 * to return: the auto-repeat reader answers 0xff for every poll after the
 * first while the tick stands still, and the loop spins.  The cases below
 * therefore drive the reader to hand back an accepted code on the first poll
 * and pin what that pass does -- which code ends the wait, that the poll is
 * tested before the tick, that nothing is drawn or allocated, and that the
 * idle flag draws exactly one rand or none.  Everything the drawing pass does
 * is a playtest contract until the sprite cache and the shadow sheet hold
 * real sheets and a timer is running.
 *
 * fdps_draw_unit_status_panel and fdps_draw_unit_inventory are each pinned
 * against fabricated sheets whose pixels say which sprite, which glyph and
 * which colour row drew them; the two sections below set out how each fixture
 * encodes that.  Every expected value is the literal the assembly pushes and
 * none of them is read off the emitted C.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "statunit.h"

#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES (VGA_SCREEN_PITCH * VGA_SCREEN_ROWS)

/* The window image the wait loop is handed.  A whole 320x200 frame, because
   the loop stamps the unit's cell into it at a fixed offset and reads nothing
   about its extent. */
static unsigned char window[VGA_SCREEN_BYTES];

/* The window image's pixel at a position.  Every position holds a different
   value from its neighbours in either direction, so a cell stamped one row or
   one column out is caught rather than matching anyway. */
static int win_pixel(int row, int col)
{
    return ((row * 31 + col * 17) & 0x7f) | 0x80;
}

static void stage_window_image(void)
{
    int row;
    int col;

    for (row = 0; row < VGA_SCREEN_ROWS; row++) {
        for (col = 0; col < VGA_SCREEN_PITCH; col++) {
            window[row * VGA_SCREEN_PITCH + col] =
                (unsigned char) win_pixel(row, col);
        }
    }
}

/* Used entries currently in the heap.  A used entry becomes a free entry the
   moment it is released, possibly merged with a neighbour, so the used ones
   are counted and the free ones are not. */
static int used_heap_blocks(void)
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

/* The seed the two rand cases below run from.  Any seed does, as long as its
   first two draws differ, which the last assertion of that case checks. */
#define RAND_SEED 7

/* Scancodes the wait loop is driven with.  0x1c is Enter, one of the codes
   fdps_unit_item_select_loop acts on (the battle window discards the result
   and closes on any accepted code); 0x7f is the last code the loop will hand
   back at all (CMP ...,0x7f / JLE at 00017117 is <=, so 0x7f ends the wait
   and 0x80 would not). */
#define SCANCODE_ENTER 0x1c
#define SCANCODE_LAST_ACCEPTED 0x7f

/* Puts the auto-repeat reader in the state where its very next poll reports
   `code` unchanged: the latch holds the code and the repeat filter remembers
   a different one, which is that reader's "a key went down" path
   (src/keybd.h).  0x100 is not a byte, so it is different from every latch
   value including 0xff. */
static void arm_next_scancode(unsigned int code)
{
    data_fdps_input_last_scancode = (unsigned char) code;
    data_fdps_input_key_repeat_prev_scancode = 0x100;
    data_fdps_input_key_repeat_counter = 0;
}

/* Which codes end the wait.  The loop keeps running while the poll answers
   above 0x7f and returns the first answer at or below it, unchanged and
   without sign-extending -- 0x7f is the largest value that gets out, and the
   test that lets it out is signed (JLE), so the code is carried in an int.

   0x1c is an ordinary accepted code -- one of the six fdps_unit_item_select_loop
   discriminates on, though the assertion does not depend on that -- 0x7f is the
   boundary the JLE puts inside the accepted range, and 0 is the other end. */
static void a_code_at_or_below_7f_ends_the_wait(void)
{
    int enter;
    int boundary;
    int zero;

    arm_next_scancode(SCANCODE_ENTER);
    enter = fdps_unit_status_window_wait_input(window, 0, 0);
    arm_next_scancode(SCANCODE_LAST_ACCEPTED);
    boundary = fdps_unit_status_window_wait_input(window, 0, 0);
    arm_next_scancode(0);
    zero = fdps_unit_status_window_wait_input(window, 0, 0);

    CHECK_EQ(enter, SCANCODE_ENTER);
    CHECK_EQ(boundary, SCANCODE_LAST_ACCEPTED);
    CHECK_EQ(zero, 0);
}

/* The poll is tested before the tick, and a pass that ends the wait draws
   nothing at all.  The last-drawn tick is set here to a value the timer
   counter does not hold, which is exactly the condition a drawing pass needs
   -- so if the tick comparison came first, or if the accepted code were
   allowed to fall through it, this call would compose and present a frame and
   leave its own tick behind.  It does neither: the last-drawn tick is
   untouched and the window image still holds the pixel the staging put in the
   unit's cell at row 10, column 161.

   Both are checked because they fail apart.  A frame drawn for the wrong
   reason writes the cell; a tick recorded without a frame writes only the
   tick. */
static void an_accepted_code_draws_no_frame(void)
{
    int premise;
    int code;
    int cell_pixel;
    int tick_after;

    data_fdps_unit_status_window_last_tick = 0x5a5a;
    premise = ((int) data_fdps_timer_tick_counter
               != data_fdps_unit_status_window_last_tick);
    cell_pixel = (int) window[10 * VGA_SCREEN_PITCH + 161];

    arm_next_scancode(SCANCODE_ENTER);
    code = fdps_unit_status_window_wait_input(window, 0, 1);
    tick_after = data_fdps_unit_status_window_last_tick;

    CHECK_EQ(premise, 1);
    CHECK_EQ(code, SCANCODE_ENTER);
    CHECK_EQ(tick_after, 0x5a5a);
    CHECK_EQ((int) window[10 * VGA_SCREEN_PITCH + 161], cell_pixel);
    CHECK_EQ((int) window[10 * VGA_SCREEN_PITCH + 161],
             win_pixel(10, 161));
}

/* allow_idle_animation is read before the loop and it decides whether rand is
   called at all: CMP byte ptr [EBP+0x1c],0x0 / JZ at 000170e9 jumps past the
   CALL at 000170ef.  So the flag is observable even on a pass that draws
   nothing -- it moves the CRT's random state by exactly one draw or by none.

   That is the whole of what a caller can see of the arming from outside: the
   frame counter it sets is a local, and the animation it starts is drawn only
   on a pass that this test cannot reach.  Consuming two draws, or consuming
   one with the flag clear, would show up here and in nothing else. */
static void the_idle_flag_decides_whether_rand_is_drawn(void)
{
    int first;
    int second;
    int after_flag_clear;
    int after_flag_set;

    srand(RAND_SEED);
    first = rand();
    second = rand();

    srand(RAND_SEED);
    arm_next_scancode(SCANCODE_ENTER);
    fdps_unit_status_window_wait_input(window, 0, 0);
    after_flag_clear = rand();

    srand(RAND_SEED);
    arm_next_scancode(SCANCODE_ENTER);
    fdps_unit_status_window_wait_input(window, 0, 1);
    after_flag_set = rand();

    CHECK_EQ(first != second, 1);
    CHECK_EQ(after_flag_clear, first);
    CHECK_EQ(after_flag_set, second);
}

/* A pass that ends the wait takes no heap.  All three of the loop's
   allocations are inside the drawing branch and all three are freed before
   that branch ends, so a wait that returns on its first poll must leave the
   heap exactly as it found it.  The first call is a warm-up, so that anything
   the CRT allocates once is already accounted for. */
static void an_accepted_code_takes_no_heap(void)
{
    int before;
    int after;

    arm_next_scancode(SCANCODE_ENTER);
    fdps_unit_status_window_wait_input(window, 0, 1);

    before = used_heap_blocks();
    arm_next_scancode(SCANCODE_ENTER);
    fdps_unit_status_window_wait_input(window, 0, 1);
    after = used_heap_blocks();

    CHECK_EQ(after, before);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* ------------------------------------------------------------------
 * fdps_draw_unit_status_panel @ 00016300
 * ------------------------------------------------------------------
 *
 * Every offset, digit count, colour row and text-id base below is read off
 * the assembly between 0001630c and 00016826 -- the nineteen ADD EAX,imm that
 * form the destinations, the PUSH 0x4 / 0x3 / 0x2 field widths, the MOV
 * [0x0006000c],0x3 / 0x1 / 0x0 stores, the IMUL by 0x75 with the ADD max /
 * DEC / IDIV that rounds the gauge up, and the ADD EAX,0x1 / 0xa1 / 0x97 in
 * front of the three text calls.  None of it is taken from the emitted C.
 *
 * WHAT THE PANEL IS OBSERVED THROUGH IS THE SURFACE IT DRAWS ON.  This
 * function writes nothing else: no return value, no global but the colour row
 * and the village index.  So the cases stage sheets whose pixels SAY which
 * slot was picked, run the whole path, and read the answer out of the
 * destination.
 *
 *   - The number sheet is the same shape tests/text.c stages: sprite n is a
 *     flat 6x8 fill of colour n + 1, and a sprite index is colour row * 13 +
 *     glyph.  One destination pixel therefore names both the digit drawn and
 *     the colour row it came from, which is what lets one assertion cover a
 *     figure's value, its position and its highlight together.
 *   - The gauge sheet's three graphics are 1, 2 and 3 in their top four rows
 *     and transparent below, so a gauge row reads back as a filled run
 *     followed by track and the fill width is the length of that run.
 *
 * THE GAUGES OVERLAP THE HP AND MP FIGURES AND ARE DRAWN OVER THEM, WHICH IS
 * WHY THE FIXTURE'S LOWER HALF IS TRANSPARENT.  The HP figures are at row 122
 * and the HP gauge covers rows 118..125; the MP pair is the same four rows
 * apart.  Both figures are drawn first and the gauge second, so an opaque bar
 * would bury them.  The shipped Bar.cel does not: its three 117x8 graphics are
 * one tapering wedge, opaque across the full width only in rows 0 and 1 and
 * narrowing to 57 columns by row 7, and the packed sheet main.c builds keeps
 * those skip runs as index 0 for fdps_blit_transparent_rect to skip.  Both
 * figure columns -- 66 and 95 measured from the gauge's own origin -- are
 * outside the wedge from row 2 down.  The fixture models that with a flat cut
 * at row 4, which is above every figure pixel an assertion reads.
 *   - The sprite cache's slot s decodes to a flat 24x24 of 0x40 + s, so the
 *     cell says which slot the village flag chose.
 *   - The font is one byte per glyph and the text block's entry k is the
 *     single glyph k, so the eight pixels a name draws spell out IN BINARY
 *     the text id the function computed.  A wrong id is read back as itself
 *     rather than as an absence.
 *
 * THE PORTRAIT IS THE REAL FACE.CEL AND CANNOT BE STOOD IN FOR.  The record
 * byte that selects it is zero-extended, so it is never the -1 that would
 * make the loader return early: every call reaches fdps_load_and_draw_portrait
 * and that routine names its sheet with a literal and exits the process on a
 * sheet it cannot open (msgwin.h).  FACE.CEL is staged by tests/gamefile.lst
 * and each case skips itself when it is not there.  Its 125x100 blit lands at
 * rows 4..103, columns 19..143, which no assertion below reads.
 *
 * WHAT IS NOT ASSERTED.  Nothing here checks the artwork -- which colour row 3
 * looks like, what portrait 0 contains -- only which slot of it was selected.
 * And the order of the draws is unobservable: all nineteen destination
 * rectangles are disjoint, so no pixel is written twice.
 */

/* The nineteen destinations, as the byte offsets the assembly adds. */
#define PN_PORTRAIT_AT 0xd21
#define PN_HP_CURRENT_AT 0x98d8
#define PN_HP_MAX_AT 0x98f5
#define PN_MP_CURRENT_AT 0xb098
#define PN_MP_MAX_AT 0xb0b5
#define PN_LEVEL_AT 0xc1fe
#define PN_EXP_AT 0xcd38
#define PN_MOVE_AT 0xc236
#define PN_HP_GAUGE_AT 0x9396
#define PN_MP_GAUGE_AT 0xab56
#define PN_HIT_AT 0xe3b8
#define PN_EV_AT 0xd878
#define PN_DX_AT 0xcd70
#define PN_AP_AT 0xd8b0
#define PN_DP_AT 0xe3f0
#define PN_NAME_AT 0x709
#define PN_CLASS_AT 0x1d80
#define PN_RACE_AT 0x1dc7

/* The three text-id bases and the colour the names are drawn in. */
#define PN_NAME_BASE 1
#define PN_RACE_BASE 0x97
#define PN_CLASS_BASE 0xa1
#define PN_TEXT_FG 0xd0

/* The colour rows: 0 plain, 1 buffed, 3 below maximum. */
#define PN_ROW_NORMAL 0
#define PN_ROW_BUFFED 1
#define PN_ROW_REDUCED 3

/* A full gauge is 0x75 columns and the two bars take graphics 1 and 2. */
#define PN_BAR_WIDTH 0x75
#define PN_HP_BAR 1
#define PN_MP_BAR 2

/* The sentinel the destination is filled with.  It is above the 65 sprite
   colours and is none of the sheet values, so an untouched pixel is always
   distinguishable from a drawn one. */
#define PN_FILL 0x5a

/* Number.cel's shape (resource_info/cel.md): the sprite offset table at a
   fixed +0x0f, one dword per sprite, five colour rows of thirteen glyphs. */
#define PN_NUM_TABLE_AT 0x0f
#define PN_NUM_GLYPHS 13
#define PN_NUM_SPRITES 65
#define PN_NUM_STREAM_AT (PN_NUM_TABLE_AT + PN_NUM_SPRITES * 4)
#define PN_NUM_STREAM_BYTES 16
#define PN_NUM_SHEET_BYTES \
    (PN_NUM_STREAM_AT + PN_NUM_SPRITES * PN_NUM_STREAM_BYTES)

/* The gauge sheet: three 0x75 x 8 graphics 0x3a8 bytes apart, opaque down to
   PN_BAR_OPAQUE_ROWS and index 0 below it. */
#define PN_BAR_GRAPHIC_STRIDE 0x3a8
#define PN_BAR_SHEET_BYTES (3 * PN_BAR_GRAPHIC_STRIDE)
#define PN_BAR_OPAQUE_ROWS 4

/* The sprite cache: a table of slots at the base, each twelve dword offsets,
   followed by one 24x24 stream per slot.  Eight slots is more than any case
   asks for and lets a wrong slot land on a different value rather than off
   the block. */
#define PN_CACHE_SLOTS 8
#define PN_CELL_SIZE 0x18
#define PN_CELL_STREAM_BYTES (PN_CELL_SIZE * 2)
#define PN_CACHE_TABLE_BYTES \
    (PN_CACHE_SLOTS * (int) sizeof(struct fdps_cel_cache_slot))
#define PN_CACHE_BYTES \
    (PN_CACHE_TABLE_BYTES + PN_CACHE_SLOTS * PN_CELL_STREAM_BYTES)

/* The text block: 256 two-byte table entries followed by 256 streams of one
   glyph and a terminator, so every id the panel can compute has an entry. */
#define PN_TEXT_ENTRIES 256
#define PN_TEXT_TABLE_BYTES (PN_TEXT_ENTRIES * 2)
#define PN_TEXT_STREAM_BYTES 4
#define PN_TEXT_BYTES \
    (PN_TEXT_TABLE_BYTES + PN_TEXT_ENTRIES * PN_TEXT_STREAM_BYTES)

/* One glyph is eight pixels wide and one row tall, so a name's eight pixels
   are the glyph byte's eight bits, most significant first. */
#define PN_GLYPH_WIDTH 8
#define PN_GLYPH_ROWS 1

#define PN_UNIT_COUNT 8

static unsigned char panel_surface[VGA_SCREEN_BYTES];
static struct fdps_unit_record panel_units[PN_UNIT_COUNT];
static unsigned char panel_number_sheet[PN_NUM_SHEET_BYTES];
static unsigned char panel_bar_sheet[PN_BAR_SHEET_BYTES];
static unsigned char panel_cache[PN_CACHE_BYTES];
static unsigned char panel_text[PN_TEXT_BYTES];
static unsigned char panel_font[PN_TEXT_ENTRIES];

/* Slot s of the sprite cache decodes to a flat cell of this colour. */
static int cell_color(int slot)
{
    return 0x40 + slot;
}

static int face_sheet_present(void)
{
    FILE *probe;

    probe = fopen("FACE.CEL", "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    return 1;
}

/* Builds every sheet the panel draws through and points the globals at them.
   Called once; the per-case staging below only refills the destination and
   the unit records. */
static void panel_stage_sheets(void)
{
    int index;
    int sprite;
    int row;
    int stream_at;
    int slot;

    for (index = 0; index < PN_NUM_SHEET_BYTES; index++) {
        panel_number_sheet[index] = 0;
    }
    for (sprite = 0; sprite < PN_NUM_SPRITES; sprite++) {
        stream_at = PN_NUM_STREAM_AT + sprite * PN_NUM_STREAM_BYTES;
        *(int *) (panel_number_sheet + PN_NUM_TABLE_AT + sprite * 4) =
            stream_at;
        /* Eight rows of one fill op: command 0x05 is op 00 with a run of
           (5 & 0x3f) + 1 = 6, which is exactly the cell width. */
        for (row = 0; row < 8; row++) {
            panel_number_sheet[stream_at + row * 2] = 0x05;
            panel_number_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (sprite + 1);
        }
    }

    for (index = 0; index < PN_BAR_SHEET_BYTES; index++) {
        row = index % PN_BAR_GRAPHIC_STRIDE / PN_BAR_WIDTH;
        if (row < PN_BAR_OPAQUE_ROWS) {
            panel_bar_sheet[index] =
                (unsigned char) (index / PN_BAR_GRAPHIC_STRIDE + 1);
        } else {
            panel_bar_sheet[index] = (unsigned char) 0;
        }
    }

    for (index = 0; index < PN_CACHE_BYTES; index++) {
        panel_cache[index] = 0;
    }
    for (slot = 0; slot < PN_CACHE_SLOTS; slot++) {
        stream_at = PN_CACHE_TABLE_BYTES + slot * PN_CELL_STREAM_BYTES;
        ((struct fdps_cel_cache_slot *) panel_cache)[slot].sprite_offset[0] =
            stream_at;
        /* 24 rows of one fill op: command 0x17 is a run of 24, the cell
           width, so every row closes on its own column count. */
        for (row = 0; row < PN_CELL_SIZE; row++) {
            panel_cache[stream_at + row * 2] = 0x17;
            panel_cache[stream_at + row * 2 + 1] =
                (unsigned char) cell_color(slot);
        }
    }

    for (index = 0; index < PN_TEXT_ENTRIES; index++) {
        panel_font[index] = (unsigned char) index;
        stream_at = PN_TEXT_TABLE_BYTES + index * PN_TEXT_STREAM_BYTES;
        *(short *) (panel_text + index * 2) = (short) stream_at;
        *(short *) (panel_text + stream_at) = (short) index;
        *(short *) (panel_text + stream_at + 2) = (short) -1;
    }

    data_fdps_number_glyph_sheet_ptr = panel_number_sheet;
    data_fdps_status_gauge_bar_sheet_ptr = panel_bar_sheet;
    data_fdps_cel_sprite_cache_ptr = panel_cache;
    data_fdps_all_game_text_ptr = panel_text;
    data_fdps_font_sheet_ptr = panel_font;
    data_fdps_font_glyph_width = (unsigned char) PN_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) PN_GLYPH_ROWS;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = PN_GLYPH_WIDTH;
    data_fdps_font_line_height = PN_GLYPH_ROWS;
    data_fdps_map_unit_array_ptr = (unsigned char *) panel_units;
}

/* Clears the records and the destination and puts the two globals the panel
   reads into their battle-phase state. */
static struct fdps_unit_record *panel_reset(void)
{
    int index;

    for (index = 0; index < VGA_SCREEN_BYTES; index++) {
        panel_surface[index] = PN_FILL;
    }
    memset(panel_units, 0, sizeof panel_units);
    data_fdps_village_mode_flag = (unsigned char) 0;
    data_fdps_number_glyph_color_row = 0;
    return &panel_units[0];
}

/* Gives the portrait buffer back between cases, the way tests/msgwin.c does:
   the loader leaves it allocated on purpose. */
static void panel_release_portrait(void)
{
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
}

/* The top-left pixel of a figure's cell number `cell`. */
static int figure_pixel(int at, int cell)
{
    return (int) panel_surface[at + cell * 6];
}

/* Which sprite of the staged sheet stands in that cell, or -1 for a cell
   nothing drew in. */
static int figure_sprite(int at, int cell)
{
    int sprite;

    sprite = figure_pixel(at, cell) - 1;
    if (sprite < 0 || sprite >= PN_NUM_SPRITES) {
        return -1;
    }
    return sprite;
}

/* The glyph the cell holds -- 0..9 for a digit, 12 for '?' -- or -1. */
static int figure_glyph(int at, int cell)
{
    int sprite;

    sprite = figure_sprite(at, cell);
    if (sprite < 0) {
        return -1;
    }
    return sprite % PN_NUM_GLYPHS;
}

/* The colour row the cell's sprite came out of, or -1. */
static int figure_row(int at, int cell)
{
    int sprite;

    sprite = figure_sprite(at, cell);
    if (sprite < 0) {
        return -1;
    }
    return sprite / PN_NUM_GLYPHS;
}

/* How many of the bar's columns came out of the filled graphic. */
static int gauge_fill(int at, int bar_index)
{
    int column;

    column = 0;
    while (column < PN_BAR_WIDTH
           && panel_surface[at + column] == (unsigned char) (bar_index + 1)) {
        column++;
    }
    return column;
}

/* The text id a name drew, read back out of its eight pixels. */
static int name_text_id(int at)
{
    int column;
    int value;

    value = 0;
    for (column = 0; column < PN_GLYPH_WIDTH; column++) {
        if (panel_surface[at + column] == (unsigned char) PN_TEXT_FG) {
            value |= 1 << (PN_GLYPH_WIDTH - 1 - column);
        }
    }
    return value;
}

/* Nine figures, nine values, nine destinations.  Each value is chosen so its
   digits identify it, so a figure that landed at another figure's offset --
   or that was drawn from another record field -- reads back as the wrong
   digits rather than as the right ones by luck.  The field widths are pinned
   at the same time: the two-digit fields must not touch a third cell and the
   four-digit ones must pad, so 56 in a four-digit field is 0056. */
static void every_figure_lands_at_its_own_offset(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->hp_current = 1234;
    unit->hp_max = 1234;
    unit->mp_current = 56;
    unit->mp_max = 56;
    unit->level = 12;
    unit->move = 34;
    unit->exp_carry = 56;
    unit->ap = 123;
    unit->dp = 456;
    unit->hit = 789;
    unit->ev = 246;
    unit->dx_base = 135;

    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();

    CHECK_EQ(figure_glyph(PN_HP_CURRENT_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_HP_CURRENT_AT, 3), 4);
    CHECK_EQ(figure_glyph(PN_HP_MAX_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_HP_MAX_AT, 3), 4);
    CHECK_EQ(figure_glyph(PN_MP_CURRENT_AT, 0), 0);
    CHECK_EQ(figure_glyph(PN_MP_CURRENT_AT, 2), 5);
    CHECK_EQ(figure_glyph(PN_MP_CURRENT_AT, 3), 6);
    CHECK_EQ(figure_glyph(PN_MP_MAX_AT, 2), 5);
    CHECK_EQ(figure_glyph(PN_LEVEL_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_LEVEL_AT, 1), 2);
    CHECK_EQ(figure_pixel(PN_LEVEL_AT, 2), PN_FILL);
    CHECK_EQ(figure_glyph(PN_MOVE_AT, 0), 3);
    CHECK_EQ(figure_glyph(PN_MOVE_AT, 1), 4);
    CHECK_EQ(figure_pixel(PN_MOVE_AT, 2), PN_FILL);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 0), 0);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 1), 5);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 2), 6);
    CHECK_EQ(figure_glyph(PN_AP_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_AP_AT, 2), 3);
    CHECK_EQ(figure_glyph(PN_DP_AT, 0), 4);
    CHECK_EQ(figure_glyph(PN_DP_AT, 2), 6);
    CHECK_EQ(figure_glyph(PN_HIT_AT, 0), 7);
    CHECK_EQ(figure_glyph(PN_HIT_AT, 2), 9);
    CHECK_EQ(figure_glyph(PN_EV_AT, 0), 2);
    CHECK_EQ(figure_glyph(PN_EV_AT, 2), 6);
    CHECK_EQ(figure_glyph(PN_DX_AT, 0), 1);
    CHECK_EQ(figure_glyph(PN_DX_AT, 2), 5);
}

/* CMP EAX,dword ptr [EBP+-0x3c] / JZ at 00016450 and its MP twin at 000164ad
   set row 3 only when the pair differs, and the MOV [0x0006000c],0x0 that
   follows each figure is what puts it back -- so the maximum is always drawn
   plain, and the global is 0 at the RET. */
static void a_current_below_its_maximum_draws_in_the_reduced_row(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->hp_current = 1;
    unit->hp_max = 2;
    unit->mp_current = 3;
    unit->mp_max = 4;

    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();

    CHECK_EQ(figure_row(PN_HP_CURRENT_AT, 3), PN_ROW_REDUCED);
    CHECK_EQ(figure_row(PN_HP_MAX_AT, 3), PN_ROW_NORMAL);
    CHECK_EQ(figure_row(PN_MP_CURRENT_AT, 3), PN_ROW_REDUCED);
    CHECK_EQ(figure_row(PN_MP_MAX_AT, 3), PN_ROW_NORMAL);
    CHECK_EQ(data_fdps_number_glyph_color_row, PN_ROW_NORMAL);
}

/* The HP compare has no else arm: JZ at 00016453 jumps past the store rather
   than to one that clears it, so a full unit inherits whatever colour row the
   caller left behind and the first figure is drawn in it.  Row 2 is a row no
   branch in this function can select, so seeing it proves the inheritance
   rather than a missed store.  MP is drawn after the first put-back and so
   starts from 0 whatever the caller did. */
static void a_full_unit_inherits_the_callers_colour_row(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->hp_current = 9;
    unit->hp_max = 9;
    unit->mp_current = 9;
    unit->mp_max = 9;
    data_fdps_number_glyph_color_row = 2;

    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();

    CHECK_EQ(figure_row(PN_HP_CURRENT_AT, 3), 2);
    CHECK_EQ(figure_row(PN_HP_MAX_AT, 3), PN_ROW_NORMAL);
    CHECK_EQ(figure_row(PN_MP_CURRENT_AT, 3), PN_ROW_NORMAL);
    CHECK_EQ(data_fdps_number_glyph_color_row, PN_ROW_NORMAL);
}

/* Each buff timer lights exactly the figures its buff moves, which is the
   grouping fdps_unit_recompute_combat_stats applies: timer 2 raises dx and so
   both hit and ev, timer 0 multiplies ap and timer 1 multiplies dp.  The
   three tests are separate CMP byte ptr [EAX+0x2x],0x0 with their own
   else arms, so a timer that is clear drives its figures back to row 0
   instead of leaving the previous group's row standing. */
static void each_buff_timer_lights_only_its_own_figures(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->status_timers[2] = 1;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_row(PN_HIT_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_EV_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_DX_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_AP_AT, 2), PN_ROW_NORMAL);
    CHECK_EQ(figure_row(PN_DP_AT, 2), PN_ROW_NORMAL);

    unit = panel_reset();
    unit->status_timers[0] = 1;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_row(PN_AP_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_DP_AT, 2), PN_ROW_NORMAL);
    CHECK_EQ(figure_row(PN_HIT_AT, 2), PN_ROW_NORMAL);

    unit = panel_reset();
    unit->status_timers[1] = 1;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_row(PN_DP_AT, 2), PN_ROW_BUFFED);
    CHECK_EQ(figure_row(PN_AP_AT, 2), PN_ROW_NORMAL);
    CHECK_EQ(data_fdps_number_glyph_color_row, PN_ROW_NORMAL);
}

/* CMP dword ptr [EBP+-0x2c],0xff / MOV ...,0x3e8 at 000163de replaces the
   sentinel with a thousand, and the field it is drawn in is three digits --
   PUSH 0x3 at 00016523 -- so fdps_draw_number's overflow guard turns it into
   three '?' glyphs, sprite 12 of the row.  A player's own unit carries 0..99
   and is drawn as a figure. */
static void the_not_player_experience_sentinel_fills_the_field(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->exp_carry = (unsigned char) 0xff;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_glyph(PN_EXP_AT, 0), 12);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 1), 12);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 2), 12);

    unit = panel_reset();
    unit->exp_carry = (unsigned char) 99;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(figure_glyph(PN_EXP_AT, 0), 0);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 1), 9);
    CHECK_EQ(figure_glyph(PN_EXP_AT, 2), 9);
}

/* The fill is (current * 0x75 + max - 1) / max and it rounds UP, so one hit
   point out of a thousand still shows a column; a full pair fills all 0x75;
   and a maximum of zero or less never reaches the divide at all (CMP ...,0x0
   / JG at 000165a5).  The HP bar comes out of graphic 1 and the MP bar out of
   graphic 2, which is what separates the two rows. */
static void the_gauges_round_up_and_take_a_graphic_each(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->hp_current = 1;
    unit->hp_max = 1000;
    unit->mp_current = 40;
    unit->mp_max = 40;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(gauge_fill(PN_HP_GAUGE_AT, PN_HP_BAR), 1);
    CHECK_EQ(gauge_fill(PN_MP_GAUGE_AT, PN_MP_BAR), PN_BAR_WIDTH);
    /* The column past the HP fill is the track, graphic 0, and the fill is
       the same width three rows down -- the two blits are rectangles at the
       destination's own pitch. */
    CHECK_EQ(panel_surface[PN_HP_GAUGE_AT + 1], 1);
    CHECK_EQ(panel_surface[PN_HP_GAUGE_AT + 3 * VGA_SCREEN_PITCH], 2);

    unit = panel_reset();
    unit->hp_current = 5;
    unit->hp_max = 0;
    unit->mp_current = 5;
    unit->mp_max = -1;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(gauge_fill(PN_HP_GAUGE_AT, PN_HP_BAR), 0);
    CHECK_EQ(gauge_fill(PN_MP_GAUGE_AT, PN_MP_BAR), 0);
    CHECK_EQ(panel_surface[PN_HP_GAUGE_AT], 1);

    /* Half of an odd maximum rounds up rather than down: 3 of 5 is
       (3 * 117 + 4) / 5 = 71, where a truncating divide would give 70. */
    unit = panel_reset();
    unit->hp_current = 3;
    unit->hp_max = 5;
    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();
    CHECK_EQ(gauge_fill(PN_HP_GAUGE_AT, PN_HP_BAR), 71);
}

/* CMP byte ptr [0x00060070],0x0 / JNZ at 000163f5 picks which index reaches
   the slot table: the record's own sprite_cache_slot on the battle map, the
   caller's unit_index in a village phase.  The store at 00016318 is the only
   thing in the image that writes data_fdps_village_status_window_unit_idx, and
   it happens only on the village arm. */
static void the_village_flag_swaps_the_cache_slot_and_latches_the_index(void)
{
    if (!face_sheet_present()) {
        return;
    }
    panel_reset();
    panel_units[2].sprite_cache_slot = (unsigned char) 5;
    data_fdps_village_status_window_unit_idx = -1;
    fdps_draw_unit_status_panel(2, panel_surface);
    panel_release_portrait();
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT], cell_color(5));
    CHECK_EQ(data_fdps_village_status_window_unit_idx, -1);

    panel_reset();
    panel_units[2].sprite_cache_slot = (unsigned char) 5;
    data_fdps_village_mode_flag = (unsigned char) 1;
    fdps_draw_unit_status_panel(2, panel_surface);
    panel_release_portrait();
    data_fdps_village_mode_flag = (unsigned char) 0;
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT], cell_color(2));
    CHECK_EQ(data_fdps_village_status_window_unit_idx, 2);
    /* And the cell is 24 x 24 at that offset and no larger. */
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT + 23 * VGA_SCREEN_PITCH + 23],
             cell_color(2));
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT + 24 * VGA_SCREEN_PITCH], PN_FILL);
    CHECK_EQ(panel_surface[PN_PORTRAIT_AT + 24], PN_FILL);
}

/* ADD EAX,0x1 at 000167c2, ADD EAX,0xa1 at 000167ec and ADD EAX,0x97 at
   0001681a: the name comes from char_id, the class name from clazz and the
   race name from race, each offset by its block's first entry.  The three
   destinations are separate, so a pair swapped between two of them is caught
   as well as a wrong base. */
static void the_three_names_come_from_their_own_blocks(void)
{
    struct fdps_unit_record *unit;

    if (!face_sheet_present()) {
        return;
    }
    unit = panel_reset();
    unit->char_id = (unsigned char) 20;
    unit->clazz = (unsigned char) 3;
    unit->race = (unsigned char) 4;

    fdps_draw_unit_status_panel(0, panel_surface);
    panel_release_portrait();

    CHECK_EQ(name_text_id(PN_NAME_AT), PN_NAME_BASE + 20);
    CHECK_EQ(name_text_id(PN_CLASS_AT), PN_CLASS_BASE + 3);
    CHECK_EQ(name_text_id(PN_RACE_AT), PN_RACE_BASE + 4);
}

/* ------------------------------------------------------------------ *
 * fdps_draw_unit_inventory @ 00024ea0
 *
 * The list is drawn entirely through four callees, so what this function
 * contributes is which sprite, which text entry, which record field and which
 * pixel -- and all four are readable off a surface once the sheets are
 * fabricated so that a drawn pixel names what drew it.  Every expected value
 * below is the literal the assembly pushes: 0x11 between rows, 0x1d/0x1e/0x1f
 * for the three icons and +4 when equipped, 0xc9 added to the item id for the
 * name, 0x40/0x41/0x3e/0x3f/0x43 for the five captions, the bounds 0, 0x15,
 * 0x27, 0x0b and 0x0c the two classifications test, and the offsets 3/9,
 * 0x13/8, 0x66/0xd and the further 0x16 for the figure.
 *
 * THE FIXTURE IS BUILT SO NOTHING COLLIDES.  A command sprite paints 0x80 plus
 * its index, a number glyph paints its digit plus one, the selection bar
 * paints 0x70, a text glyph paints 0xd0, and the surface starts at 0x5a -- so
 * one byte says both what drew it and which sprite it was.  The bar is made
 * three pixels wide and one row tall so it lives in columns 0..2, which
 * nothing else in the list ever reaches; that is what lets a case assert the
 * bar's row is the ONLY row it touched rather than merely one of them.
 *
 * The command sprites are 25 by 22 and the rows are 17 apart, so consecutive
 * rows overlap.  Every position read back below is the top-left pixel of the
 * thing that owns it, and the drawing order -- icon, name, caption, figure,
 * slot by slot -- puts the later writer on top at each of them, so no
 * assertion is reading a neighbour's overspill.
 */

#define INV_PITCH 0x140
/* A pitch that is not the 0x140 every call site passes.  Each of the three
   destinations is row * pitch + column, so at another pitch a coordinate pair
   that was folded into a fixed byte offset would land somewhere else. */
#define INV_ODD_PITCH 0x100
#define INV_ROWS 200
#define INV_SURFACE_BYTES (INV_PITCH * INV_ROWS)
#define INV_FILL 0x5a

#define INV_ROW_H 0x11
#define INV_SLOTS 8
#define INV_EMPTY 0x80
#define INV_EQUIPPED 0x40
#define INV_ICON_COL 3
#define INV_ICON_ROW_BIAS 9
#define INV_NAME_COL 0x13
#define INV_NAME_ROW_BIAS 8
#define INV_CAP_COL 0x66
#define INV_CAP_ROW_BIAS 0x0d
#define INV_FIG_COL (INV_CAP_COL + 0x16)
#define INV_BAR_ROW_BIAS 8

/* A .CEL's sprite offset table starts at +0x0f (resource_info/cel.md) and
   fdps_blit_command_sprite draws 25 by 22 of whatever the entry points at
   (sprite.h), so each stream is 22 rows of one fill op with a run of 25 --
   command byte 0x18 is op 00 with a run of (0x18 & 0x3f) + 1.  0x44 sprites is
   one more than 0x43, the highest caption the list asks for. */
#define INV_CEL_TABLE_AT 0x0f
#define INV_CMD_SPRITES 0x44
#define INV_CMD_RUN 0x18
#define INV_CMD_ROWS 22
#define INV_CMD_STREAM_BYTES (INV_CMD_ROWS * 2)
#define INV_CMD_STREAMS_AT (INV_CEL_TABLE_AT + INV_CMD_SPRITES * 4)
#define INV_CMD_SHEET_BYTES \
    (INV_CMD_STREAMS_AT + INV_CMD_SPRITES * INV_CMD_STREAM_BYTES)
#define INV_CMD_COLOR_BASE 0x80

/* Number.cel: fdps_draw_number picks colour_row * 13 + glyph off the same
   +0x0f table and draws 6 by 8, so command byte 0x05 is a run of 6.  A
   sprite's colour is its index plus one, and every case leaves
   data_fdps_number_glyph_color_row at 0, so a digit paints digit + 1. */
#define INV_NUM_GLYPHS 13
#define INV_NUM_COLOR_ROWS 5
#define INV_NUM_SPRITES (INV_NUM_GLYPHS * INV_NUM_COLOR_ROWS)
#define INV_NUM_RUN 0x05
#define INV_NUM_CELL_W 6
#define INV_NUM_CELL_H 8
#define INV_NUM_STREAM_BYTES (INV_NUM_CELL_H * 2)
#define INV_NUM_STREAMS_AT (INV_CEL_TABLE_AT + INV_NUM_SPRITES * 4)
#define INV_NUM_SHEET_BYTES \
    (INV_NUM_STREAMS_AT + INV_NUM_SPRITES * INV_NUM_STREAM_BYTES)

/* SelBar.cel.  fdps_cel_blit_sprite takes the sprite size out of the header
   rather than from its caller, so the fixture's header is what decides it:
   three columns and one row, which keeps the bar inside columns 0..2. */
#define INV_BAR_W 3
#define INV_BAR_H 1
#define INV_BAR_RUN 0x02
#define INV_BAR_SPRITES 1
#define INV_BAR_STREAMS_AT (INV_CEL_TABLE_AT + INV_BAR_SPRITES * 4)
#define INV_BAR_SHEET_BYTES (INV_BAR_STREAMS_AT + INV_BAR_H * 2)
#define INV_BAR_COLOR 0x70

/* The text block, laid out the way text.h describes: signed 16-bit offsets
   from the block's own base, then the token streams.  Entry 0xc9 + id holds
   one glyph whose index IS the id, so the eight pixels a name paints read back
   as the item id the name was fetched for. */
#define INV_NAME_TEXT_BASE 0xc9
#define INV_ITEM_IDS 256
#define INV_TEXT_ENTRIES (INV_NAME_TEXT_BASE + INV_ITEM_IDS)
#define INV_TEXT_TABLE_BYTES (INV_TEXT_ENTRIES * 2)
#define INV_TEXT_STREAM_BYTES 4
#define INV_TEXT_BYTES \
    (INV_TEXT_TABLE_BYTES + INV_TEXT_ENTRIES * INV_TEXT_STREAM_BYTES)
#define INV_TEXT_END (-1)
#define INV_TEXT_FG 0xd0

/* One glyph is eight pixels wide and one row tall, so a name's eight pixels
   are the glyph byte's eight bits, most significant first. */
#define INV_GLYPH_W 8
#define INV_GLYPH_ROWS 1

/* ITEM.DAT's stride is 0x17 and not sizeof (src/table.c). */
#define INV_ITEM_STRIDE 0x17
#define INV_ITEM_TABLE_BYTES (INV_ITEM_IDS * INV_ITEM_STRIDE)

#define INV_UNIT_COUNT 2

static unsigned char inv_surface[INV_SURFACE_BYTES];
static unsigned char inv_command_sheet[INV_CMD_SHEET_BYTES];
static unsigned char inv_number_sheet[INV_NUM_SHEET_BYTES];
static unsigned char inv_bar_sheet[INV_BAR_SHEET_BYTES];
static unsigned char inv_text[INV_TEXT_BYTES];
static unsigned char inv_font[INV_ITEM_IDS];
static unsigned char inv_item_table[INV_ITEM_TABLE_BYTES];
static struct fdps_unit_record inv_units[INV_UNIT_COUNT];
static int inv_pitch;

static void inv_stage_sheets(void)
{
    int sprite;
    int row;
    int stream_at;
    int index;
    struct fdps_cel_header *bar_header;

    memset(inv_command_sheet, 0, sizeof inv_command_sheet);
    for (sprite = 0; sprite < INV_CMD_SPRITES; sprite++) {
        stream_at = INV_CMD_STREAMS_AT + sprite * INV_CMD_STREAM_BYTES;
        *(int *) (inv_command_sheet + INV_CEL_TABLE_AT + sprite * 4) =
            stream_at;
        for (row = 0; row < INV_CMD_ROWS; row++) {
            inv_command_sheet[stream_at + row * 2] =
                (unsigned char) INV_CMD_RUN;
            inv_command_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (INV_CMD_COLOR_BASE + sprite);
        }
    }

    memset(inv_number_sheet, 0, sizeof inv_number_sheet);
    for (sprite = 0; sprite < INV_NUM_SPRITES; sprite++) {
        stream_at = INV_NUM_STREAMS_AT + sprite * INV_NUM_STREAM_BYTES;
        *(int *) (inv_number_sheet + INV_CEL_TABLE_AT + sprite * 4) = stream_at;
        for (row = 0; row < INV_NUM_CELL_H; row++) {
            inv_number_sheet[stream_at + row * 2] =
                (unsigned char) INV_NUM_RUN;
            inv_number_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (sprite + 1);
        }
    }

    memset(inv_bar_sheet, 0, sizeof inv_bar_sheet);
    bar_header = (struct fdps_cel_header *) inv_bar_sheet;
    bar_header->sprite_width = (short) INV_BAR_W;
    bar_header->sprite_height = (short) INV_BAR_H;
    bar_header->sprite_count = (short) INV_BAR_SPRITES;
    *(int *) (inv_bar_sheet + INV_CEL_TABLE_AT) = INV_BAR_STREAMS_AT;
    for (row = 0; row < INV_BAR_H; row++) {
        inv_bar_sheet[INV_BAR_STREAMS_AT + row * 2] =
            (unsigned char) INV_BAR_RUN;
        inv_bar_sheet[INV_BAR_STREAMS_AT + row * 2 + 1] =
            (unsigned char) INV_BAR_COLOR;
    }

    for (index = 0; index < INV_ITEM_IDS; index++) {
        inv_font[index] = (unsigned char) index;
    }
    memset(inv_text, 0, sizeof inv_text);
    for (index = 0; index < INV_TEXT_ENTRIES; index++) {
        stream_at = INV_TEXT_TABLE_BYTES + index * INV_TEXT_STREAM_BYTES;
        *(short *) (inv_text + index * 2) = (short) stream_at;
        if (index >= INV_NAME_TEXT_BASE) {
            *(short *) (inv_text + stream_at) =
                (short) (index - INV_NAME_TEXT_BASE);
        } else {
            *(short *) (inv_text + stream_at) = (short) 0;
        }
        *(short *) (inv_text + stream_at + 2) = (short) INV_TEXT_END;
    }
}

/* Publishes the fixture, clears the surface, the item table and the records,
   and fixes the pitch the case draws at. */
static void inv_reset(int pitch)
{
    inv_pitch = pitch;
    memset(inv_surface, INV_FILL, (size_t) INV_SURFACE_BYTES);
    memset(inv_item_table, 0, sizeof inv_item_table);
    memset(inv_units, 0, sizeof inv_units);

    data_fdps_command_sprite_sheet_ptr = inv_command_sheet;
    data_fdps_number_glyph_sheet_ptr = inv_number_sheet;
    data_fdps_selection_bar_sheet_ptr = inv_bar_sheet;
    data_fdps_all_game_text_ptr = inv_text;
    data_fdps_font_sheet_ptr = inv_font;
    data_fdps_item_effect_table_ptr = inv_item_table;
    data_fdps_map_unit_array_ptr = (unsigned char *) inv_units;
    data_fdps_font_glyph_width = (unsigned char) INV_GLYPH_W;
    data_fdps_glyph_cell_height = (unsigned char) INV_GLYPH_ROWS;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = INV_GLYPH_W;
    data_fdps_font_line_height = INV_GLYPH_ROWS;
    data_fdps_number_glyph_color_row = 0;
}

static void inv_set_slot(int slot, int flags, int item_id)
{
    inv_units[0].inventory_slots[slot * 2] = (unsigned char) flags;
    inv_units[0].inventory_slots[slot * 2 + 1] = (unsigned char) item_id;
}

static void inv_empty_all_slots(void)
{
    int slot;

    for (slot = 0; slot < INV_SLOTS; slot++) {
        inv_set_slot(slot, INV_EMPTY, 0);
    }
}

static struct fdps_item_effect *inv_item(int item_id)
{
    return (struct fdps_item_effect *)
        (inv_item_table + item_id * INV_ITEM_STRIDE);
}

static int inv_pixel(int row, int column)
{
    return (int) inv_surface[row * inv_pitch + column];
}

/* Which command sprite painted a position, or -1 for a position no command
   sprite reached. */
static int inv_command_at(int row, int column)
{
    int value;

    value = inv_pixel(row, column);
    if (value < INV_CMD_COLOR_BASE
        || value >= INV_CMD_COLOR_BASE + INV_CMD_SPRITES) {
        return -1;
    }
    return value - INV_CMD_COLOR_BASE;
}

static int inv_icon_sprite(int slot)
{
    return inv_command_at(slot * INV_ROW_H + INV_ICON_ROW_BIAS, INV_ICON_COL);
}

static int inv_caption_sprite(int slot)
{
    return inv_command_at(slot * INV_ROW_H + INV_CAP_ROW_BIAS, INV_CAP_COL);
}

/* The digit standing in cell `cell` of a row's figure, or -1 when no number
   glyph reached it. */
static int inv_figure_digit(int slot, int cell)
{
    int value;

    value = inv_pixel(slot * INV_ROW_H + INV_CAP_ROW_BIAS,
                      INV_FIG_COL + cell * INV_NUM_CELL_W);
    if (value < 1 || value > INV_NUM_GLYPHS) {
        return -1;
    }
    return value - 1;
}

/* The item id a row's name was fetched for, read back out of the glyph's
   eight pixels. */
static int inv_name_id(int slot)
{
    int column;
    int value;
    int row;

    row = slot * INV_ROW_H + INV_NAME_ROW_BIAS;
    value = 0;
    for (column = 0; column < INV_GLYPH_W; column++) {
        if (inv_pixel(row, INV_NAME_COL + column) == INV_TEXT_FG) {
            value |= 1 << (INV_GLYPH_W - 1 - column);
        }
    }
    return value;
}

/* The one row the selection bar touched, -1 when it touched none and -2 when
   it touched more than one. */
static int inv_bar_row(void)
{
    int row;
    int found;

    found = -1;
    for (row = 0; row < INV_ROWS; row++) {
        if (inv_pixel(row, 0) != INV_FILL) {
            if (found >= 0) {
                return -2;
            }
            found = row;
        }
    }
    return found;
}

static int inv_touched_bytes(void)
{
    int index;
    int count;

    count = 0;
    for (index = 0; index < INV_SURFACE_BYTES; index++) {
        if (inv_surface[index] != INV_FILL) {
            count++;
        }
    }
    return count;
}

/* IMUL EAX,dword ptr [EBP+0x18],0x11 / ADD EAX,0x8 with x = 0 and sprite 0:
   the bar sits on the highlighted row's name baseline, and every row above and
   below it is left alone. */
static void the_bar_sits_on_the_selected_rows_baseline(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, 3, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), 3 * INV_ROW_H + INV_BAR_ROW_BIAS);
    CHECK_EQ(inv_pixel(3 * INV_ROW_H + INV_BAR_ROW_BIAS, 0), INV_BAR_COLOR);
    CHECK_EQ(inv_pixel(3 * INV_ROW_H + INV_BAR_ROW_BIAS, INV_BAR_W - 1),
             INV_BAR_COLOR);
    CHECK_EQ(inv_pixel(3 * INV_ROW_H + INV_BAR_ROW_BIAS, INV_BAR_W), INV_FILL);

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, 0, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), INV_BAR_ROW_BIAS);

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, INV_SLOTS - 1, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), (INV_SLOTS - 1) * INV_ROW_H + INV_BAR_ROW_BIAS);
}

/* CMP dword ptr [EBP+0x18],0x0 / JL and CMP ...,0x8 / JL: the bar is drawn
   only for 0..7, and -1 is what fdps_battle_show_unit_status_window passes. */
static void a_selection_outside_the_eight_rows_draws_no_bar(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), -1);

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, INV_SLOTS, inv_surface, INV_PITCH);
    CHECK_EQ(inv_bar_row(), -1);
}

/* TEST dword ptr [EBP+-0xc],0x80 / JNZ: bit 7 means the slot is empty and the
   row is skipped whole -- no icon, no name, no caption -- and the equipped bit
   alongside it does not change that. */
static void the_empty_bit_skips_the_whole_row(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);
    CHECK_EQ(inv_touched_bytes(), 0);

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(2, INV_EMPTY | INV_EQUIPPED, 7);
    inv_item(7)->type = (unsigned char) 5;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);
    CHECK_EQ(inv_touched_bytes(), 0);
}

/* The three IMUL ...,0x11 and the biases 9, 8 and 0xd beside them: eight rows
   seventeen pixels apart, each with its icon at column 3, its name at column
   0x13 and its caption at column 0x66. */
static void every_row_sits_seventeen_pixels_below_the_last(void)
{
    int slot;

    inv_reset(INV_PITCH);
    for (slot = 0; slot < INV_SLOTS; slot++) {
        inv_set_slot(slot, 0, slot + 1);
        inv_item(slot + 1)->type = (unsigned char) 1;
    }
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    for (slot = 0; slot < INV_SLOTS; slot++) {
        CHECK_EQ(inv_icon_sprite(slot), 0x1d);
        CHECK_EQ(inv_caption_sprite(slot), 0x40);
        CHECK_EQ(inv_name_id(slot), slot + 1);
    }
    /* One row above the first icon and above the first name is outside the
       list: nothing widened it upwards. */
    CHECK_EQ(inv_pixel(INV_ICON_ROW_BIAS - 1, INV_ICON_COL), INV_FILL);
    CHECK_EQ(inv_pixel(INV_NAME_ROW_BIAS - 1, INV_NAME_COL), INV_FILL);
}

/* CMP ...,0x0 / JLE and CMP ...,0x15 / JLE, then CMP ...,0x27 / JLE: the icon
   is 0x1d for 1..0x15, 0x1e for 0x16..0x27 and 0x1f for everything else,
   TYPE 0 INCLUDED. */
static void the_icon_says_which_category_the_type_falls_in(void)
{
    inv_reset(INV_PITCH);
    inv_set_slot(0, 0, 1);
    inv_item(1)->type = (unsigned char) 1;
    inv_set_slot(1, 0, 2);
    inv_item(2)->type = (unsigned char) 0x15;
    inv_set_slot(2, 0, 3);
    inv_item(3)->type = (unsigned char) 0x16;
    inv_set_slot(3, 0, 4);
    inv_item(4)->type = (unsigned char) 0x27;
    inv_set_slot(4, 0, 5);
    inv_item(5)->type = (unsigned char) 0x28;
    inv_set_slot(5, 0, 6);
    inv_item(6)->type = (unsigned char) 0;
    inv_set_slot(6, 0, 7);
    inv_item(7)->type = (unsigned char) 0xff;
    inv_set_slot(7, INV_EMPTY, 0);
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_icon_sprite(0), 0x1d);
    CHECK_EQ(inv_icon_sprite(1), 0x1d);
    CHECK_EQ(inv_icon_sprite(2), 0x1e);
    CHECK_EQ(inv_icon_sprite(3), 0x1e);
    CHECK_EQ(inv_icon_sprite(4), 0x1f);
    CHECK_EQ(inv_icon_sprite(5), 0x1f);
    CHECK_EQ(inv_icon_sprite(6), 0x1f);
}

/* TEST dword ptr [EBP+-0xc],0x40 / ADD dword ptr [EBP+-0x8],0x4: the equipped
   bit moves the icon four sprites along, whichever of the three it was. */
static void the_equipped_bit_moves_the_icon_four_sprites_along(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, INV_EQUIPPED, 1);
    inv_item(1)->type = (unsigned char) 1;
    inv_set_slot(1, INV_EQUIPPED, 2);
    inv_item(2)->type = (unsigned char) 0x20;
    inv_set_slot(2, INV_EQUIPPED, 3);
    inv_item(3)->type = (unsigned char) 0x30;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_icon_sprite(0), 0x1d + 4);
    CHECK_EQ(inv_icon_sprite(1), 0x1e + 4);
    CHECK_EQ(inv_icon_sprite(2), 0x1f + 4);
}

/* ADD EAX,0xc9 on the slot's id byte: the name is message 0xc9 + item id, and
   the id read is the SECOND byte of the slot pair, not the flag byte. */
static void the_name_is_message_c9_plus_the_item_id(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, 0, 0x5a);
    inv_item(0x5a)->type = (unsigned char) 1;
    inv_set_slot(1, INV_EQUIPPED, 0xa5);
    inv_item(0xa5)->type = (unsigned char) 1;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_name_id(0), 0x5a);
    CHECK_EQ(inv_name_id(1), 0xa5);
}

/* The weapon caption: CMP EAX,0x15 / JLE then PUSH 0x40 and MOVSX EAX,word ptr
   [EAX + 0x1] -- caption 0x40 with the record's ap, in a four-digit field that
   pads. */
static void a_weapon_prints_caption_40_and_its_ap(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, 0, 1);
    inv_item(1)->type = (unsigned char) 0x15;
    inv_item(1)->ap = (short) 1234;
    inv_item(1)->dp = (short) 5678;
    inv_set_slot(1, 0, 2);
    inv_item(2)->type = (unsigned char) 1;
    inv_item(2)->ap = (short) 7;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_caption_sprite(0), 0x40);
    CHECK_EQ(inv_figure_digit(0, 0), 1);
    CHECK_EQ(inv_figure_digit(0, 1), 2);
    CHECK_EQ(inv_figure_digit(0, 2), 3);
    CHECK_EQ(inv_figure_digit(0, 3), 4);
    CHECK_EQ(inv_caption_sprite(1), 0x40);
    CHECK_EQ(inv_figure_digit(1, 0), 0);
    CHECK_EQ(inv_figure_digit(1, 2), 0);
    CHECK_EQ(inv_figure_digit(1, 3), 7);
}

/* The armour caption, and the point of keeping the two classifications apart:
   CMP EAX,0x27 / JG is reached with the type reloaded, so TYPE 0 -- which took
   the catch-all ICON -- prints caption 0x41 and a DP figure here.  A rewrite
   that classified once and reused the answer would give type 0 the 0x43
   caption and no number instead (statwin.h). */
static void armour_and_type_zero_both_print_caption_41_and_dp(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, 0, 1);
    inv_item(1)->type = (unsigned char) 0x16;
    inv_item(1)->dp = (short) 907;
    inv_set_slot(1, 0, 2);
    inv_item(2)->type = (unsigned char) 0x27;
    inv_item(2)->dp = (short) 12;
    inv_set_slot(2, 0, 3);
    /* Type 0 with a use_effect that would otherwise print an HP recovery: it
       never reaches that test. */
    inv_item(3)->type = (unsigned char) 0;
    inv_item(3)->dp = (short) 34;
    inv_item(3)->use_effect = (unsigned char) 0x0b;
    inv_item(3)->use_amount = (short) 9999;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_caption_sprite(0), 0x41);
    CHECK_EQ(inv_figure_digit(0, 1), 9);
    CHECK_EQ(inv_figure_digit(0, 2), 0);
    CHECK_EQ(inv_figure_digit(0, 3), 7);
    CHECK_EQ(inv_caption_sprite(1), 0x41);
    CHECK_EQ(inv_figure_digit(1, 2), 1);
    CHECK_EQ(inv_figure_digit(1, 3), 2);
    CHECK_EQ(inv_icon_sprite(2), 0x1f);
    CHECK_EQ(inv_caption_sprite(2), 0x41);
    CHECK_EQ(inv_figure_digit(2, 2), 3);
    CHECK_EQ(inv_figure_digit(2, 3), 4);
}

/* CMP EAX,0xb / JNZ and CMP EAX,0xc / JNZ on the use_effect byte at +0x0d,
   with MOVSX EAX,word ptr [EAX + 0xe] for the amount: only those two print a
   figure, and every other effect -- 0x20 among them, which also restores HP --
   falls through to caption 0x43 with no number beside it. */
static void only_use_effects_b_and_c_print_a_recovery_amount(void)
{
    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(0, 0, 1);
    inv_item(1)->type = (unsigned char) 0x28;
    inv_item(1)->use_effect = (unsigned char) 0x0b;
    inv_item(1)->use_amount = (short) 50;
    inv_item(1)->dp = (short) 7777;
    inv_set_slot(1, 0, 2);
    inv_item(2)->type = (unsigned char) 0x40;
    inv_item(2)->use_effect = (unsigned char) 0x0c;
    inv_item(2)->use_amount = (short) 306;
    inv_set_slot(2, 0, 3);
    inv_item(3)->type = (unsigned char) 0x28;
    inv_item(3)->use_effect = (unsigned char) 0x20;
    inv_item(3)->use_amount = (short) 40;
    fdps_draw_unit_inventory(0, -1, inv_surface, INV_PITCH);

    CHECK_EQ(inv_caption_sprite(0), 0x3e);
    CHECK_EQ(inv_figure_digit(0, 2), 5);
    CHECK_EQ(inv_figure_digit(0, 3), 0);
    CHECK_EQ(inv_caption_sprite(1), 0x3f);
    CHECK_EQ(inv_figure_digit(1, 1), 3);
    CHECK_EQ(inv_figure_digit(1, 2), 0);
    CHECK_EQ(inv_figure_digit(1, 3), 6);
    CHECK_EQ(inv_caption_sprite(2), 0x43);
    /* No number was drawn, so the figure's second cell still holds the fill
       the caption sprite did not reach. */
    CHECK_EQ(inv_figure_digit(2, 1), -1);
    CHECK_EQ(inv_pixel(2 * INV_ROW_H + INV_CAP_ROW_BIAS,
                       INV_FIG_COL + INV_NUM_CELL_W), INV_FILL);
}

/* Every destination is dest_base + y * pitch + x, computed against the pitch
   the caller passed and never against a surface descriptor -- so at a pitch
   that is not 0x140 the same row and column land somewhere else. */
static void the_list_is_folded_into_the_pointer_with_the_callers_pitch(void)
{
    inv_reset(INV_ODD_PITCH);
    inv_empty_all_slots();
    inv_set_slot(2, 0, 9);
    inv_item(9)->type = (unsigned char) 3;
    inv_item(9)->ap = (short) 8;
    fdps_draw_unit_inventory(0, 2, inv_surface, INV_ODD_PITCH);

    CHECK_EQ(inv_bar_row(), 2 * INV_ROW_H + INV_BAR_ROW_BIAS);
    CHECK_EQ(inv_icon_sprite(2), 0x1d);
    CHECK_EQ(inv_caption_sprite(2), 0x40);
    CHECK_EQ(inv_name_id(2), 9);
    CHECK_EQ(inv_figure_digit(2, 3), 8);
}

/* dest_base is the list's own corner and not the surface origin: the same list
   drawn onto a base 0x3b58 bytes in -- what all four call sites pass -- lands
   0x3b58 bytes further on and nothing is left at the origin. */
static void the_list_hangs_off_dest_base(void)
{
    int shifted_row;
    int shifted_column;

    shifted_row = 0x3b58 / INV_PITCH;
    shifted_column = 0x3b58 % INV_PITCH;

    inv_reset(INV_PITCH);
    inv_empty_all_slots();
    inv_set_slot(1, INV_EQUIPPED, 0x21);
    inv_item(0x21)->type = (unsigned char) 0x16;
    inv_item(0x21)->dp = (short) 42;
    fdps_draw_unit_inventory(0, 1, inv_surface + 0x3b58, INV_PITCH);

    CHECK_EQ(inv_command_at(shifted_row + INV_ROW_H + INV_ICON_ROW_BIAS,
                            shifted_column + INV_ICON_COL), 0x1e + 4);
    CHECK_EQ(inv_command_at(shifted_row + INV_ROW_H + INV_CAP_ROW_BIAS,
                            shifted_column + INV_CAP_COL), 0x41);
    CHECK_EQ(inv_pixel(shifted_row + INV_ROW_H + INV_BAR_ROW_BIAS,
                       shifted_column), INV_BAR_COLOR);
    CHECK_EQ(inv_pixel(INV_ROW_H + INV_ICON_ROW_BIAS, INV_ICON_COL), INV_FILL);
    CHECK_EQ(inv_pixel(INV_ROW_H + INV_BAR_ROW_BIAS, 0), INV_FILL);
}

void run_statunit_tests(void)
{
    stage_window_image();

    RUN_TEST(a_code_at_or_below_7f_ends_the_wait);
    RUN_TEST(an_accepted_code_draws_no_frame);
    RUN_TEST(the_idle_flag_decides_whether_rand_is_drawn);
    RUN_TEST(an_accepted_code_takes_no_heap);

    panel_stage_sheets();
    RUN_TEST(every_figure_lands_at_its_own_offset);
    RUN_TEST(a_current_below_its_maximum_draws_in_the_reduced_row);
    RUN_TEST(a_full_unit_inherits_the_callers_colour_row);
    RUN_TEST(each_buff_timer_lights_only_its_own_figures);
    RUN_TEST(the_not_player_experience_sentinel_fills_the_field);
    RUN_TEST(the_gauges_round_up_and_take_a_graphic_each);
    RUN_TEST(the_village_flag_swaps_the_cache_slot_and_latches_the_index);
    RUN_TEST(the_three_names_come_from_their_own_blocks);

    inv_stage_sheets();
    RUN_TEST(the_bar_sits_on_the_selected_rows_baseline);
    RUN_TEST(a_selection_outside_the_eight_rows_draws_no_bar);
    RUN_TEST(the_empty_bit_skips_the_whole_row);
    RUN_TEST(every_row_sits_seventeen_pixels_below_the_last);
    RUN_TEST(the_icon_says_which_category_the_type_falls_in);
    RUN_TEST(the_equipped_bit_moves_the_icon_four_sprites_along);
    RUN_TEST(the_name_is_message_c9_plus_the_item_id);
    RUN_TEST(a_weapon_prints_caption_40_and_its_ap);
    RUN_TEST(armour_and_type_zero_both_print_caption_41_and_dp);
    RUN_TEST(only_use_effects_b_and_c_print_a_recovery_amount);
    RUN_TEST(the_list_is_folded_into_the_pointer_with_the_callers_pitch);
    RUN_TEST(the_list_hangs_off_dest_base);

    /* Every global panel_stage_sheets() wrote goes back: all five sheet
       pointers and the unit array base are statics of this file and would
       outlive it as live pointers into a unit that has finished, and the font
       metrics and the colour row are what a later unit expects to own. */
    panel_release_portrait();
    data_fdps_number_glyph_sheet_ptr = (unsigned char *) 0;
    data_fdps_status_gauge_bar_sheet_ptr = (unsigned char *) 0;
    data_fdps_cel_sprite_cache_ptr = (unsigned char *) 0;
    data_fdps_all_game_text_ptr = (unsigned char *) 0;
    data_fdps_font_sheet_ptr = (unsigned char *) 0;
    data_fdps_map_unit_array_ptr = (unsigned char *) 0;
    data_fdps_command_sprite_sheet_ptr = (unsigned char *) 0;
    data_fdps_selection_bar_sheet_ptr = (unsigned char *) 0;
    data_fdps_item_effect_table_ptr = (unsigned char *) 0;
    data_fdps_font_glyph_width = (unsigned char) 0;
    data_fdps_glyph_cell_height = (unsigned char) 0;
    data_fdps_font_glyph_stride_bytes = 0;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = 0;
    data_fdps_font_line_height = 0;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_village_mode_flag = (unsigned char) 0;
}
