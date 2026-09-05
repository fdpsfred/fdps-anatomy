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

/* Draws a signed integer into an 8bpp surface as a run of 6x8 digit sprites out
   of Number.cel, one sprite per character of the formatted figure, stepping six
   pixels to the right for each.  This is the game's only number renderer: the
   font above draws text, this draws figures, and the two share nothing.

   dest points at the first digit cell -- callers form it themselves as
   surface + row * pitch + column -- and pitch is the destination's row stride,
   handed straight through to fdps_blit_dispatch.  The 6 by 8 cell is hardcoded
   here and the sheet's own width and height fields are never read.  There is no
   clipping: the caller owns keeping every cell inside its surface.

   digit_count is a fixed field width in digits and its zero is a mode, not a
   count: zero formats the figure at its natural width with no padding and with
   no overflow guard at all, while one to nine zero-pads to that many digits and
   replaces a figure too large for the field with that many '?' glyphs rather
   than truncating it.  The two are separate format strings and folding them
   into one "%.*d" is wrong -- a precision of zero prints value 0 as the empty
   string, and fdps_draw_cursor_info_panel does call with digit_count 0.

   show_plus draws a leading '+' in front of a value that is not negative; a
   negative value already carries its '-' out of the format and show_plus is
   ignored for it.

   Which of Number.cel's five colour rows the sprites come from is not an
   argument: it is data_fdps_number_glyph_color_row, which the caller sets
   before the call and puts back to 0 afterwards.  See its declaration in
   gamedata.h for why it must stay a global. */
extern void fdps_draw_number(unsigned char *dest, int pitch, int value,
                             int digit_count, char show_plus);
#pragma aux fdps_draw_number "*" parm caller [];

/* Draws one entry of a text block into an 8bpp surface, interpreting the
   control codes embedded in the entry, and hands back the cursor it stopped at.
   Every piece of text the game shows -- menu labels, item names, save-slot
   dates, the chapters' spoken lines -- comes out of this one routine.

   text_base is the start of a text block: a table of signed 16-bit BYTE offsets
   followed by the token streams those offsets point at, and text_id is a
   0-based index into that table.  The offset is added to text_base itself, not
   to the address the offset was read from, and it is signed.  Callers pass
   data_fdps_all_game_text_ptr, data_fdps_current_chapter_text_ptr or a block
   they have loaded themselves (gamedata.h).

   dest is the byte address the first glyph goes at AND the origin the line
   break measures from, and pitch is the destination's row stride -- 0x140 for
   the visible screen, 0x138 for the game's 312-wide offscreen pages.  The three
   colours go straight to fdps_draw_glyph above and mean what they mean there.

   The stream is walked a signed 16-bit token at a time until -1.  A token that
   is not one of the control codes is a glyph index, drawn through
   fdps_draw_glyph and followed by a step of data_fdps_glyph_advance_x.  The
   codes are -2 line break, -3 page break, -4 and -5 substitution, -6 number,
   -0x11 speaker by character id and -0x12 speaker by unit index.  Zero is NOT a
   terminator: it is glyph 0.

   THREE OF THE CODES TAKE THE PEN AWAY FROM THE CALLER'S SURFACE.  The page
   break and both speaker codes overwrite dest with a fixed VGA address and the
   line break then measures from that new value, so an entry carrying any of
   them only comes out right when the caller is drawing straight to the visible
   screen at pitch 0x140.  Those three also stand a modal wait on the keyboard
   and repaint the whole message panel, which is why a caller drawing into an
   offscreen page has to know the entry it asked for holds none of them.

   A SUBSTITUTION IGNORES THE CALLER'S COLOURS.  The -4 and -5 codes always draw
   in 0xd0 / 0 / 0x6d, so a substituted name stays in the standard message
   colours even inside text the caller asked for in another colour -- a greyed
   menu entry at 0xc8 gets a full-brightness name in the middle of it.

   The return is the cursor one glyph past the last one drawn, so a caller can
   chain a second entry onto the end of the first. */
extern unsigned char *fdps_draw_text(unsigned char *text_base, int text_id,
                                     unsigned char *dest, int pitch,
                                     int fg_color, int bg_color,
                                     int outline_color);
#pragma aux fdps_draw_text "*" parm caller [];

#endif
