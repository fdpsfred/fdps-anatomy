/* sprite.h -- the .SAF sprite drawers: one tilemap cell, one tilemap layer,
 * one composite sprite.
 *
 * A .SAF holds an animation's material in four sections -- frames, tilemaps,
 * tiles and sounds -- and drawing anything out of one is three nested walks:
 * a composite sprite is a list of parts, a part is a tilemap, and a tilemap is
 * a grid of cells each naming one tile.  Only the innermost step touches
 * pixels, and it does so by handing the tile's RLE stream to
 * fdps_blit_dispatch (blit.h), the same drawer .CEL sprites go through.
 * resource_info/saf.md is the canon for the container; saf.h holds the readers
 * that locate frames inside it.
 *
 * THE DRAW REQUEST.  The drawers in this file are handed one pointer to a
 * nine-dword block, 0x24 bytes, that the assembly addresses at +0x00 through
 * +0x20 off that pointer.  It is addressed here by element index, the way
 * saf.h addresses the playback cursor and for the same reason: the block is
 * the caller's, built on its own stack, and every field is a dword sitting at
 * its natural alignment.  Two of the slots hold pointers, which is what a
 * 32-bit flat model makes possible.
 *
 * Element 5 and element 6 are an image and an index into it, and which image
 * depends on which drawer is looking: the loaded .SAF and a tile index for
 * fdps_draw_tilemap_cell, the same .SAF and a tilemap index for the layer
 * walk above it, and the sprite bank and an entry index for
 * fdps_draw_composite_sprite.
 */
#ifndef SPRITE_H
#define SPRITE_H

#define DRAW_REQUEST_DEST_BASE 0     /* destination surface, byte pointer */
#define DRAW_REQUEST_DEST_PITCH 1    /* its pitch in bytes */
#define DRAW_REQUEST_DEST_ROWS 2     /* its height in rows */
#define DRAW_REQUEST_X 3             /* destination column of the left edge */
#define DRAW_REQUEST_Y 4             /* destination row of the top edge */
#define DRAW_REQUEST_IMAGE 5         /* image the index below indexes */
#define DRAW_REQUEST_ITEM_INDEX 6    /* item to draw out of that image */
#define DRAW_REQUEST_BLIT_OPERAND 7  /* fdps_blit_dispatch's mode operand */
#define DRAW_REQUEST_BLIT_MODE 8     /* its blit mode; only the low byte counts */
#define DRAW_REQUEST_DWORDS 9        /* the block is 0x24 bytes */

/* Draws one cell of a .SAF tilemap: blits the tile that element 6 of the
   request names into the destination at the request's x and y, or draws
   nothing at all.

   The tile's pixel stream is resolved as image + table[index], where table is
   the tile section's offset table at image + the u32 at +0x22 and index is
   scaled by four; stored offsets are file-relative, so both adds are onto the
   image base and never onto the table address.  The cell's width and height
   come from the u16 pair at +0x07 and +0x09 of the image (24x24 in every
   shipped .SAF) and are handed to fdps_blit_dispatch as the source rectangle,
   with the request's own pitch and its element 7 and element 8 for the mode.
   The destination pixel is element 0 plus x plus y times the pitch.

   THE PLACEMENT TEST IS STRICT ON ALL FOUR SIDES AND NOTHING IS CLIPPED.  The
   cell is drawn only when x > 0, y > 0, pitch - cell width > x and
   destination rows - cell height > y; a cell that fails any of them is
   dropped whole.  Writing the obvious bound instead -- x >= 0 and
   x + cell width <= pitch, and likewise down the page -- draws the outermost
   row and column of a tilemap that sits flush against the destination edge,
   where the original leaves the background showing
   (rebuild_info/pitfalls.md).

   The tile index is rejected when it is negative or not less than the tile
   section's item count, the u16 at +0x20 of the image; out of range means
   nothing is drawn, not that some other tile is.  No global is read or
   written here, and fdps_blit_dispatch is the only call. */
extern void fdps_draw_tilemap_cell(int *request);
#pragma aux fdps_draw_tilemap_cell "*" parm caller [];

/* Draws one tilemap of a .SAF: walks the grid of cells the tilemap that
   element 6 of the request names holds, and hands each cell to
   fdps_draw_tilemap_cell as a request of its own.

   The tilemap is resolved the same way a tile is one level down -- image plus
   the entry at index four of the offset table at image plus the u32 at +0x18,
   the tilemap section's start -- and the index is rejected when it is negative
   or not less than the u16 item count at +0x16.  Out of range means the whole
   layer is skipped.

   THE CELL DRAWER IS HANDED A COPY, NOT THIS REQUEST.  The 0x24 bytes are
   copied onto the stack once before the walk and the copy is what moves: its
   element 6 takes each cell's tile number, its x walks across the row and its
   y down the page.  The caller's own block is never written, which is what
   lets fdps_draw_composite_sprite keep its part's origin and index across the
   call.  x is reset at the top of every row from the CALLER's x rather than by
   subtracting the row's width back off, and y is never reset, so the grid
   lands with its top left corner at the request's x and y and every cell one
   cell width or height on from its neighbour.

   The grid's dimensions are the signed i16 pair at the front of the tilemap
   record, columns then rows, and the cells are the signed i16 that follow it
   in row-major order (resource_info/saf.md).  Signed is behaviour and not
   spelling: a negative column or row count draws nothing at all, where reading
   the same bytes unsigned would run the loop tens of thousands of times.  A
   cell whose tile number is out of range is dropped by the cell drawer, not
   here.

   The cell size read for the step is the u16 pair at +0x07 and +0x09, the same
   pair the cell drawer uses for its source rectangle, so the grid tiles
   without a gap or an overlap.  No global is read or written here. */
extern void fdps_draw_tilemap_layer(int *request);
#pragma aux fdps_draw_tilemap_layer "*" parm caller [];

/* Draws one frame of a .SAF: looks the frame that element 6 of the request
   names up in the image, hands each of its layers to fdps_draw_tilemap_layer
   at the request's origin plus the layer's own offset, and then, if
   `play_sound` is non-zero, plays the sound the frame names.  This is the
   entry point every animation in the game draws through -- eighteen callers,
   from the title screen to the combat blow.

   The frame is resolved the way a tilemap and a tile are one and two levels
   down -- image plus the entry at index four of the offset table at image plus
   the u32 at +0x0e, the frame section's start -- and the index is rejected
   when it is negative or not less than the u16 item count at +0x0c.  Out of
   range means nothing is drawn AND no sound is played: the whole body sits
   under that one test.

   EACH LAYER IS DRAWN THROUGH A COPY OF THE REQUEST, made once before the
   walk, and the layer's x and y are the CALLER's x and y plus the layer's own
   signed offsets every time -- not the previous layer's position advanced.
   The caller's block comes back untouched, which is what lets a caller draw
   frame after frame out of the same block.

   `play_sound` is a byte the assembly tests against zero and nothing else, so
   any non-zero value plays.  The sound number is the frame's leading i16 and
   is sign-extended, so the -1 a frame with no sound carries reaches
   fdps_sfx_play as -1 and is rejected there by its own lower-bound test
   (audio.h) rather than being suppressed here.

   THE BLEND SETUP IS SKIPPED WHOLE WHEN THE CALLER'S OWN BLIT MODE IS
   NON-ZERO.  Element 8 of the CALLER's block is re-read at every layer, and
   when it is anything but zero the copy keeps the caller's mode and operand
   and the layer's own blend fields are never looked at.  That is how a caller
   paints a whole frame in one mode of its choosing; it is not a fast path, and
   dropping the test would let a translucent layer override the caller.

   Only blend flags 0 and 1 are handled, and there is no else.  Flag 0 sets the
   copy's mode to 0 and leaves its operand alone; flag 1 builds the three-dword
   mode-9 descriptor (rleblend.h) out of data_fdps_palette_shade_ramp_table,
   16 minus the layer's blend level and data_fdps_inverse_palette_cube, and
   points the copy at it.  Any other flag value leaves the copy holding
   whatever the PREVIOUS layer set, so a layer with flag 2 inherits the
   layer before it.  No shipped .SAF stores anything but 0 or 1
   (resource_info/saf.md), so this is reachable only through a corrupt image --
   but it is the behaviour, and adding the else that shape asks for would
   change it.

   The blend level is inverted here and not in the kernel: the record's field
   is opacity 0..16 and the descriptor wants 0 = opaque, so 16 minus the field
   is what goes in.  Handing the field through unchanged inverts the fade
   (rleblend.h). */
extern void fdps_draw_composite_sprite(int *request, char play_sound);
#pragma aux fdps_draw_composite_sprite "*" parm caller [];

#endif
