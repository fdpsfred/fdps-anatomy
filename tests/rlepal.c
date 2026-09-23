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

/* --- fdps_rle_blit_with_palette_remap (00056b25), blit mode 2 ----------------
 *
 * Expected values are read off the assembly: MOV EBP,[EBP+1Ch] at 00056b25 loads
 * the dispatcher's mode operand as the table base, the op selector is SHL CL,1 /
 * JC taken twice, the length is SHR CL,2 / INC CL, every lookup is MOV AL,
 * [EAX+EBP*1] with EAX holding nothing but a pixel byte, the row ends on OR
 * BX,BX / JNZ and the row advance is the ADD EDI,EDX at 00056ba7, where EDX is
 * the dispatcher's pitch - width.  So a width w and advance a is a dispatch
 * with src_width w and dest_pitch a + w.
 *
 * Same stream format and same table as mode 1; op 11 is where the two differ:
 * ADD EDI,ECX / SUB BX,CX at 00056b9d writes nothing, where mode 1 remaps the
 * surface in place.  The table is rmpal_table[i] = 0xff - i and the
 * destination is pre-filled with RMPAL_SENTINEL (0x5a), so a byte left alone
 * (0x5a) is told apart from one remapped in place (0xa5).
 */
#define RMPAL_MODE     2
#define RMPAL_SENTINEL 0x5a

static unsigned char rmpal_surface[80];
static unsigned char rmpal_table[256];

static void rmpal_setup(void)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof rmpal_surface; byte_index++) {
        rmpal_surface[byte_index] = RMPAL_SENTINEL;
    }
    for (byte_index = 0; byte_index < 256; byte_index++) {
        rmpal_table[byte_index] = (unsigned char) (0xff - byte_index);
    }
}

static void rmpal_dispatch(unsigned char *stream, int width, int rows,
                           int pitch)
{
    fdps_blit_dispatch(stream, rmpal_surface, width, rows, pitch,
                       (unsigned int) rmpal_table, RMPAL_MODE);
}

/* Op 00, length (0x03 & 0x3f) + 1 = 4: LODSB / MOV AL,[EAX+EBP*1] at 00056b4a
   look the following pixel byte up once, outside the REP STOSB, and the
   remapped value covers four destination bytes.  The DEC of the row count at
   00056ba9 leaves it 0. */
static void rmpal_fill_run_writes_remapped_byte(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    rmpal_setup();
    rmpal_dispatch(stream, 4, 1, 4);

    CHECK_EQ(rmpal_surface[0], 0x55);
    CHECK_EQ(rmpal_surface[3], 0x55);
    CHECK_EQ(rmpal_surface[4], RMPAL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 10 (0x82, length 3): the lookup sits inside the loop at 00056b87, so each
   stream byte goes through the table on its own and the order is kept. */
static void rmpal_literal_run_remaps_each_stream_byte(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    rmpal_setup();
    rmpal_dispatch(stream, 3, 1, 3);

    CHECK_EQ(rmpal_surface[0], 0xee);
    CHECK_EQ(rmpal_surface[1], 0xdd);
    CHECK_EQ(rmpal_surface[2], 0xcc);
    CHECK_EQ(rmpal_surface[3], RMPAL_SENTINEL);
}

/* Op 11 (0xc1, length 2) is this kernel's transparency: ADD EDI,ECX at
   00056b9d moves the destination on and nothing is written.  0x10 and 0x20 are
   values the table would visibly change (mode 1 yields 0xef and 0xdf), so
   their coming back unaltered is the assertion. */
static void rmpal_skip_run_leaves_backdrop_untouched(void)
{
    unsigned char stream[1];

    stream[0] = 0xc1;
    rmpal_setup();
    rmpal_surface[0] = 0x10;
    rmpal_surface[1] = 0x20;
    rmpal_dispatch(stream, 2, 1, 2);

    CHECK_EQ(rmpal_surface[0], 0x10);
    CHECK_EQ(rmpal_surface[1], 0x20);
    CHECK_EQ(rmpal_surface[2], RMPAL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 01 (0x41, length 2): INC EDI then STOSB at 00056b69, twice, so bytes 1
   and 3 carry the remapped pixel and bytes 0 and 2 are never written.  The row
   is four bytes wide because the op subtracts the length twice (SUB BX,CX at
   00056b5f and 00056b62). */
static void rmpal_stretched_run_leaves_gaps_unwritten(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = 0x99;
    rmpal_setup();
    rmpal_dispatch(stream, 4, 1, 4);

    CHECK_EQ(rmpal_surface[0], RMPAL_SENTINEL);
    CHECK_EQ(rmpal_surface[1], 0x66);
    CHECK_EQ(rmpal_surface[2], RMPAL_SENTINEL);
    CHECK_EQ(rmpal_surface[3], 0x66);
    CHECK_EQ(rmpal_surface[4], RMPAL_SENTINEL);
}

/* All four ops at length 1 in one row: 0x00 fill, 0x40 stretched, 0x80
   literal, 0xc0 skip, consuming 1, 2, 1 and 1 of the five-pixel row.  Byte 4
   is the one mode 1 writes 0xa5 into and this kernel does not touch. */
static void rmpal_all_four_ops_in_one_row(void)
{
    unsigned char stream[7];

    stream[0] = 0x00;
    stream[1] = 0xa1;
    stream[2] = 0x40;
    stream[3] = 0xb2;
    stream[4] = 0x80;
    stream[5] = 0xc3;
    stream[6] = 0xc0;
    rmpal_setup();
    rmpal_dispatch(stream, 5, 1, 5);

    CHECK_EQ(rmpal_surface[0], 0x5e);
    CHECK_EQ(rmpal_surface[1], RMPAL_SENTINEL);
    CHECK_EQ(rmpal_surface[2], 0x4d);
    CHECK_EQ(rmpal_surface[3], 0x3c);
    CHECK_EQ(rmpal_surface[4], RMPAL_SENTINEL);
    CHECK_EQ(rmpal_surface[5], RMPAL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 11 reads no stream byte -- 00056b98 goes straight to the width arithmetic
   without a LODSB -- so the 0x00 at stream[1] is the next command and 0xf0 its
   pixel byte; had the skip eaten a byte, 0xf0 would decode as a command.  And
   the table index is the whole unsigned pixel byte (only AL is ever loaded into
   EAX before MOV AL,[EAX+EBP*1]), so 0xf0 reads entry 0xf0, not entry -16. */
static void rmpal_skip_consumes_no_stream_byte(void)
{
    unsigned char stream[3];

    stream[0] = 0xc0;
    stream[1] = 0x00;
    stream[2] = 0xf0;
    rmpal_setup();
    rmpal_surface[0] = 0x90;
    rmpal_dispatch(stream, 2, 1, 2);

    CHECK_EQ(rmpal_surface[0], 0x90);
    CHECK_EQ(rmpal_surface[1], 0x0f);
    CHECK_EQ(rmpal_surface[2], RMPAL_SENTINEL);
}

/* 0x3f is the widest run the six length bits carry: SHR CL,2 / INC CL at
   00056b42 makes (0x3f & 0x3f) + 1 = 64. */
static void rmpal_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0xcc;
    rmpal_setup();
    rmpal_dispatch(stream, 64, 1, 64);

    CHECK_EQ(rmpal_surface[0], 0x33);
    CHECK_EQ(rmpal_surface[63], 0x33);
    CHECK_EQ(rmpal_surface[64], RMPAL_SENTINEL);
}

/* Two rows of width 2 in a pitch of 4: the dispatcher hands over an advance of
   2, which the ADD EDI,EDX at 00056ba7 steps over.  The width is re-read at the
   top of the second row (MOV BX,[src_width] at 00056b28 is the JNZ target at
   00056bb0) and the row count comes back at zero. */
static void rmpal_second_row_starts_after_row_advance(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    rmpal_setup();
    rmpal_dispatch(stream, 2, 2, 4);

    CHECK_EQ(rmpal_surface[0], 0x55);
    CHECK_EQ(rmpal_surface[1], 0x55);
    CHECK_EQ(rmpal_surface[2], RMPAL_SENTINEL);
    CHECK_EQ(rmpal_surface[3], RMPAL_SENTINEL);
    CHECK_EQ(rmpal_surface[4], 0x44);
    CHECK_EQ(rmpal_surface[5], 0x44);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* --- fdps_rle_blit_recolor (00056bb7), blit mode 3 ---------------------------
 *
 * Expected values are read off the assembly.  The same stream format as modes
 * 1 and 2, but no table: MOV DH,[EBP+1Ch] / MOV DL,[EBP+1Dh] / MOV AH,[EBP+1Eh]
 * at 00056bb8 take the low three bytes of the dispatcher's mode operand --
 * tint_offset, color_base and band_mask, in that order -- and every pixel
 * written goes through ((source + tint_offset) & band_mask) + color_base, the
 * ADD AL,DH / AND AL,AH / ADD AL,DL triple at 00056be5, 00056c03 and 00056c28,
 * all eight bits wide.  PUSH EDX / POP EBP at 00056bb7 keeps the dispatcher's
 * pitch - width as the row advance for the ADD EDI,EBP at 00056c4e, so a width
 * w and advance a is a dispatch with src_width w and dest_pitch a + w.
 * RCOL_OPERANDS packs the three bytes the way the kernel reads them out of the
 * operand dword.
 *
 * The destination is pre-filled with RCOL_SENTINEL (0x5a), so a byte the
 * kernel did not write is told apart from one it did.
 */
#define RCOL_MODE     3
#define RCOL_SENTINEL 0x5a
#define RCOL_OPERANDS(tint_offset, color_base, band_mask) \
    (((unsigned int) (band_mask) << 16) | ((unsigned int) (color_base) << 8) \
     | (unsigned int) (tint_offset))

static unsigned char rcol_surface[80];

static void rcol_setup(void)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof rcol_surface; byte_index++) {
        rcol_surface[byte_index] = RCOL_SENTINEL;
    }
}

static void rcol_dispatch(unsigned char *stream, int width, int rows,
                          int pitch, unsigned int operands)
{
    fdps_blit_dispatch(stream, rcol_surface, width, rows, pitch, operands,
                       RCOL_MODE);
}

/* Op 00, length (0x03 & 0x3f) + 1 = 4: the pixel byte that follows is
   recoloured once, before the REP STOSB at 00056beb, and the result covers four
   destination bytes.  ((0xaa + 0x05) & 0x3f) + 0x20 is (0xaf & 0x3f) + 0x20 =
   0x4f.  The DEC of the row count at 00056c50 leaves it 0. */
static void rcol_fill_run_writes_the_recoloured_byte(void)
{
    unsigned char stream[2];

    stream[0] = 0x03;
    stream[1] = 0xaa;
    rcol_setup();
    rcol_dispatch(stream, 4, 1, 4, RCOL_OPERANDS(0x05, 0x20, 0x3f));

    CHECK_EQ(rcol_surface[0], 0x4f);
    CHECK_EQ(rcol_surface[3], 0x4f);
    CHECK_EQ(rcol_surface[4], RCOL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* The recolour reads the stream byte and nothing else: there is no MOV AL,[EDI]
   anywhere in this kernel, so three different seeded destination bytes all
   come back as the same 0x4f. */
static void rcol_fill_run_never_reads_the_destination(void)
{
    unsigned char stream[2];

    stream[0] = 0x02;
    stream[1] = 0xaa;
    rcol_setup();
    rcol_surface[0] = 0x00;
    rcol_surface[1] = 0x7f;
    rcol_surface[2] = 0xff;
    rcol_dispatch(stream, 3, 1, 3, RCOL_OPERANDS(0x05, 0x20, 0x3f));

    CHECK_EQ(rcol_surface[0], 0x4f);
    CHECK_EQ(rcol_surface[1], 0x4f);
    CHECK_EQ(rcol_surface[2], 0x4f);
}

/* Op 10 (0x82, length 3): the recolour triple is inside the loop at 00056c28,
   so each stream byte is transformed on its own -- 0x11, 0x22 and 0x33 become
   ((x + 0x01) & 0x0f) + 0x40, that is 0x42, 0x43 and 0x44, in order.  Every
   other assignment of 0x01, 0x40 and 0x0f to the three operand bytes gives a
   different first byte (0x0f, 0x02, 0x10, 0x01 or 0x40), so this also pins
   each byte to its position in the operand dword. */
static void rcol_literal_run_recolours_each_stream_byte(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x11;
    stream[2] = 0x22;
    stream[3] = 0x33;
    rcol_setup();
    rcol_dispatch(stream, 3, 1, 3, RCOL_OPERANDS(0x01, 0x40, 0x0f));

    CHECK_EQ(rcol_surface[0], 0x42);
    CHECK_EQ(rcol_surface[1], 0x43);
    CHECK_EQ(rcol_surface[2], 0x44);
    CHECK_EQ(rcol_surface[3], RCOL_SENTINEL);
}

/* Op 01 (0x41, length 2): INC EDI at 00056c09 precedes the STOSB, so the run
   lands in the odd columns and the even ones keep the surface's own byte, while
   SUB BX,CX twice at 00056bfc and 00056bff charges four columns for a length of
   two.  The identity triple (offset 0, base 0, mask 0xff) keeps 0x99 as 0x99,
   so the case is about where the pixels land. */
static void rcol_stretched_run_writes_the_odd_columns(void)
{
    unsigned char stream[2];

    stream[0] = 0x41;
    stream[1] = 0x99;
    rcol_setup();
    rcol_dispatch(stream, 4, 1, 4, RCOL_OPERANDS(0x00, 0x00, 0xff));

    CHECK_EQ(rcol_surface[0], RCOL_SENTINEL);
    CHECK_EQ(rcol_surface[1], 0x99);
    CHECK_EQ(rcol_surface[2], RCOL_SENTINEL);
    CHECK_EQ(rcol_surface[3], 0x99);
    CHECK_EQ(rcol_surface[4], RCOL_SENTINEL);
}

/* Op 11 (0xc1, length 2) is a plain transparent skip: ADD EDI,ECX / SUB BX,CX
   at 00056c40, no stream byte and no store.  The operand is the shipped
   0x0000ff00, which turns every byte this kernel writes into 0xff, so a skip
   that wrote anything could not leave 0x10 and 0x20 standing. */
static void rcol_skip_run_leaves_backdrop_untouched(void)
{
    unsigned char stream[1];

    stream[0] = 0xc1;
    rcol_setup();
    rcol_surface[0] = 0x10;
    rcol_surface[1] = 0x20;
    rcol_dispatch(stream, 2, 1, 2, 0x0000ff00);

    CHECK_EQ(rcol_surface[0], 0x10);
    CHECK_EQ(rcol_surface[1], 0x20);
    CHECK_EQ(rcol_surface[2], RCOL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* Op 11 reads no stream byte -- 00056c3b goes straight to the width arithmetic
   with no LODSB -- so the 0x00 at stream[1] is the next command and 0xf0 its
   pixel byte.  The fill's pixel is ((0xf0 + 0x10) & 0x7f) + 0x03 = 0x03, the
   sum having gone past 0xff before the mask took it back. */
static void rcol_skip_consumes_no_stream_byte(void)
{
    unsigned char stream[3];

    stream[0] = 0xc0;
    stream[1] = 0x00;
    stream[2] = 0xf0;
    rcol_setup();
    rcol_surface[0] = 0x90;
    rcol_dispatch(stream, 2, 1, 2, RCOL_OPERANDS(0x10, 0x03, 0x7f));

    CHECK_EQ(rcol_surface[0], 0x90);
    CHECK_EQ(rcol_surface[1], 0x03);
    CHECK_EQ(rcol_surface[2], RCOL_SENTINEL);
}

/* ADD AL,DL at 00056be9 is eight bits wide: with the mask wide open,
   ((0xc0 + 0) & 0xff) + 0x80 is 0x140 in a register that keeps only 0x40 --
   the add wraps rather than saturating at 0xff. */
static void rcol_color_base_add_wraps_in_eight_bits(void)
{
    unsigned char stream[2];

    stream[0] = 0x00;
    stream[1] = 0xc0;
    rcol_setup();
    rcol_dispatch(stream, 1, 1, 1, RCOL_OPERANDS(0x00, 0x80, 0xff));

    CHECK_EQ(rcol_surface[0], 0x40);
    CHECK_EQ(rcol_surface[1], RCOL_SENTINEL);
}

/* The shipped operand dword, 0x0000ff00, passed as the literal it is: band
   mask 0 collapses every pixel to color_base, so three different source bytes
   all come out as palette index 0xff (the flat silhouette of the rest flash,
   MOV dword ptr [EBP + -0x8],0xff00 at 000120dc and 0001e400). */
static void rcol_shipped_operands_collapse_to_one_index(void)
{
    unsigned char stream[4];

    stream[0] = 0x82;
    stream[1] = 0x00;
    stream[2] = 0x7f;
    stream[3] = 0xff;
    rcol_setup();
    rcol_dispatch(stream, 3, 1, 3, 0x0000ff00);

    CHECK_EQ(rcol_surface[0], 0xff);
    CHECK_EQ(rcol_surface[1], 0xff);
    CHECK_EQ(rcol_surface[2], 0xff);
    CHECK_EQ(rcol_surface[3], RCOL_SENTINEL);
}

/* All four ops in one row at length 1 -- 0x00 fill, 0x40 stretched, 0x80
   literal, 0xc0 skip -- consuming 1, 2, 1 and 1 of a five-pixel row, so a
   selector that sent an op to the wrong arm would not land the row on zero.
   The pixels are ((x + 0x02) & 0x1f) + 0x10 for x = 0xa1, 0xb2 and 0xc3, that
   is 0x13, 0x24 and 0x15; byte 1 is the stretched run's untouched even column
   and byte 4 is the skip's. */
static void rcol_all_four_ops_in_one_row(void)
{
    unsigned char stream[7];

    stream[0] = 0x00;
    stream[1] = 0xa1;
    stream[2] = 0x40;
    stream[3] = 0xb2;
    stream[4] = 0x80;
    stream[5] = 0xc3;
    stream[6] = 0xc0;
    rcol_setup();
    rcol_dispatch(stream, 5, 1, 5, RCOL_OPERANDS(0x02, 0x10, 0x1f));

    CHECK_EQ(rcol_surface[0], 0x13);
    CHECK_EQ(rcol_surface[1], RCOL_SENTINEL);
    CHECK_EQ(rcol_surface[2], 0x24);
    CHECK_EQ(rcol_surface[3], 0x15);
    CHECK_EQ(rcol_surface[4], RCOL_SENTINEL);
    CHECK_EQ(rcol_surface[5], RCOL_SENTINEL);
    CHECK_EQ(data_fdps_graphics_rle_blit_remaining_rows, 0);
}

/* 0x3f is the widest run the six length bits carry: SHR CL,2 / INC CL at
   00056bdc makes (0x3f & 0x3f) + 1 = 64.  The identity triple keeps 0xcc as
   0xcc so the case is about the count. */
static void rcol_run_length_tops_out_at_64(void)
{
    unsigned char stream[2];

    stream[0] = 0x3f;
    stream[1] = 0xcc;
    rcol_setup();
    rcol_dispatch(stream, 64, 1, 64, RCOL_OPERANDS(0x00, 0x00, 0xff));

    CHECK_EQ(rcol_surface[0], 0xcc);
    CHECK_EQ(rcol_surface[63], 0xcc);
    CHECK_EQ(rcol_surface[64], RCOL_SENTINEL);
}

/* Two rows of width 2 in a pitch of 4: the dispatcher hands over an advance of
   2, which the ADD EDI,EBP at 00056c4e steps over.  The width is re-read at the
   top of the second row (MOV BX,[src_width] at 00056bc2 is the JNZ target at
   00056c57) and the row count comes back at zero. */
static void rcol_second_row_starts_after_row_advance(void)
{
    unsigned char stream[4];

    stream[0] = 0x01;
    stream[1] = 0xaa;
    stream[2] = 0x01;
    stream[3] = 0xbb;
    rcol_setup();
    rcol_dispatch(stream, 2, 2, 4, RCOL_OPERANDS(0x00, 0x00, 0xff));

    CHECK_EQ(rcol_surface[0], 0xaa);
    CHECK_EQ(rcol_surface[1], 0xaa);
    CHECK_EQ(rcol_surface[2], RCOL_SENTINEL);
    CHECK_EQ(rcol_surface[3], RCOL_SENTINEL);
    CHECK_EQ(rcol_surface[4], 0xbb);
    CHECK_EQ(rcol_surface[5], 0xbb);
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

    RUN_TEST(rmpal_fill_run_writes_remapped_byte);
    RUN_TEST(rmpal_literal_run_remaps_each_stream_byte);
    RUN_TEST(rmpal_skip_run_leaves_backdrop_untouched);
    RUN_TEST(rmpal_stretched_run_leaves_gaps_unwritten);
    RUN_TEST(rmpal_all_four_ops_in_one_row);
    RUN_TEST(rmpal_skip_consumes_no_stream_byte);
    RUN_TEST(rmpal_run_length_tops_out_at_64);
    RUN_TEST(rmpal_second_row_starts_after_row_advance);

    RUN_TEST(rcol_fill_run_writes_the_recoloured_byte);
    RUN_TEST(rcol_fill_run_never_reads_the_destination);
    RUN_TEST(rcol_literal_run_recolours_each_stream_byte);
    RUN_TEST(rcol_stretched_run_writes_the_odd_columns);
    RUN_TEST(rcol_skip_run_leaves_backdrop_untouched);
    RUN_TEST(rcol_skip_consumes_no_stream_byte);
    RUN_TEST(rcol_color_base_add_wraps_in_eight_bits);
    RUN_TEST(rcol_shipped_operands_collapse_to_one_index);
    RUN_TEST(rcol_all_four_ops_in_one_row);
    RUN_TEST(rcol_run_length_tops_out_at_64);
    RUN_TEST(rcol_second_row_starts_after_row_advance);
}
