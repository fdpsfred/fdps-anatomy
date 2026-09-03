/* tests/rleblend.c -- cover for src/rleblend.c.
 *
 * Every expected value is read off the assembly at 0005761b: the op selector is
 * SHL CL,1 / JC taken twice and the length SHR CL,2 / INC CL, the row ends on
 * OR BX,BX / JNZ, the row advance is MOV [0x00070030],EDX at entry and ADD
 * EDI,[0x00070030] at 0005777f, the row choice is CMP ECX,0x8 / JBE with the
 * SUB ECX,0x10 / NEG ECX / XCHG EAX,EDX arm and the 0x2400 byte offset that is
 * nine rows of 256 dwords, and the blend is ADD EAX,EDX / SHR EAX,0x4 / AND
 * 0xf0f0f / (AND 0xffff | SHR 0xc) at 000576a5..000576b5.  None of them is
 * taken from the emitted C.
 *
 * HOW A CASE NAMES THE EXACT CUBE ENTRY THAT WAS READ.  The ramp and the cube
 * are the record's own slots rather than globals, so each case builds both.  The
 * ramp is the full 18 x 256 the kernel indexes, so a case that names a row nine
 * on from another really reaches the entry the original would; every entry it
 * does not mark is zero.  The cube is filled with a guard byte and only the
 * entries a case expects are marked, so a kernel that formed any other index
 * writes the guard and the assertion fails with it.
 *
 * WHERE THE INDICES COME FROM.  0x537 is what the assembly's own sequence makes
 * of the sum 0x00305070: SHR 4 gives 0x00030507, AND 0xf0f0f leaves it alone,
 * AND 0xffff gives 0x0507 and SHR 0xc gives 0x30, so the OR is 0x537.  0x5f7 is
 * the same sequence over 0x00f05070, and 0x33 over 0x00300030.  Each is built
 * here from a source weight in the red nibble and a destination weight in the
 * green and blue ones, so a case that dropped either term lands on an index no
 * entry is marked at.
 *
 * The destination is pre-filled with a sentinel, which is both the byte that
 * says nothing was written there and the palette index the blend reads out of
 * the destination weight row.
 */
#include <string.h>
#include "testharn.h"
#include "gamedata.h"
#include "rleblend.h"

/* Repeated here rather than shared with src/rleblend.c: these are what the
   assembly encodes, and a test that took them from the emitted header would
   only prove the code agrees with itself. */
#define RAMP_ROWS 18
#define RAMP_ROW_ENTRIES 256
#define RAMP_ENTRIES (RAMP_ROWS * RAMP_ROW_ENTRIES)
#define CUBE_BYTES 4096

/* No palette index any case marks, so a destination byte holding it was read
   from an unmarked cube entry. */
#define CUBE_GUARD 0xff

/* Not a value any cube entry is marked with either, so a destination byte
   holding it was never written at all. */
#define SENTINEL 0x5a

/* The pixel byte every stream below paints with, and a second one for the
   literal case. */
#define SRC_PIXEL 0x20
#define SRC_PIXEL_2 0x21

/* The three weight pairs the cases blend, and the cube index each produces. */
#define WEIGHT_SRC_A 0x00300000
#define WEIGHT_SRC_B 0x00f00000
#define WEIGHT_DST_A 0x00005070
#define WEIGHT_DST_B 0x00000030
#define INDEX_A_A 0x537 /* (0x00300000 + 0x00005070) */
#define INDEX_B_A 0x5f7 /* (0x00f00000 + 0x00005070) */
#define INDEX_A_B 0x33  /* (0x00300000 + 0x00000030) */

/* Marks for those three, all distinct from the guard and the sentinel. */
#define PAINT_A_A 0x2a
#define PAINT_B_A 0x2c
#define PAINT_A_B 0x2b

#define DEST_BYTES 80

static unsigned int blend_ramp[RAMP_ENTRIES];
static unsigned char blend_cube[CUBE_BYTES];
static unsigned char dest_surface[DEST_BYTES];
static int blend_descriptor[3];

static void set_ramp(int weight_row, int entry, unsigned int weighted_color)
{
    blend_ramp[weight_row * RAMP_ROW_ENTRIES + entry] = weighted_color;
}

/* The globals are inputs here, written the way fdps_blit_dispatch writes them
   at 000568f8 and 00056903 before it calls the kernel. */
static void blend_setup(unsigned int level, unsigned short src_width,
                        unsigned short rows)
{
    int entry_index;

    for (entry_index = 0; entry_index < RAMP_ENTRIES; entry_index++) {
        blend_ramp[entry_index] = 0;
    }
    memset(blend_cube, CUBE_GUARD, (size_t) CUBE_BYTES);
    memset(dest_surface, SENTINEL, (size_t) DEST_BYTES);

    blend_descriptor[0] = (int) blend_ramp;
    blend_descriptor[1] = (int) level;
    blend_descriptor[2] = (int) blend_cube;

    data_fdps_graphics_rle_blit_src_width = src_width;
    data_fdps_graphics_rle_blit_remaining_rows = rows;
}

/* Level 0 puts the source in row 9 and the destination in row 0, and the two
   decoys sit at the rows a kernel that swapped them would read: that pairing
   makes 0x00f00030, whose index 0xf3 is never marked. */
static void level_zero_rows(void)
{
    set_ramp(9, SRC_PIXEL, WEIGHT_SRC_A);
    set_ramp(0, SENTINEL, WEIGHT_DST_A);
    set_ramp(0, SRC_PIXEL, WEIGHT_SRC_B);
    set_ramp(9, SENTINEL, WEIGHT_DST_B);
    blend_cube[INDEX_A_A] = PAINT_A_A;
}

/* Op 00, length (0x01 & 0x3f) + 1 = 2: the pixel byte is weighted once and
   blended into two consecutive destination bytes.  The third byte is outside
   the run and outside the row. */
static void fill_run_blends_len_bytes(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = SRC_PIXEL;
    blend_setup(0, 2, 1);
    level_zero_rows();
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], PAINT_A_A);
    CHECK_EQ(dest_surface[1], PAINT_A_A);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The second term of the add is the destination byte under the cursor, not a
   constant: one source pixel over two different destination bytes has to come
   out as two different palette indices.  A kernel that never read [EDI] would
   paint both bytes the same. */
static void the_destination_byte_is_the_second_term(void)
{
    unsigned char stream[2];

    stream[0] = 0x01;
    stream[1] = SRC_PIXEL;
    blend_setup(0, 2, 1);
    set_ramp(9, SRC_PIXEL, WEIGHT_SRC_A);
    set_ramp(0, 0x11, WEIGHT_DST_A);
    set_ramp(0, 0x22, WEIGHT_DST_B);
    blend_cube[INDEX_A_A] = PAINT_A_A;
    blend_cube[INDEX_A_B] = PAINT_A_B;
    dest_surface[0] = 0x11;
    dest_surface[1] = 0x22;
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], PAINT_A_A);
    CHECK_EQ(dest_surface[1], PAINT_A_B);
}

/* At level 8 or below the source is read from row level + 9 and the destination
   from row level: 0x2400 is a byte offset applied after the row scale, so it is
   nine rows of 256 entries and not 0x2400 entries.  Level 3 marks rows 12 and 3
   and puts the decoys at 3 and 12; level 8 is the last level on this arm and
   reaches row 17, the last row of the table. */
static void level_below_nine_reads_the_source_nine_rows_on(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = SRC_PIXEL;
    blend_setup(3, 1, 1);
    set_ramp(12, SRC_PIXEL, WEIGHT_SRC_A);
    set_ramp(3, SENTINEL, WEIGHT_DST_A);
    set_ramp(3, SRC_PIXEL, WEIGHT_SRC_B);
    set_ramp(12, SENTINEL, WEIGHT_DST_B);
    blend_cube[INDEX_A_A] = PAINT_A_A;
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], PAINT_A_A);
    CHECK_EQ(dest_surface[1], SENTINEL);

    blend_setup(8, 1, 1);
    set_ramp(17, SRC_PIXEL, WEIGHT_SRC_B);
    set_ramp(8, SENTINEL, WEIGHT_DST_A);
    blend_cube[INDEX_B_A] = PAINT_B_A;
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], PAINT_B_A);
}

/* Above 8 the same two weights are only stored the other way round, so the row
   index folds to 16 - level and the nine-row offset moves to the destination:
   level 12 reads the source from row 4 and the destination from row 13, level 9
   from rows 7 and 16, and level 16 from rows 0 and 9.  The level 12 case puts
   its decoys at rows 13 and 4, where a kernel that kept the offsets on the same
   side would look. */
static void level_above_eight_folds_to_sixteen_minus_it(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = SRC_PIXEL;

    blend_setup(12, 1, 1);
    set_ramp(4, SRC_PIXEL, WEIGHT_SRC_A);
    set_ramp(13, SENTINEL, WEIGHT_DST_A);
    set_ramp(13, SRC_PIXEL, WEIGHT_SRC_B);
    set_ramp(4, SENTINEL, WEIGHT_DST_B);
    blend_cube[INDEX_A_A] = PAINT_A_A;
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);
    CHECK_EQ(dest_surface[0], PAINT_A_A);

    blend_setup(9, 1, 1);
    set_ramp(7, SRC_PIXEL, WEIGHT_SRC_B);
    set_ramp(16, SENTINEL, WEIGHT_DST_A);
    blend_cube[INDEX_B_A] = PAINT_B_A;
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);
    CHECK_EQ(dest_surface[0], PAINT_B_A);

    blend_setup(16, 1, 1);
    set_ramp(0, SRC_PIXEL, WEIGHT_SRC_A);
    set_ramp(9, SENTINEL, WEIGHT_DST_B);
    blend_cube[INDEX_A_B] = PAINT_A_B;
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);
    CHECK_EQ(dest_surface[0], PAINT_A_B);
}

/* The cube is green-major.  The sum 0x00305070 folds to 0x00030507, which is
   red 3, green 5, blue 7, and the assembly's (v >> 12) | (v & 0xffff) puts that
   at 0x537 -- green * 256 + red * 16 + blue.  The obvious
   (r << 8) | (g << 4) | b would read 0x357, which is marked with a different
   byte here, so the two spellings cannot both pass. */
static void the_cube_index_is_green_major(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = SRC_PIXEL;
    blend_setup(0, 1, 1);
    level_zero_rows();
    blend_cube[0x357] = 0x3c;
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], PAINT_A_A);
}

/* Op 01 (0x41, length 2): INC EDI, then the blend, then STOSB, twice, so bytes
   1 and 3 are blended and bytes 0 and 2 are left as they were.  This is the
   case that would come out wrong if the op were read as a stretch that doubles
   each pixel -- that spelling paints all four bytes.  The row is four bytes wide
   because the op subtracts the length twice (SUB BX,CX at 000576d1 and
   000576d4). */
static void stretched_run_blends_the_second_of_each_pair(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = SRC_PIXEL;
    blend_setup(0, 4, 1);
    level_zero_rows();
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], PAINT_A_A);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], PAINT_A_A);
    CHECK_EQ(dest_surface[4], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 10 (0x81, length 2): the two bytes after the command byte are weighted one
   per destination byte, in order, so two different source pixels come out as two
   different palette indices. */
static void literal_run_blends_each_stream_byte(void)
{
    unsigned char stream[3];

    stream[0] = 0x81;
    stream[1] = SRC_PIXEL;
    stream[2] = SRC_PIXEL_2;
    blend_setup(0, 2, 1);
    set_ramp(9, SRC_PIXEL, WEIGHT_SRC_A);
    set_ramp(9, SRC_PIXEL_2, WEIGHT_SRC_B);
    set_ramp(0, SENTINEL, WEIGHT_DST_A);
    blend_cube[INDEX_A_A] = PAINT_A_A;
    blend_cube[INDEX_B_A] = PAINT_B_A;
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], PAINT_A_A);
    CHECK_EQ(dest_surface[1], PAINT_B_A);
    CHECK_EQ(dest_surface[2], SENTINEL);
}

/* Op 11 (0xc1, length 2) is ADD EDI,ECX with no write and no read at all: the
   two bytes it covers keep the surface's own content and the fill that follows
   lands two bytes further on. */
static void skip_run_leaves_the_destination_alone(void)
{
    unsigned char stream[3];

    stream[0] = 0xc1;
    stream[1] = 0x01;
    stream[2] = SRC_PIXEL;
    blend_setup(0, 4, 1);
    level_zero_rows();
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], SENTINEL);
    CHECK_EQ(dest_surface[2], PAINT_A_A);
    CHECK_EQ(dest_surface[3], PAINT_A_A);
    CHECK_EQ(dest_surface[4], SENTINEL);
}

/* The advance is published into the global on entry and read back from it at
   every row end, so the second row starts three bytes past where the first one
   stopped and the global still holds the advance afterwards.  The row count is
   consumed to zero and the width is re-read for the second row. */
static void the_row_advance_is_published_and_applied(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = SRC_PIXEL;
    stream[2] = 0x01;
    stream[3] = SRC_PIXEL;
    blend_setup(0, 2, 2);
    level_zero_rows();
    fdps_rle_blit_translucent(stream, dest_surface, 3, blend_descriptor);

    CHECK_EQ(dest_surface[0], PAINT_A_A);
    CHECK_EQ(dest_surface[1], PAINT_A_A);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[4], SENTINEL);
    CHECK_EQ(dest_surface[5], PAINT_A_A);
    CHECK_EQ(dest_surface[6], PAINT_A_A);
    CHECK_EQ(dest_surface[7], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_dst_row_advance, 3);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* SHR CL,2 / INC CL over a command byte whose low six bits are all set: the
   longest run the format can encode is 64 pixels, and a length of zero cannot
   be encoded at all. */
static void run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = SRC_PIXEL;
    blend_setup(0, 64, 1);
    level_zero_rows();
    fdps_rle_blit_translucent(stream, dest_surface, 0, blend_descriptor);

    CHECK_EQ(dest_surface[0], PAINT_A_A);
    CHECK_EQ(dest_surface[63], PAINT_A_A);
    CHECK_EQ(dest_surface[64], SENTINEL);
}

void run_rleblend_tests(void)
{
    RUN_TEST(fill_run_blends_len_bytes);
    RUN_TEST(the_destination_byte_is_the_second_term);
    RUN_TEST(level_below_nine_reads_the_source_nine_rows_on);
    RUN_TEST(level_above_eight_folds_to_sixteen_minus_it);
    RUN_TEST(the_cube_index_is_green_major);
    RUN_TEST(stretched_run_blends_the_second_of_each_pair);
    RUN_TEST(literal_run_blends_each_stream_byte);
    RUN_TEST(skip_run_leaves_the_destination_alone);
    RUN_TEST(the_row_advance_is_published_and_applied);
    RUN_TEST(run_length_tops_out_at_64);
}
