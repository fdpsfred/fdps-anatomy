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
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "palcycle.h"
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

/* ---- fdps_shop_render_buy_target_frame, 000330e0 --------------------------
 *
 * HOW THE FRAME IS WATCHED.  The routine composes on a page it allocates and
 * frees itself and copies the visible part of it straight to 0xa9c48, so the
 * adapter is the only place its output can be read back from.  Each case
 * therefore sets mode 13h, fills the screen with a sentinel, calls, snapshots
 * the rows the window can reach and returns to text mode -- the same way
 * tests/save.c watches the save screen and tests/menu.c the ring menu.  The
 * page is rebuilt from nothing on every call, so the screen at the end is
 * exactly this one frame and nothing accumulates.
 *
 * WHY NO TIMER INTERRUPT IS INSTALLED.  Nothing in the routine waits for
 * data_fdps_timer_tick_counter to change -- the only thing it does with the
 * counter is divide it -- so leaving the counter still is what makes the blink
 * phase the case's own input.  The two retrace spins need no help either: the
 * adapter drives bit 3 of 0x3da on its own.
 *
 * WHY THE PALETTE CYCLE IS PARKED.  data_fdps_ui_palette_last_cycle_tick is
 * seeded equal to the tick, which is the state fdps_cycle_ui_palette returns
 * from having changed nothing (palcycle.h).  The DAC is not what these cases
 * are about and a case that moved it would be reading a different picture back
 * on the next one.
 *
 * THE FIXTURES ARE THREE SHEETS AND ONE GRID, ALL COLOUR-CODED.  Every sprite
 * of every sheet is a flat fill of a colour that names the sheet and the sprite
 * index, so a pixel of the snapshot says which of the four drawing steps last
 * wrote it: the window sheet paints 0x11, the selection-bar sheet 0x20 plus the
 * sprite, the Command.cel arrows 0x80 plus the sprite, the entry grid's markers
 * 0x30 plus the low six bits of the grid row, and an untouched screen keeps
 * 0xee.  The grid is zero everywhere else, which is the transparency key, so
 * everything the strip does not carry shows the window and the bar underneath.
 *
 * THE ARROW SHEET IS SHAPED LIKE THE REAL ONE ON PURPOSE.  Command.cel's cells
 * are 25 by 22 but its four arrow sprites are 7 by 5 triangles in the top-left
 * corner and every row below the fifth is a single skip run, which is what
 * keeps the down arrow -- placed at page row 64 of 76 -- from writing past the
 * page.  A stand-in that filled all 22 rows would overrun the routine's own
 * malloc block and corrupt the heap, so the fixture reproduces the sheet's
 * skip runs rather than the cell's nominal height.
 *
 * Expected values come from the assembly at 000330e0 -- PUSH 0x5ca0 at
 * 000330ec, the two fdps_cel_blit_sprite argument sets at 000330fc..0003310f
 * and 0003311d..00033132, the strip's IMUL EAX,[EBP+0x1c],0x138 / ADD
 * EAX,[EBP+0x14] at 00033155, MOV EBX,0x5 / XOR EDX,EDX / DIV EBX / AND EAX,0x1
 * at 00033168..00033176, CMP [EBP+0x20],0x0 / JZ at 0003317c, ADD EAX,0x3 / CMP
 * EAX,[0x00064114] / JGE at 0003319f..000331ab, the arrow offsets 0x6ae and
 * 0x4e96 with their 0x44 and 0x46 sprite bases, and the final PUSH set 0x43,
 * 0x130, 0x140, 0xa9c48, 0x138, page + 0x3ad at 000331f1..0003320f -- and from
 * the callees' documented behaviour in blit.h, sprite.h and palcycle.h.  None
 * of them is read off the emitted C.
 */

/* The page and the screen.  The copy takes page (row 3, column 5) to screen
   (8, 125), so a page pixel is at screen row page_row + 122 and screen column
   page_col + 3, which is what every assertion below addresses through. */
#define BT_PAGE_PITCH        0x138
#define BT_PAGE_ROWS         76
#define BT_SCREEN_PITCH      320
#define BT_SCREEN_ROWS       200
#define BT_SCREEN_BYTES      (BT_SCREEN_PITCH * BT_SCREEN_ROWS)
#define BT_VGA_BASE          0x000a0000
#define BT_MODE_TEXT         0x03
#define BT_MODE_320X200X256  0x13
#define BT_SENTINEL          0xee
#define BT_WINDOW_SCREEN_ROW 125
#define BT_WINDOW_SCREEN_COL 8
#define BT_WINDOW_W          0x130
#define BT_WINDOW_H          0x43
#define BT_PAGE_TO_SCREEN_ROW (BT_WINDOW_SCREEN_ROW - 3)
#define BT_PAGE_TO_SCREEN_COL (BT_WINDOW_SCREEN_COL - 5)

/* The band of screen rows the snapshot keeps: the window's 67 plus one row
   above and one below, so a case can say the copy stopped where it did. */
#define BT_SNAP_FIRST_ROW    (BT_WINDOW_SCREEN_ROW - 1)
#define BT_SNAP_ROWS         (BT_WINDOW_H + 2)
#define BT_SNAP_BYTES        (BT_SNAP_ROWS * BT_SCREEN_PITCH)

/* A stream command byte: the top two bits pick the op and the low six carry
   len-1, so a run is 1 to 64.  Op 0 fills with the colour byte that follows and
   op 3 skips, storing nothing (rle.h). */
#define BT_MAX_RUN           64
#define BT_SKIP_OP           0xc0
#define BT_CEL_TABLE_AT      0x0f

/* ShopWin.Cel's stand-in: one sprite the size of the whole page.  312 needs
   five fill runs, so ten bytes a row. */
#define BT_WIN_SPRITES       1
#define BT_WIN_W             BT_PAGE_PITCH
#define BT_WIN_H             BT_PAGE_ROWS
#define BT_WIN_ROW_STRIDE    10
#define BT_WIN_STREAM_AT     (BT_CEL_TABLE_AT + BT_WIN_SPRITES * 4)
#define BT_WIN_SHEET_BYTES \
    (BT_WIN_STREAM_AT + BT_WIN_SPRITES * BT_WIN_H * BT_WIN_ROW_STRIDE)
#define BT_WIN_COLOR_BASE    0x11

/* SelBar.cel's stand-in: three sprites of 101 by 17, which is the bar's real
   size, 101 being the 0x65 column stride of the grid.  Two fill runs a row. */
#define BT_BAR_SPRITES       3
#define BT_BAR_W             101
#define BT_BAR_H             17
#define BT_BAR_ROW_STRIDE    4
#define BT_BAR_STREAM_AT     (BT_CEL_TABLE_AT + BT_BAR_SPRITES * 4)
#define BT_BAR_SHEET_BYTES \
    (BT_BAR_STREAM_AT + BT_BAR_SPRITES * BT_BAR_H * BT_BAR_ROW_STRIDE)
#define BT_BAR_COLOR_BASE    0x20
#define BT_BAR_SPRITE        2
#define BT_BAR_PAGE_ROW      0x14

/* Command.cel's stand-in: 0x48 sprites of 25 by 22, of which only the top five
   rows store anything, and those only their leftmost seven pixels.

   A ROW OCCUPIES EXACTLY THE BYTES ITS OWN COMMANDS TAKE.  The decoder walks
   one continuous stream and starts the next row at the byte after the one that
   closed the last (rle.h), so a fixed per-row stride with slack in it is read
   as more commands: a padding zero is a one-pixel fill and the row after it
   never closes on the width, which sends the decoder off through the sheet
   writing pixels past the destination.  Three bytes carry a triangle row and
   one carries a skipped row, so a sprite is 5 * 3 + 17 bytes. */
#define BT_CMD_SPRITES       0x48
#define BT_CMD_W             25
#define BT_CMD_H             22
#define BT_CMD_MARKED_BYTES  3
#define BT_CMD_SKIPPED_BYTES 1
#define BT_CMD_SPRITE_BYTES \
    (BT_ARROW_ROWS * BT_CMD_MARKED_BYTES \
     + (BT_CMD_H - BT_ARROW_ROWS) * BT_CMD_SKIPPED_BYTES)
#define BT_CMD_STREAM_AT     (BT_CEL_TABLE_AT + BT_CMD_SPRITES * 4)
#define BT_CMD_SHEET_BYTES \
    (BT_CMD_STREAM_AT + BT_CMD_SPRITES * BT_CMD_SPRITE_BYTES)
#define BT_CMD_COLOR_BASE    0x80
#define BT_ARROW_W           7
#define BT_ARROW_ROWS        5

/* Where each arrow lands: page offset 0x6ae is page (5, 150) and 0x4e96 is page
   (64, 150), both of which reach the screen. */
#define BT_ARROW_UP_ROW      5
#define BT_ARROW_DOWN_ROW    64
#define BT_ARROW_COL         150
#define BT_ARROW_UP_SPRITE   0x44
#define BT_ARROW_DOWN_SPRITE 0x46
#define BT_BLINK_TICKS       5

/* The entry grid.  Four rows of entries is more than any case reads and keeps
   src_row 0x43 in range with a whole strip behind it.  Its markers sit in three
   columns: 5 and 308 are the first and last the copy carries to the screen, and
   10 is an ordinary one in between. */
#define BT_STRIP_H           0x43
#define BT_LIST_ROWS         (BT_STRIP_H * 4)
#define BT_LIST_BYTES        (BT_LIST_ROWS * BT_PAGE_PITCH)
#define BT_LIST_MARK_FIRST   5
#define BT_LIST_MARK_MID     10
#define BT_LIST_MARK_LAST    308
#define BT_LIST_MARK_BASE    0x30

/* The three cursor columns fdps_shop_select_buy_target can pass, (index % 3) *
   0x65 + 5. */
#define BT_CURSOR_X_LEFT     5
#define BT_CURSOR_X_MIDDLE   0x6a
#define BT_CURSOR_X_RIGHT    0xcf

static unsigned char bt_win_sheet[BT_WIN_SHEET_BYTES];
static unsigned char bt_bar_sheet[BT_BAR_SHEET_BYTES];
static unsigned char bt_cmd_sheet[BT_CMD_SHEET_BYTES];
static unsigned char bt_snapshot[BT_SNAP_BYTES];
static unsigned char *bt_list;

/* The colour the grid's markers carry on one of its rows, which is what says
   which grid row a screen pixel came from. */
static int bt_mark(int list_row)
{
    return BT_LIST_MARK_BASE + (list_row & 0x3f);
}

/* Lays fill runs across one row of a sprite stream until the sprite's whole
   width is accounted for, and answers how many bytes that took.  A row that did
   not close exactly would leave the decoder walking off the end of the sheet,
   and so would a gap left between one row's last byte and the next row's first
   (rle.h), which is why the caller advances by what this returns. */
static int bt_fill_row(unsigned char *stream, int width, int color)
{
    int at;
    int run;

    at = 0;
    while (width > 0) {
        run = width;
        if (run > BT_MAX_RUN) {
            run = BT_MAX_RUN;
        }
        stream[at] = (unsigned char) (run - 1);
        stream[at + 1] = (unsigned char) color;
        at += 2;
        width -= run;
    }
    return at;
}

static void bt_write_header(unsigned char *sheet, int width, int height,
                            int sprites)
{
    struct fdps_cel_header *header;

    header = (struct fdps_cel_header *) sheet;
    header->magic[0] = 'C';
    header->magic[1] = 'E';
    header->magic[2] = 'L';
    header->sprite_width = (short) width;
    header->sprite_height = (short) height;
    header->sprite_count = (short) sprites;
}

/* A sheet whose sprite n is a flat fill of color_base + n.  A stored offset is
   measured from the start of the sheet, which is the .CEL rule
   (resource_info/cel.md). */
static void bt_build_fill_sheet(unsigned char *sheet, int sprites,
                                int stream_at, int row_stride, int width,
                                int height, int color_base)
{
    int sprite;
    int row;
    int sprite_at;
    int at;

    bt_write_header(sheet, width, height, sprites);

    for (sprite = 0; sprite < sprites; sprite++) {
        sprite_at = stream_at + sprite * height * row_stride;
        *(int *) (sheet + BT_CEL_TABLE_AT + sprite * 4) = sprite_at;
        at = sprite_at;
        for (row = 0; row < height; row++) {
            at += bt_fill_row(sheet + at, width, color_base + sprite);
        }
    }
}

/* Command.cel's shape: seven pixels of colour then a skip to the end of the
   cell on the top five rows, and a single skip run on the other seventeen, laid
   down one after another with nothing between them. */
static void bt_build_arrow_sheet(void)
{
    unsigned char *stream;
    int sprite;
    int row;
    int sprite_at;
    int at;

    bt_write_header(bt_cmd_sheet, BT_CMD_W, BT_CMD_H, BT_CMD_SPRITES);

    for (sprite = 0; sprite < BT_CMD_SPRITES; sprite++) {
        sprite_at = BT_CMD_STREAM_AT + sprite * BT_CMD_SPRITE_BYTES;
        *(int *) (bt_cmd_sheet + BT_CEL_TABLE_AT + sprite * 4) = sprite_at;
        at = sprite_at;
        for (row = 0; row < BT_CMD_H; row++) {
            stream = bt_cmd_sheet + at;
            if (row < BT_ARROW_ROWS) {
                stream[0] = (unsigned char) (BT_ARROW_W - 1);
                stream[1] = (unsigned char) (BT_CMD_COLOR_BASE + sprite);
                stream[2] = (unsigned char)
                            (BT_SKIP_OP | (BT_CMD_W - BT_ARROW_W - 1));
                at += BT_CMD_MARKED_BYTES;
            } else {
                stream[0] = (unsigned char) (BT_SKIP_OP | (BT_CMD_W - 1));
                at += BT_CMD_SKIPPED_BYTES;
            }
        }
    }
}

static void bt_stage(void)
{
    int row;
    unsigned char *grid_row;

    memset(bt_win_sheet, 0, sizeof(bt_win_sheet));
    memset(bt_bar_sheet, 0, sizeof(bt_bar_sheet));
    memset(bt_cmd_sheet, 0, sizeof(bt_cmd_sheet));
    memset(bt_snapshot, 0, sizeof(bt_snapshot));

    bt_build_fill_sheet(bt_win_sheet, BT_WIN_SPRITES, BT_WIN_STREAM_AT,
                        BT_WIN_ROW_STRIDE, BT_WIN_W, BT_WIN_H,
                        BT_WIN_COLOR_BASE);
    bt_build_fill_sheet(bt_bar_sheet, BT_BAR_SPRITES, BT_BAR_STREAM_AT,
                        BT_BAR_ROW_STRIDE, BT_BAR_W, BT_BAR_H,
                        BT_BAR_COLOR_BASE);
    bt_build_arrow_sheet();

    bt_list = (unsigned char *) malloc((size_t) BT_LIST_BYTES);
    memset(bt_list, 0, (size_t) BT_LIST_BYTES);
    for (row = 0; row < BT_LIST_ROWS; row++) {
        grid_row = bt_list + row * BT_PAGE_PITCH;
        grid_row[BT_LIST_MARK_FIRST] = (unsigned char) bt_mark(row);
        grid_row[BT_LIST_MARK_MID] = (unsigned char) bt_mark(row);
        grid_row[BT_LIST_MARK_LAST] = (unsigned char) bt_mark(row);
    }

    data_fdps_village_window_sheet_ptr = bt_win_sheet;
    data_fdps_selection_bar_sheet_ptr = bt_bar_sheet;
    data_fdps_command_sprite_sheet_ptr = bt_cmd_sheet;
}

/* Back to the state a freshly started program has these in: the village phase
   frees all three of them unguarded, so a case that walked away leaving one of
   them naming a static here hands a later test a free of storage that never
   came from the heap. */
static void bt_unstage(void)
{
    data_fdps_village_window_sheet_ptr = NULL;
    data_fdps_selection_bar_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_roster_member_count = 0;
    free(bt_list);
    bt_list = NULL;
}

static void bt_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole frame, with the adapter in the mode the game draws it in, and the
   band of screen rows the window can reach brought back to text mode. */
static void bt_run(int cursor_x, int src_row, int scroll_top, int roster_count,
                   unsigned int tick)
{
    bt_stage();
    data_fdps_roster_member_count = roster_count;
    data_fdps_timer_tick_counter = tick;
    data_fdps_ui_palette_last_cycle_tick = tick;
    data_fdps_ui_palette_cycle_phase = 0;

    bt_set_mode(BT_MODE_320X200X256);
    memset((void *) BT_VGA_BASE, BT_SENTINEL, (size_t) BT_SCREEN_BYTES);
    fdps_shop_render_buy_target_frame(bt_list, cursor_x, src_row, scroll_top);
    memmove(bt_snapshot,
            (void *) (BT_VGA_BASE + BT_SNAP_FIRST_ROW * BT_SCREEN_PITCH),
            (size_t) BT_SNAP_BYTES);
    bt_set_mode(BT_MODE_TEXT);
    bt_unstage();
}

static int bt_screen(int screen_row, int screen_col)
{
    return (int) bt_snapshot[(screen_row - BT_SNAP_FIRST_ROW)
                             * BT_SCREEN_PITCH + screen_col];
}

/* The screen pixel showing page (page_row, page_col). */
static int bt_page(int page_row, int page_col)
{
    return bt_screen(page_row + BT_PAGE_TO_SCREEN_ROW,
                     page_col + BT_PAGE_TO_SCREEN_COL);
}

/* Sprite 0 of the sheet in data_fdps_village_window_sheet_ptr goes down first
   at page (0,0), so every page pixel no later step claims carries its colour.
   Page rows 67 and 69 are below the entry strip's 0x43 rows entirely, which is
   where the window is seen alone. */
static void buytarget_window_is_sprite_zero_of_the_village_sheet(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 0, 0);

    CHECK_EQ(bt_page(67, 200), BT_WIN_COLOR_BASE);
    CHECK_EQ(bt_page(69, 250), BT_WIN_COLOR_BASE);
    CHECK_EQ(bt_page(40, 200), BT_WIN_COLOR_BASE);
}

/* Sprite 2 of the sheet in data_fdps_selection_bar_sheet_ptr goes down second
   at page (cursor_x, 0x14).  101 columns and 17 rows is the bar's own size, so
   the pixel one past either edge is window again. */
static void buytarget_bar_is_sprite_two_at_row_0x14(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 0, 0);

    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, BT_CURSOR_X_MIDDLE),
             BT_BAR_COLOR_BASE + BT_BAR_SPRITE);
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW + BT_BAR_H - 1,
                     BT_CURSOR_X_MIDDLE + BT_BAR_W - 1),
             BT_BAR_COLOR_BASE + BT_BAR_SPRITE);
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW - 1, BT_CURSOR_X_MIDDLE),
             BT_WIN_COLOR_BASE);
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW + BT_BAR_H, BT_CURSOR_X_MIDDLE),
             BT_WIN_COLOR_BASE);
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, BT_CURSOR_X_MIDDLE + BT_BAR_W),
             BT_WIN_COLOR_BASE);
}

/* cursor_x is the bar's page column and nothing else moves with it. */
static void buytarget_bar_column_is_the_cursor_argument(void)
{
    bt_run(BT_CURSOR_X_RIGHT, 0, 0, 0, 0);

    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, BT_CURSOR_X_RIGHT),
             BT_BAR_COLOR_BASE + BT_BAR_SPRITE);
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, BT_CURSOR_X_RIGHT - 1),
             BT_WIN_COLOR_BASE);
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, BT_CURSOR_X_MIDDLE),
             BT_WIN_COLOR_BASE);
}

/* The strip is read from list_bitmap + src_row * 0x138 and lands on page row 0,
   so page row n shows grid row src_row + n. */
static void buytarget_strip_starts_at_src_row_times_the_pitch(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 0, 0);

    CHECK_EQ(bt_page(3, BT_LIST_MARK_MID), bt_mark(3));
    CHECK_EQ(bt_page(4, BT_LIST_MARK_MID), bt_mark(4));
    CHECK_EQ(bt_page(5, BT_LIST_MARK_MID), bt_mark(5));
}

static void buytarget_strip_follows_a_moved_src_row(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, BT_STRIP_H, 0, 0, 0);

    CHECK_EQ(bt_page(3, BT_LIST_MARK_MID), bt_mark(BT_STRIP_H + 3));
    CHECK_EQ(bt_page(4, BT_LIST_MARK_MID), bt_mark(BT_STRIP_H + 4));
    CHECK_EQ(bt_page(60, BT_LIST_MARK_MID), bt_mark(BT_STRIP_H + 60));
}

/* The strip goes through the colour-keyed blit, so a zero source byte leaves
   the window or the bar underneath showing (blit.h).  Grid column 200 is zero
   on every row; column 10 carries the row's marker. */
static void buytarget_strip_is_colour_keyed(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 0, 0);

    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, 200),
             BT_BAR_COLOR_BASE + BT_BAR_SPRITE);
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, BT_LIST_MARK_MID),
             bt_mark(BT_BAR_PAGE_ROW));
}

/* The strip is 0x43 rows tall, so page row 0x43 and everything under it is
   window only whatever the grid holds. */
static void buytarget_strip_is_0x43_rows_tall(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 0, 0);

    CHECK_EQ(bt_page(BT_STRIP_H - 1, BT_LIST_MARK_MID),
             bt_mark(BT_STRIP_H - 1));
    CHECK_EQ(bt_page(BT_STRIP_H, BT_LIST_MARK_MID), BT_WIN_COLOR_BASE);
}

/* The copy is 304 by 67 taken from page (3,5) and written at screen (8,125), so
   page column 5 is the leftmost column on the screen and 308 the rightmost,
   page row 3 the topmost and 69 the bottom one, and the pixel one outside each
   of those four edges still holds the sentinel. */
static void buytarget_screen_window_is_304_by_67_at_8_125(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 0, 0);

    CHECK_EQ(bt_screen(BT_WINDOW_SCREEN_ROW, BT_WINDOW_SCREEN_COL),
             bt_mark(3));
    CHECK_EQ(bt_screen(BT_WINDOW_SCREEN_ROW,
                       BT_WINDOW_SCREEN_COL + BT_WINDOW_W - 1),
             bt_mark(3));
    CHECK_EQ(bt_screen(BT_WINDOW_SCREEN_ROW + BT_WINDOW_H - 1,
                       BT_WINDOW_SCREEN_COL + 200),
             BT_WIN_COLOR_BASE);
    CHECK_EQ(bt_screen(BT_WINDOW_SCREEN_ROW, BT_WINDOW_SCREEN_COL - 1),
             BT_SENTINEL);
    CHECK_EQ(bt_screen(BT_WINDOW_SCREEN_ROW,
                       BT_WINDOW_SCREEN_COL + BT_WINDOW_W),
             BT_SENTINEL);
    CHECK_EQ(bt_screen(BT_WINDOW_SCREEN_ROW - 1, BT_WINDOW_SCREEN_COL + 200),
             BT_SENTINEL);
    CHECK_EQ(bt_screen(BT_WINDOW_SCREEN_ROW + BT_WINDOW_H,
                       BT_WINDOW_SCREEN_COL + 200),
             BT_SENTINEL);
}

/* The up arrow is drawn when scroll_top is not zero and not otherwise, and it
   goes at page (5, 150) with sprite 0x44 plus the blink phase. */
static void buytarget_up_arrow_needs_a_scrolled_list(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 0, 0);
    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL), BT_WIN_COLOR_BASE);

    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 0, 0);
    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE);
    CHECK_EQ(bt_page(BT_ARROW_UP_ROW + BT_ARROW_ROWS - 1,
                     BT_ARROW_COL + BT_ARROW_W - 1),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE);
    CHECK_EQ(bt_page(BT_ARROW_UP_ROW + BT_ARROW_ROWS, BT_ARROW_COL),
             BT_WIN_COLOR_BASE);
}

/* The blink is (tick / 5) & 1, so the lit frame is shown for the five ticks
   from 5 to 9 and the dark one for 0 to 4 and again from 10. */
static void buytarget_arrow_blink_flips_every_five_ticks(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 0, 0);
    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE);

    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 0, BT_BLINK_TICKS - 1);
    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE);

    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 0, BT_BLINK_TICKS);
    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE + 1);

    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 0, BT_BLINK_TICKS * 2);
    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE);
}

/* DIV EBX and not IDIV: 0xffffffff over five is 858993459, which is odd, so the
   phase is 1.  Read as a signed -1 the quotient would be 0 and the dark frame
   would be shown instead. */
static void buytarget_blink_divide_is_unsigned(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 0, 0xffffffffu);

    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE + 1);
}

/* The down arrow is drawn when scroll_top + 3 is still below the roster count,
   at page (64, 150) with sprite 0x46 plus the blink phase. */
static void buytarget_down_arrow_needs_a_row_below(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 4, 0);
    CHECK_EQ(bt_page(BT_ARROW_DOWN_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_DOWN_SPRITE);

    bt_run(BT_CURSOR_X_MIDDLE, 0, 0, 3, 0);
    CHECK_EQ(bt_page(BT_ARROW_DOWN_ROW, BT_ARROW_COL), BT_WIN_COLOR_BASE);
}

/* The test is strict and it counts from scroll_top: a roster of exactly
   scroll_top + 3 is the last row and draws nothing. */
static void buytarget_down_arrow_test_counts_from_scroll_top(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 6, 0);
    CHECK_EQ(bt_page(BT_ARROW_DOWN_ROW, BT_ARROW_COL), BT_WIN_COLOR_BASE);

    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 7, 0);
    CHECK_EQ(bt_page(BT_ARROW_DOWN_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_DOWN_SPRITE);
}

/* Both arrows take the same phase, and both can be on the same frame. */
static void buytarget_both_arrows_share_one_blink_phase(void)
{
    bt_run(BT_CURSOR_X_MIDDLE, 0, 3, 9, BT_BLINK_TICKS);

    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE + 1);
    CHECK_EQ(bt_page(BT_ARROW_DOWN_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_DOWN_SPRITE + 1);
}

/* The arrows go down after the strip, so a grid marker under one of them does
   not survive; the bar goes down before it, so one under the strip does not.
   That is the whole composing order in two assertions. */
static void buytarget_layers_go_down_back_to_front(void)
{
    bt_run(BT_CURSOR_X_LEFT, 0, 3, 0, 0);

    CHECK_EQ(bt_page(BT_ARROW_UP_ROW, BT_ARROW_COL),
             BT_CMD_COLOR_BASE + BT_ARROW_UP_SPRITE);
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, BT_LIST_MARK_MID),
             bt_mark(BT_BAR_PAGE_ROW));
    CHECK_EQ(bt_page(BT_BAR_PAGE_ROW, BT_LIST_MARK_MID + 1),
             BT_BAR_COLOR_BASE + BT_BAR_SPRITE);
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

    RUN_TEST(buytarget_window_is_sprite_zero_of_the_village_sheet);
    RUN_TEST(buytarget_bar_is_sprite_two_at_row_0x14);
    RUN_TEST(buytarget_bar_column_is_the_cursor_argument);
    RUN_TEST(buytarget_strip_starts_at_src_row_times_the_pitch);
    RUN_TEST(buytarget_strip_follows_a_moved_src_row);
    RUN_TEST(buytarget_strip_is_colour_keyed);
    RUN_TEST(buytarget_strip_is_0x43_rows_tall);
    RUN_TEST(buytarget_screen_window_is_304_by_67_at_8_125);
    RUN_TEST(buytarget_up_arrow_needs_a_scrolled_list);
    RUN_TEST(buytarget_arrow_blink_flips_every_five_ticks);
    RUN_TEST(buytarget_blink_divide_is_unsigned);
    RUN_TEST(buytarget_down_arrow_needs_a_row_below);
    RUN_TEST(buytarget_down_arrow_test_counts_from_scroll_top);
    RUN_TEST(buytarget_both_arrows_share_one_blink_phase);
    RUN_TEST(buytarget_layers_go_down_back_to_front);
}
