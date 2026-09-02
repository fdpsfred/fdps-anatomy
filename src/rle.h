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

#endif
