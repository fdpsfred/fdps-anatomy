/* tests/rlemix.c -- cover for src/rlemix.asm: the translucent, tinting and colour-range kernels.
 *
 * The kernels in rlemix.asm are hand-written assembly transcribed from the
 * original with no C interface -- they take their inputs in registers and out
 * of fdps_blit_dispatch's own stack frame -- so every case here calls
 * fdps_blit_dispatch with the kernel's mode.  The cases carry over every
 * situation the C-translation tests in tests/rleblend.c verify (those are kept
 * for reference under #if 0), and none of them depends on which spelling of
 * the kernels is linked (rebuild_info/emit_pipeline.md).
 */
#include "testharn.h"
#include "blit.h"
#include "gamedata.h"
#include "rleblend.h"

/* --- fdps_rle_blit_translucent (0005761b), blit mode 9 --------------------
 *
 * Expected values are read off the assembly at 0005761b: the op selector is
 * SHL CL,1 / JC taken twice and the length SHR CL,2 / INC CL, the row ends on
 * OR BX,BX / JNZ, the row advance is MOV [dst_row_advance],EDX at entry and ADD
 * EDI,[dst_row_advance] at 0005777f, the row choice is CMP ECX,0x8 / JBE with
 * the SUB ECX,0x10 / NEG ECX / XCHG EDX,EAX arm and the 0x2400 byte offset that
 * is nine rows of 256 dwords, and the blend is ADD EAX,EDX / SHR EAX,0x4 / AND
 * 0xf0f0f / (AND 0xffff | SHR 0xc) at 000576a5..000576b5.
 *
 * EDX on entry is the dispatcher's dest_pitch - src_width, so a width w and
 * advance a is a dispatch with src_width w and dest_pitch a + w.  The mode
 * operand is the three-dword descriptor the kernel reads at [EBP+0x1c]:
 * ramp, level, cube.
 *
 * The ramp is the full 18 x 256 the kernel indexes and every entry a case does
 * not mark is zero; the cube is filled with TRAN_CUBE_GUARD and only the
 * entries a case expects are marked, so a kernel that formed any other index
 * writes the guard and the assertion fails with it.  The destination is
 * pre-filled with TRAN_SENTINEL, which is both the byte that says nothing was
 * written there and the palette index the blend reads out of the destination
 * weight row.
 *
 * WHERE THE INDICES COME FROM.  0x537 is what the assembly's own sequence
 * makes of the sum 0x00305070: SHR 4 gives 0x00030507, AND 0xf0f0f leaves it
 * alone, AND 0xffff gives 0x0507 and SHR 0xc gives 0x30, so the OR is 0x537.
 * 0x5f7 is the same sequence over 0x00f05070, and 0x33 over 0x00300030.
 */
#define TRAN_MODE 9

#define TRAN_RAMP_ROWS 18
#define TRAN_RAMP_ROW_ENTRIES 256
#define TRAN_RAMP_ENTRIES (TRAN_RAMP_ROWS * TRAN_RAMP_ROW_ENTRIES)
#define TRAN_CUBE_BYTES 4096

#define TRAN_CUBE_GUARD 0xff
#define TRAN_SENTINEL 0x5a

#define TRAN_SRC_PIXEL 0x20
#define TRAN_SRC_PIXEL_2 0x21

#define TRAN_WEIGHT_SRC_A 0x00300000
#define TRAN_WEIGHT_SRC_B 0x00f00000
#define TRAN_WEIGHT_DST_A 0x00005070
#define TRAN_WEIGHT_DST_B 0x00000030
#define TRAN_INDEX_A_A 0x537 /* (0x00300000 + 0x00005070) */
#define TRAN_INDEX_B_A 0x5f7 /* (0x00f00000 + 0x00005070) */
#define TRAN_INDEX_A_B 0x33  /* (0x00300000 + 0x00000030) */

#define TRAN_PAINT_A_A 0x2a
#define TRAN_PAINT_B_A 0x2c
#define TRAN_PAINT_A_B 0x2b

#define TRAN_DEST_BYTES 80

static unsigned int tran_ramp[TRAN_RAMP_ENTRIES];
static unsigned char tran_cube[TRAN_CUBE_BYTES];
static unsigned char tran_surface[TRAN_DEST_BYTES];
static int tran_descriptor[3];

static void tran_set_ramp(int weight_row, int entry, unsigned int weighted_color)
{
    tran_ramp[weight_row * TRAN_RAMP_ROW_ENTRIES + entry] = weighted_color;
}

/* Clears the tables and the surface and fills the descriptor.  The rectangle
   is not set here: fdps_blit_dispatch publishes it from its own arguments. */
static void tran_setup(unsigned int level)
{
    int entry_index;

    for (entry_index = 0; entry_index < TRAN_RAMP_ENTRIES; entry_index++) {
        tran_ramp[entry_index] = 0;
    }
    for (entry_index = 0; entry_index < TRAN_CUBE_BYTES; entry_index++) {
        tran_cube[entry_index] = TRAN_CUBE_GUARD;
    }
    for (entry_index = 0; entry_index < TRAN_DEST_BYTES; entry_index++) {
        tran_surface[entry_index] = TRAN_SENTINEL;
    }

    tran_descriptor[0] = (int) tran_ramp;
    tran_descriptor[1] = (int) level;
    tran_descriptor[2] = (int) tran_cube;
}

static void tran_blit(unsigned char *stream, int src_width, int src_rows,
                      int row_advance)
{
    fdps_blit_dispatch(stream, tran_surface, src_width, src_rows,
                       row_advance + src_width,
                       (unsigned int) tran_descriptor, TRAN_MODE);
}

/* Level 0 puts the source in row 9 and the destination in row 0, and the two
   decoys sit at the rows a kernel that swapped them would read: that pairing
   makes 0x00f00030, whose index 0xf3 is never marked. */
static void tran_level_zero_rows(void)
{
    tran_set_ramp(9, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_A);
    tran_set_ramp(0, TRAN_SENTINEL, TRAN_WEIGHT_DST_A);
    tran_set_ramp(0, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_B);
    tran_set_ramp(9, TRAN_SENTINEL, TRAN_WEIGHT_DST_B);
    tran_cube[TRAN_INDEX_A_A] = TRAN_PAINT_A_A;
}

/* Op 00, length (0x01 & 0x3f) + 1 = 2: the pixel byte is weighted once and
   blended into two consecutive destination bytes.  The third byte is outside
   the run and outside the row, and the DEC WORD at 00057785 leaves the row
   count 0. */
static void tran_fill_run_blends_len_bytes(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = TRAN_SRC_PIXEL;
    tran_setup(0);
    tran_level_zero_rows();
    tran_blit(stream, 2, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[1], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[2], TRAN_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The second term of the add is the destination byte under the cursor (MOV
   AL,[EDI] at 0005769b), not a constant: one source pixel over two different
   destination bytes comes out as two different palette indices. */
static void tran_the_destination_byte_is_the_second_term(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = TRAN_SRC_PIXEL;
    tran_setup(0);
    tran_set_ramp(9, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_A);
    tran_set_ramp(0, 0x11, TRAN_WEIGHT_DST_A);
    tran_set_ramp(0, 0x22, TRAN_WEIGHT_DST_B);
    tran_cube[TRAN_INDEX_A_A] = TRAN_PAINT_A_A;
    tran_cube[TRAN_INDEX_A_B] = TRAN_PAINT_A_B;
    tran_surface[0] = 0x11;
    tran_surface[1] = 0x22;
    tran_blit(stream, 2, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[1], TRAN_PAINT_A_B);
}

/* At level 8 or below the source is read from row level + 9 and the
   destination from row level: 0x2400 is a byte offset added after the SHL
   ECX,0xa row scale, so it is nine rows of 256 dwords.  Level 3 marks rows 12
   and 3 and puts the decoys at 3 and 12; level 8 is the last level on the JBE
   arm and reaches row 17, the last row of the table. */
static void tran_level_below_nine_reads_the_source_nine_rows_on(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TRAN_SRC_PIXEL;
    tran_setup(3);
    tran_set_ramp(12, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_A);
    tran_set_ramp(3, TRAN_SENTINEL, TRAN_WEIGHT_DST_A);
    tran_set_ramp(3, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_B);
    tran_set_ramp(12, TRAN_SENTINEL, TRAN_WEIGHT_DST_B);
    tran_cube[TRAN_INDEX_A_A] = TRAN_PAINT_A_A;
    tran_blit(stream, 1, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[1], TRAN_SENTINEL);

    tran_setup(8);
    tran_set_ramp(17, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_B);
    tran_set_ramp(8, TRAN_SENTINEL, TRAN_WEIGHT_DST_A);
    tran_cube[TRAN_INDEX_B_A] = TRAN_PAINT_B_A;
    tran_blit(stream, 1, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_PAINT_B_A);
}

/* Above 8 the row index is 16 - level (SUB ECX,0x10 / NEG ECX) and XCHG moves
   the nine-row offset to the destination: level 12 reads the source from row
   4 and the destination from row 13, level 9 from rows 7 and 16, and level 16
   from rows 0 and 9.  The level 12 case puts its decoys at rows 13 and 4,
   where a kernel that kept the offsets on the same side would look. */
static void tran_level_above_eight_folds_to_sixteen_minus_it(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TRAN_SRC_PIXEL;

    tran_setup(12);
    tran_set_ramp(4, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_A);
    tran_set_ramp(13, TRAN_SENTINEL, TRAN_WEIGHT_DST_A);
    tran_set_ramp(13, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_B);
    tran_set_ramp(4, TRAN_SENTINEL, TRAN_WEIGHT_DST_B);
    tran_cube[TRAN_INDEX_A_A] = TRAN_PAINT_A_A;
    tran_blit(stream, 1, 1, 0);
    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_A);

    tran_setup(9);
    tran_set_ramp(7, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_B);
    tran_set_ramp(16, TRAN_SENTINEL, TRAN_WEIGHT_DST_A);
    tran_cube[TRAN_INDEX_B_A] = TRAN_PAINT_B_A;
    tran_blit(stream, 1, 1, 0);
    CHECK_EQ(tran_surface[0], TRAN_PAINT_B_A);

    tran_setup(16);
    tran_set_ramp(0, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_A);
    tran_set_ramp(9, TRAN_SENTINEL, TRAN_WEIGHT_DST_B);
    tran_cube[TRAN_INDEX_A_B] = TRAN_PAINT_A_B;
    tran_blit(stream, 1, 1, 0);
    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_B);
}

/* The cube is green-major.  The sum 0x00305070 folds to 0x00030507, red 3,
   green 5, blue 7, and (v >> 12) | (v & 0xffff) puts that at 0x537 -- green *
   256 + red * 16 + blue.  The r:g:b spelling would read 0x357, which is marked
   with a different byte here. */
static void tran_the_cube_index_is_green_major(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TRAN_SRC_PIXEL;
    tran_setup(0);
    tran_level_zero_rows();
    tran_cube[0x357] = 0x3c;
    tran_blit(stream, 1, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_A);
}

/* Op 01 (0x41, length 2): INC EDI, then the blend, then STOSB, twice, so bytes
   1 and 3 are blended and bytes 0 and 2 are left as they were.  The row is
   four bytes wide because the op subtracts the length twice (SUB BX,CX at
   000576d1 and 000576d4). */
static void tran_stretched_run_blends_the_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = TRAN_SRC_PIXEL;
    tran_setup(0);
    tran_level_zero_rows();
    tran_blit(stream, 4, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_SENTINEL);
    CHECK_EQ(tran_surface[1], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[2], TRAN_SENTINEL);
    CHECK_EQ(tran_surface[3], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[4], TRAN_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 10 (0x81, length 2): the LODSB is inside the loop at 0005772f, so the two
   bytes after the command byte are weighted one per destination byte, in
   order, and two different source pixels come out as two different indices. */
static void tran_literal_run_blends_each_stream_byte(void)
{
    unsigned char stream[3];

    stream[0] = 0x81;
    stream[1] = TRAN_SRC_PIXEL;
    stream[2] = TRAN_SRC_PIXEL_2;
    tran_setup(0);
    tran_set_ramp(9, TRAN_SRC_PIXEL, TRAN_WEIGHT_SRC_A);
    tran_set_ramp(9, TRAN_SRC_PIXEL_2, TRAN_WEIGHT_SRC_B);
    tran_set_ramp(0, TRAN_SENTINEL, TRAN_WEIGHT_DST_A);
    tran_cube[TRAN_INDEX_A_A] = TRAN_PAINT_A_A;
    tran_cube[TRAN_INDEX_B_A] = TRAN_PAINT_B_A;
    tran_blit(stream, 2, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[1], TRAN_PAINT_B_A);
    CHECK_EQ(tran_surface[2], TRAN_SENTINEL);
}

/* Op 11 (0xc1, length 2) is ADD EDI,ECX at 00057771 with no write and no read:
   the two bytes it covers keep the surface's own content and the fill that
   follows lands two bytes further on. */
static void tran_skip_run_leaves_the_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = TRAN_SRC_PIXEL;
    tran_setup(0);
    tran_level_zero_rows();
    tran_blit(stream, 4, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_SENTINEL);
    CHECK_EQ(tran_surface[1], TRAN_SENTINEL);
    CHECK_EQ(tran_surface[2], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[3], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[4], TRAN_SENTINEL);
}

/* The dispatcher's pitch - width (5 - 2 = 3) is published into the global on
   entry and read back from it at every row end, so the second row starts three
   bytes past where the first one stopped and the global still holds the
   advance afterwards.  The row count is consumed to zero and the width is
   re-read (MOV BX,[src_width] at 00057669) for the second row. */
static void tran_the_row_advance_is_published_and_applied(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = TRAN_SRC_PIXEL;
    stream[2] = 0x01;
    stream[3] = TRAN_SRC_PIXEL;
    tran_setup(0);
    tran_level_zero_rows();
    tran_blit(stream, 2, 2, 3);

    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[1], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[2], TRAN_SENTINEL);
    CHECK_EQ(tran_surface[4], TRAN_SENTINEL);
    CHECK_EQ(tran_surface[5], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[6], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[7], TRAN_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 3);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* SHR CL,2 / INC CL over a command byte whose low six bits are all set: the
   longest run the format encodes is 64 pixels and zero cannot be encoded. */
static void tran_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = TRAN_SRC_PIXEL;
    tran_setup(0);
    tran_level_zero_rows();
    tran_blit(stream, 64, 1, 0);

    CHECK_EQ(tran_surface[0], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[63], TRAN_PAINT_A_A);
    CHECK_EQ(tran_surface[64], TRAN_SENTINEL);
}

/* --- fdps_rle_blit_tint_sprite_and_backdrop (00057793), blit mode 10 ------
 *
 * Same four-op stream and same fold as mode 9, but the second term of every
 * add is a single constant fetched before any drawing (MOV EAX,[EBP+0x1c] /
 * SHL EAX,0x2 / ADD EAX,[EBP+0xc] / MOV EDX,[EAX] at 000577e5), and the roles
 * of the two ramp rows are the other way up from mode 9: the TINT is weighted
 * through the row the level names and the PIXEL through the row nine on, which
 * is the assignment MOV [EBP+0xc],EAX / MOV [EBP+0x10],EDX makes at 000577cd
 * with EAX and EDX carrying 0 and 0x2400 for a level of 8 or less and the two
 * exchanged by XCHG EDX,EAX above that.  The mode operand is a four-dword
 * descriptor: ramp ([EAX]), level ([EAX+4]), cube ([EAX+8]) and the tint
 * palette index ([EAX+0xc], read at 0005779c).
 *
 * EDX on entry is the dispatcher's dest_pitch - src_width, so a width w and
 * advance a is a dispatch with src_width w and dest_pitch a + w.
 *
 * The fixtures mirror the mode 9 block's but are this block's own: the ramp is
 * the full 18 x 256 and every unmarked entry is zero, the cube is filled with
 * TSB_CUBE_GUARD and only expected entries are marked, and the surface is
 * pre-filled with TSB_SENTINEL.  The cube indices are the ones the assembly's
 * sequence at 00057822..00057835 forms: 0x537 from 0x00305070, 0x5f7 from
 * 0x00f05070 and 0x33 from 0x00300030.
 */
#define TSB_MODE 10

#define TSB_RAMP_ROWS 18
#define TSB_RAMP_ROW_ENTRIES 256
#define TSB_RAMP_ENTRIES (TSB_RAMP_ROWS * TSB_RAMP_ROW_ENTRIES)
#define TSB_CUBE_BYTES 4096

#define TSB_CUBE_GUARD 0xff
#define TSB_SENTINEL 0x5a

#define TSB_SRC_PIXEL 0x20
#define TSB_SRC_PIXEL_2 0x21

/* The palette index the whole rectangle is tinted toward.  Distinct from both
   source pixels and from the sentinel, so a kernel that indexed the ramp with
   the wrong one of them lands on an unmarked entry. */
#define TSB_TINT_COLOR 0x30

#define TSB_WEIGHT_SRC_A 0x00300000
#define TSB_WEIGHT_SRC_B 0x00f00000
#define TSB_WEIGHT_DST_A 0x00005070
#define TSB_WEIGHT_DST_B 0x00000030
#define TSB_INDEX_A_A 0x537 /* (0x00300000 + 0x00005070) */
#define TSB_INDEX_B_A 0x5f7 /* (0x00f00000 + 0x00005070) */
#define TSB_INDEX_A_B 0x33  /* (0x00300000 + 0x00000030) */

#define TSB_PAINT_A_A 0x2a
#define TSB_PAINT_B_A 0x2c
#define TSB_PAINT_A_B 0x2b

#define TSB_DEST_BYTES 80

static unsigned int tsb_ramp[TSB_RAMP_ENTRIES];
static unsigned char tsb_cube[TSB_CUBE_BYTES];
static unsigned char tsb_surface[TSB_DEST_BYTES];
static int tsb_descriptor[4];

static void tsb_set_ramp(int weight_row, int entry, unsigned int weighted_color)
{
    tsb_ramp[weight_row * TSB_RAMP_ROW_ENTRIES + entry] = weighted_color;
}

/* Clears the tables and the surface and fills the descriptor.  The rectangle
   is not set here: fdps_blit_dispatch publishes it from its own arguments. */
static void tsb_setup(unsigned int level)
{
    int entry_index;

    for (entry_index = 0; entry_index < TSB_RAMP_ENTRIES; entry_index++) {
        tsb_ramp[entry_index] = 0;
    }
    for (entry_index = 0; entry_index < TSB_CUBE_BYTES; entry_index++) {
        tsb_cube[entry_index] = TSB_CUBE_GUARD;
    }
    for (entry_index = 0; entry_index < TSB_DEST_BYTES; entry_index++) {
        tsb_surface[entry_index] = TSB_SENTINEL;
    }

    tsb_descriptor[0] = (int) tsb_ramp;
    tsb_descriptor[1] = (int) level;
    tsb_descriptor[2] = (int) tsb_cube;
    tsb_descriptor[3] = TSB_TINT_COLOR;
}

static void tsb_blit(unsigned char *stream, int src_width, int src_rows,
                     int row_advance)
{
    fdps_blit_dispatch(stream, tsb_surface, src_width, src_rows,
                       row_advance + src_width,
                       (unsigned int) tsb_descriptor, TSB_MODE);
}

/* Level 0 weights the tint through row 0 and the pixel through row 9, and the
   two decoys sit at the rows a kernel that swapped them would read: that
   pairing makes 0x00f00030, whose index 0xf3 is never marked. */
static void tsb_level_zero_rows(void)
{
    tsb_set_ramp(9, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_A);
    tsb_set_ramp(0, TSB_TINT_COLOR, TSB_WEIGHT_DST_A);
    tsb_set_ramp(0, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_B);
    tsb_set_ramp(9, TSB_TINT_COLOR, TSB_WEIGHT_DST_B);
    tsb_cube[TSB_INDEX_A_A] = TSB_PAINT_A_A;
}

/* Op 00, length (0x01 & 0x3f) + 1 = 2: one stream byte weighted once, added to
   the constant tint once, and the resulting palette index written to two
   consecutive bytes by REP STOSB at 0005783a.  The DEC WORD at 00057908 leaves
   the row count 0. */
static void tsb_fill_run_paints_len_bytes(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = TSB_SRC_PIXEL;
    tsb_setup(0);
    tsb_level_zero_rows();
    tsb_blit(stream, 2, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[1], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[2], TSB_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Unlike mode 9 the fill never reads the surface: the blend is computed before
   the REP and the same byte goes to every pixel of the run.  Two different
   destination bytes under the run therefore come out identical, and the weight
   parked at one of those bytes is what a kernel that read [EDI] would pick up
   instead. */
static void tsb_fill_ignores_the_destination_byte(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = TSB_SRC_PIXEL;
    tsb_setup(0);
    tsb_level_zero_rows();
    tsb_set_ramp(9, 0x11, TSB_WEIGHT_SRC_B);
    tsb_cube[TSB_INDEX_B_A] = TSB_PAINT_B_A;
    tsb_surface[0] = 0x11;
    tsb_surface[1] = 0x22;
    tsb_blit(stream, 2, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[1], TSB_PAINT_A_A);
}

/* The op that separates this kernel from the rest of the family.  Op 11 (0xc1,
   length 2) consumes no stream byte and does not skip: MOV AL,[EDI] at
   000578d4 weights the pixel already on the surface through the pixel row,
   adds the tint and writes the result back.  The op 00 command that follows
   must therefore be read from stream[1]. */
static void tsb_backdrop_run_tints_the_surface(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = TSB_SRC_PIXEL;
    tsb_setup(0);
    tsb_level_zero_rows();
    tsb_set_ramp(9, TSB_SENTINEL, TSB_WEIGHT_SRC_B);
    tsb_cube[TSB_INDEX_B_A] = TSB_PAINT_B_A;
    tsb_blit(stream, 4, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_B_A);
    CHECK_EQ(tsb_surface[1], TSB_PAINT_B_A);
    CHECK_EQ(tsb_surface[2], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[3], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[4], TSB_SENTINEL);
}

/* And it reads each destination byte in turn (the MOV AL,[EDI] is inside the
   LOOP at 000578f7): two different backdrop bytes come out as two different
   palette indices. */
static void tsb_backdrop_run_reads_each_destination_byte(void)
{
    unsigned char stream[1];

    stream[0] = 0xc1;
    tsb_setup(0);
    tsb_set_ramp(0, TSB_TINT_COLOR, TSB_WEIGHT_DST_A);
    tsb_set_ramp(9, 0x11, TSB_WEIGHT_SRC_A);
    tsb_set_ramp(9, 0x22, TSB_WEIGHT_SRC_B);
    tsb_cube[TSB_INDEX_A_A] = TSB_PAINT_A_A;
    tsb_cube[TSB_INDEX_B_A] = TSB_PAINT_B_A;
    tsb_surface[0] = 0x11;
    tsb_surface[1] = 0x22;
    tsb_blit(stream, 2, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[1], TSB_PAINT_B_A);
}

/* Op 01 (0x41, length 2): INC EDI then STOSB at 00057874, twice, so bytes 1
   and 3 are painted and bytes 0 and 2 keep what they held.  The row is four
   bytes wide because SUB BX,CX runs twice, at 0005784b and 0005784e. */
static void tsb_stretched_run_paints_the_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = TSB_SRC_PIXEL;
    tsb_setup(0);
    tsb_level_zero_rows();
    tsb_blit(stream, 4, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_SENTINEL);
    CHECK_EQ(tsb_surface[1], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[2], TSB_SENTINEL);
    CHECK_EQ(tsb_surface[3], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[4], TSB_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 10 (0x81, length 2): the LODSB is inside the loop at 00057898, so the two
   bytes after the command byte are weighted one per destination byte, in
   order. */
static void tsb_literal_run_paints_each_stream_byte(void)
{
    unsigned char stream[3];

    stream[0] = 0x81;
    stream[1] = TSB_SRC_PIXEL;
    stream[2] = TSB_SRC_PIXEL_2;
    tsb_setup(0);
    tsb_set_ramp(9, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_A);
    tsb_set_ramp(9, TSB_SRC_PIXEL_2, TSB_WEIGHT_SRC_B);
    tsb_set_ramp(0, TSB_TINT_COLOR, TSB_WEIGHT_DST_A);
    tsb_cube[TSB_INDEX_A_A] = TSB_PAINT_A_A;
    tsb_cube[TSB_INDEX_B_A] = TSB_PAINT_B_A;
    tsb_blit(stream, 2, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[1], TSB_PAINT_B_A);
    CHECK_EQ(tsb_surface[2], TSB_SENTINEL);
}

/* At a level of 8 or less (CMP ECX,0x8 / JBE at 000577b7) the tint is read
   from row level and the pixel from row level + 9 -- the opposite assignment
   to mode 9.  Level 3 marks rows 3 and 12 and puts the decoys where the swap
   would look; level 8 is the last level on this arm and reaches row 17, the
   last row of the table. */
static void tsb_level_below_nine_reads_the_tint_in_row_level(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TSB_SRC_PIXEL;
    tsb_setup(3);
    tsb_set_ramp(12, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_A);
    tsb_set_ramp(3, TSB_TINT_COLOR, TSB_WEIGHT_DST_A);
    tsb_set_ramp(3, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_B);
    tsb_set_ramp(12, TSB_TINT_COLOR, TSB_WEIGHT_DST_B);
    tsb_cube[TSB_INDEX_A_A] = TSB_PAINT_A_A;
    tsb_blit(stream, 1, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[1], TSB_SENTINEL);

    tsb_setup(8);
    tsb_set_ramp(17, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_B);
    tsb_set_ramp(8, TSB_TINT_COLOR, TSB_WEIGHT_DST_A);
    tsb_cube[TSB_INDEX_B_A] = TSB_PAINT_B_A;
    tsb_blit(stream, 1, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_B_A);
}

/* Above 8 the row folds to 16 - level (SUB ECX,0x10 / NEG ECX) and XCHG moves
   the nine-row offset to the tint side: level 12 reads the pixel from row 4
   and the tint from row 13, level 9 from rows 7 and 16, and level 16 from rows
   0 and 9.  The level 12 case puts its decoys at rows 13 and 4, where a kernel
   that left the offset on the pixel side would look. */
static void tsb_level_above_eight_folds_to_sixteen_minus_it(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TSB_SRC_PIXEL;

    tsb_setup(12);
    tsb_set_ramp(4, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_A);
    tsb_set_ramp(13, TSB_TINT_COLOR, TSB_WEIGHT_DST_A);
    tsb_set_ramp(13, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_B);
    tsb_set_ramp(4, TSB_TINT_COLOR, TSB_WEIGHT_DST_B);
    tsb_cube[TSB_INDEX_A_A] = TSB_PAINT_A_A;
    tsb_blit(stream, 1, 1, 0);
    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);

    tsb_setup(9);
    tsb_set_ramp(7, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_B);
    tsb_set_ramp(16, TSB_TINT_COLOR, TSB_WEIGHT_DST_A);
    tsb_cube[TSB_INDEX_B_A] = TSB_PAINT_B_A;
    tsb_blit(stream, 1, 1, 0);
    CHECK_EQ(tsb_surface[0], TSB_PAINT_B_A);

    tsb_setup(16);
    tsb_set_ramp(0, TSB_SRC_PIXEL, TSB_WEIGHT_SRC_A);
    tsb_set_ramp(9, TSB_TINT_COLOR, TSB_WEIGHT_DST_B);
    tsb_cube[TSB_INDEX_A_B] = TSB_PAINT_A_B;
    tsb_blit(stream, 1, 1, 0);
    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_B);
}

/* The cube index is green-major here too: 0x00305070 folds to 0x00030507 and
   the assembly's (v >> 12) | (v & 0xffff) at 0005782b..00057833 puts that at
   0x537, not the 0x357 an (r << 8) | (g << 4) | b spelling would read, which
   is marked with a different byte. */
static void tsb_cube_index_is_green_major(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TSB_SRC_PIXEL;
    tsb_setup(0);
    tsb_level_zero_rows();
    tsb_cube[0x357] = 0x3c;
    tsb_blit(stream, 1, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);
}

/* The dispatcher's pitch - width (5 - 2 = 3) is published into the global on
   entry (MOV [dst_row_advance],EDX at 00057793) and read back at every row end
   (ADD EDI,[dst_row_advance] at 00057902), so the second row starts three
   bytes past where the first one stopped, the row count is consumed to zero
   and the width is re-read (MOV BX,[src_width] at 000577f5) for the second
   row. */
static void tsb_row_advance_is_published_and_applied(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = TSB_SRC_PIXEL;
    stream[2] = 0x01;
    stream[3] = TSB_SRC_PIXEL;
    tsb_setup(0);
    tsb_level_zero_rows();
    tsb_blit(stream, 2, 2, 3);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[1], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[2], TSB_SENTINEL);
    CHECK_EQ(tsb_surface[4], TSB_SENTINEL);
    CHECK_EQ(tsb_surface[5], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[6], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[7], TSB_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 3);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* SHR CL,2 / INC CL at 0005780f over a command byte whose low six bits are all
   set: 64 pixels is the longest run the format encodes and zero cannot be
   encoded at all. */
static void tsb_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = TSB_SRC_PIXEL;
    tsb_setup(0);
    tsb_level_zero_rows();
    tsb_blit(stream, 64, 1, 0);

    CHECK_EQ(tsb_surface[0], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[63], TSB_PAINT_A_A);
    CHECK_EQ(tsb_surface[64], TSB_SENTINEL);
}

/* --- fdps_rle_blit_tint (00057916), blit mode 11 --------------------------
 *
 * The same four-dword descriptor, the same fetch-once constant and the same
 * row assignment as mode 10: MOV [EBP+0xc],EAX / MOV [EBP+0x10],EDX at
 * 00057950 with EAX and EDX carrying 0 and 0x2400 for a level of 8 or less
 * (CMP ECX,0x8 / JBE at 0005793a) and the two exchanged by XCHG EDX,EAX above
 * that, so the TINT (descriptor[3], read at 0005791f and weighted once by MOV
 * EAX,[EBP+0x1c] / SHL EAX,0x2 / ADD EAX,[EBP+0xc] / MOV EDX,[EAX] at
 * 00057968) is weighted through the row the level names and the PIXEL through
 * the row nine on.  Ops 00, 01 and 10 are mode 10's three; op 11 is ADD
 * EDI,ECX at 00057a52, a skip that neither reads nor writes the surface and
 * consumes no stream byte.
 *
 * EDX on entry is the dispatcher's dest_pitch - src_width, so a width w and
 * advance a is a dispatch with src_width w and dest_pitch a + w.
 *
 * The fixtures are this block's own and mirror mode 10's: the ramp is the
 * full 18 x 256 and every unmarked entry is zero, the cube is filled with
 * TINT_CUBE_GUARD and only expected entries are marked, and the surface is
 * pre-filled with TINT_SENTINEL.  The cube indices are the ones the assembly's
 * sequence at 000579a8..000579b8 (SHR 4 / AND 0xf0f0f / AND 0xffff | SHR 0xc)
 * forms: 0x537 from 0x00305070, 0x5f7 from 0x00f05070 and 0x33 from
 * 0x00300030.
 */
#define TINT_MODE 11

#define TINT_RAMP_ROWS 18
#define TINT_RAMP_ROW_ENTRIES 256
#define TINT_RAMP_ENTRIES (TINT_RAMP_ROWS * TINT_RAMP_ROW_ENTRIES)
#define TINT_CUBE_BYTES 4096

#define TINT_CUBE_GUARD 0xff
#define TINT_SENTINEL 0x5a

#define TINT_SRC_PIXEL 0x20
#define TINT_SRC_PIXEL_2 0x21

/* The palette index the whole rectangle is tinted toward.  Distinct from both
   source pixels and from the sentinel, so a kernel that indexed the ramp with
   the wrong one of them lands on an unmarked entry. */
#define TINT_TINT_COLOR 0x30

#define TINT_WEIGHT_SRC_A 0x00300000
#define TINT_WEIGHT_SRC_B 0x00f00000
#define TINT_WEIGHT_DST_A 0x00005070
#define TINT_WEIGHT_DST_B 0x00000030
#define TINT_INDEX_A_A 0x537 /* (0x00300000 + 0x00005070) */
#define TINT_INDEX_B_A 0x5f7 /* (0x00f00000 + 0x00005070) */
#define TINT_INDEX_A_B 0x33  /* (0x00300000 + 0x00000030) */

#define TINT_PAINT_A_A 0x2a
#define TINT_PAINT_B_A 0x2c
#define TINT_PAINT_A_B 0x2b

#define TINT_DEST_BYTES 80

static unsigned int tint_ramp[TINT_RAMP_ENTRIES];
static unsigned char tint_cube[TINT_CUBE_BYTES];
static unsigned char tint_surface[TINT_DEST_BYTES];
static int tint_descriptor[4];

static void tint_set_ramp(int weight_row, int entry, unsigned int weighted_color)
{
    tint_ramp[weight_row * TINT_RAMP_ROW_ENTRIES + entry] = weighted_color;
}

/* Clears the tables and the surface and fills the descriptor.  The rectangle
   is not set here: fdps_blit_dispatch publishes it from its own arguments. */
static void tint_setup(unsigned int level)
{
    int entry_index;

    for (entry_index = 0; entry_index < TINT_RAMP_ENTRIES; entry_index++) {
        tint_ramp[entry_index] = 0;
    }
    for (entry_index = 0; entry_index < TINT_CUBE_BYTES; entry_index++) {
        tint_cube[entry_index] = TINT_CUBE_GUARD;
    }
    for (entry_index = 0; entry_index < TINT_DEST_BYTES; entry_index++) {
        tint_surface[entry_index] = TINT_SENTINEL;
    }

    tint_descriptor[0] = (int) tint_ramp;
    tint_descriptor[1] = (int) level;
    tint_descriptor[2] = (int) tint_cube;
    tint_descriptor[3] = TINT_TINT_COLOR;
}

static void tint_blit(unsigned char *stream, int src_width, int src_rows,
                      int row_advance)
{
    fdps_blit_dispatch(stream, tint_surface, src_width, src_rows,
                       row_advance + src_width,
                       (unsigned int) tint_descriptor, TINT_MODE);
}

/* Level 0 weights the tint through row 0 and the pixel through row 9, and the
   two decoys sit at the rows a kernel that swapped them would read: that
   pairing makes 0x00f00030, whose index 0xf3 is never marked. */
static void tint_level_zero_rows(void)
{
    tint_set_ramp(9, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_A);
    tint_set_ramp(0, TINT_TINT_COLOR, TINT_WEIGHT_DST_A);
    tint_set_ramp(0, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_B);
    tint_set_ramp(9, TINT_TINT_COLOR, TINT_WEIGHT_DST_B);
    tint_cube[TINT_INDEX_A_A] = TINT_PAINT_A_A;
}

/* Op 00, length (0x01 & 0x3f) + 1 = 2: one stream byte weighted once, added to
   the constant tint once, and the resulting palette index written to two
   consecutive bytes by REP STOSB at 000579bd.  The DEC WORD at 00057a66 leaves
   the row count 0. */
static void tint_fill_run_paints_len_bytes(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = TINT_SRC_PIXEL;
    tint_setup(0);
    tint_level_zero_rows();
    tint_blit(stream, 2, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[1], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[2], TINT_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The blend is computed before the REP, so the fill never reads the surface
   and two different destination bytes under one run come out identical.  The
   weight parked at one of those bytes is what a kernel that read [EDI] would
   pick up instead. */
static void tint_fill_ignores_the_destination_byte(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = TINT_SRC_PIXEL;
    tint_setup(0);
    tint_level_zero_rows();
    tint_set_ramp(9, 0x11, TINT_WEIGHT_SRC_B);
    tint_cube[TINT_INDEX_B_A] = TINT_PAINT_B_A;
    tint_surface[0] = 0x11;
    tint_surface[1] = 0x22;
    tint_blit(stream, 2, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[1], TINT_PAINT_A_A);
}

/* The op that separates this kernel from mode 10 next to it.  Op 11 (0xc1,
   length 2) is ADD EDI,ECX at 00057a52 with no read and no write, so the two
   bytes it covers keep the surface's own content and the op 00 that follows
   lands two bytes on -- and its command byte is read from stream[1], because
   the skip consumes no pixel byte either.  The ramp entry for the sentinel is
   weighted here so that a body which tinted the backdrop instead would paint
   TINT_PAINT_B_A over those two bytes. */
static void tint_skip_run_leaves_the_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = TINT_SRC_PIXEL;
    tint_setup(0);
    tint_level_zero_rows();
    tint_set_ramp(9, TINT_SENTINEL, TINT_WEIGHT_SRC_B);
    tint_cube[TINT_INDEX_B_A] = TINT_PAINT_B_A;
    tint_blit(stream, 4, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_SENTINEL);
    CHECK_EQ(tint_surface[1], TINT_SENTINEL);
    CHECK_EQ(tint_surface[2], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[3], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[4], TINT_SENTINEL);
}

/* Op 01 (0x41, length 2): INC EDI then STOSB at 000579f7, twice, so bytes 1
   and 3 are painted and bytes 0 and 2 keep what they held.  The row is four
   bytes wide because SUB BX,CX runs twice, at 000579ce and 000579d1. */
static void tint_stretched_run_paints_the_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = TINT_SRC_PIXEL;
    tint_setup(0);
    tint_level_zero_rows();
    tint_blit(stream, 4, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_SENTINEL);
    CHECK_EQ(tint_surface[1], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[2], TINT_SENTINEL);
    CHECK_EQ(tint_surface[3], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[4], TINT_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 10 (0x81, length 2): the LODSB is inside the loop at 00057a1b, so the two
   bytes after the command byte are weighted one per destination byte, in
   order. */
static void tint_literal_run_paints_each_stream_byte(void)
{
    unsigned char stream[3];

    stream[0] = 0x81;
    stream[1] = TINT_SRC_PIXEL;
    stream[2] = TINT_SRC_PIXEL_2;
    tint_setup(0);
    tint_set_ramp(9, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_A);
    tint_set_ramp(9, TINT_SRC_PIXEL_2, TINT_WEIGHT_SRC_B);
    tint_set_ramp(0, TINT_TINT_COLOR, TINT_WEIGHT_DST_A);
    tint_cube[TINT_INDEX_A_A] = TINT_PAINT_A_A;
    tint_cube[TINT_INDEX_B_A] = TINT_PAINT_B_A;
    tint_blit(stream, 2, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[1], TINT_PAINT_B_A);
    CHECK_EQ(tint_surface[2], TINT_SENTINEL);
}

/* At a level of 8 or less the tint is read from row level and the pixel from
   row level + 9.  Level 3 marks rows 3 and 12 and puts the decoys where the
   swap would look; level 8 is the last level on the JBE arm and reaches row
   17, the last row of the table. */
static void tint_level_below_nine_reads_the_tint_in_row_level(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TINT_SRC_PIXEL;
    tint_setup(3);
    tint_set_ramp(12, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_A);
    tint_set_ramp(3, TINT_TINT_COLOR, TINT_WEIGHT_DST_A);
    tint_set_ramp(3, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_B);
    tint_set_ramp(12, TINT_TINT_COLOR, TINT_WEIGHT_DST_B);
    tint_cube[TINT_INDEX_A_A] = TINT_PAINT_A_A;
    tint_blit(stream, 1, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[1], TINT_SENTINEL);

    tint_setup(8);
    tint_set_ramp(17, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_B);
    tint_set_ramp(8, TINT_TINT_COLOR, TINT_WEIGHT_DST_A);
    tint_cube[TINT_INDEX_B_A] = TINT_PAINT_B_A;
    tint_blit(stream, 1, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_PAINT_B_A);
}

/* Above 8 the row folds to 16 - level (SUB ECX,0x10 / NEG ECX at 00057943)
   and XCHG EDX,EAX moves the nine-row offset to the tint side: level 12 reads
   the pixel from row 4 and the tint from row 13, level 9 from rows 7 and 16,
   and level 16 from rows 0 and 9.  The level 12 case puts its decoys at rows
   13 and 4, where a kernel that left the offset on the pixel side would
   look. */
static void tint_level_above_eight_folds_to_sixteen_minus_it(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TINT_SRC_PIXEL;

    tint_setup(12);
    tint_set_ramp(4, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_A);
    tint_set_ramp(13, TINT_TINT_COLOR, TINT_WEIGHT_DST_A);
    tint_set_ramp(13, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_B);
    tint_set_ramp(4, TINT_TINT_COLOR, TINT_WEIGHT_DST_B);
    tint_cube[TINT_INDEX_A_A] = TINT_PAINT_A_A;
    tint_blit(stream, 1, 1, 0);
    CHECK_EQ(tint_surface[0], TINT_PAINT_A_A);

    tint_setup(9);
    tint_set_ramp(7, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_B);
    tint_set_ramp(16, TINT_TINT_COLOR, TINT_WEIGHT_DST_A);
    tint_cube[TINT_INDEX_B_A] = TINT_PAINT_B_A;
    tint_blit(stream, 1, 1, 0);
    CHECK_EQ(tint_surface[0], TINT_PAINT_B_A);

    tint_setup(16);
    tint_set_ramp(0, TINT_SRC_PIXEL, TINT_WEIGHT_SRC_A);
    tint_set_ramp(9, TINT_TINT_COLOR, TINT_WEIGHT_DST_B);
    tint_cube[TINT_INDEX_A_B] = TINT_PAINT_A_B;
    tint_blit(stream, 1, 1, 0);
    CHECK_EQ(tint_surface[0], TINT_PAINT_A_B);
}

/* The cube index is green-major: 0x00305070 folds to 0x00030507 and the
   assembly's (v >> 12) | (v & 0xffff) at 000579ae..000579b6 puts that at
   0x537, not the 0x357 an (r << 8) | (g << 4) | b spelling would read, which
   is marked with a different byte. */
static void tint_cube_index_is_green_major(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TINT_SRC_PIXEL;
    tint_setup(0);
    tint_level_zero_rows();
    tint_cube[0x357] = 0x3c;
    tint_blit(stream, 1, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_PAINT_A_A);
}

/* The dispatcher's pitch - width (5 - 2 = 3) is published into the global on
   entry (MOV [dst_row_advance],EDX at 00057916) and read back at every row end
   (ADD EDI,[dst_row_advance] at 00057a60), so the second row starts three
   bytes past where the first one stopped, the row count is consumed to zero
   and the width is re-read (MOV BX,[src_width] at 00057978) for the second
   row. */
static void tint_row_advance_is_published_and_applied(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = TINT_SRC_PIXEL;
    stream[2] = 0x01;
    stream[3] = TINT_SRC_PIXEL;
    tint_setup(0);
    tint_level_zero_rows();
    tint_blit(stream, 2, 2, 3);

    CHECK_EQ(tint_surface[0], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[1], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[2], TINT_SENTINEL);
    CHECK_EQ(tint_surface[4], TINT_SENTINEL);
    CHECK_EQ(tint_surface[5], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[6], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[7], TINT_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 3);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* SHR CL,2 / INC CL at 00057992 over a command byte whose low six bits are all
   set: 64 pixels is the longest run the format encodes and zero cannot be
   encoded at all. */
static void tint_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = TINT_SRC_PIXEL;
    tint_setup(0);
    tint_level_zero_rows();
    tint_blit(stream, 64, 1, 0);

    CHECK_EQ(tint_surface[0], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[63], TINT_PAINT_A_A);
    CHECK_EQ(tint_surface[64], TINT_SENTINEL);
}

/* --- fdps_rle_blit_translucent_color_range (00057a74), blit mode 12 -------
 *
 * Mode 9's blend with one range test per source pixel: in range the pixel goes
 * through the ramp and the cube exactly as mode 9 sends it, out of range it is
 * stored as itself.  The row assignment is mode 9's -- MOV EAX,0x2400 / MOV
 * EDX,0 at 00057a9e, CMP ECX,0x8 / JBE at 00057aa8, SUB ECX,0x10 / NEG ECX /
 * XCHG EDX,EAX above that, and MOV [EBP+0xc],EAX / MOV [EBP+0x10],EDX at
 * 00057abe -- so at a level of 8 or less the SOURCE reads the row nine on and
 * the DESTINATION the row the level names.
 *
 * The mode operand is a five-dword descriptor: ramp ([EAX]), level ([EAX+4]),
 * cube ([EAX+8]) and the two bounds, of which the kernel takes only sixteen
 * bits each (MOV BX,[EAX+0xc] at 00057a7d, MOV BX,[EAX+0x10] at 00057a88) into
 * the two globals it then compares against.  The compares are signed (CMP
 * AX,[min] / JL and CMP AX,[max] / JLE or JG) over a pixel byte zero-extended
 * into AX by XOR EAX,EAX / LODSB.  Out of range, op 00 is REP STOSB at 00057b17,
 * op 01 is INC EDI / STOSB / LOOP at 00057b84 and op 10 jumps straight to the
 * STOSB at 00057c28, each writing the pixel byte still in AL.
 *
 * EDX on entry is the dispatcher's dest_pitch - src_width, so a width w and
 * advance a is a dispatch with src_width w and dest_pitch a + w.
 *
 * The fixtures are this block's own and mirror mode 9's: the ramp is the full
 * 18 x 256 and every unmarked entry is zero, the cube is filled with
 * TCR_CUBE_GUARD and only expected entries are marked, and the surface is
 * pre-filled with TCR_SENTINEL.  The cube indices are the ones the assembly's
 * sequence at 00057b37..00057b45 (SHR 4 / AND 0xf0f0f / AND 0xffff | SHR 0xc)
 * forms: 0x537 from 0x00305070, 0x5f7 from 0x00f05070 and 0x33 from
 * 0x00300030.  Every out-of-range case marks the ramp entry and the cube entry
 * a body that blended anyway would land on, so the two answers cannot both
 * pass.
 */
#define TCR_MODE 12

#define TCR_RAMP_ROWS 18
#define TCR_RAMP_ROW_ENTRIES 256
#define TCR_RAMP_ENTRIES (TCR_RAMP_ROWS * TCR_RAMP_ROW_ENTRIES)
#define TCR_CUBE_BYTES 4096

#define TCR_CUBE_GUARD 0xff
#define TCR_SENTINEL 0x5a

#define TCR_SRC_PIXEL 0x20

/* The range the cases configure.  TCR_SRC_PIXEL sits inside it and
   TCR_OUT_PIXEL outside, and none of the four bound-adjacent bytes collides
   with a paint mark, the guard or the sentinel. */
#define TCR_RANGE_MIN 0x10
#define TCR_RANGE_MAX 0x30
#define TCR_OUT_PIXEL 0x40

#define TCR_WEIGHT_SRC_A 0x00300000
#define TCR_WEIGHT_SRC_B 0x00f00000
#define TCR_WEIGHT_DST_A 0x00005070
#define TCR_WEIGHT_DST_B 0x00000030
#define TCR_INDEX_A_A 0x537 /* (0x00300000 + 0x00005070) */
#define TCR_INDEX_B_A 0x5f7 /* (0x00f00000 + 0x00005070) */

#define TCR_PAINT_A_A 0x2a
#define TCR_PAINT_B_A 0x2c

#define TCR_DEST_BYTES 80

static unsigned int tcr_ramp[TCR_RAMP_ENTRIES];
static unsigned char tcr_cube[TCR_CUBE_BYTES];
static unsigned char tcr_surface[TCR_DEST_BYTES];
static int tcr_descriptor[5];

static void tcr_set_ramp(int weight_row, int entry, unsigned int weighted_color)
{
    tcr_ramp[weight_row * TCR_RAMP_ROW_ENTRIES + entry] = weighted_color;
}

/* Clears the tables and the surface and fills the descriptor, bounds included.
   The rectangle is not set here: fdps_blit_dispatch publishes it from its own
   arguments, and the kernel publishes the bounds itself. */
static void tcr_setup(unsigned int level, int color_min, int color_max)
{
    int entry_index;

    for (entry_index = 0; entry_index < TCR_RAMP_ENTRIES; entry_index++) {
        tcr_ramp[entry_index] = 0;
    }
    for (entry_index = 0; entry_index < TCR_CUBE_BYTES; entry_index++) {
        tcr_cube[entry_index] = TCR_CUBE_GUARD;
    }
    for (entry_index = 0; entry_index < TCR_DEST_BYTES; entry_index++) {
        tcr_surface[entry_index] = TCR_SENTINEL;
    }

    tcr_descriptor[0] = (int) tcr_ramp;
    tcr_descriptor[1] = (int) level;
    tcr_descriptor[2] = (int) tcr_cube;
    tcr_descriptor[3] = color_min;
    tcr_descriptor[4] = color_max;
}

static void tcr_blit(unsigned char *stream, int src_width, int src_rows,
                     int row_advance)
{
    fdps_blit_dispatch(stream, tcr_surface, src_width, src_rows,
                       row_advance + src_width,
                       (unsigned int) tcr_descriptor, TCR_MODE);
}

/* Level 0 puts the source in row 9 and the destination in row 0, and the two
   decoys sit at the rows a kernel that swapped them would read: that pairing
   makes 0x00f00030, whose index 0xf3 is never marked. */
static void tcr_level_zero_rows(void)
{
    tcr_set_ramp(9, TCR_SRC_PIXEL, TCR_WEIGHT_SRC_A);
    tcr_set_ramp(0, TCR_SENTINEL, TCR_WEIGHT_DST_A);
    tcr_set_ramp(0, TCR_SRC_PIXEL, TCR_WEIGHT_SRC_B);
    tcr_set_ramp(9, TCR_SENTINEL, TCR_WEIGHT_DST_B);
    tcr_cube[TCR_INDEX_A_A] = TCR_PAINT_A_A;
}

/* Op 00, length (0x01 & 0x3f) + 1 = 2, with the pixel byte inside the range:
   the blend is mode 9's, so both destination bytes come out at the cube entry
   0x537.  The bounds are published into the two globals on entry (00057a81,
   00057a8c) and are still there afterwards, and the DEC WORD at 00057c52 leaves
   the row count 0. */
static void tcr_fill_run_blends_a_pixel_inside_the_range(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = TCR_SRC_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_blit(stream, 2, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[1], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[2], TCR_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_translucent_color_min, TCR_RANGE_MIN);
    CHECK_EQ(data_fdps_graphics_rle_blit_translucent_color_max, TCR_RANGE_MAX);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The same op with the pixel byte outside the range is REP STOSB at 00057b17:
   the byte itself reaches the surface, untouched by ramp or cube.  Its ramp
   entry and the cube entry that entry would form are marked here, so a body
   that blended it anyway would paint TCR_PAINT_B_A instead. */
static void tcr_fill_run_stores_a_pixel_outside_the_range_raw(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = TCR_OUT_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_set_ramp(9, TCR_OUT_PIXEL, TCR_WEIGHT_SRC_B);
    tcr_cube[TCR_INDEX_B_A] = TCR_PAINT_B_A;
    tcr_blit(stream, 2, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_OUT_PIXEL);
    CHECK_EQ(tcr_surface[1], TCR_OUT_PIXEL);
    CHECK_EQ(tcr_surface[2], TCR_SENTINEL);
}

/* JL against the low bound and JLE against the high one (00057b04, 00057b11),
   so both ends are inside the blended range and the two bytes just outside
   them are not.  A body spelling either test strictly would fail on the first
   two blits and one spelling them the other way round on the last two. */
static void tcr_bounds_are_inclusive_at_both_ends(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;

    stream[1] = TCR_RANGE_MIN;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_set_ramp(9, TCR_RANGE_MIN, TCR_WEIGHT_SRC_A);
    tcr_set_ramp(0, TCR_SENTINEL, TCR_WEIGHT_DST_A);
    tcr_cube[TCR_INDEX_A_A] = TCR_PAINT_A_A;
    tcr_blit(stream, 1, 1, 0);
    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);

    stream[1] = TCR_RANGE_MAX;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_set_ramp(9, TCR_RANGE_MAX, TCR_WEIGHT_SRC_A);
    tcr_set_ramp(0, TCR_SENTINEL, TCR_WEIGHT_DST_A);
    tcr_cube[TCR_INDEX_A_A] = TCR_PAINT_A_A;
    tcr_blit(stream, 1, 1, 0);
    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);

    stream[1] = TCR_RANGE_MIN - 1;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_set_ramp(9, TCR_RANGE_MIN - 1, TCR_WEIGHT_SRC_A);
    tcr_set_ramp(0, TCR_SENTINEL, TCR_WEIGHT_DST_A);
    tcr_cube[TCR_INDEX_A_A] = TCR_PAINT_A_A;
    tcr_blit(stream, 1, 1, 0);
    CHECK_EQ(tcr_surface[0], TCR_RANGE_MIN - 1);

    stream[1] = TCR_RANGE_MAX + 1;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_set_ramp(9, TCR_RANGE_MAX + 1, TCR_WEIGHT_SRC_A);
    tcr_set_ramp(0, TCR_SENTINEL, TCR_WEIGHT_DST_A);
    tcr_cube[TCR_INDEX_A_A] = TCR_PAINT_A_A;
    tcr_blit(stream, 1, 1, 0);
    CHECK_EQ(tcr_surface[0], TCR_RANGE_MAX + 1);
}

/* The compares are signed 16-bit over a pixel that was zero-extended into AX,
   so only a bound can be negative.  A low bound of -1 lets every pixel through
   -- read as an unsigned 0xffff it would let none -- and a high bound of -1
   stops every pixel, which read as unsigned would stop none.  The two blits
   disagree in opposite directions, so no single signedness mistake passes
   both. */
static void tcr_bounds_are_read_signed(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TCR_SRC_PIXEL;

    tcr_setup(0, -1, 0x7f);
    tcr_level_zero_rows();
    tcr_blit(stream, 1, 1, 0);
    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
    CHECK_EQ(data_fdps_graphics_rle_blit_translucent_color_min, -1);

    tcr_setup(0, 0, -1);
    tcr_level_zero_rows();
    tcr_blit(stream, 1, 1, 0);
    CHECK_EQ(tcr_surface[0], TCR_SRC_PIXEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_translucent_color_max, -1);
}

/* MOV BX,word ptr [EAX+0xc] and [EAX+0x10] read sixteen bits of whole dword
   slots, so bits 16 and up of either bound are dropped.  Both bounds carry a
   high half here that would put the pixel outside the range if the dword were
   used. */
static void tcr_bounds_take_only_sixteen_bits(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TCR_SRC_PIXEL;
    tcr_setup(0, 0x00010000 + TCR_RANGE_MIN, 0x00010000 + TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_blit(stream, 1, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
    CHECK_EQ(data_fdps_graphics_rle_blit_translucent_color_min, TCR_RANGE_MIN);
    CHECK_EQ(data_fdps_graphics_rle_blit_translucent_color_max, TCR_RANGE_MAX);
}

/* Op 01 (0x41, length 2) in range: INC EDI, then the blend, then STOSB, twice
   (00057b98..00057bc0), so bytes 1 and 3 are blended and bytes 0 and 2 keep
   the surface's own content.  The row is four bytes wide because the op
   subtracts the length twice (SUB BX,CX at 00057b61 and 00057b64). */
static void tcr_stretched_run_blends_the_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = TCR_SRC_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_blit(stream, 4, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_SENTINEL);
    CHECK_EQ(tcr_surface[1], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[2], TCR_SENTINEL);
    CHECK_EQ(tcr_surface[3], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[4], TCR_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The same op out of range is INC EDI / STOSB / LOOP at 00057b84: it lands on
   the same two bytes and skips the same two, but the byte it writes is the
   stream's own.  The ramp and cube entries a blend would use are marked. */
static void tcr_stretched_run_stores_an_out_of_range_pixel_raw(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = TCR_OUT_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_set_ramp(9, TCR_OUT_PIXEL, TCR_WEIGHT_SRC_B);
    tcr_cube[TCR_INDEX_B_A] = TCR_PAINT_B_A;
    tcr_blit(stream, 4, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_SENTINEL);
    CHECK_EQ(tcr_surface[1], TCR_OUT_PIXEL);
    CHECK_EQ(tcr_surface[2], TCR_SENTINEL);
    CHECK_EQ(tcr_surface[3], TCR_OUT_PIXEL);
    CHECK_EQ(tcr_surface[4], TCR_SENTINEL);
}

/* Op 10 (0x81, length 2) tests every byte of its run: the LODSB and both
   compares are inside the LOOP at 00057be0..00057c29.  The first byte is
   inside the range and blends, the second is outside and is stored as itself;
   a body that tested once for the run would paint both the same way. */
static void tcr_literal_run_tests_every_byte(void)
{
    unsigned char stream[3];

    stream[0] = 0x81;
    stream[1] = TCR_SRC_PIXEL;
    stream[2] = TCR_OUT_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_set_ramp(9, TCR_SRC_PIXEL, TCR_WEIGHT_SRC_A);
    tcr_set_ramp(9, TCR_OUT_PIXEL, TCR_WEIGHT_SRC_B);
    tcr_set_ramp(0, TCR_SENTINEL, TCR_WEIGHT_DST_A);
    tcr_cube[TCR_INDEX_A_A] = TCR_PAINT_A_A;
    tcr_cube[TCR_INDEX_B_A] = TCR_PAINT_B_A;
    tcr_blit(stream, 2, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[1], TCR_OUT_PIXEL);
    CHECK_EQ(tcr_surface[2], TCR_SENTINEL);
}

/* Op 11 (0xc1, length 2) is ADD EDI,ECX at 00057c3e with no read, no write and
   no range test: the two bytes it covers keep the surface's own content, it
   consumes no pixel byte, and the op 00 that follows lands two bytes on. */
static void tcr_skip_run_leaves_the_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = TCR_SRC_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_blit(stream, 4, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_SENTINEL);
    CHECK_EQ(tcr_surface[1], TCR_SENTINEL);
    CHECK_EQ(tcr_surface[2], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[3], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[4], TCR_SENTINEL);
}

/* At level 8 or below the source is read from row level + 9 and the
   destination from row level, which is mode 9's assignment and not the
   tinting kernels': 0x2400 is a byte offset added after the SHL ECX,0xa row
   scale, so it is nine rows of 256 dwords.  Level 3 marks rows 12 and 3 and
   puts the decoys at 3 and 12. */
static void tcr_level_below_nine_reads_the_source_nine_rows_on(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TCR_SRC_PIXEL;
    tcr_setup(3, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_set_ramp(12, TCR_SRC_PIXEL, TCR_WEIGHT_SRC_A);
    tcr_set_ramp(3, TCR_SENTINEL, TCR_WEIGHT_DST_A);
    tcr_set_ramp(3, TCR_SRC_PIXEL, TCR_WEIGHT_SRC_B);
    tcr_set_ramp(12, TCR_SENTINEL, TCR_WEIGHT_DST_B);
    tcr_cube[TCR_INDEX_A_A] = TCR_PAINT_A_A;
    tcr_blit(stream, 1, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[1], TCR_SENTINEL);
}

/* Above 8 the row folds to 16 - level (SUB ECX,0x10 / NEG ECX at 00057ab1) and
   XCHG EDX,EAX moves the nine-row offset to the destination: level 12 reads
   the source from row 4 and the destination from row 13, with the decoys at 13
   and 4 where a body that kept both offsets on the same side would look. */
static void tcr_level_above_eight_folds_to_sixteen_minus_it(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TCR_SRC_PIXEL;
    tcr_setup(12, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_set_ramp(4, TCR_SRC_PIXEL, TCR_WEIGHT_SRC_A);
    tcr_set_ramp(13, TCR_SENTINEL, TCR_WEIGHT_DST_A);
    tcr_set_ramp(13, TCR_SRC_PIXEL, TCR_WEIGHT_SRC_B);
    tcr_set_ramp(4, TCR_SENTINEL, TCR_WEIGHT_DST_B);
    tcr_cube[TCR_INDEX_A_A] = TCR_PAINT_A_A;
    tcr_blit(stream, 1, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
}

/* The sum 0x00305070 folds to red 3, green 5, blue 7 and the assembly's
   (v >> 12) | (v & 0xffff) at 00057b3f..00057b45 puts that at 0x537, green
   major.  The obvious (r << 8) | (g << 4) | b would read 0x357, marked here
   with a byte no assertion expects. */
static void tcr_cube_index_is_green_major(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = TCR_SRC_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_cube[0x357] = 0x3c;
    tcr_blit(stream, 1, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
}

/* The dispatcher's pitch - width (5 - 2 = 3) is published into the global on
   entry (MOV [dst_row_advance],EDX at 00057a74) and read back at every row end
   (ADD EDI,[dst_row_advance] at 00057c4c), so the second row starts three
   bytes past where the first one stopped, the row count is consumed to zero
   and the width is re-read (MOV BX,[src_width] at 00057ad8) for the second
   row. */
static void tcr_row_advance_is_published_and_applied(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = TCR_SRC_PIXEL;
    stream[2] = 0x01;
    stream[3] = TCR_SRC_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_blit(stream, 2, 2, 3);

    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[1], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[2], TCR_SENTINEL);
    CHECK_EQ(tcr_surface[4], TCR_SENTINEL);
    CHECK_EQ(tcr_surface[5], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[6], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[7], TCR_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 3);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* SHR CL,2 / INC CL at 00057af2 over a command byte whose low six bits are all
   set: 64 pixels is the longest run the format encodes and zero cannot be
   encoded at all. */
static void tcr_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = TCR_SRC_PIXEL;
    tcr_setup(0, TCR_RANGE_MIN, TCR_RANGE_MAX);
    tcr_level_zero_rows();
    tcr_blit(stream, 64, 1, 0);

    CHECK_EQ(tcr_surface[0], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[63], TCR_PAINT_A_A);
    CHECK_EQ(tcr_surface[64], TCR_SENTINEL);
}

void run_rlemix_tests(void)
{
    RUN_TEST(tran_fill_run_blends_len_bytes);
    RUN_TEST(tran_the_destination_byte_is_the_second_term);
    RUN_TEST(tran_level_below_nine_reads_the_source_nine_rows_on);
    RUN_TEST(tran_level_above_eight_folds_to_sixteen_minus_it);
    RUN_TEST(tran_the_cube_index_is_green_major);
    RUN_TEST(tran_stretched_run_blends_the_second_of_each_pair);
    RUN_TEST(tran_literal_run_blends_each_stream_byte);
    RUN_TEST(tran_skip_run_leaves_the_destination_alone);
    RUN_TEST(tran_the_row_advance_is_published_and_applied);
    RUN_TEST(tran_run_length_tops_out_at_64);
    RUN_TEST(tsb_fill_run_paints_len_bytes);
    RUN_TEST(tsb_fill_ignores_the_destination_byte);
    RUN_TEST(tsb_backdrop_run_tints_the_surface);
    RUN_TEST(tsb_backdrop_run_reads_each_destination_byte);
    RUN_TEST(tsb_stretched_run_paints_the_second_of_each_pair);
    RUN_TEST(tsb_literal_run_paints_each_stream_byte);
    RUN_TEST(tsb_level_below_nine_reads_the_tint_in_row_level);
    RUN_TEST(tsb_level_above_eight_folds_to_sixteen_minus_it);
    RUN_TEST(tsb_cube_index_is_green_major);
    RUN_TEST(tsb_row_advance_is_published_and_applied);
    RUN_TEST(tsb_run_length_tops_out_at_64);
    RUN_TEST(tint_fill_run_paints_len_bytes);
    RUN_TEST(tint_fill_ignores_the_destination_byte);
    RUN_TEST(tint_skip_run_leaves_the_destination_alone);
    RUN_TEST(tint_stretched_run_paints_the_second_of_each_pair);
    RUN_TEST(tint_literal_run_paints_each_stream_byte);
    RUN_TEST(tint_level_below_nine_reads_the_tint_in_row_level);
    RUN_TEST(tint_level_above_eight_folds_to_sixteen_minus_it);
    RUN_TEST(tint_cube_index_is_green_major);
    RUN_TEST(tint_row_advance_is_published_and_applied);
    RUN_TEST(tint_run_length_tops_out_at_64);
    RUN_TEST(tcr_fill_run_blends_a_pixel_inside_the_range);
    RUN_TEST(tcr_fill_run_stores_a_pixel_outside_the_range_raw);
    RUN_TEST(tcr_bounds_are_inclusive_at_both_ends);
    RUN_TEST(tcr_bounds_are_read_signed);
    RUN_TEST(tcr_bounds_take_only_sixteen_bits);
    RUN_TEST(tcr_stretched_run_blends_the_second_of_each_pair);
    RUN_TEST(tcr_stretched_run_stores_an_out_of_range_pixel_raw);
    RUN_TEST(tcr_literal_run_tests_every_byte);
    RUN_TEST(tcr_skip_run_leaves_the_destination_alone);
    RUN_TEST(tcr_level_below_nine_reads_the_source_nine_rows_on);
    RUN_TEST(tcr_level_above_eight_folds_to_sixteen_minus_it);
    RUN_TEST(tcr_cube_index_is_green_major);
    RUN_TEST(tcr_row_advance_is_published_and_applied);
    RUN_TEST(tcr_run_length_tops_out_at_64);
}
