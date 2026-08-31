/* blit.h -- rectangle blit primitives: solid fills, transparent and inlaid
 * copies, tinted and blended copies, rotate-and-scale.
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

#endif
