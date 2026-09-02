/* rlecolor.h -- the RLE blit kernels that put their pixels through a colour
 * transform on the way to the surface: the two palette-remap kernels and the
 * recolour kernel (rebuild_info/code_layout.md).
 *
 * They decode the same four-op stream as the plain blitters in rle.h -- top two
 * bits select the op, low six bits carry len-1 -- and take the row width and the
 * row count from the same two globals in gamedata.h.  What separates them from
 * rle.c is the fourth op:
 *
 *   in rle.c op 11 is a transparent skip, the destination stepped over unwritten
 *   here it is the run the kernel does the most work in, and the family splits
 *   on it: a "sprite and backdrop" kernel transforms the destination pixels
 *   showing through those runs as well, a "sprite only" kernel steps over them
 *
 * fdps_blit_dispatch's sixth argument is what the transform reads, and each mode
 * reads it differently -- a pointer to a 256-byte table here, three packed bytes
 * in the recolour kernel at 00056bb7 (MOV DH,[EBP+0x1c] / DL,[EBP+0x1d] /
 * AH,[EBP+0x1e]) -- so it arrives as one parameter per kernel, typed for what
 * that kernel does with it.
 */
#ifndef RLECOLOR_H
#define RLECOLOR_H

/* 00056a8d.  Blit mode 1: every pixel of the width x height rectangle ends up as
   palette_remap[pixel], the sprite's own pixels and the backdrop showing through
   its transparent runs alike.

   `rle_stream` is the command stream, `dest_pixel` the first pixel of the top
   row, `dest_row_advance` what to add at the end of a row (the caller computes
   it as pitch - width), and `palette_remap` the 256-byte lookup table -- 256
   entries because the index is a whole pixel byte, MOV AL,byte ptr [EAX+EBP*1]
   with EAX holding nothing but that byte.

   The row width comes from data_fdps_graphics_rle_blit_src_width, re-read at the
   top of every row, and the row count from
   data_fdps_graphics_rle_blit_remaining_rows, which this routine decrements to
   zero.  Nothing is bounds-checked; see the note in rlecolor.c on why the row
   terminator must stay an exact-zero test.

   Nothing in the shipped executable selects mode 1: the census in the plate
   comment at 00056a8d resolves every path to the dispatcher's mode argument and
   finds 0, 3, 4, 8, 9, 0xa and 0xb.  The kernel is compiled in and unreachable,
   and is emitted because the rebuild is of the program, not of the reachable
   part of it. */
extern void fdps_rle_blit_remap_sprite_and_backdrop(unsigned char *rle_stream,
                                                    unsigned char *dest_pixel,
                                                    int dest_row_advance,
                                                    unsigned char *palette_remap);
#pragma aux fdps_rle_blit_remap_sprite_and_backdrop "*" parm caller [];

#endif
