/* rlerot.h -- the two rotating RLE sprite blitters, blit modes 5 and 6, and the
 * step vectors they publish while they run (rebuild_info/code_layout.md).
 *
 * They decode the same four-op stream as the plain blitters in rle.h -- the top
 * two bits of a command byte select the op and the low six bits carry len-1 --
 * and take the source rectangle from the same two globals in gamedata.h.  What
 * separates them from rle.c is how the destination cursor moves: instead of one
 * step right per source pixel and one row advance per source row, the cursor is
 * walked along two perpendicular fixed-point vectors built out of a single
 * (dx, dy) pair.  One source pixel moves it by (dx, -dy) / 0x1000 and one
 * source row by (dy, dx) / 0x1000, so with dx = k*cos and dy = k*sin the sprite
 * lands rotated, at unit scale when k is 0x1000 and shrunk when k is smaller.
 *
 * The pair arrives as the two halves of fdps_blit_dispatch's sixth argument,
 * which the original reads straight out of the dispatcher's live frame; here it
 * is two parameters (see the note in rlerot.c).
 *
 * The step vectors themselves are not local to the routine: the signs and the
 * pitch multiples are folded once on entry and written into the six globals
 * below, which the row and pixel loops then read back.  The two accumulators
 * declared here are the row-to-row halves of that arithmetic and are the only
 * state that deliberately survives from one source row to the next.
 */
#ifndef RLEROT_H
#define RLEROT_H

/* 00070034 and 00070040.  The destination steps taken along the x axis of the
   fixed-point vector pair, one per source pixel and one per source row: +1 or
   -1 for the pixel step, and plus or minus one destination pitch for the row
   step.  Both are the sign of dx folded into a magnitude, so they change only
   with the quadrant the rotation vector points into.  Written by the two
   kernels on entry and read back inside their loops; the pitch multiple is a
   whole dword because the destination cursor is stepped by it. */
extern int data_fdps_graphics_rle_rotate_dst_x_step_per_src_x;
extern int data_fdps_graphics_rle_rotate_dst_y_step_per_src_y;

/* 00070038 and 0007003c.  The other two steps of the same pair, carrying the
   sign of dy: minus sign(dy) pitches when a source pixel crosses a destination
   row, and plus or minus one when a source row does.  The negation on the
   pixel step is what makes the two vectors perpendicular rather than parallel,
   and a positive dy therefore walks the sprite up the surface. */
extern int data_fdps_graphics_rle_blit_rotated_src_pixel_step_y;
extern int data_fdps_graphics_rle_blit_rot_row_dest_step_x;

/* 00070044 and 00070046.  The within-row halves of the same fractional
   arithmetic, and the counterpart of the two registers mode 5 keeps them in:
   mode 6 needs DX and BP for its resampling counters instead, so it spills
   these two to memory.  Both are zeroed at the top of every destination row,
   which is what separates them from the row-to-row pair below, and they are
   read and written by fdps_rle_blit_rotated_scaled alone -- a sweep of the
   whole image for either address finds only that routine's nine accesses to
   each. */
extern unsigned short data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator;
extern unsigned short data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator;

/* 00070048 and 0007004a.  The two row-to-row fractional accumulators, cleared
   once when a blit starts and then carried across every source row of it: each
   gathers one magnitude per row and steps the row origin whenever it reaches
   0x1000.  Clearing them per row instead would make the rows step by whole
   pixels only, which is a shear and not a rotation. */
extern unsigned short data_fdps_graphics_rle_blit_rot_row_step_x_accumulator;
extern unsigned short data_fdps_graphics_rle_blit_rot_row_step_y_accumulator;

/* 0007004c and 0007004e.  The magnitudes |dx| and |dy| of the rotation vector,
   with the signs already stripped out into the four step globals above.  They
   are the amounts the fractional accumulators gather, and 0x1000 is one whole
   destination pixel, so a pair whose length is 0x1000 draws at unit scale and a
   shorter one shrinks. */
extern unsigned short data_fdps_graphics_rle_rotate_cos_magnitude;
extern unsigned short data_fdps_graphics_rle_rotate_sin_magnitude;

/* ------------------------------------------------------------------------
   The C prototypes of the kernels, REFERENCE ONLY.  In the linked build the
   kernels are the assembly in src/rleturn.asm and have no C interface:
   they are entered only from fdps_blit_dispatch, with their inputs in
   registers and in the dispatcher's own stack frame, so nothing in C may
   call them.  These declarations belong to the C translation kept under
   the matching #if 0 in the .c file (rebuild_info/code_layout.md).
   ------------------------------------------------------------------------ */
#if 0 /* RLE_C_REFERENCE -- rebuild_info/code_layout.md */

/* 00056e2a.  Blit mode 5, the rotating sprite blit: decodes the four ops into
   an 8bpp destination surface while walking the destination along the rotation
   vector `rotate_dx`, `rotate_dy` describes.

   `rle_stream` is the command stream and `dest_pixel` the destination pixel the
   source's top-left corner maps to.  `rotate_dx` and `rotate_dy` are the
   rotation vector in 1/0x1000ths of a destination pixel, signed, and their
   signs alone choose which of four quadrant arms folds the step globals above;
   they arrive out of fdps_blit_dispatch's own frame in the original (see the
   note in rlerot.c).

   The source rectangle comes from the two globals the whole family shares --
   data_fdps_graphics_rle_blit_src_width, re-read at the top of every row, and
   data_fdps_graphics_rle_blit_remaining_rows, which this routine decrements to
   zero -- and the destination pitch from
   data_fdps_graphics_rle_blit_dst_pitch.  Unlike the plain blitters it needs no
   end-of-row advance: it keeps the row's origin itself and steps that origin by
   the perpendicular vector.

   Nothing in the shipped executable selects mode 5.  The census in the plate
   comment at 00056e2a resolves every path to the dispatcher's mode argument and
   finds 0, 3, 4, 8, 9, 0xa and 0xb, so this kernel is compiled in and
   unreachable, and cannot be checked by playing the game. */
extern void fdps_rle_blit_rotated(unsigned char *rle_stream,
                                  unsigned char *dest_pixel,
                                  short rotate_dx,
                                  short rotate_dy);
#pragma aux fdps_rle_blit_rotated "*" parm caller [];

/* 00057114.  Blit mode 6, the rotating sprite blit with resampling on top:
   decodes the same four ops into an 8bpp destination surface, but the
   rectangle it walks is destination-sized rather than source-sized, so the
   sprite is resized to `blit_geometry`'s width and height as well as rotated
   by the vector the same record carries.

   `rle_stream` is the command stream and `dest_pixel` the destination pixel
   the source's top-left corner maps to, the same two as mode 5.
   `blit_geometry` is the record fdps_blit_dispatch's sixth argument points at
   -- four 32-bit slots of which only the low sixteen bits are ever read: [0]
   the destination width in pixels, [1] the destination height in rows, [2] and
   [3] the rotation vector dx and dy in 1/0x1000ths of a destination pixel,
   signed.  Mode 5 and mode 4 read that same argument slot as two packed words;
   mode 6 is the one that reads it as a pointer and dereferences it.

   Where mode 5 walks one source pixel per destination pixel, this one runs a
   Bresenham counter in each axis: the horizontal one turns the source row's
   pixels into `blit_geometry[0]` destination pixels, and the vertical one
   decides how many source rows to walk past with fdps_rle_skip_row between one
   destination row and the next.  The rotation is then applied on top, through
   the same six step globals above.

   The source rectangle comes from data_fdps_graphics_rle_blit_src_width and
   data_fdps_graphics_rle_blit_remaining_rows, both read-only here -- this
   kernel counts destination rows down in
   data_fdps_graphics_rle_blit_dest_rows_remaining instead and leaves the
   source row count alone -- and the destination pitch from
   data_fdps_graphics_rle_blit_dst_pitch.

   Nothing in the shipped executable selects mode 6 either: the census in the
   plate comment at 00056e2a resolves every path to the dispatcher's mode
   argument and finds 0, 3, 4, 8, 9, 0xa and 0xb, so this kernel is compiled in
   and unreachable and cannot be checked by playing the game. */
extern void fdps_rle_blit_rotated_scaled(unsigned char *rle_stream,
                                         unsigned char *dest_pixel,
                                         int *blit_geometry);
#pragma aux fdps_rle_blit_rotated_scaled "*" parm caller [];

#endif /* RLE_C_REFERENCE */

#endif
