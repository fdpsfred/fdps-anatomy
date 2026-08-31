/* blit.h -- rectangle blit primitives: solid fills, transparent and inlaid
 * copies, mosaic pixelation, tinted and blended copies, rotate-and-scale.
 *
 * Destinations in this file are 8bpp surfaces addressed by a byte pointer and
 * a pitch in bytes, the same convention text.c uses: 0x140 for the visible
 * mode 13h screen and 0x138 for the 312-wide offscreen pages.  The one
 * exception is fdps_fill_screen_square below, which takes no destination at
 * all and addresses the visible screen itself.
 */
#ifndef BLIT_H
#define BLIT_H

/* Paints a solid square of one palette colour straight into the visible mode
   13h screen at 0xa0000, one memset per scanline.

   x and y are the pixel column and row of the square's top-left corner, and
   the destination is 0xa0000 + y * 0x140 + x with no clipping and no bounds
   check of any kind: all three of x, y and cell_pitch are used exactly as
   given, so a run that passes column 319 spills into the following scanline
   and a corner outside the 320x200 screen writes outside it.

   color is a VGA palette index handed straight to memset, so only its low
   byte reaches the screen.

   THE SQUARE IS cell_pitch-1 ON A SIDE, NOT cell_pitch.  Both the row count
   and the memset length are the argument minus one, which is what leaves the
   one-pixel gaps between adjacent squares of the battle map overview; writing
   the obvious `for (i = 0; i < size; i++) memset(dst, color, size)` closes
   those gaps and makes every marker a pixel wider and taller.  cell_pitch <= 1
   paints nothing at all.

   Writing to 0xa0000 is the point of the routine and not an oversight: its
   caller has already presented its offscreen page, so the squares are painted
   over the finished frame on the live screen. */
extern void fdps_fill_screen_square(int x, int y, int color, int cell_pitch);
#pragma aux fdps_fill_screen_square "*" parm caller [];

/* Moves a rectangle of bytes row by row, with a source stride and a
   destination stride that are independent of each other and of
   bytes_per_row.  This is the workhorse the whole presentation layer goes
   through: whole-screen presents (0x140 wide, 0xc8 rows, from a 0x170-pitch
   page to 0xa0000), window backdrops saved and restored between the 0x138
   offscreen pages, and every transition wipe.

   ONE CALL PER ROW, AND IT IS memmove.  A rectangle whose rows happen to be
   contiguous at both ends is still transferred one row at a time, and the
   per-row transfer is memmove rather than memcpy, so a row that overlaps its
   own destination comes out shifted and not smeared.  Both of those are
   relied on: the transition routines slide a page across itself.

   rows and the two strides are signed.  rows <= 0 transfers nothing, and a
   negative stride walks that side of the transfer backwards up memory.

   SRC_STRIDE 0 IS NOT "REPEAT ONE SOURCE ROW", IT IS FILL MODE.  With
   src_stride 0 the source pointer is never read: src_or_fill is taken as a
   byte value and memset across bytes_per_row bytes of every row instead, and
   only its low 8 bits reach the destination.  That is why src_or_fill is
   declared as an unsigned int rather than a pointer -- it is a source address
   in one mode and a palette index in the other -- and why a caller in copy
   mode casts its pointer to pass it.

   Nothing is clipped and no length is checked; dst, the strides and the
   extents are used exactly as handed over. */
extern void fdps_blit_rect(unsigned int src_or_fill, int src_stride, void *dst,
                           int dst_stride, int bytes_per_row, int rows);
#pragma aux fdps_blit_rect "*" parm caller [];

/* The colour-keyed counterpart of fdps_blit_rect: the same rectangle of 8bpp
   pixels with the same pair of independent strides, except that every source
   byte is examined and only a non-zero one is stored.  Palette index 0 is the
   transparency key, so it is the sprite blit every unit portrait, gauge cap
   and window ornament goes through.

   THE DESTINATION IS READ-MODIFY-WRITE.  A source byte of 0 is skipped
   entirely and the destination pixel underneath survives; the routine never
   writes a 0.  Turning the row loop into the per-row memmove its sibling uses
   would paint colour 0 over whatever was already composed there.

   SRC_STRIDE 0 IS NOT FILL MODE HERE.  There is no second branch: src is
   always a pointer and is always dereferenced, and a stride of 0 simply makes
   every destination row read the same source row.  Handing this routine the
   palette index that fdps_blit_rect would have taken as a fill value
   dereferences it as an address.

   width and height are signed and are compared with JL, so either at 0 or
   below transfers nothing.  Nothing is clipped and no bound is checked: the
   callers clamp, as fdps_draw_gauge_fill does when it refuses a coordinate
   past 0x7d. */
extern void fdps_blit_transparent_rect(unsigned char *src, int src_stride,
                                       unsigned char *dst, int dst_stride,
                                       int width, int height);
#pragma aux fdps_blit_transparent_rect "*" parm caller [];

/* Redraws a width x height region of one 8bpp surface into another as a
   mosaic: the region is cut into block_w x block_h cells and each cell is
   flooded with a single colour, the source pixel nearest the middle of that
   cell.  fdps_battle_show_unit_status_window runs it over the portrait pane
   with block sizes 2,4,6,8,10 to dissolve a portrait away and 11,9,7,5,3,1
   to bring the next one in, one call per step with a retrace wait between, so
   the step sizes and the sampling are what the animation looks like.

   Both surfaces are addressed as a base pointer plus a byte pitch, and BOTH
   POINTERS ARE THE REGION'S TOP-LEFT PIXEL, not the surface's origin: the
   caller has already added the region's offset into each.

   THE SAMPLE POSITION IS AN ACCUMULATOR THAT IS SNAPPED, NOT A CLAMP.  It
   starts at block/2, advances by a whole block per cell, and the moment it
   reaches the far edge it is set to extent - extent%block -- the FIRST pixel
   of the trailing partial cell, not its middle and not the last pixel of the
   region.  Writing the obvious src[min(row*block_h + block_h/2, height-1)]
   [min(col*block_w + block_w/2, width-1)] agrees everywhere except that
   trailing partial row or column, and there only when the remainder is 2 or
   more but no larger than half the block; the portrait pane is 149 rows and
   the 7-pixel step leaves a 2-row band that the original takes from source row
   147 while the clamped form takes it from 148.

   NOTHING IS CLAMPED ANYWHERE ELSE EITHER.  A block bigger than the region
   still starts its sample at block/2, which is outside the region, and reads
   it: the region is one cell wide, flooded with a pixel from beyond its own
   right edge.  Both extents are signed and a region of 0 or less in either
   direction paints nothing, but a negative one hands memset a negative length.

   The cell interior is filled one memset per destination row, never as one
   long run, and the destination is only written -- no source pixel is read
   back out of it, so a surface may be its own source only if a cell's own
   output is not wanted as a later cell's input. */
extern void fdps_blit_mosaic_rect(unsigned char *src, int src_stride,
                                  unsigned char *dst, int dst_stride,
                                  int width, int height, int block_w,
                                  int block_h);
#pragma aux fdps_blit_mosaic_rect "*" parm caller [];

/* Copies a rectangle of 8bpp pixels with one constant palette colour blended
   over every pixel, resolving each blended colour back to a palette index
   through an inverse-palette lookup table.  This is what dims the screen for
   the game-over fade and for the VFS animation player's fade in and out: the
   caller walks alpha 0..0x10 one step per retrace and calls this once per
   step.

   Both surfaces are a base pointer plus a byte stride, as everywhere else in
   this file, and BOTH POINTERS ARE THE RECTANGLE'S TOP-LEFT PIXEL.

   THE TWO TABLES ARE THE CALLER'S, NOT THIS ROUTINE'S.  Both are the globals
   fdps_build_palette_tables fills -- data_fdps_palette_shade_ramp_table and
   data_fdps_inverse_palette_cube, declared in gamedata.h -- passed in as
   arguments, so this routine reads no global at all and the caller decides
   which tables it composites through.

   ALPHA IS FOLDED INTO 0..8 AND THE TWO ROWS SWAP, WHICH IS WHY THE RAMP HAS
   18 ROWS AND NOT 17.  Rows 0..8 carry weights 0..8 and rows 9..17 carry the
   complementary weights 16..8, which is exactly the multiplier table
   fdps_build_palette_tables scales each row by.  For alpha <= 8 the tint reads
   row alpha and each source pixel reads row 9+alpha; for alpha > 8 alpha
   becomes 16-alpha, the tint reads row 9+alpha' and the source pixel reads row
   alpha'.  Either way the tint ends up weighted by the caller's alpha and the
   source pixel by 16-alpha -- writing the obvious shade_ramp[alpha] for the
   tint and shade_ramp[16-alpha] for the source indexes rows that are not
   there.

   THE CUBE INDEX IS NOT THE PACKED COLOUR IN CHANNEL ORDER.  A ramp entry is
   0x000R0G0B, one nibble per channel sitting in the low nibble of its own
   byte, so the two weighted entries can be added with no channel carrying into
   the next.  The sum is shifted right by four -- which drops the remainder of
   each channel's division by 16 into the byte's low nibble -- and masked with
   0x000F0F0F, leaving red in bits 16..19, green in 8..11 and blue in 0..3.
   The fold (v & 0xFFFF) | (v >> 12) then lands GREEN in bits 8..11, RED in
   bits 4..7 and BLUE in bits 0..3.  Green above red is not a slip: the cube is
   filled green-outermost, red, then blue, so its index really is g:r:b and the
   obvious r:g:b packing reads the wrong entry for every colour whose red and
   green differ.

   NOTHING IS CLIPPED, NOTHING IS KEYED OUT AND NOTHING IS SKIPPED.  Unlike
   fdps_blit_transparent_rect above there is no test on the source byte: every
   pixel of the rectangle is written, palette index 0 included.  width and
   height are signed and compared with JL, so either at 0 or below transfers
   nothing.  alpha is signed too, and the fold's own test is signed. */
extern void fdps_blit_tint_rect(unsigned char *src, int src_stride,
                                unsigned char *dst, int dst_stride, int width,
                                int height, unsigned int *shade_ramp,
                                unsigned char *inverse_palette_cube,
                                int tint_color, int alpha);
#pragma aux fdps_blit_tint_rect "*" parm caller [];

/* The colour-keyed counterpart of fdps_blit_tint_rect: the same fold, the same
   two table reads, the same blend and the same cube index, except that a source
   byte of 0 is passed over.  It is what draws a unit's gauge bar tinted --
   fdps_draw_unit_gauge hands it one 0x2b-wide record of the gauge sheet and
   forwards its own blit mode as tint_color -- so the bar's rounded caps let the
   window behind them through while the bar itself is tinted.

   EVERYTHING ABOVE THE LOOPS IS fdps_blit_tint_rect'S, INCLUDING THE TRAPS.
   alpha is folded into 0..8 with the two rows swapping, so the ramp is read at
   rows alpha and 9+alpha and never at 16-alpha; the cube index is g:r:b and not
   r:g:b; the source byte is zero-extended, so pixel 0xff is entry 255 of its
   row; and width, height and alpha are all signed.  The two paragraphs on
   fdps_blit_tint_rect above say what each of those costs to get wrong.

   THE KEY SKIPS THE BLEND, NOT JUST THE STORE.  A source pixel of 0 jumps over
   both the second table read and the arithmetic, so the destination byte
   underneath survives untouched and the tint is never painted on its own.  A
   rewrite that blended every pixel and stored only the non-zero SOURCE ones
   would agree; one that stored every pixel and merely skipped the source term
   would paint the tint colour over the transparent parts of the sprite.

   THE ROW ADVANCE IS OUTSIDE THE KEY.  Both cursors move by their own stride at
   the end of every row whether or not any pixel in it was written, so a fully
   transparent row still steps the destination.

   Nothing is clipped and no extent is checked. */
extern void fdps_blit_tint_transparent_rect(unsigned char *src, int src_stride,
                                            unsigned char *dst, int dst_stride,
                                            int width, int height,
                                            unsigned int *shade_ramp,
                                            unsigned char *inverse_palette_cube,
                                            int tint_color, int alpha);
#pragma aux fdps_blit_tint_transparent_rect "*" parm caller [];

#endif
