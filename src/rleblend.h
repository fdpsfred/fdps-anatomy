/* rleblend.h -- the RLE blit kernels that resolve every pixel they draw
 * through the palette blend tables: the translucent kernel, the two tinting
 * kernels and the colour-range blend (rebuild_info/code_layout.md).
 *
 * They decode the same four-op stream as the plain blitters in rle.h -- the top
 * two bits of a command byte select the op and the low six bits carry len-1, so
 * a run is 1..64 pixels -- and take the row width and the row count from the
 * same two globals in gamedata.h.  What separates them from rle.c is that no
 * pixel reaches the surface as itself: a pixel is weighted through a row of the
 * shade ramp, added to a second weighted colour, and the sum is resolved back to
 * a palette index through the inverse colour cube.
 *
 * Both tables and the blend level arrive in a record fdps_blit_dispatch's sixth
 * argument points at, which each kernel dereferences itself; the slots common to
 * the whole family are
 *
 *   +0x00  the shade ramp base, 18 rows of 256 dwords, each entry a palette
 *          colour packed one nibble per byte as 0x000R0G0B and multiplied by
 *          that row's coefficient -- the coefficient is the row index for rows
 *          0..8 and 16 - (row - 9) for rows 9..17
 *   +0x04  the blend level, 0..16
 *   +0x08  the inverse colour cube base, 4096 bytes, one palette index per
 *          quantised 4-bit rgb triple
 *
 * and the kernels that need more read it at +0x0c and beyond.
 *
 * The nibble layout is what lets one 32-bit add weight all three channels at
 * once: the two rows a kernel picks always carry coefficients summing to 16, so
 * no channel of the sum exceeds 15 * 16 and none of them carries into the next.
 */
#ifndef RLEBLEND_H
#define RLEBLEND_H

/* 0005761b.  Blit mode 9, the translucent sprite blit: draws one sprite stream
   with every pixel it paints blended into the destination pixel underneath it,
   so the whole sprite comes out see-through at the level the descriptor names.

   `rle_stream` is the command stream, `dest_pixel` the first pixel of the top
   row, and `dest_row_advance` what to add to the destination cursor at the end
   of a row -- the caller computes it as pitch - width, and the kernel publishes
   it in data_fdps_graphics_rle_blit_dst_row_advance before it starts, which is
   where it reads it back from at every row end.

   `blend_descriptor` is the three-dword record described above: [0] the shade
   ramp base, [1] the blend level and [2] the inverse colour cube base.  The
   level runs 0 = the source drawn opaque through 16 = the source invisible,
   which is the complement of the opacity its callers hold --
   fdps_draw_composite_sprite computes 16 - opacity before it fills the record
   (0001424e), so an opacity passed straight through inverts the fade.  The
   source always carries weight (16 - level)/16 and the destination level/16:
   at level 8 or below the source pixels are looked up in ramp row level + 9 and
   the destination pixels in row level, and above 8 the same two weights are only
   stored the other way round, so the source reads row 16 - level and the
   destination row 25 - level.

   The row width comes from data_fdps_graphics_rle_blit_src_width, re-read at the
   top of every row, and the row count from
   data_fdps_graphics_rle_blit_remaining_rows, which this routine decrements to
   zero.  Nothing is bounds-checked; see the note in rleblend.c on why the row
   terminator must stay an exact-zero test. */
extern void fdps_rle_blit_translucent(unsigned char *rle_stream,
                                      unsigned char *dest_pixel,
                                      int dest_row_advance,
                                      int *blend_descriptor);
#pragma aux fdps_rle_blit_translucent "*" parm caller [];

/* 00057793.  Blit mode 0x0a, the tinting sprite blit: draws one sprite stream
   with every pixel it paints blended toward a single constant palette colour,
   AND blends that same colour into the destination pixels showing through the
   sprite's transparent runs, so the whole width x height rectangle comes out
   tinted rather than only the sprite's opaque pixels.

   The first three parameters are what they are for fdps_rle_blit_translucent:
   the command stream, the first pixel of the top row, and the row advance the
   caller computes as pitch - width, which this kernel likewise publishes in
   data_fdps_graphics_rle_blit_dst_row_advance on entry and reads back from
   there at every row end.

   `blend_descriptor` here is FOUR dwords, not three: [0] the shade ramp base,
   [1] the blend level, [2] the inverse colour cube base and [3] the tint
   colour, a palette index.  Only one of the two blended terms varies per pixel,
   so ramp[tint row][tint colour] is fetched once before any drawing and every
   pixel is that constant added to its own weighted colour.  The level runs 0 =
   no tint through 16 = every pixel replaced by the tint: the tint always
   carries weight level/16 and the pixel 16 - level, with the level read in ramp
   row level and the pixel in row level + 9 for a level of 8 or less, and the
   two rows folded to 16 - level and swapped above that.

   Its caller fdps_draw_scene_layer picks this mode for a map tile whose
   movement-grid cell is marked, which makes it the battle map's movement-range
   highlight; the row width and row count come from the same two globals in
   gamedata.h.  See the note in rleblend.c on why the transparent op must tint
   rather than skip and why the stretched op starts one pixel late. */
extern void fdps_rle_blit_tint_sprite_and_backdrop(unsigned char *rle_stream,
                                                   unsigned char *dest_pixel,
                                                   int dest_row_advance,
                                                   int *blend_descriptor);
#pragma aux fdps_rle_blit_tint_sprite_and_backdrop "*" parm caller [];

#endif
