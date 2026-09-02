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

/* 00056b25.  Blit mode 2: the sprite's own pixels end up as
   palette_remap[pixel], and the transparent runs are stepped over the way the
   plain blitter steps over them -- the backdrop showing through them is left
   exactly as it was.  That is the only thing separating this kernel from
   fdps_rle_blit_remap_sprite_and_backdrop above.

   The parameters are that kernel's, and mean the same things: `rle_stream` the
   command stream, `dest_pixel` the first pixel of the top row,
   `dest_row_advance` the pitch - width the caller computed, and
   `palette_remap` the 256-byte lookup table, 256 entries because the index is a
   whole pixel byte (MOV AL,byte ptr [EAX + EBP*1] at 00056b4b with EAX holding
   nothing but that byte).

   The row width comes from data_fdps_graphics_rle_blit_src_width, re-read at
   the top of every row, and the row count from
   data_fdps_graphics_rle_blit_remaining_rows, which this routine decrements to
   zero.  Nothing is bounds-checked; see the note in rlecolor.c on why the row
   terminator must stay an exact-zero test.

   Nothing in the shipped executable selects mode 2 either: the census in the
   plate comment at 00056b25 resolves every path to the dispatcher's mode
   argument and finds 0, 3, 4, 8, 9, 0xa and 0xb.  The kernel is compiled in and
   unreachable, and is emitted because the rebuild is of the program, not of the
   reachable part of it. */
extern void fdps_rle_blit_with_palette_remap(unsigned char *rle_stream,
                                             unsigned char *dest_pixel,
                                             int dest_row_advance,
                                             unsigned char *palette_remap);
#pragma aux fdps_rle_blit_with_palette_remap "*" parm caller [];

/* 00056bb7.  Blit mode 3: every pixel the sprite writes comes out as
   ((pixel + tint_offset) & band_mask) + color_base, each step wrapping in eight
   bits, and the transparent runs are stepped over the way the plain blitter
   steps over them.  There is no lookup table in this mode: the transform is
   arithmetic on the palette index.

   `rle_stream`, `dest_pixel` and `dest_row_advance` mean what they mean in the
   two kernels above -- the command stream, the first pixel of the top row, and
   the pitch - width the caller computed.  `recolor_operands` is the dispatcher's
   sixth argument taken as a packed value rather than as a pointer, and this
   kernel unpacks it the way the original's first three instructions do:

     bits 0..7    tint_offset, added to the source pixel before the mask
                  (MOV DH,byte ptr [EBP + 0x1c])
     bits 8..15   color_base, added after it (MOV DL,byte ptr [EBP + 0x1d])
     bits 16..23  band_mask (MOV AH,byte ptr [EBP + 0x1e])

   so mask 7 folds the sprite into the eight-colour palette band anchored at
   color_base and mask 0 collapses it to the single index color_base.  Bits
   24..31 are never read.

   The row width comes from data_fdps_graphics_rle_blit_src_width, re-read at the
   top of every row, and the row count from
   data_fdps_graphics_rle_blit_remaining_rows, which this routine decrements to
   zero.  Nothing is bounds-checked; see the note in rlecolor.c on why the row
   terminator must stay an exact-zero test.

   The shipped program reaches mode 3 with 0x0000ff00 -- tint_offset 0,
   color_base 0xff, band mask 0 -- through fdps_blit_unit_sprite, which paints
   the unit as a flat silhouette in palette index 0xff for the rest flash. */
extern void fdps_rle_blit_recolor(unsigned char *rle_stream,
                                  unsigned char *dest_pixel,
                                  int dest_row_advance,
                                  unsigned int recolor_operands);
#pragma aux fdps_rle_blit_recolor "*" parm caller [];

#endif
