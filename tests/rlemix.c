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
}
