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

#endif
