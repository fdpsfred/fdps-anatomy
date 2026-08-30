/* tests/indicat.c -- cover for src/indicat.c.
 *
 * Expected values come from the assembly at 0001f510 -- the four cull compares
 * at 0001f56e (JGE), 0001f58b (JG), 0001f5a8 (JLE) and 0001f5c7 (JGE), each
 * over an IDIV by 0x18; the cell x formed by MUL AH with AH = 6 and ADD AL,0x2
 * at 0001f604; the unit index byte at 0001f622; the CMP EAX,[EBP-0x8] / JBE at
 * 0001f637 that right-aligns; the glyph formed by ADD DL / SUB AL,0x30 at
 * 0001f642 and the 0xff blank at 0001f669; and the ADD [0x00064378],0x4 at
 * 0001f678 -- and from the record offsets ticket 17 measured (pos_x +0, pos_y
 * +1).  None of them is read off the emitted C.
 *
 * The unit array and the queue are staged here rather than read from a game
 * file, for the same reason tests/aitarget.c stages its own: the function takes
 * its whole input from those globals and its three arguments.  Nothing below
 * asserts what any global holds on its own -- ticket 23 owns that.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "indicat.h"

#define STAGE_UNITS 4
#define FILLER 0xee

/* The two glyph bases the shipped callers pass, and one of the digit ids each
   produces: 0x0d for an MP restore or a stat gain, 0x27 for healing. */
#define MP_GLYPH_BASE 0x0d
#define HEAL_GLYPH_BASE 0x27

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero every record, hand the array to the function under test, fill the whole
   queue with a value it never writes, and put the view at the map origin. */
static void stage(void)
{
    unsigned char *raw;
    int i;

    raw = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        raw[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = STAGE_UNITS;

    for (i = 0; i < INDICATOR_QUEUE_CELLS; i++) {
        data_fdps_indicator_queue_cell_x_offset[i] = FILLER;
        data_fdps_battle_indicator_queue_unit_idx[i] = FILLER;
        data_fdps_indicator_queue_glyph_ids[i] = FILLER;
    }
    data_fdps_indicator_queue_count = 0;

    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
}

static void place(int index, int x, int y)
{
    stage_units[index].pos_x = (unsigned char) x;
    stage_units[index].pos_y = (unsigned char) y;
}

/* Stand unit 0 on (tile_x, tile_y) with the view scrolled to those pixel
   origins, ask for a popup over it and hand back how many cells the queue took:
   4 when the unit was inside the window, 0 when it was culled. */
static int cells_queued(int scroll_x, int scroll_y, int tile_x, int tile_y)
{
    stage();
    data_fdps_battle_view_window_origin_x = scroll_x;
    data_fdps_battle_view_window_origin_y = scroll_y;
    place(0, tile_x, tile_y);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 0);
    return data_fdps_indicator_queue_count;
}

/* MOV AL,byte ptr [EBX] and MOV AL,byte ptr [EBX+0x1] at 0001f548 and 0001f552:
   the cull reads the first two bytes of the record, so those two fields have to
   be the record's first two bytes for the C to look at the same tile. */
static void number_position_is_the_first_two_record_bytes(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
}

/* DEC EAX / CMP EAX,[EBP-0x14] / JGE at 0001f56d: the left edge is exclusive
   against origin_tx - 1, so the first column kept is the origin column itself
   and the one before it is culled. */
static void number_x_window_starts_at_the_origin_column(void)
{
    CHECK_EQ(cells_queued(0, 0, 0, 0), 4);
    CHECK_EQ(cells_queued(240, 0, 9, 0), 0);
    CHECK_EQ(cells_queued(240, 0, 10, 0), 4);
}

/* ADD EAX,0xd / CMP EAX,[EBP-0x14] / JG at 0001f588: the right edge is
   exclusive against origin_tx + 13, so the last column kept is twelve to the
   right of the origin. */
static void number_x_window_is_thirteen_columns(void)
{
    CHECK_EQ(cells_queued(0, 0, 12, 0), 4);
    CHECK_EQ(cells_queued(0, 0, 13, 0), 0);
    CHECK_EQ(cells_queued(240, 0, 22, 0), 4);
    CHECK_EQ(cells_queued(240, 0, 23, 0), 0);
}

/* DEC EAX / CMP EAX,[EBP-0x10] / JLE at 0001f5a7 is INCLUSIVE where the x test
   at 0001f56d is exclusive: the row one above the origin row still queues a
   popup.  Writing the two tests alike changes which units get a number at the
   map edge (rebuild_info/pitfalls.md). */
static void number_y_window_starts_one_row_above_the_origin(void)
{
    CHECK_EQ(cells_queued(0, 240, 0, 8), 0);
    CHECK_EQ(cells_queued(0, 240, 0, 9), 4);
    CHECK_EQ(cells_queued(0, 240, 0, 10), 4);
}

/* ADD EAX,0x8 / CMP EAX,[EBP-0x10] / JGE at 0001f5c4: the bottom edge is
   inclusive against origin_ty + 8, so ten rows are kept in all against the x
   test's thirteen columns. */
static void number_y_window_ends_eight_rows_below(void)
{
    CHECK_EQ(cells_queued(0, 0, 0, 8), 4);
    CHECK_EQ(cells_queued(0, 0, 0, 9), 0);
    CHECK_EQ(cells_queued(0, 240, 0, 18), 4);
    CHECK_EQ(cells_queued(0, 240, 0, 19), 0);
}

/* MOV EBX,0x18 / IDIV EBX before each compare: the origins are pixels and the
   window is tiles, and the division truncates, so a view scrolled 23 pixels is
   still standing on column 0 and one scrolled 24 has moved a whole column. */
static void number_view_origin_is_divided_by_the_tile_size(void)
{
    CHECK_EQ(cells_queued(23, 0, 0, 0), 4);
    CHECK_EQ(cells_queued(23, 0, 12, 0), 4);
    CHECK_EQ(cells_queued(24, 0, 0, 0), 0);
    CHECK_EQ(cells_queued(24, 0, 13, 0), 4);
    CHECK_EQ(cells_queued(0, 23, 0, 8), 4);
    CHECK_EQ(cells_queued(0, 23, 0, 9), 0);
    CHECK_EQ(cells_queued(0, 24, 0, 9), 4);
    CHECK_EQ(cells_queued(0, 24, 0, 10), 0);
}

/* Every branch of the cull jumps to 0001f67f, past both the append and the ADD
   [0x00064378],0x4: a culled request writes nothing at all and leaves the cursor
   exactly where it found it, cells and all. */
static void number_culled_request_leaves_the_queue_untouched(void)
{
    stage();
    data_fdps_indicator_queue_count = 8;
    place(0, 40, 40);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_count, 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[8], FILLER);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[8], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[8], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[11], FILLER);
}

/* MOV AH,0x6 / MUL AH / ADD AL,0x2 at 0001f604: the four cells sit six pixels
   apart starting two pixels in.  The fixed-word popups at 0001f690 and 0001f7d0
   start at one instead, so this spacing is the number popup's own. */
static void number_cell_x_offsets_are_two_and_six_apart(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[0], 2);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[1], 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[2], 14);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[3], 20);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[4], FILLER);
}

/* MOV AL,byte ptr [EBP+0x1c] / MOV byte ptr [EDX+0x641e8],AL at 0001f622: the
   argument goes into every one of the four cells, blank cells included, and it
   is the index that was passed rather than anything read out of the record. */
static void number_unit_index_goes_into_every_cell(void)
{
    stage();
    place(2, 3, 3);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[0], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[1], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[2], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[3], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[4], FILLER);
}

/* CMP EAX,[EBP-0x8] / JBE at 0001f637 against a countdown that starts at 3 and
   drops every cell: a one-digit number lands in the LAST cell with the three
   before it blanked with 0xff, which is what right-aligns the popup without
   moving it. */
static void number_one_digit_lands_in_the_last_cell(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0xff);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], 0xff);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0xff);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], MP_GLYPH_BASE + 7);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[4], FILLER);
}

/* The digit cursor at [EBP-0x4] advances only on the cells that emitted
   (0001f659), so the digits stay in order in the cells that are left: 42 is 4
   then 2 in the last two cells and not 2 then 4. */
static void number_two_digits_keep_their_order(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(42, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0xff);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], 0xff);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], MP_GLYPH_BASE + 4);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], MP_GLYPH_BASE + 2);
}

/* Four digits fill all four cells and nothing is blanked: the countdown reaches
   0 on the last cell and strlen is still greater than it. */
static void number_four_digits_fill_every_cell(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(1234, HEAL_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], HEAL_GLYPH_BASE + 1);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], HEAL_GLYPH_BASE + 2);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], HEAL_GLYPH_BASE + 3);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], HEAL_GLYPH_BASE + 4);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[4], FILLER);
}

/* MOV DL,byte ptr [EBP+0x18] / ADD DL,... / SUB AL,0x30 at 0001f63f: the glyph
   is the base plus the digit, and the base is the caller's -- the same 7 is a
   different sprite for an MP restore and for a heal. */
static void number_glyph_is_the_callers_base_plus_the_digit(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(7, HEAL_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], HEAL_GLYPH_BASE + 7);
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(0, 0, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], 0);
}

/* The whole glyph arithmetic is eight bits wide -- ADD DL,byte / SUB AL,0x30
   over a byte parameter -- so a minus sign, whose ASCII is three below '0',
   wraps round from a base of 0 instead of going negative.  A rebuild that did
   the sum in ints and stored the low byte lands on the same 0xfd; one that
   clamped or widened would not. */
static void number_minus_sign_takes_a_cell_and_wraps(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(-5, 0, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0xff);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], 0xff);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0xfd);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], 5);
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(-5, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], MP_GLYPH_BASE - 3);
}

/* The loop runs exactly four times whatever the number is, and the digit cursor
   is only advanced by the cells that emitted, so the fifth and later digits are
   formatted and then never queued. */
static void number_five_digits_keep_the_first_four(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_number_indicator(12345, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], MP_GLYPH_BASE + 1);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], MP_GLYPH_BASE + 2);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], MP_GLYPH_BASE + 3);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], MP_GLYPH_BASE + 4);
    CHECK_EQ(data_fdps_indicator_queue_count, 4);
}

/* Every store is indexed [0x00064378] + cell (0001f60a, 0001f619, 0001f64a):
   the popup is appended AT the cursor and not at the front, and the cursor moves
   by exactly four, so a second request lands beside the first rather than over
   it. */
static void number_appends_at_the_cursor(void)
{
    stage();
    data_fdps_indicator_queue_count = 8;
    place(0, 3, 3);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_count, 12);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[7], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[8], 2);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[11], 20);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[11], MP_GLYPH_BASE + 7);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[12], FILLER);

    place(1, 3, 3);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 1);
    CHECK_EQ(data_fdps_indicator_queue_count, 16);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[11], 0);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[12], 1);
}

/* The record the cull reads is the one fdps_get_unit_record resolves from the
   argument, so which unit is on screen is decided by the unit the popup is for
   and not by any other. */
static void number_culls_against_the_named_units_record(void)
{
    stage();
    place(0, 40, 40);
    place(1, 3, 3);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 1);
    CHECK_EQ(data_fdps_indicator_queue_count, 4);
    stage();
    place(0, 40, 40);
    place(1, 3, 3);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
}

/* Nothing in the function writes through the record pointer: the unit it floats
   a number over is left exactly as it was found. */
static void number_does_not_touch_the_record(void)
{
    stage();
    place(0, 3, 4);
    stage_units[0].hp_current = 25;
    fdps_show_number_indicator(1234, MP_GLYPH_BASE, 0);
    CHECK_EQ(stage_units[0].pos_x, 3);
    CHECK_EQ(stage_units[0].pos_y, 4);
    CHECK_EQ(stage_units[0].hp_current, 25);
}

/* ---- fdps_show_miss_indicator @ 0001f690 -------------------------------- */

/* Stand unit 0 on (tile_x, tile_y) with the view scrolled to those pixel
   origins, ask for a MISS over it and hand back how many cells the queue took:
   4 when the unit was inside the window, 0 when it was culled. */
static int miss_cells_queued(int scroll_x, int scroll_y, int tile_x, int tile_y)
{
    stage();
    data_fdps_battle_view_window_origin_x = scroll_x;
    data_fdps_battle_view_window_origin_y = scroll_y;
    place(0, tile_x, tile_y);
    fdps_show_miss_indicator(0);
    return data_fdps_indicator_queue_count;
}

/* MOV EAX,[0x0001c2d2] / MOV [EBP-0x4],EAX at 0001f69c, over the four bytes
   34 35 36 36 that live there: the word is fixed in the function and the two S
   cells really do repeat the same glyph id rather than taking a second one. */
static void miss_glyphs_are_the_four_fixed_ids(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_miss_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0x34);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], 0x35);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0x36);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], 0x36);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[4], FILLER);
}

/* MOV AH,0x6 / MUL AH / INC AL at 0001f778 for every cell but one, and the
   branch at 0001f758 that sends cell 1 to ADD AL,0x2 instead: the offsets are
   1, 8, 13, 19 and NOT the uniform 1, 7, 13, 19.  Cell 1 is the only one that
   differs, so a rebuild that nudged the wrong cell or nudged them all lands
   here (rebuild_info/pitfalls.md). */
static void miss_cell_one_is_nudged_a_pixel_right(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_miss_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[0], 1);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[1], 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[2], 13);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[3], 19);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[4], FILLER);
}

/* The word popup and the number popup do NOT share their cell x offsets: the
   same four cells come out 1, 8, 13, 19 here and 2, 8, 14, 20 at 0001f510, so
   folding the two producers together would move one of them. */
static void miss_offsets_differ_from_the_number_popups(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_miss_indicator(0);
    fdps_show_number_indicator(7, MP_GLYPH_BASE, 0);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[0], 1);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[2], 13);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[4], 2);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[6], 14);
}

/* MOV AL,byte ptr [EBP+0x14] / MOV byte ptr [EDX+0x641e8],AL at 0001f799: the
   argument goes into all four cells as a byte, and it is the index that was
   passed rather than anything read out of the record. */
static void miss_unit_index_goes_into_every_cell(void)
{
    stage();
    place(2, 3, 3);
    fdps_show_miss_indicator(2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[0], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[1], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[2], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[3], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[4], FILLER);
}

/* DEC EAX / CMP EAX,[EBP-0x10] / JGE at 0001f6e1: the left edge is exclusive
   against origin_tx - 1, so the first column kept is the origin column and the
   one before it is culled. */
static void miss_x_window_starts_at_the_origin_column(void)
{
    CHECK_EQ(miss_cells_queued(0, 0, 0, 0), 4);
    CHECK_EQ(miss_cells_queued(240, 0, 9, 0), 0);
    CHECK_EQ(miss_cells_queued(240, 0, 10, 0), 4);
}

/* ADD EAX,0xd / CMP EAX,[EBP-0x10] / JG at 0001f6fe: the right edge is
   exclusive against origin_tx + 13, so the last column kept is twelve right of
   the origin. */
static void miss_x_window_is_thirteen_columns(void)
{
    CHECK_EQ(miss_cells_queued(0, 0, 12, 0), 4);
    CHECK_EQ(miss_cells_queued(0, 0, 13, 0), 0);
    CHECK_EQ(miss_cells_queued(240, 0, 22, 0), 4);
    CHECK_EQ(miss_cells_queued(240, 0, 23, 0), 0);
}

/* DEC EAX / CMP EAX,[EBP-0xc] / JLE at 0001f71b is INCLUSIVE where the x test
   at 0001f6e1 is exclusive: the row one above the origin row still queues a
   popup.  Writing the two axes alike changes which units get a MISS at the map
   edge (rebuild_info/pitfalls.md). */
static void miss_y_window_starts_one_row_above_the_origin(void)
{
    CHECK_EQ(miss_cells_queued(0, 240, 0, 8), 0);
    CHECK_EQ(miss_cells_queued(0, 240, 0, 9), 4);
    CHECK_EQ(miss_cells_queued(0, 240, 0, 10), 4);
}

/* ADD EAX,0x8 / CMP EAX,[EBP-0xc] / JGE at 0001f73a: the bottom edge is
   inclusive against origin_ty + 8, so ten rows are kept against the x test's
   thirteen columns. */
static void miss_y_window_ends_eight_rows_below(void)
{
    CHECK_EQ(miss_cells_queued(0, 0, 0, 8), 4);
    CHECK_EQ(miss_cells_queued(0, 0, 0, 9), 0);
    CHECK_EQ(miss_cells_queued(0, 240, 0, 18), 4);
    CHECK_EQ(miss_cells_queued(0, 240, 0, 19), 0);
}

/* MOV EBX,0x18 / IDIV EBX before each of the four compares: the origins are
   pixels and the window is tiles, and the signed division truncates, so a view
   scrolled 23 pixels is still standing on column 0 and one scrolled 24 has
   moved a whole column. */
static void miss_view_origin_is_divided_by_the_tile_size(void)
{
    CHECK_EQ(miss_cells_queued(23, 0, 0, 0), 4);
    CHECK_EQ(miss_cells_queued(23, 0, 12, 0), 4);
    CHECK_EQ(miss_cells_queued(24, 0, 0, 0), 0);
    CHECK_EQ(miss_cells_queued(24, 0, 13, 0), 4);
    CHECK_EQ(miss_cells_queued(0, 23, 0, 8), 4);
    CHECK_EQ(miss_cells_queued(0, 23, 0, 9), 0);
    CHECK_EQ(miss_cells_queued(0, 24, 0, 9), 4);
    CHECK_EQ(miss_cells_queued(0, 24, 0, 10), 0);
}

/* The origins are signed and IDIV is the signed divide, truncating toward zero:
   a scroll of -24 puts the origin on column -1, which keeps tile 0 and culls
   tile 12.  Reading either origin as unsigned would divide -24 into an enormous
   column and cull every unit on the map. */
static void miss_negative_scroll_divides_signed(void)
{
    CHECK_EQ(miss_cells_queued(-24, 0, 0, 0), 4);
    CHECK_EQ(miss_cells_queued(-24, 0, 11, 0), 4);
    CHECK_EQ(miss_cells_queued(-24, 0, 12, 0), 0);
    CHECK_EQ(miss_cells_queued(0, -24, 0, 7), 4);
    CHECK_EQ(miss_cells_queued(0, -24, 0, 8), 0);
}

/* Every branch of the cull jumps to 0001f7c1, past both the append and the
   ADD [0x00064378],0x4: a culled request writes nothing at all and leaves the
   cursor exactly where it found it. */
static void miss_culled_request_leaves_the_queue_untouched(void)
{
    stage();
    data_fdps_indicator_queue_count = 8;
    place(0, 40, 40);
    fdps_show_miss_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_count, 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[8], FILLER);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[8], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[8], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[11], FILLER);
}

/* Every store is indexed [0x00064378] + cell (0001f76d, 0001f787, 0001f796,
   0001f7a8) and the cursor moves by exactly four at 0001f7ba: the popup is
   appended AT the cursor, so a MISS queued after a number lands beside it and
   not over it. */
static void miss_appends_at_the_cursor(void)
{
    stage();
    data_fdps_indicator_queue_count = 8;
    place(0, 3, 3);
    fdps_show_miss_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_count, 12);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[7], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[8], 1);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[11], 19);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[8], 0x34);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[11], 0x36);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[12], FILLER);

    place(1, 3, 3);
    fdps_show_miss_indicator(1);
    CHECK_EQ(data_fdps_indicator_queue_count, 16);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[11], 0);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[12], 1);
}

/* The record the cull reads is the one fdps_get_unit_record resolves from the
   argument at 0001f6a8, so which unit is on screen is decided by the unit the
   MISS is for and not by any other. */
static void miss_culls_against_the_named_units_record(void)
{
    stage();
    place(0, 40, 40);
    place(1, 3, 3);
    fdps_show_miss_indicator(1);
    CHECK_EQ(data_fdps_indicator_queue_count, 4);
    stage();
    place(0, 40, 40);
    place(1, 3, 3);
    fdps_show_miss_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
}

/* XOR EAX,EAX before each of MOV AL,[EBX] and MOV AL,[EBX+1] at 0001f6b3 and
   0001f6bd: the two record bytes are ZERO extended into the signed compare, so
   a tile of 200 is 200 and not -56.  Scrolling the view onto column and row 200
   is what tells the two apart: a zero-extended 200 is inside that window and a
   sign-extended one is 256 tiles to the left of it and culled. */
static void miss_tile_bytes_are_zero_extended(void)
{
    CHECK_EQ(miss_cells_queued(4800, 0, 200, 0), 4);
    CHECK_EQ(miss_cells_queued(0, 4800, 0, 200), 4);
    CHECK_EQ(miss_cells_queued(0, 0, 200, 0), 0);
    CHECK_EQ(miss_cells_queued(0, 0, 0, 200), 0);
}

/* Nothing in the function writes through the record pointer: the unit it floats
   the word over is left exactly as it was found. */
static void miss_does_not_touch_the_record(void)
{
    stage();
    place(0, 3, 4);
    stage_units[0].hp_current = 25;
    fdps_show_miss_indicator(0);
    CHECK_EQ(stage_units[0].pos_x, 3);
    CHECK_EQ(stage_units[0].pos_y, 4);
    CHECK_EQ(stage_units[0].hp_current, 25);
}

/* ---- fdps_show_cure_indicator @ 0001f7d0 -------------------------------- */

/* Stand unit 0 on (tile_x, tile_y) with the view scrolled to those pixel
   origins, ask for a CURE over it and hand back how many cells the queue took:
   4 when the unit was inside the window, 0 when it was culled. */
static int cure_cells_queued(int scroll_x, int scroll_y, int tile_x, int tile_y)
{
    stage();
    data_fdps_battle_view_window_origin_x = scroll_x;
    data_fdps_battle_view_window_origin_y = scroll_y;
    place(0, tile_x, tile_y);
    fdps_show_cure_indicator(0);
    return data_fdps_indicator_queue_count;
}

/* MOV EAX,[0x0001c2d6] / MOV [EBP-0x4],EAX at 0001f7dc, over the four bytes
   37 38 39 3a that live there -- the four that follow MISS's own 34 35 36 36.
   The word is fixed in the function and all four ids differ, so a rebuild that
   took the address four bytes early would queue MISS out of this function and
   nothing else about it would look wrong. */
static void cure_glyphs_are_the_four_fixed_ids(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_cure_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0x37);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], 0x38);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0x39);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], 0x3a);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[4], FILLER);
}

/* The two fixed-word popups are separate functions over separate glyph dwords:
   queueing one after the other puts two different words in the queue rather
   than the same one twice. */
static void cure_and_miss_queue_different_words(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_cure_indicator(0);
    fdps_show_miss_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0x37);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], 0x3a);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[4], 0x34);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[7], 0x36);
    CHECK_EQ(data_fdps_indicator_queue_count, 8);
}

/* MOV AH,0x6 / MUL AH / INC AL at 0001f8bf for every cell but one, and the
   branch at 0001f89c that sends cell 1 to ADD AL,0x2 at 0001f8a5 instead: the
   offsets are 1, 8, 13, 19 here as well.  The branch is on the cell index, so
   the extra pixel lands on the U of CURE just as it lands on the I of MISS --
   a rebuild that turned the nudge into a narrow-letter rule would leave this
   popup at 1, 7, 13, 19 (rebuild_info/pitfalls.md). */
static void cure_cell_one_is_nudged_a_pixel_right(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_cure_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[0], 1);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[1], 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[2], 13);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[3], 19);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[4], FILLER);
}

/* MOV AL,byte ptr [EBP+0x14] / MOV byte ptr [EDX+0x641e8],AL at 0001f8d9: the
   argument goes into all four cells as a byte, and it is the index that was
   passed rather than anything read out of the record. */
static void cure_unit_index_goes_into_every_cell(void)
{
    stage();
    place(2, 3, 3);
    fdps_show_cure_indicator(2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[0], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[1], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[2], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[3], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[4], FILLER);
}

/* DEC EAX / CMP EAX,[EBP-0x10] / JGE at 0001f821: the left edge is exclusive
   against origin_tx - 1, so the first column kept is the origin column and the
   one before it is culled. */
static void cure_x_window_starts_at_the_origin_column(void)
{
    CHECK_EQ(cure_cells_queued(0, 0, 0, 0), 4);
    CHECK_EQ(cure_cells_queued(240, 0, 9, 0), 0);
    CHECK_EQ(cure_cells_queued(240, 0, 10, 0), 4);
}

/* ADD EAX,0xd / CMP EAX,[EBP-0x10] / JG at 0001f83e: the right edge is
   exclusive against origin_tx + 13, so the last column kept is twelve right of
   the origin. */
static void cure_x_window_is_thirteen_columns(void)
{
    CHECK_EQ(cure_cells_queued(0, 0, 12, 0), 4);
    CHECK_EQ(cure_cells_queued(0, 0, 13, 0), 0);
    CHECK_EQ(cure_cells_queued(240, 0, 22, 0), 4);
    CHECK_EQ(cure_cells_queued(240, 0, 23, 0), 0);
}

/* DEC EAX / CMP EAX,[EBP-0xc] / JLE at 0001f85b is INCLUSIVE where the x test
   at 0001f821 is exclusive: the row one above the origin row still queues a
   popup.  Writing the two axes alike changes which units get a CURE at the map
   edge (rebuild_info/pitfalls.md). */
static void cure_y_window_starts_one_row_above_the_origin(void)
{
    CHECK_EQ(cure_cells_queued(0, 240, 0, 8), 0);
    CHECK_EQ(cure_cells_queued(0, 240, 0, 9), 4);
    CHECK_EQ(cure_cells_queued(0, 240, 0, 10), 4);
}

/* ADD EAX,0x8 / CMP EAX,[EBP-0xc] / JGE at 0001f87a: the bottom edge is
   inclusive against origin_ty + 8, so ten rows are kept against the x test's
   thirteen columns. */
static void cure_y_window_ends_eight_rows_below(void)
{
    CHECK_EQ(cure_cells_queued(0, 0, 0, 8), 4);
    CHECK_EQ(cure_cells_queued(0, 0, 0, 9), 0);
    CHECK_EQ(cure_cells_queued(0, 240, 0, 18), 4);
    CHECK_EQ(cure_cells_queued(0, 240, 0, 19), 0);
}

/* MOV EBX,0x18 / IDIV EBX before each of the four compares (0001f81b, 0001f836,
   0001f855, 0001f872): the origins are pixels and the window is tiles, and the
   division truncates, so a view scrolled 23 pixels is still standing on column
   0 and one scrolled 24 has moved a whole column. */
static void cure_view_origin_is_divided_by_the_tile_size(void)
{
    CHECK_EQ(cure_cells_queued(23, 0, 0, 0), 4);
    CHECK_EQ(cure_cells_queued(23, 0, 12, 0), 4);
    CHECK_EQ(cure_cells_queued(24, 0, 0, 0), 0);
    CHECK_EQ(cure_cells_queued(24, 0, 13, 0), 4);
    CHECK_EQ(cure_cells_queued(0, 23, 0, 8), 4);
    CHECK_EQ(cure_cells_queued(0, 23, 0, 9), 0);
    CHECK_EQ(cure_cells_queued(0, 24, 0, 9), 4);
    CHECK_EQ(cure_cells_queued(0, 24, 0, 10), 0);
}

/* SAR EDX,0x1f before each IDIV: the origins are signed and the divide is the
   signed one, truncating toward zero, so a scroll of -24 puts the origin on
   column -1, which keeps tile 0 and culls tile 12.  Reading either origin as
   unsigned would divide -24 into an enormous column and cull every unit. */
static void cure_negative_scroll_divides_signed(void)
{
    CHECK_EQ(cure_cells_queued(-24, 0, 0, 0), 4);
    CHECK_EQ(cure_cells_queued(-24, 0, 11, 0), 4);
    CHECK_EQ(cure_cells_queued(-24, 0, 12, 0), 0);
    CHECK_EQ(cure_cells_queued(0, -24, 0, 7), 4);
    CHECK_EQ(cure_cells_queued(0, -24, 0, 8), 0);
}

/* Every branch of the cull jumps to 0001f901, past both the append and the
   ADD [0x00064378],0x4 at 0001f8fa: a culled request writes nothing at all and
   leaves the cursor exactly where it found it. */
static void cure_culled_request_leaves_the_queue_untouched(void)
{
    stage();
    data_fdps_indicator_queue_count = 8;
    place(0, 40, 40);
    fdps_show_cure_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_count, 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[8], FILLER);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[8], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[8], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[11], FILLER);
}

/* Every store is indexed [0x00064378] + cell (0001f8ad, 0001f8c7, 0001f8d6,
   0001f8e8) and the cursor moves by exactly four at 0001f8fa: the popup is
   appended AT the cursor, so a CURE queued after another popup lands beside it
   and not over it. */
static void cure_appends_at_the_cursor(void)
{
    stage();
    data_fdps_indicator_queue_count = 8;
    place(0, 3, 3);
    fdps_show_cure_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_count, 12);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[7], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[8], 1);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[11], 19);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[8], 0x37);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[11], 0x3a);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[12], FILLER);

    place(1, 3, 3);
    fdps_show_cure_indicator(1);
    CHECK_EQ(data_fdps_indicator_queue_count, 16);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[11], 0);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[12], 1);
}

/* The record the cull reads is the one fdps_get_unit_record resolves from the
   argument at 0001f7e8, so which unit is on screen is decided by the unit the
   CURE is for and not by any other. */
static void cure_culls_against_the_named_units_record(void)
{
    stage();
    place(0, 40, 40);
    place(1, 3, 3);
    fdps_show_cure_indicator(1);
    CHECK_EQ(data_fdps_indicator_queue_count, 4);
    stage();
    place(0, 40, 40);
    place(1, 3, 3);
    fdps_show_cure_indicator(0);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
}

/* XOR EAX,EAX before each of MOV AL,[EBX] and MOV AL,[EBX+1] at 0001f7f3 and
   0001f7fd: the two record bytes are ZERO extended into the signed compare, so
   a tile of 200 is 200 and not -56.  Scrolling the view onto column and row 200
   is what tells the two apart. */
static void cure_tile_bytes_are_zero_extended(void)
{
    CHECK_EQ(cure_cells_queued(4800, 0, 200, 0), 4);
    CHECK_EQ(cure_cells_queued(0, 4800, 0, 200), 4);
    CHECK_EQ(cure_cells_queued(0, 0, 200, 0), 0);
    CHECK_EQ(cure_cells_queued(0, 0, 0, 200), 0);
}

/* Nothing in the function writes through the record pointer.  The callers clear
   the status-ailment bytes themselves AFTER this call returns -- MOV byte ptr
   [EAX+0x25],0x0 at 000268a4, which is status_timers[3], and the memset in
   fdps_cast_spell_on_targets over +0x25..+0x27 --
   so a rebuild that cleared them here would clear them twice and, worse, would
   clear them on the path where the popup was culled. */
static void cure_does_not_touch_the_record(void)
{
    stage();
    place(0, 3, 4);
    stage_units[0].hp_current = 25;
    stage_units[0].status_timers[3] = 3;
    fdps_show_cure_indicator(0);
    CHECK_EQ(stage_units[0].pos_x, 3);
    CHECK_EQ(stage_units[0].pos_y, 4);
    CHECK_EQ(stage_units[0].hp_current, 25);
    CHECK_EQ(stage_units[0].status_timers[3], 3);
}

/* ---- fdps_show_sprite_indicator @ 0001fc00 ------------------------------ */

/* The three labels the shipped caller passes, read out of the table at
   0x00027668: 3b 3c 3c 00 / 3d 3e 3f 00 / 3d 3e 40 00.  Each is three glyph ids
   followed by the zero that marks the fourth cell unused, and decoding
   Number.cel makes them Att, Def and Dex. */
static unsigned char sprite_label_att[4] = { 0x3b, 0x3c, 0x3c, 0x00 };
static unsigned char sprite_label_def[4] = { 0x3d, 0x3e, 0x3f, 0x00 };
static unsigned char sprite_label_dex[4] = { 0x3d, 0x3e, 0x40, 0x00 };

/* Stand unit 0 on (tile_x, tile_y) with the view scrolled to those pixel
   origins, ask for the Att label over it and hand back how many cells the queue
   took: 3 when the unit was inside the window -- the label's three non-zero ids
   -- and 0 when it was culled. */
static int sprite_cells_queued(int scroll_x, int scroll_y, int tile_x,
                               int tile_y)
{
    stage();
    data_fdps_battle_view_window_origin_x = scroll_x;
    data_fdps_battle_view_window_origin_y = scroll_y;
    place(0, tile_x, tile_y);
    fdps_show_sprite_indicator(0, sprite_label_att);
    return data_fdps_indicator_queue_count;
}

/* MOV EAX,dword ptr [EBP+0x18] / MOV AL,byte ptr [EAX] at 0001fcca and
   0001fd2e: the glyph ids come from the caller's buffer and are stored into the
   queue unchanged, with no base added and no translation -- unlike the number
   popup, which adds its caller's glyph_base to every digit. */
static void sprite_glyph_ids_are_copied_verbatim(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, sprite_label_att);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0x3b);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], 0x3c);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0x3c);
}

/* The word really is the argument and not a constant of the function: Def and
   Dex share their first two ids and differ in the third, so queueing each in
   turn puts two different words in the queue. */
static void sprite_word_comes_from_the_argument(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, sprite_label_def);
    fdps_show_sprite_indicator(0, sprite_label_dex);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0x3d);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0x3f);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], 0x3d);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[5], 0x40);
    CHECK_EQ(data_fdps_indicator_queue_count, 6);
}

/* CMP byte ptr [EAX],0x0 / JZ 0001fd3c at 0001fcd0: a zero id writes NOTHING
   for its cell.  The queue's own blank marker is 0xff and this function never
   writes it, so the fourth cell of a three-id label is left exactly as it was
   found -- a rebuild that queued the unused cell blank would look identical on
   screen and push every later popup along by a cell. */
static void sprite_zero_id_queues_no_cell_at_all(void)
{
    stage();
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, sprite_label_att);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[3], FILLER);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[3], FILLER);
}

/* The JZ at 0001fcd3 jumps to the loop's increment at 0001fcc2 and not out of
   the loop: a zero in the MIDDLE of the four skips only its own cell and the
   cells after it are still queued.  They are also stored at cursor + the loop
   counter (0001fce4, 0001fd0d, 0001fd25), so the third id lands in slot 2 with
   slot 1 untouched rather than being packed down into it. */
static void sprite_zero_id_skips_only_its_own_cell(void)
{
    unsigned char gapped[4];

    gapped[0] = 0x3b;
    gapped[1] = 0x00;
    gapped[2] = 0x3d;
    gapped[3] = 0x00;

    stage();
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, gapped);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], 0x3b);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[1], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0x3d);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[3], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[0], 1);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[2], 13);
}

/* MOV EAX,[EBP-0x4] / ADD [0x00064378],EAX at 0001fd3e: the cursor advances by
   the number of cells actually written and not by four, so a three-id label
   costs the queue three cells.  The sibling producers add a constant four
   (0001f678, 0001f7ba, 0001f8fa) and writing that here would leave a stale
   fourth cell inside the drawn range. */
static void sprite_cursor_advances_by_the_cells_written(void)
{
    unsigned char full[4];

    stage();
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, sprite_label_att);
    CHECK_EQ(data_fdps_indicator_queue_count, 3);

    full[0] = 0x3b;
    full[1] = 0x3c;
    full[2] = 0x3d;
    full[3] = 0x3e;
    stage();
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, full);
    CHECK_EQ(data_fdps_indicator_queue_count, 4);
}

/* The cells are indexed by the loop counter while the cursor moves by the
   write count, so the two only agree while the non-zero ids form a prefix.  A
   gapped label leaves the cursor two on with a cell standing at slot 2, and the
   next request overwrites it -- that is the original's behaviour and not a
   defect to be tidied away by packing the cells or by advancing the cursor past
   the highest slot written. */
static void sprite_gapped_label_leaves_the_cursor_short(void)
{
    unsigned char gapped[4];

    gapped[0] = 0x3b;
    gapped[1] = 0x00;
    gapped[2] = 0x3d;
    gapped[3] = 0x00;

    stage();
    place(0, 3, 3);
    place(1, 3, 3);
    fdps_show_sprite_indicator(0, gapped);
    CHECK_EQ(data_fdps_indicator_queue_count, 2);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0x3d);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[2], 0);

    fdps_show_sprite_indicator(1, sprite_label_att);
    CHECK_EQ(data_fdps_indicator_queue_count, 5);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[2], 0x3b);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[2], 1);
}

/* A label of four zeros passes the cull, runs the loop four times, writes
   nothing and adds nothing: the ADD at 0001fd41 is reached with EAX = 0. */
static void sprite_all_zero_label_queues_nothing(void)
{
    unsigned char empty[4];

    empty[0] = 0;
    empty[1] = 0;
    empty[2] = 0;
    empty[3] = 0;

    stage();
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, empty);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[0], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[0], FILLER);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[0], FILLER);
}

/* MOV AH,0x6 / MUL AH / INC AL at 0001fcf5 for every cell but one, and the
   branch at 0001fcd5 that sends cell 1 to ADD AL,0x2 at 0001fce2 instead: the
   offsets are the fixed-word popup's 1, 8, 13, 19 and not the number popup's
   2, 8, 14, 20.  The nudge is on the cell index, so it lands on the second
   letter of every label alike. */
static void sprite_cell_x_offsets_are_the_word_offsets(void)
{
    unsigned char full[4];

    full[0] = 0x3b;
    full[1] = 0x3c;
    full[2] = 0x3d;
    full[3] = 0x3e;

    stage();
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, full);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[0], 1);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[1], 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[2], 13);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[3], 19);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[4], FILLER);
}

/* MOV AL,byte ptr [EBP+0x14] / MOV byte ptr [EDX+0x641e8],AL at 0001fd16: the
   argument goes into every cell that was queued as a byte, and it is the index
   that was passed rather than anything read out of the record. */
static void sprite_unit_index_goes_into_every_queued_cell(void)
{
    stage();
    place(2, 3, 3);
    fdps_show_sprite_indicator(2, sprite_label_att);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[0], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[1], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[2], 2);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[3], FILLER);
}

/* DEC EAX / CMP EAX,[EBP-0x10] / JGE at 0001fc4c: the left edge is exclusive
   against origin_tx - 1, so the first column kept is the origin column and the
   one before it is culled. */
static void sprite_x_window_starts_at_the_origin_column(void)
{
    CHECK_EQ(sprite_cells_queued(0, 0, 0, 0), 3);
    CHECK_EQ(sprite_cells_queued(240, 0, 9, 0), 0);
    CHECK_EQ(sprite_cells_queued(240, 0, 10, 0), 3);
}

/* ADD EAX,0xd / CMP EAX,[EBP-0x10] / JG at 0001fc67: the right edge is
   exclusive against origin_tx + 13, so the last column kept is twelve right of
   the origin. */
static void sprite_x_window_is_thirteen_columns(void)
{
    CHECK_EQ(sprite_cells_queued(0, 0, 12, 0), 3);
    CHECK_EQ(sprite_cells_queued(0, 0, 13, 0), 0);
    CHECK_EQ(sprite_cells_queued(240, 0, 22, 0), 3);
    CHECK_EQ(sprite_cells_queued(240, 0, 23, 0), 0);
}

/* DEC EAX / CMP EAX,[EBP-0xc] / JLE at 0001fc86 is INCLUSIVE where the x test
   at 0001fc4c is exclusive: the row one above the origin row still queues a
   popup.  Writing the two axes alike changes which units get their buff label
   at the map edge (rebuild_info/pitfalls.md). */
static void sprite_y_window_starts_one_row_above_the_origin(void)
{
    CHECK_EQ(sprite_cells_queued(0, 240, 0, 8), 0);
    CHECK_EQ(sprite_cells_queued(0, 240, 0, 9), 3);
    CHECK_EQ(sprite_cells_queued(0, 240, 0, 10), 3);
}

/* ADD EAX,0x8 / CMP EAX,[EBP-0xc] / JGE at 0001fca3: the bottom edge is
   inclusive against origin_ty + 8, so ten rows are kept against the x test's
   thirteen columns. */
static void sprite_y_window_ends_eight_rows_below(void)
{
    CHECK_EQ(sprite_cells_queued(0, 0, 0, 8), 3);
    CHECK_EQ(sprite_cells_queued(0, 0, 0, 9), 0);
    CHECK_EQ(sprite_cells_queued(0, 240, 0, 18), 3);
    CHECK_EQ(sprite_cells_queued(0, 240, 0, 19), 0);
}

/* MOV EBX,0x18 / IDIV EBX before each of the four compares (0001fc4a, 0001fc65,
   0001fc84, 0001fca1): the origins are pixels and the window is tiles, and the
   division truncates, so a view scrolled 23 pixels is still standing on column
   0 and one scrolled 24 has moved a whole column. */
static void sprite_view_origin_is_divided_by_the_tile_size(void)
{
    CHECK_EQ(sprite_cells_queued(23, 0, 0, 0), 3);
    CHECK_EQ(sprite_cells_queued(23, 0, 12, 0), 3);
    CHECK_EQ(sprite_cells_queued(24, 0, 0, 0), 0);
    CHECK_EQ(sprite_cells_queued(24, 0, 13, 0), 3);
    CHECK_EQ(sprite_cells_queued(0, 23, 0, 8), 3);
    CHECK_EQ(sprite_cells_queued(0, 23, 0, 9), 0);
    CHECK_EQ(sprite_cells_queued(0, 24, 0, 9), 3);
    CHECK_EQ(sprite_cells_queued(0, 24, 0, 10), 0);
}

/* SAR EDX,0x1f before each IDIV: the origins are signed and the divide is the
   signed one, truncating toward zero, so a scroll of -24 puts the origin on
   column -1, which keeps tile 0 and culls tile 12.  Reading either origin as
   unsigned would divide -24 into an enormous column and cull every unit. */
static void sprite_negative_scroll_divides_signed(void)
{
    CHECK_EQ(sprite_cells_queued(-24, 0, 0, 0), 3);
    CHECK_EQ(sprite_cells_queued(-24, 0, 11, 0), 3);
    CHECK_EQ(sprite_cells_queued(-24, 0, 12, 0), 0);
    CHECK_EQ(sprite_cells_queued(0, -24, 0, 7), 3);
    CHECK_EQ(sprite_cells_queued(0, -24, 0, 8), 0);
}

/* XOR EAX,EAX before each of MOV AL,[EBX] and MOV AL,[EBX+1] at 0001fc22 and
   0001fc2c: the two record bytes are ZERO extended into the signed compare, so
   a tile of 200 is 200 and not -56. */
static void sprite_tile_bytes_are_zero_extended(void)
{
    CHECK_EQ(sprite_cells_queued(4800, 0, 200, 0), 3);
    CHECK_EQ(sprite_cells_queued(0, 4800, 0, 200), 3);
    CHECK_EQ(sprite_cells_queued(0, 0, 200, 0), 0);
    CHECK_EQ(sprite_cells_queued(0, 0, 0, 200), 0);
}

/* Every branch of the cull jumps to 0001fd47, past both the append and the ADD
   [0x00064378],EAX at 0001fd41: a culled request writes nothing at all and
   leaves the cursor exactly where it found it. */
static void sprite_culled_request_leaves_the_queue_untouched(void)
{
    stage();
    data_fdps_indicator_queue_count = 8;
    place(0, 40, 40);
    fdps_show_sprite_indicator(0, sprite_label_att);
    CHECK_EQ(data_fdps_indicator_queue_count, 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[8], FILLER);
    CHECK_EQ(data_fdps_battle_indicator_queue_unit_idx[8], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[8], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[10], FILLER);
}

/* Every store is indexed [0x00064378] + cell (0001fce4, 0001fd0d, 0001fd25):
   the popup is appended AT the cursor, so a label queued after another popup
   lands beside it and not over it. */
static void sprite_appends_at_the_cursor(void)
{
    stage();
    data_fdps_indicator_queue_count = 8;
    place(0, 3, 3);
    fdps_show_sprite_indicator(0, sprite_label_att);
    CHECK_EQ(data_fdps_indicator_queue_count, 11);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[7], FILLER);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[8], 1);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[9], 8);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[10], 13);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[8], 0x3b);
    CHECK_EQ(data_fdps_indicator_queue_glyph_ids[10], 0x3c);
    CHECK_EQ(data_fdps_indicator_queue_cell_x_offset[11], FILLER);
}

/* The record the cull reads is the one fdps_get_unit_record resolves from the
   argument at 0001fc17, so which unit is on screen is decided by the unit the
   label is for and not by any other. */
static void sprite_culls_against_the_named_units_record(void)
{
    stage();
    place(0, 40, 40);
    place(1, 3, 3);
    fdps_show_sprite_indicator(1, sprite_label_att);
    CHECK_EQ(data_fdps_indicator_queue_count, 3);
    stage();
    place(0, 40, 40);
    place(1, 3, 3);
    fdps_show_sprite_indicator(0, sprite_label_att);
    CHECK_EQ(data_fdps_indicator_queue_count, 0);
}

/* Nothing in the function writes through the record pointer or through the
   caller's label: the unit is left as it was found and so are the four bytes
   the caller handed over, which matters because the shipped caller passes a row
   of a table it reuses for every target. */
static void sprite_touches_neither_the_record_nor_the_label(void)
{
    stage();
    place(0, 3, 4);
    stage_units[0].hp_current = 25;
    fdps_show_sprite_indicator(0, sprite_label_att);
    CHECK_EQ(stage_units[0].pos_x, 3);
    CHECK_EQ(stage_units[0].pos_y, 4);
    CHECK_EQ(stage_units[0].hp_current, 25);
    CHECK_EQ(sprite_label_att[0], 0x3b);
    CHECK_EQ(sprite_label_att[1], 0x3c);
    CHECK_EQ(sprite_label_att[2], 0x3c);
    CHECK_EQ(sprite_label_att[3], 0x00);
}

void run_indicat_tests(void)
{
    RUN_TEST(number_position_is_the_first_two_record_bytes);
    RUN_TEST(number_x_window_starts_at_the_origin_column);
    RUN_TEST(number_x_window_is_thirteen_columns);
    RUN_TEST(number_y_window_starts_one_row_above_the_origin);
    RUN_TEST(number_y_window_ends_eight_rows_below);
    RUN_TEST(number_view_origin_is_divided_by_the_tile_size);
    RUN_TEST(number_culled_request_leaves_the_queue_untouched);
    RUN_TEST(number_cell_x_offsets_are_two_and_six_apart);
    RUN_TEST(number_unit_index_goes_into_every_cell);
    RUN_TEST(number_one_digit_lands_in_the_last_cell);
    RUN_TEST(number_two_digits_keep_their_order);
    RUN_TEST(number_four_digits_fill_every_cell);
    RUN_TEST(number_glyph_is_the_callers_base_plus_the_digit);
    RUN_TEST(number_minus_sign_takes_a_cell_and_wraps);
    RUN_TEST(number_five_digits_keep_the_first_four);
    RUN_TEST(number_appends_at_the_cursor);
    RUN_TEST(number_culls_against_the_named_units_record);
    RUN_TEST(number_does_not_touch_the_record);

    RUN_TEST(miss_glyphs_are_the_four_fixed_ids);
    RUN_TEST(miss_cell_one_is_nudged_a_pixel_right);
    RUN_TEST(miss_offsets_differ_from_the_number_popups);
    RUN_TEST(miss_unit_index_goes_into_every_cell);
    RUN_TEST(miss_x_window_starts_at_the_origin_column);
    RUN_TEST(miss_x_window_is_thirteen_columns);
    RUN_TEST(miss_y_window_starts_one_row_above_the_origin);
    RUN_TEST(miss_y_window_ends_eight_rows_below);
    RUN_TEST(miss_view_origin_is_divided_by_the_tile_size);
    RUN_TEST(miss_negative_scroll_divides_signed);
    RUN_TEST(miss_culled_request_leaves_the_queue_untouched);
    RUN_TEST(miss_appends_at_the_cursor);
    RUN_TEST(miss_culls_against_the_named_units_record);
    RUN_TEST(miss_tile_bytes_are_zero_extended);
    RUN_TEST(miss_does_not_touch_the_record);

    RUN_TEST(cure_glyphs_are_the_four_fixed_ids);
    RUN_TEST(cure_and_miss_queue_different_words);
    RUN_TEST(cure_cell_one_is_nudged_a_pixel_right);
    RUN_TEST(cure_unit_index_goes_into_every_cell);
    RUN_TEST(cure_x_window_starts_at_the_origin_column);
    RUN_TEST(cure_x_window_is_thirteen_columns);
    RUN_TEST(cure_y_window_starts_one_row_above_the_origin);
    RUN_TEST(cure_y_window_ends_eight_rows_below);
    RUN_TEST(cure_view_origin_is_divided_by_the_tile_size);
    RUN_TEST(cure_negative_scroll_divides_signed);
    RUN_TEST(cure_culled_request_leaves_the_queue_untouched);
    RUN_TEST(cure_appends_at_the_cursor);
    RUN_TEST(cure_culls_against_the_named_units_record);
    RUN_TEST(cure_tile_bytes_are_zero_extended);
    RUN_TEST(cure_does_not_touch_the_record);

    RUN_TEST(sprite_glyph_ids_are_copied_verbatim);
    RUN_TEST(sprite_word_comes_from_the_argument);
    RUN_TEST(sprite_zero_id_queues_no_cell_at_all);
    RUN_TEST(sprite_zero_id_skips_only_its_own_cell);
    RUN_TEST(sprite_cursor_advances_by_the_cells_written);
    RUN_TEST(sprite_gapped_label_leaves_the_cursor_short);
    RUN_TEST(sprite_all_zero_label_queues_nothing);
    RUN_TEST(sprite_cell_x_offsets_are_the_word_offsets);
    RUN_TEST(sprite_unit_index_goes_into_every_queued_cell);
    RUN_TEST(sprite_x_window_starts_at_the_origin_column);
    RUN_TEST(sprite_x_window_is_thirteen_columns);
    RUN_TEST(sprite_y_window_starts_one_row_above_the_origin);
    RUN_TEST(sprite_y_window_ends_eight_rows_below);
    RUN_TEST(sprite_view_origin_is_divided_by_the_tile_size);
    RUN_TEST(sprite_negative_scroll_divides_signed);
    RUN_TEST(sprite_tile_bytes_are_zero_extended);
    RUN_TEST(sprite_culled_request_leaves_the_queue_untouched);
    RUN_TEST(sprite_appends_at_the_cursor);
    RUN_TEST(sprite_culls_against_the_named_units_record);
    RUN_TEST(sprite_touches_neither_the_record_nor_the_label);

    /* Put the globals back before leaving.  The runners share one process, and
       a later unit that expects an empty battle or an empty queue would
       otherwise inherit this file's fixture and pass or fail for the wrong
       reason. */
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_indicator_queue_count = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
}
