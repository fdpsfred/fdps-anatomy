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

/* Alpha-blends TWO 8bpp rectangles into a third, resolving every blended
   colour back to a palette index through the same inverse-palette cube
   fdps_blit_tint_rect uses.  Where that routine weighs one constant colour
   against the source pixel, this one weighs two source pixels against each
   other: fg carries weight alpha out of 16 and bg carries 16 - alpha.  It is
   what slides the message window and the unit status panel in and out --
   fdps_message_window_open, fdps_message_window_close and
   fdps_battle_show_unit_status_window each ramp alpha a step per retrace and
   call this once per step.

   All three surfaces are a base pointer plus a byte stride, and ALL THREE
   POINTERS ARE THE RECTANGLE'S TOP-LEFT PIXEL.  The three strides are
   genuinely independent: the two window callers hand over a packed panel of
   pitch 0x97 or 0x12e against a 0x140 screen.

   THE FOLD SWAPS THE TWO SOURCES, NOT THE TWO ROW OFFSETS.  This is where it
   parts company with fdps_blit_tint_rect.  For alpha > 8 both source pointers
   AND both source strides exchange places and alpha becomes 16 - alpha, after
   which fg is always read from ramp row alpha and bg always from row 9 + alpha.
   Swapping only the pointers leaves each cursor advancing by the other
   rectangle's pitch, which is invisible at height 1 and wrong from the second
   row on -- and the callers' pitches differ, so it is wrong in the game.  The
   ramp rows carry weights 0..8 and 16..8, so folding is what keeps both
   lookups inside the 18 rows fdps_build_palette_tables fills.

   THE CUBE INDEX IS g:r:b, NOT r:g:b, exactly as in fdps_blit_tint_rect above:
   the sum of the two weighted entries is shifted right by four, masked with
   0x000F0F0F and folded as (v & 0xFFFF) | (v >> 12).  The paragraph on
   fdps_blit_tint_rect says what the obvious r:g:b packing costs.

   NOTHING IS KEYED OUT AND NOTHING IS CLIPPED.  Unlike
   fdps_blit_blend_transparent_rect, which is this routine plus one index-0
   test, every pixel of the rectangle is read from both sources and written,
   palette index 0 included.  width, height and alpha are all signed, so either
   extent at 0 or below blends nothing.

   BG AND DST MAY BE THE SAME ADDRESS AND THE TWO WINDOW CALLERS MAKE THEM SO.
   Each pixel is read from bg and stored to dst before the next column is
   touched, so an in-place blend consumes the pre-blend byte at every pixel;
   any rewrite that stages a row must not read bg back out of dst. */
extern void fdps_blit_blend_rect(unsigned char *fg, int fg_stride,
                                 unsigned char *bg, int bg_stride,
                                 unsigned char *dst, int dst_stride, int width,
                                 int height, unsigned int *shade_ramp,
                                 unsigned char *inverse_palette_cube,
                                 int alpha);
#pragma aux fdps_blit_blend_rect "*" parm caller [];

/* The colour-keyed counterpart of fdps_blit_blend_rect: the same fold, the same
   two ramp reads, the same blend and the same g:r:b cube index, with palette
   index 0 keying the pixel out.  It is what draws a unit's gauge bar and the
   floating experience number translucently over the surface underneath them --
   fdps_draw_unit_gauge and fdps_unit_award_exp_and_level_up both pass one
   address as bg and as dst, so the sprite is blended onto what it reads.

   THE KEY IS READ FROM THE ORIGINAL fg, WHICH ABOVE ALPHA 8 IS NO LONGER THE
   FOREGROUND.  This is the one thing this routine does that none of the other
   five blend blits does, and it is invisible in the obvious rewrite.  fg and
   fg_stride are copied aside before the fold; the fold then exchanges the two
   rectangles and the two strides exactly as fdps_blit_blend_rect does, and the
   inner loop keys on the SAVED pointer -- so for alpha > 8 the mask is the
   rectangle the blend is reading as its background.  Testing the post-swap fg
   instead keys on the wrong rectangle for the whole upper half of the alpha
   range, which paints the sprite's transparent pixels; the callers ramp alpha
   across 8 while the same sprite is on screen, so it shows.

   THE MASK HAS ITS OWN STRIDE AS WELL AS ITS OWN POINTER.  It is advanced by
   the ORIGINAL fg_stride at the end of every row, which above the fold differs
   from the stride fg is being walked by.  Keeping the saved pointer but
   advancing it with fg agrees for a single row and diverges from the second on,
   and the two callers' pitches genuinely differ -- 0x2b and 0x28 for the source
   graphics against 0x140 or 0x168 for the pages.

   THE KEY SKIPS THE BLEND, NOT JUST THE STORE, and it is the only test in the
   loop: a masked-out column costs neither ramp read, and the pixel underneath
   survives.  bg is not keyed -- a background pixel of 0 is blended and stored
   like any other.

   THE ROW ADVANCE IS OUTSIDE THE KEY.  All four cursors step at the end of
   every row whether or not any pixel in it survived.

   Everything the paragraphs on fdps_blit_tint_rect and fdps_blit_blend_rect say
   about the shared machinery holds here unchanged: the ramp is read at rows
   alpha and 9 + alpha and never at 16 - alpha, the cube index is g:r:b and not
   r:g:b, both source bytes are zero-extended so 0xff is entry 255 of its row,
   and width, height and alpha are all signed.  Nothing is clipped and no extent
   is checked against any of the four surfaces. */
extern void fdps_blit_blend_transparent_rect(unsigned char *fg, int fg_stride,
                                             unsigned char *bg, int bg_stride,
                                             unsigned char *dst, int dst_stride,
                                             int width, int height,
                                             unsigned int *shade_ramp,
                                             unsigned char *inverse_palette_cube,
                                             int alpha);
#pragma aux fdps_blit_blend_transparent_rect "*" parm caller [];

/* Redraws one 320x200 8bpp image into another, scaled about a chosen point of
   the source and, when either angle is non-zero, rotated and tilted into
   perspective.  It is the zoom the screen transition runs through:
   fdps_transition_zoom walks a nine-entry height ramp and calls this once per
   step with the mode 13h screen as the destination and its own saved copy of
   the frame as the source, so every step redraws the whole picture from the
   untouched original rather than from the previous step's output.

   THE TWO SURFACES ARE 320-BYTE-PITCH PAGES AND NEITHER IS AN ARGUMENT'S
   WORTH OF CHOICE.  There is no stride argument: the source is addressed at a
   fixed 0x140 pitch and the destination is walked byte by byte, 318 bytes per
   row and then two more, which comes to the same 0x140.  Handing it a page of
   any other pitch tears the picture.

   ONLY 318 x 198 OF THE DESTINATION IS WRITTEN.  The loops cover rows -99..98
   and columns -159..158, so the last two columns of every row and the bottom
   two rows of the page keep whatever was there before.  The caller relies on
   it in the other direction as well: at the ramp's 1:1 step it does not call
   this routine at all but memmoves the whole 0xfa00 bytes instead, which is
   the only step that fills the screen edge to edge.

   camera_height IS AN INVERSE SCALE, 0..1999.  The view covers
   500 / (2000 - camera_height) source pixels per destination pixel: 0
   magnifies four times, 1500 is 1:1 and anything larger shrinks the picture
   and leaves palette index 0 around it.  2000 or above divides by zero or
   turns the projection inside out; the caller's table stays inside the range.

   center_x AND center_y ARE QUARTER PIXELS, NOT PIXELS.  They name the source
   point that lands on the middle of the destination, and their usable ranges
   are 0..0x4f8 and 0..0x318 -- 318 * 4 and 198 * 4.  Passing plain pixel
   coordinates zooms about a point a quarter of the way in.

   THE PICTURE IS DRAWN FOUR BYTES OFF ITS NOMINAL CENTRE.  The sample base
   carries an unconditional + 4, so at 1:1 the destination is the source
   shifted four bytes: the top-left destination pixel is source byte 4, and the
   rightmost column of each row reads the start of the next source row.  It is
   not a rounding term and it is not compensated for anywhere -- a rewrite that
   drops it moves the whole picture four pixels against the memmove the caller
   uses for the 1:1 step, and the seam shows as the ramp crosses it.

   EVERY DIVISION IN THE SAMPLING PATH TRUNCATES TOWARD ZERO.  The source
   offsets are 1/128-pixel fixed point and are negative for everything left of
   or above the centre, so writing the obvious offset >> 7 floors instead and
   shifts the whole left and top of the picture by a source pixel.  The
   difference is plainest at the middle: truncation makes the seven destination
   rows nearest the centre read one source row, a shift makes it four.

   A SAMPLE OUTSIDE THE SOURCE IS PALETTE INDEX 0, AND BOTH BOUNDS ARE
   INCLUSIVE.  There is no clamp and no wrap: a shrunk picture is surrounded by
   index 0 and a centre near an edge simply loses that side.  The destination
   is written unconditionally, so nothing underneath survives -- this is not a
   keyed blit.

   tilt AND rotation ARE RADIANS AND GO STRAIGHT TO THE LIBRARY sin AND cos,
   whose results are kept as floats.  tilt makes the scale and the row origin
   vary from row to row; rotation turns the sampling grid about the centre.
   The only caller passes 0.0 for both, so neither path runs in the shipped
   game, but both are reachable and neither is a no-op for a non-zero angle. */
extern void fdps_blit_rotated_scaled(unsigned char *dst, unsigned char *src,
                                     int camera_height, float tilt,
                                     int center_x, int center_y,
                                     float rotation);
#pragma aux fdps_blit_rotated_scaled "*" parm caller [];

/* 000568db.  The one entry point into the RLE sprite blitters: every .CEL
   sheet, map tile, font glyph and window frame the game draws reaches the
   pixels through here.  It publishes the blit rectangle into the three globals
   the whole kernel family reads, works out the end-of-row advance, and hands
   the stream to one of the thirteen kernels in rle.c, rlecolor.c, rlerot.c and
   rleblend.c.

   `rle_stream` is the command stream and `dest_pixel` the first pixel of the
   top destination row.  Both reach the chosen kernel unchanged.

   `src_width` and `src_rows` are the source rectangle and `dest_pitch` the
   destination surface's pitch in bytes.  All three are published, truncated to
   sixteen bits, into data_fdps_graphics_rle_blit_src_width,
   data_fdps_graphics_rle_blit_remaining_rows and
   data_fdps_graphics_rle_blit_dst_pitch (gamedata.h), and the kernels read the
   rectangle from there rather than from any parameter of their own.  A
   src_rows of 0 therefore does not draw nothing: the kernels count the rows
   down with a do-while, so it asks for 0x10000 of them.

   THE ROW ADVANCE IS NOT ONE OF THE PUBLISHED VALUES.  It is dest_pitch -
   src_width computed in full 32 bits from the arguments, before either is
   truncated, and passed to the kernels that take one.  Recomputing it from the
   two globals instead loses the difference whenever either argument does not
   fit in sixteen bits.

   `mode_operand` is a single dword that each mode reads its own way, which is
   why the kernels cannot share one parameter list:

     0   passthrough                not read
     1   remap sprite and backdrop  a 256-byte palette remap table
     2   palette remap              a 256-byte palette remap table
     3   recolor                    three packed bytes (rlecolor.h)
     4   scaled                     low word destination width, high word
                                    destination height
     5   rotated                    low word dx, high word dy, both signed
     6   rotated and scaled         a four-slot geometry record (rlerot.h)
     7   mirrored horizontal        not read
     8   mirrored vertical          not read
     9   translucent                a three-dword blend descriptor (rleblend.h)
     10  tint sprite and backdrop   a four-dword blend descriptor
     11  tint                       a four-dword blend descriptor
     12  translucent colour range   a five-dword blend descriptor

   Modes 4 to 8 are also the ones that are handed no row advance: the scaling,
   rotating and mirroring kernels each derive their own destination stepping
   from data_fdps_graphics_rle_blit_dst_pitch, and handing them pitch - width
   would double-count the width.

   `blit_mode` is that table's index, and only its low eight bits are read.  A
   mode above 12 runs off the end of the compare chain and draws nothing at
   all -- there is no default kernel and no clamp -- while still leaving the
   three globals published, which is the one thing an out-of-range mode does
   change.  Only 0, 3, 4, 8, 9, 10 and 11 are reachable in the shipped game;
   the other six kernels are compiled in and unreached. */
extern void fdps_blit_dispatch(unsigned char *rle_stream,
                               unsigned char *dest_pixel,
                               int src_width, int src_rows, int dest_pitch,
                               unsigned int mode_operand,
                               unsigned char blit_mode);
#pragma aux fdps_blit_dispatch "*" parm caller [];

#endif
