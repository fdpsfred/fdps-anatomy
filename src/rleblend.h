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

/* 00070050 and 00070052.  The inclusive palette-index range that decides which
   pixels fdps_rle_blit_translucent_color_range blends and which it draws
   opaque.  The kernel copies sixteen bits of each of its descriptor's last two
   fields here on entry (MOV [0x00070050],BX at 00057a81 and MOV
   [0x00070052],BX at 00057a8c) and re-reads them from here at every one of its
   six compares; nothing else in the image touches either address, so they are
   that kernel's own state and they keep the last call's range after it
   returns.

   They are signed because the compares are: CMP AX,[0x00070050] / JL over a
   pixel byte that was zero-extended into AX, so the value under test is always
   0..255 and only a bound can be negative -- a bound with bit 15 set passes
   every pixel, which reading them as unsigned would turn into passing none. */
extern short data_fdps_graphics_rle_blit_translucent_color_min;
extern short data_fdps_graphics_rle_blit_translucent_color_max;

/* ------------------------------------------------------------------------
   The C prototypes of the kernels, REFERENCE ONLY.  In the linked build the
   kernels are the assembly in src/rlemix.asm and have no C interface:
   they are entered only from fdps_blit_dispatch, with their inputs in
   registers and in the dispatcher's own stack frame, so nothing in C may
   call them.  These declarations belong to the C translation kept under
   the matching #if 0 in the .c file (rebuild_info/code_layout.md).
   ------------------------------------------------------------------------ */
#if 0 /* RLE_C_REFERENCE -- rebuild_info/code_layout.md */

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

/* 00057916.  Blit mode 0x0b, the tinting SPRITE blit: draws one sprite stream
   with every pixel it paints blended toward a single constant palette colour,
   and leaves the transparent runs alone.  Op for op it is
   fdps_rle_blit_tint_sprite_and_backdrop above with the fourth op reverted to
   the family's plain skip, ADD EDI,ECX at 00057a52, so the tint covers the
   sprite's own pixels and nothing else.

   The first three parameters are what they are for the other two kernels: the
   command stream, the first pixel of the top row, and the row advance the
   caller computes as pitch - width, which this kernel likewise publishes in
   data_fdps_graphics_rle_blit_dst_row_advance on entry and reads back from
   there at every row end.

   `blend_descriptor` is the same FOUR dwords mode 0x0a takes -- [0] the shade
   ramp base, [1] the blend level, [2] the inverse colour cube base and [3] the
   tint colour -- with the same row assignment: the tint carries weight
   level/16 through ramp row level and the pixel 16 - level through row
   level + 9, the two folded to 16 - level and swapped above 8.  Only the pixel
   term varies, so ramp[tint row][tint colour] is fetched once before any
   drawing.

   One call site in the whole image reaches this mode: fdps_draw_map_unit at
   0002d198, drawing a battle-map unit's 0x18 x 0x18 cell at level 8 with tint
   colour 0 when bit 0x80 of the unit's flag byte is set, which is the
   acted-this-turn flag -- so this is what makes a unit that has already moved
   look half-faded into palette colour 0.  The row width and row count come
   from the same two globals in gamedata.h. */
extern void fdps_rle_blit_tint(unsigned char *rle_stream,
                               unsigned char *dest_pixel,
                               int dest_row_advance,
                               int *blend_descriptor);
#pragma aux fdps_rle_blit_tint "*" parm caller [];

/* 00057a74.  Blit mode 0x0c, the colour-range translucent blit: op for op it is
   fdps_rle_blit_translucent above, with one test added per source pixel.  A
   pixel whose palette index falls inside the range the descriptor names is
   blended into the destination pixel underneath it exactly as mode 9 blends it;
   a pixel outside the range is stored opaque, so the sprite comes out
   see-through only in the colours the caller picked and solid everywhere else.

   The first three parameters are what they are for the other three kernels: the
   command stream, the first pixel of the top row, and the row advance the
   caller computes as pitch - width, which this kernel likewise publishes in
   data_fdps_graphics_rle_blit_dst_row_advance on entry and reads back from
   there at every row end.

   `blend_descriptor` is FIVE dwords: [0] the shade ramp base, [1] the blend
   level, [2] the inverse colour cube base, [3] the low palette index of the
   blended range and [4] the high one.  Only the low sixteen bits of the last
   two are read, and they are copied into the two globals declared at the top of
   this header before any drawing.  The level means what it means for mode 9 --
   0 = the source drawn opaque through 16 = the source invisible, the source
   weighted through ramp row level + 9 and the destination through row level for
   a level of 8 or less, the two folded to 16 - level and swapped above that.

   No call site in the image selects mode 0x0c: fdps_blit_dispatch is the only
   entry and no path in the program produces that mode value, so this kernel
   never runs in the shipped game and nothing observes the two range globals
   either.  The row width and row count still come from the same two globals in
   gamedata.h. */
extern void fdps_rle_blit_translucent_color_range(unsigned char *rle_stream,
                                                  unsigned char *dest_pixel,
                                                  int dest_row_advance,
                                                  int *blend_descriptor);
#pragma aux fdps_rle_blit_translucent_color_range "*" parm caller [];

#endif /* RLE_C_REFERENCE */

#endif
