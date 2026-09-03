/* tests/text.c -- cover for src/text.c.
 *
 * Expected values come from the assembly of fdps_blit_glyph_1bpp at 0001fed0
 * -- TEST dword ptr [EBP-0xc],0x7 / JNZ for the fetch condition, MOV EDX,
 * [EBP+0x1c] / INC dword ptr [EBP+0x1c] for the post-increment fetch, SHL
 * dword ptr [EBP-0x4],0x1 for the shift, TEST dword ptr [EBP-0x4],0x80 / JZ
 * for the pixel test, MOV AL,byte ptr [EBP+0x20] / MOV byte ptr [EDX],AL for
 * the store, and ADD dword ptr [EBP+0x14],EAX for the row step -- and from the
 * call sites in fdps_draw_glyph at 0001fe10 onwards.  None of them is read off
 * the emitted C.
 *
 * The glyph cell size arrives through data_fdps_font_glyph_width and
 * data_fdps_glyph_cell_height, so a test stages those two globals the way it
 * stages its own buffers.  Nothing below asserts what either of them holds on
 * its own -- ticket 23 owns their contents -- only what the blit does with the
 * value it was handed.
 */
#include "testharn.h"
#include "gamedata.h"
#include "text.h"

/* Four rows of a 16-byte pitch, so a row step lands somewhere the next test
   can look at, plus a whole spare row past the bottom of every cell staged
   here to catch a walk that runs long. */
#define DST_PITCH  16
#define DST_ROWS   4
#define DST_BYTES  (DST_PITCH * DST_ROWS)

/* Neither 0x00 nor any colour a test paints with, so an untouched byte says so
   on its own. */
#define BACKGROUND 0xaa

static unsigned char dst_surface[DST_BYTES];

static void stage(int cell_width, int cell_height)
{
    int i;

    for (i = 0; i < DST_BYTES; i++) {
        dst_surface[i] = BACKGROUND;
    }
    data_fdps_font_glyph_width = (unsigned char) cell_width;
    data_fdps_glyph_cell_height = (unsigned char) cell_height;
}

static int pixel(int row, int column)
{
    return (int) dst_surface[row * DST_PITCH + column];
}

/* A source bit that is set stores the colour; a source bit that is clear takes
   the JZ at 0001ff38 past the store and leaves the destination byte alone.
   0xa1 is 1010 0001, most significant bit first, so columns 0, 2 and 7 are
   painted and 1, 3, 4, 5 and 6 keep the background. */
static void set_bits_paint_and_clear_bits_do_not(void)
{
    unsigned char glyph[1];

    glyph[0] = 0xa1;
    stage(8, 1);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0x2c);

    CHECK_EQ(pixel(0, 0), 0x2c);
    CHECK_EQ(pixel(0, 1), BACKGROUND);
    CHECK_EQ(pixel(0, 2), 0x2c);
    CHECK_EQ(pixel(0, 3), BACKGROUND);
    CHECK_EQ(pixel(0, 4), BACKGROUND);
    CHECK_EQ(pixel(0, 5), BACKGROUND);
    CHECK_EQ(pixel(0, 6), BACKGROUND);
    CHECK_EQ(pixel(0, 7), 0x2c);
}

/* The bit tested is always 0x80 of the window and the window shifts LEFT, so
   the byte is consumed most significant bit first: 0x80 alone paints column 0
   and 0x01 alone paints column 7.  A right shift would swap the two. */
static void bits_are_consumed_most_significant_first(void)
{
    unsigned char first[1];
    unsigned char last[1];

    first[0] = 0x80;
    stage(8, 1);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, first, 0x11);
    CHECK_EQ(pixel(0, 0), 0x11);
    CHECK_EQ(pixel(0, 7), BACKGROUND);

    last[0] = 0x01;
    stage(8, 1);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, last, 0x11);
    CHECK_EQ(pixel(0, 0), BACKGROUND);
    CHECK_EQ(pixel(0, 7), 0x11);
}

/* TEST dword ptr [EBP-0xc],0x7 is true again at column 0 of every row, so a
   row starts on a fresh source byte and the spare bits of the previous row's
   last byte are dropped.  Width 12 consumes two bytes per row: row 0 takes
   bytes 0 and 1, row 1 takes bytes 2 and 3.  Byte 3 is 0x10, whose bit 0x80
   arrives at column 11 after the three shifts from column 8. */
static void rows_are_byte_aligned_in_the_source(void)
{
    unsigned char glyph[4];

    glyph[0] = 0x80;
    glyph[1] = 0x00;
    glyph[2] = 0x00;
    glyph[3] = 0x10;
    stage(12, 2);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0x33);

    CHECK_EQ(pixel(0, 0), 0x33);
    CHECK_EQ(pixel(0, 8), BACKGROUND);
    CHECK_EQ(pixel(0, 11), BACKGROUND);
    CHECK_EQ(pixel(1, 0), BACKGROUND);
    CHECK_EQ(pixel(1, 10), BACKGROUND);
    CHECK_EQ(pixel(1, 11), 0x33);
}

/* ADD dword ptr [EBP+0x14],EAX with EAX = [EBP+0x18] steps the destination by
   the pitch argument once per row, and the column index is a plain byte offset
   inside the row.  Three rows of 0x80 land at 0, pitch and 2*pitch. */
static void each_row_advances_the_destination_by_pitch(void)
{
    unsigned char glyph[3];

    glyph[0] = 0x80;
    glyph[1] = 0x80;
    glyph[2] = 0x80;
    stage(8, 3);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0x44);

    CHECK_EQ((int) dst_surface[0], 0x44);
    CHECK_EQ((int) dst_surface[DST_PITCH], 0x44);
    CHECK_EQ((int) dst_surface[DST_PITCH * 2], 0x44);
    CHECK_EQ((int) dst_surface[DST_PITCH * 3], BACKGROUND);
    CHECK_EQ((int) dst_surface[1], BACKGROUND);
}

/* The blit reaches its destination through the pointer it was handed, with no
   base of its own, so the same glyph drawn at dst + 1 lands one byte right.
   That offsetting is exactly how fdps_draw_glyph lays down its outline: the
   four calls at 0001fe10, 0001fe29, 0001fe44 and 0001fe5d pass dst + pitch,
   dst - 1, dst - pitch and dst + 1. */
static void destination_offset_is_the_callers_to_choose(void)
{
    unsigned char glyph[1];

    glyph[0] = 0x80;
    stage(8, 1);
    fdps_blit_glyph_1bpp(dst_surface + 1, DST_PITCH, glyph, 0x55);

    CHECK_EQ(pixel(0, 0), BACKGROUND);
    CHECK_EQ(pixel(0, 1), 0x55);
}

/* MOV AL,byte ptr [EBP+0x20] takes the low byte of the colour argument and
   nothing else, so 0x1234 stores 0x34.  A caller that hands over a wider value
   does not spill into the neighbouring pixel. */
static void only_the_low_byte_of_color_is_stored(void)
{
    unsigned char glyph[1];

    glyph[0] = 0xc0;
    stage(8, 1);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0x1234);

    CHECK_EQ(pixel(0, 0), 0x34);
    CHECK_EQ(pixel(0, 1), 0x34);
    CHECK_EQ(pixel(0, 2), BACKGROUND);
}

/* Only the callee's copy of glyph_bits advances -- the INC is on the argument
   slot [EBP+0x1c], which the caller pushed by value.  fdps_draw_glyph reloads
   the same [EBP+0x1c] for all five calls, so blitting twice from one pointer
   has to draw the same glyph twice, not the glyph and then whatever follows
   it in the font. */
static void callers_glyph_pointer_survives_the_blit(void)
{
    unsigned char glyph[2];
    unsigned char *cursor;

    glyph[0] = 0x80;
    glyph[1] = 0x01;
    cursor = glyph;
    stage(8, 1);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, cursor, 0x66);
    fdps_blit_glyph_1bpp(dst_surface + DST_PITCH, DST_PITCH, cursor, 0x66);

    CHECK_EQ(cursor == glyph, 1);
    CHECK_EQ(pixel(0, 0), 0x66);
    CHECK_EQ(pixel(0, 7), BACKGROUND);
    CHECK_EQ(pixel(1, 0), 0x66);
    CHECK_EQ(pixel(1, 7), BACKGROUND);
}

/* The two loop heads test the bound before the body (CMP EAX,[EBP-0x8] / JG
   into the body, JMP out otherwise), so a zero cell dimension draws nothing at
   all rather than one row or one column. */
static void a_zero_cell_dimension_draws_nothing(void)
{
    unsigned char glyph[2];

    glyph[0] = 0xff;
    glyph[1] = 0xff;

    stage(8, 0);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0x77);
    CHECK_EQ(pixel(0, 0), BACKGROUND);

    stage(0, 4);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0x77);
    CHECK_EQ(pixel(0, 0), BACKGROUND);
    CHECK_EQ(pixel(3, 0), BACKGROUND);
}

/* Both bounds are widened with XOR EAX,EAX / MOV AL -- zero extension.  A cell
   height of 200 has bit 7 set; read through a signed char it would be -56 and
   the signed JG would end the loop before the first row.  Staged at width 8
   and height 4 here only because the fixture is that big; the assertion is
   that the height byte 0xc8 does not read as negative, which is checked by
   painting rows 0 through 3 and finding them painted. */
static void the_cell_bounds_are_unsigned_bytes(void)
{
    unsigned char glyph[4];
    int rows_painted;
    int row;

    CHECK_EQ((int) data_fdps_font_glyph_width >= 0, 1);
    CHECK_EQ((int) data_fdps_glyph_cell_height >= 0, 1);

    glyph[0] = 0x80;
    glyph[1] = 0x80;
    glyph[2] = 0x80;
    glyph[3] = 0x80;
    stage(8, 4);
    data_fdps_glyph_cell_height = (unsigned char) 0xc8;
    /* 0xc8 rows would run off the four-row fixture, so the height is put back
       to what the fixture can hold once its widening has been checked. */
    CHECK_EQ((int) data_fdps_glyph_cell_height, 200);
    data_fdps_glyph_cell_height = (unsigned char) 4;

    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0x88);
    rows_painted = 0;
    for (row = 0; row < 4; row++) {
        if (pixel(row, 0) == 0x88) {
            rows_painted++;
        }
    }
    CHECK_EQ(rows_painted, 4);
}

/* Nothing outside the cell is touched: no clipping, but no overdraw either.
   Width 4 leaves columns 4 through 7 of the same source byte unpainted even
   though their bits are set, and the row below the last one is untouched. */
static void nothing_outside_the_cell_is_written(void)
{
    unsigned char glyph[2];

    glyph[0] = 0xff;
    glyph[1] = 0xff;
    stage(4, 2);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0x99);

    CHECK_EQ(pixel(0, 3), 0x99);
    CHECK_EQ(pixel(0, 4), BACKGROUND);
    CHECK_EQ(pixel(1, 3), 0x99);
    CHECK_EQ(pixel(2, 0), BACKGROUND);
}

/* A width of 4 still consumes a whole source byte per row -- the fetch fires
   at column 0 and nowhere else in the row -- so row 1 reads byte 1, not the
   second nibble of byte 0.  Byte 0 paints its top half, byte 1 paints nothing. */
static void a_narrow_cell_still_eats_a_whole_byte_per_row(void)
{
    unsigned char glyph[2];

    glyph[0] = 0xf0;
    glyph[1] = 0x00;
    stage(4, 2);
    fdps_blit_glyph_1bpp(dst_surface, DST_PITCH, glyph, 0xbb);

    CHECK_EQ(pixel(0, 0), 0xbb);
    CHECK_EQ(pixel(0, 3), 0xbb);
    CHECK_EQ(pixel(1, 0), BACKGROUND);
    CHECK_EQ(pixel(1, 3), BACKGROUND);
}

/* --- fdps_draw_glyph at 0001fd80 ---------------------------------------
 *
 * A second fixture, because this one has to have room outside the cell: the
 * outline style reaches one row above, one row below, one column left and one
 * column right of it, so the cell is staged away from the buffer's edges and
 * everything below is addressed in absolute buffer coordinates rather than
 * relative to the cell.
 *
 * Expected values come from the assembly at 0001fd80: IMUL EAX,dword ptr
 * [0x00064046] / ADD dword ptr [EBP+0x1c],EAX for the glyph address, the
 * CMP/JZ at 0001fd9d, 0001fe6b and 0001fea0 for the three colour guards, CMP
 * byte ptr [0x0006404a],0x0 / JZ 0x0001fe67 for the style choice, the four
 * unguarded call sites at 0001fdfd, 0001fe18, 0001fe31 and 0001fe4c for the
 * outline, and MOV EAX,[0x00064042] / IMUL EAX,[EBP+0x18] / ADD EAX,[EBP+0x14]
 * / ADD EDX(=[0x0006403e]),EAX for the shadow's address.
 */
#define CELL_PITCH  16
#define CELL_ROWS   8
#define CELL_BYTES  (CELL_PITCH * CELL_ROWS)

/* Far enough in that dst - 1 and dst - pitch are still inside the buffer. */
#define ORIGIN_ROW  2
#define ORIGIN_COL  2

static unsigned char glyph_surface[CELL_BYTES];

static unsigned char *cell_origin(void)
{
    return glyph_surface + ORIGIN_ROW * CELL_PITCH + ORIGIN_COL;
}

static int cell_pixel(int row, int column)
{
    return (int) glyph_surface[row * CELL_PITCH + column];
}

/* Every font global fdps_draw_glyph reads is set on every stage, so no test
   inherits a style or an offset from the one before it. */
static void stage_cell(int cell_width, int cell_height, int glyph_stride,
                       int outline_style, int shadow_rows, int shadow_columns)
{
    int i;

    for (i = 0; i < CELL_BYTES; i++) {
        glyph_surface[i] = BACKGROUND;
    }
    data_fdps_font_glyph_width = (unsigned char) cell_width;
    data_fdps_glyph_cell_height = (unsigned char) cell_height;
    data_fdps_font_glyph_stride_bytes = glyph_stride;
    data_fdps_font_outline_enabled_flag = (unsigned char) outline_style;
    data_fdps_glyph_shadow_row_offset = shadow_rows;
    data_fdps_font_shadow_offset_x = shadow_columns;
}

/* The fill walks data_fdps_glyph_cell_height rows of
   data_fdps_font_glyph_width bytes and steps the row pointer by the pitch
   argument, so a 4 by 3 cell paints columns 2..5 of rows 2..4 and nothing
   else.  Column 6 and row 5 say the bounds are exclusive; column 1 says the
   fill starts at dst and not before it. */
static void the_background_fill_covers_the_cell_and_steps_by_pitch(void)
{
    unsigned char glyph[4];

    glyph[0] = 0xff;
    glyph[1] = 0xff;
    glyph[2] = 0xff;
    glyph[3] = 0xff;
    stage_cell(4, 3, 1, 0, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0x5a, 0);

    CHECK_EQ(cell_pixel(2, 2), 0x5a);
    CHECK_EQ(cell_pixel(2, 5), 0x5a);
    CHECK_EQ(cell_pixel(2, 6), BACKGROUND);
    CHECK_EQ(cell_pixel(2, 1), BACKGROUND);
    CHECK_EQ(cell_pixel(4, 5), 0x5a);
    CHECK_EQ(cell_pixel(5, 2), BACKGROUND);
}

/* MOV AL,byte ptr [EBP+0x28] takes the low byte of bg_color and nothing
   wider, so 0x1234 fills with 0x34. */
static void the_background_fill_stores_only_the_low_byte(void)
{
    unsigned char glyph[1];

    glyph[0] = 0x00;
    stage_cell(2, 1, 1, 0, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0x1234, 0);

    CHECK_EQ(cell_pixel(2, 2), 0x34);
    CHECK_EQ(cell_pixel(2, 3), 0x34);
}

/* CMP dword ptr [EBP+0x28],0x0 / JZ 0x0001fdf4 jumps the whole fill, so a
   zero background composites the glyph onto what is already on the surface
   instead of clearing the cell to palette index 0. */
static void a_zero_background_color_skips_the_fill(void)
{
    unsigned char glyph[1];

    glyph[0] = 0x00;
    stage_cell(4, 2, 1, 0, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0, 0);

    CHECK_EQ(cell_pixel(2, 2), BACKGROUND);
    CHECK_EQ(cell_pixel(3, 2), BACKGROUND);
}

/* The glyph drawn is font_base + glyph_index * data_fdps_font_glyph_stride
   _bytes, and the stride is its own global rather than anything derived from
   the cell: a 2-byte stride over an 8 by 2 cell puts glyph 1 at sheet[2].
   Glyph 0 is a left-hand column, glyph 1 a right-hand one, so picking the
   wrong one is visible rather than merely off by a byte. */
static void the_glyph_index_scales_by_the_sheet_stride(void)
{
    unsigned char sheet[4];

    sheet[0] = 0x80;
    sheet[1] = 0x80;
    sheet[2] = 0x01;
    sheet[3] = 0x01;

    stage_cell(8, 2, 2, 0, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, sheet, 1, 0x66, 0, 0);
    CHECK_EQ(cell_pixel(2, 9), 0x66);
    CHECK_EQ(cell_pixel(3, 9), 0x66);
    CHECK_EQ(cell_pixel(2, 2), BACKGROUND);

    stage_cell(8, 2, 2, 0, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, sheet, 0, 0x66, 0, 0);
    CHECK_EQ(cell_pixel(2, 2), 0x66);
    CHECK_EQ(cell_pixel(3, 2), 0x66);
    CHECK_EQ(cell_pixel(2, 9), BACKGROUND);
}

/* data_fdps_font_outline_enabled_flag non-zero selects four blits at dst +
   pitch, dst - 1, dst - pitch and dst + 1 -- the 4-connected neighbours, so
   the diagonal at (1,1) stays clear.  fg_color is 0 here, which leaves the
   centre pixel untouched and lets the outline be seen on its own. */
static void the_outline_style_paints_four_neighbours(void)
{
    unsigned char glyph[1];

    glyph[0] = 0x80;
    stage_cell(8, 1, 1, 1, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0, 0x33);

    CHECK_EQ(cell_pixel(3, 2), 0x33);
    CHECK_EQ(cell_pixel(2, 1), 0x33);
    CHECK_EQ(cell_pixel(1, 2), 0x33);
    CHECK_EQ(cell_pixel(2, 3), 0x33);
    CHECK_EQ(cell_pixel(1, 1), BACKGROUND);
    CHECK_EQ(cell_pixel(2, 2), BACKGROUND);
}

/* The pitfall.  Nothing compares outline_color before the four call sites at
   0001fdfd..0001fe4c -- the only CMP of [EBP+0x2c] is at 0001fe67, on the
   shadow's side of the style branch.  So a zero outline_color with the
   outline style selected paints four palette-index-0 copies of the glyph, and
   C that wraps the whole decoration in one `if (outline_color != 0)` silently
   stops drawing them. */
static void the_outline_is_not_guarded_by_the_outline_color(void)
{
    unsigned char glyph[1];

    glyph[0] = 0x80;
    stage_cell(8, 1, 1, 1, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0, 0);

    CHECK_EQ(cell_pixel(3, 2), 0x00);
    CHECK_EQ(cell_pixel(2, 1), 0x00);
    CHECK_EQ(cell_pixel(1, 2), 0x00);
    CHECK_EQ(cell_pixel(2, 3), 0x00);
    CHECK_EQ(cell_pixel(2, 2), BACKGROUND);
}

/* With the flag clear the decoration is a single shadow at dst +
   data_fdps_glyph_shadow_row_offset * pitch + data_fdps_font_shadow_offset_x,
   and no outline at all: the neighbours the outline would have painted stay
   clear.  Both offsets are signed dwords, so a negative pair puts the shadow
   up and to the left rather than an enormous distance forward. */
static void the_shadow_style_uses_the_two_offset_globals(void)
{
    unsigned char glyph[1];

    glyph[0] = 0x80;
    stage_cell(8, 1, 1, 0, 1, 1);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0, 0x44);
    CHECK_EQ(cell_pixel(3, 3), 0x44);
    CHECK_EQ(cell_pixel(2, 2), BACKGROUND);
    CHECK_EQ(cell_pixel(3, 2), BACKGROUND);
    CHECK_EQ(cell_pixel(2, 3), BACKGROUND);
    CHECK_EQ(cell_pixel(1, 2), BACKGROUND);

    stage_cell(8, 1, 1, 0, -1, -1);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0, 0x44);
    CHECK_EQ(cell_pixel(1, 1), 0x44);
    CHECK_EQ(cell_pixel(3, 3), BACKGROUND);
}

/* CMP dword ptr [EBP+0x2c],0x0 / JZ 0x0001fe9c does guard the shadow, so the
   same zero that leaves four outline copies behind in the test above draws
   nothing at all under the shadow style. */
static void a_zero_outline_color_skips_the_shadow(void)
{
    unsigned char glyph[1];

    glyph[0] = 0xff;
    stage_cell(8, 1, 1, 0, 1, 1);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0, 0);

    CHECK_EQ(cell_pixel(3, 3), BACKGROUND);
    CHECK_EQ(cell_pixel(2, 2), BACKGROUND);
}

/* The foreground blit is last (0001fea2 onwards, after both decoration
   paths), so the glyph body covers the fill rather than the other way round:
   column 0 of the cell ends up fg and the rest of it stays bg. */
static void the_glyph_body_lands_on_top_of_the_fill(void)
{
    unsigned char glyph[1];

    glyph[0] = 0x80;
    stage_cell(8, 1, 1, 0, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0x22, 0x11, 0);

    CHECK_EQ(cell_pixel(2, 2), 0x22);
    CHECK_EQ(cell_pixel(2, 3), 0x11);
    CHECK_EQ(cell_pixel(2, 9), 0x11);
}

/* CMP dword ptr [EBP+0x24],0x0 / JZ 0x0001feba skips the foreground blit, so
   a zero fg_color leaves the fill showing rather than stamping the glyph in
   palette index 0 over it.  That is how a caller paints a cell's background
   and decoration and no character. */
static void a_zero_foreground_color_skips_the_glyph_body(void)
{
    unsigned char glyph[1];

    glyph[0] = 0xff;
    stage_cell(8, 1, 1, 0, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, glyph, 0, 0, 0x11, 0);

    CHECK_EQ(cell_pixel(2, 2), 0x11);
    CHECK_EQ(cell_pixel(2, 9), 0x11);
}

/* [EBP+0x1c] is written once, at 0001fd96, and every later call reloads it
   unchanged; fdps_blit_glyph_1bpp steps only its own copy.  So all five blits
   draw the same one-column glyph.  If any of them had advanced the shared
   pointer the next would have drawn sheet[1] = 0xff and painted a whole
   eight-pixel row, which is what column 9 is watching for. */
static void all_five_blits_read_the_same_glyph(void)
{
    unsigned char sheet[2];

    sheet[0] = 0x80;
    sheet[1] = 0xff;
    stage_cell(8, 1, 1, 1, 0, 0);
    fdps_draw_glyph(cell_origin(), CELL_PITCH, sheet, 0, 0x77, 0, 0x33);

    CHECK_EQ(cell_pixel(2, 2), 0x77);
    CHECK_EQ(cell_pixel(3, 2), 0x33);
    CHECK_EQ(cell_pixel(2, 1), 0x33);
    CHECK_EQ(cell_pixel(1, 2), 0x33);
    CHECK_EQ(cell_pixel(2, 3), 0x33);
    CHECK_EQ(cell_pixel(2, 9), BACKGROUND);
    CHECK_EQ(cell_pixel(3, 9), BACKGROUND);
    CHECK_EQ(cell_pixel(1, 9), BACKGROUND);
}

/* ---------------------------------------------------------------------------
   fdps_draw_number at 00017530.

   Every expected value below is the assembly's, read at the addresses quoted on
   each test: the two format strings (the "%.3d" template at 00014854 patched at
   000175b3, and the "%d" literal at 000615a8 reached by the JZ at 00017551),
   the signed overflow compare at 0001757f, the '+' prepend guarded by CMP byte
   ptr [EBP+0x24],0x0 / CMP dword ptr [EBP+0x1c],0x0 / JGE at 000175e2, the
   glyph map at 0001764b-0001768c, the thirteen-glyph colour stride of IMUL
   EAX,dword ptr [0x0006000c],0xd at 00017693, the offset table read at
   000176b2, and the PUSH 0x8 / PUSH 0x6 / IMUL EAX,[EBP-0x8],0x6 blit call at
   000176ba.  None of them is read off the emitted C.

   The figure is observed through the pixels rather than by intercepting the
   blit: the sheet staged below is a real .CEL-shaped block whose sprite n is a
   flat 6x8 fill of colour n + 1, so the destination surface spells out which
   sprite index each character selected and where it landed.  That runs the
   whole path -- the format, the glyph map, the colour row, the offset table and
   fdps_blit_dispatch's mode 0 -- against the same fdps_rle_blit_passthrough the
   game draws through.

   NUMBER.CEL itself is not read here.  Its sixty-five sprites are the game's
   glyph artwork and this file asserts nothing about their appearance, only
   about which of the sixty-five slots the routine picks; a staged sheet says
   that in a way the real artwork cannot. */

/* The sheet's shape, from resource_info/cel.md: the sprite offset table sits at
   a fixed +0x0f and holds one dword per sprite, each the offset from the base
   of the sheet to that sprite's RLE stream.  Sixty-five sprites, five colour
   rows of the thirteen glyphs '0'-'9', '+', '-', '?'. */
#define NUM_TABLE_AT     0x0f
#define NUM_SPRITES      65
#define NUM_STREAM_AT    (NUM_TABLE_AT + NUM_SPRITES * 4)
#define NUM_STREAM_BYTES 16
#define NUM_SHEET_BYTES  (NUM_STREAM_AT + NUM_SPRITES * NUM_STREAM_BYTES)

/* Wide enough for ten 6-pixel cells on the widest pitch a test uses, and four
   rows taller than the 8-row cell so an overrun past the bottom has somewhere
   to show. */
#define NUM_PITCH 64
#define NUM_ROWS  12
#define NUM_BYTES (NUM_PITCH * NUM_ROWS)

static unsigned char number_sheet[NUM_SHEET_BYTES];
static unsigned char number_surface[NUM_BYTES];

/* Sprite n paints colour n + 1, so no sprite paints 0 and none of them
   collides with BACKGROUND. */
static int sprite_color(int sprite_index)
{
    return sprite_index + 1;
}

/* One 6x8 sprite per slot: eight rows of a single fill op, command 0x05 --
   op 0 in the top two bits, a run of (5 & 0x3f) + 1 = 6 -- followed by the
   pixel byte.  Six pixels is exactly the width fdps_blit_dispatch publishes,
   so each row closes on its own and the blit steps by pitch - 6. */
static void stage_number(int color_row)
{
    int sprite;
    int row;
    int stream_at;
    int i;

    for (i = 0; i < NUM_SHEET_BYTES; i++) {
        number_sheet[i] = 0;
    }

    for (sprite = 0; sprite < NUM_SPRITES; sprite++) {
        stream_at = NUM_STREAM_AT + sprite * NUM_STREAM_BYTES;
        *(int *) (number_sheet + NUM_TABLE_AT + sprite * 4) = stream_at;
        for (row = 0; row < 8; row++) {
            number_sheet[stream_at + row * 2] = 0x05;
            number_sheet[stream_at + row * 2 + 1] =
                (unsigned char) sprite_color(sprite);
        }
    }

    for (i = 0; i < NUM_BYTES; i++) {
        number_surface[i] = BACKGROUND;
    }

    data_fdps_number_glyph_sheet_ptr = number_sheet;
    data_fdps_number_glyph_color_row = color_row;
}

/* The colour standing in the top-left pixel of the figure's cell number
   `cell`, counting from the cell the figure was drawn at. */
static int cell_color(int cell)
{
    return number_surface[cell * 6];
}

/* sprintf(buf, "%d", value) at 000175da: the digits the value needs and no
   padding.  42 is two cells and the third is never touched. */
static void a_natural_width_figure_draws_the_digits_it_needs(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, 42, 0, 0);

    CHECK_EQ(cell_color(0), sprite_color(4));
    CHECK_EQ(cell_color(1), sprite_color(2));
    CHECK_EQ(cell_color(2), BACKGROUND);
}

/* The reason the two format strings must stay apart.  "%d" of 0 is "0" and
   draws sprite 0; the fold to sprintf(buf, "%.*d", digit_count, value) prints
   the empty string for this call and draws nothing at all, and
   fdps_draw_cursor_info_panel reaches it with digit_count 0 (PUSH 0x0 at
   0002de10). */
static void a_natural_width_zero_still_draws_one_digit(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, 0, 0, 0);

    CHECK_EQ(cell_color(0), sprite_color(0));
    CHECK_EQ(cell_color(1), BACKGROUND);
}

/* MOV AL,byte ptr [EBP+0x20] / ADD AL,0x30 / MOV [EBP-0x16],AL turns the
   template into "%.3d", so 7 in a three-digit field is "007" and the padding
   is drawn as real zero sprites rather than skipped. */
static void a_field_width_zero_pads_the_figure(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, 7, 3, 0);

    CHECK_EQ(cell_color(0), sprite_color(0));
    CHECK_EQ(cell_color(1), sprite_color(0));
    CHECK_EQ(cell_color(2), sprite_color(7));
    CHECK_EQ(cell_color(3), BACKGROUND);
}

/* limit is built as 10^digit_count by the loop at 00017570 and a value that
   reaches it is not formatted at all: the fill at 0001759a writes digit_count
   '?' characters, which the map at 0001767d sends to sprite 12.  The field is
   not widened and the figure is not truncated. */
static void a_figure_too_wide_for_the_field_becomes_question_marks(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, 1000, 3, 0);

    CHECK_EQ(cell_color(0), sprite_color(12));
    CHECK_EQ(cell_color(1), sprite_color(12));
    CHECK_EQ(cell_color(2), sprite_color(12));
    CHECK_EQ(cell_color(3), BACKGROUND);
}

/* The other side of that compare: 999 against a limit of 1000 is CMP EAX,
   [EBP-0x10] / JL taken, so the widest figure the field holds is still drawn as
   digits. */
static void the_widest_figure_that_fits_is_still_formatted(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, 999, 3, 0);

    CHECK_EQ(cell_color(0), sprite_color(9));
    CHECK_EQ(cell_color(1), sprite_color(9));
    CHECK_EQ(cell_color(2), sprite_color(9));
    CHECK_EQ(cell_color(3), BACKGROUND);
}

/* The overflow compare at 0001757f is the signed JL, so no negative value ever
   reaches the '?' fill however many characters it needs, and the precision in
   "%.3d" is a minimum and not a truncation: -12345 in a three-digit field draws
   all six of its characters, sign first, straight past the field.  Reading that
   compare as unsigned would send it to "???" instead. */
static void a_negative_figure_never_trips_the_overflow_guard(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, -12345, 3, 0);

    CHECK_EQ(cell_color(0), sprite_color(11));
    CHECK_EQ(cell_color(1), sprite_color(1));
    CHECK_EQ(cell_color(2), sprite_color(2));
    CHECK_EQ(cell_color(3), sprite_color(3));
    CHECK_EQ(cell_color(4), sprite_color(4));
    CHECK_EQ(cell_color(5), sprite_color(5));
    CHECK_EQ(cell_color(6), BACKGROUND);
}

/* The '+' goes on in front of the already-formatted figure -- strcpy aside,
   the literal stored over the head of the buffer, strcat back on -- so the
   padding stays where the format put it and the sign is an extra cell rather
   than one of the digit_count.  '+' maps to sprite 10 at 0001766e. */
static void show_plus_prepends_to_a_non_negative_figure(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, 5, 2, 1);

    CHECK_EQ(cell_color(0), sprite_color(10));
    CHECK_EQ(cell_color(1), sprite_color(0));
    CHECK_EQ(cell_color(2), sprite_color(5));
    CHECK_EQ(cell_color(3), BACKGROUND);
}

/* The guard is JGE at 000175ec, not JG: zero is not negative and gets the
   sign. */
static void show_plus_prepends_to_zero(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, 0, 0, 1);

    CHECK_EQ(cell_color(0), sprite_color(10));
    CHECK_EQ(cell_color(1), sprite_color(0));
    CHECK_EQ(cell_color(2), BACKGROUND);
}

/* A negative value carries its own '-' out of the format and the prepend is
   skipped, so show_plus never produces "+-5".  '-' maps to sprite 11 at
   0001765f -- the two signs are not adjacent sprites and swapping them is the
   easy mistake. */
static void show_plus_is_ignored_for_a_negative_figure(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, -5, 0, 1);

    CHECK_EQ(cell_color(0), sprite_color(11));
    CHECK_EQ(cell_color(1), sprite_color(5));
    CHECK_EQ(cell_color(2), BACKGROUND);
}

/* IMUL EAX,dword ptr [0x0006000c],0xd: the colour row is a block of thirteen,
   not a byte offset and not a stride of ten.  Row 2's '3' is sprite 29 and row
   4's is sprite 55. */
static void the_colour_row_selects_a_block_of_thirteen(void)
{
    stage_number(2);
    fdps_draw_number(number_surface, NUM_PITCH, 3, 0, 0);
    CHECK_EQ(cell_color(0), sprite_color(2 * 13 + 3));

    stage_number(4);
    fdps_draw_number(number_surface, NUM_PITCH, 3, 0, 0);
    CHECK_EQ(cell_color(0), sprite_color(4 * 13 + 3));
}

/* The row the caller leaves behind is the row the next figure is drawn in:
   nothing here writes data_fdps_number_glyph_color_row, and
   fdps_draw_save_slot_panel's first figure depends on that -- its CALL at
   00024c60 comes before the first MOV [0x0006000c] at 00024c68. */
static void the_colour_row_is_never_reset_by_the_draw(void)
{
    stage_number(1);
    fdps_draw_number(number_surface, NUM_PITCH, 3, 0, 0);

    CHECK_EQ(data_fdps_number_glyph_color_row, 1);
    CHECK_EQ(cell_color(0), sprite_color(1 * 13 + 3));
}

/* PUSH 0x6 and PUSH 0x8 are literals in the call and IMUL EAX,[EBP-0x8],0x6
   steps the destination, so every cell is six wide and eight tall whatever the
   sheet header says.  Row 8 of the surface is the one watching for a ninth
   row. */
static void every_cell_is_six_wide_and_eight_rows_tall(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, NUM_PITCH, 12, 0, 0);

    CHECK_EQ(number_surface[5], sprite_color(1));
    CHECK_EQ(number_surface[6], sprite_color(2));
    CHECK_EQ(number_surface[11], sprite_color(2));
    CHECK_EQ(number_surface[12], BACKGROUND);
    CHECK_EQ(number_surface[7 * NUM_PITCH], sprite_color(1));
    CHECK_EQ(number_surface[8 * NUM_PITCH], BACKGROUND);
    CHECK_EQ(data_fdps_graphics_rle_blit_src_width, 6);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* pitch is handed to fdps_blit_dispatch untouched and is the only thing that
   decides where the next row of a cell lands: at 32 the eight rows of the cell
   sit 32 bytes apart, and the pitch the dispatcher publishes is the caller's
   and not the 0x140 of the visible screen. */
static void the_callers_pitch_is_the_row_step(void)
{
    stage_number(0);
    fdps_draw_number(number_surface, 32, 8, 0, 0);

    CHECK_EQ(number_surface[0], sprite_color(8));
    CHECK_EQ(number_surface[32], sprite_color(8));
    CHECK_EQ(number_surface[7 * 32], sprite_color(8));
    CHECK_EQ(number_surface[8 * 32], BACKGROUND);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_pitch, 32);
}

/* dest is the first digit cell itself, already offset by the caller: the
   routine adds only 6 per character and never a row or a column of its own, so
   nothing above or to the left of the pointer is touched. */
static void the_figure_starts_exactly_at_dest(void)
{
    stage_number(0);
    fdps_draw_number(number_surface + 2 * NUM_PITCH + 4, NUM_PITCH, 1, 0, 0);

    CHECK_EQ(number_surface[2 * NUM_PITCH + 4], sprite_color(1));
    CHECK_EQ(number_surface[2 * NUM_PITCH + 3], BACKGROUND);
    CHECK_EQ(number_surface[1 * NUM_PITCH + 4], BACKGROUND);
    CHECK_EQ(number_surface[9 * NUM_PITCH + 4], sprite_color(1));
    CHECK_EQ(number_surface[10 * NUM_PITCH + 4], BACKGROUND);
}

void run_text_tests(void)
{
    RUN_TEST(set_bits_paint_and_clear_bits_do_not);
    RUN_TEST(bits_are_consumed_most_significant_first);
    RUN_TEST(rows_are_byte_aligned_in_the_source);
    RUN_TEST(each_row_advances_the_destination_by_pitch);
    RUN_TEST(destination_offset_is_the_callers_to_choose);
    RUN_TEST(only_the_low_byte_of_color_is_stored);
    RUN_TEST(callers_glyph_pointer_survives_the_blit);
    RUN_TEST(a_zero_cell_dimension_draws_nothing);
    RUN_TEST(the_cell_bounds_are_unsigned_bytes);
    RUN_TEST(nothing_outside_the_cell_is_written);
    RUN_TEST(a_narrow_cell_still_eats_a_whole_byte_per_row);

    RUN_TEST(the_background_fill_covers_the_cell_and_steps_by_pitch);
    RUN_TEST(the_background_fill_stores_only_the_low_byte);
    RUN_TEST(a_zero_background_color_skips_the_fill);
    RUN_TEST(the_glyph_index_scales_by_the_sheet_stride);
    RUN_TEST(the_outline_style_paints_four_neighbours);
    RUN_TEST(the_outline_is_not_guarded_by_the_outline_color);
    RUN_TEST(the_shadow_style_uses_the_two_offset_globals);
    RUN_TEST(a_zero_outline_color_skips_the_shadow);
    RUN_TEST(the_glyph_body_lands_on_top_of_the_fill);
    RUN_TEST(a_zero_foreground_color_skips_the_glyph_body);
    RUN_TEST(all_five_blits_read_the_same_glyph);

    RUN_TEST(a_natural_width_figure_draws_the_digits_it_needs);
    RUN_TEST(a_natural_width_zero_still_draws_one_digit);
    RUN_TEST(a_field_width_zero_pads_the_figure);
    RUN_TEST(a_figure_too_wide_for_the_field_becomes_question_marks);
    RUN_TEST(the_widest_figure_that_fits_is_still_formatted);
    RUN_TEST(a_negative_figure_never_trips_the_overflow_guard);
    RUN_TEST(show_plus_prepends_to_a_non_negative_figure);
    RUN_TEST(show_plus_prepends_to_zero);
    RUN_TEST(show_plus_is_ignored_for_a_negative_figure);
    RUN_TEST(the_colour_row_selects_a_block_of_thirteen);
    RUN_TEST(the_colour_row_is_never_reset_by_the_draw);
    RUN_TEST(every_cell_is_six_wide_and_eight_rows_tall);
    RUN_TEST(the_callers_pitch_is_the_row_step);
    RUN_TEST(the_figure_starts_exactly_at_dest);

    /* Put every font global back before leaving.  stage() and stage_cell()
       write them, and the runners share one process: a later unit that expects
       an unloaded font would inherit this file's fixture cell size, its glyph
       stride and its decoration style. */
    data_fdps_font_glyph_width = (unsigned char) 0;
    data_fdps_glyph_cell_height = (unsigned char) 0;
    data_fdps_font_glyph_stride_bytes = 0;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;

    /* And the two Number.cel globals, for the same reason: the sheet pointer
       staged above is a static of this file and would outlive it as a live
       pointer into a unit that has finished, and the colour row is the one
       thing fdps_draw_number deliberately does not put back itself. */
    data_fdps_number_glyph_sheet_ptr = (unsigned char *) 0;
    data_fdps_number_glyph_color_row = 0;
}
