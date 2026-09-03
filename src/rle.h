/* rle.h -- the RLE sprite blitters: the plain pass-through kernel, the scaled
 * and row-skipping variants, and the horizontal and vertical mirrors
 * (rebuild_info/code_layout.md).
 *
 * The whole family decodes one stream format, a run of command bytes whose top
 * two bits select the op and whose low six bits carry len-1, so a run is 1..64
 * pixels long:
 *
 *   00  fill        one pixel byte follows; it is written len times
 *   01  stretched   one pixel byte follows; it is written into the SECOND byte
 *                   of each of len destination pairs, leaving the first byte of
 *                   every pair holding whatever was already there
 *   10  literal     len pixel bytes follow and are copied straight through
 *   11  skip        no bytes follow; len destination bytes are stepped over
 *
 * A row ends when the pixels it has accounted for reach the source row width
 * exactly, and the kernels then step the destination on by the row advance
 * their caller computed.  Which rows and how wide they are is not passed in:
 * fdps_blit_dispatch writes the width and the row count into the two globals
 * declared in gamedata.h before it calls, and the kernel consumes the row
 * count as it runs.
 */
#ifndef RLE_H
#define RLE_H

/* 00056a0d.  Blit mode 0, the plain sprite and tile blit: decodes the four ops
   above into an 8bpp destination, writing every pixel it draws unchanged.

   `rle_stream` is the command stream, `dest_pixel` the first pixel of the top
   row, and `dest_row_advance` what to add to the destination cursor at the end
   of a row -- the caller computes it as pitch - width, and it is signed
   because fdps_rle_blit_mirrored_vertical (blit mode 8) reaches this same
   decoder with a negated advance to draw bottom-up (NEG EDX at 00057614).

   The row width comes from data_fdps_graphics_rle_blit_src_width and is
   re-read at the top of every row; the row count is
   data_fdps_graphics_rle_blit_remaining_rows, which this routine decrements to
   zero.  Neither the destination nor the stream is bounds-checked, and a
   stream whose runs overshoot the row width is not caught: see the note in
   rle.c on why the row terminator must stay an exact-zero test. */
extern void fdps_rle_blit_passthrough(unsigned char *rle_stream,
                                      unsigned char *dest_pixel,
                                      int dest_row_advance);
#pragma aux fdps_rle_blit_passthrough "*" parm caller [];

/* 00056c5e.  Blit mode 4, the scaled sprite blit: decodes the same four ops
   into an 8bpp destination while rescaling the sprite to `dest_width` by
   `dest_height`, by a Bresenham step in each axis.  Scaling up repeats a
   source pixel or a source row, scaling down drops them, and a run whose
   pixels are still owing when the destination row fills is abandoned there.

   `rle_stream` is the command stream and `dest_pixel` the first pixel of the
   top destination row, the same two the pass-through kernel takes.  The scale
   is not a global on the way in: the original reads it out of
   fdps_blit_dispatch's own frame as the two halves of the dispatcher's sixth
   argument, so it becomes two parameters here (see the note in rle.c).

   The source rectangle comes from the same two globals the pass-through kernel
   reads -- data_fdps_graphics_rle_blit_src_width and
   data_fdps_graphics_rle_blit_remaining_rows, the latter being the source
   height here and never decremented -- and the destination pitch from
   data_fdps_graphics_rle_blit_dst_pitch.  The routine publishes its own scale
   and its working counters into the rest of that block (gamedata.h) and
   consumes data_fdps_graphics_rle_blit_dest_rows_remaining down to zero.

   Vertical stepping is done by calling fdps_rle_skip_row once per source row
   the accumulator drops, which is why a shrunk sprite still reads every row of
   its stream. */
extern void fdps_rle_blit_scaled(unsigned char *rle_stream,
                                 unsigned char *dest_pixel,
                                 unsigned short dest_width,
                                 unsigned short dest_height);
#pragma aux fdps_rle_blit_scaled "*" parm caller [];

/* 00056dc9.  Advances a stream cursor past one encoded row without drawing
   anything, and returns where the next row's first command byte starts.  The
   scaled blitters call it once per source row the vertical Bresenham step
   decides to drop, so a shrunk sprite reads every row of its stream even
   though it paints only some of them.

   The row width comes from data_fdps_graphics_rle_blit_src_width, read once
   on entry: this walks exactly one row, unlike the drawing kernels, which
   re-read the width per row and run until the row count is exhausted.  Only
   the four ops' byte costs matter here -- 2 for fill and for stretched, 1 +
   len for a literal, 1 for a skip -- so no pixel byte is ever looked at. */
extern unsigned char *fdps_rle_skip_row(unsigned char *rle_stream);
#pragma aux fdps_rle_skip_row "*" parm caller [];

/* 00057551.  Blit mode 7, the left-right mirror: decodes the same four ops as
   the pass-through kernel into an 8bpp destination, but fills each row from its
   right-hand column leftwards, so what lands is that kernel's output reflected
   inside the same destination rectangle.

   `rle_stream` is the command stream and `dest_pixel` the first pixel of the top
   destination row -- the rectangle's top-left corner, the same pointer mode 0
   takes.  The routine reaches the row's right-hand column itself by adding the
   width, so a caller must NOT hand it the right-hand end.

   There is no row-advance parameter, which is what separates this from
   fdps_rle_blit_passthrough: it restores each row's origin itself and steps that
   by the full destination pitch, read from
   data_fdps_graphics_rle_blit_dst_pitch, so the pitch - width advance the
   dispatcher computes for the other kernels is not used here and would
   double-count the width.  The row width is
   data_fdps_graphics_rle_blit_src_width, re-read at the top of every row, and
   the row count is data_fdps_graphics_rle_blit_remaining_rows, which this
   routine decrements to zero. */
extern void fdps_rle_blit_mirrored_horizontal(unsigned char *rle_stream,
                                              unsigned char *dest_pixel);
#pragma aux fdps_rle_blit_mirrored_horizontal "*" parm caller [];

/* 000575ed.  Blit mode 8, the top-bottom mirror: it decodes nothing itself.  It
   aims the destination at the bottom row of the rectangle and hands the stream
   to fdps_rle_blit_passthrough with an upward row advance, so that kernel
   climbs the rectangle while consuming the stream forwards and its output comes
   out reflected top to bottom inside the same box.

   `rle_stream` is the command stream and `dest_pixel` the first pixel of the
   TOP destination row -- the rectangle's top-left corner, the same pointer
   modes 0 and 7 take.  The routine finds the bottom row itself by adding
   data_fdps_graphics_rle_blit_dst_pitch times one less than
   data_fdps_graphics_rle_blit_remaining_rows, so a caller must NOT hand it the
   bottom row.

   The advance it passes on is -(pitch + width) and not -pitch, because the
   pass-through kernel adds it only after the cursor has already walked one full
   width across the row.  The row width is
   data_fdps_graphics_rle_blit_src_width; the row count is
   data_fdps_graphics_rle_blit_remaining_rows, which this routine only reads --
   the pass-through kernel is what decrements it to zero. */
extern void fdps_rle_blit_mirrored_vertical(unsigned char *rle_stream,
                                            unsigned char *dest_pixel);
#pragma aux fdps_rle_blit_mirrored_vertical "*" parm caller [];

#endif
