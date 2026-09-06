/* tests/shopdraw.c -- cover for src/shopdraw.c.
 *
 * ---- fdps_shop_draw_item_entry, 00032a70 ----------------------------------
 *
 * Expected values come from the assembly at 00032a70 -- the message base ADD
 * EAX,0xc9 at 00032a90, the three colour pushes at 00032a7c-00032a85, the
 * caption column ADD EAX,0x54 at 00032ac0 and 00032ad2, the price caption's
 * SUB EAX,pitch at 00032acf, the second line's LEA EAX,[EAX*0x8] over the
 * pitch at 00032ab6, the price column ADD EAX,0x64 at 00032af7, the figure
 * bias ADD EAX,0x16 at 00032b42, the field widths PUSH 0x5 at 00032ae1 and
 * PUSH 0x4 at 00032b31, the sprite immediates 0x29 0x40 0x41 0x3e 0x3f 0x43,
 * the zero-extended price MOV AX / AND EAX,0xffff at 00032ae6, the
 * sign-extended stats MOVSX at 00032b36 00032b7c 00032bc3 00032c07, and the
 * two-compare type test CMP byte ptr [EAX],0x0 / JBE at 00032b06 followed by
 * CMP EAX,0x15 / JLE at 00032b15 -- cross-checked against the four callees'
 * documented behaviour in sprite.h, table.h and text.h.  None of them is read
 * off the emitted C.
 *
 * The fixture is the one tests/village.c and tests/text.c use: real
 * .CEL-shaped sheets whose sprite n is a flat fill of a colour that identifies
 * n, and a font whose glyph n paints exactly column n, so the destination
 * surface spells out which sprite each step chose and where it put it.  The
 * four colour ranges are kept apart -- Command.cel sprites paint 0x80 and up,
 * Number.cel glyphs 0x10 and up, the item name 0xd0 with a 0x6d shadow, and
 * the untouched surface 0x77 -- so every pixel says which of the drawing steps
 * last wrote it.
 *
 * Nothing here reads the game's own artwork or its ITEM.DAT: the routine's
 * behaviour is which sprite slot it picks and where it puts it, and the record
 * fields are this file's own inputs.  The one shipped fact leant on is that
 * prices reach 45000, which is what makes the unsigned read observable.
 */
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "shopdraw.h"

/* The surface.  Wide enough for the second line's figure at column 0x6a plus
   five digit cells, and tall enough for the second-line caption's 22 rows; the
   origin is inside it on both axes so a case can say the entry is drawn where
   it is pointed, and it is two rows down because the price caption reaches one
   row ABOVE the entry. */
#define SD_PITCH          176
#define SD_ROWS           48
#define SD_SURFACE_BYTES  (SD_PITCH * SD_ROWS)
#define SD_ORIGIN_ROW     4
#define SD_ORIGIN_COL     6
#define SD_BG             0x77

/* Command.cel's shape: the offset table at a fixed +0x0f, one dword per
   sprite, each the offset from the base of the sheet to that sprite's stream.
   Sixty-eight sprites is enough to hold 0x43, the last one the entry asks for.
   25 by 22 is the size fdps_blit_command_sprite publishes (sprite.h). */
#define SD_CMD_SPRITES    68
#define SD_CMD_W          0x19
#define SD_CMD_H          0x16
#define SD_CEL_TABLE_AT   0x0f
#define SD_ROW_BYTES      2
#define SD_CMD_STREAM_AT  (SD_CEL_TABLE_AT + SD_CMD_SPRITES * 4)
#define SD_CMD_SHEET_BYTES \
    (SD_CMD_STREAM_AT + SD_CMD_SPRITES * SD_CMD_H * SD_ROW_BYTES)

/* Number.cel's: one colour row of the thirteen glyphs '0'-'9', '+', '-', '?',
   each 6 by 8, which is the row fdps_draw_number selects with a colour row of
   zero (text.h). */
#define SD_NUM_SPRITES    13
#define SD_NUM_W          6
#define SD_NUM_H          8
#define SD_NUM_STREAM_AT  (SD_CEL_TABLE_AT + SD_NUM_SPRITES * 4)
#define SD_NUM_SHEET_BYTES \
    (SD_NUM_STREAM_AT + SD_NUM_SPRITES * SD_NUM_H * SD_ROW_BYTES)

/* A single fill op covering a whole row: op 0 in the top two bits and a run of
   (byte & 0x3f) + 1, so the row closes on its own width. */
#define SD_FILL_CMD(width) ((unsigned char) ((width) - 1))

/* The four colour ranges. */
#define SD_CMD_COLOR(sprite) (0x80 + (sprite))
#define SD_NUM_COLOR(glyph)  (0x10 + (glyph))
#define SD_NAME_FG           0xd0
#define SD_NAME_OUTLINE      0x6d

/* The glyph slots fdps_draw_number maps its characters onto (text.c): a digit
   is its own value, '+' is 10, '-' is 11 and '?' is 12. */
#define SD_GLYPH_MINUS    11
#define SD_GLYPH_QUERY    12

/* The font.  One 16-column, one-row cell per glyph, two bytes of it, whose
   glyph n sets exactly the bit for column n -- so a painted column names the
   glyph the routine asked for.  The shadow is put one column right of the body
   so the outline colour the routine forwards is visible beside it. */
#define SD_FONT_GLYPHS    16
#define SD_FONT_CELL_W    16
#define SD_FONT_CELL_H    1
#define SD_FONT_STRIDE    2
#define SD_FONT_ADVANCE   24
#define SD_FONT_LINE_H    2
#define SD_FONT_SHADOW_X  1

/* The text block: a table of signed 16-bit byte offsets from the block's own
   base, one per entry, and the streams they point at.  0xd1 entries covers
   0xc9 plus every item id this file draws. */
#define SD_TEXT_ENTRIES   0xd1
#define SD_TEXT_EMPTY_AT  440
#define SD_TEXT_NAME_AT   444
#define SD_TEXT_NEXT_AT   452
#define SD_TEXT_BYTES     480

/* The geometry the assertions measure against, named again here so a case
   states the assembly's number rather than the header's constant. */
#define SD_NAME_TEXT_BASE 0xc9
#define SD_CAPTION_COL    0x54
#define SD_PRICE_COL      0x64
#define SD_SECOND_ROW     8
#define SD_FIGURE_COL     (SD_CAPTION_COL + 0x16)
#define SD_PRICE_SPRITE   0x29
#define SD_AP_SPRITE      0x40
#define SD_DP_SPRITE      0x41
#define SD_HP_SPRITE      0x3e
#define SD_MP_SPRITE      0x3f
#define SD_PLAIN_SPRITE   0x43

/* The item table.  fdps_get_item_record steps it by a literal 0x17 (table.c),
   which is what struct fdps_item_effect measures, so an array of the struct is
   the table. */
#define SD_ITEM_COUNT     8
#define SD_ITEM_ID        3
#define SD_ITEM_STRIDE    0x17

static unsigned char sd_surface[SD_SURFACE_BYTES];
static unsigned char sd_cmd_sheet[SD_CMD_SHEET_BYTES];
static unsigned char sd_num_sheet[SD_NUM_SHEET_BYTES];
static unsigned char sd_font[SD_FONT_GLYPHS * SD_FONT_STRIDE];
static unsigned char sd_text_block[SD_TEXT_BYTES];
static struct fdps_item_effect sd_items[SD_ITEM_COUNT];

static void sd_build_sheet(unsigned char *sheet, int sprites, int stream_at,
                           int width, int height, int color_base)
{
    struct fdps_cel_header *header;
    int sprite;
    int row;
    int at;

    header = (struct fdps_cel_header *) sheet;
    header->magic[0] = 'C';
    header->magic[1] = 'E';
    header->magic[2] = 'L';
    header->sprite_width = (short) width;
    header->sprite_height = (short) height;
    header->sprite_count = (short) sprites;

    for (sprite = 0; sprite < sprites; sprite++) {
        at = stream_at + sprite * height * SD_ROW_BYTES;
        *(int *) (sheet + SD_CEL_TABLE_AT + sprite * 4) = at;
        for (row = 0; row < height; row++) {
            sheet[at + row * SD_ROW_BYTES] = SD_FILL_CMD(width);
            sheet[at + row * SD_ROW_BYTES + 1] =
                (unsigned char) (color_base + sprite);
        }
    }
}

/* A stream token and a table entry are both signed 16-bit words written at a
   byte position, which is the whole of the block format (text.h). */
static void sd_put_word(unsigned char *block, int byte_at, int value)
{
    *(short *) (block + byte_at) = (short) value;
}

/* Points text entry `text_index` at a stream holding one glyph and the end
   marker, so drawing that entry paints exactly column `glyph`. */
static void sd_name_entry(int text_index, int stream_at, int glyph)
{
    sd_put_word(sd_text_block, text_index * 2, stream_at);
    sd_put_word(sd_text_block, stream_at, glyph);
    sd_put_word(sd_text_block, stream_at + 2, -1);
}

/* Both sheets, the font, an all-empty text block, a cleared item table and a
   background-filled surface. */
static void sd_stage(void)
{
    int i;

    memset(sd_surface, SD_BG, (size_t) SD_SURFACE_BYTES);
    memset(sd_cmd_sheet, 0, (size_t) SD_CMD_SHEET_BYTES);
    memset(sd_num_sheet, 0, (size_t) SD_NUM_SHEET_BYTES);
    memset(sd_font, 0, sizeof(sd_font));
    memset(sd_text_block, 0, sizeof(sd_text_block));
    memset(sd_items, 0, sizeof(sd_items));

    sd_build_sheet(sd_cmd_sheet, SD_CMD_SPRITES, SD_CMD_STREAM_AT, SD_CMD_W,
                   SD_CMD_H, 0x80);
    sd_build_sheet(sd_num_sheet, SD_NUM_SPRITES, SD_NUM_STREAM_AT, SD_NUM_W,
                   SD_NUM_H, 0x10);

    for (i = 0; i < SD_FONT_GLYPHS; i++) {
        if (i < 8) {
            sd_font[i * SD_FONT_STRIDE] = (unsigned char) (0x80 >> i);
        } else {
            sd_font[i * SD_FONT_STRIDE + 1] =
                (unsigned char) (0x80 >> (i - 8));
        }
    }

    for (i = 0; i < SD_TEXT_ENTRIES; i++) {
        sd_put_word(sd_text_block, i * 2, SD_TEXT_EMPTY_AT);
    }
    sd_put_word(sd_text_block, SD_TEXT_EMPTY_AT, -1);

    data_fdps_command_sprite_sheet_ptr = sd_cmd_sheet;
    data_fdps_number_glyph_sheet_ptr = sd_num_sheet;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_font_sheet_ptr = sd_font;
    data_fdps_font_glyph_width = (unsigned char) SD_FONT_CELL_W;
    data_fdps_glyph_cell_height = (unsigned char) SD_FONT_CELL_H;
    data_fdps_font_glyph_stride_bytes = SD_FONT_STRIDE;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = SD_FONT_SHADOW_X;
    data_fdps_glyph_advance_x = SD_FONT_ADVANCE;
    data_fdps_font_line_height = SD_FONT_LINE_H;
    data_fdps_all_game_text_ptr = sd_text_block;
    data_fdps_item_effect_table_ptr = (unsigned char *) sd_items;
}

/* Put every pointer back the way a freshly started program has it: they hold
   blocks the game's own loaders free, and leaving one pointing at a static
   here hands a later test a free of storage that never came from the heap. */
static void sd_unstage(void)
{
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
}

static void sd_draw(int item_id)
{
    fdps_shop_draw_item_entry(item_id,
                              sd_surface + SD_ORIGIN_ROW * SD_PITCH
                                  + SD_ORIGIN_COL,
                              SD_PITCH);
}

/* A pixel of the surface, addressed from the entry's own top-left corner. */
static int sd_pixel(int row, int col)
{
    return (int) sd_surface[(SD_ORIGIN_ROW + row) * SD_PITCH + SD_ORIGIN_COL
                            + col];
}

/* The colour standing in the top-left pixel of digit cell `cell` of the price
   figure, and of the second line's figure. */
static int sd_price_cell(int cell)
{
    return sd_pixel(0, SD_PRICE_COL + cell * SD_NUM_W);
}

static int sd_stat_cell(int cell)
{
    return sd_pixel(SD_SECOND_ROW, SD_FIGURE_COL + cell * SD_NUM_W);
}

/* The name's message id is the item id plus 0xc9, so entry 0xcc is what item 3
   draws and neither of its neighbours is touched.  The body lands in the
   foreground colour and the drop shadow beside it in the outline colour, which
   is the two colour arguments 0xd0 and 0x6d arriving in the right places; the
   background argument is zero, so the glyph cell around them is not filled and
   the surface keeps its own colour there. */
static void shopdraw_name_is_entry_c9_plus_the_item_id(void)
{
    sd_stage();
    sd_name_entry(SD_NAME_TEXT_BASE + SD_ITEM_ID, SD_TEXT_NAME_AT, 5);
    sd_name_entry(SD_NAME_TEXT_BASE + SD_ITEM_ID + 1, SD_TEXT_NEXT_AT, 9);
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(0, 5), SD_NAME_FG);
    CHECK_EQ(sd_pixel(0, 6), SD_NAME_OUTLINE);
    CHECK_EQ(sd_pixel(0, 4), SD_BG);
    CHECK_EQ(sd_pixel(0, 9), SD_BG);
    CHECK_EQ(sd_pixel(0, 10), SD_BG);
    sd_unstage();
}

/* dest is a parameter and nothing in the body is an absolute address: the name
   begins exactly at the pointer the routine was handed and nothing is written
   in front of it -- except the price caption, which is deliberately one row
   above dest and is the only thing the entry puts there. */
static void shopdraw_entry_is_drawn_where_it_is_pointed(void)
{
    sd_stage();
    sd_name_entry(SD_NAME_TEXT_BASE + SD_ITEM_ID, SD_TEXT_NAME_AT, 0);
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(0, 0), SD_NAME_FG);
    CHECK_EQ(sd_pixel(0, -1), SD_BG);
    CHECK_EQ(sd_pixel(-1, 0), SD_BG);
    CHECK_EQ((int) sd_surface[0], SD_BG);
    CHECK_EQ(sd_pixel(-1, SD_CAPTION_COL), SD_CMD_COLOR(SD_PRICE_SPRITE));
    CHECK_EQ(sd_pixel(-2, SD_CAPTION_COL), SD_BG);
    sd_unstage();
}

/* The money mark is Command.cel sprite 0x29 at column 0x54, one row above the
   entry, and it is 25 by 22 like every sprite of that sheet. */
static void shopdraw_price_caption_is_sprite_29_one_row_up(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 0x28;
    sd_items[SD_ITEM_ID].use_effect = 0x20;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(-1, SD_CAPTION_COL), SD_CMD_COLOR(SD_PRICE_SPRITE));
    CHECK_EQ(sd_pixel(-1, SD_CAPTION_COL - 1), SD_BG);
    CHECK_EQ(sd_pixel(-2, SD_CAPTION_COL), SD_BG);
    CHECK_EQ(sd_pixel(-1, SD_CAPTION_COL + SD_CMD_W - 1),
             SD_CMD_COLOR(SD_PRICE_SPRITE));
    CHECK_EQ(sd_pixel(-1, SD_CAPTION_COL + SD_CMD_W), SD_BG);
    CHECK_EQ(sd_pixel(7, SD_CAPTION_COL), SD_CMD_COLOR(SD_PRICE_SPRITE));
    sd_unstage();
}

/* PUSH 0x5 makes the price field five wide and the padding is drawn as real
   zero glyphs rather than skipped.  The figure's own column is 0x64, ten to
   the right of the caption's, and it goes down after the caption, so the cell
   in front of the first digit is still caption. */
static void shopdraw_price_is_five_zero_padded_digits_at_0x64(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].price = 7;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_price_cell(0), SD_NUM_COLOR(0));
    CHECK_EQ(sd_price_cell(1), SD_NUM_COLOR(0));
    CHECK_EQ(sd_price_cell(2), SD_NUM_COLOR(0));
    CHECK_EQ(sd_price_cell(3), SD_NUM_COLOR(0));
    CHECK_EQ(sd_price_cell(4), SD_NUM_COLOR(7));
    CHECK_EQ(sd_pixel(0, SD_PRICE_COL - 1), SD_CMD_COLOR(SD_PRICE_SPRITE));
    CHECK_EQ(sd_price_cell(5), SD_BG);
    sd_unstage();
}

/* MOV AX,word ptr [EAX+0x13] / AND EAX,0xffff: the price is zero-extended.
   Shipped ITEM.DAT prices reach 45000, which a signed 16-bit read turns into
   -20536 and draws as a minus sign followed by five digits of nonsense; read
   unsigned it is the five digits 4 5 0 0 0 and fits the field exactly. */
static void shopdraw_price_is_read_unsigned(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].price = 45000;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_price_cell(0), SD_NUM_COLOR(4));
    CHECK_EQ(sd_price_cell(1), SD_NUM_COLOR(5));
    CHECK_EQ(sd_price_cell(2), SD_NUM_COLOR(0));
    CHECK_EQ(sd_price_cell(3), SD_NUM_COLOR(0));
    CHECK_EQ(sd_price_cell(4), SD_NUM_COLOR(0));
    sd_unstage();
}

/* A weapon type takes caption 0x40 and the record's attack power.  The second
   line is eight rows below the entry -- row 7 is still the price caption --
   and its figure is 0x16 to the right of its caption, on top of it. */
static void shopdraw_weapon_draws_the_ap_caption_and_the_ap(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 1;
    sd_items[SD_ITEM_ID].ap = 55;
    sd_items[SD_ITEM_ID].dp = 4321;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_AP_SPRITE));
    CHECK_EQ(sd_pixel(SD_SECOND_ROW - 1, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_PRICE_SPRITE));
    CHECK_EQ(sd_stat_cell(0), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(1), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(2), SD_NUM_COLOR(5));
    CHECK_EQ(sd_stat_cell(3), SD_NUM_COLOR(5));
    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_FIGURE_COL - 1),
             SD_CMD_COLOR(SD_AP_SPRITE));
    sd_unstage();
}

/* MOVSX word ptr [EAX+0x1]: the attack power is sign-extended, so a negative
   one draws its minus glyph.  Read unsigned it would be 65533, which is past
   the four-digit field's overflow limit and would come out as four '?'
   glyphs -- so the first cell tells the two readings apart on its own.  0x15
   is the last type that reaches this branch. */
static void shopdraw_weapon_ap_is_sign_extended(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 0x15;
    sd_items[SD_ITEM_ID].ap = -3;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_AP_SPRITE));
    CHECK_EQ(sd_stat_cell(0), SD_NUM_COLOR(SD_GLYPH_MINUS));
    CHECK_EQ(sd_stat_cell(1), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(2), SD_NUM_COLOR(0));
    sd_unstage();
}

/* CMP byte ptr [EAX],0x0 / JBE at 00032b06 is tested before the <= 0x15
   compare, so type 0 misses the weapon branch and falls into the armour one:
   caption 0x41 and the defence power, not the attack power.  The record's
   attack power is 4321 here, so a plain type <= 0x15 test would spell 4 3 2 1
   across the four cells instead. */
static void shopdraw_type_zero_takes_the_armour_branch(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 0;
    sd_items[SD_ITEM_ID].ap = 4321;
    sd_items[SD_ITEM_ID].dp = 12;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_DP_SPRITE));
    CHECK_EQ(sd_stat_cell(0), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(1), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(2), SD_NUM_COLOR(1));
    CHECK_EQ(sd_stat_cell(3), SD_NUM_COLOR(2));
    sd_unstage();
}

/* The armour band is 0x16 through 0x27 and its figure is the sign-extended
   defence power at +5. */
static void shopdraw_armour_band_draws_the_dp_caption(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 0x16;
    sd_items[SD_ITEM_ID].dp = -5;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_DP_SPRITE));
    CHECK_EQ(sd_stat_cell(0), SD_NUM_COLOR(SD_GLYPH_MINUS));
    CHECK_EQ(sd_stat_cell(1), SD_NUM_COLOR(0));

    sd_stage();
    sd_items[SD_ITEM_ID].type = 0x27;
    sd_items[SD_ITEM_ID].dp = 9;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_DP_SPRITE));
    CHECK_EQ(sd_stat_cell(3), SD_NUM_COLOR(9));
    sd_unstage();
}

/* Past the armour band the second line comes off use_effect instead: 0x0b is
   HP recovery, caption 0x3e, and the figure is use_amount at +0xe and not the
   defence power the record still carries. */
static void shopdraw_hp_recovery_draws_the_hp_caption_and_amount(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 0x28;
    sd_items[SD_ITEM_ID].dp = 7777;
    sd_items[SD_ITEM_ID].use_effect = 0x0b;
    sd_items[SD_ITEM_ID].use_amount = 30;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_HP_SPRITE));
    CHECK_EQ(sd_stat_cell(0), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(1), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(2), SD_NUM_COLOR(3));
    CHECK_EQ(sd_stat_cell(3), SD_NUM_COLOR(0));
    sd_unstage();
}

/* 0x0c is MP recovery, caption 0x3f, and it reads the same use_amount.  The
   type byte here is 0xff, which is past both bands and is what an unsigned
   read of the type has to produce for this branch to be reachable at all. */
static void shopdraw_mp_recovery_draws_the_mp_caption(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 0xff;
    sd_items[SD_ITEM_ID].use_effect = 0x0c;
    sd_items[SD_ITEM_ID].use_amount = 8;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_MP_SPRITE));
    CHECK_EQ(sd_stat_cell(0), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(1), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(2), SD_NUM_COLOR(0));
    CHECK_EQ(sd_stat_cell(3), SD_NUM_COLOR(8));
    sd_unstage();
}

/* Any other use_effect draws caption 0x43 ALONE: the fifth arm has no
   fdps_draw_number call at all, so the figure's cells still hold whatever the
   caption put there -- the first is inside the 25-pixel caption and the second
   is past its right edge and never written.  The price line is drawn on this
   arm like any other. */
static void shopdraw_other_effect_draws_the_plain_caption_alone(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 0x28;
    sd_items[SD_ITEM_ID].use_effect = 0x20;
    sd_items[SD_ITEM_ID].use_amount = 1234;
    sd_items[SD_ITEM_ID].price = 250;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_PLAIN_SPRITE));
    CHECK_EQ(sd_stat_cell(0), SD_CMD_COLOR(SD_PLAIN_SPRITE));
    CHECK_EQ(sd_stat_cell(1), SD_BG);
    CHECK_EQ(sd_price_cell(2), SD_NUM_COLOR(2));
    CHECK_EQ(sd_price_cell(4), SD_NUM_COLOR(0));
    sd_unstage();
}

/* The id the routine hands the record accessor is its own item_id argument, so
   the record drawn is the one 0x17 bytes per id along the table and not its
   neighbour: id 2 is a weapon here and id 3 an armour, and drawing 3 must
   produce the defence caption. */
static void shopdraw_record_comes_from_the_item_id(void)
{
    sd_stage();
    sd_items[2].type = 1;
    sd_items[2].ap = 4444;
    sd_items[3].type = 0x20;
    sd_items[3].dp = 2222;
    sd_draw(3);

    CHECK_EQ((int) sizeof(struct fdps_item_effect), SD_ITEM_STRIDE);
    CHECK_EQ(sd_pixel(SD_SECOND_ROW, SD_CAPTION_COL),
             SD_CMD_COLOR(SD_DP_SPRITE));
    CHECK_EQ(sd_stat_cell(0), SD_NUM_COLOR(2));
    CHECK_EQ(sd_stat_cell(1), SD_NUM_COLOR(2));
    CHECK_EQ(sd_stat_cell(2), SD_NUM_COLOR(2));
    CHECK_EQ(sd_stat_cell(3), SD_NUM_COLOR(2));
    sd_unstage();
}

/* A four-digit field cannot hold 10000 and up: fdps_draw_number replaces the
   whole figure with '?' glyphs rather than truncating it (text.h), which is
   what a headline stat past the field is drawn as. */
static void shopdraw_overlong_stat_becomes_question_marks(void)
{
    sd_stage();
    sd_items[SD_ITEM_ID].type = 2;
    sd_items[SD_ITEM_ID].ap = 20000;
    sd_draw(SD_ITEM_ID);

    CHECK_EQ(sd_stat_cell(0), SD_NUM_COLOR(SD_GLYPH_QUERY));
    CHECK_EQ(sd_stat_cell(1), SD_NUM_COLOR(SD_GLYPH_QUERY));
    CHECK_EQ(sd_stat_cell(2), SD_NUM_COLOR(SD_GLYPH_QUERY));
    CHECK_EQ(sd_stat_cell(3), SD_NUM_COLOR(SD_GLYPH_QUERY));
    sd_unstage();
}

void run_shopdraw_tests(void)
{
    RUN_TEST(shopdraw_name_is_entry_c9_plus_the_item_id);
    RUN_TEST(shopdraw_entry_is_drawn_where_it_is_pointed);
    RUN_TEST(shopdraw_price_caption_is_sprite_29_one_row_up);
    RUN_TEST(shopdraw_price_is_five_zero_padded_digits_at_0x64);
    RUN_TEST(shopdraw_price_is_read_unsigned);
    RUN_TEST(shopdraw_weapon_draws_the_ap_caption_and_the_ap);
    RUN_TEST(shopdraw_weapon_ap_is_sign_extended);
    RUN_TEST(shopdraw_type_zero_takes_the_armour_branch);
    RUN_TEST(shopdraw_armour_band_draws_the_dp_caption);
    RUN_TEST(shopdraw_hp_recovery_draws_the_hp_caption_and_amount);
    RUN_TEST(shopdraw_mp_recovery_draws_the_mp_caption);
    RUN_TEST(shopdraw_other_effect_draws_the_plain_caption_alone);
    RUN_TEST(shopdraw_record_comes_from_the_item_id);
    RUN_TEST(shopdraw_overlong_stat_becomes_question_marks);
}
