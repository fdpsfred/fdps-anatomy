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

/* Draws one glyph of the font sheet complete: the cell's background fill, the
   outline or drop shadow underneath, and the glyph body on top, in that order,
   every pixel of it through fdps_blit_glyph_1bpp above.

   font_base is the base of the sheet and glyph_index selects within it; the
   glyph's bitmap is font_base + glyph_index * data_fdps_font_glyph_stride_bytes
   and the caller never has to do that arithmetic itself.  dst is the top-left
   corner of the cell and pitch is the destination's row stride, which doubles
   as the one-row step for the outline and the shadow.

   The three colours are palette indices and each of them means something
   different by zero.  bg_color of 0 skips the background fill, so the glyph
   composites onto what is already there.  fg_color of 0 skips the glyph body,
   which is how a caller draws a cell's decoration and nothing else.
   outline_color of 0 skips the drop shadow -- but NOT the outline: when
   data_fdps_font_outline_enabled_flag is set, all four outline blits happen
   unconditionally and a zero there paints four palette-index-0 copies of the
   glyph around it.

   There is no clipping anywhere below this, and the outline reaches one pixel
   left, right, above and below the cell: the caller owns keeping that whole
   region inside its surface. */
extern void fdps_draw_glyph(unsigned char *dst, int pitch,
                            unsigned char *font_base, int glyph_index,
                            int fg_color, int bg_color, int outline_color);
#pragma aux fdps_draw_glyph "*" parm caller [];

#endif
