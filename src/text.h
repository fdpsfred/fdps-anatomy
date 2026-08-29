/* text.h -- drawing text and numbers, and the 1bpp glyph blit underneath them.
 *
 * A glyph is a 1bpp bitmap, most significant bit first, whose rows are padded
 * to whole bytes: one row consumes ceil(width/8) source bytes and the spare
 * bits of a row's last byte are dropped rather than carried into the next row.
 * The cell the glyph is drawn into is not part of the glyph data -- its width
 * and height are the two globals data_fdps_font_glyph_width and
 * data_fdps_glyph_cell_height, which the font loader fills in, so every glyph
 * on screen shares one cell size.
 *
 * Destinations are 8bpp surfaces addressed by a byte pointer and a pitch in
 * bytes: 0x140 for the visible screen and 0x138 for the 312-wide offscreen
 * pages.
 */
#ifndef TEXT_H
#define TEXT_H

/* Paints one glyph into an 8bpp surface: stores the low byte of color at
   dst[column] for every source bit that is set, and leaves the destination
   pixel exactly as it was for every bit that is clear, so glyphs composite
   onto whatever is already there.  The cell walked is
   data_fdps_font_glyph_width by data_fdps_glyph_cell_height, dst advances by
   pitch once per row, and glyph_bits is consumed a byte at a time from the
   start of each row.

   There is no clipping and no bounds check of any kind: the caller owns
   keeping the whole cell inside its surface.  The caller's glyph_bits is not
   disturbed -- only the callee's own copy advances -- which is what lets
   fdps_draw_glyph blit the same glyph five times for its outline and shadow
   passes. */
extern void fdps_blit_glyph_1bpp(unsigned char *dst, int pitch,
                                 unsigned char *glyph_bits, int color);
#pragma aux fdps_blit_glyph_1bpp "*" parm caller [];

#endif
