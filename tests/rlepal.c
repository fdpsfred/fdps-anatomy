/* tests/rlepal.c -- cover for src/rlepal.asm: the palette remap and recolour kernels.
 *
 * The kernels in rlepal.asm are hand-written assembly transcribed from the
 * original with no C interface -- they take their inputs in registers and out
 * of fdps_blit_dispatch's own stack frame -- so every case here calls
 * fdps_blit_dispatch with the kernel's mode.  The cases carry over every
 * situation the C-translation tests in tests/rlecolor.c verify (those are kept
 * for reference under #if 0), and none of them depends on which spelling of
 * the kernels is linked (rebuild_info/emit_pipeline.md).
 */
#include "testharn.h"
#include "blit.h"
#include "gamedata.h"

/* --- fdps_rle_blit_remap_sprite_and_backdrop (00056a8d), blit mode 1 ---------
 *
 * Expected values are read off the assembly: MOV EBP,[EBP+1Ch] loads the
 * dispatcher's mode operand as the table base, the op selector is SHL CL,1 / JC
 * taken twice, the length is SHR CL,2 / INC CL, every lookup is MOV AL,
 * [EAX+EBP*1] with EAX holding nothing but a pixel byte, the row ends on OR
 * BX,BX / JNZ and the row advance is the ADD EDI,EDX at 00056b15, where EDX is
 * the dispatcher's pitch - width.  So a width w and advance a is a dispatch
 * with src_width w and dest_pitch a + w.
 *
 * The table is rmsb_table[i] = 0xff - i, which is not the identity anywhere, so
 * a byte that went through it once is told apart from one that did not.  The
 * destination is pre-filled with RMSB_SENTINEL (0x5a), so a byte left alone
 * (0x5a) is told apart from one remapped in place (0xa5).
 */
#define RMSB_MODE     1
#define RMSB_SENTINEL 0x5a

static unsigned char rmsb_surface[80];
static unsigned char rmsb_table[256];

static void rmsb_setup(void)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof rmsb_surface; byte_index++) {
        rmsb_surface[byte_index] = RMSB_SENTINEL;
    }
    for (byte_index = 0; byte_index < 256; byte_index++) {
        rmsb_table[byte_index] = (unsigned char) (0xff - byte_index);
    }
}

static void rmsb_dispatch(unsigned char *stream, int width, int rows,
                          int pitch)
{
    fdps_blit_dispatch(stream, rmsb_surface, width, rows, pitch,
                       (unsigned int) rmsb_table, RMSB_MODE);
}

/* Op 00, length (0x03 & 0x3f) + 1 = 4: the one pixel byte that follows is
   looked up once (LODSB then MOV AL,[EAX+EBP*1] at 00056ab2, both before the
   REP STOSB) and the remapped value covers four destination bytes.  The DEC of
   the row count at 00056b17 leaves it 0. */
static void rmsb_fill_run_writes_remapped_byte(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    rmsb_setup();
    rmsb_dispatch(stream, 4, 1, 4);

    CHECK_EQ(rmsb_surface[0], 0x55);
    CHECK_EQ(rmsb_surface[3], 0x55);
    CHECK_EQ(rmsb_surface[4], RMSB_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 00 writes the one looked-up value over the whole run; the destination
   bytes it covers are never read, whatever they held.  The opposite of op 11,
   and indistinguishable from it on a uniform destination. */
static void rmsb_fill_run_ignores_what_the_destination_held(void)
{
    unsigned char stream[2];

    stream[0] = 0x02;
    stream[1] = 0xaa;
    rmsb_setup();
    rmsb_surface[0] = 0x00;
    rmsb_surface[1] = 0x7f;
    rmsb_surface[2] = 0xff;
    rmsb_dispatch(stream, 3, 1, 3);

    CHECK_EQ(rmsb_surface[0], 0x55);
    CHECK_EQ(rmsb_surface[1], 0x55);
    CHECK_EQ(rmsb_surface[2], 0x55);
}

/* Op 10 (0x82, length 3): the lookup is inside the loop at 00056aef, so each
   stream byte goes through the table on its own and the order is kept. */
static void rmsb_literal_run_remaps_each_stream_byte(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    rmsb_setup();
    rmsb_dispatch(stream, 3, 1, 3);

    CHECK_EQ(rmsb_surface[0], 0xee);
    CHECK_EQ(rmsb_surface[1], 0xdd);
    CHECK_EQ(rmsb_surface[2], 0xcc);
    CHECK_EQ(rmsb_surface[3], RMSB_SENTINEL);
}

/* Op 11 (0xc1, length 2): MOV AL,[EDI] / MOV AL,[EAX+EBP*1] / STOSB at 00056b08
   consumes no stream byte and remaps the pixel already on the surface.  The two
   seeded bytes differ, pinning the lookup as per pixel. */
static void rmsb_backdrop_run_remaps_destination_in_place(void)
{
    unsigned char stream[1];

    stream[0] = 0xc1;
    rmsb_setup();
    rmsb_surface[0] = 0x10;
    rmsb_surface[1] = 0x20;
    rmsb_dispatch(stream, 2, 1, 2);

    CHECK_EQ(rmsb_surface[0], 0xef);
    CHECK_EQ(rmsb_surface[1], 0xdf);
    CHECK_EQ(rmsb_surface[2], RMSB_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 01 (0x41, length 2): INC EDI then STOSB, twice, so bytes 1 and 3 carry the
   remapped pixel and bytes 0 and 2 are not written at all -- 0x5a, not the
   0xa5 an in-place remap would leave.  The row is four bytes wide because the
   op subtracts the length twice (SUB BX,CX at 00056ac7 and 00056aca). */
static void rmsb_stretched_run_leaves_gaps_unremapped(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = 0x99;
    rmsb_setup();
    rmsb_dispatch(stream, 4, 1, 4);

    CHECK_EQ(rmsb_surface[0], RMSB_SENTINEL);
    CHECK_EQ(rmsb_surface[1], 0x66);
    CHECK_EQ(rmsb_surface[2], RMSB_SENTINEL);
    CHECK_EQ(rmsb_surface[3], 0x66);
    CHECK_EQ(rmsb_surface[4], RMSB_SENTINEL);
}

/* All four ops at length 1 in one row: 0x00 fill, 0x40 stretched, 0x80
   literal, 0xc0 backdrop.  They consume widths 1, 2, 1 and 1, so the row is
   exactly five pixels, and only the stretched op's gap stays unremapped. */
static void rmsb_all_four_ops_in_one_row(void)
{
    unsigned char stream[7];

    stream[0] = 0x00;
    stream[1] = 0xa1;
    stream[2] = 0x40;
    stream[3] = 0xb2;
    stream[4] = 0x80;
    stream[5] = 0xc3;
    stream[6] = 0xc0;
    rmsb_setup();
    rmsb_dispatch(stream, 5, 1, 5);

    CHECK_EQ(rmsb_surface[0], 0x5e);
    CHECK_EQ(rmsb_surface[1], RMSB_SENTINEL);
    CHECK_EQ(rmsb_surface[2], 0x4d);
    CHECK_EQ(rmsb_surface[3], 0x3c);
    CHECK_EQ(rmsb_surface[4], 0xa5);
    CHECK_EQ(rmsb_surface[5], RMSB_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The table index is the whole unsigned pixel byte: only AL is ever loaded
   into EAX before MOV AL,[EAX+EBP*1], so 0xf0 reads entry 0xf0, not entry -16.
   Both the backdrop byte (0x90 -> 0x6f) and the fill byte (0xf0 -> 0x0f) are
   indexed that way. */
static void rmsb_table_index_is_unsigned(void)
{
    unsigned char stream[3];

    stream[0] = 0xc0;
    stream[1] = 0x00;
    stream[2] = 0xf0;
    rmsb_setup();
    rmsb_surface[0] = 0x90;
    rmsb_dispatch(stream, 2, 1, 2);

    CHECK_EQ(rmsb_surface[0], 0x6f);
    CHECK_EQ(rmsb_surface[1], 0x0f);
    CHECK_EQ(rmsb_surface[2], RMSB_SENTINEL);
}

/* 0x3f is the widest run the six length bits carry: (0x3f & 0x3f) + 1 = 64. */
static void rmsb_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0xcc;
    rmsb_setup();
    rmsb_dispatch(stream, 64, 1, 64);

    CHECK_EQ(rmsb_surface[0], 0x33);
    CHECK_EQ(rmsb_surface[63], 0x33);
    CHECK_EQ(rmsb_surface[64], RMSB_SENTINEL);
}

/* Two rows of width 2 in a pitch of 4: the dispatcher hands over an advance of
   2, which the ADD EDI,EDX at 00056b15 steps over.  The width is re-read at the
   top of the second row (MOV BX,[src_width] at 00056a90 is the JNZ target) and
   the row count comes back at zero. */
static void rmsb_second_row_starts_after_row_advance(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    rmsb_setup();
    rmsb_dispatch(stream, 2, 2, 4);

    CHECK_EQ(rmsb_surface[0], 0x55);
    CHECK_EQ(rmsb_surface[1], 0x55);
    CHECK_EQ(rmsb_surface[2], RMSB_SENTINEL);
    CHECK_EQ(rmsb_surface[3], RMSB_SENTINEL);
    CHECK_EQ(rmsb_surface[4], 0x44);
    CHECK_EQ(rmsb_surface[5], 0x44);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

void run_rlepal_tests(void)
{
    RUN_TEST(rmsb_fill_run_writes_remapped_byte);
    RUN_TEST(rmsb_fill_run_ignores_what_the_destination_held);
    RUN_TEST(rmsb_literal_run_remaps_each_stream_byte);
    RUN_TEST(rmsb_backdrop_run_remaps_destination_in_place);
    RUN_TEST(rmsb_stretched_run_leaves_gaps_unremapped);
    RUN_TEST(rmsb_all_four_ops_in_one_row);
    RUN_TEST(rmsb_table_index_is_unsigned);
    RUN_TEST(rmsb_run_length_tops_out_at_64);
    RUN_TEST(rmsb_second_row_starts_after_row_advance);
}
