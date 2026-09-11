/* tests/spellmnu.c -- the spell list UI (src/spellmnu.c).
 *
 * HOW THE PAGE IS READ BACK.  Nothing here is mocked: fdps_draw_spell_list_page
 * is linked against the real collector, the real .CEL drawers, the real text
 * drawer and the real figure drawer, and every expected value below comes from
 * the assembly at 000276f0 or from the record layout in src/fdpstype.h.  What
 * makes the drawing readable is the artwork.  Each fabricated sprite paints
 * exactly ONE pixel, its own top left, and skips the rest of its rectangle, so
 * a blit that would otherwise cover 25 by 22 leaves a single byte on the page
 * whose position is the blit's destination and whose value says which sprite it
 * was.  That is the .CEL stream format doing its job -- op 11 is a skip and
 * writes nothing (rle.h) -- and not a stand-in for a drawer.
 *
 * The fabricated font is one bit wide and one row tall with every glyph solid,
 * so a text entry of n glyphs paints n consecutive foreground pixels and the
 * length of that run says which entry was drawn.  Entry n of the fabricated
 * text block is 1 + (n % 3) glyphs long, so two entries that are one apart are
 * never the same length: an off-by-one in the 0x1be name base shows up on every
 * row rather than on some of them.
 *
 * WHY THE PITCH IS 0x97.  It is the narrow status-window stride the plate
 * comment names, and it is not a power of two and not 0x140, so a row address
 * built as (row * 0x11 + k) * pitch + x cannot be confused with any other
 * reading of the same constants.
 *
 * fdps_spell_list_window_wait_input IS COVERED ONLY ON THE PASS THAT ENDS THE
 * WAIT.  Its loop polls the keyboard and draws a frame whenever the timer tick
 * has moved; nothing in the test binary moves that tick, because the counter is
 * written by the game's timer interrupt handler and no test installs it.  A
 * call made to draw one frame therefore cannot then be made to return -- the
 * auto-repeat reader answers 0xff for every poll after the first while the tick
 * stands still, and the loop spins.  The cases below drive the reader to hand
 * back an accepted code on the first poll and pin what that pass does: which
 * code ends the wait, that the poll is tested before the tick, that the reader
 * is polled exactly once, and that nothing is drawn or allocated.  Everything
 * the drawing pass does is a playtest contract until the sprite cache, the
 * shadow sheet and a running timer are all real.
 */
#include <i86.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "sprite.h"
#include "statunit.h"
#include "table.h"
#include "text.h"
#include "unitstat.h"
#include "spellmnu.h"
#include "testharn.h"

/* The surface.  160 rows is above the 154 the bottom row's MP caption reaches
   (7 * 0x11 + 0x0d + 22), so every blit the drawer can make stays inside the
   array even where the case expects it to paint nothing. */
#define SPELL_PITCH 0x97
#define SPELL_PAGE_ROWS 160
#define SPELL_PAGE_BYTES (SPELL_PITCH * SPELL_PAGE_ROWS)
#define SPELL_SENTINEL 0xee

/* The geometry the cases read the page at, taken from the assembly: IMUL
   ...,0x11 for the row, then +8 for the selection bar and the name, +9 for the
   spell icon and +0x0d for the MP caption; x is 0, 3, 0x13 and 0x66, and the
   figure stands 0x16 right of the caption. */
#define SPELL_ROW_PITCH 0x11
#define SPELL_BAR_Y 8
#define SPELL_BAR_X 0
#define SPELL_ICON_Y 9
#define SPELL_ICON_X 3
#define SPELL_NAME_Y 8
#define SPELL_NAME_X 0x13
#define SPELL_MP_Y 0x0d
#define SPELL_MP_X 0x66
#define SPELL_FIGURE_X (SPELL_MP_X + 0x16)
#define SPELL_ROWS 8

/* The sprites the drawer asks for, and the name base it adds the spell id to:
   PUSH 0x0, PUSH 0x20, PUSH 0x42 and ADD EAX,0x1be. */
#define SPELL_BAR_SPRITE 0
#define SPELL_ICON_SPRITE 0x20
#define SPELL_MP_SPRITE 0x42
#define SPELL_NAME_TEXT_BASE 0x1be

/* A .CEL's offset table starts at +0x0f and holds one file-relative dword per
   sprite (resource_info/cel.md). */
#define SPELL_CEL_TABLE_AT 0x0f

/* The fabricated SelBar.cel: one sprite, and a header that declares it 1 by 1
   -- fdps_cel_blit_sprite takes the size out of the SHEET header, so those two
   fields are what bound the blit (sprite.h).  Its stream is a single fill run
   of one pixel. */
#define SPELL_BAR_SHEET_STREAM_AT (SPELL_CEL_TABLE_AT + 4)
#define SPELL_BAR_SHEET_BYTES (SPELL_BAR_SHEET_STREAM_AT + 2)
#define SPELL_BAR_COLOR 0x7f

/* The fabricated Command.cel: 0x43 sprites, one past the highest index the
   drawer asks for.  fdps_blit_command_sprite always draws 25 by 22, so each
   stream is one fill of a single pixel followed by a skip of 24 -- command
   0xd7 is op 11 with a run of (0xd7 & 0x3f) + 1 = 24 -- and then 21 rows of a
   bare 25-pixel skip, command 0xd8.  A sprite's colour is 0x80 plus its index,
   which keeps it clear of the sentinel, of the bar and of the figure
   colours. */
#define SPELL_CMD_SPRITES 0x43
#define SPELL_CMD_ROWS 22
#define SPELL_CMD_FILL_ONE 0x00
#define SPELL_CMD_SKIP_24 0xd7
#define SPELL_CMD_SKIP_25 0xd8
#define SPELL_CMD_STREAM_BYTES (3 + (SPELL_CMD_ROWS - 1))
#define SPELL_CMD_STREAMS_AT (SPELL_CEL_TABLE_AT + SPELL_CMD_SPRITES * 4)
#define SPELL_CMD_SHEET_BYTES \
    (SPELL_CMD_STREAMS_AT + SPELL_CMD_SPRITES * SPELL_CMD_STREAM_BYTES)
#define SPELL_CMD_COLOR_BASE 0x80

/* The fabricated Number.cel.  fdps_draw_number picks sprite
   colour_row * 13 + glyph off the same +0x0f table and draws 6 by 8 (text.c),
   and the colour row is held at 0 here, so thirteen sprites is the whole sheet
   the figure can reach.  Each is one pixel then a skip of 5 (0xc4) and seven
   rows of a bare 6-pixel skip (0xc5).  A sprite's colour is its glyph index
   plus one, so no figure cell can hold zero and the colour reads straight back
   as the digit. */
#define SPELL_NUM_GLYPHS 13
#define SPELL_NUM_ROWS 8
#define SPELL_NUM_SKIP_5 0xc4
#define SPELL_NUM_SKIP_6 0xc5
#define SPELL_NUM_CELL_W 6
#define SPELL_NUM_STREAM_BYTES (3 + (SPELL_NUM_ROWS - 1))
#define SPELL_NUM_STREAMS_AT (SPELL_CEL_TABLE_AT + SPELL_NUM_GLYPHS * 4)
#define SPELL_NUM_SHEET_BYTES \
    (SPELL_NUM_STREAMS_AT + SPELL_NUM_GLYPHS * SPELL_NUM_STREAM_BYTES)
#define SPELL_FIGURE_DIGITS 4
#define SPELL_GLYPH_DIGIT_0 1
#define SPELL_GLYPH_PLUS 11
#define SPELL_GLYPH_MINUS 12

/* The fabricated font: 256 solid glyphs, one byte each, drawn through a 1 by 1
   cell so one glyph is one pixel and the advance is one column. */
#define SPELL_FONT_GLYPHS 256
#define SPELL_FONT_SOLID 0x80
#define SPELL_TEXT_GLYPH 70
#define SPELL_TEXT_FG 0xd0
#define SPELL_TEXT_OUTLINE 0x6d

/* The fabricated global text block: a table of signed 16-bit byte offsets from
   the block's own base, then three token streams of one, two and three glyphs.
   Entry n points at stream 1 + (n % 3), so the run on the page identifies the
   entry. */
#define SPELL_TEXT_ENTRIES (SPELL_NAME_TEXT_BASE + 40)
#define SPELL_TEXT_TABLE_BYTES (SPELL_TEXT_ENTRIES * 2)
#define SPELL_TEXT_STREAM_1_AT SPELL_TEXT_TABLE_BYTES
#define SPELL_TEXT_STREAM_2_AT (SPELL_TEXT_STREAM_1_AT + 4)
#define SPELL_TEXT_STREAM_3_AT (SPELL_TEXT_STREAM_2_AT + 6)
#define SPELL_TEXT_BYTES (SPELL_TEXT_STREAM_3_AT + 8)
#define SPELL_TEXT_END (-1)

/* The unit array and the MAGICDAT.DAT table.  A spell record is seven bytes
   with the MP cost at +5, which is struct fdps_spell_effect's mp_cost, and the
   bitmap in the unit record is five bytes so spell ids run 0..39. */
#define SPELL_UNITS 4
#define SPELL_TABLE_RECORDS 40
#define SPELL_BITMAP_BYTES 5

static unsigned char spell_page[SPELL_PAGE_BYTES];
static unsigned char spell_bar_sheet[SPELL_BAR_SHEET_BYTES];
static unsigned char spell_cmd_sheet[SPELL_CMD_SHEET_BYTES];
static unsigned char spell_num_sheet[SPELL_NUM_SHEET_BYTES];
static unsigned char spell_font[SPELL_FONT_GLYPHS];
static unsigned char spell_text[SPELL_TEXT_BYTES];
static struct fdps_unit_record spell_units[SPELL_UNITS];
static struct fdps_spell_effect spell_table[SPELL_TABLE_RECORDS];

/* The MP cost of every spell id the cases can reach.  The values are chosen so
   that each drawn row prints a different four-digit figure and so that three of
   them separate a zero-extended cost from a sign-extended one: 255 and 128 both
   have bit 7 set and would format as "-001" and "-128" if the byte at +5 were
   read as a signed char, and 0 and 100 pin the zero padding. */
static unsigned char spell_mp_costs[SPELL_TABLE_RECORDS] = {
    9, 33, 44, 7, 9,
    12, 9, 9, 9, 99,
    9, 9, 100, 9, 9,
    9, 9, 9, 9, 9,
    255, 9, 9, 9, 9,
    9, 9, 0, 9, 9,
    9, 5, 9, 9, 9,
    9, 9, 9, 40, 128
};

/* The nine spells the main cases give unit 0.  fdps_unit_collect_known_spells
   walks the bitmap low byte first and low bit first, so the collected order is
   ascending by id (unitstat.h). */
#define SPELL_SET_COUNT 9
static int spell_set_ids[SPELL_SET_COUNT] = { 3, 5, 9, 12, 20, 27, 31, 38, 39 };
static unsigned char spell_set_bitmap[SPELL_BITMAP_BYTES] = {
    0x28, 0x12, 0x10, 0x88, 0xc0
};

/* Twenty spells, ids 0..19, for the case that asks whether the page stops at
   eight rows. */
#define SPELL_WIDE_COUNT 20
static unsigned char spell_wide_bitmap[SPELL_BITMAP_BYTES] = {
    0xff, 0xff, 0x0f, 0x00, 0x00
};

/* Unit 1's own two spells, ids 1 and 2, whose costs 33 and 44 appear nowhere in
   unit 0's list. */
static unsigned char spell_other_bitmap[SPELL_BITMAP_BYTES] = {
    0x06, 0x00, 0x00, 0x00, 0x00
};

static unsigned char spell_at(int row, int column)
{
    return spell_page[row * SPELL_PITCH + column];
}

static void spell_build_sheets(void)
{
    int sprite;
    int row;
    int stream_at;
    int index;

    ((struct fdps_cel_header *) spell_bar_sheet)->sprite_width = 1;
    ((struct fdps_cel_header *) spell_bar_sheet)->sprite_height = 1;
    *(int *) (spell_bar_sheet + SPELL_CEL_TABLE_AT) =
        SPELL_BAR_SHEET_STREAM_AT;
    spell_bar_sheet[SPELL_BAR_SHEET_STREAM_AT] = SPELL_CMD_FILL_ONE;
    spell_bar_sheet[SPELL_BAR_SHEET_STREAM_AT + 1] = SPELL_BAR_COLOR;

    for (sprite = 0; sprite < SPELL_CMD_SPRITES; sprite++) {
        stream_at = SPELL_CMD_STREAMS_AT + sprite * SPELL_CMD_STREAM_BYTES;
        *(int *) (spell_cmd_sheet + SPELL_CEL_TABLE_AT + sprite * 4) =
            stream_at;
        spell_cmd_sheet[stream_at] = SPELL_CMD_FILL_ONE;
        spell_cmd_sheet[stream_at + 1] =
            (unsigned char) (SPELL_CMD_COLOR_BASE + sprite);
        spell_cmd_sheet[stream_at + 2] = SPELL_CMD_SKIP_24;
        for (row = 1; row < SPELL_CMD_ROWS; row++) {
            spell_cmd_sheet[stream_at + 2 + row] = SPELL_CMD_SKIP_25;
        }
    }

    for (sprite = 0; sprite < SPELL_NUM_GLYPHS; sprite++) {
        stream_at = SPELL_NUM_STREAMS_AT + sprite * SPELL_NUM_STREAM_BYTES;
        *(int *) (spell_num_sheet + SPELL_CEL_TABLE_AT + sprite * 4) =
            stream_at;
        spell_num_sheet[stream_at] = SPELL_CMD_FILL_ONE;
        spell_num_sheet[stream_at + 1] =
            (unsigned char) (sprite + SPELL_GLYPH_DIGIT_0);
        spell_num_sheet[stream_at + 2] = SPELL_NUM_SKIP_5;
        for (row = 1; row < SPELL_NUM_ROWS; row++) {
            spell_num_sheet[stream_at + 2 + row] = SPELL_NUM_SKIP_6;
        }
    }

    for (index = 0; index < SPELL_FONT_GLYPHS; index++) {
        spell_font[index] = SPELL_FONT_SOLID;
    }

    for (index = 0; index < SPELL_TEXT_ENTRIES; index++) {
        if (index % 3 == 0) {
            *(short *) (spell_text + index * 2) =
                (short) SPELL_TEXT_STREAM_1_AT;
        } else if (index % 3 == 1) {
            *(short *) (spell_text + index * 2) =
                (short) SPELL_TEXT_STREAM_2_AT;
        } else {
            *(short *) (spell_text + index * 2) =
                (short) SPELL_TEXT_STREAM_3_AT;
        }
    }
    *(short *) (spell_text + SPELL_TEXT_STREAM_1_AT) = SPELL_TEXT_GLYPH;
    *(short *) (spell_text + SPELL_TEXT_STREAM_1_AT + 2) = SPELL_TEXT_END;
    *(short *) (spell_text + SPELL_TEXT_STREAM_2_AT) = SPELL_TEXT_GLYPH;
    *(short *) (spell_text + SPELL_TEXT_STREAM_2_AT + 2) = SPELL_TEXT_GLYPH;
    *(short *) (spell_text + SPELL_TEXT_STREAM_2_AT + 4) = SPELL_TEXT_END;
    *(short *) (spell_text + SPELL_TEXT_STREAM_3_AT) = SPELL_TEXT_GLYPH;
    *(short *) (spell_text + SPELL_TEXT_STREAM_3_AT + 2) = SPELL_TEXT_GLYPH;
    *(short *) (spell_text + SPELL_TEXT_STREAM_3_AT + 4) = SPELL_TEXT_GLYPH;
    *(short *) (spell_text + SPELL_TEXT_STREAM_3_AT + 6) = SPELL_TEXT_END;
}

/* How long entry 0x1be + spell_id is in the fabricated block.  The base is the
   ADD EAX,0x1be at 000277d3 and nothing else; the modulo is the harness's own
   rule for how long an entry is. */
static int spell_name_glyphs(int spell_id)
{
    return 1 + ((SPELL_NAME_TEXT_BASE + spell_id) % 3);
}

/* The page back to its sentinel, the sheets and the tables published, and every
   unit's bitmap cleared. */
static void spell_reset(void)
{
    int index;

    memset(spell_page, SPELL_SENTINEL, (size_t) SPELL_PAGE_BYTES);
    spell_build_sheets();

    for (index = 0; index < SPELL_UNITS; index++) {
        memset(&spell_units[index], 0,
               (size_t) sizeof(struct fdps_unit_record));
    }
    for (index = 0; index < SPELL_TABLE_RECORDS; index++) {
        memset(&spell_table[index], 0,
               (size_t) sizeof(struct fdps_spell_effect));
        spell_table[index].mp_cost = spell_mp_costs[index];
    }

    data_fdps_selection_bar_sheet_ptr = spell_bar_sheet;
    data_fdps_command_sprite_sheet_ptr = spell_cmd_sheet;
    data_fdps_number_glyph_sheet_ptr = spell_num_sheet;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_font_sheet_ptr = spell_font;
    data_fdps_all_game_text_ptr = spell_text;
    data_fdps_map_unit_array_ptr = (unsigned char *) spell_units;
    data_fdps_battle_spell_effect_table_ptr = (unsigned char *) spell_table;

    data_fdps_font_glyph_width = (unsigned char) 1;
    data_fdps_glyph_cell_height = (unsigned char) 1;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = 1;
    data_fdps_font_line_height = 1;
}

static void spell_give(int unit_index, unsigned char *bitmap)
{
    int index;

    for (index = 0; index < SPELL_BITMAP_BYTES; index++) {
        spell_units[unit_index].spells_known_bitmap[index] = bitmap[index];
    }
}

/* Every byte of the page that is not the sentinel. */
static int spell_page_touched(void)
{
    int index;
    int touched;

    touched = 0;
    for (index = 0; index < SPELL_PAGE_BYTES; index++) {
        if (spell_page[index] != SPELL_SENTINEL) {
            touched++;
        }
    }
    return touched;
}

/* The four-digit figure on one row, read back out of the glyph colours: sprite
   colour is glyph index plus one and glyphs 0..9 are the digits, so a colour of
   1 is a '0'.  A '+' or a '-' would come back as 11 or 12 and make the value
   nonsense, which is the point. */
static int spell_figure(int row)
{
    int digit_index;
    int value;
    int color;

    value = 0;
    for (digit_index = 0; digit_index < SPELL_FIGURE_DIGITS; digit_index++) {
        color = (int) spell_at(row * SPELL_ROW_PITCH + SPELL_MP_Y,
                               SPELL_FIGURE_X
                               + digit_index * SPELL_NUM_CELL_W);
        value = value * 10 + (color - SPELL_GLYPH_DIGIT_0);
    }
    return value;
}

/* The length of the foreground run the spell name left on one row. */
static int spell_name_run(int row)
{
    int column;
    int run;

    run = 0;
    for (column = 0; column < 6; column++) {
        if (spell_at(row * SPELL_ROW_PITCH + SPELL_NAME_Y,
                     SPELL_NAME_X + column) != SPELL_TEXT_FG) {
            break;
        }
        run++;
    }
    return run;
}

static int spell_row_has_icon(int row)
{
    return spell_at(row * SPELL_ROW_PITCH + SPELL_ICON_Y, SPELL_ICON_X)
           == (unsigned char) (SPELL_CMD_COLOR_BASE + SPELL_ICON_SPRITE);
}

static int spell_row_has_bar(int row)
{
    return spell_at(row * SPELL_ROW_PITCH + SPELL_BAR_Y, SPELL_BAR_X)
           == (unsigned char) SPELL_BAR_COLOR;
}

/* A unit that knows nothing draws NOTHING, not an empty frame: CMP dword ptr
   [EBP-0x18],0x0 / JZ at 00027713 goes straight to the epilogue.  The second
   half of the case is what makes the first half mean something -- with one
   spell the same call paints. */
static void empty_spell_list_draws_nothing(void)
{
    spell_reset();
    fdps_draw_spell_list_page(0, 0, 0, spell_page, SPELL_PITCH);
    CHECK_EQ(spell_page_touched(), 0);

    spell_reset();
    spell_give(0, spell_set_bitmap);
    fdps_draw_spell_list_page(0, 0, 0, spell_page, SPELL_PITCH);
    CHECK_EQ(spell_page_touched() > 0, 1);
}

/* Where the three sprites land.  The icon is at (row * 0x11 + 9, 3) and the MP
   caption at (row * 0x11 + 0x0d, 0x66), on every drawn row; the whole point of
   the odd pitch is that no other reading of 0x11, 9 and 0x0d puts a byte at
   those addresses. */
static void row_geometry_is_the_row_pitch_and_the_three_offsets(void)
{
    int row;
    int icons;
    int captions;

    spell_reset();
    spell_give(0, spell_set_bitmap);
    fdps_draw_spell_list_page(0, 0, -1, spell_page, SPELL_PITCH);

    icons = 0;
    captions = 0;
    for (row = 0; row < SPELL_ROWS; row++) {
        if (spell_row_has_icon(row)) {
            icons++;
        }
        if (spell_at(row * SPELL_ROW_PITCH + SPELL_MP_Y, SPELL_MP_X)
            == (unsigned char) (SPELL_CMD_COLOR_BASE + SPELL_MP_SPRITE)) {
            captions++;
        }
    }
    CHECK_EQ(icons, SPELL_ROWS);
    CHECK_EQ(captions, SPELL_ROWS);

    /* The two extremes spelled out, so a failure says which end moved. */
    CHECK_EQ(spell_at(SPELL_ICON_Y, SPELL_ICON_X),
             SPELL_CMD_COLOR_BASE + SPELL_ICON_SPRITE);
    CHECK_EQ(spell_at(7 * SPELL_ROW_PITCH + SPELL_ICON_Y, SPELL_ICON_X),
             SPELL_CMD_COLOR_BASE + SPELL_ICON_SPRITE);
    CHECK_EQ(spell_at(7 * SPELL_ROW_PITCH + SPELL_MP_Y, SPELL_MP_X),
             SPELL_CMD_COLOR_BASE + SPELL_MP_SPRITE);
}

/* The MP figure is byte 5 of the spell record, four digits wide, zero padded
   and with no sign: PUSH 0x4 and a zeroed EAX behind it at 00027810, and MOV
   AL,byte ptr [EAX+0x5] / AND EAX,0xff at 00027815 for the value.  Costs 255
   and 128 are the two that separate the zero-extension from a sign-extension --
   read signed they would format as "-001" and "-128" and the figure would not
   come back as a number at all. */
static void mp_figure_is_the_record_byte_zero_extended(void)
{
    int row;

    spell_reset();
    spell_give(0, spell_set_bitmap);
    fdps_draw_spell_list_page(0, 0, -1, spell_page, SPELL_PITCH);

    for (row = 0; row < SPELL_ROWS; row++) {
        CHECK_EQ(spell_figure(row),
                 (int) spell_mp_costs[spell_set_ids[row]]);
    }

    /* The leading cell of a padded figure is the '0' glyph, colour 1, and
       never the '+' at 11 or the '-' at 12.  Row 0's cost is 7 and row 4's is
       255, so between them they rule out both a sign flag that was not zero
       and a value that arrived negative. */
    CHECK_EQ(spell_at(SPELL_MP_Y, SPELL_FIGURE_X), SPELL_GLYPH_DIGIT_0);
    CHECK_EQ(spell_at(4 * SPELL_ROW_PITCH + SPELL_MP_Y, SPELL_FIGURE_X),
             SPELL_GLYPH_DIGIT_0);
}

/* The name is entry 0x1be + spell_id of the global text block, drawn at
   (row * 0x11 + 8, 0x13).  Entry lengths in the fabricated block differ between
   any two adjacent entries, so a base that was one out, or an id taken from the
   row instead of the list, changes the run on every row. */
static void name_entry_is_the_base_plus_the_spell_id(void)
{
    int row;

    spell_reset();
    spell_give(0, spell_set_bitmap);
    fdps_draw_spell_list_page(0, 0, -1, spell_page, SPELL_PITCH);

    for (row = 0; row < SPELL_ROWS; row++) {
        CHECK_EQ(spell_name_run(row),
                 spell_name_glyphs(spell_set_ids[row]));
    }
}

/* The bar is drawn on the row whose LIST INDEX equals cursor_index, and on no
   other, and a cursor that is not on the page draws none at all -- CMP
   EAX,dword ptr [EBP-0xc] / JNZ at 0002774b is an equality against list_top +
   row and not against row. */
static void selection_bar_marks_the_cursor_row_only(void)
{
    int row;
    int bars;

    spell_reset();
    spell_give(0, spell_set_bitmap);
    fdps_draw_spell_list_page(0, 0, 4, spell_page, SPELL_PITCH);

    bars = 0;
    for (row = 0; row < SPELL_ROWS; row++) {
        if (spell_row_has_bar(row)) {
            bars++;
        }
    }
    CHECK_EQ(bars, 1);
    CHECK_EQ(spell_row_has_bar(4), 1);

    spell_reset();
    spell_give(0, spell_set_bitmap);
    fdps_draw_spell_list_page(0, 0, -1, spell_page, SPELL_PITCH);
    bars = 0;
    for (row = 0; row < SPELL_ROWS; row++) {
        if (spell_row_has_bar(row)) {
            bars++;
        }
    }
    CHECK_EQ(bars, 0);
}

/* list_top scrolls the list, so row r shows list entry list_top + r, the ids
   buffer is indexed with that entry and the cursor is matched against it.  With
   list_top 4 and nine spells the page stops after five rows, the first row
   carries spell id 20 -- entry 4 -- and cursor_index 6 puts the bar on row 2. */
static void list_top_scrolls_the_page_and_the_cursor_with_it(void)
{
    int row;
    int drawn;
    int bars;

    spell_reset();
    spell_give(0, spell_set_bitmap);
    fdps_draw_spell_list_page(0, 4, 6, spell_page, SPELL_PITCH);

    drawn = 0;
    bars = 0;
    for (row = 0; row < SPELL_ROWS; row++) {
        if (spell_row_has_icon(row)) {
            drawn++;
        }
        if (spell_row_has_bar(row)) {
            bars++;
        }
    }
    CHECK_EQ(drawn, 5);
    CHECK_EQ(bars, 1);
    CHECK_EQ(spell_row_has_bar(2), 1);
    CHECK_EQ(spell_row_has_icon(4), 1);
    CHECK_EQ(spell_row_has_icon(5), 0);

    /* The id came out of spell_ids[4] and not spell_ids[0]: entry 4 is spell
       20, whose cost is 255, while entry 0 is spell 3 at cost 7. */
    CHECK_EQ(spell_figure(0), (int) spell_mp_costs[spell_set_ids[4]]);
    CHECK_EQ(spell_name_run(0), spell_name_glyphs(spell_set_ids[4]));
    CHECK_EQ(spell_figure(4), (int) spell_mp_costs[spell_set_ids[8]]);
}

/* The page is eight rows whatever the count says -- CMP dword ptr
   [EBP-0x14],0x8 / JL at 00027720 -- and the row that would be ninth is never
   touched. */
static void page_stops_at_eight_rows(void)
{
    int row;
    int drawn;

    spell_reset();
    spell_give(0, spell_wide_bitmap);
    fdps_draw_spell_list_page(0, 0, -1, spell_page, SPELL_PITCH);

    drawn = 0;
    for (row = 0; row < SPELL_ROWS; row++) {
        if (spell_row_has_icon(row)) {
            drawn++;
        }
    }
    CHECK_EQ(drawn, SPELL_ROWS);
    CHECK_EQ(spell_at(SPELL_ROWS * SPELL_ROW_PITCH + SPELL_ICON_Y,
                      SPELL_ICON_X), SPELL_SENTINEL);
    CHECK_EQ(spell_at(SPELL_ROWS * SPELL_ROW_PITCH + SPELL_MP_Y, SPELL_MP_X),
             SPELL_SENTINEL);

    /* The wide set is ids 0..19 in order, so row r carries spell id r: cost 44
       on row 2 and cost 7 on row 3 are two figures no other row prints. */
    CHECK_EQ(spell_figure(2), (int) spell_mp_costs[2]);
    CHECK_EQ(spell_figure(3), (int) spell_mp_costs[3]);
    CHECK_EQ(spell_name_run(5), spell_name_glyphs(5));
}

/* A list shorter than the page leaves the rows past it untouched: the signed
   JGE at 00027742 skips the whole row body rather than drawing an empty one. */
static void rows_past_the_count_are_left_alone(void)
{
    int row;
    int drawn;

    spell_reset();
    spell_give(0, spell_other_bitmap);
    fdps_draw_spell_list_page(0, 0, -1, spell_page, SPELL_PITCH);

    drawn = 0;
    for (row = 0; row < SPELL_ROWS; row++) {
        if (spell_row_has_icon(row)) {
            drawn++;
        }
    }
    CHECK_EQ(drawn, 2);
    CHECK_EQ(spell_at(2 * SPELL_ROW_PITCH + SPELL_ICON_Y, SPELL_ICON_X),
             SPELL_SENTINEL);
    CHECK_EQ(spell_at(2 * SPELL_ROW_PITCH + SPELL_NAME_Y, SPELL_NAME_X),
             SPELL_SENTINEL);
    CHECK_EQ(spell_at(2 * SPELL_ROW_PITCH + SPELL_MP_Y, SPELL_FIGURE_X),
             SPELL_SENTINEL);
}

/* unit_index goes straight through to the collector, so the page belongs to the
   unit that was asked for and not to unit 0.  Unit 1 knows spells 1 and 2,
   costs 33 and 44, neither of which is in unit 0's nine. */
static void the_page_belongs_to_the_unit_that_was_asked_for(void)
{
    spell_reset();
    spell_give(0, spell_set_bitmap);
    spell_give(1, spell_other_bitmap);
    fdps_draw_spell_list_page(1, 0, -1, spell_page, SPELL_PITCH);

    CHECK_EQ(spell_figure(0), (int) spell_mp_costs[1]);
    CHECK_EQ(spell_figure(1), (int) spell_mp_costs[2]);
    CHECK_EQ(spell_row_has_icon(2), 0);
}

/* The destination and the pitch are the caller's: every address is dest plus a
   multiple of the pitch, so drawing into the middle of a surface moves the
   whole page by exactly that offset and nothing is written in front of it. */
static void the_surface_and_the_pitch_are_the_callers(void)
{
    int offset;

    spell_reset();
    spell_give(0, spell_other_bitmap);
    offset = 3 * SPELL_PITCH + 5;
    fdps_draw_spell_list_page(0, 0, 0, spell_page + offset, SPELL_PITCH);

    CHECK_EQ(spell_at(3 + SPELL_ICON_Y, 5 + SPELL_ICON_X),
             SPELL_CMD_COLOR_BASE + SPELL_ICON_SPRITE);
    CHECK_EQ(spell_at(3 + SPELL_BAR_Y, 5 + SPELL_BAR_X), SPELL_BAR_COLOR);
    CHECK_EQ(spell_at(SPELL_ICON_Y, SPELL_ICON_X), SPELL_SENTINEL);
}

/* ------------------------------------------------------------------
 * fdps_spell_list_window_wait_input @ 000278e0
 * ------------------------------------------------------------------
 *
 * The window image the loop is handed: a whole 320x200 frame, the size both
 * shipped callers malloc.  Every position holds a different value from its
 * neighbours in either direction, so a byte written one row or one column out
 * is caught rather than matching anyway.
 */
#define WAIT_PITCH 0x140
#define WAIT_ROWS 0xc8
#define WAIT_BYTES (WAIT_PITCH * WAIT_ROWS)

/* The panel background the loop would blit over the list area, 151 x 149 at
   its own stride.  Staged so the argument is a real buffer, never read on the
   pass these cases reach. */
#define WAIT_PANEL_BYTES (0x97 * 0x95)

static unsigned char wait_window[WAIT_BYTES];
static unsigned char wait_panel[WAIT_PANEL_BYTES];

static int wait_pixel(int row, int col)
{
    return ((row * 31 + col * 17) & 0x7f) | 0x80;
}

static void wait_stage_window(void)
{
    int row;
    int col;

    for (row = 0; row < WAIT_ROWS; row++) {
        for (col = 0; col < WAIT_PITCH; col++) {
            wait_window[row * WAIT_PITCH + col] =
                (unsigned char) wait_pixel(row, col);
        }
    }
    memset(wait_panel, 0x11, (size_t) WAIT_PANEL_BYTES);
}

/* Every byte of the window image that no longer holds the value the staging
   put there.  A drawing pass rewrites four regions of it, so a count above
   zero says a frame was composed. */
static int wait_window_changed(void)
{
    int row;
    int col;
    int changed;

    changed = 0;
    for (row = 0; row < WAIT_ROWS; row++) {
        for (col = 0; col < WAIT_PITCH; col++) {
            if (wait_window[row * WAIT_PITCH + col]
                != (unsigned char) wait_pixel(row, col)) {
                changed++;
            }
        }
    }
    return changed;
}

/* Used entries currently in the heap.  A used entry becomes a free entry the
   moment it is released, possibly merged with a neighbour, so the used ones are
   counted and the free ones are not. */
static int wait_used_heap_blocks(void)
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

/* Scancodes the wait loop is driven with.  0x48 is the up arrow, one of the
   codes fdps_spell_list_select_loop acts on (CMP dword ptr [EBP-0x1c],0x48 at
   000281d7); 0x7f is the last code the loop hands back at all, because CMP
   ...,0x7f / JLE at 00027907 is <=, so 0x7f ends the wait and 0x80 would
   not. */
#define WAIT_SCANCODE_UP 0x48
#define WAIT_SCANCODE_LAST_ACCEPTED 0x7f

/* A tick the timer counter does not hold, so a pass that reaches the tick test
   would find the two different and draw. */
#define WAIT_STALE_TICK 0x5a5a

/* Puts the auto-repeat reader in the state where its very next poll reports
   `code` unchanged: the latch holds the code and the repeat filter remembers a
   different one, which is that reader's "a key went down" path (src/keybd.h).
   0x100 is not a byte, so it differs from every latch value including 0xff. */
static void wait_arm_scancode(unsigned int code)
{
    data_fdps_input_last_scancode = (unsigned char) code;
    data_fdps_input_key_repeat_prev_scancode = 0x100;
    data_fdps_input_key_repeat_counter = 0;
}

/* The unit array and the window image staged together.  spell_reset publishes
   the array the collector walks; the wait loop calls that collector before its
   first poll, so the array has to be real even on a pass that draws nothing. */
static void wait_reset(void)
{
    spell_reset();
    spell_give(0, spell_set_bitmap);
    wait_stage_window();
}

/* Which codes end the wait.  The loop keeps running while the poll answers
   above 0x7f and returns the first answer at or below it, unchanged and without
   sign-extending -- 0x7f is the largest value that gets out, and the test that
   lets it out is signed (JLE), so the code is carried in an int.

   0x48 is an ordinary accepted code, 0x7f is the boundary the JLE puts inside
   the accepted range, and 0 is the other end. */
static void a_code_at_or_below_7f_ends_the_wait(void)
{
    int up;
    int boundary;
    int zero;

    wait_reset();
    wait_arm_scancode(WAIT_SCANCODE_UP);
    up = fdps_spell_list_window_wait_input(wait_window, wait_panel, 0, 0, 0);
    wait_arm_scancode(WAIT_SCANCODE_LAST_ACCEPTED);
    boundary = fdps_spell_list_window_wait_input(wait_window, wait_panel, 0, 0,
                                                 0);
    wait_arm_scancode(0);
    zero = fdps_spell_list_window_wait_input(wait_window, wait_panel, 0, 0, 0);

    CHECK_EQ(up, WAIT_SCANCODE_UP);
    CHECK_EQ(boundary, WAIT_SCANCODE_LAST_ACCEPTED);
    CHECK_EQ(zero, 0);
}

/* The poll is tested before the tick, and a pass that ends the wait draws
   nothing at all.  The last-drawn tick is set here to a value the timer counter
   does not hold, which is exactly the condition a drawing pass needs -- so if
   the tick comparison came first, or if the accepted code were allowed to fall
   through it, this call would compose and present a frame and leave its own
   tick behind.  It does neither: the latch is untouched and every byte of the
   window image still holds what the staging put there, including the four
   positions a frame rewrites.

   All of them are checked because they fail apart.  A frame drawn for the wrong
   reason writes the image; a tick recorded without a frame writes only the
   latch. */
static void an_accepted_code_draws_no_frame(void)
{
    int premise;
    int code;
    int tick_after;

    wait_reset();
    data_fdps_spell_list_window_last_tick = WAIT_STALE_TICK;
    premise = ((int) data_fdps_timer_tick_counter
               != data_fdps_spell_list_window_last_tick);

    wait_arm_scancode(WAIT_SCANCODE_UP);
    code = fdps_spell_list_window_wait_input(wait_window, wait_panel, 0, 0, 0);
    tick_after = data_fdps_spell_list_window_last_tick;

    CHECK_EQ(premise, 1);
    CHECK_EQ(code, WAIT_SCANCODE_UP);
    CHECK_EQ(tick_after, WAIT_STALE_TICK);
    CHECK_EQ(wait_window_changed(), 0);
    /* The list area, the two arrows and the caster's cell, spelled out so a
       failure says which region moved: rows 47, 49, 191 and 10 at columns 152,
       220, 220 and 161. */
    CHECK_EQ((int) wait_window[47 * WAIT_PITCH + 152], wait_pixel(47, 152));
    CHECK_EQ((int) wait_window[49 * WAIT_PITCH + 220], wait_pixel(49, 220));
    CHECK_EQ((int) wait_window[191 * WAIT_PITCH + 220], wait_pixel(191, 220));
    CHECK_EQ((int) wait_window[10 * WAIT_PITCH + 161], wait_pixel(10, 161));
}

/* An accepted code leaves on the FIRST poll.  A second poll would find the
   reader's filter already holding that code and would take its repeat path,
   which advances the hold counter and stamps the timer tick into
   data_fdps_input_key_repeat_last_tick (src/keybd.c).  Neither moves here, and
   the filter holds the code the one poll put in it.

   The repeat tick is seeded away from the timer counter on purpose: that is the
   state in which a second poll WOULD change both of those, so the assertions
   distinguish one poll from two rather than merely from many. */
static void the_accepted_code_costs_exactly_one_poll(void)
{
    int code;

    wait_reset();
    wait_arm_scancode(WAIT_SCANCODE_UP);
    data_fdps_input_key_repeat_last_tick = 0x1234;
    data_fdps_input_key_repeat_counter = 5;

    code = fdps_spell_list_window_wait_input(wait_window, wait_panel, 0, 0, 0);

    CHECK_EQ(code, WAIT_SCANCODE_UP);
    CHECK_EQ((int) data_fdps_input_key_repeat_last_tick, 0x1234);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 0);
    CHECK_EQ((int) data_fdps_input_key_repeat_prev_scancode,
             WAIT_SCANCODE_UP);
}

/* A pass that ends the wait takes no heap.  All three of the loop's allocations
   are inside the drawing branch and all three are freed before that branch
   ends, so a wait that returns on its first poll must leave the heap exactly as
   it found it.  The first call is a warm-up, so anything the CRT allocates once
   is already accounted for. */
static void an_accepted_code_takes_no_heap(void)
{
    int before;
    int after;

    wait_reset();
    wait_arm_scancode(WAIT_SCANCODE_UP);
    fdps_spell_list_window_wait_input(wait_window, wait_panel, 0, 0, 0);

    before = wait_used_heap_blocks();
    wait_arm_scancode(WAIT_SCANCODE_UP);
    fdps_spell_list_window_wait_input(wait_window, wait_panel, 0, 0, 0);
    after = wait_used_heap_blocks();

    CHECK_EQ(after, before);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* ------------------------------------------------------------------
 * fdps_spell_list_select_loop @ 00028130
 * ------------------------------------------------------------------
 *
 * ONLY THE PASS THAT ENDS THE LOOP CAN BE OBSERVED, AND THAT IS STRUCTURAL.
 * The loop's exit is fdps_spell_list_window_wait_input, whose reader
 * fdps_read_scancode_auto_repeat reports the latched byte only when it differs
 * from data_fdps_input_key_repeat_prev_scancode; the reader then records the
 * code it reported, and nothing inside a call can change the latch again,
 * because only the INT 09h handler writes it and no test installs one.  A
 * second wait in the same call therefore polls 0xff for ever, and the frame
 * that would end that spin is never drawn either, because a frame needs the
 * timer tick to move and the tick is written by an interrupt as well.  So a
 * case can drive exactly one make code into one call, and the four branches
 * that repaint and go round again -- the two cursor moves, the silent refusal
 * of a spell the caster cannot pay for, and every unrecognised code -- cannot
 * be reached and returned from at all.  What they do is a playtest contract.
 *
 * TWO CASES BELOW DISTINGUISH BY NOT COMING BACK.  The confirm looks the cost
 * up with ids[*cursor_index]; a body that indexed the collected buffer with
 * the row, with the scroll position or with zero would find a spell the caster
 * cannot afford, and a refusal repaints and waits again, so the failure shows
 * up as the case hanging with the heartbeat frozen on its name rather than as
 * a check that did not hold.
 *
 * Expected values come from the assembly at 00028130: PUSH 0x95 / PUSH 0x97 /
 * PUSH 0x140 / ADD EAX,0x3b58 / PUSH 0x97 at 00028169 for the panel blit, PUSH
 * 0x140 / ADD EAX,0x3b58 / PUSH dword ptr [EAX] at 00028192 for the page, the
 * six CMPs against 0x48, 0x50, 0x1c, 0x39, 0x1 and 0x53, MOV AL,byte ptr
 * [EAX+EBP*1-0x44] / AND EAX,0xff at 00028248 for the id, MOV AL,byte ptr
 * [EDX+0x5] at 00028264 for the cost and MOVSX EAX,word ptr [EAX+0x44] / CMP /
 * JL at 0002826d for the payment test.  The row geometry inside the list area
 * is fdps_draw_spell_list_page's and is read back through the same fabricated
 * artwork the cases above use.
 */

/* The window image the menu is composed into: the 320x200 page at pitch 0x140
   both shipped callers hand in, plus eight guard rows the shipped page does
   not have.  Row 7's MP caption reaches list-area scanline 153, which is
   absolute row 200 -- one past a real page -- and the fabricated Command.cel
   sprite skips every row but its first, so nothing should be stored there; the
   guard is what says so rather than letting a stray byte land in another
   test's fixture. */
#define SEL_PITCH 0x140
#define SEL_ROWS 0xc8
#define SEL_GUARD_ROWS 8
#define SEL_BYTES (SEL_PITCH * (SEL_ROWS + SEL_GUARD_ROWS))
#define SEL_LIST_AT 0x3b58
#define SEL_PANEL_W 0x97
#define SEL_PANEL_H 0x95
#define SEL_PANEL_BYTES (SEL_PANEL_W * SEL_PANEL_H)

/* What an untouched byte of the window reads back as.  It is not a colour any
   fabricated sprite, glyph or panel byte can produce. */
#define SEL_WINDOW_FILL 0x11

/* The make codes the cases drive in.  The two cursor keys, 0x48 and 0x50, are
   not among them: their branches go round again and cannot return. */
#define SEL_KEY_ENTER 0x1c
#define SEL_KEY_SPACE 0x39
#define SEL_KEY_ESC 0x01
#define SEL_KEY_DELETE 0x53

static unsigned char sel_window[SEL_BYTES];
static unsigned char sel_panel[SEL_PANEL_BYTES];

/* The panel's byte at an offset.  Every value is in 0x20..0x5f, which is clear
   of the fill, of the selection bar's 0x7f, of the Command.cel colours from
   0x80 up, of the text foreground 0xd0 and of the figure glyph colours 1..13,
   so a byte says on its own what put it there; and the low bits change with
   the offset, so a row copied at the wrong stride reads back as a different
   byte rather than matching anyway. */
static int sel_panel_pixel(int at)
{
    return 0x20 + (at % 0x40);
}

/* The window painted to its fill, the panel painted to its pattern, the tables
   published with every unit's bitmap cleared, and one make code latched for
   the next poll to report. */
static void sel_stage(unsigned int scancode)
{
    int index;

    spell_reset();
    for (index = 0; index < SEL_BYTES; index++) {
        sel_window[index] = (unsigned char) SEL_WINDOW_FILL;
    }
    for (index = 0; index < SEL_PANEL_BYTES; index++) {
        sel_panel[index] = (unsigned char) sel_panel_pixel(index);
    }
    wait_arm_scancode(scancode);
}

/* A byte of the window read at a position inside the list area. */
static int sel_pixel(int row, int column)
{
    return sel_window[SEL_LIST_AT + row * SEL_PITCH + column];
}

/* The selection bar's byte on one drawn row, and the four-digit MP figure and
   the spell-name run on it -- the same three readings the page cases make,
   taken through the window's 0x140 pitch instead of the narrow page's. */
static int sel_row_bar(int row)
{
    return sel_pixel(row * SPELL_ROW_PITCH + SPELL_BAR_Y, SPELL_BAR_X);
}

static int sel_row_figure(int row)
{
    int digit_index;
    int value;
    int color;

    value = 0;
    for (digit_index = 0; digit_index < SPELL_FIGURE_DIGITS; digit_index++) {
        color = sel_pixel(row * SPELL_ROW_PITCH + SPELL_MP_Y,
                          SPELL_FIGURE_X + digit_index * SPELL_NUM_CELL_W);
        value = value * 10 + (color - SPELL_GLYPH_DIGIT_0);
    }
    return value;
}

/* Bytes of the guard rows -- everything past a real 320x200 page -- that no
   longer hold the fill. */
static int sel_guard_touched(void)
{
    int index;
    int touched;

    touched = 0;
    for (index = SEL_PITCH * SEL_ROWS; index < SEL_BYTES; index++) {
        if (sel_window[index] != (unsigned char) SEL_WINDOW_FILL) {
            touched++;
        }
    }
    return touched;
}

static int sel_row_name_run(int row)
{
    int column;
    int run;

    run = 0;
    for (column = 0; column < 6; column++) {
        if (sel_pixel(row * SPELL_ROW_PITCH + SPELL_NAME_Y,
                      SPELL_NAME_X + column) != SPELL_TEXT_FG) {
            break;
        }
        run++;
    }
    return run;
}

/* Escape answers -1 and leaves both of the caller's indices exactly as they
   were: MOV dword ptr [EBP-0x4],0xffffffff at 0002828d, with nothing written
   through [EBP+0x20] or [EBP+0x24] on that path. */
static void escape_cancels_and_leaves_the_scroll_and_the_cursor_alone(void)
{
    int list_top;
    int cursor_index;

    sel_stage(SEL_KEY_ESC);
    spell_give(0, spell_set_bitmap);
    list_top = 2;
    cursor_index = 5;

    CHECK_EQ(fdps_spell_list_select_loop(0, sel_window, sel_panel, &list_top,
                                         &cursor_index), -1);
    CHECK_EQ(list_top, 2);
    CHECK_EQ(cursor_index, 5);
}

/* Delete reaches the same store: CMP dword ptr [EBP-0x1c],0x53 / JNZ at
   00028287 falls into the branch escape took. */
static void delete_cancels_the_same_way_escape_does(void)
{
    int list_top;
    int cursor_index;

    sel_stage(SEL_KEY_DELETE);
    spell_give(0, spell_set_bitmap);
    list_top = 1;
    cursor_index = 3;

    CHECK_EQ(fdps_spell_list_select_loop(0, sel_window, sel_panel, &list_top,
                                         &cursor_index), -1);
    CHECK_EQ(list_top, 1);
    CHECK_EQ(cursor_index, 3);
}

/* The confirm costs the spell ids[*cursor_index] names, and MP equal to the
   cost pays: the refusal is JL, strictly less, so a caster with 0 MP confirms
   a spell that costs 0.
   THIS CASE DISTINGUISHES BY NOT COMING BACK.  The cursor is on list entry 5,
   spell 27, whose cost is 0; entry 0 costs 7, the scroll position's entry 2
   costs 99, the drawn row's entry 3 costs 100 and their sum's entry 7 costs
   40, so a body that indexed the buffer any other way would refuse and would
   never return.  Neither index is written on the confirm path either. */
static void enter_confirms_the_spell_the_cursor_names(void)
{
    int list_top;
    int cursor_index;

    sel_stage(SEL_KEY_ENTER);
    spell_give(0, spell_set_bitmap);
    spell_units[0].mp_current = (short) 0;
    list_top = 2;
    cursor_index = 5;

    CHECK_EQ(fdps_spell_list_select_loop(0, sel_window, sel_panel, &list_top,
                                         &cursor_index), 1);
    CHECK_EQ(list_top, 2);
    CHECK_EQ(cursor_index, 5);
}

/* Space is the second half of the same test: CMP dword ptr [EBP-0x1c],0x39 /
   JNZ at 0002823f falls into the branch enter took. */
static void space_confirms_the_same_way_enter_does(void)
{
    int list_top;
    int cursor_index;

    sel_stage(SEL_KEY_SPACE);
    spell_give(0, spell_set_bitmap);
    spell_units[0].mp_current = (short) 0;
    list_top = 2;
    cursor_index = 5;

    CHECK_EQ(fdps_spell_list_select_loop(0, sel_window, sel_panel, &list_top,
                                         &cursor_index), 1);
    CHECK_EQ(cursor_index, 5);
}

/* MP above the cost pays as well, and the caster's MP is the one read out of
   its own record: the cursor is on list entry 6, spell 31, which costs 5, and
   the caster is given 6.
   THIS CASE ALSO DISTINGUISHES BY NOT COMING BACK -- entry 0 costs 7, one more
   than the caster holds, so a body that read the buffer from its base would
   refuse and spin. */
static void mp_above_the_cost_pays_too(void)
{
    int list_top;
    int cursor_index;

    sel_stage(SEL_KEY_ENTER);
    spell_give(0, spell_set_bitmap);
    spell_units[0].mp_current = (short) 6;
    list_top = 0;
    cursor_index = 6;

    CHECK_EQ(fdps_spell_list_select_loop(0, sel_window, sel_panel, &list_top,
                                         &cursor_index), 1);
    CHECK_EQ(cursor_index, 6);
}

/* The panel is laid back over the list area before the wait, out of a source
   at its own 0x97 stride into the window at 0x140 from byte 0x3b58.  The
   caster knows nothing here, so the page drawer paints nothing over it and the
   whole rectangle reads back as the panel.  That the rectangle holds panel
   bytes at all on a pass that returned is what pins the order: the wait that
   ended this call drew nothing, so the blit must have come first. */
static void the_panel_is_laid_back_over_the_list_area(void)
{
    int list_top;
    int cursor_index;

    sel_stage(SEL_KEY_ESC);
    list_top = 0;
    cursor_index = 0;

    CHECK_EQ(fdps_spell_list_select_loop(0, sel_window, sel_panel, &list_top,
                                         &cursor_index), -1);

    CHECK_EQ(sel_pixel(0, 0), sel_panel_pixel(0));
    CHECK_EQ(sel_pixel(0, SEL_PANEL_W - 1), sel_panel_pixel(SEL_PANEL_W - 1));
    CHECK_EQ(sel_pixel(1, 0), sel_panel_pixel(SEL_PANEL_W));
    CHECK_EQ(sel_pixel(SEL_PANEL_H - 1, SEL_PANEL_W - 1),
             sel_panel_pixel((SEL_PANEL_H - 1) * SEL_PANEL_W
                             + SEL_PANEL_W - 1));
}

/* Nothing outside the 0x97 by 0x95 rectangle is touched: the byte in front of
   it, the column past its width on the first and last rows, the first row past
   its height, and the guard rows past the end of a real page. */
static void the_panel_blit_stops_at_the_rectangles_edges(void)
{
    int list_top;
    int cursor_index;

    sel_stage(SEL_KEY_ESC);
    list_top = 0;
    cursor_index = 0;

    CHECK_EQ(fdps_spell_list_select_loop(0, sel_window, sel_panel, &list_top,
                                         &cursor_index), -1);

    CHECK_EQ(sel_window[SEL_LIST_AT - 1], SEL_WINDOW_FILL);
    CHECK_EQ(sel_pixel(0, SEL_PANEL_W), SEL_WINDOW_FILL);
    CHECK_EQ(sel_pixel(SEL_PANEL_H - 1, SEL_PANEL_W), SEL_WINDOW_FILL);
    CHECK_EQ(sel_pixel(SEL_PANEL_H, 0), SEL_WINDOW_FILL);
}

/* The page goes down over the restored panel, at the caller's scroll position
   and with the bar on the caller's cursor.  list_top is 1 and the cursor is on
   entry 4, so drawn row 0 is list entry 1 -- spell 5, whose name is
   0x1be + 5 and whose cost is 12 -- row 1 is entry 2, spell 9 costing 99, and
   the bar is on row 3.  A body that passed the two indices the other way round
   would put the bar on row 0 and start the list at entry 4; a body that passed
   the pointers themselves would draw nothing recognisable at all.  Row 0's bar
   position is checked to still hold the panel, and the guard rows past a real
   page to still hold the fill. */
static void the_page_is_drawn_with_the_callers_scroll_and_cursor(void)
{
    int list_top;
    int cursor_index;

    sel_stage(SEL_KEY_ESC);
    spell_give(0, spell_set_bitmap);
    list_top = 1;
    cursor_index = 4;

    CHECK_EQ(fdps_spell_list_select_loop(0, sel_window, sel_panel, &list_top,
                                         &cursor_index), -1);

    CHECK_EQ(sel_row_bar(3), SPELL_BAR_COLOR);
    CHECK_EQ(sel_row_bar(0),
             sel_panel_pixel((0 * SPELL_ROW_PITCH + SPELL_BAR_Y)
                             * SEL_PANEL_W + SPELL_BAR_X));
    CHECK_EQ(sel_row_figure(0), spell_mp_costs[spell_set_ids[1]]);
    CHECK_EQ(sel_row_figure(1), spell_mp_costs[spell_set_ids[2]]);
    CHECK_EQ(sel_row_name_run(0), spell_name_glyphs(spell_set_ids[1]));
    CHECK_EQ(sel_guard_touched(), 0);
}

/* ------------------------------------------------------------------------
 * fdps_battle_spell_command -- the pass the player backs out of
 * ---------------------------------------------------------------------- */

/* WHY ONLY ONE OF ITS PATHS IS DRIVEN.  Every arm past the spell list goes
   through fdps_map_cursor_select_loop, which repaints the whole battle view
   once a pass out of the loaded scene layers, the tile sheets and the map unit
   array, and which cannot return until a timer tick only the game's own INT 08h
   handler moves.  The list's own cancel is the one way out of the retry loop
   that a test binary can reach, and it is also the function's whole -1
   contract: the early return at 00027d71, taken before the first store into
   data_fdps_map_cursor_draw_mode, is what says the unit has not acted.
   Everything past it is settled by comparing the built SPELLMNU.OBJ against
   00027c20 instead.

   The run is real all the way down: Status.cel is decoded out of the shipped
   MISC.VFS, the caster's stat panel is painted through the sheets staged below
   and the portrait read out of the shipped FACE.CEL, the window is animated in
   over a copy of the adapter and out again, and the list's select loop is ended
   by an armed Escape.  Nothing is mocked and no path is shortened.

   THE VILLAGE FLAG IS SET FOR THE RUN.  fdps_close_status_window composes its
   background from the scene layers when the flag is clear (statwin.h), which
   needs a loaded map; with the flag set it copies the staged page instead, and
   the cancel path this file drives is the same code either way.  The flag also
   sends fdps_draw_unit_status_panel through the cache slot the unit INDEX
   names, which is what makes data_fdps_village_status_window_unit_idx a witness
   that the panel ran with the index this command was given. */

/* The caster.  Unit 1 rather than unit 0, so an index that never travelled
   would read a different record and paint a different panel. */
#define CMD_CASTER 1

/* Its HP and MP.  Current equals maximum on purpose: a shortfall sends
   fdps_draw_unit_status_panel through colour row 3 of the number sheet
   (statunit.c), and the fabricated Number.cel above is thirteen sprites -- row
   0 only -- so the reduced row would index off the end of its offset table. */
#define CMD_HP 20
#define CMD_MP 10

/* The fabricated Bar.cel gauge sheet: three graphics of 117 by 8 laid end to
   end, which is the stride fdps_draw_gauge_bar steps by (gauge.h). */
#define CMD_BAR_GRAPHIC_STRIDE 0x3a8
#define CMD_BAR_SHEET_BYTES (3 * CMD_BAR_GRAPHIC_STRIDE)

/* The fabricated sprite cache: eight slots of a twelve-entry offset table
   followed by one 24 by 24 cell each.  A cell's stream is 24 rows of a single
   fill run -- command 0x17 is op 00 with len-1 of 23, so 24 pixels of the
   colour byte behind it (rle.h) -- and the colour is the slot number, so a cell
   drawn from the wrong slot reads back as a different byte. */
#define CMD_CACHE_SLOTS 8
#define CMD_CELL_SIZE 24
#define CMD_CELL_FILL_24 0x17
#define CMD_CELL_COLOR_BASE 0x40
#define CMD_CELL_STREAM_BYTES (CMD_CELL_SIZE * 2)
#define CMD_CACHE_TABLE_BYTES \
    (CMD_CACHE_SLOTS * (int) sizeof(struct fdps_cel_cache_slot))
#define CMD_CACHE_BYTES \
    (CMD_CACHE_TABLE_BYTES + CMD_CACHE_SLOTS * CMD_CELL_STREAM_BYTES)

/* The village page the close puts back, a whole 320x200 frame.  It goes on the
   heap rather than into a static: the test image's headroom is a documented
   hazard (rebuild_info/emit_pipeline.md). */
#define CMD_PAGE_BYTES 0xfa00
#define CMD_PAGE_FILL 0x33

/* The values the globals are seeded with before a run.  None of them is a
   value the cancel path can produce, so each says on its own whether the
   function ran past the early return. */
#define CMD_CURSOR_MODE_SEED 0x5a
#define CMD_XP_SEED 0x1234
#define CMD_TELEPORT_X_SEED 0x11
#define CMD_TELEPORT_Y_SEED 0x22
#define CMD_PANEL_UNIT_SEED (-9)

/* The mode the game draws this window in, and the one the console is put back
   into before anything is reported on it. */
#define CMD_VIDEO_MODE_TEXT 0x03
#define CMD_VIDEO_MODE_320X200X256 0x13

static unsigned char cmd_bar_sheet[CMD_BAR_SHEET_BYTES];
static unsigned char cmd_cache[CMD_CACHE_BYTES];
static struct fdps_vfs_image_header cmd_sfx_pack;
static unsigned char *cmd_page;

static void cmd_set_video_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Both shipped files this path reads by name.  Neither can be stood in for:
   fdps_load_status_cel_image names MISC.VFS and Status.cel as literals, and
   fdps_load_and_draw_portrait ends the process at exit(1) on a FACE.CEL it
   cannot open (statwin.h, msgwin.h). */
static int cmd_inputs_present(void)
{
    FILE *probe;

    probe = fopen("MISC.VFS", "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    probe = fopen("FACE.CEL", "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    return 1;
}

/* Everything one run reads, seeded afresh: the caster, the two sheets the stat
   panel draws through, the empty sound bank the close's sfx lookup misses in,
   the village page, the four seeded globals and one armed Escape. */
static void cmd_stage(void)
{
    int index;
    int slot;
    int row;
    int stream_at;

    spell_reset();
    spell_give(CMD_CASTER, spell_set_bitmap);
    spell_units[CMD_CASTER].hp_current = CMD_HP;
    spell_units[CMD_CASTER].hp_max = CMD_HP;
    spell_units[CMD_CASTER].mp_current = CMD_MP;
    spell_units[CMD_CASTER].mp_max = CMD_MP;

    for (index = 0; index < CMD_BAR_SHEET_BYTES; index++) {
        cmd_bar_sheet[index] = (unsigned char) (index & 0x7f);
    }

    memset(cmd_cache, 0, sizeof cmd_cache);
    for (slot = 0; slot < CMD_CACHE_SLOTS; slot++) {
        stream_at = CMD_CACHE_TABLE_BYTES + slot * CMD_CELL_STREAM_BYTES;
        ((struct fdps_cel_cache_slot *) cmd_cache)[slot].sprite_offset[0] =
            stream_at;
        for (row = 0; row < CMD_CELL_SIZE; row++) {
            cmd_cache[stream_at + row * 2] = (unsigned char) CMD_CELL_FILL_24;
            cmd_cache[stream_at + row * 2 + 1] =
                (unsigned char) (CMD_CELL_COLOR_BASE + slot);
        }
    }

    memset(&cmd_sfx_pack, 0, sizeof cmd_sfx_pack);
    memset(cmd_page, CMD_PAGE_FILL, (size_t) CMD_PAGE_BYTES);

    data_fdps_status_gauge_bar_sheet_ptr = cmd_bar_sheet;
    data_fdps_cel_sprite_cache_ptr = cmd_cache;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = (unsigned char *) &cmd_sfx_pack;
    data_fdps_village_backdrop_page_ptr = cmd_page;
    data_fdps_village_mode_flag = (unsigned char) 1;

    data_fdps_village_status_window_unit_idx = CMD_PANEL_UNIT_SEED;
    data_fdps_map_cursor_draw_mode = CMD_CURSOR_MODE_SEED;
    data_fdps_battle_pending_xp_credit = CMD_XP_SEED;
    data_fdps_battle_teleport_dest_tile_x = CMD_TELEPORT_X_SEED;
    data_fdps_teleport_destination_tile_y = CMD_TELEPORT_Y_SEED;

    wait_arm_scancode(SEL_KEY_ESC);
}

/* Nothing this file published may be left published: several of these globals
   are freed unguarded by the resource loaders, and a static behind one of them
   breaks the allocator for every later case (rebuild_info/emit_pipeline.md).
   The portrait block the panel loaded is the one real allocation a run leaves
   behind. */
static void cmd_unstage(void)
{
    data_fdps_status_gauge_bar_sheet_ptr = NULL;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_village_backdrop_page_ptr = NULL;
    data_fdps_village_mode_flag = (unsigned char) 0;
    data_fdps_selection_bar_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_battle_spell_effect_table_ptr = NULL;
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
}

/* One whole pass, with the adapter in the mode the window is drawn in and the
   console put back before anything is reported. */
static int cmd_run(void)
{
    int answer;

    cmd_stage();
    cmd_set_video_mode(CMD_VIDEO_MODE_320X200X256);
    answer = fdps_battle_spell_command(CMD_CASTER);
    cmd_set_video_mode(CMD_VIDEO_MODE_TEXT);
    return answer;
}

/* Escape on the spell list ends the call at -1 and the unit has not acted.
   The four seeded globals say so from the other side: every one of them is
   written past the early return at 00027d71 -- the cursor mode on the very
   next line at 00027db5, the two teleport tiles at 0002801c and 00028033 and
   the experience at 000280e9 -- so a body that fell through into the aim would
   move at least the first of them. */
static void backing_out_of_the_spell_list_answers_minus_one(void)
{
    int answer;

    CHECK_EQ(cmd_inputs_present(), 1);
    if (!cmd_inputs_present()) {
        return;
    }

    answer = cmd_run();

    CHECK_EQ(answer, -1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CMD_CURSOR_MODE_SEED);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, CMD_XP_SEED);
    CHECK_EQ(data_fdps_battle_teleport_dest_tile_x, CMD_TELEPORT_X_SEED);
    CHECK_EQ(data_fdps_teleport_destination_tile_y, CMD_TELEPORT_Y_SEED);
    cmd_unstage();
}

/* The pass that was cancelled still built the whole window first, and built it
   for the caster this command was given.  The panel's village store is the
   index it was handed (statunit.h), the portrait block is the one allocation
   fdps_load_and_draw_portrait leaves behind, and the reader's filter holds the
   code the select loop's single poll consumed -- three witnesses spread across
   the opening, the middle and the end of the pass, so a call that returned -1
   without doing the work fails here rather than passing. */
static void the_cancelled_pass_opened_the_window_for_the_caster(void)
{
    CHECK_EQ(cmd_inputs_present(), 1);
    if (!cmd_inputs_present()) {
        return;
    }

    cmd_run();

    CHECK_EQ(data_fdps_village_status_window_unit_idx, CMD_CASTER);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr != NULL, 1);
    CHECK_EQ((int) data_fdps_input_key_repeat_prev_scancode, SEL_KEY_ESC);
    cmd_unstage();
}

/* All three blocks come back.  The window image, the 64000-byte copy of the
   adapter and the 151 x 149 panel copy are taken at the top of the pass and
   released after the close, at 00027d4d, 00027d59 and 00027d65, and every
   block the callees take is released by the callee -- so a cancelled pass must
   leave the heap where it found it.  The portrait is the one block that
   outlives a call and is freed and taken again by the next one, which is why
   the measured pass is the second.

   Dropping any one of the three frees shows up here and nowhere else. */
static void a_cancelled_pass_gives_all_three_blocks_back(void)
{
    int before;
    int after;

    CHECK_EQ(cmd_inputs_present(), 1);
    if (!cmd_inputs_present()) {
        return;
    }

    cmd_run();
    before = wait_used_heap_blocks();
    cmd_run();
    after = wait_used_heap_blocks();

    CHECK_EQ(after, before);
    CHECK_EQ(_heapchk(), _HEAPOK);
    cmd_unstage();
}

void run_spellmnu_tests(void)
{
    RUN_TEST(empty_spell_list_draws_nothing);
    RUN_TEST(row_geometry_is_the_row_pitch_and_the_three_offsets);
    RUN_TEST(mp_figure_is_the_record_byte_zero_extended);
    RUN_TEST(name_entry_is_the_base_plus_the_spell_id);
    RUN_TEST(selection_bar_marks_the_cursor_row_only);
    RUN_TEST(list_top_scrolls_the_page_and_the_cursor_with_it);
    RUN_TEST(page_stops_at_eight_rows);
    RUN_TEST(rows_past_the_count_are_left_alone);
    RUN_TEST(the_page_belongs_to_the_unit_that_was_asked_for);
    RUN_TEST(the_surface_and_the_pitch_are_the_callers);

    RUN_TEST(a_code_at_or_below_7f_ends_the_wait);
    RUN_TEST(an_accepted_code_draws_no_frame);
    RUN_TEST(the_accepted_code_costs_exactly_one_poll);
    RUN_TEST(an_accepted_code_takes_no_heap);

    RUN_TEST(escape_cancels_and_leaves_the_scroll_and_the_cursor_alone);
    RUN_TEST(delete_cancels_the_same_way_escape_does);
    RUN_TEST(enter_confirms_the_spell_the_cursor_names);
    RUN_TEST(space_confirms_the_same_way_enter_does);
    RUN_TEST(mp_above_the_cost_pays_too);
    RUN_TEST(the_panel_is_laid_back_over_the_list_area);
    RUN_TEST(the_panel_blit_stops_at_the_rectangles_edges);
    RUN_TEST(the_page_is_drawn_with_the_callers_scroll_and_cursor);

    cmd_page = (unsigned char *) malloc((size_t) CMD_PAGE_BYTES);
    RUN_TEST(backing_out_of_the_spell_list_answers_minus_one);
    RUN_TEST(the_cancelled_pass_opened_the_window_for_the_caster);
    RUN_TEST(a_cancelled_pass_gives_all_three_blocks_back);
    free(cmd_page);
    cmd_page = NULL;
}
