/* blit.c -- rectangle blit primitives, plus the rotate-and-scale blit.
 *
 * See blit.h for the destination convention.  Nothing here owns state: every
 * routine works entirely on the surface and the coordinates it is handed.
 */
#include <math.h>
#include <string.h>
#include "blit.h"

/* The VGA graphics aperture as a flat linear address, and the mode 13h
   scanline pitch in bytes.  Both are hard-coded in the original (ADD
   EAX,0xa0000 and IMUL EAX,[EBP+0x18],0x140) and both stay literals here:
   0xa0000 is where the display adapter answers, not the address of anything
   the linker places, so there is no symbol to reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The shape of the shade ramp the two tinting blits are handed: one row per
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

/* 00030120.  fdps_blit_tint_rect's colour-keyed twin, instruction for
   instruction, with one test added inside the inner loop.

   Everything above the loops is the same shape as at 00030010.  CMP dword ptr
   [EBP+0x38],0x8 / JG at 0003012c is the signed fold and the two branches
   differ only in which row offset gets 0 and which gets 0x900, plus the
   MOV EAX,0x10 / SUB EAX,[EBP+0x38] at 00030150 that rewrites alpha in place.
   Both offsets are dword counts: 0003016f and 000301cc are both
   LEA EAX,[EAX*0x4 + 0x0] applied after the offset has already been added.
   SHL EAX,0xa at 0003015e turns the folded alpha into the byte offset of its
   row, that base is kept in [EBP-0x4] for the whole run, and the tint's scaled
   colour is read once at 00030179 -- before either loop, so a source rectangle
   that is entirely transparent still costs that one fetch and nothing else.

   Both loop tests are signed: CMP EAX,[EBP+0x28] / JL at 0003018b counts the
   rows and CMP EAX,[EBP+0x24] / JL at 000301a7 the columns.

   THE KEY IS THE WHOLE DIFFERENCE.  XOR EAX,EAX / MOV AL,byte ptr [EDX] at
   000301b9 loads the source byte zero-extended, and CMP dword ptr [EBP-0x8],
   0x0 / JZ 0x00030212 at 000301c0 jumps over the entire blend -- the second
   table read, the addition, the shift, the mask, the fold and the store alike.
   A source pixel of 0 costs no lookup and leaves the destination byte exactly
   as the previous blits left it, which is what fdps_blit_tint_rect does not do.

   The blend itself is byte for byte the sibling's: SAR EAX,0x4 at 000301e1,
   AND 0xf0f0f at 000301e7, then (v & 0xffff) | (v >> 12) at 000301ee-000301ff
   with green ending up above red.  Both shifts are arithmetic in the original
   and come out logical here, which no operand can tell apart -- the mask
   between them keeps only bits 0..19 and a ramp entry is a nibble per byte
   times a weight of at most 16, so bits 24..31 are always clear.

   ADD dword ptr [EBP+0x14],EAX at 00030217 and ADD dword ptr [EBP+0x1c],EAX at
   0003021d advance the argument slots themselves, and they sit outside the key,
   so a row whose pixels are all transparent still steps both cursors.  The
   original holds the source pixel and the blended value in the one slot
   [EBP-0x8]; splitting them into two named locals here is codegen, not
   behaviour.

   Nothing is clipped and no extent is checked against either surface. */
void fdps_blit_tint_transparent_rect(unsigned char *src, int src_stride,
                                     unsigned char *dst, int dst_stride,
                                     int width, int height,
                                     unsigned int *shade_ramp,
                                     unsigned char *inverse_palette_cube,
                                     int tint_color, int alpha)
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

            if (source_pixel != 0) {
                blended = (scaled_tint
                           + weight_row[source_pixel + source_row_offset]) >> 4;
                blended &= 0x000f0f0fu;
                cube_index = (blended & 0xffffu) | (blended >> 12);
                dst[column] = inverse_palette_cube[cube_index];
            }
        }

        src += src_stride;
        dst += dst_stride;
    }
}

/* 00030230.  The two-source member of the family: the same ramp, the same
   shift, mask and fold as the two tinting blits above, with the constant
   colour replaced by a second rectangle that is read pixel for pixel alongside
   the first.

   THE FOLD SWAPS THE ARGUMENTS, NOT THE ROW OFFSETS.  CMP dword ptr
   [EBP+0x3c],0x8 / JLE at 0003023c is the signed test, and the branch it
   guards is six MOVs through two scratch slots: 00030242..00030251 exchange
   the two source POINTERS in [EBP+0x14] and [EBP+0x1c], and
   00030254..00030263 exchange the two STRIDES in [EBP+0x18] and [EBP+0x20],
   before MOV EAX,0x10 / SUB EAX,[EBP+0x3c] at 00030266 rewrites alpha.  Both
   halves matter.  The pointer swap is what puts the heavier-weighted
   rectangle on the row-alpha lookup; the stride swap is what keeps each
   cursor advancing by the pitch of the rectangle it is now walking.  Leaving
   the strides behind agrees with the original for a single row and diverges
   from the second row on, and the callers' two pitches really do differ --
   0x97 or 0x12e for the panel against 0x140 for the screen.

   After the fold there is only one pair of lookups and no second branch:
   00030274 SHL EAX,0xa scales the folded alpha into the byte offset of its
   row and [EBP-0x8] holds that base for the whole run, 000302d4
   LEA EAX,[EAX*0x4+0x0] / ADD [EBP-0x8] reads the fg pixel at offset 0, and
   000302f0 MOV EAX,[EAX+0x2400] reads the bg pixel nine rows on.  0x2400 is a
   BYTE displacement applied after the scale, so it is 0x900 entries -- the
   same nine rows the tinting blits reach with a 0x900 added before their own
   LEA.

   Both loop tests are signed: CMP EAX,[EBP+0x30] / JL at 00030289 counts the
   rows and CMP EAX,[EBP+0x2c] / JL at 000302a5 the columns.  Both source
   loads are XOR EAX,EAX / MOV AL,byte ptr [EDX], at 000302bd and 000302ca, so
   a pixel of 0xff indexes entry 255 of its row and not entry -1.

   There is no test of any kind inside the inner loop: MOV byte ptr [EDX],AL at
   0003032e is reached unconditionally, so a source pixel of 0 on either side
   is blended and stored like any other.  That is the whole difference from
   fdps_blit_blend_transparent_rect at 00030360.

   SAR EAX,0x4 at 000302ff and SAR EAX,0xc at 00030318 are arithmetic while the
   entries are unsigned here, and as with the tinting blits no operand can tell
   the two apart: the AND 0xf0f0f between them keeps only bits 0..19, and a
   ramp entry is one nibble per byte times a weight of at most 16, so no byte
   of the sum exceeds 0xf0 and bits 24..31 are always clear.

   The three row advances at 00030335..00030344 add each stride to its own
   argument slot, so all three cursors are walked directly and no separate
   cursor is kept.  The store happens before the next column is read, which is
   what lets fdps_message_window_open pass one address as both bg and dst.

   Nothing is clipped and no extent is checked against any of the three
   surfaces. */
void fdps_blit_blend_rect(unsigned char *fg, int fg_stride, unsigned char *bg,
                          int bg_stride, unsigned char *dst, int dst_stride,
                          int width, int height, unsigned int *shade_ramp,
                          unsigned char *inverse_palette_cube, int alpha)
{
    unsigned char *swapped_source;
    int swapped_stride;
    unsigned int *weight_row;
    int foreground_pixel;
    int background_pixel;
    unsigned int blended;
    unsigned int cube_index;
    int row;
    int column;

    if (alpha > 8) {
        swapped_source = fg;
        fg = bg;
        bg = swapped_source;

        swapped_stride = fg_stride;
        fg_stride = bg_stride;
        bg_stride = swapped_stride;

        alpha = 16 - alpha;
    }

    weight_row = shade_ramp + alpha * SHADE_RAMP_ROW_ENTRIES;

    for (row = 0; row < height; row++) {
        for (column = 0; column < width; column++) {
            foreground_pixel = (int) fg[column];
            background_pixel = (int) bg[column];

            blended = (weight_row[foreground_pixel]
                       + weight_row[background_pixel
                                    + SHADE_RAMP_COMPLEMENT_ROWS]) >> 4;
            blended &= 0x000f0f0fu;
            cube_index = (blended & 0xffffu) | (blended >> 12);
            dst[column] = inverse_palette_cube[cube_index];
        }

        fg += fg_stride;
        bg += bg_stride;
        dst += dst_stride;
    }
}

/* 00030360.  fdps_blit_blend_rect at 00030230 with one test added inside the
   inner loop -- and the whole difficulty of the routine is that the test does
   not read the rectangle the blend reads.

   THE KEY IS THE ORIGINAL FIRST RECTANGLE, NOT THE POST-SWAP FOREGROUND.  The
   two instructions before the fold are MOV EAX,[EBP+0x18] / MOV [EBP-0x10],EAX
   at 0003036c and MOV EAX,[EBP+0x14] / MOV [EBP-0x4],EAX at 00030372: the
   stride first, then the pointer, both taken from the fg arguments while they
   still hold what the caller passed.  Those two slots are what the inner loop
   reads at 00030400 (MOV EAX,[EBP-0x4] / ADD EAX,[EBP-0x1c] / CMP byte ptr
   [EAX],0x0 / JZ) and what the row advance steps at 00030488 (ADD [EBP-0x4],
   [EBP-0x10]).  Neither is touched by the swap at 0003037e..000303a2, which
   exchanges only the argument slots.

   So the mask is a FOURTH cursor with its own pointer and its own stride, and
   for every alpha above 8 it is the rectangle that is being read as the
   BACKGROUND term by then.  Writing the obvious form -- swap the two operands,
   then test fg[column] -- keys on the background instead of the sprite, and the
   sprite's transparent pixels come out painted.  That path is live:
   fdps_unit_award_exp_and_level_up feeds alpha = n / 3 + 8, which crosses 8 as
   the floating experience number ramps up, so the same sprite is drawn keyed
   correctly below the crossing and wrongly above it.

   THE MASK STRIDE IS THE ORIGINAL FOREGROUND STRIDE TOO.  ADD [EBP-0x4],
   [EBP-0x10] adds the saved stride, not the possibly-swapped [EBP+0x18], so
   above the fold the mask walks by the sprite's pitch while the foreground
   cursor walks by the background's.  Keeping the mask pointer but letting it
   advance with fg agrees for one row and diverges from the second on, and the
   callers' pitches do differ: fdps_draw_unit_gauge passes 0x2b for the gauge
   sheet against the page's own pitch, fdps_unit_award_exp_and_level_up 0x28
   against 0x168.

   Everything else is the sibling's, instruction for instruction.  CMP dword ptr
   [EBP+0x3c],0x8 / JLE at 00030378 is the signed fold and it exchanges both the
   pointers (0003037e..0003038d) and the strides (00030390..0003039f) before
   MOV EAX,0x10 / SUB EAX,[EBP+0x3c] rewrites alpha.  SHL EAX,0xa at 000303b0
   scales the folded alpha into the byte offset of its ramp row and [EBP-0xc]
   holds that base for the whole run; 0003041b LEA EAX,[EAX*0x4+0x0] reads the
   foreground entry at offset 0 and 00030437 MOV EAX,[EAX+0x2400] the background
   entry nine rows on, 0x2400 being a byte displacement applied after the scale.
   Both loop tests are signed -- CMP EAX,[EBP+0x30] / JL at 000303c5 for the rows
   and CMP EAX,[EBP+0x2c] / JL at 000303e1 for the columns.

   THE FOREGROUND PIXEL IS FETCHED BEFORE THE KEY IS TESTED.  000303f3..000303fd
   load fg[column] zero-extended, and only then does 00030406 test the mask; the
   background pixel at 0003040b is inside the branch.  Nothing observable turns
   on it -- the load has no side effect -- but it is what the original does and
   it is why a fully keyed-out row still touches the foreground rectangle.

   SAR EAX,0x4 at 00030446 and SAR EAX,0xc at 0003045f are arithmetic while the
   entries are unsigned here, and as everywhere else in this family no operand
   can tell the two apart: the AND 0xf0f0f between them keeps only bits 0..19,
   and a ramp entry is one nibble per byte times a weight of at most 16, so no
   byte of the sum exceeds 0xf0 and bits 24..31 are always clear.

   The three remaining row advances at 0003047c..00030494 add each stride to its
   own argument slot, and all four sit outside the key, so a row with no
   surviving pixel still steps every cursor.  Nothing is clipped and no extent is
   checked against any of the four surfaces. */
void fdps_blit_blend_transparent_rect(unsigned char *fg, int fg_stride,
                                      unsigned char *bg, int bg_stride,
                                      unsigned char *dst, int dst_stride,
                                      int width, int height,
                                      unsigned int *shade_ramp,
                                      unsigned char *inverse_palette_cube,
                                      int alpha)
{
    unsigned char *mask_row;
    int mask_stride;
    unsigned char *swapped_source;
    int swapped_stride;
    unsigned int *weight_row;
    int foreground_pixel;
    int background_pixel;
    unsigned int blended;
    unsigned int cube_index;
    int row;
    int column;

    /* Saved before the fold, in this order, and never swapped: these two are
       the key's own cursor for the whole run. */
    mask_stride = fg_stride;
    mask_row = fg;

    if (alpha > 8) {
        swapped_source = fg;
        fg = bg;
        bg = swapped_source;

        swapped_stride = fg_stride;
        fg_stride = bg_stride;
        bg_stride = swapped_stride;

        alpha = 16 - alpha;
    }

    weight_row = shade_ramp + alpha * SHADE_RAMP_ROW_ENTRIES;

    for (row = 0; row < height; row++) {
        for (column = 0; column < width; column++) {
            foreground_pixel = (int) fg[column];

            if (mask_row[column] != 0) {
                background_pixel = (int) bg[column];

                blended = (weight_row[foreground_pixel]
                           + weight_row[background_pixel
                                        + SHADE_RAMP_COMPLEMENT_ROWS]) >> 4;
                blended &= 0x000f0f0fu;
                cube_index = (blended & 0xffffu) | (blended >> 12);
                dst[column] = inverse_palette_cube[cube_index];
            }
        }

        fg += fg_stride;
        bg += bg_stride;
        mask_row += mask_stride;
        dst += dst_stride;
    }
}

/* 00031920.  The rotate-and-scale blit, and the only routine in this file with
   an FPU in it.  The projection is worked out in floating point once per
   destination row; everything inside the column loop is plain fixed-point
   integer sampling.

   THE sin AND cos CALLS ARE THE ORDINARY LIBRARY ONES, NOT AN INTRINSIC.
   0003193e..00031958 is FLD float / SUB ESP,0x8 / FSTP qword ptr [ESP] / CALL /
   the double coming back in EDX:EAX -- the stack-convention library call --
   four times over, for sin(tilt), cos(tilt), sin(rotation) and cos(rotation)
   in that order.  math.h in 10.0a carries an unguarded
   #pragma intrinsic(log,cos,sin,...), but that pragma only bites once the
   optimiser is on: under -od wcc386 emits this same call sequence with the
   pragma in force, measured byte for byte against the original.  Each result is
   stored down to a FLOAT local (FSTP dword ptr), so every product below is
   taken against a single-precision sine or cosine and not a double one.

   EVERY DIVIDE IN THE SAMPLING PATH TRUNCATES TOWARD ZERO, WHICH IS NOT A
   SHIFT.  The SAR 0x1f / SHL / SBB / SAR idiom appears four times -- at
   00031a28 and 00031a44 for center_y / 4 and center_x / 4, and at 00031b45 and
   00031b61 for sample_y / 128 and sample_x / 128.  Writing >> 7 for the last
   two, which is the obvious thing to write in a fixed-point sampler, floors
   instead, and both offsets are negative for every pixel left of or above the
   centre: the whole left half and top half of the picture move a source pixel.
   The tell is visible at the middle itself -- with truncation the seven
   destination rows whose offset lies between -128 and 128 all read the same
   source row, and with a shift only four do.  The multiplications by 32 at
   000319ba and by 32 and 5088 at 00031a73 and 00031a84 are shifts and IMULs in
   the original and are left as multiplications here, where the two forms agree.

   THE SAMPLE BASE CARRIES AN UNCONDITIONAL + 4 (ADD EAX,0x4 at 00031a57).  The
   picture is therefore drawn four bytes off its nominal centre: at 1:1 the
   destination is exactly the source shifted four bytes, so the top-left
   destination pixel is source byte 4 and the rightmost column of each row reads
   into the start of the following source row.  It is not a rounding term and
   there is nothing to correct.

   THE LOOPS ARE 318 COLUMNS AND 198 ROWS, NOT 320 AND 200.  MOV [EBP-0x34],
   0xffffff9d / CMP ...,0x63 counts rows -99..98 and MOV [EBP-0x38],0xffffff61 /
   CMP ...,0x9f counts columns -159..158.  The destination cursor is then
   stepped twice more at 00031b9a and 00031ba0, which is what makes the walk a
   320-byte stride, and the destination's last two columns and bottom two rows
   are never written at all.

   THE CLIP TEST IS FOUR SIGNED COMPARES AND ITS FAILING SIDE COMES FIRST.
   00031b1e JL, 00031b26 JLE, 00031b30 JGE and 00031b3a JLE are all signed, and
   the branch they guard puts MOV dword ptr [EBP-0x40],0x0 at 00031b3c -- ahead
   of the sampling block at 00031b45 -- which is the layout of an || chain whose
   body is the transparent case.  Both bounds are inclusive: a sample exactly on
   the window's far edge is read, not keyed out.

   The sample base is recomputed on every row (00031a28..00031a5a) although
   nothing in it varies; it is left inside the loop because that is where the
   original computes it.

   Neither surface is bounds-checked and neither pointer is advanced by a
   stride: the source is addressed at a fixed 0x140 pitch and the destination is
   walked byte by byte. */
void fdps_blit_rotated_scaled(unsigned char *dst, unsigned char *src,
                              int camera_height, float tilt, int center_x,
                              int center_y, float rotation)
{
    /* Declared in the order the original's frame is laid out, as everywhere
       else in this file: [EBP-4] the sample base down to [EBP-0x40] the pixel,
       then the four float locals at [EBP-0x44]..[EBP-0x50].  Codegen, not
       behaviour. */
    unsigned char *sample_base;
    int clip_y_min;
    int clip_y_max;
    int clip_x_min;
    int clip_x_max;
    int sample_y;
    int column_step_y;
    int row_origin_x;
    int row_source_y;
    int projection_distance;
    int column_step_x;
    int sample_x;
    int row;
    int column;
    int row_camera_height;
    int pixel;
    float cos_rotation;
    float sin_rotation;
    float sin_tilt;
    float cos_tilt;

    /* The first store is dead in the original too: MOV dword ptr [EBP-0x28],
       0x190 at 0003192c is overwritten three instructions later by
       MOV EAX,0x7d0 / SUB EAX,camera_height / MOV [EBP-0x28],EAX.  400 never
       reaches any of the arithmetic below; it is kept because it is what the
       original's frame does and it costs nothing to keep. */
    projection_distance = 400;
    projection_distance = 2000 - camera_height;

    sin_tilt = (float) sin(tilt);
    cos_tilt = (float) cos(tilt);
    sin_rotation = (float) sin(rotation);
    cos_rotation = (float) cos(rotation);

    /* The reachable source area as offsets from the centre, in 1/128 pixels:
       center_x and center_y are quarter pixels, 0x4f8 is 318 * 4 and 0x318 is
       198 * 4, and the * 32 turns a quarter pixel into 1/128 pixels. */
    clip_x_max = (0x4f8 - center_x) * 32;
    clip_x_min = -center_x * 32;
    clip_y_max = (0x318 - center_y) * 32;
    clip_y_min = -center_y * 32;

    for (row = -99; row < 99; row++) {
        /* The perspective divide.  projection_distance + camera_height is 2000
           by construction and the original really does add the two back
           together (MOV EAX,[EBP-0x28] / ADD EAX,[EBP+0x1c] at 00031a0c)
           rather than using the constant. */
        row_source_y = (int) (row * (projection_distance + camera_height)
                              / (projection_distance * cos_tilt
                                 + row * sin_tilt));

        sample_base = src + (center_y / 4) * VGA_SCREEN_PITCH + center_x / 4 + 4;

        /* The camera height this row projects from.  With tilt 0 the sine is
           zero and every row shares the argument's own height, which is what
           makes an untilted picture a uniform scale. */
        row_camera_height = (int) (camera_height - row_source_y * sin_tilt);

        column_step_x = row_camera_height * 32 / projection_distance + 32;
        row_origin_x = -5088 - row_camera_height * 5088 / projection_distance;

        /* Quarter pixels into 1/128 pixels, so the row's y offset is in the
           same units as row_origin_x before the pair is rotated. */
        row_source_y *= 32;

        sample_x = (int) (row_origin_x * cos_rotation
                          + row_source_y * sin_rotation);
        sample_y = (int) (-row_origin_x * sin_rotation
                          + row_source_y * cos_rotation);

        /* This pair must stay in this order: the y delta is taken from the
           unrotated step and the next line then overwrites that step with the
           x delta, in the one slot [EBP-0x2c] the original keeps them in. */
        column_step_y = (int) (-column_step_x * sin_rotation);
        column_step_x = (int) (column_step_x * cos_rotation);

        for (column = -159; column < 159; column++) {
            if (sample_x < clip_x_min || sample_x > clip_x_max
                || sample_y < clip_y_min || sample_y > clip_y_max) {
                pixel = 0;
            } else {
                pixel = *(sample_base + (sample_y / 128) * VGA_SCREEN_PITCH
                          + sample_x / 128);
            }

            *dst = (unsigned char) pixel;
            sample_x += column_step_x;
            sample_y += column_step_y;
            dst++;
        }

        dst++;
        dst++;
    }
}
