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
