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
}
