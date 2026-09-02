/* tests/rlecolor.c -- cover for src/rlecolor.c.
 *
 * Every expected value is read off the assembly at 00056a8d: the op selector is
 * SHL CL,1 / JC taken twice, the length is SHR CL,2 / INC CL, the lookup is MOV
 * AL,byte ptr [EAX + EBP*1] with EAX holding nothing but a pixel byte, the row
 * ends on OR BX,BX / JNZ and the row advance is the ADD EDI,EDX at 00056b15.
 * None of them is taken from the emitted C.
 *
 * The remap table is remap[i] = 0xff - i, which is not the identity anywhere and
 * tells the table's two directions apart: a byte that went through it once can
 * be distinguished from one that did not and from one that went through twice.
 *
 * The destination is pre-filled with 0x5a, so a byte the kernel left alone
 * (0x5a) can be told apart from one it remapped in place (0xa5).  That is the
 * distinction op 01 and op 11 exist to be tested on: op 11 remaps the backdrop
 * it walks over, op 01 leaves the first byte of every pair completely untouched.
 */
#include "testharn.h"
#include "gamedata.h"
#include "rlecolor.h"

#define SENTINEL 0x5a

static unsigned char dest_surface[80];
static unsigned char remap_table[256];

/* The two globals are inputs here, written the way fdps_blit_dispatch writes
   them at 000568f8 and 00056903 before it calls the kernel.  The table is what
   the dispatcher's sixth argument points at. */
static void blit_setup(unsigned short src_width, unsigned short rows)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof dest_surface; byte_index++) {
        dest_surface[byte_index] = SENTINEL;
    }
    for (byte_index = 0; byte_index < 256; byte_index++) {
        remap_table[byte_index] = (unsigned char) (0xff - byte_index);
    }
    data_fdps_graphics_rle_blit_src_width = src_width;
    data_fdps_graphics_rle_blit_remaining_rows = rows;
}

/* Op 00, length (0x03 & 0x3f) + 1 = 4: the one pixel byte that follows is looked
   up once (LODSB then MOV AL,[EAX+EBP*1], both before the REP STOSB) and the
   remapped value is written over four destination bytes. */
static void remap_fill_run_writes_remapped_byte(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    blit_setup(4, 1);
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 0,
                                            remap_table);

    CHECK_EQ(dest_surface[0], 0x55);
    CHECK_EQ(dest_surface[3], 0x55);
    CHECK_EQ(dest_surface[4], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 10 (0x82): the lookup is inside the loop at 00056aef, so each of the three
   stream bytes goes through the table on its own and the order is kept. */
static void remap_literal_run_remaps_each_stream_byte(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    blit_setup(3, 1);
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 0,
                                            remap_table);

    CHECK_EQ(dest_surface[0], 0xee);
    CHECK_EQ(dest_surface[1], 0xdd);
    CHECK_EQ(dest_surface[2], 0xcc);
    CHECK_EQ(dest_surface[3], SENTINEL);
}

/* Op 11 (0xc1, length 2) is what separates this kernel from the plain blitter:
   MOV AL,[EDI] / MOV AL,[EAX+EBP*1] / STOSB at 00056b08 consumes no stream byte
   and remaps the pixel already on the surface.  The two seeded bytes differ, so
   this also pins the lookup as per-pixel rather than one value reused. */
static void remap_backdrop_run_remaps_destination_in_place(void)
{
    unsigned char stream[1];

    stream[0] = 0xc1;
    blit_setup(2, 1);
    dest_surface[0] = 0x10;
    dest_surface[1] = 0x20;
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 0,
                                            remap_table);

    CHECK_EQ(dest_surface[0], 0xef);
    CHECK_EQ(dest_surface[1], 0xdf);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 01 (0x41, length 2): INC EDI then STOSB, twice, so bytes 1 and 3 carry the
   remapped pixel and bytes 0 and 2 are not written at all -- not stepped over
   and remapped the way op 11 does it, but left holding the surface's own byte.
   0x5a rather than 0xa5 in the gaps is the whole assertion.  The row is four
   bytes wide because the op subtracts the length twice (SUB BX,CX at 00056ac7
   and 00056aca). */
static void remap_stretched_run_leaves_gaps_unremapped(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = 0x99;
    blit_setup(4, 1);
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 0,
                                            remap_table);

    CHECK_EQ(dest_surface[0], SENTINEL);
    CHECK_EQ(dest_surface[1], 0x66);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], 0x66);
    CHECK_EQ(dest_surface[4], SENTINEL);
}

/* All four ops in one row, each at length 1, which is the selector's own test:
   0x00 fill, 0x40 stretched, 0x80 literal, 0xc0 backdrop.  The widths they
   consume are 1, 2, 1 and 1, which is what makes the row exactly five pixels,
   and only the stretched op's gap comes out unremapped. */
static void remap_all_four_ops_in_one_row(void)
{
    unsigned char stream[7];

    stream[0] = 0x00;
    stream[1] = 0xa1;
    stream[2] = 0x40;
    stream[3] = 0xb2;
    stream[4] = 0x80;
    stream[5] = 0xc3;
    stream[6] = 0xc0;
    blit_setup(5, 1);
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 0,
                                            remap_table);

    CHECK_EQ(dest_surface[0], 0x5e);
    CHECK_EQ(dest_surface[1], SENTINEL);
    CHECK_EQ(dest_surface[2], 0x4d);
    CHECK_EQ(dest_surface[3], 0x3c);
    CHECK_EQ(dest_surface[4], 0xa5);
    CHECK_EQ(dest_surface[5], SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The table index is a whole unsigned pixel byte: the original zero-extends it
   into EAX before MOV AL,[EAX+EBP*1], so 0xf0 reads entry 0xf0 and not entry
   -16.  Both the byte the fill op takes from the stream and the byte the
   backdrop op takes off the surface are indexed that way. */
static void remap_table_index_is_unsigned(void)
{
    unsigned char stream[3];

    stream[0] = 0xc0;
    stream[1] = 0x00;
    stream[2] = 0xf0;
    blit_setup(2, 1);
    dest_surface[0] = 0x90;
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 0,
                                            remap_table);

    CHECK_EQ(dest_surface[0], 0x6f);
    CHECK_EQ(dest_surface[1], 0x0f);
    CHECK_EQ(dest_surface[2], SENTINEL);
}

/* 0x3f is the widest run the six length bits can carry: (0x3f & 0x3f) + 1 is
   64, not 63 and not 0x3f. */
static void remap_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0xcc;
    blit_setup(64, 1);
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 0,
                                            remap_table);

    CHECK_EQ(dest_surface[0], 0x33);
    CHECK_EQ(dest_surface[63], 0x33);
    CHECK_EQ(dest_surface[64], SENTINEL);
}

/* Two rows, and the row advance stepping the destination over the part of the
   scanline the sprite does not cover: width 2 in a pitch of 4 gives the advance
   of 2 that fdps_blit_dispatch computes as pitch - width.  The width is re-read
   at the top of the second row (MOV BX,[0x00070024] is the JNZ target at
   00056a90), and the row count comes back at zero. */
static void remap_second_row_starts_after_row_advance(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    blit_setup(2, 2);
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 2,
                                            remap_table);

    CHECK_EQ(dest_surface[0], 0x55);
    CHECK_EQ(dest_surface[1], 0x55);
    CHECK_EQ(dest_surface[2], SENTINEL);
    CHECK_EQ(dest_surface[3], SENTINEL);
    CHECK_EQ(dest_surface[4], 0x44);
    CHECK_EQ(dest_surface[5], 0x44);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 00 looks the pixel byte up once, before the REP STOSB (LODSB / MOV
   AL,[EAX+EBP*1] at 00056ab2, outside the run), and writes that one value over
   the whole run: the destination bytes it covers are never read, whatever they
   held.  This is the op 11 case's opposite, and the two would be
   indistinguishable on a destination that was uniform going in. */
static void remap_fill_run_ignores_what_the_destination_held(void)
{
    unsigned char stream[2];

    stream[0] = 0x02;
    stream[1] = 0xaa;
    blit_setup(3, 1);
    dest_surface[0] = 0x00;
    dest_surface[1] = 0x7f;
    dest_surface[2] = 0xff;
    fdps_rle_blit_remap_sprite_and_backdrop(stream, dest_surface, 0,
                                            remap_table);

    CHECK_EQ(dest_surface[0], 0x55);
    CHECK_EQ(dest_surface[1], 0x55);
    CHECK_EQ(dest_surface[2], 0x55);
}

void run_rlecolor_tests(void)
{
    RUN_TEST(remap_fill_run_writes_remapped_byte);
    RUN_TEST(remap_fill_run_ignores_what_the_destination_held);
    RUN_TEST(remap_literal_run_remaps_each_stream_byte);
    RUN_TEST(remap_backdrop_run_remaps_destination_in_place);
    RUN_TEST(remap_stretched_run_leaves_gaps_unremapped);
    RUN_TEST(remap_all_four_ops_in_one_row);
    RUN_TEST(remap_table_index_is_unsigned);
    RUN_TEST(remap_run_length_tops_out_at_64);
    RUN_TEST(remap_second_row_starts_after_row_advance);
}
