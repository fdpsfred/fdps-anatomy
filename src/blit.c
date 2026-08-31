/* blit.c -- rectangle blit primitives.
 *
 * See blit.h for the destination convention.  Nothing here owns state: every
 * routine works entirely on the surface and the coordinates it is handed.
 */
#include <string.h>
#include "blit.h"

/* The VGA graphics aperture as a flat linear address, and the mode 13h
   scanline pitch in bytes.  Both are hard-coded in the original (ADD
   EAX,0xa0000 and IMUL EAX,[EBP+0x18],0x140) and both stay literals here:
   0xa0000 is where the display adapter answers, not the address of anything
   the linker places, so there is no symbol to reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The shape of the shade ramp fdps_blit_tint_rect is handed: one row per
   weight, one word per palette entry, and a second block of nine rows holding
   the complementary weights.  SHL EAX,0xa at 00030051 scales the weight by
   0x100 words and the 0x900 at 00030029 is the nine-row step to the
   complementary block.  Both agree with the table fdps_build_palette_tables
   fills -- 4608 words, 18 rows of 0x100. */
#define SHADE_RAMP_ROW_ENTRIES 0x100
#define SHADE_RAMP_COMPLEMENT_ROWS (9 * SHADE_RAMP_ROW_ENTRIES)

/* 0002e5e0.  The square's side is cell_pitch - 1 in both directions: the loop
   bound at 0002e60a and the memset length at 0002e61d are the same DEC EAX on
   the same argument.  That is deliberate -- it is what spaces the battle map
   overview's markers one pixel apart -- and blit.h says what closing the gap
   would look like.

   CMP EAX,dword ptr [EBP + -0x4] / JG is the signed compare, and both operands
   are signed ints: cell_pitch of 0 gives the bound -1 and the loop never runs.
   Reading cell_pitch as unsigned turns that into a walk of four billion
   scanlines across the whole address space.

   The row address is held in one local that is advanced by a whole scanline
   per iteration (ADD dword ptr [EBP + -0x8],0x140); memset's return value is
   discarded, so the walk does not depend on what the CRT hands back.

   Neither coordinate is clipped and no bound is checked.  Both call sites in
   the original are in fdps_battle_map_overview, which centres the map on the
   screen and can hand this routine a corner outside it for a map big enough;
   the original writes there anyway and so does this. */
void fdps_fill_screen_square(int x, int y, int color, int cell_pitch)
{
    unsigned char *row_dst;
    int row;

    row_dst = (unsigned char *) (VGA_SCREEN_BASE + y * VGA_SCREEN_PITCH + x);

    for (row = 0; row < cell_pitch - 1; row++) {
        memset(row_dst, color, (size_t) (cell_pitch - 1));
        row_dst += VGA_SCREEN_PITCH;
    }
}

/* 0002f2f0.  Two loops that never both run: CMP dword ptr [EBP + 0x18],0x0 /
   JNZ at 0002f308 picks the copy loop when src_stride is non-zero and falls
   through into the fill loop when it is zero.  Both loops are counted the same
   way -- MOV [EBP-0x10],0x0 then CMP EAX,[EBP+0x28] / JL -- so the compare
   against rows is signed and rows <= 0 leaves the destination alone.

   Both cursors are read out of the arguments into locals before the branch is
   taken (0002f2fc and 0002f302), which is why the caller's own pointers are
   never advanced.  The copy loop advances the source by src_stride and the
   destination by dst_stride independently (0002f378 and 0002f37e); the fill
   loop advances only the destination (0002f341), because it has no source.

   src_or_fill carries the source pointer in copy mode and the fill byte in
   fill mode; the fill branch takes its own copy of it into [EBP-0xc] at
   0002f30e and hands that to memset, which uses only the low 8 bits.

   The copy is memmove (CALL 0x0003d514), not memcpy: a row whose source and
   destination overlap comes out shifted rather than smeared, and the
   transition routines that slide a page across itself depend on that.  Each
   row is one call -- a run of rows with matching strides is NOT one long
   transfer, and folding them into one would change both the timing and the
   overlap behaviour.

   Nothing is clipped, no length is checked and there is no transparency test:
   bytes_per_row bytes are written on each of rows rows wherever the arithmetic
   lands. */
void fdps_blit_rect(unsigned int src_or_fill, int src_stride, void *dst,
                    int dst_stride, int bytes_per_row, int rows)
{
    /* Declared in this order because that is the order the original's frame
       is laid out in: [EBP-4] is the destination cursor, [EBP-8] the source
       cursor, [EBP-0xc] the fill value and [EBP-0x10] the row counter, and
       wcc386 hands out the slots in declaration order.  Nothing depends on
       it -- the arrangement is codegen, not behaviour -- but written this way
       the object file this compiles to differs from the original's bytes only
       in the two idioms the toolchain settings decide. */
    unsigned char *dst_cursor;
    unsigned char *src_cursor;
    unsigned int fill_value;
    int row;

    src_cursor = (unsigned char *) src_or_fill;
    dst_cursor = (unsigned char *) dst;

    if (src_stride == 0) {
        fill_value = src_or_fill;

        for (row = 0; row < rows; row++) {
            memset(dst_cursor, (int) fill_value, (size_t) bytes_per_row);
            dst_cursor += dst_stride;
        }
    } else {
        for (row = 0; row < rows; row++) {
            memmove(dst_cursor, src_cursor, (size_t) bytes_per_row);
            src_cursor += src_stride;
            dst_cursor += dst_stride;
        }
    }
}

/* 0002f390.  One shape, no mode branch: the arguments are copied into the two
   cursors at 0002f39c and 0002f3a2 and then the row loop runs unconditionally.
   Both loop tests are signed -- CMP EAX,[EBP+0x28] / JL at 0002f3b2 for the
   rows and CMP EAX,[EBP+0x24] / JL at 0002f3cb for the columns -- so a width
   or a height of 0 or less transfers nothing at all.

   The inner body is the whole point of the routine.  XOR EAX,EAX / MOV AL,
   byte ptr [EDX] at 0002f3e0 loads one source byte zero-extended into the
   pixel slot, CMP dword ptr [EBP-0x10],0x0 / JZ at 0002f3e7 skips the store
   when it is zero, and only the surviving byte is written through
   MOV byte ptr [EDX],AL at 0002f3f6.  There is no per-row transfer and no
   call of any kind: the destination is read-modify-write, and the pixels a
   zero source byte passes over keep whatever the previous blits left there.

   Both cursors are indexed by the same column counter and are advanced by
   their own stride once per row (0002f3fd and 0002f403), which is what lets
   the source sheet and the destination page have different pitches -- 0x75
   and 0x7d for the two gauge sheets, 0x138 for the offscreen pages.  A
   src_stride of 0 is not a fill mode here as it is in fdps_blit_rect above;
   it just points every row at the same source row.

   Nothing is clipped and no extent is checked. */
void fdps_blit_transparent_rect(unsigned char *src, int src_stride,
                                unsigned char *dst, int dst_stride,
                                int width, int height)
{
    /* Declared in the order the original's frame is laid out, as in
       fdps_blit_rect above: [EBP-4] the destination cursor, [EBP-8] the
       source cursor, [EBP-0xc] the column counter, [EBP-0x10] the pixel and
       [EBP-0x14] the row counter.  Codegen, not behaviour. */
    unsigned char *dst_cursor;
    unsigned char *src_cursor;
    int column;
    unsigned int pixel;
    int row;

    src_cursor = src;
    dst_cursor = dst;

    for (row = 0; row < height; row++) {
        for (column = 0; column < width; column++) {
            pixel = (unsigned int) src_cursor[column];

            if (pixel != 0) {
                dst_cursor[column] = (unsigned char) pixel;
            }
        }

        src_cursor += src_stride;
        dst_cursor += dst_stride;
    }
}

/* 0002fe40.  Three nested loops -- cell rows, cell columns, then destination
   scanlines inside one cell -- with a clamp at the top of each of the outer
   two and a snap at the bottom.

   THE CELL COUNTS ARE CEILINGS.  0002fe4c and 0002fe71 each divide, then
   divide a second time and TEST EDX,EDX / JZ to add one when the remainder is
   non-zero, so a region that does not fill its last cell still gets that cell.
   Both are IDIV, not DIV, and every loop test in the routine is JL while every
   overrun test is JLE against a signed operand: width, height, block_w and
   block_h are all signed ints.

   THE TWO SAMPLE POSITIONS ARE ACCUMULATORS THAT ARE SNAPPED, NOT CLAMPED.
   sample_col starts at block_w/2 and grows by block_w; the moment it reaches
   width (CMP EAX,[EBP+0x24] / JL at 0002ff9f, so the snap runs on >=) it is
   set to width - width%block_w and run_width to that remainder.  sample_row
   does the same against height at 0002ffda.  That lands on the FIRST pixel of
   the trailing partial cell rather than its middle, which is not what
   min(col*block_w + block_w/2, width-1) produces -- blit.h says exactly where
   the two disagree and what it costs.

   Nothing else is clamped either.  A block wider than the region leaves
   sample_col at block_w/2, outside the region, and that byte is read and
   flooded across the one cell the region has.  The final snap of each loop
   leaves run_width or band_height at 0 whenever the extent divides exactly;
   that value is never used only because the loop that would use it has just
   ended, so a rewrite that iterated once more would read a pixel past the
   region and write a zero-length run.

   ONE memset PER DESTINATION ROW.  0002ff83 calls memset once per scanline of
   the cell with band_row + dest_x, the sampled byte and run_width, then adds
   dst_stride (0002ff8b).  The cell is never one long run even when the rows
   are contiguous, and the caller wraps each whole call in a 0x3da retrace
   wait, so the animation's pace comes from the retrace and not from here.  The
   sampled byte is loaded MOV AL,[EAX] / AND EAX,0xff -- zero-extended, so the
   pixel is unsigned.

   The destination band pointer advances by dst_stride * block_h (0002ffca)
   while dest_y advances by band_height, which differ only on the last band --
   after which the cell-row loop is over.  Nothing is read back from the
   destination.  Neither surface is bounds-checked. */
void fdps_blit_mosaic_rect(unsigned char *src, int src_stride,
                           unsigned char *dst, int dst_stride, int width,
                           int height, int block_w, int block_h)
{
    /* Declared in the order the original's frame is laid out, as in the two
       routines above: [EBP-4] the scanline cursor inside a cell, [EBP-8] the
       band's first row, [EBP-0xc] the band pointer, [EBP-0x10] the sampled
       source row, then the six counters and extents down to [EBP-0x3c].
       Codegen, not behaviour. */
    unsigned char *band_row;
    unsigned char *band_top;
    unsigned char *dst_band;
    unsigned char *src_row;
    int dest_y;
    int dest_x;
    int sample_row;
    int sample_col;
    int band_height;
    int run_width;
    int cell_rows;
    int cell_cols;
    int cell_row;
    int cell_col;
    int fill_row;

    cell_cols = width / block_w;
    if (width % block_w != 0) {
        cell_cols++;
    }

    cell_rows = height / block_h;
    if (height % block_h != 0) {
        cell_rows++;
    }

    sample_row = block_h / 2;
    band_height = block_h;
    dst_band = dst;
    dest_y = 0;

    for (cell_row = 0; cell_row < cell_rows; cell_row++) {
        src_row = src + sample_row * src_stride;
        run_width = block_w;
        sample_col = block_w / 2;
        band_top = dst_band;
        dest_x = 0;

        if (dest_y + block_h > height) {
            band_height = height - dest_y;
        }

        for (cell_col = 0; cell_col < cell_cols; cell_col++) {
            if (dest_x + block_w > width) {
                run_width = width - dest_x;
            }

            band_row = band_top;

            for (fill_row = 0; fill_row < band_height; fill_row++) {
                memset(band_row + dest_x, (int) src_row[sample_col],
                       (size_t) run_width);
                band_row += dst_stride;
            }

            dest_x += run_width;
            sample_col += block_w;

            if (sample_col >= width) {
                run_width = width % block_w;
                sample_col = width - run_width;
            }
        }

        dest_y += band_height;
        dst_band += dst_stride * block_h;
        sample_row += block_h;

        if (sample_row >= height) {
            band_height = height % block_h;
            sample_row = height - band_height;
        }
    }
}

/* 00030010.  Two nested loops with no call in them at all, over a frame set up
   by four instructions at the top.

   CMP dword ptr [EBP+0x38],0x8 / JG at 0003001c is the fold, and it is a
   SIGNED test.  The two branches differ only in which of the two row offsets
   gets 0 and which gets 0x900, plus the MOV EAX,0x10 / SUB EAX,[EBP+0x38] at
   00030040 that rewrites alpha in place on the greater-than side.  Both offsets
   are dword counts, not byte counts: 0003005f and 000300b6 are both
   LEA EAX,[EAX*0x4 + 0x0] applied after the offset has already been added, so
   0x900 is 0x900 entries and lands nine rows on.

   SHL EAX,0xa at 00030051 turns the folded alpha into the byte offset of its
   row -- 0x100 dwords -- and that base is held in [EBP-0x4] for the whole run.
   The tint's scaled colour is read once, before either loop, at 00030069.

   Both loop tests are signed: CMP EAX,[EBP+0x28] / JL at 00030078 counts the
   rows and CMP EAX,[EBP+0x24] / JL at 00030094 counts the columns, so either
   extent at 0 or below transfers nothing.

   The inner body has no test in it.  XOR EAX,EAX / MOV AL,byte ptr [EDX] at
   000300a9 loads the source byte zero-extended -- so pixel 0xff indexes entry
   255 of the row and not entry -1 -- and MOV byte ptr [EDX],AL at 000300fa is
   reached unconditionally.  There is no transparency key here: unlike
   fdps_blit_transparent_rect above, a source pixel of 0 is blended and written
   like any other.

   SAR EAX,0x4 at 000300cb and SAR EAX,0xc at 000300e4 are arithmetic shifts
   while the entries are unsigned here, so both come out as SHR.  Neither shift
   can tell the two forms apart: the AND 0xf0f0f between them keeps only bits
   0..19, and no bit the two disagree about survives.  Nor can the operand
   reach them -- a ramp entry is a nibble per byte times a weight of at most
   16, so no byte of the sum exceeds 0xf0 and bits 24..31 are always clear.

   ADD dword ptr [EBP+0x14],EAX and ADD dword ptr [EBP+0x1c],EAX at 00030101
   and 00030107 advance the argument slots themselves rather than a pair of
   local cursors, so src and dst are walked directly here and no separate
   cursor is kept.

   Nothing is clipped and no extent is checked against either surface. */
void fdps_blit_tint_rect(unsigned char *src, int src_stride, unsigned char *dst,
                         int dst_stride, int width, int height,
                         unsigned int *shade_ramp,
                         unsigned char *inverse_palette_cube, int tint_color,
                         int alpha)
{
    unsigned int *weight_row;
    unsigned int scaled_tint;
    int tint_row_offset;
    int source_row_offset;
    int source_pixel;
    unsigned int blended;
    unsigned int cube_index;
    int row;
    int column;

    if (alpha <= 8) {
        tint_row_offset = 0;
        source_row_offset = SHADE_RAMP_COMPLEMENT_ROWS;
    } else {
        tint_row_offset = SHADE_RAMP_COMPLEMENT_ROWS;
        source_row_offset = 0;
        alpha = 16 - alpha;
    }

    weight_row = shade_ramp + alpha * SHADE_RAMP_ROW_ENTRIES;
    scaled_tint = weight_row[tint_color + tint_row_offset];

    for (row = 0; row < height; row++) {
        for (column = 0; column < width; column++) {
            source_pixel = (int) src[column];
            blended = (scaled_tint
                       + weight_row[source_pixel + source_row_offset]) >> 4;
            blended &= 0x000f0f0fu;
            cube_index = (blended & 0xffffu) | (blended >> 12);
            dst[column] = inverse_palette_cube[cube_index];
        }

        src += src_stride;
        dst += dst_stride;
    }
}
